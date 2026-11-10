#!/usr/bin/env python3
"""Source contracts for what a downloaded map, model, save or script may make the engine do.

Each check names the property a loader or handler has to keep: a count,
index or size taken from a file is held to the table it is used with before
anything is written through it. The properties were found missing in the
audit of 10 November 2026; the comment beside each says what went wrong.
"""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="latin-1").replace("\r\n", "\n")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"Unexpected {token!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    require(text, first, context)
    require(text, second, context)
    if text.index(first) >= text.index(second):
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    depth = 0
    quote = ""
    index = opening
    while index < len(source):
        char = source[index]
        if quote:
            # braces inside string and character literals do not nest
            if char == "\\":
                index += 1
            elif char == quote:
                quote = ""
        elif source.startswith("//", index):
            index = source.find("\n", index)
            if index < 0:
                break
        elif source.startswith("/*", index):
            index = source.find("*/", index) + 1
            if index <= 0:
                break
        elif char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
        index += 1
    raise AssertionError(f"Unbalanced body for {signature!r} in {context}")


def validate_animation_and_models() -> None:
    for tree in ("src/game", "src/mpgame"):
        # numAnimatedComponents * numFrames was taken in int and sized the frame table.
        anim = function_body(read(f"{tree}/anim/Anim.cpp"), "bool idMD5Anim::LoadAnim(", f"{tree} md5anim loader")
        require_before(
            anim,
            "static_cast<long long>( numAnimatedComponents ) * numFrames > 0x7fffffff",
            "componentFrames.SetNum( numAnimatedComponents * numFrames )",
            f"{tree} md5anim frame table size",
        )

    md5 = read("src/renderer/Model_md5.cpp")
    # Vertices share weights, so their total is not bounded by the weight count.
    mesh = function_body(md5, "void idMD5Mesh::ParseMesh(", "md5mesh parser")
    require_before(mesh, "numWeightsForVertex[i] > 0x7fffffff / 32 - numWeights", "numWeights += numWeightsForVertex[i];",
                   "md5mesh weight total")
    # numJoints sizes a stack allocation.
    require_before(md5, "if ( num < 0 || num > 4096 ) {", "poseMat3 = ( idJointMat * )_alloca16(", "md5mesh joint count")

    md3 = read("src/renderer/Model_md3.cpp")
    # Every offset and count was used as the file gave it.
    require_before(md3, "!MD3_FileIsConsistent( (const byte *)buffer, size )", "pinmodel = (md3Header_t *)buffer;",
                   "MD3 consistency check before any use")
    consistent = function_body(md3, "static bool MD3_FileIsConsistent(", "MD3 consistency check")
    for token in (
        "ofsEnd > size",
        "surfaceEnd > ofsEnd - surfaceOffset",
        "sizeof( md3Shader_t ), surfaceEnd",
        "sizeof( md3Triangle_t ), surfaceEnd",
        "sizeof( md3XyzNormal_t ), surfaceEnd",
        "index < 0 || index >= numVerts",
    ):
        require(consistent, token, "MD3 consistency check")

    lwo = read("src/renderer/Model_lwo.cpp")
    # Point and polygon numbers were counted, linked and written through unchecked.
    in_range = function_body(lwo, "static int lwIndicesInRange( const lwPointList *point, const lwPolygonList *polygon, const lwVMap *vmap )\n",
                             "LWO index check")
    require(in_range, "polygon->pol[ i ].v[ j ].index >= point->count", "LWO polygon point range")
    require(in_range, "vmap->pindex[ i ] >= polygon->count", "LWO vertex map polygon range")
    assert lwo.count("if ( !lwIndicesInRange( &layer->point, &layer->polygon, layer->vmap )) goto Fail") == 2, \
        "both LWO readers check indices before their finishing passes"
    for reader_fail in ("goto Fail;\n      lwGetBoundingBox", "goto Fail2;\n   lwGetBoundingBox"):
        require(lwo, reader_fail, "LWO index check precedes the bounding box and normal passes")
    tags = function_body(lwo, "int lwGetPolygonTags(", "LWO polygon tags")
    require_before(tags, "if ( i < 0 || i >= plist->count ) return 0;", "plist->pol[ i ].surfIndex = j;", "LWO tag polygon range")


def validate_images() -> None:
    # The JPEG source copied a full buffer on every refill, whatever was left.
    source = read("src/external/jpeg-6/jdatasrc.c")
    fill = function_body(source, "fill_input_buffer (j_decompress_ptr cinfo)", "JPEG refill")
    require(fill, "size_t count = (size_t) (src->infileEnd - src->infile);", "JPEG refill bound")
    require(fill, "src->buffer[1] = (JOCTET) JPEG_EOI;", "JPEG end-of-image at end of data")
    reject(fill, "memcpy( src->buffer, src->infile, INPUT_BUF_SIZE );", "unbounded JPEG refill")
    require(read("src/imagetools/Image_files.cpp"), "jpeg_stdio_src(&cinfo, fbuffer, fbufferLength);", "JPEG source length")

    # A failed inner image program leaves *pic NULL with a size still set.
    program = read("src/imagetools/Image_program.cpp")
    body = program[program.index('if (!token.Icmp("scale")) {') : program.index("// if we are just parsing instead of loading or checking,")]
    assert body.count("if (pic && *pic) {") == 5 and "if (pic) {" not in body, "image operations skip a failed inner program"

    # A NaN index failed both range tests of a clamped table.
    table = function_body(read("src/framework/DeclTable.cpp"), "float idDeclTable::TableLookup(", "table lookup")
    require(table, "} else if ( !( index > 0 ) ) {", "clamped table lookup with a NaN index")


def validate_maps() -> None:
    cm = read("src/cm/CollisionModel_files.cpp")
    edges = function_body(cm, "void idCollisionModelManagerLocal::ParseEdges(", "collision edges")
    require(edges, "model->edges[i].vertexNum[1] >= model->numVertices", "collision edge vertex range")
    polygons = function_body(cm, "void idCollisionModelManagerLocal::ParsePolygons(", "collision polygons")
    require_before(polygons, "if ( numEdges < 1 || numEdges > 65536 ) {", "p = AllocPolygon( model, numEdges );", "collision polygon edge count")
    require(polygons, "p->edges[i] <= -model->numEdges || p->edges[i] >= model->numEdges", "collision polygon edge range")
    brushes = function_body(cm, "void idCollisionModelManagerLocal::ParseBrushes(", "collision brushes")
    require_before(brushes, "if ( numPlanes < 1 || numPlanes > 65536 ) {", "b = AllocBrush( model, numPlanes );", "collision brush plane count")

    aas = read("src/aas/AASFile.cpp")
    boxes = function_body(aas, "bool idAASSettings::ParseBBoxes(", "AAS bounding boxes")
    require_before(boxes, "if ( numBoundingBoxes >= MAX_AAS_BOUNDING_BOXES ) {", "boundingBoxes[numBoundingBoxes++] = bounds;",
                   "AAS bounding box count")
    areas = function_body(aas, "bool idAASFileLocal::ParseAreas(", "AAS areas")
    require_before(areas, "reach->toAreaNum < 0 || reach->toAreaNum >= areas.Num()", "LinkReversedReachability();",
                   "AAS reachability target range")

    patch = function_body(read("src/idlib/mapfile.cpp"), "idMapPatch *idMapPatch::Parse(", "map patch")
    require_before(patch, "info[0] <= 1024.0f && info[1] >= 1.0f && info[1] <= 1024.0f", "new idMapPatch( info[0], info[1] )",
                   "map patch size")


def validate_scripts_and_saves() -> None:
    for tree in ("src/game", "src/mpgame"):
        thread = read(f"{tree}/script/Script_Thread.cpp")
        # A "ss" event passes C strings; this took idStr references.
        require(thread, "void idThread::Event_StrFind( const char *sourceStr, const char *subStr ) {", f"{tree} strFind signature")
        require(read(f"{tree}/script/Script_Thread.h"), "Event_StrFind( const char *sourceStr, const char *subStr );",
                f"{tree} strFind declaration")
        # Already formatted text was passed on as a format.
        require(thread, 'interpreter.Error( "%s", text );', f"{tree} script error text")
        require(thread, 'interpreter.Warning( "%s", text );', f"{tree} script warning text")
        reject(thread, "interpreter.Error( text );", f"{tree} script error as a format")

        # The float was tested for zero, the truncated integer divided.
        interpreter = read(f"{tree}/script/Script_Interpreter.cpp")
        for opcode, operand in (("case OP_MOD_F:", "*var_b.floatPtr"), ("case OP_UMOD_F:", "*var_a.floatPtr")):
            block = interpreter[interpreter.index(opcode) :]
            block = block[: block.index("break;")]
            require(block, f"const int divisor = static_cast<int>( {operand} );", f"{tree} {opcode} integer divisor")
            require(block, "if ( divisor == 0 ) {", f"{tree} {opcode} zero divisor")
            require(block, "} else if ( divisor == -1 ) {", f"{tree} {opcode} minus one divisor")

        blend = read(f"{tree}/anim/Anim_Blend.cpp")
        # Four frame commands parsed without their argument and played through NULL.
        commands = blend[blend.index('} else if ( token == "speak" )  {') : blend.index("// RAVEN END", blend.index('token == "attachment_show"'))]
        assert commands.count('return "Unexpected end of line";') == 4, f"{tree} frame commands require their argument"
        # Raven's joint helpers took INVALID_JOINT.
        for signature in (
            "void idAnimator::AimJointAt (",
            "void idAnimator::SetJointAngularVelocity (",
            "idAngles idAnimator::GetJointAngularVelocity (",
            "void idAnimator::ClearJointAngularVelocity (",
        ):
            helper = function_body(blend, signature, f"{tree} {signature}")
            require_before(helper, "( jointnum < 0 ) || ( jointnum >= numJoints )", "FindJointMod ( jointnum )",
                           f"{tree} {signature} joint range")

        # Save and Restore disagreed on the order of two fields.
        actor = read(f"{tree}/Actor.cpp")
        require_before(actor, "savefile->WriteInt( lightningNextTime );", "savefile->WriteInt( lightningEffects );", f"{tree} actor save order")
        require_before(actor, "savefile->ReadInt( lightningNextTime );", "savefile->ReadInt( lightningEffects );", f"{tree} actor restore order")

        local = read(f"{tree}/Game_local.cpp")
        require(local, "if ( localClientNum < -1 || localClientNum >= MAX_CLIENTS ) {", f"{tree} restored local client range")


def main() -> None:
    validate_animation_and_models()
    validate_images()
    validate_maps()
    validate_scripts_and_saves()
    print("untrusted_file_bounds: ok")


if __name__ == "__main__":
    main()

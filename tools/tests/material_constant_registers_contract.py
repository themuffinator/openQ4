#!/usr/bin/env python3
"""Pin the constant-register path for materials.

A material whose expressions never read the time, an entity or global parm, the
sound level or the live GLSL capability hands every draw surface the one
register block evaluated when it was parsed (idMaterial::ConstantRegisters),
so the surface does not allocate and evaluate its own every view.

The path was dead: the parse data is zeroed before each parse, and nothing set
its registersAreConstant flag, so no material was ever constant. This pins the
initialisation, the exact list of terms that clear the flag, the terms that are
folded to literals while parsing, and the consumers that must not read entity
parms out of a shared block. A new expression term, operation or predefined
register fails here until it is classified.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

# Terms whose value can change after the material is parsed. Each clears the flag.
PREDEFINED_TERMS = {
    "time": "EXP_REG_TIME",
    **{f"parm{i}": f"EXP_REG_PARM{i}" for i in range(12)},
    "DecalLife": "EXP_REG_PARM4",
    "DecalSpawn": "EXP_REG_PARM5",
    "VertexRandomizer": "EXP_REG_VERTEX_RANDOM",
    **{f"global{i}": f"EXP_REG_GLOBAL{i}" for i in range(8)},
}
RUNTIME_OP_TERMS = {
    "sound": "OP_TYPE_SOUND",
    "glslPrograms": "OP_TYPE_GLSL_ENABLED",
}
VARYING_TERMS = set(PREDEFINED_TERMS) | set(RUNTIME_OP_TERMS)

# Terms folded to a literal while parsing. A surface that evaluates its registers
# copies the same literal, so the constant block is exactly as fresh as an
# evaluation; a video mode or session change reaches neither without a reparse.
PARSE_TIME_TERMS = {
    "fragmentPrograms", "POTCorrectionX", "POTCorrectionY",
    "VideoWidth", "VideoHeight", "IsMultiplayer",
}

# Operations that only combine registers. The two above read state outside them.
REGISTER_ONLY_OPS = (
    "OP_TYPE_ADD", "OP_TYPE_SUBTRACT", "OP_TYPE_MULTIPLY", "OP_TYPE_DIVIDE", "OP_TYPE_MOD",
    "OP_TYPE_TABLE", "OP_TYPE_GT", "OP_TYPE_GE", "OP_TYPE_LT", "OP_TYPE_LE", "OP_TYPE_EQ",
    "OP_TYPE_NE", "OP_TYPE_AND", "OP_TYPE_OR",
)

# Renderer sources that name a predefined register. The fixtures build their
# own register blocks; ScenePackets reads entity parms only from an evaluated one.
PREDEFINED_REGISTER_SOURCES = {
    "src/renderer/Material.cpp",
    "src/renderer/Material.h",
    "src/renderer/ScenePackets.cpp",
    "src/renderer/GeometryResources.cpp",
    "src/renderer/MaterialResourceTable.cpp",
}


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def compact(value: str) -> str:
    return " ".join(value.split())


def require_order(haystack: str, needles: tuple[str, ...], context: str) -> None:
    compact_haystack = compact(haystack)
    previous = -1
    for needle in needles:
        position = compact_haystack.find(compact(needle), previous + 1)
        if position == -1:
            raise AssertionError(f"Missing {compact(needle)!r} (or out of order) in {context}")
        previous = position


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    depth = 0
    index = opening
    while index < len(source):
        char = source[index]
        if char in "\"'":
            # a brace inside a string or character literal is not a block
            index += 1
            while source[index] != char:
                index += 2 if source[index] == "\\" else 1
        elif source.startswith("//", index):
            index = source.find("\n", index)
        elif source.startswith("/*", index):
            index = source.find("*/", index) + 1
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
        index += 1
    raise AssertionError(f"Unterminated body for {signature!r} in {context}")


def enum_members(header: str, name: str) -> list[str]:
    match = re.search(r"typedef enum \{([^}]*)\} " + name + ";", header)
    if match is None:
        raise AssertionError(f"Missing enum {name} in Material.h")
    return [m for m in re.findall(r"\b([A-Z][A-Z0-9_]+)\b", match.group(1))]


def validate_initialisation(material: str) -> None:
    body = function_body(material, "void idMaterial::ParseMaterial( idLexer &src )", "Material.cpp")
    require_order(
        body,
        ("numStages = 0;", "pd->registersAreConstant = true;", "while ( 1 ) {"),
        "a material starts constant before its first token is read",
    )
    if material.count("registersAreConstant = true;") != 1:
        raise AssertionError("registersAreConstant may be set only where a parse starts")

    parse = function_body(material, "bool idMaterial::Parse( const char *text, const int textLength )", "Material.cpp")
    require_order(
        parse,
        (
            "memset( &parsingData, 0, sizeof( parsingData ) );",
            "pd = &parsingData;",
            "ParseMaterial( src );",
            "memcpy( expressionRegisters, pd->shaderRegisters,",
            "CheckForConstantRegisters();",
            "pd = NULL;",
        ),
        "the block is evaluated once the literals are stored and while the parse data is live",
    )

    check = function_body(material, "void idMaterial::CheckForConstantRegisters()", "Material.cpp")
    require_order(
        check,
        (
            "if ( !pd->registersAreConstant ) {",
            "return;",
            "constantRegisters = (float *)R_ClearedStaticAlloc( GetNumRegisters() * sizeof( float ) );",
            "memset( shaderParms, 0, sizeof( shaderParms ) );",
            "memset( &viewDef, 0, sizeof( viewDef ) );",
            "EvaluateRegisters( constantRegisters, shaderParms, &viewDef, 0 );",
        ),
        "a constant material's registers are evaluated once, with no view and no entity",
    )

    require_order(
        function_body(material, "void idMaterial::CommonInit()", "Material.cpp"),
        ("constantRegisters = NULL;",),
        "a reparse starts without a block",
    )
    require_order(
        function_body(material, "void idMaterial::FreeData()", "Material.cpp"),
        ("R_StaticFree( constantRegisters );", "constantRegisters = NULL;"),
        "the block is freed with the stages it describes",
    )


def validate_terms(material: str, header: str) -> None:
    body = function_body(material, "int idMaterial::ParseTerm( idLexer &src )", "Material.cpp")
    blocks = re.findall(r'if \( !token\.Icmp\( "(\w+)" \) \) \{(.*?)\n\t\}', body, re.S)
    names = [name for name, _ in blocks]
    if len(names) != len(set(names)):
        raise AssertionError("ParseTerm names a term twice")

    clearing = {name for name, block in blocks if "pd->registersAreConstant = false;" in block}
    if clearing != VARYING_TERMS:
        raise AssertionError(
            "terms that clear registersAreConstant changed: "
            f"unexpected {sorted(clearing - VARYING_TERMS)}, missing {sorted(VARYING_TERMS - clearing)}"
        )
    folded = set(names) - clearing
    if folded != PARSE_TIME_TERMS:
        raise AssertionError(
            "terms that keep a material constant changed: "
            f"unexpected {sorted(folded - PARSE_TIME_TERMS)}, missing {sorted(PARSE_TIME_TERMS - folded)}; "
            "a new term must clear registersAreConstant unless it is folded to a literal while parsing"
        )

    for name, block in blocks:
        returns = re.findall(r"return ([^;]+);", block)
        if len(returns) != 1:
            raise AssertionError(f"term {name!r} must have one result")
        if name in PREDEFINED_TERMS:
            if returns[0] != PREDEFINED_TERMS[name]:
                raise AssertionError(f"term {name!r} must read {PREDEFINED_TERMS[name]}, found {returns[0]}")
        elif name in RUNTIME_OP_TERMS:
            if returns[0] != f"EmitOp( 0, 0, {RUNTIME_OP_TERMS[name]} )":
                raise AssertionError(f"term {name!r} must emit {RUNTIME_OP_TERMS[name]}")
        elif not returns[0].startswith("GetExpressionConstant( "):
            raise AssertionError(f"term {name!r} keeps a material constant, so it has to fold to a literal")
        if name in VARYING_TERMS and block.index("pd->registersAreConstant = false;") > block.index("return "):
            raise AssertionError(f"term {name!r} must clear the flag before it returns")

    # every predefined register is reachable only through a term that clears the flag
    predefined = [m for m in enum_members(header, "expRegister_t") if m != "EXP_REG_NUM_PREDEFINED"]
    if set(predefined) != set(PREDEFINED_TERMS.values()):
        raise AssertionError(
            "predefined registers changed: "
            f"{sorted(set(predefined) ^ set(PREDEFINED_TERMS.values()))} need a term that clears the flag"
        )

    stage = function_body(material, "void idMaterial::ParseStage(", "Material.cpp")
    colored = re.search(r'if \( !token\.Icmp\( "colored" \) \) \{(.*?)\n\t\t\}', stage, re.S)
    if colored is None:
        raise AssertionError("missing the colored stage shorthand")
    require_order(
        colored.group(1),
        (
            "ss->color.registers[0] = EXP_REG_PARM0;",
            "ss->color.registers[1] = EXP_REG_PARM1;",
            "ss->color.registers[2] = EXP_REG_PARM2;",
            "ss->color.registers[3] = EXP_REG_PARM3;",
            "pd->registersAreConstant = false;",
        ),
        "the colored shorthand reads the entity colour without an expression",
    )

    # nothing else in the parser may hand a stage a predefined register
    remainder = material
    for signature in (
        "int idMaterial::ParseTerm( idLexer &src )",
        "void idMaterial::EvaluateRegisters(",
        "void idMaterial::EvaluateStageRegisters(",
        "bool idMaterial::GetConstantRegisterValue(",
        "static bool R_MaterialConstantRegistersSelfTest( void )",
        "bool R_PBRMaterialParserSelfTest( void )",
    ):
        remainder = remainder.replace(function_body(material, signature, "Material.cpp"), "")
    remainder = remainder.replace(colored.group(1), "")
    stray = sorted(set(re.findall(r"\bEXP_REG_(?!NUM_PREDEFINED\b)\w+", remainder)))
    if stray:
        raise AssertionError(f"predefined registers used outside the classified terms: {stray}")


def validate_operations(material: str, header: str) -> None:
    ops = enum_members(header, "expOpType_t")
    expected = list(REGISTER_ONLY_OPS) + list(RUNTIME_OP_TERMS.values())
    if ops != expected:
        raise AssertionError(
            f"expression operations changed ({sorted(set(ops) ^ set(expected))}); an operation that reads "
            "anything but its operand registers needs a term that clears registersAreConstant"
        )
    for op in RUNTIME_OP_TERMS.values():
        emitted = re.findall(r"EmitOp\([^;]*\b" + op + r"\b", material)
        if len(emitted) != 1:
            raise AssertionError(f"{op} must be emitted by exactly one term")

    evaluate = function_body(material, "void idMaterial::EvaluateRegisters(", "Material.cpp")
    cases = re.split(r"\n\t\tcase ", evaluate)
    if sorted(case.split(":", 1)[0] for case in cases[1:]) != sorted(expected):
        raise AssertionError("EvaluateRegisters must handle each expression operation exactly once")
    for case in cases[1:]:
        op = case.split(":", 1)[0]
        text = case.split("\n\t\tdefault:", 1)[0]
        reads_outside = "soundEmitter" in text or "R_MaterialGLSLProgramsAvailable" in text
        if op in REGISTER_ONLY_OPS:
            if reads_outside or "view->" in text or "shaderParms" in text:
                raise AssertionError(f"{op} must read only its operand registers")
        elif op in RUNTIME_OP_TERMS.values():
            if not reads_outside:
                raise AssertionError(f"{op} no longer reads run-time state; reclassify it")
        else:
            raise AssertionError(f"unclassified operation {op} in EvaluateRegisters")
    # the view and the entity reach the registers only through the predefined ones
    for line in cases[0].splitlines():
        if ("view->" in line or "shaderParms[" in line) and not re.match(r"\s*registers\[EXP_REG_\w+\] = ", line):
            raise AssertionError(f"EvaluateRegisters reads the view or the entity outside a predefined register: {line.strip()}")
    if evaluate.count("view->") != 9 or len(re.findall(r"= shaderParms\[", evaluate)) != 13:
        raise AssertionError("EvaluateRegisters seeds the time, eight globals, twelve parms and the vertex randomizer")


def validate_switch_and_consumers(material: str) -> None:
    require_order(
        function_body(material, "const float *idMaterial::ConstantRegisters() const", "Material.cpp"),
        ("if ( !r_useConstantMaterials.GetBool() ) {", "return NULL;", "return constantRegisters;"),
        "r_useConstantMaterials 0 withholds every block, so each surface evaluates as before",
    )
    require_order(
        read("src/renderer/RenderSystem_init.cpp"),
        ('idCVar r_useConstantMaterials( "r_useConstantMaterials", "1", CVAR_RENDERER | CVAR_BOOL,',),
        "the path is on by default and is not archived",
    )

    setup = function_body(read("src/renderer/tr_light.cpp"), "const float *R_SetupDrawSurfShaderRegisters(", "tr_light.cpp")
    require_order(
        setup,
        (
            "const float *constRegs = shader->ConstantRegisters();",
            "if ( constRegs != NULL ) {",
            "return constRegs;",
            "float *regs = (float *)R_FrameAlloc( shader->GetNumRegisters() * sizeof( float ) );",
            "shader->EvaluateRegisters( regs, shaderParms, tr.viewDef, soundEmitter );",
        ),
        "a constant material skips the per-view allocation and evaluation",
    )

    # the shared block holds no entity parms: its predefined registers are zero
    packets = function_body(read("src/renderer/ScenePackets.cpp"), "int idScenePacketFrame::FindOrAddInstanceRecord(", "ScenePackets.cpp")
    require_order(
        packets,
        (
            "const bool sharedConstantRegisters = shaderRegisters != NULL && drawSurf->material != NULL",
            "&& shaderRegisters == drawSurf->material->ConstantRegisters();",
            "if ( shaderRegisters != NULL && !sharedConstantRegisters ) {",
            "record.entityColor[0] = shaderRegisters[EXP_REG_PARM0];",
            "} else if ( space->entityDef != NULL ) {",
            "record.entityColor[0] = space->entityDef->parms.shaderParms[SHADERPARM_RED];",
        ),
        "an instance record reads the entity colour from the entity when the registers are the shared block",
    )

    found = set()
    for path in sorted((ROOT / "src/renderer").rglob("*")):
        if path.suffix not in (".cpp", ".h", ".inl", ".c"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if re.search(r"\bEXP_REG_(?!NUM_PREDEFINED\b)\w+", text):
            found.add(path.relative_to(ROOT).as_posix())
    if found != PREDEFINED_REGISTER_SOURCES:
        raise AssertionError(
            "renderer sources naming a predefined register changed "
            f"({sorted(found ^ PREDEFINED_REGISTER_SOURCES)}); a reader of entity or view registers "
            "must not be handed a constant block"
        )


def validate_runtime_self_test(material: str) -> None:
    body = function_body(material, "static bool R_MaterialConstantRegistersSelfTest( void )", "Material.cpp")
    for term in sorted(VARYING_TERMS):
        if f'"rgb {term}"' not in body:
            raise AssertionError(f"the runtime self-test does not parse a material that reads {term!r}")
    if '"colored"' not in body:
        raise AssertionError("the runtime self-test does not parse the colored shorthand")
    for term in sorted(PARSE_TIME_TERMS):
        if f'"rgb {term}"' not in body:
            raise AssertionError(f"the runtime self-test does not parse a material that reads {term!r}")
    require_order(
        body,
        (
            "r_useConstantMaterials.SetBool( true );",
            "material->ConstantRegisters() != NULL",
            "const float *constants = material->ConstantRegisters();",
            "material->EvaluateRegisters( evaluated, entityParms, &evaluationView );",
            "memcmp( constants + EXP_REG_NUM_PREDEFINED, evaluated + EXP_REG_NUM_PREDEFINED,",
            "r_useConstantMaterials.SetBool( false );",
            "material->ConstantRegisters() != NULL",
            "r_useConstantMaterials.SetBool( savedConstantMaterials );",
        ),
        "the self-test proves both lists, the block's value and the switch",
    )
    require_order(
        function_body(material, "bool R_PBRMaterialParserSelfTest( void )", "Material.cpp"),
        ("mixed->ConstantRegisters() == NULL", "if ( !R_MaterialConstantRegistersSelfTest() ) {", "return false;"),
        "rendererPBRMaterialSelfTest runs the constant-register self-test",
    )


def main() -> None:
    material = read("src/renderer/Material.cpp")
    header = read("src/renderer/Material.h")
    validate_initialisation(material)
    validate_terms(material, header)
    validate_operations(material, header)
    validate_switch_and_consumers(material)
    validate_runtime_self_test(material)

    for registry in ("tools/validation/openq4_validate.py", ".github/workflows/commit-validation.yml",
                     ".github/workflows/push-verification.yml"):
        if "material_constant_registers_contract.py" not in read(registry):
            raise AssertionError(f"{registry} does not run the constant-register contract")

    print("material_constant_registers_contract: ok")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Guard generic engine code against LP64 and AArch64 portability regressions."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class CppToken:
    value: str
    line: int


def cpp_tokens(source: str) -> list[CppToken]:
    """Return code tokens while ignoring comments and quoted text."""
    tokens: list[CppToken] = []
    line = 1
    index = 0
    operators = ("->", "++", "--", "+=", "-=", "==", "!=", "&&", "||", "::")

    while index < len(source):
        character = source[index]
        if character.isspace():
            line += character == "\n"
            index += 1
            continue
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            index = len(source) if end < 0 else end
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            end = len(source) if end < 0 else end + 2
            line += source.count("\n", index, end)
            index = end
            continue
        if character in ('"', "'"):
            quote = character
            index += 1
            while index < len(source):
                if source[index] == "\\":
                    index += 2
                    continue
                line += source[index] == "\n"
                if source[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if character.isalpha() or character == "_":
            end = index + 1
            while end < len(source) and (source[end].isalnum() or source[end] == "_"):
                end += 1
            tokens.append(CppToken(source[index:end], line))
            index = end
            continue
        if character.isdigit():
            end = index + 1
            while end < len(source) and (source[end].isalnum() or source[end] in "._"):
                end += 1
            tokens.append(CppToken(source[index:end], line))
            index = end
            continue

        operator = next((candidate for candidate in operators if source.startswith(candidate, index)), None)
        if operator is not None:
            tokens.append(CppToken(operator, line))
            index += len(operator)
            continue
        tokens.append(CppToken(character, line))
        index += 1

    return tokens


def token_pairs(tokens: list[CppToken]) -> tuple[dict[int, int], dict[int, int]]:
    opening_for = {")": "(", "]": "[", "}": "{"}
    stacks: dict[str, list[int]] = {"(": [], "[": [], "{": []}
    pairs: dict[int, int] = {}
    reverse_pairs: dict[int, int] = {}
    for index, token in enumerate(tokens):
        if token.value in stacks:
            stacks[token.value].append(index)
        elif token.value in opening_for:
            opening = opening_for[token.value]
            if not stacks[opening]:
                continue
            start = stacks[opening].pop()
            pairs[start] = index
            reverse_pairs[index] = start
    return pairs, reverse_pairs


def is_identifier_token(token: CppToken) -> bool:
    return token.value[0].isalpha() or token.value[0] == "_"


def looks_like_c_style_cast(tokens: list[CppToken], start: int, end: int) -> bool:
    if start >= end:
        return False
    cast_punctuation = {"*", "&", "::", "<", ">", ",", "[", "]"}
    return all(is_identifier_token(token) or token.value in cast_punctuation for token in tokens[start:end])


def position_derived_expression_span(
    tokens: list[CppToken],
    pairs: dict[int, int],
    reverse_pairs: dict[int, int],
    start: int,
    end: int,
) -> tuple[int, int]:
    """Include casts/grouping that preserve a Position-derived pointer value."""
    while True:
        changed = False
        if start > 0 and tokens[start - 1].value == ")":
            cast_start = reverse_pairs.get(start - 1)
            if cast_start is not None and looks_like_c_style_cast(tokens, cast_start + 1, start - 1):
                start = cast_start
                changed = True

        if (
            start > 0
            and end + 1 < len(tokens)
            and tokens[start - 1].value == "("
            and pairs.get(start - 1) == end + 1
        ):
            before_group = tokens[start - 2] if start > 1 else None
            if before_group is None or (
                not is_identifier_token(before_group) and before_group.value not in (")", "]")
            ):
                start -= 1
                end += 1
                changed = True

        if not changed:
            return start, end


def is_position_derived_boolean_cast(
    tokens: list[CppToken], pairs: dict[int, int], start: int, end: int
) -> bool:
    # Functional cast: bool( positionDerivedValue )
    if (
        start > 1
        and end + 1 < len(tokens)
        and tokens[start - 1].value == "("
        and pairs.get(start - 1) == end + 1
        and tokens[start - 2].value == "bool"
    ):
        return True

    # Named cast: static_cast<bool>( positionDerivedValue ). The expression
    # span has already absorbed the argument parentheses at this point.
    return (
        start >= 4
        and [token.value for token in tokens[start - 4 : start]]
        == ["static_cast", "<", "bool", ">"]
    )


def containing_function_end(
    tokens: list[CppToken], pairs: dict[int, int], reverse_pairs: dict[int, int], position: int
) -> int:
    containing_braces = [
        start for start, end in pairs.items() if tokens[start].value == "{" and start < position < end
    ]
    control_words = {"if", "for", "while", "switch", "catch"}
    for brace in reversed(containing_braces):
        previous = brace - 1
        if previous >= 0 and tokens[previous].value == ")":
            parameters = reverse_pairs.get(previous)
            if parameters is not None and parameters > 0 and tokens[parameters - 1].value not in control_words:
                return pairs[brace]
    if containing_braces:
        return pairs[containing_braces[0]]
    return len(tokens)


def position_assignment_alias(tokens: list[CppToken], position: int) -> str | None:
    statement_start = position - 1
    while statement_start >= 0 and tokens[statement_start].value not in (";", "{", "}"):
        statement_start -= 1
    assignment = next(
        (index for index in range(position - 1, statement_start, -1) if tokens[index].value == "="),
        None,
    )
    if assignment is None or any(token.value == "," for token in tokens[assignment + 1 : position]):
        return None
    for token in reversed(tokens[statement_start + 1 : assignment]):
        if token.value[0].isalpha() or token.value[0] == "_":
            return token.value
    return None


def reject_unsafe_position_alias(
    path: Path,
    tokens: list[CppToken],
    pairs: dict[int, int],
    reverse_pairs: dict[int, int],
    alias: str,
    start: int,
    end: int,
    visited: set[tuple[str, int]] | None = None,
) -> None:
    if visited is None:
        visited = set()
    origin = (alias, start)
    if origin in visited:
        return
    visited.add(origin)

    null_values = {"0", "NULL", "nullptr"}
    arithmetic = {"+", "-", "+=", "-=", "++", "--"}
    index = start
    while index < end:
        if tokens[index].value != alias:
            index += 1
            continue

        immediate_following = tokens[index + 1].value if index + 1 < len(tokens) else ""

        # A later assignment ends this alias's Position-derived lifetime. This
        # also prevents unrelated locals with a common name such as `ac` from
        # being diagnosed elsewhere in the same function.
        if immediate_following == "=":
            return

        span_start, span_end = position_derived_expression_span(
            tokens, pairs, reverse_pairs, index, index
        )
        previous = tokens[span_start - 1].value if span_start > 0 else ""
        previous_previous = tokens[span_start - 2].value if span_start > 1 else ""
        following = tokens[span_end + 1].value if span_end + 1 < len(tokens) else ""
        following_following = tokens[span_end + 2].value if span_end + 2 < len(tokens) else ""
        direct_boolean_context = (
            span_start > 1
            and span_end + 1 < len(tokens)
            and tokens[span_start - 1].value == "("
            and pairs.get(span_start - 1) == span_end + 1
            and tokens[span_start - 2].value in {"if", "while", "switch"}
        )
        boolean_cast = is_position_derived_boolean_cast(tokens, pairs, span_start, span_end)

        reason = None
        if following in {"->", "["}:
            reason = "typed member/index access"
        elif following in arithmetic or previous in arithmetic:
            reason = "pointer arithmetic"
        elif previous == "*":
            before_dereference = previous_previous
            if before_dereference == "(" and span_start > 2:
                before_dereference = tokens[span_start - 3].value
            if before_dereference not in {"sizeof", "decltype"}:
                reason = "pointer dereference"
        elif previous == "!":
            reason = "zero-offset null test"
        elif boolean_cast or direct_boolean_context or following in {"?", "&&", "||"} or previous in {"&&", "||"}:
            reason = "zero-offset boolean test"
        elif following in {"==", "!="} and following_following in null_values:
            reason = "zero-offset null comparison"
        elif previous in {"==", "!="} and previous_previous in null_values:
            reason = "zero-offset null comparison"

        if reason is not None:
            raise AssertionError(
                f"Unsafe vertexCache.Position()-derived alias {alias!r} ({reason}) "
                f"in {path.relative_to(ROOT)}:{tokens[index].line}"
            )

        copied_alias = position_assignment_alias(tokens, index)
        if copied_alias is not None and copied_alias != alias:
            reject_unsafe_position_alias(
                path, tokens, pairs, reverse_pairs, copied_alias, index + 1, end, visited
            )
        index += 1


def audit_renderer_source(path: Path, source: str) -> int:
    """Audit one renderer translation unit and return its Position() count."""
    tokens = cpp_tokens(source)
    pairs, reverse_pairs = token_pairs(tokens)
    positions = [
        index
        for index in range(len(tokens) - 3)
        if [token.value for token in tokens[index : index + 4]] == ["vertexCache", ".", "Position", "("]
    ]
    for position in positions:
        call_open = position + 3
        call_end = pairs.get(call_open)
        if call_end is None:
            raise AssertionError(
                f"Unbalanced vertexCache.Position() call in {path.relative_to(ROOT)}:{tokens[position].line}"
            )

        span_start, span_end = position_derived_expression_span(
            tokens, pairs, reverse_pairs, position, call_end
        )
        previous = tokens[span_start - 1].value if span_start > 0 else ""
        previous_previous = tokens[span_start - 2].value if span_start > 1 else ""
        following = tokens[span_end + 1].value if span_end + 1 < len(tokens) else ""
        following_following = tokens[span_end + 2].value if span_end + 2 < len(tokens) else ""
        direct_boolean_context = (
            span_start > 1
            and span_end + 1 < len(tokens)
            and tokens[span_start - 1].value == "("
            and pairs.get(span_start - 1) == span_end + 1
            and tokens[span_start - 2].value in {"if", "while", "switch"}
        )
        boolean_cast = is_position_derived_boolean_cast(tokens, pairs, span_start, span_end)
        if previous in {"!", "+", "-", "++", "--", "*"} or following in {
            "->", "[", "+", "-", "++", "--", "+=", "-=", "?", "&&", "||",
        } or previous in {"&&", "||"} or direct_boolean_context or boolean_cast or (
            following in {"==", "!="} and following_following in {"0", "NULL", "nullptr"}
        ) or (
            previous in {"==", "!="} and previous_previous in {"0", "NULL", "nullptr"}
        ):
            raise AssertionError(
                f"Unsafe direct vertexCache.Position() pointer use in "
                f"{path.relative_to(ROOT)}:{tokens[position].line}"
            )

        alias = position_assignment_alias(tokens, position)
        if alias is not None:
            reject_unsafe_position_alias(
                path,
                tokens,
                pairs,
                reverse_pairs,
                alias,
                call_end + 1,
                containing_function_end(tokens, pairs, reverse_pairs, position),
            )

    # This helper intentionally returns the encoded VBO offset through an
    # output reference. Treat every caller's second argument as Position-
    # derived so new caller-side member arithmetic cannot bypass the audit.
    helper_name = "RB_GLSLPrepareInteractionVertexCache"
    for index in range(len(tokens) - 1):
        if tokens[index].value != helper_name or tokens[index + 1].value != "(":
            continue
        call_end = pairs.get(index + 1)
        if call_end is None or (call_end + 1 < len(tokens) and tokens[call_end + 1].value == "{"):
            continue
        depth = 0
        comma = None
        for argument_index in range(index + 2, call_end):
            value = tokens[argument_index].value
            if value in ("(", "[", "{"):
                depth += 1
            elif value in (")", "]", "}"):
                depth -= 1
            elif value == "," and depth == 0:
                comma = argument_index
                break
        if comma is None:
            raise AssertionError(
                f"Cannot inventory {helper_name} output argument in "
                f"{path.relative_to(ROOT)}:{tokens[index].line}"
            )
        output_identifiers = [
            token.value
            for token in tokens[comma + 1 : call_end]
            if token.value[0].isalpha() or token.value[0] == "_"
        ]
        if not output_identifiers:
            raise AssertionError(
                f"Cannot identify {helper_name} output alias in "
                f"{path.relative_to(ROOT)}:{tokens[index].line}"
            )
        reject_unsafe_position_alias(
            path,
            tokens,
            pairs,
            reverse_pairs,
            output_identifiers[-1],
            call_end + 1,
            containing_function_end(tokens, pairs, reverse_pairs, index),
        )

    return len(positions)


def audit_renderer_vertex_cache_consumers() -> dict[str, int]:
    """Inventory every renderer Position() call and reject zero-offset UB."""
    inventory: dict[str, int] = {}
    renderer_root = ROOT / "src/renderer"
    renderer_extensions = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
    renderer_sources = sorted(
        path
        for path in renderer_root.rglob("*")
        if path.is_file() and path.suffix.lower() in renderer_extensions
    )
    for path in renderer_sources:
        source = path.read_text(encoding="utf-8", errors="surrogateescape")
        count = audit_renderer_source(path, source)
        if count:
            inventory[str(path.relative_to(ROOT))] = count

    if not inventory:
        raise AssertionError("No renderer vertexCache.Position() consumers were inventoried")
    return inventory


def self_check_renderer_vertex_cache_audit() -> None:
    """Prove the lightweight analyzer recognizes its critical safe/unsafe forms."""
    synthetic_path = ROOT / "src/renderer/__position_audit_self_check.cpp"
    unsafe_sources = {
        "renamed member": "void f() { idDrawVert *verts = (idDrawVert *)vertexCache.Position( h ); verts->xyz; }",
        "copied alias": "void f() { idDrawVert *verts = (idDrawVert *)vertexCache.Position( h ); idDrawVert *copy = verts; &copy[0].st; }",
        "negated alias": "void f() { void *offset = vertexCache.Position( h ); if ( !offset ) {} }",
        "alias nullptr": "void f() { void *offset = vertexCache.Position( h ); if ( offset == nullptr ) {} }",
        "reversed alias NULL": "void f() { void *offset = vertexCache.Position( h ); if ( NULL != offset ) {} }",
        "direct zero": "void f() { if ( vertexCache.Position( h ) == 0 ) {} }",
        "reversed direct nullptr": "void f() { if ( nullptr != vertexCache.Position( h ) ) {} }",
        "pointer arithmetic": "void f() { idDrawVert *verts = (idDrawVert *)vertexCache.Position( h ); use( verts + n ); }",
        "output reference": "void f() { idDrawVert *verts = NULL; if ( RB_GLSLPrepareInteractionVertexCache( surf, verts ) ) { verts->xyz; } }",
        "cast-wrapped direct member": "void f() { ((idDrawVert *)vertexCache.Position( h ))->normal; }",
        "cast-wrapped alias member": "void f() { void *v = vertexCache.Position( h ); ((idDrawVert *)v)->normal; }",
        "positive boolean alias": "void f() { void *v = vertexCache.Position( h ); if ( v ) {} }",
        "parenthesized alias nullptr": "void f() { void *v = vertexCache.Position( h ); if ( ( v ) == nullptr ) {} }",
        "named boolean alias cast": "void f() { void *v = vertexCache.Position( h ); if ( static_cast<bool>( v ) ) {} }",
        "functional boolean alias cast": "void f() { void *v = vertexCache.Position( h ); if ( bool( v ) ) {} }",
    }
    for context, source in unsafe_sources.items():
        try:
            audit_renderer_source(synthetic_path, source)
        except AssertionError:
            continue
        raise AssertionError(f"Renderer Position() analyzer accepted unsafe synthetic case: {context}")

    safe_source = """
        // A comment mentioning vertexCache.Position( is not a consumer.
        void f() {
            void *offset = vertexCache.Position( h );
            glVertexPointer( 3, GL_FLOAT, stride, offset );
            glTexCoordPointer( 2, GL_FLOAT, stride, vertexCache.Position( h ) );
            glNormalPointer( GL_FLOAT, stride, RB_DrawVertAttributePointer( offset, normalOffset ) );
        }
    """
    if audit_renderer_source(synthetic_path, safe_source) != 2:
        raise AssertionError("Renderer Position() analyzer failed its legal offset-zero self-check")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="surrogateescape")


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(source: str, needle: str, context: str) -> None:
    if needle in source:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def audit_tool_size_formats(sources: dict[str, str]) -> None:
    """Reject obvious size_t values passed to legacy 32-bit integer formats."""
    call_pattern = re.compile(
        r"(?s)\b(?:common->(?:Printf|Warning|Error)|Sys_Status|WriteFileString|"
        r"MemFile_fprintf|(?:idStr::)?snPrintf|sprintf|fprintf|printf)"
        r"\s*\(\s*\"(?P<format>(?:\\.|[^\"])*)\"\s*,(?P<args>.*?)(?:\);)"
    )
    narrow_format = re.compile(r"%(?!%)[-+ #0-9.*]*[diuoxX]")
    size_format = re.compile(r"%(?!%)[-+ #0-9.*]*z[diuoxX]")
    size_value = re.compile(r"\b(?:strlen\s*\([^)]*\)|sizeof\s*(?:\([^)]*\)|\w+))")
    guarded_cast = re.compile(
        r"(?:static_cast\s*<\s*(?:unsigned\s+)?int\s*>\s*\(\s*|"
        r"\(\s*(?:unsigned\s+)?int\s*\)\s*)$"
    )

    for path, source in sources.items():
        for call in call_pattern.finditer(source):
            format_string = call.group("format")
            if not narrow_format.search(format_string) or size_format.search(format_string):
                continue
            arguments = call.group("args")
            for value in size_value.finditer(arguments):
                prefix = arguments[max(0, value.start() - 64):value.start()]
                if guarded_cast.search(prefix):
                    continue
                line = source.count("\n", 0, call.start()) + 1
                raise AssertionError(
                    f"Potential size_t value passed to a 32-bit format in {path}:{line}"
                )


def main() -> None:
    meson = read("meson.build")
    md4 = read("src/idlib/hashing/MD4.cpp")
    md4_h = read("src/idlib/hashing/MD4.h")
    md5 = read("src/idlib/hashing/MD5.cpp")
    md5_h = read("src/idlib/hashing/MD5.h")
    crc32 = read("src/idlib/hashing/CRC32.cpp")
    crc32_h = read("src/idlib/hashing/CRC32.h")
    crc16 = read("src/idlib/hashing/CRC16.cpp")
    crc16_h = read("src/idlib/hashing/CRC16.h")
    crc8 = read("src/idlib/hashing/CRC8.cpp")
    crc8_h = read("src/idlib/hashing/CRC8.h")
    honeyman = read("src/idlib/hashing/Honeyman.cpp")
    honeyman_h = read("src/idlib/hashing/Honeyman.h")
    math = read("src/idlib/math/Math.h")
    str_cpp = read("src/idlib/Str.cpp")
    trace_model = read("src/idlib/geometry/TraceModel.cpp")
    random = read("src/idlib/math/Random.h")
    simd = read("src/idlib/math/Simd.cpp")
    simd_sse2 = read("src/idlib/math/Simd_SSE2.h")
    simd_generic = read("src/idlib/math/Simd_generic.cpp")
    vector = read("src/idlib/math/Vector.h")
    vector_set = read("src/idlib/containers/VectorSet.h")
    token = read("src/idlib/Token.h")
    lexer = read("src/idlib/Lexer.cpp")
    parser = read("src/idlib/Parser.cpp")
    bit_msg = read("src/idlib/BitMsg.cpp")
    common_cpp = read("src/framework/Common.cpp")
    console_cpp = read("src/framework/Console.cpp")
    file_system = read("src/framework/FileSystem.cpp")
    file_system_h = read("src/framework/FileSystem.h")
    file_cpp = read("src/framework/File.cpp")
    file_h = read("src/framework/File.h")
    unzip = read("src/framework/Unzip.cpp")
    session = read("src/framework/Session.cpp")
    async_client = read("src/framework/async/AsyncClient.cpp")
    async_server = read("src/framework/async/AsyncServer.cpp")
    decl_mat_type = read("src/framework/declMatType.h")
    decl_manager = read("src/framework/DeclManager.cpp")
    collision_local = read("src/cm/CollisionModel_local.h")
    collision_model = read("src/cm/CollisionModel.cpp")
    collision_load = read("src/cm/CollisionModel_load.cpp")
    collision_contents = read("src/cm/CollisionModel_contents.cpp")
    collision_translate = read("src/cm/CollisionModel_translate.cpp")
    compressor = read("src/framework/Compressor.cpp")
    msg_channel = read("src/framework/async/MsgChannel.cpp")
    renderer_light = read("src/renderer/tr_light.cpp")
    renderer_local = read("src/renderer/tr_local.h")
    renderer_vertex_cache = read("src/renderer/VertexCache.h")
    renderer_vertex_cache_cpp = read("src/renderer/VertexCache.cpp")
    renderer_vk_gui = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    renderer_draw_arb2 = read("src/renderer/draw_arb2.cpp")
    renderer_main = read("src/renderer/tr_main.cpp")
    renderer_trisurf = read("src/rendergeo/RenderGeometryTriSurf.cpp")
    render_world = read("src/renderer/RenderWorld.cpp")
    image_files = read("src/imagetools/Image_files.cpp")
    image_tools_h = read("src/imagetools/ImageTools.h")
    image_tools_state = read("src/imagetools/ImageToolsState.cpp")
    bse_manager = read("src/bse/BSE_Manager.cpp")
    model_h = read("src/renderer/Model.h")
    model_cpp = read("src/renderer/Model.cpp")
    model_ma = read("src/renderer/Model_ma.cpp")
    model_lwo = read("src/renderer/Model_lwo.cpp")
    render_system_init = read("src/renderer/RenderSystem_init.cpp")
    mapfile = read("src/idlib/mapfile.cpp")
    dict_cpp = read("src/idlib/Dict.cpp")
    lib = read("src/idlib/Lib.cpp")
    lib_h = read("src/idlib/Lib.h")
    heap_h = read("src/idlib/Heap.h")
    heap_cpp = read("src/idlib/Heap.cpp")
    rv_memsys = read("src/idlib/rvMemSys.cpp")
    swap = read("src/idlib/Swap.h")
    aas_file = read("src/aas/AASFile.cpp")
    cvar_system = read("src/framework/CVarSystem.cpp")
    cvar_system_h = read("src/framework/CVarSystem.h")
    linux_input = read("src/sys/linux/input.cpp")
    sys_public = read("src/sys/sys_public.h")
    win_net = read("src/sys/win32/win_net.cpp")
    win_main = read("src/sys/win32/win_main.cpp")
    win_local = read("src/sys/win32/win_local.h")
    win_syscon = read("src/sys/win32/win_syscon.cpp")
    posix_threads = read("src/sys/posix/posix_threads.cpp")
    renderer_gl_module = read("src/renderer/RendererGLModule.cpp")
    render_geometry_h = read("src/rendergeo/RenderGeometry.h")
    renderer_material = read("src/renderer/Material.cpp")
    maya_import = read("src/mayaimport/maya_main.cpp")
    linux_sound_h = read("src/sys/linux/sound.h")
    al_sound_sample = read("src/sound/OpenAL/AL_SoundSample.cpp")
    macos_sound = read("src/sys/macos/macos_sound.cpp")
    macos_controller = read("src/sys/macos/DOOMController.mm")
    macos_glimp = read("src/sys/macos/macos_glimp.mm")
    macos_preferences_h = read("src/sys/macos/PreferencesDialog.h")
    dmap = read("src/tools/compilers/dmap/dmap.cpp")
    tool_property_grid = read("src/tools/common/PropertyGrid.cpp")
    tool_path_tree_h = read("src/tools/comafx/CPathTreeCtrl.h")
    tool_path_tree = read("src/tools/comafx/CPathTreeCtrl.cpp")
    tool_prop_tree = read("src/tools/common/PropTree/PropTree.cpp")
    tool_prop_tree_combo = read("src/tools/common/PropTree/PropTreeItemCombo.cpp")
    tool_debugger_window = read("src/tools/debugger/DebuggerWindow.cpp")
    tool_ge_app = read("src/tools/guied/GEApp.cpp")
    tool_ge_item_props = read("src/tools/guied/GEItemPropsDlg.cpp")
    tool_ge_navigator = read("src/tools/guied/GENavigator.cpp")
    tool_ge_status_bar = read("src/tools/guied/GEStatusBar.cpp")
    tool_ge_wrapper = read("src/tools/guied/GEWindowWrapper.cpp")
    tool_ge_workspace = read("src/tools/guied/GEWorkspace.cpp")
    tool_radiant_cam = read("src/tools/radiant/CamWnd.cpp")
    tool_radiant_entity_list = read("src/tools/radiant/EntityListDlg.cpp")
    tool_radiant_main = read("src/tools/radiant/MainFrm.cpp")
    tool_radiant_map = read("src/tools/radiant/EditorMap.cpp")
    tool_radiant_new_tex_h = read("src/tools/radiant/NewTexWnd.h")
    tool_radiant_new_tex = read("src/tools/radiant/NewTexWnd.cpp")
    tool_radiant_xy = read("src/tools/radiant/XYWnd.cpp")
    renderer_lightrun = read("src/renderer/tr_lightrun.cpp")
    win32_tool_sources = {
        path.relative_to(ROOT).as_posix(): path.read_text(encoding="utf-8", errors="surrogateescape")
        for path in (ROOT / "src" / "tools").rglob("*")
        if path.is_file() and path.suffix.lower() in {".c", ".cc", ".cpp", ".h"}
    }

    require(
        meson,
        "linux_cross_dso_sanitizer_cpp_args += ['-fno-sanitize=vptr']",
        "Linux engine/game cross-DSO sanitizer boundary",
    )
    require(
        meson,
        "engine_cpp_args = shared_cpp_args + common_header_cpp_args + ['-D__DOOM_DLL__'] + linux_cross_dso_sanitizer_cpp_args",
        "Linux engine cross-DSO sanitizer scope",
    )
    require(
        meson,
        "game_common_cpp_args = shared_cpp_args + common_header_cpp_args + ['-DGAME_DLL'] + linux_cross_dso_sanitizer_cpp_args",
        "Linux game cross-DSO sanitizer scope",
    )
    require(
        meson,
        "bse_cpp_args = shared_cpp_args + common_header_cpp_args + linux_cross_dso_sanitizer_cpp_args",
        "Linux BSE cross-DSO sanitizer scope",
    )
    require(
        meson,
        "renderer_gl_module_args = engine_cpp_args + [",
        "OpenGL renderer cross-DSO sanitizer scope",
    )
    require(
        meson,
        "renderer_vk_module_args = engine_cpp_args + [",
        "Vulkan renderer cross-DSO sanitizer scope",
    )
    reject(
        meson,
        "shared_cpp_args += ['-fno-sanitize=vptr']",
        "over-broad UBSan vptr exclusion",
    )
    reject(meson, "-fno-sanitize=pointer-overflow", "masked renderer file-ownership defect")
    require(meson, "dedicated_engine_cpp_args = engine_cpp_args", "dedicated sanitizer inheritance")
    require(
        meson,
        "Keep AddressSanitizer and every other requested UBSan check enabled.",
        "Linux game-module sanitizer scope",
    )
    require(
        aas_file,
        'WriteFloatString( "\\t%d ( %d %d %d %d %d %d %d %d ) %d {\\n"',
        "complete AAS area writer arguments",
    )
    reject(
        aas_file,
        'WriteFloatString( "\\t%d ( %d %d %d %d %d %d %d %d %d %d ) %d {\\n"',
        "AAS area writer missing format arguments",
    )
    require(cvar_system, 'common->Printf( "%s", string.c_str() );', "literal cvar-list output format")
    reject(cvar_system, "common->Printf( string );", "dynamic cvar-list output format")

    for source, safe_call, unsafe_call, context in (
        (
            console_cpp,
            'SCR_DrawTextRightAlign( y, "%s", msg.c_str() );',
            'SCR_DrawTextRightAlign( y, msg.c_str() );',
            "async statistics console text",
        ),
        (
            file_system,
            'common->Printf( "%s", status.c_str() );',
            "common->Printf( status.c_str() );",
            "filesystem search-path status",
        ),
        (
            session,
            'common->Printf( "%s", message.c_str() );',
            "common->Printf( message );",
            "timedemo result text",
        ),
        (
            async_client,
            'common->Printf( "%s", message.c_str() );',
            "common->Printf( message );",
            "localized client download error text",
        ),
        (
            async_server,
            'common->Printf( "%s\\n", msg.c_str() );',
            'common->Printf( va( "%s\\n", msg.c_str() ) );',
            "server async statistics text",
        ),
        (
            simd,
            'idLib::common->Printf( "%s", string );',
            "idLib::common->Printf( string );",
            "SIMD benchmark label",
        ),
    ):
        require(source, safe_call, f"literal format for {context}")
        reject(source, unsafe_call, f"dynamic format for {context}")

    require(
        bit_msg,
        'FatalError( "idBitMsg: overflow without allowOverflow set: %d > %d*8",',
        "literal BitMsg overflow format",
    )
    reject(bit_msg, 'FatalError( va( "idBitMsg: overflow', "preformatted BitMsg overflow format")
    require(parser, 'scriptstack->Error( "%s", text );', "literal parser error forwarding format")
    require(parser, 'scriptstack->Warning( "%s", text );', "literal parser warning forwarding format")
    reject(parser, "scriptstack->Error( text );", "dynamic parser error forwarding format")
    reject(parser, "scriptstack->Warning( text );", "dynamic parser warning forwarding format")
    for safe_call in (
        'MA_VERBOSE(("MESH %s - parent %s\\n", header.name, header.parent));',
        'MA_VERBOSE(("\\tverts:%d\\n", maGlobal.currentObject->mesh.numVertexes));',
        'MA_VERBOSE(("\\tfaces:%d\\n", maGlobal.currentObject->mesh.numFaces));',
    ):
        require(model_ma, safe_call, "literal Maya verbose format")
    reject(model_ma, "MA_VERBOSE((va(", "preformatted Maya verbose format")
    for string_id in ("#str_41106", "#str_06780"):
        require(
            render_system_init,
            f'common->Error( "%s", common->GetLanguageDict()->GetString( "{string_id}" ) );',
            f"literal localized renderer error format for {string_id}",
        )
        reject(
            render_system_init,
            f'common->Error( common->GetLanguageDict()->GetString( "{string_id}" ) );',
            f"dynamic localized renderer error format for {string_id}",
        )

    require(md4, "typedef uint32_t UINT4;", "MD4 word type")
    require(md4, 'static_assert( sizeof( UINT4 ) == 4, "MD4 requires 32-bit words" );', "MD4 word contract")
    reject(md4, "typedef unsigned long int UINT4;", "LP64-wide MD4 word")
    require(md4_h, "uint32_t MD4_BlockChecksum", "fixed-width MD4 public result")
    require(md5_h, "uint32_t MD5_BlockChecksum", "fixed-width MD5 public result")
    require(md4, "uint32_t MD4_BlockChecksum", "fixed-width MD4 implementation result")
    require(md5, "uint32_t MD5_BlockChecksum", "fixed-width MD5 implementation result")
    reject(md4_h, "unsigned long MD4_BlockChecksum", "LP64-wide MD4 public result")
    reject(md5_h, "unsigned long MD5_BlockChecksum", "LP64-wide MD5 public result")

    for source, header, word_type, prefix, context in (
        (crc8, crc8_h, "uint8_t", "CRC8", "CRC-8"),
        (crc16, crc16_h, "uint16_t", "CRC16", "CRC-16"),
    ):
        require(header, f"void {prefix}_InitChecksum( {word_type} &crcvalue );", f"fixed-width {context} public state")
        require(header, f"{word_type} {prefix}_BlockChecksum", f"fixed-width {context} public result")
        require(source, f"static const {word_type} crctable[256]", f"fixed-width {context} table")
        require(source, f"void {prefix}_UpdateChecksum( {word_type} &crcvalue", f"fixed-width {context} implementation state")
        reject(header, "unsigned long", f"LP64-wide {context} public API")
    require(crc8, "uint32_t poly, c;", "fixed-width CRC-8 table generation")
    require(crc16, "uint32_t poly, c;", "fixed-width CRC-16 table generation")

    require(
        str_cpp,
        "const unsigned char character = static_cast<unsigned char>( data[i] );",
        "unsigned filename character-table index",
    )
    require(str_cpp, "upperCaseCharacter[character]", "bounded filename character-table lookup")
    require(str_cpp, "isdigit(character)", "defined ctype filename lookup")
    reject(str_cpp, "upperCaseCharacter[data[i]]", "signed filename character-table index")
    require(str_cpp, "int length = temp.Length();", "quote-strip length guard")
    require(str_cpp, "memmove(string, string + 1, length);", "overlap-safe leading quote removal")
    require(str_cpp, "length > 0 && string[length - 1] == '\\\"'", "empty-safe trailing quote check")
    reject(str_cpp, "strcpy(string, string + 1);", "overlapping quote-strip copy")
    reject(str_cpp, "string[strlen(string) - 1]", "empty quote-strip index")
    require(str_cpp, "uint32_t\thash;", "fixed-width filename hash accumulator")
    require(str_cpp, "static_cast<uint32_t>( letter )", "defined filename hash arithmetic")
    reject(str_cpp, "long\thash;", "LP64-wide filename hash accumulator")
    require(file_system, "int\t\t\t\t\t\tHashFileName( const char *fname ) const;", "fixed-width filesystem hash result")
    require(file_system, "uint32_t\thash;", "fixed-width filesystem hash accumulator")
    require(file_system, "static_cast<uint32_t>( letter )", "defined filesystem hash arithmetic")
    reject(file_system, "long idFileSystemLocal::HashFileName", "LP64-wide filesystem hash result")
    require(file_system, "uint32_t\t\t\tpos;", "fixed-width classic-ZIP file position")
    require(file_h, "uint32_t\t\t\t\tzipFilePos;", "fixed-width open ZIP file position")
    require(
        file_system,
        "fileInfoPosition > static_cast<unsigned long>( UINT32_MAX )",
        "checked classic-ZIP position narrowing",
    )
    require(
        file_system,
        "unzSetCurrentFileInfoPosition( pak->handle, static_cast<unsigned long>( pakFile->pos ) )",
        "explicit native minizip position conversion",
    )
    require(
        file_cpp,
        "unzSetCurrentFileInfoPosition( z, static_cast<unsigned long>( zipFilePos ) )",
        "explicit native minizip seek conversion",
    )
    reject(file_system, "unsigned long\t\tpos;", "LP64-wide stored classic-ZIP position")
    reject(file_h, "int\t\t\t\t\t\tzipFilePos;", "signed stored classic-ZIP position")
    require(
        trace_model,
        "if ( numSilEdges == 0 ) {\n\t\treturn 0;\n\t}",
        "empty silhouette edge list",
    )

    for source, header, word_type, assertion, prefix, context in (
        (crc32, crc32_h, "crc32Word_t", "CRC-32 requires a 32-bit word", "CRC32", "CRC-32"),
        (honeyman, honeyman_h, "honeymanWord_t", "Honeyman checksum requires a 32-bit word", "Honeyman", "Honeyman"),
    ):
        require(source, f"typedef uint32_t {word_type};", f"fixed-width {context} word")
        require(source, f'static_assert( sizeof( {word_type} ) == 4, "{assertion}" );', f"fixed-width {context} contract")
        require(source, f"static const {word_type} crctable[256]", f"fixed-width {context} table")
        require(header, f"void {prefix}_InitChecksum( uint32_t &crcvalue );", f"fixed-width {context} public state")
        require(header, f"uint32_t {prefix}_BlockChecksum", f"fixed-width {context} public result")
        require(source, f"void {prefix}_InitChecksum( uint32_t &crcvalue )", f"fixed-width {context} implementation state")
        reject(header, "unsigned long &crcvalue", f"LP64-wide {context} public state")
        reject(source, "static unsigned long crctable[256]", f"LP64-wide {context} table")

    for helper in ("idMath_FloatBits", "idMath_FloatFromBits", "idMath_FloatXorBits"):
        require(math, helper, "alias-safe float-bit helpers")
    reject(math, "reinterpret_cast<int *>", "strict-aliasing float-bit access")
    require(math, "static uint32_t\t\t\tFtol( float f );", "fixed-width float-to-word result")
    require(math, "ID_INLINE uint32_t idMath::Ftol( float f )", "fixed-width float-to-word implementation")
    reject(math, "unsigned long idMath::Ftol", "LP64-wide float-to-word result")
    require(random, "int\t\t\t\t\tseed;", "legacy signed idRandom save/network state")
    require(random, "unsigned int\t\t\tseed;", "32-bit idRandom2 state")
    require(random, "const unsigned int nextSeed = 69069u * static_cast<unsigned int>( seed ) + 1u;", "32-bit idRandom wraparound")
    require(random, "? -1 - static_cast<int>( ~nextSeed )", "representable signed idRandom state mapping")
    require(random, "return static_cast<int>( nextSeed & static_cast<unsigned int>( idRandom::MAX_RAND ) );", "unsigned idRandom output mask")
    reject(random, "seed = 69069 * seed + 1;", "signed-overflow idRandom state update")
    require(random, "seed = 1664525u * seed + 1013904223u;", "32-bit idRandom2 wraparound")
    reject(random, "unsigned long\t\t\tseed;", "LP64-wide idRandom2 state")
    require(simd_generic, "signBit = idMath_FloatBits( area ) & ( 1u << 31 );", "generic tangent sign extraction")
    if simd_generic.count("if ( count <= 0 ) {\n\t\treturn;\n\t}") != 3:
        raise AssertionError("SIMD Memcpy, Memset, and Zero16 must accept empty null-backed ranges")
    require(vector, "idMath_FloatBits( x ) | idMath_FloatBits( y ) | idMath_FloatBits( z )", "idVec3 zero test")
    initialized_vector_hash = "boxHashSize = 16;\n\thash.Clear( idMath::IPow( boxHashSize, dimension ), 128 );"
    if vector_set.count(initialized_vector_hash) != 2:
        raise AssertionError("Both default vector hash containers must initialize boxHashSize before using it")
    reject(
        vector_set,
        "hash.Clear( idMath::IPow( boxHashSize, dimension ), 128 );\n\tboxHashSize = 16;",
        "uninitialized default vector hash size",
    )
    require(simd_sse2, "defined( __x86_64__ )", "x86-only SSE2 declaration guard")
    require(simd, "#if defined( ID_SIMD_SSE2_AVAILABLE )", "guarded SSE2 processor selection")
    require(simd, "#define TIME_TYPE int64_t", "fixed-width SIMD benchmark clock type")
    require(simd, "TIME_TYPE baseClocks = 0;", "fixed-width SIMD benchmark baseline")
    require(simd, "void PrintClocks( const char *string, int dataCount, TIME_TYPE clocks, TIME_TYPE otherClocks = 0 )", "wide SIMD benchmark reporting")
    reject(simd, "long baseClocks", "LP64-dependent SIMD benchmark baseline")
    reject(simd, "#define TIME_TYPE int\n", "truncated SIMD benchmark clock type")

    require(token, "unsigned int\tintvalue;", "32-bit binary token storage")
    require(token, "uint32_t\t\tGetUnsignedLongValue( void );", "fixed-width binary token accessor")
    require(token, "ID_INLINE uint32_t idToken::GetUnsignedLongValue( void )", "fixed-width binary token accessor implementation")
    reject(token, "unsigned long\tintvalue;", "LP64-wide binary token storage")
    reject(token, "ID_INLINE unsigned long\tidToken::GetUnsignedLongValue", "LP64-wide binary token accessor")
    for expected in (
        "unsigned int val = static_cast<unsigned int>( tok->GetUnsignedLongValue() );",
        "case BTT_SUBTYPE_INT: {",
        "const int signedValue = static_cast<int>( token->intvalue );",
        'idStr::snPrintf( buffer, buffersize, "%d", signedValue );',
        'idStr::snPrintf( buffer, buffersize, "%u", token->intvalue );',
        "signed char byteVal = static_cast<signed char>( val );",
        "signed char byteVal = static_cast<signed char>( intval );",
        "TextCompiler::WriteValue<signed char>(byteVal, mBinaryFile, swapBytes);",
        "token->intvalue = ReadValue<signed char>(OBJ);",
        "token->floatvalue = ReadValue<signed char>(OBJ);",
    ):
        require(lexer, expected, "32-bit binary token serialization")
    for legacy in (
        "assert(sizeof(long) == sizeof(int));",
        '"%ld"',
        '"%lu"',
        "char byteVal = (char)val;",
        "char byteVal = (char)intval;",
        "TextCompiler::WriteValue<char>(byteVal, mBinaryFile, swapBytes);",
        "token->intvalue = ReadValue<char>(OBJ);",
        "token->floatvalue = ReadValue<char>(OBJ);",
    ):
        reject(lexer, legacy, "LP64-unsafe binary token serialization")

    require(parser, "int32_t intvalue;", "fixed-width parser expression storage")
    for directive in (
        "int idParser::Directive_elif( void ) {",
        "int idParser::Directive_if( void ) {",
        "int idParser::Directive_eval( void ) {",
        "int idParser::DollarDirective_evalint( void ) {",
    ):
        directive_start = parser.find(directive)
        if directive_start < 0 or "int32_t value;" not in parser[directive_start : directive_start + 400]:
            raise AssertionError(f"Parser directive {directive!r} must retain a fixed-width result")
    if parser.count('sprintf( buf, "%u", magnitude );') != 2:
        raise AssertionError("Both integer parser directives must format a defined 32-bit magnitude")
    require(parser, "-static_cast<int64_t>( value )", "INT32_MIN-safe parser magnitude")
    reject(parser, "signed long int", "LP64-wide parser expression storage")
    reject(parser, 'sprintf( buf, "%ld",', "LP64-dependent parser integer format")
    for helper in (
        "idParser_Int32Negate",
        "idParser_Int32Add",
        "idParser_Int32Subtract",
        "idParser_Int32Multiply",
        "idParser_Int32LeftShift",
        "idParser_Int32ArithmeticRightShift",
        "idParser_Int32Divide",
        "idParser_Int32Modulo",
    ):
        require(parser, helper, "defined 32-bit parser expression arithmetic")
    require(
        parser,
        "return idParser_Int32FromBits( 0u - idParser_Int32Bits( value ) );",
        "modulo-2^32 parser unary negation",
    )
    require(
        parser,
        "return shift >= 0 && shift < 32;",
        "bounded parser shift count",
    )
    if parser.count("if ( !idParser_IsValidShiftCount( v2->intvalue ) ) {") != 2:
        raise AssertionError("Both parser shift operators must validate counts before shifting")
    if parser.count("if ( lhs == INT32_MIN && rhs == -1 ) {") != 2:
        raise AssertionError("Parser division and modulo must handle INT32_MIN / -1 explicitly")
    require(parser, "return INT32_MIN;", "defined parser INT32_MIN / -1 division result")
    require(parser, "return 0;", "defined parser INT32_MIN / -1 modulo result")
    evaluator = parser.split("int idParser::EvaluateTokens", 1)[1].split("idParser::Evaluate\n", 1)[0]
    if re.search(r"v1->intvalue\s*(?:\*=|/=|%=|\+=|-=|<<=|>>=|&=|\|=|\^=)", evaluator):
        raise AssertionError("Parser evaluator contains direct signed compound arithmetic")
    reject(evaluator, "v->intvalue = - t->GetIntValue()", "direct signed parser unary negation")
    for shift_call in (
        "idParser_Int32ArithmeticRightShift( v1->intvalue, static_cast<uint32_t>( v2->intvalue ) )",
        "idParser_Int32LeftShift( v1->intvalue, static_cast<uint32_t>( v2->intvalue ) )",
    ):
        require(evaluator, shift_call, "checked fixed-width parser shift")

    if re.search(r"\btypedef\b[^;]+\bulong\s*;", lib_h):
        raise AssertionError("Project-local ulong alias conflicts with LP64 system headers")
    if linux_sound_h.count("Lock( void **pDSLockedBuffer, uint32_t *dwDSLockedBufferSize )") != 2:
        raise AssertionError("Both Linux sound stubs must expose a fixed-width lock size")
    if linux_sound_h.count("GetCurrentPosition( uint32_t *pdwCurrentWriteCursor )") != 2:
        raise AssertionError("Both Linux sound stubs must expose a fixed-width cursor")
    require(macos_sound, "Lock( void **pDSLockedBuffer, uint32_t *dwDSLockedBufferSize )", "fixed-width macOS sound lock size")
    require(macos_sound, "GetCurrentPosition( uint32_t *pdwCurrentWriteCursor )", "fixed-width macOS sound cursor")
    reject(linux_sound_h, "ulong *", "ambiguous Linux sound word width")
    reject(macos_sound, "ulong *", "ambiguous macOS sound word width")
    require(heap_h, "static const size_t ID_HEAP_MAX_SIZE = 0x7fffffffu;", "shared signed-32-bit heap boundary")
    if heap_h.count("( uintptr_t )( addr + 15 ) & ~(uintptr_t)15") != 2:
        raise AssertionError("Both Mem_StackAlloc16 variants must preserve pointer high bits")
    reject(heap_h, "0xfffffff0", "32-bit stack-alignment pointer mask")
    if heap_h.count("Mem_Alloc( const size_t size") != 2:
        raise AssertionError("Normal and debug heap APIs must accept native-width allocation sizes")
    if heap_h.count("Mem_ClearedAlloc( const size_t size") != 2:
        raise AssertionError("Normal and debug cleared-allocation APIs must accept native-width sizes")
    if heap_h.count("Mem_Alloc16( const size_t size") != 2:
        raise AssertionError("Normal and debug aligned-allocation APIs must accept native-width sizes")
    reject(heap_h, "Mem_Alloc( const int size", "truncated public allocation size")
    require(heap_h, "class alignas(16) idDynamicBlock", "aligned dynamic-block payload header")
    require(heap_h, "static bool\t\t\t\t\t\tGetAlignedBlockSize( const int num, int &alignedBytes );", "checked dynamic-block size calculation")
    require(heap_h, "usedBlockMemory += oldBlockSize;", "failed dynamic-block resize accounting rollback")
    require(heap_h, "block = ( idDynamicBlock<type> * ) Mem_Alloc16( allocSize, memoryTag );\n//RAVEN END\n\t\tif ( block == NULL )", "failed dynamic-block base allocation handling")
    require(heap_h, "Mem_Alloc16( baseBlockSize, memoryTag );\n//RAVEN END\n\t\tif ( block == NULL )", "failed fixed-block allocation handling")
    if heap_h.count("GetUsedBlockMemorySize( void ) const") != 2:
        raise AssertionError("Both dynamic allocators must expose full-width memory accounting")
    require(heap_h, "static_cast<int64_t>( sizeof( idDynamicBlock<type> ) )", "wide adjacent-block size calculation")
    require(heap_cpp, "#define MALLOC_HEADER_SIZE\t\t0", "release allocator metadata model")
    require(heap_cpp, "static bool Mem_ValidateAllocSize( const size_t size", "checked legacy-heap narrowing boundary")
    require(heap_cpp, "allocation of %zu bytes exceeds the 32-bit heap limit", "native-width allocation diagnostic")
    require(heap_cpp, "const dword bytesLeft = pageSize - smallCurPageOffset;", "width-stable small-page remainder")
    reject(heap_cpp, "(long)(pageSize) - smallCurPageOffset", "LP64-dependent small-page remainder")
    require(rv_memsys, "static bool MemSys_ValidateAllocSize( const size_t size", "checked alternate-heap narrowing boundary")
    reject(rv_memsys, "void *Mem_Alloc( const int size", "truncated alternate-heap allocation size")
    require(cvar_system, "Mem_Alloc( allocationSize )", "native-width CVar string-table allocation")
    reject(cvar_system, "Mem_Alloc( (int)allocationSize )", "truncated CVar string-table allocation")
    require(file_system_h, "FILE_NOT_FOUND_TIMESTAMP\t= static_cast<ID_TIME_T>( -1 );", "width-stable missing-file timestamp")
    reject(file_system_h, "FILE_NOT_FOUND_TIMESTAMP\t= 0xFFFFFFFF", "32-bit-only missing-file timestamp")

    require(sys_public, "typedef uintptr_t idSocketHandle_t;", "Win64-sized socket handle")
    require(sys_public, "idSocketHandle_t\tnetSocket;", "Win64-safe UDP socket storage")
    require(sys_public, "idSocketHandle_t\tfd;", "Win64-safe TCP socket storage")
    reject(sys_public, "int\t\t\tnetSocket;", "Win64-truncated UDP socket storage")
    reject(sys_public, "int\t\t\tfd;", "Win64-truncated TCP socket storage")
    for signature in (
        "static SOCKET NET_IPSocketForFamily(",
        "static bool Net_BindDualStack( const char *ipv4Text, const char *ipv6Text, int portNumber, SOCKET &socket4, SOCKET &socket6",
        "static SOCKET Net_SocketForAddress( SOCKET netSocket, SOCKET netSocket6",
        "static void Net_SendMulticast6Packet( SOCKET netSocket",
        "bool Net_WaitForUDPPacket( SOCKET netSocket, SOCKET netSocket6",
        "bool Net_GetUDPPacket( SOCKET netSocket",
        "void Net_SendUDPPacket( SOCKET netSocket",
    ):
        require(win_net, signature, "Win64-safe Winsock helper signature")
    # A socket handle is 64-bit on Win64, so nothing may narrow one to int.
    for narrowing in (
        "int NET_IPSocket",
        "int Net_SocketForAddress",
        "( int )netSocket",
        "(int)netSocket",
    ):
        reject(win_net, narrowing, "Win64-truncated Winsock handle")
    require(win_local, "LRESULT CALLBACK MainWndProc", "pointer-sized main window result")
    # Splash, console, input-line and button-subclass procedures.
    if win_syscon.count("LRESULT CALLBACK") != 4:
        raise AssertionError("All system-console window procedures must return pointer-sized LRESULT")
    reject(win_syscon, "LONG WINAPI ConWndProc", "truncated system-console window result")
    require(win_main, "struct __stat64 st;", "64-bit Windows file timestamp source")
    require(win_main, "_fstat64( _fileno( fp ), &st )", "64-bit Windows file timestamp query")
    reject(win_main, "return (long)st.st_mtime;", "LLP64-truncated Windows file timestamp")
    require(win_main, "#include <ShlObj.h>", "Windows Known Folder declarations")
    require(win_main, "SHGetKnownFolderPath( folderId, KF_FLAG_DEFAULT", "Windows Known Folder lookup")
    require(win_main, "FOLDERID_LocalAppData", "Windows Local AppData save root")
    require(win_main, "FOLDERID_SavedGames", "Windows redirected Saved Games fallback")
    require(win_main, "savePath.SlashesToBackSlashes();", "native Windows save-path separators")
    require(win_main, "!Sys_IsAbsoluteWindowsPath( savePath )", "absolute Windows save-root fallback guard")
    require(meson, "windows_shell_dep = cc.find_library('shell32', required: true)", "Windows Shell link dependency")
    require(meson, "windows_ole_dep = cc.find_library('ole32', required: true)", "Windows COM allocator link dependency")
    if meson.count("\n      windows_shell_dep,") + meson.count("\n    windows_shell_dep,") != 3 or meson.count("\n      windows_ole_dep,") + meson.count("\n    windows_ole_dep,") != 3:
        raise AssertionError("All Windows client and dedicated dependency sets must retain Known Folder libraries")
    require(sys_public, "uint32_t\t\tthreadId;", "fixed-width cross-platform thread id")
    require(win_main, "DWORD threadId = 0;", "Win32-native CreateThread id output")
    reject(win_main, "&info.threadId", "non-DWORD CreateThread id output")
    # openQ4-game carried this pin too, but src/sys/win32/ is untracked there,
    # so it could only ever run for developers with both checkouts. Enforce it
    # here, against the file this repository actually owns.
    require(
        win_main,
        "info.threadId = static_cast<uint32_t>( threadId );",
        "fixed-width stored thread identifier",
    )
    require(posix_threads, "static_assert( sizeof( pthread_t ) <= sizeof( uintptr_t )", "representable POSIX thread handle")
    require(posix_threads, "memcpy( &handle, &thread, sizeof( thread ) );", "complete POSIX thread-handle encoding")
    require(renderer_gl_module, "DWORD threadId = 0;", "renderer Win32-native CreateThread id output")
    require(renderer_gl_module, "static_assert( sizeof( pthread_t ) <= sizeof( uintptr_t )", "renderer POSIX thread handle representation")

    if maya_import.count("va_arg( argPtr, int )") != 2:
        raise AssertionError("Maya integer formatters must read promoted int arguments")
    if maya_import.count("va_arg( argPtr, unsigned int )") != 4:
        raise AssertionError("Maya unsigned formatters must read promoted unsigned-int arguments")
    reject(maya_import, "va_arg( argPtr, long )", "LP64-invalid Maya promoted integer read")
    reject(maya_import, "va_arg( argPtr, unsigned long )", "LP64-invalid Maya promoted unsigned read")
    require(macos_preferences_h, "typedef int32_t LONG;", "fixed-width legacy macOS point coordinate")
    require(macos_preferences_h, "uint32_t\t\t\tflags;", "fixed-width legacy macOS display flags")
    for source, declaration, context in (
        (macos_sound, "SInt32 gestaltOSVersion = 0;", "legacy macOS sound OS version"),
        (macos_glimp, "SInt32 system_version = 0;", "legacy macOS OpenGL OS version"),
        (macos_controller, "SInt32 gestaltOSVersion = 0;", "legacy macOS controller OS version"),
        (macos_controller, "SInt32 gestaltSpeed = 0;", "legacy macOS processor speed"),
        (macos_controller, "SInt32 osVers = 0;", "legacy macOS processor OS version"),
        (macos_controller, "SInt32 ramSize = 0;", "legacy macOS physical RAM response"),
        (macos_controller, "SInt32 cpu = 0;", "legacy macOS CPU response"),
    ):
        require(source, declaration, f"Gestalt-compatible {context}")
    require(macos_glimp, "const GLint\t\t\t\t\t\tswap_limit = 0;", "OpenGL-sized macOS swap interval")
    require(macos_controller, "uint32_t hi;", "fixed-width PPC floating-point control word")
    require(macos_controller, "uint32_t lo;", "fixed-width PPC floating-point control word")
    require(macos_controller, "static bool OSX_ReadRegistryUInt32( CFTypeRef value, UInt32& result )", "fixed-width macOS PCI registry reader")
    require(macos_controller, "CFDataGetLength( data ) < static_cast<CFIndex>( sizeof( result ) )", "bounds-checked macOS PCI registry value")
    require(macos_controller, "memcpy( &result, CFDataGetBytePtr( data ), sizeof( result ) );", "alignment-safe macOS PCI registry value")
    reject(macos_controller, "*((long*)CFDataGetBytePtr", "LP64-wide macOS PCI registry read")
    reject(macos_controller, "*(UInt32*)CFDataGetBytePtr", "unaligned macOS PCI registry read")
    reject(macos_sound, "long gestaltOSVersion", "LP64-wide Gestalt response")
    reject(macos_glimp, "long system_version", "LP64-wide Gestalt response")
    for legacy_response in ("long gestaltOSVersion", "long gestaltSpeed", "long osVers", "long ramSize", "long cpu"):
        reject(macos_controller, legacy_response, "LP64-wide Gestalt response")

    cvar_sentinel = "reinterpret_cast< idCVar * >( static_cast<uintptr_t>( -1 ) )"
    if cvar_system_h.count(cvar_sentinel) != 3:
        raise AssertionError("Every CVar registration sentinel must be constructed at pointer width")
    reject(cvar_system_h, "(idCVar *)-1", "integer-to-pointer CVar sentinel cast")
    require(render_geometry_h, "reinterpret_cast< srfTriangles_t * >( static_cast<uintptr_t>( -1 ) )", "pointer-width deferred-light sentinel")
    require(render_geometry_h, "reinterpret_cast< byte * >( static_cast<uintptr_t>( -1 ) )", "pointer-width cull sentinel")
    reject(render_geometry_h, "((srfTriangles_t *)-1)", "integer-to-pointer deferred-light sentinel cast")
    require(renderer_material, "reinterpret_cast< idImage * >( static_cast<uintptr_t>( 1 ) )", "pointer-width renderer self-test sentinel")
    reject(renderer_material, "(idImage *)1", "integer-to-pointer renderer self-test sentinel cast")

    require(
        common_cpp,
        'truncated to %zu characters\\n", strlen(msg)-1',
        "size_t-safe common truncation diagnostic",
    )
    reject(
        common_cpp,
        'truncated to %d characters\\n", strlen(msg)-1',
        "LP64-unsafe common truncation diagnostic",
    )
    require(
        linux_input,
        "XLookupString buffer '%s' (%zu)\\n\", buf, strlen(buf)",
        "size_t-safe Linux input diagnostic",
    )
    reject(
        linux_input,
        "XLookupString buffer '%s' (%d)\\n\", buf, strlen(buf)",
        "LP64-unsafe Linux input diagnostic",
    )
    for path_name, extension in (("regionPath", ".reg"), ("leakPath", ".lin")):
        require(dmap, f"idStr {path_name} = dmapGlobals.mapFileBase;", f"dynamic dmap {extension} path")
        require(dmap, f'{path_name} += "{extension}";', f"bounded dmap {extension} suffix")
        require(dmap, f"fileSystem->RemoveFile( {path_name} );", f"dmap {extension} cleanup")
    reject(dmap, 'sprintf( path, "%s.reg"', "fixed-buffer dmap region path")
    reject(dmap, 'sprintf( path, "%s.lin"', "fixed-buffer dmap leak path")

    require(decl_mat_type, "memset( mTint, 0, sizeof( mTint ) );", "four-byte material tint initialization")
    require(decl_mat_type, "memcpy( mTint, tint, sizeof( mTint ) );", "four-byte material tint copy")
    reject(decl_mat_type, "*( ulong *)mTint", "LP64-wide material tint access")
    require(decl_manager, "uint32_t\t\t\t\tbits[8];", "32-bit Huffman storage")
    require(decl_manager, "1u << ( code.numBits & 31 )", "defined Huffman high-bit shift")

    require(collision_local, "unsigned int\t\t\tside;", "32-bit collision sidedness mask")
    require(collision_local, "unsigned int\t\t\tsideSet;", "32-bit collision sidedness cache mask")
    reject(collision_local, "unsigned long\t\t\tside", "LP64-wide collision sidedness mask")
    for source in (collision_contents, collision_translate):
        reject(source, "(1<<bitNum)", "signed collision high-bit shift")
        reject(source, "(1 << bitNum)", "signed collision high-bit shift")
    require(collision_contents, "(1u<<bitNum)", "unsigned collision contents shift")
    require(collision_translate, "(1u << bitNum)", "unsigned collision translation shift")
    for source, context in (
        (collision_model, "collision-model instance memory report"),
        (collision_load, "collision-model manager memory report"),
    ):
        for label in ("vertices", "edges", "nodes", "polygon refs", "brush refs"):
            require(source, f"{label} (%zu KB)", f"size_t-safe {context}")
            reject(source, f"{label} (%i KB)", f"LP64-unsafe {context}")

    require(compressor, "memcpy( &word1, p1, sizeof( word1 ) );", "unaligned bitstream comparison")
    require(compressor, "memcpy( &word2, p2, sizeof( word2 ) );", "unaligned bitstream comparison")
    reject(compressor, "*(const int *)p1", "unaligned bitstream integer access")
    require(msg_channel, "#define\tFRAGMENT_BIT\t\t\t(1u<<31)", "defined unsigned network fragment flag")
    require(msg_channel, "static_cast<unsigned int>( outgoingSequence ) | FRAGMENT_BIT", "unsigned fragment flag serialization")
    require(msg_channel, "const unsigned int sequenceBits = static_cast<unsigned int>( sequence );", "unsigned fragment flag parsing")
    reject(msg_channel, "#define\tFRAGMENT_BIT\t\t\t(1<<31)", "undefined signed network high-bit shift")
    require(renderer_light, "memcpy( &word, bytes + i * sizeof( word ), sizeof( word ) );", "unaligned joint hash access")
    reject(renderer_light, "reinterpret_cast<const unsigned long long *>( joints )", "unaligned joint hash access")
    require(
        renderer_light,
        "if ( count > 0 ) {\n\t\t\tmemcpy( tr.viewDef->drawSurfs, old, count );\n\t\t}",
        "empty draw-surface list growth",
    )
    require(renderer_local, "alignas(16) byte base[4];", "16-byte frame allocator payload")
    require(
        renderer_local,
        'static_assert( offsetof( frameMemoryBlock_t, base ) % 16 == 0, "frame allocator payload must remain 16-byte aligned" );',
        "compile-time frame allocator alignment contract",
    )
    reject(renderer_local, "int\t\tpoop;", "32-bit-only manual frame allocator padding")
    require(renderer_main, "buf = block->base + block->used;", "aligned frame allocation base")
    require(renderer_main, "Mem_Alloc16( MEMORY_BLOCK_SIZE + sizeof( *block ) )", "aligned initial frame block")
    require(renderer_main, "Mem_Alloc16( static_cast<size_t>( size ) + sizeof( *fresh ) )", "native-width aligned frame growth")
    require(renderer_main, "Mem_Free16( block );", "paired aligned frame block release")
    reject(renderer_main, "Mem_Alloc( size + sizeof( *block ) )", "unaligned frame block allocation")
    reject(renderer_main, "Mem_Free( block );", "mismatched frame block release")
    require(renderer_draw_arb2, "char\t*start = NULL, *end;", "initialized ARB program start")
    require(
        renderer_vertex_cache,
        "ID_INLINE void *RB_DrawVertAttributePointer( const void *base, const size_t byteOffset ) {",
        "shared VBO attribute-offset helper",
    )
    require(
        renderer_vertex_cache,
        "reinterpret_cast<uintptr_t>( base ) + byteOffset",
        "integer-space VBO attribute offset",
    )
    require(
        renderer_vertex_cache_cpp,
        "reinterpret_cast<void *>( static_cast<uintptr_t>( buffer->offset ) )",
        "pointer-width VBO offset sentinel",
    )
    reject(renderer_vertex_cache_cpp, "(void *)buffer->offset", "integer-to-pointer VBO offset cast")
    require(renderer_vk_gui, "VK_Ring_Alloc( vkRing_t &ring, const void *data, size_t bytes, int alignment )", "native-width Vulkan ring upload size")
    require(renderer_vk_gui, "bytes > capacity - offset", "overflow-free Vulkan ring capacity check")
    require(renderer_vk_gui, "( alignment & ( alignment - 1 ) ) != 0", "validated Vulkan ring alignment")
    for geometry_type in ("idDrawVert", "glIndex_t", "shadowCache_t", "idVec3"):
        require(
            renderer_vk_gui,
            f"static_cast<size_t>( tri->num{'Indexes' if geometry_type == 'glIndex_t' else 'Verts'} ) * sizeof( {geometry_type} )",
            f"native-width Vulkan {geometry_type} upload product",
        )
    reject(renderer_vk_gui, "tri->numVerts * (int)sizeof", "signed-overflowing Vulkan vertex upload product")
    reject(renderer_vk_gui, "tri->numIndexes * (int)sizeof", "signed-overflowing Vulkan index upload product")
    self_check_renderer_vertex_cache_audit()
    audit_renderer_vertex_cache_consumers()
    for guarded_copy, context in (
        (
            "if ( tri->numDupVerts > 0 ) {\n\t\tmemcpy( tri->dupVerts, tempDupVerts, tri->numDupVerts * 2 * sizeof( tri->dupVerts[0] ) );",
            "empty duplicate-vertex list",
        ),
        (
            "if ( numSilEdges > 0 ) {\n\t\tmemcpy( tri->silEdges, silEdges, numSilEdges * sizeof( tri->silEdges[0] ) );",
            "empty silhouette-edge list",
        ),
        (
            "if ( tri->numVerts > 0 ) {\n\t\t\tmemcpy( newTri->verts + totalVerts, tri->verts, tri->numVerts * sizeof( *tri->verts ) );",
            "empty merged surface vertex list",
        ),
        (
            "if ( tri.numVerts > 0 ) {\n\t\tSIMDProcessor->Memcpy( tri.verts, verts, tri.numVerts * sizeof( tri.verts[0] ) );",
            "empty deform-info vertex list",
        ),
    ):
        require(renderer_trisurf, guarded_copy, context)
    require(renderer_trisurf, '"%6zu kB in %d triangle surfaces\\n"', "size_t-safe triangle surface memory report")
    require(renderer_trisurf, '"%6zu kB total triangle memory\\n"', "size_t-safe total triangle memory report")
    reject(renderer_trisurf, '"%6d kB total triangle memory\\n"', "LP64-unsafe total triangle memory report")
    require(render_world, '"%i interaction take %zu bytes\\n"', "size_t-safe interaction memory report")
    reject(render_world, '"%i interaction take %i bytes\\n"', "LP64-unsafe interaction memory report")
    require(image_files, "R_ReadTargaUInt16( const byte *data )", "unaligned TGA header read")
    reject(image_files, "LittleShort ( *(short *)buf_p )", "unaligned TGA header read")
    require(image_tools_h, "R_StaticAlloc( size_t bytes )", "native-width static image allocation API")
    require(image_tools_h, "R_ClearedStaticAlloc( size_t bytes )", "native-width cleared image allocation API")
    require(image_tools_h, "( *onStaticAlloc )( size_t bytes )", "native-width static allocation hook")
    reject(image_tools_h, "R_StaticAlloc( int bytes )", "truncated static image allocation API")
    require(image_tools_state, 'R_StaticAlloc failed on %zu bytes', "native-width static allocation diagnostic")
    require(renderer_local, "size_t\t\t\t\t\tstaticAllocCount;", "native-width renderer static allocation accounting")
    require(render_world, "staticAllocCount = %zu", "native-width interaction allocation report")
    require(renderer_lightrun, "staticAllocCount = %zu", "native-width regenerated-world allocation report")
    require(unzip, "Mem_ClearedAlloc(static_cast<size_t>(items) * static_cast<size_t>(size))", "wide zlib allocator multiplication")
    reject(unzip, "Mem_ClearedAlloc(items*size)", "32-bit zlib allocator multiplication")
    require(unzip, "uint8_t bytes[2];", "byte-wise classic-ZIP 16-bit read")
    require(unzip, "const uint16_t value = static_cast<uint16_t>( bytes[0] )", "unsigned classic-ZIP 16-bit assembly")
    require(unzip, "uint8_t bytes[4];", "byte-wise classic-ZIP 32-bit read")
    require(unzip, "const uint32_t value = static_cast<uint32_t>( bytes[0] )", "unsigned classic-ZIP 32-bit assembly")
    if unzip.count("fread( bytes, 1, sizeof( bytes ), fin ) != sizeof( bytes )") != 2:
        raise AssertionError("Both classic-ZIP scalar readers must reject short fread results")
    reject(unzip, "LittleShort( v)", "signed classic-ZIP 16-bit read")
    reject(unzip, "LittleLong( v)", "signed classic-ZIP 32-bit read")
    zip_set_position = unzip.split("extern int unzSetCurrentFileInfoPosition", 1)[1].split(
        "extern int unzLocateFile", 1
    )[0]
    require(zip_set_position, "s->current_file_ok = (err == UNZ_OK);", "classic-ZIP seek validity state")
    require(zip_set_position, "return err;", "classic-ZIP seek error propagation")
    reject(zip_set_position, "return UNZ_OK;", "discarded classic-ZIP seek failure")
    require(al_sound_sample, "const uint64_t decodedByteCount", "wide ADPCM decoded-size product")
    require(al_sound_sample, "int64_t widenedSample =", "wide ADPCM predictor accumulation")
    require(al_sound_sample, "static_cast<int64_t>( state->iSamp1 ) * state->coef1", "overflow-free ADPCM first coefficient product")
    require(al_sound_sample, "static_cast<int64_t>( state->iSamp2 ) * state->coef2", "overflow-free ADPCM second coefficient product")
    reject(al_sound_sample, "state->iSamp1 * state->coef1", "signed-overflowing ADPCM predictor product")
    require(
        al_sound_sample,
        "decodedByteCount > static_cast<uint64_t>( idMath::INT_MAX )",
        "checked ADPCM legacy-size narrowing",
    )
    require(
        al_sound_sample,
        "blockHeaderBytes + encodedSampleBytes != static_cast<uint64_t>( blockSize )",
        "validated ADPCM block geometry",
    )
    adpcm_decode = al_sound_sample.split("int idSoundSample_OpenAL::MS_ADPCM_decode", 1)[1]
    validation_end = adpcm_decode.index("if( encoded_len != 0")
    if "*audio_buf = decodedBuffer;" in adpcm_decode[:validation_end] or "*audio_len =" in adpcm_decode[:validation_end]:
        raise AssertionError("ADPCM caller outputs are mutated before decode validation succeeds")
    require(adpcm_decode, "*audio_buf = decodedBuffer;", "commit decoded ADPCM buffer after success")
    require(adpcm_decode, "*audio_len = static_cast<uint32_t>( decodedBytes );", "checked ADPCM output-length narrowing")
    require(
        read("src/framework/DeclMatType.cpp"),
        "cachedWidth <= idMath::INT_MAX / cachedHeight",
        "checked cached material-map pixel product",
    )
    require(
        bse_manager,
        '"bse_active: %i particles: %i traces: %i segments: %i\\n"',
        "integer BSE activity statistic format",
    )
    reject(
        bse_manager,
        '"bse_active: %i particles: %i traces: %i texels: %i\\n"',
        "integer format for floating BSE texel statistic",
    )
    require(model_h, "virtual\t\t\t\t\t\t~idRenderModel( void );", "polymorphic render-model destruction")
    reject(model_h, "//virtual\t\t\t\t\t\t~idRenderModel", "disabled render-model base destructor")
    require(model_h, "enum jointHandle_t : int {", "fixed-width integer joint-index handle")
    require(model_h, "INVALID_JOINT = -1", "invalid joint sentinel")
    reject(model_h, "typedef enum {\n\tINVALID_JOINT", "non-fixed enum used for integer handles")
    require(model_cpp, "idRenderModel::~idRenderModel()", "render-model base destructor definition")
    require(model_lwo, "lwKey *key = NULL;", "initialized LWO envelope key state")
    require(model_lwo, "lwTexture *tex = NULL;", "initialized legacy LWO texture state")
    require(model_lwo, "lwPlugin *shdr = NULL;", "initialized legacy LWO shader state")
    require(model_lwo, "if ( !s ) return NULL;", "legacy LWO texture source validation")
    require(model_lwo, "Mem_Free( s );\n      return NULL;", "legacy LWO texture allocation cleanup")
    surface5_start = model_lwo.index("lwSurface *lwGetSurface5(")
    surface5_end = model_lwo.index("int lwGetPolygons5(", surface5_start)
    surface5 = model_lwo[surface5_start:surface5_end]
    if surface5.count("if ( !tex ) goto Fail;") != 21:
        raise AssertionError("Every legacy LWO texture-dependent subchunk must fail closed")
    require(
        model_lwo,
        "case ID_SDAT:\n            if ( !shdr ) goto Fail;",
        "legacy LWO shader-data ordering guard",
    )
    require(
        model_lwo,
        "case ID_TFLG:\n            if ( !tex ) goto Fail;\n            flags = getU2( fp );\n            i = 0;",
        "legacy LWO texture-axis default",
    )
    require(mapfile, "return idMath_FloatBits( f );", "alias-safe map float checksum")
    require(dict_cpp, 'Printf( "%5zu KB in %d keys\\n"', "size_t-safe dictionary memory reporting")
    require(dict_cpp, 'Printf( "%5zu KB in %d values\\n"', "size_t-safe dictionary value reporting")
    reject(dict_cpp, 'Printf( "%5d KB in %d keys\\n"', "LP64-unsafe dictionary memory reporting")
    require(lib, "memcpy( &swapvalue, swaptest, sizeof( swapvalue ) );", "alignment-safe endian probe")
    reject(lib, "*(short *)swaptest", "unaligned endian probe")
    require(swap, "static constexpr uint32_t idSwapBig32( const uint32_t value )", "single-evaluation 32-bit byte swap")
    require(swap, "static constexpr uint16_t idSwapBig16( const uint16_t value )", "single-evaluation 16-bit byte swap")
    require(swap, "return static_cast<uint16_t>( ( value >> 8 ) | ( value << 8 ) );", "16-bit byte-swap truncation")
    require(swap, 'static_assert( idSwapBig32( 0x12345678u ) == 0x78563412u, "BIG32 must swap all four bytes" );', "compile-time BIG32 value contract")
    require(swap, 'static_assert( idSwapBig16( 0x1234u ) == 0x3412u, "BIG16 must swap and truncate both bytes" );', "compile-time BIG16 value contract")
    require(swap, "#define BIG32(v) idSwapBig32( static_cast<uint32_t>( v ) )", "fixed-width BIG32 helper")
    require(swap, "#define BIG16(v) idSwapBig16( static_cast<uint16_t>( v ) )", "fixed-width BIG16 helper")
    reject(swap, "((uint32)(v))", "undefined BIG32 alias")
    reject(swap, "((uint16)(v))", "undefined BIG16 alias")

    # Legacy editor targets are not part of the Meson build, so keep their
    # Win64 ABI safety explicit in this source contract.
    for warning_suppression in ("/wd4302", "/wd4311", "/wd4312"):
        reject(meson, warning_suppression, "suppressed MSVC pointer-width diagnostic")
    for warning_error in ("/we4302", "/we4311", "/we4312"):
        require(meson, warning_error, "enforced MSVC pointer-width diagnostic")

    for path, source in win32_tool_sources.items():
        for legacy_index in (
            "GWL_USERDATA",
            "GWL_WNDPROC",
            "GWL_HINSTANCE",
            "GWL_HWNDPARENT",
            "DWL_DLGPROC",
            "DWL_MSGRESULT",
            "GCL_HICON",
            "GCL_HICONSM",
            "GCL_WNDPROC",
            "GCL_HBRBACKGROUND",
        ):
            reject(source, legacy_index, f"Win64-unsafe window-long index in {path}")
        if re.search(
            r"MAKEINTRESOURCE\s*\(\s*IDC_(?:APPSTARTING|ARROW|CROSS|HAND|HELP|IBEAM|ICON|NO|"
            r"SIZEALL|SIZENESW|SIZENS|SIZENWSE|SIZEWE|UPARROW|WAIT)\s*\)",
            source,
        ):
            raise AssertionError(f"System cursor was truncated through MAKEINTRESOURCE in {path}")
        if re.search(r"\bOnTimer\s*\(\s*UINT\s+", source):
            raise AssertionError(f"Win64-truncated MFC timer id in {path}")
        if re.search(r"\b(?:virtual\s+)?int\s+[A-Za-z_0-9:]*OnToolHitTest\s*\(", source):
            raise AssertionError(f"Win64-truncated MFC tooltip id in {path}")
        if re.search(r"\bUINT\s+\w+\s*=\s*\w+->idFrom\b", source):
            raise AssertionError(f"Win64-truncated notification id in {path}")
        if re.search(r"\(\s*HMENU\s*\)\s*(?:IDC_|IDR_|\d)", source):
            raise AssertionError(f"Win64-unsafe child-control HMENU cast in {path}")
        if re.search(r"\bBOOL\s+CALLBACK\s+RBFProc\b|\(\s*DLGPROC\s*\)\s*RBFProc\b", source):
            raise AssertionError(f"Win64-truncated dialog procedure in {path}")
        if "unsigned int m_nTimerID" in source or re.search(r"m_nTimerID\s*(?:[!=]=|=)\s*-1", source):
            raise AssertionError(f"Win64-truncated MFC timer handle in {path}")
        if re.search(r'_T\(\s*"%d"\s*\)\s*,\s*lParam\b', source):
            raise AssertionError(f"LPARAM passed directly to a 32-bit vararg format in {path}")

    audit_tool_size_formats(win32_tool_sources)

    require(tool_property_grid, "GetWindowLongPtr( mWindow, GWLP_WNDPROC )", "pointer-sized property-grid subclass procedure")
    require(tool_property_grid, "reinterpret_cast< LPARAM >( item )", "pointer-sized property-grid item data")
    require(tool_property_grid, "reinterpret_cast< HMENU >( static_cast< INT_PTR >( id ) )", "pointer-sized child control id")
    reject(tool_property_grid, "(LONG)item", "truncated property-grid item data")
    require(tool_path_tree_h, "virtual INT_PTR\t\tOnToolHitTest", "pointer-sized path-tree tooltip id")
    require(tool_path_tree, "reinterpret_cast<UINT_PTR>( hitem )", "pointer-sized path-tree item tooltip id")
    require(tool_path_tree, "reinterpret_cast<UINT_PTR>( m_hWnd )", "pointer-sized path-tree tooltip window id")
    reject(tool_path_tree, "pTI->uId = (UINT)hitem", "truncated path-tree tooltip id")
    require(tool_prop_tree, "const UINT_PTR id = static_cast< UINT_PTR >( ::GetDlgCtrlID( m_hWnd ) );", "pointer-sized property-tree notification id")
    reject(tool_prop_tree, "(UINT)::GetMenu(m_hWnd)", "truncated property-tree control id")
    require(tool_prop_tree_combo, "static_cast< DWORD_PTR >( lParam )", "pointer-sized property-tree combo data")
    reject(tool_prop_tree_combo, "(DWORD)lParam", "truncated property-tree combo data")

    require(tool_debugger_window, "GetWindowLongPtr( mWndScript, GWLP_WNDPROC )", "pointer-sized debugger subclass procedure")
    require(tool_debugger_window, "reinterpret_cast< WPARAM >( &num )", "pointer-sized rich-edit zoom numerator")
    reject(tool_debugger_window, "(LONG)&num", "truncated rich-edit zoom numerator")

    require(tool_ge_app, "SetClassLongPtr( mMDIFrame, GCLP_HICON", "pointer-sized GUI editor class icon")
    if tool_ge_app.count("reinterpret_cast< LPARAM >( workspace )") != 2:
        raise AssertionError("Both GUI editor MDI creation paths must preserve the workspace pointer")
    reject(tool_ge_app, "(LONG)workspace", "truncated GUI editor workspace pointer")
    if tool_ge_item_props.count("reinterpret_cast< LPARAM >( new rvGEItemProps") != 4:
        raise AssertionError("Every GUI editor property-sheet page must preserve its page pointer")
    if tool_ge_item_props.count("item.lParam = reinterpret_cast< LPARAM >( key );") != 2:
        raise AssertionError("Every GUI editor key-list item must preserve its key pointer")
    reject(tool_ge_item_props, "item.lParam = (LONG) key", "truncated GUI editor list-item pointer")
    require(tool_ge_navigator, "item.lParam = reinterpret_cast< LPARAM >( window );", "pointer-sized GUI navigator item data")
    require(tool_ge_status_bar, "reinterpret_cast< LPARAM >( parts )", "pointer-sized status-bar parts payload")
    require(tool_ge_workspace, "reinterpret_cast< UINT_PTR >( popup )", "pointer-sized GUI editor submenu handle")
    require(tool_ge_wrapper, "idWinStr *var = new idWinStr();", "pointer-width-safe GUI window-wrapper association")
    require(tool_ge_wrapper, 'sscanf( var->c_str(), "%p", &wrapper )', "GUI window-wrapper pointer round trip")
    reject(tool_ge_wrapper, "int x = (int)this;", "truncated GUI window-wrapper pointer")

    require(tool_radiant_cam, "LRESULT CALLBACK CamWndProc", "pointer-sized Radiant camera window result")
    require(tool_radiant_xy, "LRESULT CALLBACK XYWndProc", "pointer-sized Radiant XY window result")
    reject(tool_radiant_cam, "LONG WINAPI CamWndProc", "truncated Radiant camera window result")
    reject(tool_radiant_xy, "LONG WINAPI XYWndProc", "truncated Radiant XY window result")
    if tool_radiant_xy.count("reinterpret_cast< UINT_PTR >") != 4:
        raise AssertionError("Every Radiant entity submenu must preserve its HMENU value")
    require(tool_radiant_entity_list, "reinterpret_cast< DWORD_PTR >( pEntity )", "pointer-sized Radiant list-item data")
    reject(tool_radiant_entity_list, "reinterpret_cast<DWORD>(pEntity)", "truncated Radiant list-item data")
    require(tool_radiant_main, 'input string was %zu bytes too large', "size_t-safe Radiant string warning")
    reject(tool_radiant_main, 'input string was %d bytes too large', "size_t-unsafe Radiant string warning")
    for handler in ("OnDisplayChange", "OnBSPStatus", "OnBSPDone"):
        require(
            tool_radiant_main,
            f"LRESULT CMainFrame::{handler}(WPARAM wParam, LPARAM lParam)",
            f"pointer-width Radiant {handler} message ABI",
        )
    reject(tool_radiant_main, "UINT wParam, long lParam", "truncated Radiant registered-message ABI")
    require(tool_radiant_new_tex_h, "virtual INT_PTR OnToolHitTest", "pointer-sized Radiant texture tooltip id")
    require(tool_radiant_new_tex, "INT_PTR CNewTexWnd::OnToolHitTest", "pointer-sized Radiant texture tooltip implementation")
    if tool_radiant_map.count("va_arg( argPtr, int )") != 4:
        raise AssertionError("Radiant integer formatters must read promoted int arguments")
    if tool_radiant_map.count("va_arg( argPtr, unsigned int )") != 8:
        raise AssertionError("Radiant unsigned formatters must read promoted unsigned-int arguments")
    reject(tool_radiant_map, "va_arg( argPtr, long )", "LP64-invalid promoted integer read")
    reject(tool_radiant_map, "va_arg( argPtr, unsigned long )", "LP64-invalid promoted unsigned read")

    print("linux_arm64_source_portability: ok")


if __name__ == "__main__":
    main()

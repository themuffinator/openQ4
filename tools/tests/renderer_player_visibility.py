#!/usr/bin/env python3
"""Contract checks for the openQ4 multiplayer player visibility overlays.

Three effects share one backend pass: an additive brightskin wash, an additive
rimlight, and a silhouette outline shell. What is pinned here is the part that is
easy to break and impossible to notice from a single screenshot.

The outline shell's extrusion is checked as arithmetic rather than as tokens. Its
job is to hold a constant pixel width, and the offset it applies is built from a
clip-space quantity and two per-pixel scales, so it is entirely possible to write
a version that is the right length and still points the wrong way. That failure
is invisible at 16:9 and obvious at 32:9, which is exactly the kind of thing a
test should catch instead of a bug report from someone with a wide monitor.

The rest pins the wiring: which surfaces may grow a shell, how the two depth
groups keep out of each other's silhouette mask, what the see-through flag now
covers, and that the pass hands the stencil back the way the frame expects it.
"""

import math
import os
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"
GLPROGS = ROOT / "content" / "baseoq4" / "pak0" / "glprogs"
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()

# Mirror of RB_PLAYER_OUTLINE_MIN/MAX_WIDTH and cl_player_outline_width.
PLAYER_OUTLINE_MIN_WIDTH = 0.5
PLAYER_OUTLINE_MAX_WIDTH = 6.0

# Mirror of RB_PLAYER_RIMLIGHT_MIN/MAX_POWER and _FLOOR.
RIMLIGHT_MIN_POWER = 0.25
RIMLIGHT_MAX_POWER = 8.0
RIMLIGHT_MIN_FLOOR = 0.0
RIMLIGHT_MAX_FLOOR = 1.0


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def assert_close(actual, expected, message, tolerance=1e-6):
    if abs(actual - expected) > tolerance:
        raise AssertionError(f"{message} (expected {expected}, got {actual})")


def cxx_function_body(source, signature):
    start = source.find(signature)
    assert_true(start >= 0, f"{signature} should exist")

    open_brace = source.find("{", start)
    assert_true(open_brace >= 0, f"{signature} should have a function body")

    depth = 0
    for index in range(open_brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1:index]

    raise AssertionError(f"{signature} should have a closed function body")


# ---------------------------------------------------------------------------
# The extrusion, as arithmetic
# ---------------------------------------------------------------------------


def projection_scales(viewport_width, viewport_height):
    """An aspect-correct perspective frustum, as R_SetupProjection builds one.

    Only the ratio matters here. Square pixels mean tan( fovx / 2 ) over
    tan( fovy / 2 ) equals the aspect ratio, so P00 / P11 is its reciprocal.
    """
    p11 = 1.0
    p00 = p11 * viewport_height / viewport_width
    return p00, p11


def true_screen_normal(nx, ny, viewport_width, viewport_height):
    """Direction, in pixels, that leaves the surface perpendicular to its own
    silhouette.

    A tangent direction in eye space reaches pixels through diag( P00 * W / 2,
    P11 * H / 2 ), so a normal - a covector - reaches them through the inverse
    transpose. With square pixels and an aspect-correct frustum the two aspect
    factors cancel and what is left is the eye-space normal's own xy.
    """
    p00, p11 = projection_scales(viewport_width, viewport_height)
    vec = (nx / (p00 * viewport_width), ny / (p11 * viewport_height))
    length = math.hypot(*vec)
    return (vec[0] / length, vec[1] / length)


def outline_pixel_offset(nx, ny, viewport_width, viewport_height, width_px, normalize_in_pixel_space):
    """Mirror of glprogs/player_outline.vs, reported in pixels.

    normalize_in_pixel_space False reproduces the clip-space normalize the
    program used before, so the two can be compared directly.
    """
    p00, p11 = projection_scales(viewport_width, viewport_height)
    clip_normal = (p00 * nx, p11 * ny)

    # uOutlineParams.y / .z
    clip_per_pixel = (2.0 / viewport_width, 2.0 / viewport_height)

    if normalize_in_pixel_space:
        vec = (clip_normal[0] / clip_per_pixel[0], clip_normal[1] / clip_per_pixel[1])
    else:
        vec = clip_normal

    length = math.hypot(*vec)
    if length <= 0.0001:
        return (0.0, 0.0)
    unit = (vec[0] / length, vec[1] / length)

    # The program multiplies by w to cancel the perspective divide, so the offset
    # lands in NDC as unit * clip_per_pixel * width. NDC reaches pixels through
    # the same scale, which is what makes the width a pixel count.
    return (unit[0] * width_px, unit[1] * width_px)


def test_the_extrusion_is_the_requested_pixel_width():
    """Both the clip-space and the pixel-space normalize produce an offset of the
    requested length. That is what made the old direction error survive review:
    nothing about the thickness of the offset itself was ever wrong."""

    for viewport in ((1920, 1080), (3840, 1080), (1280, 1024)):
        for width_px in (0.5, 2.0, 6.0):
            for angle in range(0, 360, 15):
                nx = math.cos(math.radians(angle))
                ny = math.sin(math.radians(angle))
                offset = outline_pixel_offset(nx, ny, viewport[0], viewport[1], width_px, True)
                assert_close(
                    math.hypot(*offset),
                    width_px,
                    f"the shell offset must be {width_px} pixels long at {viewport} and {angle} degrees",
                    tolerance=1e-9,
                )


def test_the_extrusion_leaves_along_the_screen_normal():
    """Length is not enough. An offset that is the right length but points off
    the silhouette normal only covers width * cos( error ) of the ring, so the
    ink thins wherever the silhouette runs diagonally."""

    for viewport in ((1920, 1080), (3440, 1440), (5120, 1440), (1280, 1024)):
        for angle in range(0, 360, 9):
            nx = math.cos(math.radians(angle))
            ny = math.sin(math.radians(angle))
            expected = true_screen_normal(nx, ny, viewport[0], viewport[1])
            offset = outline_pixel_offset(nx, ny, viewport[0], viewport[1], 2.0, True)
            unit = (offset[0] / 2.0, offset[1] / 2.0)

            assert_close(
                unit[0] * expected[0] + unit[1] * expected[1],
                1.0,
                f"the shell must leave along the screen normal at {viewport} and {angle} degrees",
                tolerance=1e-9,
            )


def test_normalizing_in_clip_space_is_what_thinned_the_ring():
    """Guards the fix from being undone. Normalizing before the per-pixel scales
    tilts the push towards the short axis; the effective ring width is the offset
    projected back onto the true normal, so the loss is real ink."""

    worst = {}
    for viewport in ((1920, 1080), (3440, 1440), (5120, 1440)):
        worst[viewport] = 1.0
        for angle in range(0, 360, 3):
            nx = math.cos(math.radians(angle))
            ny = math.sin(math.radians(angle))
            expected = true_screen_normal(nx, ny, viewport[0], viewport[1])
            old = outline_pixel_offset(nx, ny, viewport[0], viewport[1], 1.0, False)
            covered = old[0] * expected[0] + old[1] * expected[1]
            worst[viewport] = min(worst[viewport], covered)

    assert_true(
        worst[(1920, 1080)] < 0.97,
        "a clip-space normalize should measurably thin the ring even at 16:9",
    )
    assert_true(
        worst[(5120, 1440)] < 0.85,
        "a clip-space normalize should cost well over a tenth of the width at 32:9",
    )
    assert_true(
        worst[(5120, 1440)] < worst[(1920, 1080)],
        "the wider the aspect ratio, the worse a clip-space normalize gets",
    )


def test_the_shader_normalizes_where_the_pixels_are():
    program = read(GLPROGS / "player_outline.vs")

    assert_true(
        "vec2 pixelsPerClip = vec2( 1.0 / uOutlineParams.y, 1.0 / uOutlineParams.z );" in program,
        "the extrusion must convert the clip-space normal into pixels before choosing a direction",
    )
    assert_true(
        "vec2 pixelNormal = clipNormal * pixelsPerClip;" in program,
        "the direction must be taken from the pixel-space normal",
    )
    assert_true(
        "float pixelLength = length( pixelNormal );" in program,
        "the normalize must happen on the pixel-space normal, not the clip-space one",
    )
    assert_true(
        "( pixelNormal / pixelLength ) * vec2( uOutlineParams.y, uOutlineParams.z )" in program,
        "the unit pixel direction must be scaled back into clip space by the per-pixel scales",
    )
    assert_true(
        "max( clipPosition.w, 0.001 )" in program,
        "the offset must cancel the perspective divide so the width stays a pixel count",
    )


# ---------------------------------------------------------------------------
# Which surfaces may grow a shell
# ---------------------------------------------------------------------------


def test_translucent_and_depth_hacked_surfaces_never_participate():
    source = read(RENDERER / "draw_common.cpp")
    body = cxx_function_body(source, "static bool RB_PlayerVisibilityEffectsSurfaceAllowed( const drawSurf_t *surf )")

    assert_true(
        "shader->Coverage() == MC_TRANSLUCENT" in body,
        "a translucent surface leaves no depth behind for the overlays to compare against",
    )
    assert_true(
        "surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f" in body,
        "a depth hacked space compares against the wrong depths",
    )
    assert_true(
        "( surf->dsFlags & DSF_BSE_EFFECT ) != 0" in body,
        "effect surfaces are not bodies and must not be outlined",
    )
    assert_true(
        "shader->GetSort() >= SS_POST_PROCESS" in body,
        "post-process sorted surfaces run after the overlays and must be left alone",
    )


def test_alpha_tested_surfaces_still_grow_a_shell():
    """Quake 4 player bodies are alpha tested for small details and the mesh is
    still player-shaped, so the shell traces the right silhouette. Excluding
    perforated surfaces - on the reasoning that a shell samples no texture and
    would ink a whole quad, which is true of a grate and not of a character -
    measured a drop in peak outline coverage on q4dm1 from 8019 pixels to 962: it
    removed the body ring from every player and left the head and weapon."""

    source = read(RENDERER / "draw_common.cpp")

    allowed = cxx_function_body(source, "static bool RB_PlayerVisibilityOutlineShellAllowed( const drawSurf_t *surf )")
    assert_true(
        "surf->material->Coverage() != MC_TRANSLUCENT" in allowed,
        "only translucent surfaces are excluded from the shell; perforated bodies must ring",
    )
    assert_true(
        "MC_PERFORATED" not in allowed,
        "excluding perforated surfaces strips the outline off Quake 4 player bodies",
    )

    front = cxx_function_body(
        read(RENDERER / "tr_light.cpp"), "static bool R_ThroughWorldOutlineShaderAllowed( const idMaterial *shader )"
    )
    assert_true(
        "shader->Coverage() == MC_TRANSLUCENT" in front,
        "the front end must accept the same coverage the shell does, or forced entities "
        "lose the body ring the ordinary path keeps",
    )


# ---------------------------------------------------------------------------
# The two depth groups
# ---------------------------------------------------------------------------


def test_see_through_and_depth_tested_outlines_get_separate_masks():
    """A see-through body marks every pixel it covers, occluded ones included. On
    one shared mask a teammate behind a wall erases the ring off an enemy in
    front of it, and teammate outlines are always see-through, so any team mode
    with both outlines on hits this."""

    source = read(RENDERER / "draw_common.cpp")
    group = cxx_function_body(source, "static bool RB_PlayerVisibilityDrawOutlineGroup(")

    assert_true(
        "glClear( GL_STENCIL_BUFFER_BIT );" in group,
        "each group must start from a mask bit of its own",
    )

    mask_list = cxx_function_body(
        source, "static void RB_PlayerVisibilityMaskOutlineList( drawSurf_t **surfs, int numSurfs, const bool seeThroughGroup )"
    )
    assert_true(
        "if ( !seeThroughGroup && RB_PlayerVisibilityIsSeeThrough( surf->space->entityDef->parms ) ) {" in mask_list,
        "the depth tested group must not record see-through silhouettes in its mask",
    )

    shell_list = cxx_function_body(source, "static bool RB_PlayerVisibilityDrawOutlineList(")
    assert_true(
        "if ( RB_PlayerVisibilityIsSeeThrough( surf->space->entityDef->parms ) != seeThroughGroup ) {" in shell_list,
        "each group must draw only its own shells, so no surface is inked twice",
    )

    mask_call = group.find("RB_PlayerVisibilityMaskOutlineList( drawSurfs, numDrawSurfs, seeThroughGroup );")
    shell_call = group.find("RB_PlayerVisibilityDrawOutlineList( drawSurfs, numDrawSurfs, seeThroughGroup,")
    assert_true(
        0 <= mask_call < shell_call,
        "the mask must be built before the shells that read it",
    )
    # Both lists have to go through both stages, or a forced surface either inks
    # over the body it belongs to or never gets masked at all.
    assert_true(
        "RB_PlayerVisibilityMaskOutlineList( outlineOnly, numOutlineOnly, seeThroughGroup );" in group,
        "forced surfaces must contribute to the silhouette mask",
    )
    assert_true(
        "RB_PlayerVisibilityDrawOutlineList( outlineOnly, numOutlineOnly, seeThroughGroup," in group,
        "forced surfaces must have their shells drawn",
    )


def test_the_depth_tested_group_is_drawn_first():
    source = read(RENDERER / "draw_common.cpp")
    body = cxx_function_body(
        source,
        "static bool RB_PlayerVisibilityDrawOutlinePass( drawSurf_t **drawSurfs, int numDrawSurfs, const rbPlayerVisibilityWork_t &work )",
    )

    depth_tested = body.find("work.outlineDepthTested")
    see_through = body.find("work.outlineSeeThrough")
    assert_true(depth_tested >= 0, "the pass must gate the depth tested group on gathered work")
    assert_true(see_through >= 0, "the pass must gate the see-through group on gathered work")
    assert_true(
        depth_tested < see_through,
        "the see-through group must land on top; it is the one meant to cut through geometry",
    )

    assert_true(
        "const bool maskSilhouette = glConfig.stencilBits > 0;" in body,
        "the shell must still draw without a stencil buffer, just without the mask",
    )
    assert_true(
        "R_ValidateGLSLProgram( &rbPlayerOutlineStage )" in body,
        "the shell must fall back to the fixed-function hull when GLSL is unavailable",
    )


def test_gathered_work_separates_the_two_groups():
    source = read(RENDERER / "draw_common.cpp")
    body = cxx_function_body(
        source,
        "static bool RB_PlayerVisibilityGatherWork( drawSurf_t **drawSurfs, int numDrawSurfs, rbPlayerVisibilityWork_t &work )",
    )

    assert_true(
        "work.outlineSeeThrough = true;" in body and "work.outlineDepthTested = true;" in body,
        "the scan must record which groups have work, or a group runs a stencil clear for nothing",
    )
    assert_true(
        "return work.brightSkin || work.rimlight || work.outlineDepthTested || work.outlineSeeThrough;" in body,
        "a view with no overlays at all must report no work and touch no GL state",
    )


# ---------------------------------------------------------------------------
# What the see-through flag covers
# ---------------------------------------------------------------------------


def test_only_the_outline_reads_through_geometry():
    """A ring says "someone is there". A rimlight or a brightskin drawn through a
    wall shades the whole body and turns an ally marker into a wallhack, so the
    see-through flag governs the outline and nothing else."""

    source = read(RENDERER / "draw_common.cpp")

    for signature, effect in (
        ("static bool RB_PlayerVisibilityDrawRimlightSurface( const drawSurf_t *surf )", "rimlight"),
        ("static bool RB_PlayerVisibilityDrawBrightSkinSurface( const drawSurf_t *surf )", "brightskin"),
    ):
        body = cxx_function_body(source, signature)
        assert_true(
            "GLS_DEPTHFUNC_EQUAL" in body,
            f"the {effect} must only paint the fragments the scene actually shows",
        )
        assert_true(
            "GLS_DEPTHFUNC_ALWAYS" not in body,
            f"the {effect} must never drop its depth test",
        )
        assert_true(
            "RB_PlayerVisibilityIsSeeThrough" not in body,
            f"the {effect} must not follow the see-through flag",
        )

    rimlight = cxx_function_body(
        source, "static bool RB_PlayerVisibilityDrawRimlightSurface( const drawSurf_t *surf )"
    )
    assert_true(
        "GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE" in rimlight,
        "the rimlight is additive; it must not start blending against the frame",
    )


def test_the_see_through_flag_has_one_reader():
    source = read(RENDERER / "draw_common.cpp")
    reader = cxx_function_body(source, "static bool RB_PlayerVisibilityIsSeeThrough( const renderEntity_t &renderEntity )")
    assert_true(
        "renderEntity.outlineFlags & ( REF_OUTLINE_NODEPTH | REF_OUTLINE_THROUGH_WORLD )" in reader,
        "the see-through helper must read the flags the game sets",
    )
    assert_true(
        source.count("outlineFlags & ( REF_OUTLINE_NODEPTH" ) == 1,
        "the flags must be tested through the helper only, so every effect agrees on what they mean",
    )


# ---------------------------------------------------------------------------
# Widths and rimlight shaping
# ---------------------------------------------------------------------------


def test_the_shared_shell_helper_does_not_impose_the_player_width_ladder():
    """The cel outline shares the shell helpers and runs a wider ladder. A clamp
    inside the shared helper held its fixed-function fallback to 6 pixels while
    the GLSL path honoured all 8, which reads as the width cvar going deaf on
    old drivers."""

    source = read(RENDERER / "draw_common.cpp")
    scale = cxx_function_body(
        source,
        "static float RB_PlayerVisibilityOutlineScale( const drawSurf_t *surf, const float requestedWidth )",
    )

    assert_true(
        "const float width = Max( 0.0f, requestedWidth );" in scale,
        "the shared scale helper must take the width already clamped by its caller",
    )
    assert_true(
        "6.0f" not in scale,
        "the shared scale helper must not re-impose the player outline ceiling",
    )

    width_helper = cxx_function_body(
        source, "static float RB_PlayerVisibilityOutlineWidth( const renderEntity_t &renderEntity )"
    )
    assert_true(
        "RB_PLAYER_OUTLINE_MIN_WIDTH, RB_PLAYER_OUTLINE_MAX_WIDTH" in width_helper,
        "the player ladder must be named, not spelled out at each use",
    )

    assert_true(
        f"static const float RB_PLAYER_OUTLINE_MIN_WIDTH = {PLAYER_OUTLINE_MIN_WIDTH}f;" in source,
        "the player outline floor must match cl_player_outline_width",
    )
    assert_true(
        f"static const float RB_PLAYER_OUTLINE_MAX_WIDTH = {PLAYER_OUTLINE_MAX_WIDTH}f;" in source,
        "the player outline ceiling must match cl_player_outline_width",
    )

    uniforms = cxx_function_body(
        source, "static void RB_PlayerVisibilitySetOutlineUniforms( const renderEntity_t &renderEntity )"
    )
    assert_true(
        "RB_PlayerVisibilityOutlineWidth( renderEntity )" in uniforms,
        "the GLSL path and the fallback must clamp the width the same way",
    )


def test_the_rimlight_shape_is_tunable_and_defaults_to_the_old_look():
    source = read(RENDERER / "draw_common.cpp")
    body = cxx_function_body(
        source,
        "static void RB_PlayerVisibilitySetRimlightUniforms( const drawSurf_t *surf, const renderEntity_t &renderEntity )",
    )

    assert_true(
        "r_playerRimlightPower.GetFloat()" in body,
        "the falloff exponent must come from the cvar, not from a literal in the pass",
    )
    assert_true(
        "r_playerRimlightFloor.GetFloat()" in body,
        "the rim floor must come from the cvar",
    )
    assert_true(
        "RB_PLAYER_RIMLIGHT_MIN_POWER, RB_PLAYER_RIMLIGHT_MAX_POWER" in body,
        "the pass must clamp the exponent even though the cvar carries a range",
    )
    assert_true(
        "RB_PLAYER_RIMLIGHT_MIN_FLOOR, RB_PLAYER_RIMLIGHT_MAX_FLOOR" in body,
        "the pass must clamp the floor even though the cvar carries a range",
    )

    header = read(RENDERER / "tr_local.h")
    for name, value in (
        ("RB_PLAYER_RIMLIGHT_MIN_POWER", RIMLIGHT_MIN_POWER),
        ("RB_PLAYER_RIMLIGHT_MAX_POWER", RIMLIGHT_MAX_POWER),
        ("RB_PLAYER_RIMLIGHT_MIN_FLOOR", RIMLIGHT_MIN_FLOOR),
        ("RB_PLAYER_RIMLIGHT_MAX_FLOOR", RIMLIGHT_MAX_FLOOR),
    ):
        assert_true(
            f"const float {name} = {value}f;" in header,
            f"{name} must stay on the ladder this test mirrors",
        )

    init = read(RENDERER / "RenderSystem_init.cpp")
    assert_true(
        'idCVar r_playerRimlightPower( "r_playerRimlightPower", "2.0"' in init,
        "the default exponent must reproduce the squared falloff the pass used to hard-code",
    )
    assert_true(
        'idCVar r_playerRimlightFloor( "r_playerRimlightFloor", "0"' in init,
        "the default floor must leave the rim shape unchanged",
    )
    for cvar in ("r_playerRimlightPower", "r_playerRimlightFloor"):
        line = next(line for line in init.splitlines() if f'idCVar {cvar}(' in line)
        assert_true("CVAR_ARCHIVE" in line, f"{cvar} must persist; it is a look preference")
        assert_true(
            "RB_PLAYER_RIMLIGHT_MIN" in line and "RB_PLAYER_RIMLIGHT_MAX" in line,
            f"{cvar} must carry the shared range so the console rejects nonsense",
        )


def test_the_rimlight_program_applies_the_floor_to_the_rim_term():
    """Lifting the final colour instead would punch a fixed wash through a
    rimlight the player asked to be faint, since the entity's strength is what
    the alpha carries."""

    program = read(GLPROGS / "player_rimlight.fs")

    assert_true(
        "rim = pow( max( rim, 0.0 ), max( uRimParams.x, 0.001 ) );" in program,
        "the exponent must be applied to the rim term and guarded against zero",
    )
    assert_true(
        "rim = clamp( rim + uRimParams.z, 0.0, 1.0 );" in program,
        "the floor must lift the rim term, so it still scales with the entity's strength",
    )
    assert_true(
        "float contribution = clamp( uColor.a * rim * uRimParams.y, 0.0, 1.0 );" in program,
        "the entity's rimlight alpha must remain the outer strength control",
    )


# ---------------------------------------------------------------------------
# Frame integration
# ---------------------------------------------------------------------------


def test_the_pass_costs_nothing_when_there_is_nothing_to_draw():
    source = read(RENDERER / "draw_common.cpp")
    body = cxx_function_body(
        source, "static void RB_STD_DrawPlayerVisibilityEffects( drawSurf_t **drawSurfs, int numDrawSurfs )"
    )

    gather = body.find("RB_PlayerVisibilityGatherWork( drawSurfs, numDrawSurfs, work )")
    first_gl = body.find("glMatrixMode( GL_PROJECTION );")
    assert_true(gather >= 0, "the pass must scan for work before doing anything")
    assert_true(
        0 <= gather < first_gl,
        "the scan must come before the first GL state change, or singleplayer pays for the feature",
    )
    assert_true(
        "r_skipPlayerVisibilityEffects.GetBool()" in body,
        "the whole group must stay switchable off for clean captures",
    )


def test_the_pass_hands_the_stencil_back():
    source = read(RENDERER / "draw_common.cpp")
    outline = cxx_function_body(
        source,
        "static bool RB_PlayerVisibilityDrawOutlinePass( drawSurf_t **drawSurfs, int numDrawSurfs, const rbPlayerVisibilityWork_t &work )",
    )

    assert_true(
        "glStencilMask( 0xff );" in outline,
        "the pass must give the full stencil write mask back",
    )
    assert_true(
        "glClearStencil( RB_PlayerVisibilitySafeStencilClearValue() );" in outline,
        "the pass must restore the stencil clear value the rest of the frame expects",
    )

    group = cxx_function_body(source, "static bool RB_PlayerVisibilityDrawOutlineGroup(")
    assert_true(
        "glStencilMask( RB_PLAYER_OUTLINE_STENCIL_BIT );" in group,
        "the mask must claim a single stencil bit so shadow counts survive",
    )

    safe = cxx_function_body(source, "static GLint RB_PlayerVisibilitySafeStencilClearValue( void )")
    assert_true(
        "glConfig.stencilBits" in safe,
        "the restored clear value must follow the buffer the driver actually gave us",
    )


def test_frame_order_places_the_overlays_where_they_survive():
    source = read(RENDERER / "draw_common.cpp")
    body = cxx_function_body(source, "void\tRB_STD_DrawView( void )")

    order = [
        "RB_STD_ForceAmbient();",
        "RB_STD_DrawPlayerVisibilityEffects( drawSurfs, processed );",
        "RB_STD_Bloom();",
    ]
    positions = []
    for token in order:
        index = body.find(token)
        assert_true(index >= 0, f"RB_STD_DrawView should call {token}")
        positions.append(index)

    assert_true(
        positions[0] < positions[1],
        "the overlays must be drawn after the ambient floor, or r_forceAmbient washes them out",
    )
    assert_true(
        positions[1] < positions[2],
        "the overlays must be in the frame before bloom reads it",
    )


def test_the_unavailable_rimlight_says_so_once():
    """The outline and the brightskin still draw, so the symptom is a rimlight
    strength that appears to do nothing whatsoever."""

    source = read(RENDERER / "draw_common.cpp")
    body = cxx_function_body(
        source, "static bool RB_PlayerVisibilityDrawRimlightPass( drawSurf_t **drawSurfs, int numDrawSurfs )"
    )

    assert_true(
        "static bool reportedUnavailable = false;" in body,
        "the notice must be one-shot, not once per frame",
    )
    assert_true(
        "player rimlight unavailable" in body,
        "the notice must name the effect that is missing",
    )
    report = body.find("reportedUnavailable = true;")
    bail = body.find("return false;")
    assert_true(
        0 <= report < bail,
        "the notice must be emitted before the pass gives up",
    )


# ---------------------------------------------------------------------------
# The game side: who the overlays are applied to
# ---------------------------------------------------------------------------


def test_the_game_applies_the_overlays_to_every_player_including_bots():
    """Bots occupy real client slots and are ordinary idPlayer entities, so
    nothing here may key on being a bot. Verified in a live match: with nine bots
    the diagnostic reports 'applied as enemy' for each of them."""

    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        print(f"renderer_player_visibility: skipped game checks (no GameLibs checkout at {GAME_LIBS_ROOT})")
        return

    source = read(mp / "Player.cpp")
    body = cxx_function_body(source, "void idPlayer::UpdateMultiplayerVisibilityEffects( renderEntity_t *headRenderEnt )")

    for token in ("IsBot", "isBot", "botManager.IsBot"):
        assert_true(
            token not in body,
            f"the overlay decision must not branch on {token}; a bot is just another client",
        )

    assert_true(
        "Player_SetVisibilityEffects( &renderEntity," in body
        and "Player_SetVisibilityEffects( headRenderEnt," in body
        and "Player_SetVisibilityEffects( weaponRenderEnt," in body,
        "body, head and world weapon are separate render entities and all three must be set",
    )

    # Every path out of the function has to say what it decided, or the
    # diagnostic answers some questions with silence.
    assert_true(
        body.count("Player_ReportVisibilityEffects(") == body.count("return;") + 1,
        "every early return must report its reason, and the applied path must report too",
    )


def test_the_visibility_diagnostic_does_not_repeat_itself():
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    source = read(mp / "Player.cpp")
    body = cxx_function_body(
        source,
        "static void Player_ReportVisibilityEffects( const int clientNum, const int reasonId, const char *reason )",
    )

    off = body.find("if ( !g_showPlayerVisibilityEffects.GetBool() ) {")
    reset = body.find("lastReason[clientNum] = PVR_NONE;")
    compare = body.find("if ( reasonId == lastReason[clientNum] ) {")
    assert_true(off >= 0, "the reporter must handle the diagnostic being off on its own")
    assert_true(
        off < reset < compare,
        "the last reason must be forgotten while the diagnostic is off, or turning it on says nothing",
    )
    assert_true(
        "botManager.IsBot( clientNum )" in body,
        "the report must say whether the client is a bot; that is the question it exists to answer",
    )

    assert_true(
        'idCVar g_showPlayerVisibilityEffects( "g_showPlayerVisibilityEffects", "0"' in source,
        "the diagnostic must default to off",
    )

    # Deduping on an id rather than the message text is what lets the applied case
    # vary along several axes without a literal per combination.
    for flag in (
        "PVR_APPLIED_TEAM_COLOR",
        "PVR_APPLIED_SPECTATING",
        "PVR_APPLIED_HEAD",
        "PVR_APPLIED_WEAPON",
        "PVR_APPLIED_THROUGH",
    ):
        assert_true(
            flag in source,
            f"{flag} must be part of the dedupe key, or that state change never prints",
        )


# ---------------------------------------------------------------------------
# Through-world outlines
# ---------------------------------------------------------------------------


def test_the_outline_only_list_is_read_by_the_outline_pass_alone():
    """The whole safety argument for forcing entities into a view is that their
    surfaces go somewhere no other pass looks. Such an entity is in no depth
    buffer, receives no light and casts no shadow, so the depth fill, the ambient
    pass, the interaction passes, the cel passes and the motion vectors would all
    draw it wrong - and would do so by simply not knowing about it."""

    readers = {}
    for path in sorted((RENDERER).glob("*.cpp")) + sorted((RENDERER).glob("*.h")):
        hits = read(path).count("outlineDrawSurfs")
        if hits:
            readers[path.name] = hits

    assert_true(
        set(readers) == {"tr_light.cpp", "tr_local.h", "draw_common.cpp", "FrameSnapshot.cpp"},
        "only the writer (tr_light.cpp), the declaration (tr_local.h), the outline pass "
        f"(draw_common.cpp) and the frame snapshot (FrameSnapshot.cpp) may name the list; found {sorted(readers)}",
    )

    # The render-thread frame snapshot retains the list's surfaces in place for
    # the overlapped frame; it may only copy them, never hand them to a pass.
    snapshot_uses = [
        line.strip() for line in read(RENDERER / "FrameSnapshot.cpp").splitlines() if "outlineDrawSurfs" in line
    ]
    retained = (
        "snapshot.Surface( view->outlineDrawSurfs[i]",
        "prepareMaterial( view->outlineDrawSurfs[i]->material )",
    )
    assert_true(
        all(any(use in line for use in retained) for line in snapshot_uses),
        f"the frame snapshot may only retain outline-only surfaces, not route them; found {snapshot_uses}",
    )

    source = read(RENDERER / "draw_common.cpp")
    accessor = cxx_function_body(
        source, "static drawSurf_t **RB_PlayerVisibilityOutlineOnlySurfaces( int &count )"
    )
    assert_true(
        "backEnd.viewDef->outlineDrawSurfs == NULL" in accessor,
        "a view that never built the list must read as empty, not as garbage",
    )
    assert_true(
        source.count("backEnd.viewDef->outlineDrawSurfs") == 2,
        "the backend must reach the list through the one accessor",
    )


def test_forced_entities_stay_out_of_every_light_list():
    """ambientViewCount is the flag light interactions test to decide whether a
    surface is visible this view. Leaving it alone is what keeps an entity that
    exists only for its ring out of every light list, so it must stay unset."""

    body = cxx_function_body(
        read(RENDERER / "tr_light.cpp"), "static void R_AddThroughWorldOutlineEntity( idRenderEntityLocal *def )"
    )

    assert_true(
        "ambientViewCount" in body,
        "the omission must be commented, or a later edit adds the assignment back as an oversight",
    )
    assert_true(
        "tri->ambientViewCount = tr.viewCount" not in body,
        "setting ambientViewCount would put a through-world entity into the light lists",
    )
    assert_true(
        "R_CreateAmbientCache( tri, false )" in body,
        "the shell needs positions and normals, never lighting vectors",
    )


def test_only_occlusion_is_defeated_not_the_frustum():
    body = cxx_function_body(
        read(RENDERER / "tr_light.cpp"),
        "static bool R_ThroughWorldOutlineEntityAllowed( const idRenderEntityLocal *def )",
    )

    assert_true(
        "R_CullLocalBox( def->referenceBounds, def->modelMatrix, 5, tr.viewDef->frustum )" in body,
        "off screen must stay off screen; only occlusion is being defeated",
    )
    assert_true(
        "def->viewCount == tr.viewCount" in body,
        "an entity the portal walk already reached must not be added twice",
    )
    assert_true(
        "suppressSurfaceInViewID" in body and "allowSurfaceInViewID" in body,
        "the same view-id suppression the portal walk honours, or a player rings itself",
    )
    assert_true(
        "outlineColor[3] <= 0.0f" in body,
        "the game clears the colour rather than the flag, so an invisible ring must cost nothing",
    )


def test_the_forced_entity_registry_cannot_go_stale():
    """A handle left in the registry would put a ring around whatever entity next
    reused the slot."""

    world = read(RENDERER / "RenderWorld.cpp")
    tracker = cxx_function_body(
        world, "void idRenderWorldLocal::TrackThroughWorldOutlineEntity( qhandle_t entityHandle, int outlineFlags )"
    )
    assert_true(
        "REF_OUTLINE_THROUGH_WORLD" in tracker and "RemoveIndex" in tracker,
        "the tracker must both add and remove",
    )

    update = cxx_function_body(
        world, "void idRenderWorldLocal::UpdateEntityDef( qhandle_t entityHandle, const renderEntity_t *re )"
    )
    track_at = update.find("TrackThroughWorldOutlineEntity( entityHandle, re->outlineFlags )")
    first_return = update.find("return;")
    assert_true(
        0 <= track_at,
        "entity updates must keep the registry in step",
    )
    assert_true(
        track_at < update.find("if ( !re->forceUpdate ) {"),
        "tracking must precede the early-out paths, or a flag change on an otherwise "
        "unchanged entity is never recorded",
    )

    free_body = cxx_function_body(world, "void idRenderWorldLocal::FreeEntityDef( qhandle_t entityHandle )")
    assert_true(
        "TrackThroughWorldOutlineEntity( entityHandle, 0 )" in free_body,
        "freeing an entity must drop its handle before the slot is reused",
    )
    assert_true(
        "throughWorldOutlineEntities.Clear();" in read(RENDERER / "RenderWorld_load.cpp"),
        "a map change must not carry handles into the next map",
    )


def test_through_world_implies_no_depth_test():
    source = read(RENDERER / "draw_common.cpp")
    helper = cxx_function_body(
        source, "static bool RB_PlayerVisibilityIsSeeThrough( const renderEntity_t &renderEntity )"
    )
    assert_true(
        "REF_OUTLINE_NODEPTH | REF_OUTLINE_THROUGH_WORLD" in helper,
        "the renderer must draw the implication itself rather than trusting the game to set both",
    )

    group = cxx_function_body(source, "static bool RB_PlayerVisibilityDrawOutlineGroup(")
    assert_true(
        "if ( seeThroughGroup && maskSilhouette ) {" in group,
        "forced surfaces are see-through only, and need a stencil buffer to mask against",
    )
    assert_true(
        "RB_PlayerVisibilityOutlineOnlySurfaces( numOutlineOnly )" in group,
        "the see-through group must pick up the forced surfaces",
    )


def test_the_view_build_adds_forced_outlines_last():
    body = cxx_function_body(read(RENDERER / "tr_main.cpp"), "void R_RenderView( viewDef_t *parms ) {")

    order = [
        "R_AddModelSurfaces();",
        "R_AddEffectSurfaces();",
        "R_AddThroughWorldOutlines();",
        "R_SortDrawSurfs();",
    ]
    positions = []
    for token in order:
        index = body.find(token)
        assert_true(index >= 0, f"R_RenderView should call {token}")
        positions.append(index)

    assert_true(
        positions[0] < positions[2] and positions[1] < positions[2],
        "forced entities must be added after every pass that walks viewEntitys or builds "
        "interactions, so none of them can pick the entities up",
    )
    assert_true(
        positions[2] < positions[3],
        "the sort must not see the forced surfaces; they are on their own list",
    )


# ---------------------------------------------------------------------------
# The game side: spectators, colours and replication
# ---------------------------------------------------------------------------


def test_spectators_see_every_player_through_the_world():
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    source = read(mp / "Player.cpp")
    body = cxx_function_body(source, "void idPlayer::UpdateMultiplayerVisibilityEffects( renderEntity_t *headRenderEnt )")

    assert_true(
        "const bool throughWorld = teamColor || spectatorView;" in body,
        "allies always, and everyone once spectating, get a ring wherever they are",
    )
    assert_true(
        "REF_OUTLINE_NODEPTH | REF_OUTLINE_THROUGH_WORLD" in body,
        "the through-world outline must carry both flags",
    )
    # A free spectator used to be dropped outright.
    assert_true(
        "viewer is a free spectator" not in body,
        "a free spectator is exactly who most needs to see everyone, not a reason to skip",
    )
    assert_true(
        "!spectatorView && reference->health <= 0" in body,
        "a live viewer must be alive to be shown anything; a spectator always is",
    )

    reference = cxx_function_body(
        source, "static idPlayer *Player_VisibilityReference( idPlayer *localViewer, bool &spectatorView )"
    )
    assert_true(
        "localViewer->spectator != localViewer->entityNumber" in reference,
        "following yourself is free flight, not a followed player",
    )
    assert_true(
        "!followed->spectating" in reference,
        "a followed player who is itself spectating gives no side to borrow",
    )


def test_spectator_colours_mean_something_in_team_games():
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    source = read(mp / "Player.cpp")
    colour = cxx_function_body(
        source,
        "static bool Player_VisibilityUsesTeamColor( const idPlayer *reference, const idPlayer *subject )",
    )
    assert_true(
        "return reference->team == subject->team;" in colour,
        "with a reference player it stays the ordinary ally-or-opponent question",
    )
    assert_true(
        "return subject->team == TEAM_MARINE;" in colour,
        "free flight has no side, so the two colours become the two teams, keyed on the "
        "team index so the assignment holds for a whole match",
    )
    assert_true(
        "if ( !gameLocal.IsTeamGame() ) {" in colour,
        "outside a team game there are no sides and everyone reads as an opponent",
    )

    strength = cxx_function_body(
        source,
        "static float Player_VisibilityStrength( const bool spectatorView, const bool teamColor,",
    )
    assert_true(
        "if ( spectatorView ) {" in strength and "Max( teamValue, enemyValue )" in strength,
        "a spectator watching the whole match must not depend on which of a player's two "
        "side-specific cvars happens to be set",
    )


def test_pvs_replication_is_exempted_only_where_the_viewer_is_entitled():
    """The PVS gate is the only thing stopping every client from being handed every
    enemy position. Widening it for allies and spectators is the feature; widening
    it for opponents would make a wallhack a matter of reading packets."""

    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    source = read(mp / "Game_network.cpp")
    body = cxx_function_body(
        source,
        "bool idGameLocal::SnapshotExemptFromPVS( const idEntity *ent, bool exemptAllPlayers, int exemptTeam ) const",
    )

    assert_true(
        "!ent->IsType( idPlayer::GetClassType() )" in body,
        "only players are exempt; the gate still applies to everything else in the map",
    )
    assert_true(
        "if ( exemptAllPlayers ) {" in body,
        "a spectator watches the whole match",
    )
    assert_true(
        "IsTeamGame() && exemptTeam != TEAM_NONE && other->team == exemptTeam" in body,
        "a live player is entitled to its own team and to nothing more",
    )
    assert_true(
        "other->spectating || other->health <= 0" in body,
        "a player with no body to mark is not worth the bandwidth",
    )

    server = cxx_function_body(
        source,
        "void idGameLocal::ServerWriteSnapshot( int clientNum, int sequence, idBitMsg &msg, dword *clientInPVS, int numPVSClients, int lastSnapshotFrame )",
    )
    assert_true(
        "const bool pvsExemptAllPlayers = player->spectating;" in server,
        "the spectator exemption must key on the recipient, not on the player being written",
    )
    assert_true(
        "spectated->team : TEAM_NONE" in server,
        "a follow spectator must see what the player it follows would",
    )


def main():
    tests = [
        test_the_extrusion_is_the_requested_pixel_width,
        test_the_extrusion_leaves_along_the_screen_normal,
        test_normalizing_in_clip_space_is_what_thinned_the_ring,
        test_the_shader_normalizes_where_the_pixels_are,
        test_translucent_and_depth_hacked_surfaces_never_participate,
        test_alpha_tested_surfaces_still_grow_a_shell,
        test_see_through_and_depth_tested_outlines_get_separate_masks,
        test_the_depth_tested_group_is_drawn_first,
        test_gathered_work_separates_the_two_groups,
        test_only_the_outline_reads_through_geometry,
        test_the_see_through_flag_has_one_reader,
        test_the_shared_shell_helper_does_not_impose_the_player_width_ladder,
        test_the_rimlight_shape_is_tunable_and_defaults_to_the_old_look,
        test_the_rimlight_program_applies_the_floor_to_the_rim_term,
        test_the_pass_costs_nothing_when_there_is_nothing_to_draw,
        test_the_pass_hands_the_stencil_back,
        test_frame_order_places_the_overlays_where_they_survive,
        test_the_unavailable_rimlight_says_so_once,
        test_the_game_applies_the_overlays_to_every_player_including_bots,
        test_the_visibility_diagnostic_does_not_repeat_itself,
        test_the_outline_only_list_is_read_by_the_outline_pass_alone,
        test_forced_entities_stay_out_of_every_light_list,
        test_only_occlusion_is_defeated_not_the_frustum,
        test_the_forced_entity_registry_cannot_go_stale,
        test_through_world_implies_no_depth_test,
        test_the_view_build_adds_forced_outlines_last,
        test_spectators_see_every_player_through_the_world,
        test_spectator_colours_mean_something_in_team_games,
        test_pvs_replication_is_exempted_only_where_the_viewer_is_entitled,
    ]

    for test in tests:
        test()

    print("renderer_player_visibility: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

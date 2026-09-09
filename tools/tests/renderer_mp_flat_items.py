#!/usr/bin/env python3
"""Focused contracts for multiplayer flat-shaded items and world weapons.

The feature crosses a deliberately narrow renderer/game ABI: the game tags a
render entity with an icon-derived RGB colour, while the renderer substitutes
only the lit diffuse image and optionally carries a soft upward lightness band.
These checks pin the arithmetic, every shipping lighting path, the versioned
ABI, legacy simple-item compatibility, live style changes, and the two menu
controls without needing retail assets.
"""

from __future__ import annotations

import math
import os
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "src" / "renderer"
GLPROGS = ROOT / "content" / "baseoq4" / "pak0" / "glprogs"
VK_SHADERS = RENDERER / "Vulkan" / "shaders"
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")
).resolve()

GL_INTERACTIONS = (
    "material_interaction",
    "shadow_interaction",
    "shadow_point_interaction",
)
VK_INTERACTIONS = (
    "interaction",
    "interaction_shadow",
    "interaction_shadow_point",
)
GAME_COPIES = ("game", "mpgame")

SWEEP_STRENGTH = 0.30
SWEEP_CYCLES_PER_SECOND = 0.28
SWEEP_INNER_EDGE = 0.045
SWEEP_OUTER_EDGE = 0.16


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required contract source is missing: {path}")
    return path.read_text(encoding="utf-8")


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def require(source: str, token: str, message: str):
    assert_true(token in source, message)


def reject(source: str, token: str, message: str):
    assert_true(token not in source, message)


def compact(source: str) -> str:
    return re.sub(r"\s+", "", source)


def delimited_body(source: str, marker: str, opener: str = "{", closer: str = "}") -> str:
    start = source.find(marker)
    assert_true(start >= 0, f"{marker} should exist")

    open_at = source.find(opener, start + len(marker))
    assert_true(open_at >= 0, f"{marker} should have a {opener}{closer} body")

    depth = 0
    for index in range(open_at, len(source)):
        char = source[index]
        if char == opener:
            depth += 1
        elif char == closer:
            depth -= 1
            if depth == 0:
                return source[open_at + 1 : index]

    raise AssertionError(f"{marker} should have a closed {opener}{closer} body")


def cxx_function_body(source: str, signature: str) -> str:
    return delimited_body(source, signature)


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def sweep_phase(time_seconds: float) -> float:
    unwrapped = time_seconds * SWEEP_CYCLES_PER_SECOND
    return unwrapped - math.floor(unwrapped)


def sweep_band_at_phase(height: float, phase: float) -> float:
    height = clamp(height, 0.0, 1.0)
    phase = phase - math.floor(phase)
    distance = abs(height - phase)
    distance = min(distance, 1.0 - distance)
    return 1.0 - smoothstep(SWEEP_INNER_EDGE, SWEEP_OUTER_EDGE, distance)


def lift_toward_white(rgb: tuple[float, float, float], band: float) -> tuple[float, float, float]:
    amount = SWEEP_STRENGTH * clamp(band, 0.0, 1.0)
    return tuple(channel + (1.0 - channel) * amount for channel in rgb)


def test_upward_cyclic_soft_band_math_and_true_lightness():
    period = 1.0 / SWEEP_CYCLES_PER_SECOND
    for time_seconds in (0.0, 0.2, 1.0, 12.5):
        assert_true(
            math.isclose(sweep_phase(time_seconds), sweep_phase(time_seconds + period), abs_tol=1.0e-9),
            "the sweep phase must repeat exactly once per cycle",
        )
        assert_true(0.0 <= sweep_phase(time_seconds) < 1.0, "the phase must stay normalized")

    lower_phase = 0.20
    upper_phase = 0.55
    assert_true(
        sweep_phase(lower_phase / SWEEP_CYCLES_PER_SECOND)
        < sweep_phase(upper_phase / SWEEP_CYCLES_PER_SECOND),
        "positive time must move the band from low model-local Z toward high model-local Z",
    )
    assert_true(
        sweep_band_at_phase(lower_phase, lower_phase) == 1.0
        and sweep_band_at_phase(upper_phase, lower_phase) == 0.0,
        "the earlier phase must peak at the lower height",
    )
    assert_true(
        sweep_band_at_phase(upper_phase, upper_phase) == 1.0
        and sweep_band_at_phase(lower_phase, upper_phase) == 0.0,
        "the later phase must peak at the higher height",
    )

    assert_true(
        math.isclose(
            sweep_band_at_phase(0.99, 0.01),
            sweep_band_at_phase(0.01, 0.99),
            abs_tol=1.0e-12,
        ),
        "the band must cross the top/bottom seam without a one-frame reset",
    )
    assert_true(
        sweep_band_at_phase(0.0, 0.0) == 1.0 and sweep_band_at_phase(1.0, 0.0) == 1.0,
        "normalized height endpoints must meet at the cyclic seam",
    )

    distances = [index / 1000.0 for index in range(0, 251)]
    samples = [sweep_band_at_phase(distance, 0.0) for distance in distances]
    assert_true(all(0.0 <= sample <= 1.0 for sample in samples), "the soft band must stay bounded")
    assert_true(
        all(left + 1.0e-12 >= right for left, right in zip(samples, samples[1:])),
        "the band must ease monotonically away from its centre",
    )
    assert_true(
        sweep_band_at_phase(SWEEP_INNER_EDGE, 0.0) == 1.0,
        "the band must retain a small tasteful full-strength centre",
    )
    assert_true(
        0.0 < sweep_band_at_phase((SWEEP_INNER_EDGE + SWEEP_OUTER_EDGE) * 0.5, 0.0) < 1.0,
        "the band edge must be feathered rather than hard-stepped",
    )
    assert_true(
        sweep_band_at_phase(SWEEP_OUTER_EDGE, 0.0) == 0.0,
        "the band must fade completely by its outer edge",
    )

    saturated_red = lift_toward_white((1.0, 0.0, 0.0), 1.0)
    assert_true(
        saturated_red == (1.0, SWEEP_STRENGTH, SWEEP_STRENGTH),
        "the cue must lift saturated icon colours toward white, not multiply unchanged channels",
    )
    for original in ((0.0, 0.2, 0.8), (1.0, 0.0, 0.0), (1.0, 1.0, 1.0)):
        lifted = lift_toward_white(original, 1.0)
        assert_true(
            all(before <= after <= 1.0 for before, after in zip(original, lifted)),
            "the lightness cue must never darken or overbright an icon colour",
        )


def test_cpu_diffuse_gate_preserves_alpha_layers_and_excludes_view_weapons():
    source = read(RENDERER / "FlatDiffuse.cpp")

    active = cxx_function_body(source, "bool RB_FlatDiffuseSurfaceActive( const drawSurf_t *surf )")
    require(
        active,
        "!surf->space->weaponDepthHack",
        "the back end must reject the depth-hacked first-person weapon",
    )
    require(active, "REF_FLAT_DIFFUSE", "flat diffuse must be an explicit per-entity renderer flag")

    sweep = cxx_function_body(source, "bool RB_FlatDiffuseSweepActive( const drawSurf_t *surf )")
    require(sweep, "REF_FLAT_DIFFUSE_SWEEP", "the moving band must have its own opt-in flag")
    require(
        sweep,
        "surf->space->flatDiffuseInvHeight > 0.0f",
        "a sweep without valid local-height bounds must stay disabled",
    )

    params = cxx_function_body(
        source, "void RB_GetFlatDiffuseParams( const drawSurf_t *surf, idVec4 &params )"
    )
    require(params, "params.Zero();", "disabled and held-weapon sweeps must upload zero strength")
    require(
        params,
        "FLAT_DIFFUSE_SWEEP_STRENGTH = 0.30f",
        "the CPU strength must match the arithmetic contract",
    )
    require(
        params,
        "FLAT_DIFFUSE_SWEEP_CYCLES_PER_SECOND = 0.28f",
        "the CPU speed must match the arithmetic contract",
    )
    require(params, "phase -= idMath::Floor( phase );", "the CPU must wrap phase cyclically")

    apply = cxx_function_body(
        source,
        "void RB_ApplyFlatDiffuseStage( const drawSurf_t *surf, idImage **diffuseImage, "
        "float diffuseColor[4], idVec4 &params )",
    )
    require(
        apply,
        "*diffuseImage = globalImages->whiteImage;",
        "flat colour must replace only the lit diffuse texture",
    )
    require(
        apply,
        "component < 3",
        "the flat-colour loop must be restricted to RGB so authored alpha survives",
    )
    require(
        apply,
        "diffuseColor[component] *= flatColor;",
        "the entity's icon colour must tint the diffuse RGB",
    )
    require(
        apply,
        "*diffuseImage == globalImages->blackImage",
        "materials with no diffuse contribution must not acquire one",
    )
    reject(apply, "diffuseColor[3]", "flat shading must not overwrite stage alpha")
    reject(apply, "specular", "flat shading must not alter the specular layer")
    reject(apply, "bump", "flat shading must not alter the bump layer")

    interactions = read(RENDERER / "tr_render.cpp")
    interaction_body = cxx_function_body(
        interactions,
        "void RB_CreateSingleDrawInteractionsFiltered( const drawSurf_t *surf, "
        "void (*DrawInteraction)(const drawInteraction_t *), "
        "drawInteractionStageFilter_t StageFilter )",
    )
    diffuse_at = interaction_body.find("case SL_DIFFUSE:")
    apply_at = interaction_body.find("RB_ApplyFlatDiffuseStage(")
    specular_at = interaction_body.find("case SL_SPECULAR:")
    assert_true(
        0 <= diffuse_at < apply_at < specular_at,
        "the CPU substitution must occur inside the diffuse case before the separate specular case",
    )
    assert_true(
        interaction_body.count("RB_ApplyFlatDiffuseStage(") == 1,
        "the standard interaction decomposition must not apply flat shading to another stage",
    )

    front_end = read(RENDERER / "tr_light.cpp")
    view_entity = cxx_function_body(
        front_end, "viewEntity_t *R_SetEntityDefViewEntity( idRenderEntityLocal *def )"
    )
    require(
        view_entity,
        "def->parms.weaponDepthHackInViewID != 0 || def->parms.allowSurfaceInViewID != 0",
        "view-only render entities must be rejected even in mirrors and render demos",
    )
    guard = delimited_body(
        view_entity,
        "if ( def->parms.weaponDepthHackInViewID != 0 || "
        "def->parms.allowSurfaceInViewID != 0 )",
    )
    require(guard, "vModel->flatDiffuseColor.Zero();", "view-only entities must lose their flat colour")
    require(guard, "vModel->flatDiffuseFlags = 0;", "view-only entities must lose both flat flags")

    for name in ("ModernGLDrawPlan.cpp", "ModernGLExecutor.cpp"):
        modern = read(RENDERER / name)
        require(
            modern,
            "RB_FlatDiffuseSurfaceActive( draw.legacyDrawSurf )",
            f"{name} must leave tagged materials on the stage-faithful legacy owner",
        )
        require(
            modern,
            "flat-diffuse-legacy" if name == "ModernGLExecutor.cpp" else "materialFallbackDraws",
            f"{name} must explicitly record the compatibility fallback",
        )


def test_gl_interactions_share_the_diffuse_only_lightness_lift():
    normalized_bodies = {}
    expected_lift = "returnmix(diffuse,vec3(1.0),uFlatDiffuseParams.x*band);"

    for stem in GL_INTERACTIONS:
        fragment_name = f"{stem}.fs"
        fragment = read(GLPROGS / fragment_name)
        require(
            fragment,
            "uniform vec4 uFlatDiffuseParams;",
            f"{fragment_name} must receive the per-surface sweep parameters",
        )
        sweep = cxx_function_body(fragment, "vec3 ApplyFlatDiffuseSweep( vec3 diffuse, float localZ )")
        require(
            compact(sweep),
            expected_lift,
            f"{fragment_name} must lift diffuse lightness toward white",
        )
        reject(
            compact(sweep),
            "diffuse*(1.0+",
            f"{fragment_name} must not use multiplicative brightening that disappears on saturated RGB",
        )
        reject(sweep, "specular", f"{fragment_name}'s sweep helper must be diffuse-only")
        normalized_bodies[fragment_name] = compact(sweep)

        diffuse_at = fragment.find("vec3 diffuse = texture2D(")
        sweep_at = fragment.find("diffuse = ApplyFlatDiffuseSweep(", diffuse_at)
        specular_at = fragment.find("vec3 specularSample =", sweep_at)
        assert_true(
            0 <= diffuse_at < sweep_at < specular_at,
            f"{fragment_name} must sweep diffuse before sampling the untouched specular layer",
        )

        vertex_name = f"{stem}.vs"
        vertex = compact(read(GLPROGS / vertex_name))
        require(
            vertex,
            "vLightFalloffTexCoord=vec4(dot(position,uLightFalloffS),0.5,position.z,1.0);",
            f"{vertex_name} must carry model-local Z to the fragment without another varying",
        )

    assert_true(
        len(set(normalized_bodies.values())) == 1,
        "all three OpenGL interaction families must carry one identical sweep",
    )

    backend = read(RENDERER / "draw_arb2.cpp")
    backend_compact = compact(backend)
    for program in (
        "g_materialInteractionProgram",
        "g_shadowMapProgram",
        "g_pointShadowMapProgram",
    ):
        require(
            backend_compact,
            f'{program}.flatDiffuseParams=glGetUniformLocationARB(programObject,"uFlatDiffuseParams");',
            f"{program} must resolve the flat-diffuse uniform",
        )
        require(
            backend_compact,
            f"glUniform4fvARB({program}.flatDiffuseParams,1,din->flatDiffuseParams.ToFloatPtr());",
            f"{program} must refresh the flat-diffuse uniform per interaction",
        )

    material_path = cxx_function_body(
        backend, "static void RB_DrawMaterialInteractions( const drawSurf_t *surf )"
    )
    require(
        material_path,
        "RB_DrawSurfChainNeedsFlatDiffuseSweep( surf )",
        "a swept item must select the GLSL interaction path even when enhanced materials are off",
    )
    require(
        material_path,
        "RB_FlatDiffuseSweepActive( &singleSurf )",
        "mixed light chains must choose GLSL only for surfaces that need the moving band",
    )


def test_light_grid_uses_the_same_diffuse_only_sweep():
    fragment = read(GLPROGS / "lightgrid_indirect.fs")
    vertex = read(GLPROGS / "lightgrid_indirect.vs")

    require(fragment, "uniform vec4 uFlatDiffuseParams;", "light-grid shading must receive sweep parameters")
    require(vertex, "varying float vLocalZ;", "the light-grid vertex stage must expose local height")
    require(vertex, "vLocalZ = gl_Vertex.z;", "the light-grid height must stay in model-local space")

    light_grid_sweep = cxx_function_body(
        fragment, "vec3 ApplyFlatDiffuseSweep( vec3 diffuse, float localZ )"
    )
    standard_sweep = cxx_function_body(
        read(GLPROGS / "material_interaction.fs"),
        "vec3 ApplyFlatDiffuseSweep( vec3 diffuse, float localZ )",
    )
    assert_true(
        compact(light_grid_sweep) == compact(standard_sweep),
        "direct and light-grid illumination must use the same cyclic lightness ramp",
    )

    sample_at = fragment.find("vec3 diffuseSample = texture2D(")
    sweep_at = fragment.find("diffuseSample = ApplyFlatDiffuseSweep(", sample_at)
    lighting_at = fragment.find("vec3 diffuseLighting =", sweep_at)
    assert_true(
        0 <= sample_at < sweep_at < lighting_at,
        "light-grid shading must sweep only the diffuse sample before indirect lighting",
    )

    backend = read(RENDERER / "draw_common.cpp")
    require(
        backend,
        '{ "uFlatDiffuseParams", 4 }',
        "the light-grid program table must reserve the four sweep parameters",
    )
    require(
        backend,
        "RB_ApplyFlatDiffuseStage( surf, &diffuseImage, diffuseColor, flatDiffuseParams );",
        "light-grid albedo must use the same diffuse-only CPU substitution",
    )
    require(
        backend,
        "RB_GetFlatDiffuseParams( surf, representativeFlatDiffuseParams );",
        "receiver-only light-grid draws must refresh the phase for their representative diffuse stage",
    )
    require(
        compact(backend),
        "shaderParmLocations[RB_LIGHTGRID_UNIFORM_FLAT_DIFFUSE_PARAMS],1,"
        "flatDiffuseParams.ToFloatPtr());",
        "light-grid draws must upload the sweep parameters",
    )


def test_vulkan_interactions_match_and_generated_payloads_exist():
    for stem in VK_INTERACTIONS:
        fragment_name = f"{stem}.frag"
        vertex_name = f"{stem}.vert"
        fragment = read(VK_SHADERS / fragment_name)
        vertex = read(VK_SHADERS / vertex_name)

        for source, name in ((fragment, fragment_name), (vertex, vertex_name)):
            require(
                source,
                "vec4 flatDiffuseParams;",
                f"{name} must keep the Vulkan UBO layout in sync with the CPU block",
            )

        require(
            compact(vertex),
            "vLightFalloffTexCoord=vec4(dot(position,inter.lightFalloffS),0.5,position.z,1.0);",
            f"{vertex_name} must carry model-local Z to the fragment",
        )

        sweep = cxx_function_body(fragment, "vec3 ApplyFlatDiffuseSweep(vec3 diffuse, float localZ)")
        require(
            compact(sweep),
            "returnmix(diffuse,vec3(1.0),inter.flatDiffuseParams.x*band);",
            f"{fragment_name} must use the same true lightness lift as OpenGL",
        )
        reject(
            compact(sweep),
            "diffuse*(1.0+",
            f"{fragment_name} must not regress to multiplicative brightening",
        )
        reject(sweep, "specular", f"{fragment_name}'s sweep helper must be diffuse-only")

        diffuse_at = fragment.find("vec3 diffuse = texture(")
        sweep_at = fragment.find("diffuse = ApplyFlatDiffuseSweep(", diffuse_at)
        specular_at = fragment.find("vec3 specular =", sweep_at)
        assert_true(
            0 <= diffuse_at < sweep_at < specular_at,
            f"{fragment_name} must sweep diffuse before computing untouched specular",
        )

    backend = read(RENDERER / "Vulkan" / "vk_Interactions.cpp")
    block = delimited_body(backend, "typedef struct vkInteractionBlock_s")
    require(
        block,
        "float\t\t\tflatDiffuseParams[ 4 ];",
        "the Vulkan CPU UBO mirror must reserve one vec4 for the sweep",
    )
    require(
        compact(backend),
        "memcpy(block.flatDiffuseParams,din->flatDiffuseParams.ToFloatPtr(),"
        "sizeof(block.flatDiffuseParams));",
        "the Vulkan interaction UBO must copy each draw's sweep parameters",
    )

    interactions = cxx_function_body(
        backend, "static void VK_CreateSingleDrawInteractions( const drawSurf_t *surf )"
    )
    diffuse_at = interactions.find("case SL_DIFFUSE:")
    apply_at = interactions.find("RB_ApplyFlatDiffuseStage(")
    specular_at = interactions.find("case SL_SPECULAR:")
    assert_true(
        0 <= diffuse_at < apply_at < specular_at,
        "Vulkan must apply flat shading only inside its diffuse stage",
    )
    assert_true(
        interactions.count("RB_ApplyFlatDiffuseStage(") == 1,
        "Vulkan's standard decomposition must not apply flat shading to another layer",
    )

    generated = read(VK_SHADERS / "gui_shaders_spv.h")
    for stem in VK_INTERACTIONS:
        for stage in ("vert", "frag"):
            symbol = f"vk_{stem}_{stage}_spv"
            require(generated, f"static const unsigned char {symbol}[]", f"{symbol} must be packaged")
            require(generated, f"{symbol}_size", f"{symbol} must publish its generated byte size")


def test_render_entity_render_demo_and_module_abi_are_versioned():
    engine_header = read(RENDERER / "RenderWorld.h")
    game_header = read(GAME_LIBS_ROOT / "src" / "renderer" / "RenderWorld.h")

    for source, owner in ((engine_header, "engine"), (game_header, "companion game modules")):
        require(source, "REF_FLAT_DIFFUSE = 1 << 0", f"{owner} must agree on the base flag bit")
        require(
            source,
            "REF_FLAT_DIFFUSE_SWEEP = 1 << 1",
            f"{owner} must agree on the sweep flag bit",
        )
        entity = delimited_body(source, "typedef struct renderEntity_s")
        match = re.search(
            r"idVec4\s+flatDiffuseColor\s*;\s*int\s+flatDiffuseFlags\s*;",
            entity,
            re.MULTILINE,
        )
        assert_true(match is not None, f"{owner} renderEntity_t must append colour then flags")

    session = read(ROOT / "src" / "framework" / "Session.h")
    require(
        session,
        "OPENQ4_RENDERDEMO_CURRENT_VERSION = 10",
        "new render demos must advertise the flat-diffuse payload",
    )
    require(
        session,
        "OPENQ4_RENDERDEMO_FLAT_DIFFUSE_VERSION = 10",
        "flat-diffuse demo reads must have their own compatibility gate",
    )

    demo = read(RENDERER / "RenderWorld_demo.cpp")
    writer = cxx_function_body(
        demo,
        "void\tidRenderWorldLocal::WriteRenderEntity( qhandle_t handle, "
        "const renderEntity_t *ent )",
    )
    color_at = writer.find("WriteVec4( ent->flatDiffuseColor )")
    flags_at = writer.find("WriteInt( ent->flatDiffuseFlags )")
    assert_true(0 <= color_at < flags_at, "render demos must write flat colour before its flags")

    reader = cxx_function_body(demo, "bool\tidRenderWorldLocal::ReadRenderEntity()")
    require(
        reader,
        "renderEntity_t\t\tent = {};",
        "old render demos must leave the new presentation fields safely zeroed",
    )
    gate = delimited_body(
        reader,
        "if ( session->renderdemoVersion >= OPENQ4_RENDERDEMO_FLAT_DIFFUSE_VERSION )",
    )
    require(gate, "ent.flatDiffuseColor", "version 10 demos must restore flat colour")
    require(gate, "ent.flatDiffuseFlags", "version 10 demos must restore flat flags")

    api = read(RENDERER / "RenderModuleAPI.h")
    require(api, "#define RENDER_API_VERSION\t\t\t15", "the current renderer ABI must be version 15")

    loader = read(RENDERER / "RendererModule.cpp")
    require(
        loader,
        "moduleImport.version = RENDER_API_VERSION;",
        "the engine must advertise the current renderer ABI",
    )
    require(
        loader,
        "moduleExport->version != RENDER_API_VERSION",
        "the engine must reject a stale renderer module",
    )

    module = read(RENDERER / "RendererGLModule.cpp")
    require(
        module,
        "rgm_export.version = RENDER_API_VERSION;",
        "renderer modules must export the current ABI",
    )
    require(
        module,
        "moduleImport->version != RENDER_API_VERSION",
        "renderer modules must reject stale engine imports",
    )

    discovery = read(ROOT / "tools" / "build" / "meson_sources.py")
    assert_true(
        discovery.count('"renderer/*.cpp"') >= 3,
        "engine, OpenGL module, and Vulkan module discovery must all include FlatDiffuse.cpp",
    )


def cvar_call(source: str, name: str) -> str:
    return delimited_body(source, f"idCVar {name}", "(", ")")


def test_both_game_copies_keep_styles_zero_one_and_add_two_three():
    assert_true(
        GAME_LIBS_ROOT.is_dir(),
        f"companion game-library repo is required (set OPENQ4_GAMELIBS_REPO): {GAME_LIBS_ROOT}",
    )

    for copy in GAME_COPIES:
        source_root = GAME_LIBS_ROOT / "src" / copy
        cvars = read(source_root / "gamesys" / "SysCvar.cpp")

        simple_cvar = compact(cvar_call(cvars, "g_simpleItems"))
        require(
            simple_cvar,
            '"g_simpleItems","0",CVAR_GAME|PC_CVAR_ARCHIVE|CVAR_INTEGER',
            f"{copy} must preserve original models as the archived default",
        )
        assert_true(
            simple_cvar.endswith(",0,3"),
            f"{copy} g_simpleItems must expose exactly styles 0 through 3",
        )

        weapon_cvar = compact(cvar_call(cvars, "g_mpFlatOpponentWeapons"))
        require(
            weapon_cvar,
            '"g_mpFlatOpponentWeapons","0",CVAR_GAME|PC_CVAR_ARCHIVE|CVAR_BOOL',
            f"{copy} opponent weapon flat shading must be archived and opt-in",
        )

        item = read(source_root / "Item.cpp")
        style = compact(cxx_function_body(item, "int idItem::GetSimpleItemStyle( void )"))
        require(
            style,
            "idMath::ClampInt(0,3,g_simpleItems.GetInteger())",
            f"{copy} must clamp config-file values to the four supported styles",
        )

        spawn = compact(cxx_function_body(item, "void idItem::Spawn( void )"))
        require(
            spawn,
            "simpleItem=GetSimpleItemStyle()==1&&gameLocal.isMultiplayer",
            f"{copy} style 1 alone must retain the legacy billboard path",
        )

        update = cxx_function_body(item, "void idItem::UpdateFlatDiffusePresentation( void )")
        update_compact = compact(update)
        require(
            update_compact,
            "renderEntity.flatDiffuseColor.Zero();renderEntity.flatDiffuseFlags=0;",
            f"{copy} must clear presentation when style 0/1 is selected",
        )
        require(
            update_compact,
            "if(!gameLocal.isMultiplayer||simpleItem||style<2)",
            f"{copy} styles 0/1 and single-player must remain behavior-compatible",
        )
        require(
            update_compact,
            "renderEntity.flatDiffuseFlags=REF_FLAT_DIFFUSE;",
            f"{copy} style 2 must enable flat diffuse colour",
        )
        require(
            update_compact,
            "if(style>=3){renderEntity.flatDiffuseFlags|=REF_FLAT_DIFFUSE_SWEEP;",
            f"{copy} style 3 alone must add the moving band",
        )

        resolve = cxx_function_body(item, "void idItem::ResolveFlatDiffuseColor( const idDict &args, idVec4 &color )")
        for icon_key in ('"mtr_simple_icon"', '"inv_icon"', '"mtr_icon"'):
            require(resolve, icon_key, f"{copy} must derive flat colour from available item/weapon icons")
        require(
            resolve,
            "Item_TryFlatColorFromMaterial",
            f"{copy} must prefer authored icon material colour where available",
        )
        require(
            resolve,
            "Item_TryFlatColorFromName",
            f"{copy} must retain a deterministic icon-name fallback",
        )


def test_both_game_copies_rebuild_live_style_changes():
    for copy in GAME_COPIES:
        multiplayer = read(GAME_LIBS_ROOT / "src" / copy / "MultiplayerGame.cpp")
        rebuild = delimited_body(multiplayer, "if( g_simpleItems.IsModified() )")
        rebuild_compact = compact(rebuild)

        for token, description in (
            ("constintsimpleItemStyle=idItem::GetSimpleItemStyle();", "read the new style once"),
            ("for(inti=0;i<MAX_GENTITIES;i++)", "visit every live entity"),
            ("ent->IsType(idItem::GetClassType())", "limit rebuilds to items"),
            ("item->FreeModelDef();", "drop the old render model"),
            ("item->simpleItem=simpleItemStyle==1", "reserve sprites for legacy style 1"),
            ('renderModelManager->FindModel("_sprite")', "rebuild the legacy icon"),
            (
                "gameEdit->ParseSpawnArgsToRenderEntity(&item->spawnArgs,renderEntity);",
                "restore the authored model for styles 0, 2, and 3",
            ),
            ("item->UpdateFlatDiffusePresentation();", "retag the rebuilt model"),
            ("g_simpleItems.ClearModified();", "complete the live cvar transition"),
        ):
            require(rebuild_compact, token, f"{copy} live style changes must {description}")

        parse_at = rebuild_compact.find("gameEdit->ParseSpawnArgsToRenderEntity(")
        retag_at = rebuild_compact.find("item->UpdateFlatDiffusePresentation();")
        clear_at = rebuild_compact.find("g_simpleItems.ClearModified();")
        assert_true(
            0 <= parse_at < retag_at < clear_at,
            f"{copy} must restore the model before tagging it, then clear the modified flag",
        )


def test_held_weapon_effect_is_opponent_only_world_model_without_sweep():
    for copy in GAME_COPIES:
        source_root = GAME_LIBS_ROOT / "src" / copy

        weapon = read(source_root / "Weapon.cpp")
        world_model = cxx_function_body(weapon, "void rvWeapon::InitWorldModel( void )")
        require(
            world_model,
            "worldModelRenderEntity->flatDiffuseColor.Zero();",
            f"{copy} must reset reused world-weapon colour state",
        )
        require(
            world_model,
            "worldModelRenderEntity->flatDiffuseFlags = 0;",
            f"{copy} must leave held weapon shading disabled until viewer classification",
        )
        require(
            world_model,
            "ResolveFlatDiffuseColor( spawnArgs, worldModelRenderEntity->flatDiffuseColor )",
            f"{copy} must cache the held world weapon's icon-derived colour",
        )
        reject(
            world_model,
            "REF_FLAT_DIFFUSE",
            f"{copy} weapon setup must not globally enable the effect for every viewer",
        )

        player = read(source_root / "Player.cpp")
        visibility = cxx_function_body(
            player,
            "void idPlayer::UpdateMultiplayerVisibilityEffects( renderEntity_t *headRenderEnt )",
        )
        visibility_compact = compact(visibility)
        require(
            visibility_compact,
            "weaponRenderEnt=weaponWorldModel->GetRenderEntity();",
            f"{copy} must tag only the third-person/world weapon entity",
        )
        require(
            visibility_compact,
            "weaponRenderEnt->flatDiffuseFlags=0;",
            f"{copy} must clear stale per-view weapon state before every early return",
        )
        if "Player_VisibilityReference(" in player:
            require(
                visibility_compact,
                "reference==this",
                f"{copy} must exclude the player whose view a spectator is following",
            )
            require(
                visibility_compact,
                "instanceOwner->GetInstance()!=instance",
                f"{copy} must exclude players outside the spectator-aware viewer instance",
            )
            opponent_classification = "!teamColor"
        else:
            require(
                visibility_compact,
                "viewer==this",
                f"{copy} must exclude the local player's own held weapon",
            )
            require(
                visibility_compact,
                "viewer->GetInstance()!=instance",
                f"{copy} must exclude players outside the viewer's multiplayer instance",
            )
            opponent_classification = "!teammate"
        require(
            visibility_compact,
            "g_mpFlatOpponentWeapons.GetBool()&&" + opponent_classification + "&&weaponRenderEnt!=NULL",
            f"{copy} must require both the client option and opponent classification",
        )
        require(
            visibility_compact,
            "weaponRenderEnt->flatDiffuseFlags=REF_FLAT_DIFFUSE;",
            f"{copy} must enable only the base flat-diffuse flag for opponent weapons",
        )
        reject(
            visibility,
            "REF_FLAT_DIFFUSE_SWEEP",
            f"{copy} held weapons must never receive the world-item lightness band",
        )
        reject(
            visibility,
            "g_simpleItems",
            f"{copy} the opponent-weapon option must remain independent of item style",
        )

        assignment_targets = re.findall(
            r"([A-Za-z_][A-Za-z0-9_]*)->flatDiffuseFlags\s*(?:\|?=)",
            visibility,
        )
        assert_true(
            assignment_targets and set(assignment_targets) == {"weaponRenderEnt"},
            f"{copy} player visibility code must never tag the body, head, or view weapon",
        )


def test_settings_menu_exposes_the_two_client_cvars():
    menu = read(ROOT / "content" / "baseoq4" / "pak0" / "guis" / "menu" / "settings" / "game.gui")

    item_choice = compact(delimited_body(menu, "choiceDef set_game_simpleitems_value"))
    require(item_choice, "cvarg_simpleItems", "the game menu must bind the four item styles")
    require(item_choice, 'values"0;1;2;3"', "the item-style menu must expose exactly 0/1/2/3")

    weapon_choice = compact(delimited_body(menu, "choiceDef set_game_opponentweaponstyle_value"))
    require(
        weapon_choice,
        "cvarg_mpFlatOpponentWeapons",
        "the game menu must bind the opponent world-weapon toggle",
    )
    require(weapon_choice, 'values"0;1"', "the opponent weapon option must remain a simple opt-in")


def main():
    tests = [
        test_upward_cyclic_soft_band_math_and_true_lightness,
        test_cpu_diffuse_gate_preserves_alpha_layers_and_excludes_view_weapons,
        test_gl_interactions_share_the_diffuse_only_lightness_lift,
        test_light_grid_uses_the_same_diffuse_only_sweep,
        test_vulkan_interactions_match_and_generated_payloads_exist,
        test_render_entity_render_demo_and_module_abi_are_versioned,
        test_both_game_copies_keep_styles_zero_one_and_add_two_three,
        test_both_game_copies_rebuild_live_style_changes,
        test_held_weapon_effect_is_opponent_only_world_model_without_sweep,
        test_settings_menu_exposes_the_two_client_cvars,
    ]

    for test in tests:
        test()

    print("renderer_mp_flat_items: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Coverage checks for openQ4 settings-menu cvar exposure."""

from collections import defaultdict
import json
import os
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")
).resolve()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def cvar_statement(source_text: str, name: str, context: str) -> str:
    match = re.search(
        rf'^idCVar\s+\w+\(\s*"{re.escape(name)}".*?;\s*$',
        source_text,
        re.IGNORECASE | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"Missing CVar declaration for {name!r} in {context}")
    return " ".join(match.group(0).split())


def cvar_signature(source_text: str, name: str, context: str) -> tuple[str, set[str]]:
    statement = cvar_statement(source_text, name, context)
    match = re.match(
        rf'idCVar\s+\w+\(\s*"{re.escape(name)}"\s*,\s*"([^"]*)"\s*,\s*([^,]+),',
        statement,
        re.IGNORECASE,
    )
    if match is None:
        raise AssertionError(f"Cannot parse CVar declaration for {name!r} in {context}")
    return match.group(1), {flag.strip() for flag in match.group(2).split("|")}


def gui_block(source_text: str, widget_name: str) -> str:
    pattern = re.compile(rf"\b(?:bindDef|choiceDef|editDef|sliderDef|windowDef)\s+{re.escape(widget_name)}\b")
    match = pattern.search(source_text)
    if match is None:
        raise AssertionError(f"Missing GUI widget block for {widget_name}")

    brace_start = source_text.find("{", match.end())
    if brace_start < 0:
        raise AssertionError(f"Missing opening brace for GUI widget block {widget_name}")

    depth = 0
    for index in range(brace_start, len(source_text)):
        char = source_text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source_text[brace_start : index + 1]

    raise AssertionError(f"Missing closing brace for GUI widget block {widget_name}")


def load_locale_strings() -> dict[str, str]:
    locales: dict[str, str] = defaultdict(str)
    for lang_path in sorted((ROOT / "content/baseoq4/pak0/strings").glob("*.lang")):
        locale = lang_path.stem.split("_", 1)[0]
        locales[locale] += "\n" + read(lang_path)
    if not locales:
        raise AssertionError("No localized string tables found")
    return dict(locales)


def require_localized(locales: dict[str, str], string_id: str, context: str) -> None:
    if not string_id.startswith("#str_"):
        return
    for locale, lang_text in sorted(locales.items()):
        require(lang_text, string_id, f"{context} localization for {locale}")


def localized_string_value(locales: dict[str, str], locale: str, string_id: str, context: str) -> str:
    lang_text = locales.get(locale, "")
    match = re.search(rf'^\s*"{re.escape(string_id)}"\s+"([^"]*)"', lang_text, re.MULTILINE)
    if match is None:
        raise AssertionError(f"Missing {string_id!r} in {context} localization for {locale}")
    return match.group(1)


def normalize_bind_command(command: str) -> str:
    return command.strip().strip('"').lower()


def parse_default_binds(source_text: str) -> dict[str, str]:
    binds: dict[str, str] = {}
    for line in source_text.splitlines():
        line = line.split("//", 1)[0].strip()
        if not line.lower().startswith("bind"):
            continue

        match = re.match(r'bind\s+("[^"]+"|\S+)\s+(.+)$', line, re.IGNORECASE)
        if not match:
            continue

        key = match.group(1).strip('"').lower()
        command = normalize_bind_command(match.group(2))
        binds[key] = command

    return binds


def validate_bind_target(setting: dict, source_text: str, default_binds: dict[str, str]) -> None:
    if setting["widget_type"] != "bind":
        raise AssertionError(f"{setting['id']} bind target must use widget_type 'bind'")

    bind_commands = []
    for widget in setting["widgets"]:
        block = gui_block(source_text, widget)
        bind_commands.extend(
            normalize_bind_command(match.group(1))
            for match in re.finditer(r"^\s*bind\s+(.+?)\s*$", block, re.MULTILINE)
        )

    expected_target = normalize_bind_command(setting["target"])
    if expected_target not in bind_commands:
        raise AssertionError(f"{setting['id']} bind target {setting['target']!r} is not declared by its widgets")

    if "default_keys" not in setting:
        raise AssertionError(f"{setting['id']} bind target must declare default_keys")

    default_keys = setting["default_keys"]
    if not isinstance(default_keys, list):
        raise AssertionError(f"{setting['id']} default_keys must be a list")

    expected_default = ";".join(default_keys) if default_keys else "unbound"
    if setting["default"] != expected_default:
        raise AssertionError(f"{setting['id']} default should be {expected_default!r}")

    for key in default_keys:
        actual = default_binds.get(str(key).lower())
        if actual != expected_target:
            raise AssertionError(
                f"{setting['id']} default key {key!r} binds {actual!r}, expected {expected_target!r}"
            )


def validate_range(setting: dict, source_text: str) -> None:
    ui_range = setting.get("ui_range")
    if not ui_range:
        return

    for token in ("low", "high", "step"):
        if token in ui_range:
            require(source_text, f"{token}\t{ui_range[token]}", f"{setting['id']} UI range")

    cvar_range = setting.get("cvar_range")
    if not cvar_range:
        return

    ui_bounds = (ui_range.get("low"), ui_range.get("high"))
    cvar_bounds = (cvar_range.get("low"), cvar_range.get("high"))
    if ui_bounds != cvar_bounds and "range_intent" not in setting:
        raise AssertionError(
            f"{setting['id']} UI range {ui_bounds!r} differs from cvar range "
            f"{cvar_bounds!r} without a range_intent note"
        )


def validate_edit_fields(setting: dict, source_text: str) -> None:
    edit_fields = setting.get("edit_fields", [])
    if setting["widget_type"] in ("edit", "slider_edit") and not edit_fields:
        raise AssertionError(f"{setting['id']} must declare edit_fields metadata")

    for edit_field in edit_fields:
        edit_name = edit_field["name"]
        if edit_name not in setting["widgets"]:
            raise AssertionError(f"{setting['id']} edit field {edit_name!r} is not listed in widgets")

        block = gui_block(source_text, edit_name)
        require(block, f"maxchars\t{edit_field['maxchars']}", f"{setting['id']} {edit_name} maxchars")

        if edit_field.get("numeric") is True:
            require(block, "numeric\t1", f"{setting['id']} {edit_name} numeric input")
        elif edit_field.get("numeric") is False:
            reject(block, "numeric\t1", f"{setting['id']} {edit_name} signed input")


def parse_cpp_string_array(source_text: str, symbol: str) -> list[str]:
    pattern = re.compile(
        rf"(?:static\s+)?const\s+char\s*\*\s*{re.escape(symbol)}\s*\[\]\s*=\s*\{{(?P<body>.*?)\}};",
        re.DOTALL,
    )
    match = pattern.search(source_text)
    if match is None:
        raise AssertionError(f"Missing C++ string array {symbol}")
    return re.findall(r'"([^"]+)"', match.group("body"))


def parse_gui_choice_values(block: str, context: str) -> list[str]:
    match = re.search(r'^\s*values\s+"([^"]+)"', block, re.MULTILINE)
    if match is None:
        raise AssertionError(f"Missing values list in {context}")
    return match.group(1).split(";")


def registry_setting(registry: dict, setting_id: str) -> dict:
    for setting in registry.get("settings", []):
        if setting.get("id") == setting_id:
            return setting
    raise AssertionError(f"Missing settings registry entry {setting_id}")


def performance_doc_preset_order(display_docs: str) -> list[str]:
    try:
        section = display_docs.split("## Performance Presets", 1)[1].split("## Core Display Settings", 1)[0]
    except IndexError as exc:
        raise AssertionError("Display settings guide is missing the Performance Presets section") from exc
    return re.findall(r"^\| `([^`]+)` \|", section, re.MULTILINE)


def validate_performance_preset_wiring(
    common_cpp: str,
    system_gui: str,
    session_menu: str,
    registry_text: str,
    display_docs: str,
    locales: dict[str, str],
) -> None:
    command_values = parse_cpp_string_array(common_cpp, "com_performancePresetArgs")
    cvar_values = parse_cpp_string_array(common_cpp, "com_performancePresetValueStrings")
    preset_block = gui_block(system_gui, "set_sys_perf_preset_val")
    autodetect_block = gui_block(system_gui, "set_sys_perf_autodetect_button")
    gui_values = parse_gui_choice_values(preset_block, "Performance Preset GUI")
    registry = json.loads(registry_text)
    preset_setting = registry_setting(registry, "system.performance_preset")
    autodetect_setting = registry_setting(registry, "system.performance_autodetect")
    docs_values = performance_doc_preset_order(display_docs)

    if gui_values != command_values:
        raise AssertionError(f"Performance Preset GUI values {gui_values!r} differ from engine command values {command_values!r}")
    if preset_setting.get("values") != command_values:
        raise AssertionError(
            f"Performance Preset registry values {preset_setting.get('values')!r} differ from engine command values {command_values!r}"
        )
    if docs_values != command_values:
        raise AssertionError(f"Performance Preset docs order {docs_values!r} differs from engine command values {command_values!r}")
    if cvar_values[:1] != ["balanced"]:
        raise AssertionError("com_performancePreset value strings must start with balanced for invalid-value fallback")
    if sorted(value.lower() for value in cvar_values) != sorted(value.lower() for value in command_values):
        raise AssertionError("com_performancePreset value strings must contain the same presets as command completion")
    if preset_setting.get("choices_key") != "#str_229977":
        raise AssertionError("Performance Preset registry choices key should point at #str_229977")
    for locale in sorted(locales):
        labels = localized_string_value(locales, locale, preset_setting["choices_key"], "Performance Preset choices").split(";")
        if len(labels) != len(command_values):
            raise AssertionError(
                f"Performance Preset {locale} choices {labels!r} has {len(labels)} labels for {len(command_values)} engine values"
            )
        if any(not label.strip() for label in labels):
            raise AssertionError(f"Performance Preset {locale} choices contain an empty label")
    require(preset_block, 'set "cmd" "applyPerformancePreset" ;', "Performance Preset GUI apply action")
    require(autodetect_block, 'set "cmd" "play main_menu_selection ; autoDetectPerformancePreset" ;', "Performance Auto-Detect GUI action")

    for token in (
        "Common_ApplyPerformancePresetCommand",
        "Common_AutoDetectPerformancePresetCommand",
        "Common_PerformancePresetTargetIsDeclared",
        "Common_RestorePerformancePresetCVars",
        "Common_RestorePerformancePresetModifiedFlags",
        "rejected target normalization did not roll back atomically",
    ):
        require(common_cpp, token, "Performance preset engine wiring")

    implementation_start = common_cpp.index("using openQ4PerformancePreset_t = openq4::PerformancePreset;")
    implementation_end = common_cpp.index("Com_ReloadEngine_f", implementation_start)
    implementation = common_cpp[implementation_start:implementation_end] + read(ROOT / "src/framework/PerformancePreset.cpp")
    for shared in ("openq4::PerformancePresets()", "openq4::PerformancePresetTargets()", "openq4::ExpandPerformancePreset(preset,expansion,expansionError)", "openq4::DetectPerformancePreset(signals)"):
        require(common_cpp, shared, "shared performance preset source")
    for obsolete_target in (
        "OPENQ4_PERFORMANCE_PRESET_DYNAMIC_CVARS",
        '"image_filter"',
        '"image_lodbias"',
        '"image_forceDownSize"',
        '"image_roundDown"',
        '"image_preload"',
        '"image_useAllFormats"',
        '"image_downsize"',
        '"image_useCompression"',
        '"image_useNormalCompression"',
    ):
        reject(implementation, obsolete_target, "Performance preset effective cvar targets")

    image_manager = read(ROOT / "src/renderer/ImageManager.cpp")
    gl_image = read(ROOT / "src/renderer/OpenGL/gl_Image.cpp")
    for cvar_name in (
        "image_anisotropy",
        "image_downSize",
        "image_downSizeLimit",
        "image_downSizeSpecular",
        "image_downSizeSpecularLimit",
        "image_downSizeBump",
        "image_downSizeBumpLimit",
    ):
        require(image_manager, f'"{cvar_name}"', f"registered performance preset cvar {cvar_name}")
    require(gl_image, 'GetCVarInteger( "image_anisotropy" )', "effective anisotropic filtering control")
    for token in (
        "AppendMainMenuCommandArgsUntilSeparator",
        "RefreshMainMenuPerformancePresetGuiVars",
        'presetArgs.AppendArg( "applyPerformancePreset" )',
        'presetArgs.AppendArg( "autoDetectPerformancePreset" )',
    ):
        require(session_menu, token, "Performance preset menu bridge")
    if preset_setting.get("adapter", {}).get("command") != "Common_ApplyPerformancePresetCommand":
        raise AssertionError("Performance Preset registry adapter should point at Common_ApplyPerformancePresetCommand")
    if autodetect_setting.get("adapter", {}).get("command") != "Common_AutoDetectPerformancePresetCommand":
        raise AssertionError("Performance Auto-Detect registry adapter should point at Common_AutoDetectPerformancePresetCommand")


def validate_settings_registry() -> None:
    registry_path = ROOT / "docs/dev/settings-menu-registry.json"
    registry = json.loads(read(registry_path))
    if registry.get("version") != 1:
        raise AssertionError("Settings registry version must be 1")

    sources = registry.get("sources", {})
    if not sources:
        raise AssertionError("Settings registry has no source map")

    source_texts = {}
    for source_name, rel_path in sorted(sources.items()):
        source_path = ROOT / rel_path
        if not source_path.exists():
            raise AssertionError(f"Settings registry source does not exist: {rel_path}")
        source_texts[source_name] = read(source_path)

    default_bind_sources = registry.get("default_bind_sources", [])
    default_bind_text = "\n".join(source_texts[source_name] for source_name in default_bind_sources)
    default_binds = parse_default_binds(default_bind_text)

    locales = load_locale_strings()
    settings = registry.get("settings", [])
    if len(settings) < 40:
        raise AssertionError("Settings registry should cover the refactored settings surface")

    required_fields = (
        "id",
        "pane",
        "section",
        "path",
        "source",
        "container",
        "widgets",
        "widget_type",
        "label_key",
        "target_type",
        "target",
        "default",
        "restart_required",
        "accessibility",
    )

    seen_ids = set()
    bound_cvars: dict[str, list[str]] = defaultdict(list)
    panes = set()

    for setting in settings:
        setting_id = setting.get("id", "<missing id>")
        for field in required_fields:
            if field not in setting:
                raise AssertionError(f"{setting_id} missing required registry field {field!r}")

        if setting_id in seen_ids:
            raise AssertionError(f"Duplicate settings registry id: {setting_id}")
        seen_ids.add(setting_id)
        panes.add(setting["pane"])

        source_name = setting["source"]
        if source_name not in source_texts:
            raise AssertionError(f"{setting_id} references unknown source {source_name!r}")
        source_text = source_texts[source_name]

        require(source_text, setting["container"], f"{setting_id} container")
        widgets = setting["widgets"]
        if not isinstance(widgets, list) or not widgets:
            raise AssertionError(f"{setting_id} widgets must be a non-empty list")
        for widget in widgets:
            require(source_text, widget, f"{setting_id} widget")

        require(source_text, setting["label_key"], f"{setting_id} label key")
        require_localized(locales, setting["label_key"], f"{setting_id} label key")

        choices_key = setting.get("choices_key")
        if choices_key:
            require(source_text, choices_key, f"{setting_id} choices key")
            require_localized(locales, choices_key, f"{setting_id} choices key")

        choices = setting.get("choices")
        if choices:
            require(source_text, choices, f"{setting_id} literal choices")

        values = setting.get("values")
        if values:
            require(source_text, ";".join(values), f"{setting_id} values")

        for dynamic_choices in setting.get("dynamic_choices", []):
            require(source_text, dynamic_choices["choices"], f"{setting_id} dynamic choices")
            require(source_text, dynamic_choices["values"], f"{setting_id} dynamic values")

        target_type = setting["target_type"]
        if target_type not in ("cvar", "gui_state", "bind"):
            raise AssertionError(f"{setting_id} has unsupported target_type {target_type!r}")
        if target_type == "bind":
            validate_bind_target(setting, source_text, default_binds)
        else:
            require(source_text, setting["target"], f"{setting_id} target")
        if target_type == "cvar":
            canonical = setting.get("cvar_canonical", setting["target"]).lower()
            bound_cvars[canonical].append(setting_id)

        for write_target in setting.get("writes", []):
            require(source_text, write_target, f"{setting_id} write target")
        for write_token in setting.get("write_tokens", []):
            require(source_text, write_token, f"{setting_id} write token")

        adapter = setting.get("adapter")
        if adapter:
            adapter_source_name = adapter["source"]
            if adapter_source_name not in source_texts:
                raise AssertionError(f"{setting_id} adapter references unknown source {adapter_source_name!r}")
            adapter_text = source_texts[adapter_source_name]
            require(adapter_text, adapter["command"], f"{setting_id} adapter command")
            for write_target in adapter.get("writes", []):
                require(adapter_text, write_target, f"{setting_id} adapter write target")
            for write_token in adapter.get("write_tokens", []):
                require(adapter_text, write_token, f"{setting_id} adapter write token")

        validate_range(setting, source_text)
        validate_edit_fields(setting, source_text)

        if not isinstance(setting["restart_required"], bool):
            raise AssertionError(f"{setting_id} restart_required must be a boolean")
        if not str(setting["accessibility"]).strip():
            raise AssertionError(f"{setting_id} accessibility note must be non-empty")

    for expected_pane in ("Controls", "Audio", "System", "Game Options"):
        if expected_pane not in panes:
            raise AssertionError(f"Settings registry missing {expected_pane} pane coverage")

    for cvar, setting_ids in sorted(bound_cvars.items()):
        if len(setting_ids) > 1:
            shared_ok = all(
                next(setting for setting in settings if setting["id"] == setting_id).get("allow_shared_target")
                for setting_id in setting_ids
            )
            if not shared_ok:
                raise AssertionError(f"Duplicate direct cvar binding for {cvar}: {setting_ids}")

    controls_gui = source_texts.get("controls")
    if controls_gui is not None:
        actual_bind_widgets = set(re.findall(r"\bbindDef\s+([A-Za-z0-9_]+)", controls_gui))
        registered_bind_widgets = {
            widget
            for setting in settings
            if setting["source"] == "controls" and setting["target_type"] == "bind"
            for widget in setting["widgets"]
            if widget.endswith("_key")
        }
        if actual_bind_widgets != registered_bind_widgets:
            missing = sorted(actual_bind_widgets - registered_bind_widgets)
            extra = sorted(registered_bind_widgets - actual_bind_widgets)
            raise AssertionError(f"Controls registry bind coverage mismatch, missing={missing}, extra={extra}")

    game_gui = source_texts.get("game")
    if game_gui is not None:
        actual_game_value_widgets = set(
            re.findall(r"\b(?:choiceDef|editDef)\s+(set_game_[A-Za-z0-9_]*_value)\b", game_gui)
        )
        registered_game_value_widgets = {
            widget
            for setting in settings
            if setting["source"] == "game"
            for widget in setting["widgets"]
            if re.match(r"set_game_[A-Za-z0-9_]*_value$", widget)
        }
        if actual_game_value_widgets != registered_game_value_widgets:
            missing = sorted(actual_game_value_widgets - registered_game_value_widgets)
            extra = sorted(registered_game_value_widgets - actual_game_value_widgets)
            raise AssertionError(f"Game Options registry value-widget coverage mismatch, missing={missing}, extra={extra}")


def validate_value_entry_widgets(mainmenu: str, system_gui: str, audio_gui: str, popups_gui: str, game_gui: str) -> None:
    positive_numeric_edits = {
        "popups": (
            "pop_setAdv_ambientbr_valnum",
        ),
        "system": (
            "set_sys_gamma_value",
            "set_sys_ambientbr_valnum",
            "set_sys_window_width_val",
            "set_sys_window_height_val",
            "set_sys_custom_width_val",
            "set_sys_custom_height_val",
        ),
        "audio": (
            "set_audio_vol_value",
            "set_audio_music_value",
        ),
        "game": (
            "set_game_msmooth_value",
            "set_game_scopesensitivity_value",
            "set_game_msensitivity_value",
            "set_game_mcpi_value",
            "set_game_mfilter_value",
            "set_game_macceloffset_value",
            "set_game_maccelpower_value",
            "set_game_msenscap_value",
            "set_game_controller_deadzone_value",
            "set_game_controller_look_sensitivity_value",
            "set_game_controller_look_curve_value",
            "set_game_controller_trigger_value",
            "set_game_controller_rumble_strength_value",
            "set_game_controller_gyro_sensitivity_value",
            "set_game_controller_touchpad_sensitivity_value",
            "set_game_controller_low_battery_rumble_value",
            "set_game_controller_low_battery_rumble_cap_value",
            "set_game_cl_gunfov_value",
        ),
    }
    source_texts = {
        "mainmenu": mainmenu,
        "system": system_gui,
        "audio": audio_gui,
        "popups": popups_gui,
        "game": game_gui,
    }
    for source_name, widgets in positive_numeric_edits.items():
        for widget in widgets:
            require(gui_block(source_texts[source_name], widget), "numeric\t1", f"{widget} numeric input")

    for widget in ("set_game_maccel_value", "set_game_cl_gun_x_value", "set_game_cl_gun_y_value", "set_game_cl_gun_z_value"):
        reject(gui_block(game_gui, widget), "numeric\t1", f"{widget} signed input")


def validate_legacy_settings_audit(mainmenu: str, system_gui: str, popups_gui: str) -> None:
    removed_system_audio_widgets = (
        "set_sys_vol_bg",
        "set_sys_vol_slider",
        "set_sys_vol_value",
        "set_sys_b9_corner",
        "set_sys_t_b9",
        "set_sys_vol2_bg",
        "set_sys_vol2_slider",
        "set_sys_vol3_bg",
        "set_sys_vol3_slider",
        "s_speakerFraction",
    )
    active_settings_text = mainmenu + "\n" + system_gui + "\n" + popups_gui
    for widget in removed_system_audio_widgets:
        reject(active_settings_text, widget, "removed System-embedded legacy audio widgets")

    require(popups_gui, "windowDef pop_p_setAdv", "documented dormant System Advanced popup")
    require(popups_gui, "windowDef pop_p_set_sndAdv", "documented dormant Sound Advanced popup")
    require(mainmenu, 'set "set_b_system_adv::visible" "0"', "dormant System Advanced entry remains hidden")
    reject(mainmenu, 'set "set_b_system_adv::visible" "1"', "dormant System Advanced entry must not be exposed")


def validate_settings_popups_extraction(mainmenu: str, popups_gui: str) -> None:
    require(mainmenu, '#include "guis/menu/settings/popups.gui"', "Settings popups include")
    for popup in (
        "pop_p_defaults",
        "pop_p_setAdv",
        "pop_p_auto",
        "pop_p_ultrawarn",
        "pop_p_set_sndAdv",
    ):
        require(popups_gui, f"windowDef {popup}", "Settings popup definitions")
        reject(mainmenu, f"windowDef {popup}", "mainmenu Settings popup extraction")

    require(mainmenu, "windowDef pop_p_mods", "global Mods popup remains in main menu")
    reject(popups_gui, "windowDef pop_p_mods", "Settings popups include scope")

    for token in (
        "anim_pop_defaultsIn",
        "anim_pop_defaultsOut",
        "anim_pop_autoIn",
        "anim_pop_autoOut",
        "anim_pop_ultrawarnOut",
        "anim_pop_setAdvIn",
        "anim_pop_sndadvOut",
    ):
        require(mainmenu, token, "Settings popup animation wiring")


def validate_controls_pane_extraction(mainmenu: str, controls_gui: str) -> None:
    require(mainmenu, '#include "guis/menu/settings/controls.gui"', "Controls settings include")
    reject(mainmenu, "windowDef p_settings_ctrls", "mainmenu Controls pane extraction")
    require(controls_gui, "windowDef p_settings_ctrls", "Controls settings pane")

    for subpane in ("p_set_ctrls_move", "p_set_ctrls_weap", "p_set_ctrls_attk", "p_set_ctrls_other"):
        require(controls_gui, f"windowDef {subpane}", "Controls category panes")

    for selector in ("set_b_controls_1", "set_b_controls_2", "set_b_controls_3", "set_b_controls_4"):
        require(mainmenu, f"windowDef {selector}", "Controls shell category selectors")

    bind_targets = (
        "_forward",
        "_back",
        "_moveleft",
        "_moveright",
        "_moveUp",
        "_moveDown",
        "_left",
        "_right",
        "_strafe",
        "_speed",
        "_impulse0",
        "_impulse1",
        "_impulse2",
        "_impulse3",
        "_impulse4",
        "_impulse5",
        "_impulse6",
        "_impulse7",
        "_impulse8",
        "_impulse9",
        "_impulse10",
        '"buymenu"',
        "_attack",
        "_impulse15",
        "_impulse14",
        "_impulse13",
        "_lookUp",
        "_lookDown",
        "_mlook",
        "centerview",
        "_zoom",
        "_impulse19",
        "_impulse50",
        "screenshot",
        "_weaponWheel",
        '"savegame quick"',
        '"loadgame quick"',
        "clientMessageMode",
        '"clientMessageMode 1"',
        "_impulse28",
        "_impulse29",
        "_ingamestats",
        "_impulse17",
        '"emote salute"',
        '"emote cheer"',
        '"emote taunt"',
        '"emote grab_a"',
        "_voicechat",
    )
    for bind_target in bind_targets:
        require(controls_gui, f"bind\t{bind_target}", "Controls bind targets")

    if controls_gui.count("bindDef ") < 48:
        raise AssertionError("Controls pane should preserve the full bindDef surface")


def validate_scripted_pseudo_setting_adapters(game_gui: str, session_menu: str) -> None:
    require(game_gui, 'set "cmd" "applyForceModelChoice"', "Force Model typed adapter command")
    for token in (
        'consoleCMD "g_forceModel',
        'consoleCMD "g_forceMarineModel',
        'consoleCMD "g_forcestroggmodel',
    ):
        reject(game_gui, token, "Force Model GUI cvar ladder")

    for token in (
        "MAINMENU_FORCE_MODEL_MARINE",
        "MAINMENU_FORCE_MODEL_STROGG",
        "ApplyMainMenuForceModelChoice",
        "g_forceModel",
        "g_forceMarineModel",
        "g_forceStroggModel",
        "model_player_marine_helmeted_bright",
        "model_player_tactical_transfer_bright",
    ):
        require(session_menu, token, "Force Model typed adapter")

    require(session_menu, 'enabled ? MAINMENU_FORCE_MODEL_MARINE : ""', "Force Model disable clears marine cvars")
    require(session_menu, 'enabled ? MAINMENU_FORCE_MODEL_STROGG : ""', "Force Model disable clears strogg cvar")

    require(game_gui, 'set "cmd" "applyGunPositionChoice"', "Gun Position typed adapter command")
    for token in (
        'consoleCMD "g_gunX',
        'consoleCMD "g_gunY',
        'consoleCMD "g_gunZ',
        'consoleCMD "g_weaponFovEffect',
    ):
        reject(game_gui, token, "Gun Position GUI cvar ladder")

    for token in (
        "MAINMENU_GUN_POSITION_PRESETS",
        "ApplyMainMenuGunPositionChoice",
        "g_gunX",
        "g_gunY",
        "g_gunZ",
        "g_weaponFovEffect",
        "1.0f, 2.0f, -1.0f",
        "1.0f, -5.0f, -1.0f",
    ):
        require(session_menu, token, "Gun Position typed adapter")

    require(game_gui, 'set "cmd" "applyCorpseTimeChoice"', "Corpse Time typed adapter command")
    reject(game_gui, 'consoleCMD "seta g_corpseRemoveDelaySP', "Corpse Time GUI SP cvar ladder")
    reject(game_gui, 'consoleCMD "seta g_corpseRemoveDelayMP', "Corpse Time GUI MP cvar ladder")

    for token in (
        "MAINMENU_CORPSE_TIME_PRESET_VALUES",
        "ApplyMainMenuCorpseTimeChoice",
        "g_corpseRemoveDelaySP",
        "g_corpseRemoveDelayMP",
        "0.0f",
        "-1.0f",
        "60.0f",
    ):
        require(session_menu, token, "Corpse Time typed adapter")


def validate_ingame_menu_activation_contract(
    mainmenu: str,
    session_menu: str,
    session_cpp: str,
) -> None:
    set_gui = session_menu[
        session_menu.index("void idSessionLocal::SetGUI") :
        session_menu.index("idSessionLocal::ExitMenu")
    ]
    for token in (
        "const bool refreshCatalogs = !mapSpawned;",
        "if ( refreshCatalogs )",
        "SetMainMenuGuiVars( refreshCatalogs );",
    ):
        require(set_gui, token, "bounded in-game main-menu activation")

    set_main_menu_vars = session_menu[
        session_menu.index("void idSessionLocal::SetMainMenuGuiVars") :
        session_menu.index("idSessionLocal::HandleSaveGameMenuCommand")
    ]
    for token in (
        "SetKeyBindingNames()",
        "PopulateMainMenuAudioDeviceChoices( guiMainMenu )",
        "PopulateMainMenuDisplayChoices( guiMainMenu )",
        "SetModsMenuGuiVars()",
        "SetMainMenuMPModelVars( guiMainMenu )",
    ):
        require(set_main_menu_vars, token, "lazy main-menu catalogs")
    reject(set_main_menu_vars, "SetMainMenuBackgroundMontageGuiVars()", "synchronous menu activation")

    montage_init = session_cpp[
        session_cpp.index('guiMainMenu = uiManager->FindGui( "guis/mainmenu.gui"') :
        session_cpp.index("guiMainMenu_MapList = uiManager->AllocListGUI()")
    ]
    require(montage_init, "PrimeMainMenuGuiResources();", "session-time menu resource warmup")

    map_load_prime = session_cpp[
        session_cpp.index("uiManager->BeginLevelLoad();") :
        session_cpp.index("// set the loading gui that we will wipe to")
    ]
    require(map_load_prime, "PrimeMainMenuGuiResources();", "level-load menu resource precache")
    prime_resources = session_cpp[
        session_cpp.index("void idSessionLocal::PrimeMainMenuGuiResources") :
        session_cpp.index("static void Session_DrawFallbackLoadingScreen")
    ]
    for token in (
        "SetMainMenuBackgroundMontageGuiVars();",
        'declManager->FindMaterial( fallbackLevelshot )',
        "renderSystem->PreloadImage( fallbackLevelshot );",
        'declManager->FindSound( "main_menu_gameplay" );',
    ):
        require(prime_resources, token, "complete in-game menu resource precache")

    load_in = gui_block(mainmenu, "anim_loadIn")
    save_in = gui_block(mainmenu, "anim_saveIn")
    settings_in = gui_block(mainmenu, "anim_settingsIn")
    mods_in = gui_block(mainmenu, "anim_pop_modsIn")
    require(load_in, 'set "cmd" "refreshSaveGameList" ;', "lazy Load Game catalog")
    reject(load_in, 'set "cmd" "MAPScan" ;', "Load Game map-declaration scan")
    require(save_in, 'set "cmd" "refreshSaveGameList" ;', "lazy Save Game catalog")
    require(mods_in, 'set "cmd" "refreshMods" ;', "lazy Mods catalog")
    for command in ("refreshKeyBindings", "refreshSystemSettings", "refreshAudioSettings"):
        require(settings_in, f'set "cmd" "{command}" ;', "lazy Settings catalog")
        require(session_menu, f'!idStr::Icmp( cmd, "{command}" )', "lazy Settings command handler")

    for widget, command in (
        ("set_b_controls", "refreshKeyBindings"),
        ("set_b_system", "refreshSystemSettings"),
        ("set_b_audio", "refreshAudioSettings"),
    ):
        require(gui_block(mainmenu, widget), f'set "cmd" "{command}" ;', f"{widget} lazy refresh")
    for command in ("refreshSaveGameList", "refreshMods"):
        require(session_menu, f'!idStr::Icmp( cmd, "{command}" )', f"{command} handler")

    for token in (
        "Session_openQ4AssertMenuActivation_f",
        "OPENQ4_MENU_ACTIVATION PASS elapsed=%dms limit=%dms",
        'AddCommand( "openq4_assertMenuActivation"',
    ):
        require(session_cpp, token, "runtime in-game menu activation assertion")


def main() -> None:
    validate_settings_registry()

    mainmenu = read(ROOT / "content/baseoq4/pak0/guis/mainmenu.gui")
    controls_gui = read(ROOT / "content/baseoq4/pak0/guis/menu/settings/controls.gui")
    system_gui = read(ROOT / "content/baseoq4/pak0/guis/menu/settings/system.gui")
    audio_gui = read(ROOT / "content/baseoq4/pak0/guis/menu/settings/audio.gui")
    popups_gui = read(ROOT / "content/baseoq4/pak0/guis/menu/settings/popups.gui")
    game_gui = read(ROOT / "content/baseoq4/pak0/guis/menu/settings/game.gui")
    game_hovers = read(ROOT / "content/baseoq4/pak0/guis/menu/settings/game_hovers.gui")
    session_menu = read(ROOT / "src/framework/Session_menu.cpp")
    session_cpp = read(ROOT / "src/framework/Session.cpp")
    common_cpp = read(ROOT / "src/framework/Common.cpp")
    slider_window = read(ROOT / "src/ui/SliderWindow.cpp")
    registry_text = read(ROOT / "docs/dev/settings-menu-registry.json")
    display_docs = read(ROOT / "docs/user/display-settings.md")
    locales = load_locale_strings()
    validate_value_entry_widgets(mainmenu, system_gui, audio_gui, popups_gui, game_gui)
    validate_legacy_settings_audit(mainmenu, system_gui, popups_gui)
    validate_settings_popups_extraction(mainmenu, popups_gui)
    validate_controls_pane_extraction(mainmenu, controls_gui)
    validate_scripted_pseudo_setting_adapters(game_gui, session_menu)
    validate_performance_preset_wiring(common_cpp, system_gui, session_menu, registry_text, display_docs, locales)
    validate_ingame_menu_activation_contract(mainmenu, session_menu, session_cpp)

    require(mainmenu, '#include "guis/menu/settings/system.gui"', "System settings include")
    reject(mainmenu, "windowDef p_settings_sys", "mainmenu System pane extraction")
    require(mainmenu, '#include "guis/menu/settings/audio.gui"', "Audio settings include")
    reject(mainmenu, "windowDef p_settings_audio", "mainmenu Audio pane extraction")

    audio_cvars = [
        "s_volume",
        "s_useOpenAL",
        "s_deviceName",
        "s_useEAXReverb",
        "s_numberOfSpeakers",
        "s_openALHRTF",
        "s_musicVolume",
    ]
    for cvar in audio_cvars:
        require(audio_gui, cvar, "Audio settings include")

    audio_surface = audio_gui + "\n" + mainmenu
    for token in (
        "windowDef set_audio_cat_levels",
        "windowDef set_audio_cat_backend",
        "windowDef set_audio_cat_effects",
        "#str_41096",
        "#str_229962",
        "#str_229963",
        "rect\t0,104,640,250",
        "rect\t-24,-84,640,330",
        "rect\t245,142,361,20",
        "rect\t245,294,361,20",
    ):
        require(audio_surface, token, "Audio grouped layout")

    audio_order = (
        "windowDef set_audio_cat_levels",
        "windowDef set_audio_vol",
        "windowDef set_audio_music",
        "windowDef set_audio_cat_backend",
        "windowDef set_audio_backend",
        "windowDef set_audio_device",
        "windowDef set_audio_cat_effects",
        "windowDef set_audio_eax",
        "windowDef set_audio_speakers",
        "windowDef set_audio_hrtf",
    )
    last_position = -1
    for token in audio_order:
        position = audio_gui.find(token)
        if position < 0:
            raise AssertionError(f"Missing {token!r} in Audio grouped layout")
        if position <= last_position:
            raise AssertionError(f"Audio grouped layout order regressed at {token!r}")
        last_position = position

    display_cvars = [
        "display_mode_choice",
        "display_mode_choices",
        "display_mode_values",
        "ui_aspectCorrection",
        "display_refresh_choices",
        "display_refresh_values",
        "r_displayRefresh",
        "r_windowWidth",
        "r_windowHeight",
        "r_customWidth",
        "r_customHeight",
    ]
    for cvar in display_cvars:
        require(system_gui, cvar, "System settings include")
    for token in (
        "applyDisplayModeChoice",
        "applyCustomDisplaySize",
        "refreshDisplayChoices",
        "BuildMainMenuDisplayModeChoices",
        "BuildMainMenuRefreshChoices",
        "SDL_GetFullscreenDisplayModes",
        "r_customWidth",
        "r_customHeight",
        'SetCVarInteger( "r_mode", -1 )',
        'SetCVarInteger( "r_mode", -2 )',
    ):
        require(session_menu + system_gui, token, "Display resolution adapter")

    game_cvars = [
        "pm_zoomedSlow",
        "g_simpleItems",
        "g_mpFlatOpponentWeapons",
        "g_autoSkipCinematics",
        "cl_gunfov",
        "cl_gunfov_adjust",
        "cl_gun_x",
        "cl_gun_y",
        "cl_gun_z",
    ]
    for cvar in game_cvars:
        require(game_gui, cvar, "Game Options settings")

    game_rows = [
        "set_game_scopesensitivity",
        "set_game_simpleitems",
        "set_game_opponentweaponstyle",
        "set_game_autoskipcinematics",
        "set_game_viewweapon",
        "set_game_cl_gunfov",
        "set_game_cl_gunfov_adjust",
        "set_game_cl_gun_x",
        "set_game_cl_gun_y",
        "set_game_cl_gun_z",
    ]
    for row in game_rows:
        require(game_gui, f"windowDef {row}", "Game Options definitions")
        if row != "set_game_viewweapon":
            require(mainmenu, f'{row}::visible', "Game Options show/hide events")
            require(game_hovers, f"windowDef {row}_hover", "Game Options hovers")

    system_surface = system_gui + "\n" + popups_gui + "\n" + mainmenu
    for token in ("high\t28", "cvar\tgui_set_sys_scroll"):
        require(system_surface, token, "System scroll coverage")
    for token in (
        "windowDef set_sys_section",
        "choiceDef set_sys_section_choice",
        "#str_229959",
        "#str_229961",
        "sys_section_choice",
        'set "gui::gui_set_sys_scroll" "0"',
        'set "gui::gui_set_sys_scroll" "5"',
        'set "gui::gui_set_sys_scroll" "9"',
        'set "gui::gui_set_sys_scroll" "14"',
        'set "gui::gui_set_sys_scroll" "21"',
        'set "gui::gui_set_sys_scroll" "28"',
        'set "cmd" "applySettingsScroll system"',
        "set_sys_section_choice::noevents",
    ):
        require(system_surface, token, "System section jump navigation")
    reject(system_surface, 'set "gui_set_sys_scroll"', "System scroll GUI state assignment")
    reject(system_surface, "set gui_set_sys_scroll", "System scroll cvar command")
    for token in (
        "rect\t204,104,377,16",
        "rect\t0,128,640,256",
        "rect\t-24,-41,640,1428",
        "rect\t613,128,16,256",
        "high\t46",
        "cvar\tgui_set_game_scroll",
        "640,1428",
    ):
        require(game_gui + mainmenu, token, "Game Options scroll coverage")
    for token in (
        "windowDef set_game_section",
        "choiceDef set_game_section_choice",
        "#str_229959",
        "#str_229960",
        "game_section_choice",
        'set "gui::gui_set_game_scroll" "0"',
        'set "gui::gui_set_game_scroll" "11"',
        'set "gui::gui_set_game_scroll" "21"',
        'set "gui::gui_set_game_scroll" "37"',
        'set "gui::gui_set_game_scroll" "44"',
        'set "gui::gui_set_game_scroll" "46"',
        'set "cmd" "applySettingsScroll game"',
        "set_game_section_choice::noevents",
    ):
        require(game_gui + mainmenu, token, "Game Options section jump navigation")
    reject(game_gui + mainmenu, 'set "gui_set_game_scroll"', "Game Options scroll GUI state assignment")
    reject(game_gui + mainmenu, "set gui_set_game_scroll", "Game Options scroll cvar command")
    require(mainmenu, 'set "gui::gui_set_audio_scroll" "0"', "Audio scroll GUI state reset")
    require(audio_gui + mainmenu, 'set "cmd" "applySettingsScroll audio"', "Audio scroll apply command")
    reject(mainmenu, 'set "gui_set_audio_scroll"', "Audio scroll GUI state assignment")
    reject(audio_gui + mainmenu, "set gui_set_audio_scroll", "Audio scroll cvar command")
    for token in (
        'gui_set_sys_scroll( "gui_set_sys_scroll", "0", CVAR_GUI | CVAR_INTEGER, "display menu scroll step", 0, 28 )',
        'gui_set_game_scroll( "gui_set_game_scroll", "0", CVAR_GUI | CVAR_INTEGER, "game menu scroll step", 0, 46 )',
        "HandleMainMenuSettingsScrollInput( guiActive, event->evValue )",
        'MainMenuWindowStateEqualsInt( gui, "desktop::curr", page.expectedPage )',
        "MainMenuSettingsPopupIsVisible( gui )",
        "MAINMENU_SETTINGS_SCROLL_PAGES",
        "ApplyMainMenuSettingsScrollPage( gui, page, next, true )",
        "SyncMainMenuSettingsScrollPages( guiActive )",
        "SyncMainMenuSettingsScrollPages( guiTest != NULL ? guiTest : guiActive )",
        'if ( !idStr::Icmp( cmd, "applySettingsScroll" ) )',
        '"set_game_content::rect"',
        '"set_sys_content::rect"',
        '"set_audio_content::rect"',
        "958",
        "902",
        "24.0f",
    ):
        require(session_menu, token, "C++ settings scroll bounds")
    reject(session_menu, 'MainMenuWindowStateIsNonZero( gui, "desktop::active" )', "C++ settings scroll active-state gate")
    for token in (
        '"desktop::curr" == 22 && "desktop::active" == 0',
        '"desktop::curr" == 43 && "desktop::active" == 0',
        '"desktop::curr" == 21 && "desktop::active" == 0',
        'transition "set_sys_content::rect"',
        'transition "set_game_content::rect"',
        'transition "set_audio_content::rect"',
    ):
        reject(mainmenu, token, "GUI settings scroll active-state gate")
    for token in (
        "pop_p_defaults::visible",
        "pop_p_setAdv::visible",
        "pop_p_auto::visible",
        "pop_p_ultrawarn::visible",
        "pop_p_vidwarn::visible",
        "pop_p_set_sndadv::visible",
    ):
        require(session_menu, token, "C++ settings popup scroll guard")
        require(mainmenu, token, "GUI settings popup scroll guard")
    require(slider_window, "if ( scrollbar ) {\n\t\t\tRunScript( ON_ACTION );\n\t\t}", "scrollbar drag apply event")
    require(slider_window, "return RouteMouseCoords(0.0f, 0.0f);", "scrollbar mouse-down command propagation")
    require(slider_window, "return cmd;", "scrollbar script command propagation")

    for state, cvar in (
        ("r_specularEnabled", "r_skipSpecular"),
        ("r_bumpEnabled", "r_skipBump"),
        ("r_skyEnabled", "r_skipSky"),
    ):
        require(system_surface, f'gui\t"{state}"', f"{state} positive GUI binding")
        require(system_surface, f'set {cvar} 0', f"{state} enable adapter")
        require(system_surface, f'set {cvar} 1', f"{state} disable adapter")
        require(session_menu, f'SetStateInt( "{state}", cvarSystem->GetCVarBool( "{cvar}" ) ? 0 : 1 )', f"{state} cvar sync")
        reject(system_surface, f'cvar\t"{cvar}"', f"{cvar} direct negative GUI binding")

    string_ids = [f"#str_{value}" for value in range(229943, 229976)]
    for lang_path in sorted((ROOT / "content/baseoq4/pak0/strings").glob("*_openq4.lang")):
        lang_text = read(lang_path)
        for string_id in string_ids:
            require(lang_text, string_id, lang_path.name)

    sp_cvars = read(GAME_LIBS / "src/game/gamesys/SysCvar.cpp")
    mp_cvars = read(GAME_LIBS / "src/mpgame/gamesys/SysCvar.cpp")
    mp_header = read(GAME_LIBS / "src/mpgame/gamesys/SysCvar.h")
    mp_player = read(GAME_LIBS / "src/mpgame/Player.cpp")

    for text, context in ((sp_cvars, "SP game cvars"), (mp_cvars, "MP game cvars")):
        require(text, 'g_autoSkipCinematics",\t\t"0",\t\t\tCVAR_GAME | PC_CVAR_ARCHIVE | CVAR_BOOL', context)

    # Stock multiplayer GUIs bind these names directly. Both game modules can
    # be the first module loaded by the unified executable, so each must
    # register an identical typed declaration rather than leaving a typeless
    # GUI-created placeholder behind.
    for name in (
        "net_menulanserver",
        "net_serverMenuDedicated",
        "s_voiceChatSend",
        "s_voiceChatReceive",
        "s_voiceChatEcho",
        "s_voiceVolume",
        "s_micInputLevel",
    ):
        sp_statement = cvar_statement(sp_cvars, name, "SP game cvars")
        mp_statement = cvar_statement(mp_cvars, name, "MP game cvars")
        if sp_statement != mp_statement:
            raise AssertionError(f"SP/MP GUI CVar declarations differ for {name!r}")

    # The modules intentionally retain Quake 4's distinct SP (box) and MP
    # (cylinder) defaults, but they share an integer type so an in-process
    # module switch cannot register the same global CVar with two types.
    sp_default, sp_flags = cvar_signature(sp_cvars, "pm_usecylinder", "SP game cvars")
    mp_default, mp_flags = cvar_signature(mp_cvars, "pm_usecylinder", "MP game cvars")
    if (sp_default, mp_default) != ("0", "1"):
        raise AssertionError("pm_usecylinder no longer preserves the stock SP/MP defaults")
    for flags, context in ((sp_flags, "SP"), (mp_flags, "MP")):
        if "CVAR_INTEGER" not in flags or "CVAR_BOOL" in flags:
            raise AssertionError(f"{context} pm_usecylinder must use the shared integer type")

    for cvar in ("cl_gun_x", "cl_gun_y", "cl_gun_z"):
        require(mp_cvars, f'idCVar {cvar}', "MP game cvar definitions")
        require(mp_header, f"extern idCVar\t{cvar}", "MP game cvar declarations")
        require(mp_player, f"+ {cvar}.GetFloat()", "MP weapon offset use")

    print("settings_menu_coverage: ok")


if __name__ == "__main__":
    main()

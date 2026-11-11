#!/usr/bin/env python3
"""Negative controls for the rotating fullscreen/HUD qualification evidence."""
from airdefense_performance import check_rotation, QUALITY_PROFILES, POST_AA

VALID = '''Vulkan: first SSAO pass drew
Vulkan: first motion blur pass drew (camera only)
Vulkan: first bloom/tone map pass drew
Retained UI root profile: {"frames":1000,"views":3}
Retained UI document work: path=guis/hud/remastered_marine.rui cpu_ms=0.1
benchmarkViewSweep: start, 360.0 degrees over 12000 ms
benchmarkViewSweep: complete, 360.0 degrees over 12000 ms, yaw -105 -> -105
benchmarkViewSweep: start, 720.0 degrees over 24000 ms
origin: (10325.0 -6962.8 7.0) angles: (0 -105 0)
Frame pacing stats reset
benchmarkViewSweep: complete, 720.0 degrees over 24000 ms, yaw -105 -> -105
Frame pacing snapshot (SP): samples=6000, swapInterval=0, cap=0, refresh=240.0 Hz
Frame timing window (SP): samples=6000, span_ms=27000, mean_ms=4.5
Renderer API: requested=vulkan active=vulkan disposition=module
Renderer AA: MSAA requested=16 effective=8 reason=vulkan-scene postAAEffective=1 postAAReason=SMAA1xUltra screenFraction=100%
'''
def check_profile(preset):
    quality = QUALITY_PROFILES[preset]
    valid = VALID.replace("requested=16 effective=8", f"requested={quality['r_multiSamples']} effective={min(8, quality['r_multiSamples'])}").replace("SMAA1xUltra", POST_AA[preset])
    if not quality["r_motionBlur"]:
        valid = valid.replace("Vulkan: first motion blur pass drew (camera only)\n", "")
    valid += "".join(f'"{key}" is:"{value}" default:"0"\n' for key,value in quality.items())
    check = lambda text, api="vk": check_rotation(text, api, preset)
    assert not check(valid)
    for before, after in (
        ("complete, 720.0", "incomplete, 720.0"),
        ("complete, 360.0", "incomplete, 360.0"),
        ("active=vulkan", "active=gl"),
        (f"effective={min(8, quality['r_multiSamples'])}", "effective=0"),
        ("screenFraction=100%", "screenFraction=85%"),
        ("postAAEffective=1", "postAAEffective=0"),
        ("cap=0,", "cap=60,"),
        ("span_ms=27000", "span_ms=5000"),
        ('"frames":1000', '"frames":500'),
        ("remastered_marine.rui", "artwork.rui"),
        ("10325.0", "0.0"),
    ):
        assert check(valid.replace(before, after)), (before, after)
    assert check(valid + "Frame pacing stats reset\n")
    assert check(valid + "Frame pacing snapshot (SP): cap=0,\n")
    assert check(valid.replace("Frame pacing stats reset\n", "") + "Frame pacing stats reset\n")
    assert check(valid, "gl")
    assert not check(valid.replace("Vulkan:", "OpenGL:").replace("vulkan", "gl"), "gl")
    for effect in ("SSAO", "motion blur", "bloom/tone map"):
        marker = f"Vulkan: first {effect} pass drew"
        if effect == "motion blur" and not quality["r_motionBlur"]:
            continue
        assert check(valid.replace(marker, "missing")), effect
        assert check(valid.replace(marker, "missing") + marker), effect
    for key,value in quality.items():
        assert check(valid.replace(f'"{key}" is:"{value}"', f'"{key}" is:"wrong"')), key
    assert check(valid + "Vulkan: required shadow resource unavailable")
    other = "ultra" if preset == "quality" else "quality"
    assert check_rotation(valid, "vk", other)


def main():
    for preset in QUALITY_PROFILES:
        check_profile(preset)
    print("airdefense performance contract: both presets, rotation, HUD, renderer, sample and workload controls passed")


if __name__ == "__main__":
    main()

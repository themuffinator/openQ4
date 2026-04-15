#!/usr/bin/env python3
import math
import sys


LUMA = (0.2126, 0.7152, 0.0722)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def clamp(value, low, high):
    return max(low, min(high, value))


def mix(a, b, t):
    return tuple(x * (1.0 - t) + y * t for x, y in zip(a, b))


def max_channel(color):
    return max(color[0], color[1], color[2])


def smoothstep(edge0, edge1, value):
    if edge1 <= edge0:
        return 1.0 if value >= edge1 else 0.0
    t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def bright_mask(color, threshold, soft_knee):
    brightness = dot(color, LUMA)
    if threshold <= 1.0e-4:
        return 1.0

    knee = max(threshold * soft_knee, 0.0)
    if knee > 1.0e-4:
        knee_start = max(threshold - knee, 0.0)
        return smoothstep(knee_start, threshold, brightness)

    return 1.0 if brightness >= threshold else 0.0


def aces_film_scalar(value):
    a = 2.51
    b = 0.03
    c = 2.43
    d = 0.59
    e = 0.14
    return (value * (a * value + b)) / (value * (c * value + d) + e)


def highlight_compress(color, highlight_desaturation, gamut_compression):
    luma = dot(color, LUMA)
    peak = max_channel(color)
    highlight = clamp((peak - 0.6) / 0.4, 0.0, 1.0)
    color = mix(color, (luma, luma, luma), clamp(highlight * highlight_desaturation, 0.0, 1.0))

    peak = max_channel(color)
    if peak > 1.0 and gamut_compression > 0.0:
        compressed_peak = 1.0 + (peak - 1.0) / (1.0 + gamut_compression * (peak - 1.0))
        scale = compressed_peak / peak
        color = tuple(channel * scale for channel in color)

    return color


def tone_map_hdr(color, exposure, white_point, highlight_desaturation, gamut_compression):
    safe_exposure = max(exposure, 1.0e-3)
    exposed = tuple(channel * safe_exposure for channel in color)
    safe_white = max(white_point, 1.0)
    white_scale = 1.0 / max(aces_film_scalar(safe_white * safe_exposure), 1.0e-4)
    mapped = tuple(aces_film_scalar(channel) * white_scale for channel in exposed)
    compressed = highlight_compress(mapped, highlight_desaturation, gamut_compression)
    return tuple(clamp(channel, 0.0, 1.0) for channel in compressed)


def apply_lift_gamma_gain(color, lift, post_gamma, gain):
    safe_gamma = max(post_gamma, 1.0e-3)
    lifted = tuple(max(channel + lift, 0.0) for channel in color)
    corrected = tuple(math.pow(channel, 1.0 / safe_gamma) for channel in lifted)
    return tuple(channel * gain for channel in corrected)


def apply_vibrance(color, vibrance):
    luma = dot(color, LUMA)
    saturation = max_channel(color) - min(color[0], color[1], color[2])
    vibrance_mix = clamp(1.0 + vibrance * (1.0 - saturation), 0.0, 2.0)
    return mix((luma, luma, luma), color, vibrance_mix)


def apply_color_adjustments(color, lift, post_gamma, gain, vibrance, saturation, contrast):
    color = apply_lift_gamma_gain(color, lift, post_gamma, gain)
    color = apply_vibrance(color, vibrance)
    luma = dot(color, LUMA)
    color = mix((luma, luma, luma), color, saturation)
    color = tuple((channel - 0.5) * contrast + 0.5 for channel in color)
    return tuple(clamp(channel, 0.0, 1.0) for channel in color)


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def test_bright_mask_monotonic():
    previous = -1.0
    for step in range(0, 200):
        value = step / 16.0
        mask = bright_mask((value, value, value), 1.0, 0.25)
        assert_true(mask >= previous - 1.0e-6, "bright mask must be monotonic")
        previous = mask


def test_bright_mask_reaches_full_strength_at_threshold():
    mask = bright_mask((1.0, 1.0, 1.0), 1.0, 0.25)
    assert_true(abs(mask - 1.0) < 1.0e-6, "bright mask should reach full strength at threshold")


def test_bright_mask_preserves_above_threshold_energy():
    color = (1.25, 1.25, 1.25)
    extracted = tuple(channel * bright_mask(color, 1.0, 0.25) for channel in color)
    assert_true(abs(extracted[0] - color[0]) < 1.0e-6, "above-threshold color should be preserved for bloom")


def test_tone_map_monotonic():
    previous = -1.0
    for step in range(0, 1024):
        value = step / 32.0
        mapped = tone_map_hdr((value, value, value), 1.0, 6.0, 0.35, 1.0)[0]
        assert_true(mapped >= previous - 1.0e-6, "tone map must be monotonic")
        previous = mapped


def test_white_point_near_one():
    mapped = tone_map_hdr((6.0, 6.0, 6.0), 1.0, 6.0, 0.35, 1.0)[0]
    assert_true(abs(mapped - 1.0) < 0.05, "white point should map close to display white")


def test_no_nan_edge_cases():
    cases = [
        ((0.0, 0.0, 0.0), 0.0, 1.0, 0.0, 0.0),
        ((32.0, 0.1, 0.1), 0.01, 16.0, 1.0, 4.0),
        ((0.001, 0.002, 0.003), 8.0, 1.0, 0.0, 0.0),
    ]
    for color, exposure, white_point, desat, compression in cases:
        mapped = tone_map_hdr(color, exposure, white_point, desat, compression)
        adjusted = apply_color_adjustments(mapped, -0.25, 2.5, 2.0, 1.0, 2.0, 3.0)
        for channel in adjusted:
            assert_true(math.isfinite(channel), "color adjustment produced a non-finite value")


def main():
    tests = [
        test_bright_mask_monotonic,
        test_bright_mask_reaches_full_strength_at_threshold,
        test_bright_mask_preserves_above_threshold_energy,
        test_tone_map_monotonic,
        test_white_point_near_one,
        test_no_nan_edge_cases,
    ]

    for test in tests:
        test()

    print("hdr_postprocess_math: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

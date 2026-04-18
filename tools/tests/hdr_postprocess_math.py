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


def bloom_contribution(color, threshold, soft_knee):
    brightness = dot(color, LUMA)
    if brightness <= 1.0e-4:
        return 0.0

    if threshold <= 1.0e-4:
        return 1.0

    knee = max(threshold * soft_knee, 0.0)
    soft = 0.0
    if knee > 1.0e-4:
        soft = brightness - threshold + knee
        soft = clamp(soft, 0.0, 2.0 * knee)
        soft = (soft * soft) / max(4.0 * knee, 1.0e-4)

    hard = max(brightness - threshold, 0.0)
    contribution = max(hard, soft)
    return contribution / brightness


def extract_bloom(color, threshold, soft_knee):
    contribution = bloom_contribution(color, threshold, soft_knee)
    return tuple(channel * contribution for channel in color)


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


def test_bloom_contribution_monotonic():
    previous = -1.0
    for step in range(0, 200):
        value = step / 16.0
        contribution = bloom_contribution((value, value, value), 1.0, 0.25)
        assert_true(contribution >= previous - 1.0e-6, "bloom contribution must be monotonic")
        previous = contribution


def test_bloom_threshold_is_soft():
    contribution = bloom_contribution((1.0, 1.0, 1.0), 1.0, 0.25)
    assert_true(contribution > 0.0, "soft knee should start contributing at threshold")
    assert_true(contribution < 0.1, "threshold hit should not keep the full source color")


def test_bloom_extract_keeps_only_excess_energy():
    color = (1.25, 1.25, 1.25)
    extracted = extract_bloom(color, 1.0, 0.25)
    assert_true(extracted[0] > 0.0, "above-threshold color should still contribute to bloom")
    assert_true(extracted[0] < color[0], "bloom should keep only energy above threshold")


def test_zero_threshold_extracts_full_color():
    color = (0.8, 0.4, 0.2)
    extracted = extract_bloom(color, 0.0, 0.15)
    for extracted_channel, source_channel in zip(extracted, color):
        assert_true(abs(extracted_channel - source_channel) < 1.0e-6, "zero threshold should keep the full color")


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
        test_bloom_contribution_monotonic,
        test_bloom_threshold_is_soft,
        test_bloom_extract_keeps_only_excess_energy,
        test_zero_threshold_extracts_full_color,
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

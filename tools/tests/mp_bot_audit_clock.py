#!/usr/bin/env python3
"""Audit extensions must never count warmup, partial lines or a reset clock."""
from mp_bot_audit import SimulationProgress


def main():
    text = ("MP_STATE time=900000 phase=0\n"
            "^2MP_BOT_AUDIT_BEGIN \r\n"
            "MP_STATE time=1000 phase=3\n"
            "noise MP_STATE time=999999 phase=3\n"
            "MP_STATE time=271000 phase=3\n")
    # Every possible read boundary must preserve the same measured interval.
    for split in range(len(text) + 1):
        progress = SimulationProgress()
        progress.feed(text[:split])
        progress.feed(text[split:])
        assert progress.seconds == 270.0
    progress = SimulationProgress()
    for byte in text:
        progress.feed(byte)
    assert progress.seconds == 270.0
    progress.feed("MP_STATE time=900000 phase=3")
    assert progress.seconds == 270.0  # a partial line is not a sample
    progress.feed("\n")
    assert progress.seconds == 899.0
    progress.feed("MP_STATE time=1 phase=3\n")
    assert progress.seconds is None
    progress.feed("MP_STATE time=2000000 phase=3\n")
    assert progress.seconds is None  # later samples cannot hide a reset
    progress = SimulationProgress()
    progress.feed("MP_STATE time=900000 phase=3\n")
    assert progress.seconds is None
    progress.feed("MP_BOT_AUDIT_BEGIN\nMP_STATE time=1 phase=3\n")
    assert progress.seconds == 0.0
    progress.feed("MP_BOT_AUDIT_BEGIN\nMP_STATE time=900000 phase=3\n")
    assert progress.seconds is None
    print("mp_bot_audit_clock: OK (warmup, split reads, partial lines and resets fail closed)")


if __name__ == "__main__":
    main()

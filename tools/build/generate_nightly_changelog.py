#!/usr/bin/env python3
"""Compatibility entry point for the release changelog generator."""

from __future__ import annotations

import sys

from generate_release_changelog import main


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

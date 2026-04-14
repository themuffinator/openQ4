#!/usr/bin/env python3
"""Compatibility entry point for OpenQ4 release packaging."""

from __future__ import annotations

import sys

from package_nightly import main


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

#!/usr/bin/env bash
# Adapted from Emile Belanger (emileb), https://github.com/emileb/openQ4
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$REPO_ROOT/tools/debug/run_renderer.sh" vulkan "$@"

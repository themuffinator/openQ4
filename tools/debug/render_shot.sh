#!/usr/bin/env bash
# Portable adaptation of Emile Belanger's engine-render-target capture harness.
# Usage: OPENQ4_BASEPATH=/path/to/Quake4 render_shot.sh arb2|glesd3|vulkan [--mode mp ...]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
case "${1:-}" in
    arb2) API=gl ;;
    glesd3) API=gles ;;
    vulkan) API=vk ;;
    *) echo "usage: $0 arb2|glesd3|vulkan [smoke options]" >&2; exit 2 ;;
esac
shift
exec python3 "$REPO_ROOT/tools/debug/render_smoke.py" \
    --runtime "${OPENQ4_RUNTIME:-$REPO_ROOT/.install}" \
    --basepath "${OPENQ4_BASEPATH:?set OPENQ4_BASEPATH to the retail Quake 4 installation}" \
    --render-api "$API" "$@"

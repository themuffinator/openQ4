#!/usr/bin/env python3
"""Pin the lazy Vulkan display-color fragment module to its GLSL source."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / 'tools/build/spirv_to_header.py'
SHADERS = ROOT / 'src/renderer/Vulkan/shaders'


def main():
    sys.path.insert(0, str(GENERATOR.parent))
    import spirv_to_header
    if spirv_to_header.find_glslang(None) is None:
        print('vk_display_color_mapping_shader_pin: skipped (glslangValidator unavailable)')
        return
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='vk-display-pin-', dir=ROOT / '.tmp') as temporary:
        generated = Path(temporary) / 'display_color_mapping_spv.h'
        subprocess.run([sys.executable, str(GENERATOR), '--header-out', str(generated),
                        '--guard', '__VK_DISPLAY_COLOR_MAPPING_SPV_H__',
                        str(SHADERS / 'display_color_mapping.frag')], check=True,
                       env={**os.environ, 'TMPDIR': temporary})
        normalize = lambda data: data.replace(b'\r\n', b'\n')
        assert normalize(generated.read_bytes()) == normalize((SHADERS / generated.name).read_bytes()), \
            'display-color shader header is stale; regenerate with tools/build/spirv_to_header.py'
    print('vk_display_color_mapping_shader_pin: passed')


if __name__ == '__main__':
    main()

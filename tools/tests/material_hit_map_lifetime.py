#!/usr/bin/env python3
"""Execute hit-map allocation/replacement/release with distinct module owners."""
import os
from pathlib import Path
import subprocess
import tempfile
from sound_settings import body

ROOT = Path(__file__).resolve().parents[2]


def source(mutation=None):
    material = (ROOT / 'src/renderer/Material.cpp').read_text()
    manager = (ROOT / 'src/framework/DeclManager.cpp').read_text()
    release = body(material, 'if ( materialTypeArray != NULL )')
    replace = body(material, 'else if (!token.Icmp("materialImage"))')
    replace = replace[replace.index('{'):]
    get = body(manager, 'byte *idDeclManagerLocal::GetMaterialTypeArray(')
    free = body(manager, 'void idDeclManagerLocal::FreeMaterialTypeArray(')
    if mutation == 'foreign_free':
        release = release.replace('declManager->FreeMaterialTypeArray', 'Mem_Free')
    elif mutation == 'overwrite':
        replace = replace.replace('declManager->FreeMaterialTypeArray( materialTypeArray );', '')
    elif mutation == 'missing_release':
        free = free.replace('Mem_Free( array );', '(void)array;')
    fixture = (ROOT / 'tools/tests/native/MaterialHitMapLifetimeTest.cpp').read_text()
    return (fixture.replace('// @GET@', get).replace('// @FREE@', free)
            .replace('// @RELEASE@', release).replace('// @REPLACE@', replace))


def main():
    header = (ROOT / 'src/framework/declManager.h').read_text()
    assert 'FreeMaterialTypeArray( byte *array ) = 0;' in header
    abi = (ROOT / 'src/renderer/RenderModuleAPI.h').read_text()
    assert '#define RENDER_API_VERSION\t\t\t34' in abi
    scratch = ROOT / '.tmp/material-hit-map-lifetime'
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as directory:
        env = dict(os.environ, TEMP=directory, TMP=directory, TMPDIR=directory)
        for mutation in (None, 'foreign_free', 'overwrite', 'missing_release'):
            cpp = Path(directory) / ((mutation or 'production') + '.cpp')
            exe = cpp.with_suffix('.exe' if os.name == 'nt' else '')
            cpp.write_text(source(mutation))
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(cpp), '-o', str(exe)], cwd=directory, env=env, check=True)
            result = subprocess.run([str(exe)], cwd=directory, env=env, text=True, capture_output=True)
            if mutation:
                assert result.returncode != 0 and 'FAIL' in result.stderr, result
                print(mutation + ': rejected (' + result.stderr.strip() + ')')
            else:
                assert result.returncode == 0, result.stdout + result.stderr
                print(result.stdout.strip())


if __name__ == '__main__':
    main()

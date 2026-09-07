#!/usr/bin/env python3
"""Compile and exercise the production chat model without a game or input devices."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '.tmp' / 'chat-history-test'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'tools/tests/native/ChatHistoryTest.cpp'
    exe = OUT / ('chat_history.exe' if os.name == 'nt' else 'chat_history')
    if os.name == 'nt':
        command = OUT / 'build.cmd'
        command.write_text('@echo off\ncall "' + str(ROOT / 'tools/build/openq4_devcmd.cmd') + '"\n'
            'if errorlevel 1 exit /b 1\n' + subprocess.list2cmdline([
                'cl', '/nologo', '/std:c++17', '/EHsc', '/W4', str(source),
                '/Fo:' + str(OUT / 'chat_history.obj'), '/Fe:' + str(exe)]) + '\n', encoding='utf-8')
        subprocess.run(['cmd', '/d', '/c', str(command)], cwd=OUT, check=True)
    else:
        subprocess.run([os.environ.get('CXX', shutil.which('c++') or 'c++'), '-std=c++17',
            '-Wall', '-Wextra', '-pedantic', str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], cwd=OUT, check=True)

if __name__ == '__main__':
    main()

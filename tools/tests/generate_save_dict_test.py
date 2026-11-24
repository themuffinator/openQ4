"""Compile the production dictionary writer against an independent wire oracle."""
from pathlib import Path
import sys

parts = []
for path in sys.argv[1:2]:
    source = Path(path).read_text(encoding='utf-8')
    for method in ('WriteChecked', 'FlushWriteBuffer', 'Write', 'WriteInt', 'WriteJoint',
                   'WriteShort', 'WriteByte', 'WriteSignedChar', 'WriteFloat', 'WriteBool',
                   'WriteString', 'WriteDict', 'InvalidateUserInterface', 'WriteUserInterface'):
        start = source.index(f'void idSaveGame::{method}(')
        opening = source.index('{', start)
        depth, end = 1, opening + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        parts.append(source[start:end].replace('idSaveGame::', 'idSaveGame_game::'))
Path(sys.argv[2]).write_text('\n'.join(parts) + '\n', encoding='utf-8')

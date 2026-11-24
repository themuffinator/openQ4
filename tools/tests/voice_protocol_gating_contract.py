#!/usr/bin/env python3
"""Pin the capability gates that keep openQ4 voice away from peers without it.

Older openQ4 releases speak the same 2.41 protocol but do not know the
appended unreliable VOICE message ids; an engine that receives one prints
"unknown unreliable ..." for every packet. Mixed releases are refused at
connect today (pure pak identity or the non-pure decl checksum, see
voice_mixed_version.py), so these gates are the second line: the server
admits and forwards voice only for peers advertising `ui_voiceProtocol`, and a
client sends only to a server advertising `si_voiceProtocol`. Both are
read-only, so a peer cannot claim a version it does not implement.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMMS = ROOT / 'src/game/mp/VoiceComms.cpp'
NETWORK = ROOT / 'src/framework/async/AsyncNetwork.h'
PACKET = ROOT / 'src/sound/VoicePacket.h'


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    depth, index = 0, text.index('{', start)
    for position in range(index, len(text)):
        depth += {'{': 1, '}': -1}.get(text[position], 0)
        if depth == 0:
            return text[start:position + 1]
    raise AssertionError(f'unterminated {signature}')


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    comms = COMMS.read_text(encoding='utf-8')
    network = NETWORK.read_text(encoding='utf-8')
    packet = PACKET.read_text(encoding='utf-8')
    gate = 'GetInt( "ui_voiceProtocol" ) != oq4voice::Version'

    require(re.search(r'idCVar si_voiceProtocol\( "si_voiceProtocol", "1", [^;]*CVAR_ROM \| CVAR_SERVERINFO', comms) is not None,
            'si_voiceProtocol must stay a read-only server-info capability')
    require(re.search(r'idCVar ui_voiceProtocol\( "ui_voiceProtocol", "1", [^;]*CVAR_ROM \| CVAR_USERINFO', comms) is not None,
            'ui_voiceProtocol must stay a read-only user-info capability')
    require(re.search(r'constexpr int Version = 1;', packet) is not None, 'voice wire version moved; update both capabilities together')

    forward = function(comms, 'void idMultiplayerGame::ReceiveAndForwardVoiceData(')
    sender_gate = forward.index('from->GetUserInfo()->' + gate)
    require(sender_gate < forward.index('.Accept('), 'the sender capability gate must precede admission')
    loop = forward[forward.index('for ( int i = 0; i < gameLocal.numClients; ++i )'):]
    require(('to->GetUserInfo()->' + gate) in loop and loop.index('to->GetUserInfo()->' + gate) < loop.index('ServerSendVoice'),
            'every recipient must advertise ui_voiceProtocol before ServerSendVoice')
    require(loop.count('ServerSendVoice') == 1, 'one guarded recipient send only')

    transmit = function(comms, 'void idMultiplayerGame::XmitVoiceData(')
    active = transmit[transmit.index('const bool active'):transmit.index(';', transmit.index('const bool active'))]
    require('gameLocal.serverInfo.GetInt( "si_voiceProtocol" ) == oq4voice::Version' in active,
            'a client must not transmit to a server without si_voiceProtocol')
    require(transmit.index('if ( !active ) return;') < transmit.index('ClientSendVoice'), 'ClientSendVoice must follow the active gate')

    # The game module is the only voice sender; no engine path bypasses the gates.
    callers = [path for path in (ROOT / 'src').rglob('*.cpp')
               if re.search(r'\b(?:ServerSendVoice|ClientSendVoice)\s*\(', path.read_text(encoding='utf-8', errors='replace'))]
    allowed = {COMMS.resolve(), (ROOT / 'src/framework/async/NetworkSystem.cpp').resolve()}
    require({path.resolve() for path in callers} <= allowed, f'unexpected voice senders: {sorted(map(str, callers))}')

    # The voice ids were appended; older peers keep their existing numbering.
    for prefix in ('SERVER_UNRELIABLE_MESSAGE_', 'CLIENT_UNRELIABLE_MESSAGE_'):
        block = re.search(rf'enum \{{\s*({prefix}EMPTY = 0,.*?)\}};', network, re.S)
        require(block is not None, f'{prefix} enum not found')
        names = re.findall(rf'({prefix}\w+)', block[1])
        require(names[-1] == prefix + 'VOICE', f'{prefix}VOICE must remain the last, appended id')
    print('voice_protocol_gating_contract: PASS (read-only capabilities gate admission, forwarding and transmission; ids appended)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

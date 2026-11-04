#!/usr/bin/env python3
"""Reject semantically damaged current saves, show recovery UI and load again.

All fixtures retain a valid outer CRC, so this exercises restore-time validation
after map teardown. Engine console scripts issue game commands and screenshots;
no OS input or screen capture is used. Source saves are never modified.
"""
from pathlib import Path
import argparse
import json
import os
import re
import struct
import subprocess
import time
import zlib

from savegame_restore_cleanup_runtime import ROOT, checksum_fixture, sha


def class_table(data):
    """Decode the current save's object identities and their class-name offsets."""
    _, metadata = checksum_fixture(data)
    position = data.index(b'OQ4S') + 12  # magic, version and build

    def string():
        nonlocal position
        size = struct.unpack_from('<i', data, position)[0]
        position += 4
        assert 0 <= size < 4096 and position+size <= len(data)
        offset = position
        value = data[position:position+size].decode('ascii')
        position += size
        return offset, value

    assert string()[1] == metadata['snapshot']
    assert struct.unpack_from('<i', data, position)[0] == metadata['source_count']
    position += 4
    assert string()[1] == metadata['abi']
    count = struct.unpack_from('<i', data, position)[0]
    position += 4
    assert count == metadata['objects']
    entries = []
    for index in range(1, count+1):
        offset, name = string()
        entries.append(dict(index=index, offset=offset, name=name))
    return entries, metadata


def object_class_fixtures(data):
    """Known classes outside the three standalone save-object families."""
    entries, metadata = class_table(data)
    result = {}
    for label, original, replacement in (
            ('StandaloneWeapon', 'idThread', 'rvWeapon'),
            ('StandalonePhysics', 'idLocationEntity', 'idPhysics_Player'),
            ('StandaloneRoot', 'idLight', 'idClass')):
        entry = next(item for item in entries if item['name'] == original)
        assert len(original) == len(replacement)
        changed = bytearray(data)
        offset = entry['offset']
        changed[offset:offset+len(original)] = replacement.encode('ascii')
        assert len(changed) == len(data)
        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
        result[label] = bytes(changed), dict(object_index=entry['index'], class_offset=offset,
            original_class=original, replacement_class=replacement,
            expected="unsupported object-table class '%s' for object %d" % (replacement, entry['index']))
    return result, metadata


def factory_fixtures(data):
    """Replace actual class-table entries with a throwing abstract factory."""
    entries, metadata = class_table(data)
    candidates = [(entry['index'], entry['offset']) for entry in entries if entry['name'] == 'rvClientEffect']
    assert len(candidates) >= 3
    assert 'ABSTRACT_DECLARATION( idClass, rvClientEntity )' in (ROOT/'src/game/client/ClientEntity.cpp').read_text()
    result = {}
    for label, (index, offset) in zip(('EarlyFactory', 'MiddleFactory', 'LateFactory'),
                                    (candidates[0], candidates[len(candidates)//2], candidates[-1])):
        changed = bytearray(data)
        changed[offset:offset+14] = b'rvClientEntity'
        assert len(changed) == len(data)
        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
        result[label] = bytes(changed), dict(object_index=index, class_offset=offset,
            original_class='rvClientEffect', replacement_class='rvClientEntity',
            expected='Cannot instanciate abstract class rvClientEntity.')
    return result, metadata


def gui_fixtures(data):
    """Replace a framed GUI name while retaining its unframed state payload."""
    _, metadata = checksum_fixture(data)
    for marker in re.finditer(re.escape(b'OQ4Y'), data[:-16]):
        offset = marker.start()+8
        size = struct.unpack_from('<i', data, offset)[0]
        name = data[offset+4:offset+4+size]
        if not (11 <= size < 512 and name.startswith(b'guis/')
                and name.endswith((b'.gui', b'.q4ui'))
                and data[offset+4+size] in (0, 1)):
            continue
        original = name.decode('ascii')
        # A missing retained document fails creation, whereas the legacy GUI
        # reader may create a placeholder for a missing definition.
        prefix, suffix = 'guis/', '.q4ui'
        replacement = prefix+'x'*(size-len(prefix)-len(suffix))+suffix
        changed = bytearray(data)
        changed[offset+4:offset+4+size] = replacement.encode('ascii')
        assert len(changed) == len(data)
        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
        detail = dict(sync_offset=marker.start(), name_offset=offset+4,
                      original=original, replacement=replacement,
                      expected="could not load serialized GUI '%s'" % replacement)
        return {'MissingSerializedGui': (bytes(changed), detail)}, metadata
    raise AssertionError('No framed, nonempty GUI record was found.')


def damaged_fixtures(data, spawn_refs=False, threads=False, events=False, legacy_events=False):
    _, metadata = checksum_fixture(data)
    position = metadata['server_info_sync']
    sequence = 0

    def integer():
        nonlocal position
        value = struct.unpack_from('<i', data, position)[0]
        position += 4
        return value

    def string():
        nonlocal position
        size = integer()
        assert 0 <= size < 1048576
        value = data[position:position+size]
        assert len(value) == size
        position += size
        return value

    def skip(size):
        nonlocal position
        assert 0 <= size < 1048576 and position+size <= len(data)
        position += size

    def boolean():
        nonlocal position
        value = data[position]
        assert value in (0, 1)
        position += 1
        return value

    def dictionary():
        nonlocal sequence
        assert integer() == int.from_bytes(b'OQ4Y', 'little') and integer() == sequence
        sequence += 1
        count = integer()
        assert -1 <= count < 65536
        for _ in range(max(0, count)):
            string()
            string()

    def capacity(symbol):
        text = (ROOT/'src/game/Game_local.h').read_text()
        bits = int(re.search(r'#define\s+'+symbol+r'\s+(\d+)', text)[1])
        assert bits == 12, 'Review save fixture parsing if table capacities change.'
        return 1 << bits

    def table(count):
        result = []
        for index in range(count):
            offset = position
            reference, spawn_id = integer(), integer()
            assert 0 <= reference <= metadata['objects']
            if reference:
                result.append(dict(slot=index, offset=offset, reference=reference, spawn_id=spawn_id))
        assert len(result) > 2
        assert len({item['reference'] for item in result}) == len(result)
        return result

    dictionary()  # serverInfo
    clients = integer()
    assert 0 <= clients <= 32
    for _ in range(clients):
        dictionary()  # persistentPlayerInfo
    entities = table(capacity('GENTITYNUM_BITS'))
    client_entities = table(capacity('CENTITYNUM_BITS'))
    first_free_offset = position
    assert 0 <= integer() < capacity('GENTITYNUM_BITS')
    if spawn_refs or threads or events or legacy_events:
        assert 0 <= integer() <= 4096  # num_entities
        assert 0 < integer() <= metadata['objects']  # world
        for maximum in (4096, 8192, 4096, 4096):
            count = integer()  # spawned entities, script proxies, client spawned, active
            assert 0 <= count <= maximum
            skip(count*4)
        assert 0 <= integer() <= 4096  # numEntitiesToDeactivate
        boolean()  # sortPushers
        boolean()  # sortTeamMasters
        dictionary()  # persistentLevelInfo
        skip(12*4+4+3*12*4)  # global shader parms, effects enabled and effect parms
        skip(4)  # random seed
        frame_offset = position
        frame_reference = integer()
        string()  # session command
        if threads:
            return thread_fixtures(data, frame_offset, frame_reference)
        for _ in range(2):  # AI team members
            count = integer()
            assert 0 <= count <= 4096
            skip(count*4)
        skip(2*12*4)  # AI team timers
        count = integer()  # AI avoids
        assert 0 <= count <= 65536
        skip(count*20)
        skip(3*4)  # cinematic skip/stop/max times
        boolean()  # inCinematic
        boolean()  # skipCinematic
        boolean()  # isMultiplayer
        skip(4+3*4+2*4)  # game type, frame/clocks, vacuum area and entity def bits
        boolean()  # isServer
        boolean()  # isClient
        boolean()  # isListenServer
        skip(4+4)  # local client and real client time
        boolean()  # isNewFrame
        boolean()  # mapCycleLoaded
        skip(4)  # spawnCount
        count = integer()  # location entities
        assert 0 <= count <= 65536
        skip(count*4)
        skip(4)  # camera
        string()  # global material
        reference_offset = position
        original = integer()  # lastAIAlertActor
        skip(3*4)  # lastAIAlertEntity and both alert times
        dictionary()  # spawnArgs proves the next complete sync boundary
        if events or legacy_events:
            skip(4*4+12)  # player PVS, connected areas and gravity
            boolean()  # influenceActive
            skip(4)  # nextGibTime
            return event_fixtures(data, position, legacy=legacy_events)
        parsed, _ = render_handle_fixture(data)
        entity = parsed['ForeignModelHandle'][1]
        assert entity['entity_class'] == 'func_static'
        entry = next(item for item in entities if item['slot'] == entity['entity_number'])
        replacement = (entry['spawn_id'] << 12) | entry['slot']
        assert 0 < replacement < 2147483648 and replacement != original
        changed = bytearray(data)
        struct.pack_into('<i', changed, reference_offset, replacement)
        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
        detail = dict(entity=entity['entity'], entity_number=entry['slot'], object_reference=entry['reference'],
                      offset=reference_offset, original=original, replacement=replacement,
                      expected="idEntityPtr::Restore: entity %d has type 'idStaticEntity', expected 'idActor'" % entry['slot'])
        return {'WrongActorSpawnReference': (bytes(changed), detail)}, metadata
    definitions = [
        ('BadObjectIndex', entities[1]['offset'], 2147483647, 'invalid object index 2147483647'),
        ('DuplicateEntity', entities[1]['offset'], entities[0]['reference'], 'invalid entity registration'),
        ('DuplicateClient', client_entities[1]['offset'], client_entities[0]['reference'], 'invalid client entity registration'),
        ('WrongClientType', client_entities[0]['offset'], entities[0]['reference'], "expected 'rvClientEntity'"),
        ('BadFreeIndex', first_free_offset, capacity('GENTITYNUM_BITS'), 'invalid first free entity index 4096'),
    ]
    fixtures = {}
    for name, offset, value, expected in definitions:
        changed = bytearray(data)
        original = struct.unpack_from('<i', data, offset)[0]
        assert original != value
        struct.pack_into('<i', changed, offset, value)
        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
        fixtures[name] = (bytes(changed), dict(offset=offset, original=original, replacement=value, expected=expected))
    metadata.update(clients=clients, registered_entities=len(entities), registered_clients=len(client_entities))
    return fixtures, metadata


def event_fixtures(data, offset, legacy=False):
    """Decode typed queue entries before changing fields or appending a legacy record."""
    entries, metadata = class_table(data)
    classes = {entry['index']: entry['name'] for entry in entries}
    position = offset

    def integer():
        nonlocal position
        assert position+4 <= len(data)-16
        value = struct.unpack_from('<i', data, position)[0]
        position += 4
        return value

    def string():
        nonlocal position
        begin = position
        size = integer()
        assert 0 <= size < 4096 and position+size <= len(data)-16
        value = data[position:position+size].decode('ascii')
        position += size
        return begin, position, value

    formats = {}
    for source in (ROOT/'src/game').rglob('*.cpp'):
        for name, arguments in re.findall(
                r'\bidEventDef\s+\w+\s*\(\s*"([^"\n]+)"\s*(?:,\s*(NULL|"[^"]*"))?', source.read_text()):
            value = arguments.strip('"') if arguments != 'NULL' else ''
            assert name not in formats or formats[name] == value
            formats[name] = value
    count = integer()
    assert 2 <= count <= 8192
    records = []
    for _ in range(count if legacy else 2):
        record = dict(time_offset=position, time=integer())
        record['name_start'], record['name_end'], record['name'] = string()
        record['type_start'], record['type_end'], record['type'] = string()
        record['target_offset'] = position
        record['target'] = integer()
        assert classes[record['target']] == record['type']
        record['format_offset'] = position
        assert integer() == -2
        for argument in formats[record['name']]:
            if argument in 'fdeE':
                integer()
            elif argument == 'y':
                integer(); integer()
            elif argument == 'v':
                integer(); integer(); integer()
            elif argument == 's':
                string()
            else:
                raise AssertionError('Use a source with no trace arguments in the decoded events.')
        records.append(record)
    if legacy:
        assert [r['time'] for r in records] == sorted(r['time'] for r in records)
        assert records[-1]['time'] < 2147483000
        return legacy_event_fixtures(data, offset, position, records, classes, metadata)
    first, second = records
    assert -2147483648 < first['time'] <= second['time']
    result = {}

    def mutate(name, begin, end, replacement, expected, **details):
        assert len(replacement) == end-begin, 'Preserve the embedded footer offset.'
        changed = bytearray(data[:begin]+replacement+data[end:])
        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
        checksum_fixture(changed)
        result[name] = (bytes(changed), dict(offset=begin, original_hex=data[begin:end].hex(),
            replacement_hex=replacement.hex(), expected=expected, **details))

    def number(name, begin, value, expected):
        mutate(name, begin, begin+4, struct.pack('<i', value), expected)

    def text(name, field, value, expected):
        encoded = value.encode('ascii')
        mutate(name, first[field+'_start'], first[field+'_end'], struct.pack('<i', len(encoded))+encoded,
               expected, replacement_text=value)

    number('NegativeEventCount', offset, -1, 'idEvent::Restore: invalid event count -1')
    number('EventQueueOrder', second['time_offset'], first['time']-1, 'idEvent::Restore: event 1 is out of order')
    missing_name, missing_type = 'x'*len(first['name']), 'x'*len(first['type'])
    assert missing_name not in formats and missing_type not in classes.values()
    text('MissingCallback', 'name', missing_name, "idEvent::Restore: unknown event '%s'" % missing_name)
    text('MissingTargetClass', 'type', missing_type, "idEvent::Restore: unknown class '%s'" % missing_type)
    number('NullEventTarget', first['target_offset'], 0, "event '%s' has a NULL target object" % first['name'])
    number('BadEventArguments', first['format_offset'], -1, 'idEvent::Restore: invalid arg size -1')
    # setShader belongs to idLight, not idThread, and has the same name length.
    # Rejection must precede its differing argument layout.
    assert (first['type'], first['name']) == ('idThread', '<execute>')
    unsupported = 'setShader'
    assert formats[unsupported] == 's'
    text('UnsupportedEvent', 'name', unsupported,
         "target class '%s' does not support event '%s'" % (first['type'], unsupported))
    metadata.update(event_count=count, first_events=records)
    return result, metadata


def legacy_event_fixtures(data, offset, end, records, classes, metadata):
    """Synthetic raw-argument records inside a current Windows x64 container.

    The native event test pins these sizes/offsets against the production trace
    types. This exercises the legacy argument reader, not a historical writer.
    """
    assert re.search(r'#define\s+MAX_STRING_LEN\s+128', (ROOT/'src/game/script/Script_Program.h').read_text())
    assert struct.unpack_from('<4sii', data, len(data)-36) == (b'OQ4F', 1, len(data)-36)
    thread = next(index for index, name in classes.items() if name == 'idThread')
    trigger = next(index for index, name in classes.items() if name == 'idTrigger_Multi')

    def string(value):
        value = value.encode('ascii')
        return struct.pack('<i', len(value))+value

    def append(event_name, target, raw, expected):
        event = struct.pack('<i', 2147483000)+string(event_name)+string(classes[target])+struct.pack('<ii', target, len(raw))
        changed = bytearray(data[:end]+event+raw+data[end:])
        struct.pack_into('<i', changed, offset, len(records)+1)
        struct.pack_into('<i', changed, len(changed)-28, len(changed)-36)  # embedded footer offset
        struct.pack_into('<II', changed, len(changed)-8, len(changed)-16, zlib.crc32(changed[:-16]))
        checksum_fixture(changed)
        return bytes(changed), dict(event=event_name, target=target, target_class=classes[target],
            record_offset=end, arguments_offset=end+len(event), expected=expected)

    # Old trace bytes contain obsolete process addresses. Only presence matters.
    trace = bytearray(128)
    struct.pack_into('<f', trace, 0, 0.25)
    struct.pack_into('<Q', trace, 56+40, 0x1234)  # contact material
    struct.pack_into('<Q', trace, 56+64, 0x5678)  # contact material type
    entity = bytes(8)  # an expired/NULL entity reference; the future event never runs
    material = b'_default\0'.ljust(128, b'\0')
    result = {
        'LegacyString': append('print', thread, b'x'*128,
                               "unterminated legacy string on arg 0 of event 'print'"),
        'LegacyTraceFlag': append('<touch>', trigger, entity+b'\x02'+trace+material,
                                  "invalid legacy trace flag on arg 1 of event '<touch>'"),
        'LegacyMaterialName': append('<touch>', trigger, entity+b'\x01'+trace+b'x'*128,
                                    "unterminated legacy trace material on arg 1 of event '<touch>'"),
    }
    metadata.update(event_count=len(records), legacy_trace_size=128, legacy_contact_offset=56,
                    valid_material='_default', material_name_offset=result['LegacyMaterialName'][1]['arguments_offset']+8+1+128,
                    scope='Synthetic legacy argument records in a current container, not original-writer evidence.')
    return result, metadata


def thread_fixtures(data, frame_offset, frame_reference):
    """Decode bounded thread/animated records, including their full sync frames."""
    entries, metadata = class_table(data)
    classes = {entry['index']: entry['name'] for entry in entries}
    thread_ids = [index for index, name in classes.items() if name == 'idThread']
    animated_ids = [index for index, name in classes.items() if name == 'idAnimated']
    assert classes[frame_reference] == 'idThread'
    assert re.search(r'ANIM_NumAnimChannels\s*=\s*5;', (ROOT/'src/game/anim/Anim.h').read_text())

    class Reader:
        def __init__(self, offset):
            self.position = offset
            self.sequence = struct.unpack_from('<i', data, offset+4)[0]

        def integer(self):
            value = struct.unpack_from('<i', data, self.position)[0]
            self.position += 4
            return value

        def skip(self, size):
            assert 0 <= size < 1048576 and self.position+size <= len(data)
            self.position += size

        def string(self):
            size = self.integer()
            assert 0 <= size < 4096
            value = data[self.position:self.position+size].decode('ascii')
            self.skip(size)
            return value

        def boolean(self):
            value = data[self.position]
            assert value in (0, 1)
            self.position += 1
            return value

        def sync(self):
            assert self.integer() == int.from_bytes(b'OQ4Y', 'little') and self.integer() == self.sequence
            self.sequence += 1

    thread_records, animated_records = [], []
    for marker in re.finditer(re.escape(b'OQ4Y'), data):
        reader = Reader(marker.start())
        try:
            reader.sync()  # idThread class start
            for _ in range(2):  # current and waiting thread references
                reference = reader.integer()
                assert reference == 0 or classes.get(reference) == 'idThread'
            reader.skip(8)  # waitingFor and waitingUntil
            depth = reader.integer()
            assert 0 <= depth <= 64
            reader.skip(depth*12)
            assert depth <= reader.integer() <= 64  # maxStackDepth
            used = reader.integer()
            assert 0 <= used <= 12288
            reader.skip(used)
            assert 0 <= reader.integer() <= used  # localstackBase
            assert used <= reader.integer() <= 12288  # maxLocalstackUsed
            reader.skip(12)  # current function, instruction pointer and pending parameters
            reader.string()  # multi-frame event
            reference = reader.integer()  # eventEntity
            assert 0 <= reference <= metadata['objects']
            self_offset = reader.position
            reference = reader.integer()
            assert classes.get(reference) == 'idThread'
            reader.boolean()  # doneProcessing
            reader.boolean()  # threadDying
            terminate_offset = reader.position
            terminate = reader.boolean()
            reader.boolean()  # debug
            reader.sync()  # spawnArgs
            count = reader.integer()
            assert -1 <= count <= 65536
            for _ in range(max(count, 0)):
                reader.string()
                reader.string()
            reader.skip(4)  # thread number
            name = reader.string()
            reader.skip(8)  # last execution and creation times
            reader.boolean()  # manualControl
            reader.skip(4)  # threadIndex
            reader.sync()  # idThread class end
            reader.sync()  # following object class start
            thread_records.append(dict(reference=reference, name=name, self_offset=self_offset,
                                       terminate_offset=terminate_offset, terminate=terminate))
        except (AssertionError, IndexError, UnicodeDecodeError, struct.error):
            pass
        reader = Reader(marker.start())
        try:
            reader.sync()  # idAFEntity_Gibbable class start
            reader.boolean()  # gibbed
            reader.boolean()  # hasCombatModel
            reader.sync()  # idAFEntity_Gibbable class end
            reader.sync()  # idAnimated class start
            count = reader.integer()
            assert 0 <= count <= 65536
            assert 0 <= reader.integer() <= count  # current animation
            assert 0 <= reader.integer() <= 65536  # animation handle
            reader.skip(12)  # blend frames, sound joint and activator spawn ID
            reader.boolean()  # activated
            owner_offset = reader.position
            owner = reader.integer()
            assert owner == 0 or classes.get(owner) == 'idThread'
            state_offset = reader.position
            reader.string()  # state
            reader.string()  # idealState
            reader.skip(5*4)  # animation completion times
            reader.sync()  # idAnimated class end
            reader.sync()  # following object class start
            animated_records.append(dict(owner_offset=owner_offset, owner=owner, state_offset=state_offset))
        except (AssertionError, IndexError, UnicodeDecodeError, struct.error):
            pass
    assert [record['reference'] for record in thread_records] == thread_ids, 'Thread record/table identity mismatch'
    assert len(animated_records) == len(animated_ids) and len(animated_ids) >= 2, 'Animated record/table count mismatch'
    for record, index in zip(animated_records, animated_ids):
        record['object_reference'] = index
    frame = next(record for record in thread_records if record['reference'] == frame_reference)
    assert frame['name'] == 'frameCommands' and frame['terminate'] == 0
    unowned = [record for record in animated_records if not record['owner']]
    assert len(unowned) >= 2
    first, second = unowned[:2]
    claimed = {record['owner'] for record in animated_records} | {frame_reference}
    shared_thread = next(record for record in thread_records if record['reference'] not in claimed)
    # Each edit retains both the original save length and a correct outer CRC.
    definitions = [
        ('DuplicateAnimatedOwner', [(first['owner_offset'], shared_thread['reference'], '<i'),
                                    (second['owner_offset'], shared_thread['reference'], '<i')],
         'duplicate owning reference to script thread'),
        ('DuplicateFrameOwner', [(first['owner_offset'], frame_reference, '<i')],
         'duplicate owning reference to script thread'),
        ('PartialAnimatedOwner', [(first['owner_offset'], shared_thread['reference'], '<i'),
                                 (first['state_offset'], -1, '<i')],
         'idRestoreGame::ReadString: invalid length (-1)'),
        ('WrongInterpreterOwner', [(frame['self_offset'], shared_thread['reference'], '<i')],
         'saved thread does not match interpreter owner'),
        ('NullInterpreterOwner', [(frame['self_offset'], 0, '<i')],
         'saved thread does not match interpreter owner'),
        ('OwnedThreadAutoDelete', [(frame['terminate_offset'], 1, '<B')],
         "owned script thread 'frameCommands' would delete itself"),
    ]
    result = {}
    for name, edits, expected in definitions:
        changed = bytearray(data)
        for offset, value, fmt in edits:
            struct.pack_into(fmt, changed, offset, value)
        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
        result[name] = bytes(changed), dict(edits=edits, expected=expected)
    metadata.update(frame_owner_offset=frame_offset, frame=frame, animated_owners=[first, second], shared_thread=shared_thread,
                    thread_records=len(thread_records), animated_records=len(animated_records))
    return result, metadata


def af_fixtures(data):
    """Decode an AF entity prefix and body table before mutating its identity.

    Only active ragdolls with attached heads are selected, rather than an earlier
    map light that also uses AF physics. Contact records are decoded as well.
    Every intervening class/static-object sync is checked, and no offsets are
    inferred from a raw occurrence of a body or declaration name.
    """
    entries, metadata = class_table(data)
    classes = {entry['index']: entry['name'] for entry in entries}
    foreign = next(index for index, name in classes.items() if name == 'idWorldspawn')
    for marker in re.finditer(re.escape(b'OQ4Y'), data):
        position = marker.start()
        sequence = struct.unpack_from('<i', data, position+4)[0]

        def integer():
            nonlocal position
            value = struct.unpack_from('<i', data, position)[0]
            position += 4
            return value

        def skip(size):
            nonlocal position
            assert 0 <= size < 1048576 and position+size <= len(data)-16
            position += size

        def string():
            size = integer()
            assert 0 <= size < 4096
            value = data[position:position+size].decode('ascii')
            skip(size)
            return value

        def boolean():
            nonlocal position
            value = data[position]
            assert value in (0, 1)
            position += 1
            return value

        def sync():
            nonlocal sequence
            assert integer() == int.from_bytes(b'OQ4Y', 'little') and integer() == sequence
            sequence += 1

        def body():
            offset = position+4
            name = string()
            assert re.fullmatch(r'[A-Za-z_][A-Za-z_0-9 ]*', name)
            skip(300)  # typed scalars, vectors, matrices and solver indexes
            assert data[position] < 64  # six packed flags
            skip(1)
            return dict(offset=offset, name=name)

        try:
            sync()  # idAFEntity_Base begins
            boolean()  # noPlayerImpactFX
            skip(4)  # combat model contents
            if boolean():  # combat clip model
                boolean()
                skip(4+4+4+12+36+24+24+4)
                string()
                skip(8)
                boolean()
                skip(4)
                boolean()
            skip(12+36+4)  # spawn pose and sound time
            owner_offset = position
            owner = integer()
            assert owner in classes and classes[owner] == 'idAFEntity_WithAttachedHead'
            name_offset = position+4
            name = string()
            assert re.fullmatch(r'[A-Za-z_][A-Za-z_0-9/]*', name)
            boolean()  # bind constraints
            skip(12+36+8)  # base pose, pose/rest times
            loaded_offset = position
            assert boolean() == 1
            active_offset = position
            assert boolean() == 1
            sync()  # WriteStaticObject(physicsObj)
            empty = 0
            for _ in range(2):  # idClass and optional idPhysics empty frames
                if data[position+8:position+12] != b'OQ4Y':
                    break
                sync()
                sync()
                empty += 1
            assert empty in (1, 2)
            sync()  # idPhysics_Base
            assert integer() == owner
            skip(4+12+12)
            contacts = integer()
            assert 0 <= contacts <= 4096
            for _ in range(contacts):
                assert 0 <= integer() <= 3  # contact type
                skip(12+12+4+4)  # point, normal, distance, contents
                string()  # material
                skip(4+4)  # features
                assert 0 <= integer() < 4096  # entity number
                skip(4)  # clip ID
                string()  # material type
            contact_entities = integer()
            assert 0 <= contact_entities <= 4096
            skip(contact_entities*4)  # spawn IDs
            sync()
            sync()  # idPhysics_AF
            count = integer()
            assert 1 <= count <= 128
            bodies = [body() for _ in range(count)]
            assert len({b['name'].lower() for b in bodies}) == count
            if boolean():
                body()  # optional master body
            constraints = integer()
            assert 1 <= constraints <= 256
            type_offset = position
            constraint_type = integer()
            assert 1 <= constraint_type <= 14
            constraint_offset = position+4
            constraint_name = string()
            assert re.fullmatch(r'[A-Za-z_][A-Za-z_0-9 ]*', constraint_name)
            assert all(integer() == -1 for _ in range(6))
            integer()  # solver first index
            assert data[position] < 64
            definitions = [
                ('ForeignAFOwner', [(owner_offset, foreign, '<i')], 'idAF::Restore: inconsistent owner reference'),
                ('NullLoadedAFOwner', [(owner_offset, 0, '<i')], 'idAF::Restore: inconsistent loaded/active state'),
                ('ActiveUnloadedAF', [(loaded_offset, 0, '<B'), (active_offset, 1, '<B')], 'idAF::Restore: inconsistent loaded/active state'),
                ('WrongAFConstraintType', [(type_offset, 0, '<i')], f'type mismatch 0 (expected {constraint_type})'),
            ]
            result = {}
            detail = dict(owner=owner, classname=classes[owner], figure=name, active=True,
                          bodies=bodies, constraints=constraints, constraint_type=constraint_type,
                          constraint_name=constraint_name, prefix_offset=marker.start())
            for label, edits, expected in definitions:
                changed = bytearray(data)
                for offset, value, fmt in edits:
                    struct.pack_into(fmt, changed, offset, value)
                struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
                result[label] = bytes(changed), dict(**detail, edits=edits, expected=expected)
            for label, offset, original, expected in (
                ('MissingAFDefinition', name_offset, name, 'idAF::Restore: could not reconstruct loaded figure'),
                ('WrongAFBodyName', bodies[0]['offset'], bodies[0]['name'], 'idAFBody::Restore: body name mismatch'),
                ('WrongAFConstraintName', constraint_offset, constraint_name, 'idAFConstraint::Restore: constraint name mismatch'),
            ):
                changed = bytearray(data)
                replacement = 'x'*len(original)
                assert replacement != original
                changed[offset:offset+len(original)] = replacement.encode('ascii')
                struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
                result[label] = bytes(changed), dict(**detail, name_offset=offset,
                    original=original, replacement=replacement, expected=expected)
            return result, metadata
        except (AssertionError, IndexError, UnicodeError, struct.error):
            continue
    raise AssertionError('No loaded articulated-figure prefix and body table were found.')


def animation_fixtures(data):
    """Decode complete actor frames before editing embedded IK/animation owners."""
    entries, metadata = class_table(data)
    classes = {entry['index']: entry['name'] for entry in entries}
    foreign = next(index for index, name in classes.items() if name == 'idWorldspawn')
    for marker in re.finditer(re.escape(b'OQ4Y'), data):
        position = marker.start()
        sequence = struct.unpack_from('<i', data, position+4)[0]
        calls = []

        def integer():
            nonlocal position
            value = struct.unpack_from('<i', data, position)[0]
            position += 4
            return value

        def skip(size):
            nonlocal position
            assert 0 <= size < 1048576 and position+size <= len(data)
            position += size

        def string():
            size = integer()
            assert 0 <= size < 4096
            value = data[position:position+size].decode('ascii')
            skip(size)
            return value

        def boolean():
            nonlocal position
            value = data[position]
            assert value in (0, 1)
            position += 1
            return value

        def count(maximum):
            value = integer()
            assert 0 <= value <= maximum
            return value

        def sync():
            nonlocal sequence
            assert integer() == int.from_bytes(b'OQ4Y', 'little') and integer() == sequence
            sequence += 1

        def state_thread():
            name = string()
            skip(8)  # result and typed flags
            for kind in ('active', 'interrupted'):
                for _ in range(count(65536)):
                    offset = position+4
                    function = string()
                    assert function
                    calls.append(dict(kind=kind, offset=offset, function=function))
                    skip(20)  # flags, delay and state parameters
            return name

        try:
            sync()  # idAFEntity_Gibbable start
            boolean()
            boolean()
            sync()  # idAFEntity_Gibbable end
            sync()  # idActor start
            skip(8+36)  # team, rank, viewAxis
            skip(count(4096)*4)  # enemies
            skip(8+12+4*12+8)  # lightning, FOVs, offsets/angles, pain timers
            for _ in range(count(65536)):
                string()  # damage groups
            skip(count(65536)*4)  # damage scales
            boolean()  # inDamageEvent
            boolean()  # use_combat_bbox
            skip(7*4)  # joints
            initialized_offset = position
            initialized = boolean()
            active_offset = position
            active = boolean()
            ik_owner_offset = position
            ik_owner = integer()
            assert ik_owner == 0 or ik_owner in classes
            ik_anim_offset = position+4
            ik_anim = string()
            skip(12)  # model offset
            if boolean():  # foot clip model
                boolean()
                skip(4+4+4+12+36+24+24+4)
                string()
                skip(8)
                boolean()
                skip(4)
                boolean()
            count(8)  # numLegs
            count(255)  # enabledLegs
            skip(5*8*4+4+2*8*12+2*8*4+2*8*36)  # joint arrays, directions, lengths, axes
            skip(8*4)  # smoothing/trace distances
            boolean()
            boolean()
            skip(4+4+12)  # pivot
            boolean()
            skip(4+8*4+12)  # old heights, waist offset
            for _ in range(3):
                string()  # animation/pain names
            skip(4*4)  # blink
            states = []
            for expected_channel in (3, 1, 2):  # HEAD, TORSO, LEGS
                boolean()
                skip(8)
                owner_offset = position
                owner, animator_owner, channel = integer(), integer(), integer()
                assert owner == animator_owner and owner in classes and channel == expected_channel
                assert ik_owner in (0, owner)
                if states:
                    assert owner == states[0]['owner']
                boolean()
                thread_name = state_thread()
                assert thread_name.endswith(f'_anim_{channel}')
                states.append(dict(owner=owner, owner_offset=owner_offset, animator_offset=owner_offset+4,
                                   thread_name=thread_name, channel=channel))
            state_thread()
            skip(4)  # head spawn ID
            for _ in range(3):
                boolean()
            skip(4)  # pain time
            skip(count(4096)*8)  # attachments
            skip(8+4+4+12)  # vehicle controller and flashlight handle/joint/offset
            sync()  # render light
            skip(36+12+8)
            for _ in range(6):
                boolean()
            skip(4+7*12+4)
            string()  # material
            skip(12*4+4+4+12+4)  # shader parms, sound and death push
            sync()  # idActor end
            assert classes[owner] == 'rvMonsterGrunt' and calls
            # Some stock actors have no leg IK. Mark those records initialized
            # to exercise the resource gate without changing the payload length.
            definitions = [
                ('ForeignIKOwner', [(ik_owner_offset, foreign, '<i')], 'idIK::Restore: inconsistent owner reference'),
                ('NullActiveIKOwner', [(ik_owner_offset, 0, '<i'), (initialized_offset, 1, '<B')], 'idIK::Restore: active IK has no owner, model or animation'),
                ('ForeignAnimOwner', [(states[0]['owner_offset'], foreign, '<i')], 'idAnimState::Restore: inconsistent owner references'),
                ('NullAnimOwner', [(states[0]['owner_offset'], 0, '<i')], 'idAnimState::Restore: inconsistent owner references'),
                ('ForeignAnimatorOwner', [(states[0]['animator_offset'], foreign, '<i')], 'idAnimState::Restore: inconsistent owner references'),
                ('NullAnimatorOwner', [(states[0]['animator_offset'], 0, '<i')], 'idAnimState::Restore: inconsistent owner references'),
            ]
            result = {}
            for label, edits, expected in definitions:
                changed = bytearray(data)
                for offset, value, fmt in edits:
                    struct.pack_into(fmt, changed, offset, value)
                struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
                result[label] = bytes(changed), dict(object_reference=owner, object_class=classes[owner],
                    states=states, class_offset=marker.start(), edits=edits, expected=expected)
            changed = bytearray(data)
            struct.pack_into('<i', changed, ik_owner_offset, owner)
            struct.pack_into('<B', changed, initialized_offset, 1)
            changed[ik_anim_offset:ik_anim_offset+len(ik_anim)] = b'x'*len(ik_anim)
            struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
            result['MissingIKAnimation'] = bytes(changed), dict(object_reference=owner, animation=ik_anim,
                animation_offset=ik_anim_offset, expected='idIK::Restore: active IK has no owner, model or animation')
            call = calls[0]
            changed = bytearray(data)
            replacement = 'x'*len(call['function'])
            changed[call['offset']:call['offset']+len(replacement)] = replacement.encode('ascii')
            struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
            result['UnknownActorState'] = bytes(changed), dict(object_reference=owner, call=call,
                expected="stateCall_t::Restore: unknown state '%s'" % replacement)
            return result, metadata
        except (AssertionError, IndexError, UnicodeError, struct.error):
            continue
    raise AssertionError('No complete actor frame was found.')


def render_handle_fixture(data, bindings=False, movers=False, physics=False):
    """Locate a v4 entity with empty GUI slots, using its named class record.

    The parser checks every intervening sync and bounded record. It skips records
    with GUI payloads rather than assuming their variable legacy layout.
    """
    _, metadata = checksum_fixture(data)
    markers = list(re.finditer(re.escape(b'OQ4Y'), data))
    mover_records = []
    for candidate in markers:
        position = candidate.start()
        sequence = struct.unpack_from('<i', data, position+4)[0]

        def integer():
            nonlocal position
            value = struct.unpack_from('<i', data, position)[0]
            position += 4
            return value

        def skip(size):
            nonlocal position
            assert 0 <= size < 1048576 and position+size <= len(data)
            position += size

        def string():
            size = integer()
            assert 0 <= size < 65536
            value = data[position:position+size]
            skip(size)
            return value.decode('ascii')

        def sync():
            nonlocal sequence
            assert integer() == int.from_bytes(b'OQ4Y', 'little') and integer() == sequence
            sequence += 1

        def boolean():
            nonlocal position
            value = data[position]
            assert value in (0, 1)
            position += 1
            return value

        try:
            sync()  # idEntity class start
            number, definition, snapshot, bits = integer(), integer(), integer(), integer()
            assert 0 <= number < 4095 and -1 <= definition < 65536
            name = string()
            assert re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name)
            sync()  # spawnArgs
            count = integer()
            assert 0 < count < 4096
            args = {}
            for _ in range(count):
                key = string()
                args[key] = string()
            assert args.get('name') == name and args.get('classname')
            if movers and args['classname'] not in ('func_door', 'func_plat', 'func_button'):
                continue
            typename = string()
            if typename:
                skip(integer())  # script object bytes
            skip(4+4+1+4)  # thinkFlags, dormantStart, cinematic, cameraTarget
            targets = integer()
            assert 0 <= targets < 4096
            skip(targets*4)
            skip(4)  # health
            clients = integer()
            assert 0 <= clients < 4096
            skip(clients*4)
            skip(4)  # v4 typed entity flags
            sync()  # ReadRenderEntity
            model = string()
            skip(4+4+24+20+12+36)  # entity/body, bounds, five view flags, origin, axis
            for _ in range(4):
                string()  # custom/reference/overlay shader and skin
            skip(4+12*4)  # sound handle and MAX_ENTITY_SHADER_PARMS
            for _ in range(3):
                sync()
                assert string() == ''  # only empty GUI slots
            skip(4+4+4+4+4)  # depth float, four bools, depth-view int, LOD float/int
            handle_offset = position
            handle = integer()
            assert handle >= -1
            sound_sync = position
            sync()  # immediately following ReadRefSound
            sound_handle = integer()
            assert -1 <= sound_handle < 1048576
            if bindings or movers or physics:
                skip(12+12+4)  # reference sound origin, velocity and listener
                string()  # sound shader
                skip(4)  # diversity
                boolean()  # waitfortrigger
                skip(16+8)  # distance/volume/shakes and flags/class
                skip(4)  # forwardDamageEnt spawn ID
                sync()  # ReadStaticObject(defaultPhysicsObj)
                empty_frames = 0
                for _ in range(2):  # idClass and optional idPhysics empty frame
                    if data[position+8:position+12] != b'OQ4Y':
                        break
                    sync()
                    sync()
                    empty_frames += 1
                assert empty_frames in (1, 2)
                sync()  # idPhysics_Static class start
                physics_owner_offset = position
                self_reference = integer()
                assert 0 < self_reference <= metadata['objects']
                skip(12+36+12+36)  # origin/axis and local origin/axis
                if boolean():  # ReadClipModel
                    boolean()  # enabled
                    skip(4+4+4+12+36+24+24+4)  # entity/id/owner, transform, bounds and contents
                    string()  # collision model
                    skip(4+4)  # trace model and render model handles
                    boolean()  # linked
                    skip(4)  # touch count
                    boolean()  # checked
                boolean()  # hasMaster
                boolean()  # isOrientated
                physics_end_offset = position
                sync()  # idPhysics_Static class end
                binding_offset = position
                original_links = [integer() for _ in range(5)]
                if not movers:
                    assert original_links[0] == original_links[3] == original_links[4] == 0
                pvs_offset = position
                pvs_count = integer()
                assert -1 <= pvs_count <= 4
                skip(4*4)  # MAX_PVS_AREAS
                assert not boolean()  # only records without signal payloads
                skip(4+4+4)  # mpGUIState, instance and clipWorld
                sync()  # idEntity class end proves the complete record boundary
                if physics:
                    # This late object survives until MapShutdown has deleted
                    # the earlier saved owner. Interrupt before RestorePhysics
                    # can reattach it, while retaining the outer integrity CRC.
                    if number < 32:
                        continue
                    registration_cases, _ = damaged_fixtures(data)
                    other_reference = registration_cases['DuplicateEntity'][1]['replacement']
                    assert self_reference != other_reference
                    fixtures = {}
                    for label, owner in (('ForeignPhysicsOwner', other_reference), ('NullPhysicsOwner', 0)):
                        changed = bytearray(data)
                        struct.pack_into('<i', changed, physics_owner_offset, owner)
                        struct.pack_into('<i', changed, physics_end_offset, 0)
                        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
                        fixtures[label] = bytes(changed), dict(entity=name, entity_number=number,
                            object_reference=self_reference, owner_offset=physics_owner_offset,
                            replacement_owner=owner, interruption_offset=physics_end_offset,
                            expected='ReadSyncId: marker mismatch while reading Callrestore_r end  for idPhysics_Static')
                    return fixtures, metadata
                if movers:
                    sync()  # idMover_Binary class start
                    skip(12+12)  # endpoints
                    assert 0 <= integer() <= 3  # moverState
                    master_offset = position
                    saved_master, saved_chain = integer(), integer()
                    if saved_master != self_reference or saved_chain != 0:
                        continue  # choose two independent activation roots
                    skip(5*4+2*4+3*4+4+4)  # sounds, wait/damage, durations, activator and start time
                    string()  # team name
                    boolean()  # enabled
                    boolean()  # deferred open
                    skip(4+4)  # move thread and status
                    buddy_offset = position
                    buddy_count = integer()
                    assert 0 <= buddy_count <= 4096
                    for _ in range(buddy_count):
                        string()
                    sync()  # ReadStaticObject(physicsObj) bounds the mover prefix
                    mover_records.append(dict(entity=name, entity_number=number, entity_class=args['classname'],
                                              object_reference=self_reference, master_offset=master_offset,
                                              chain_offset=master_offset+4, buddy_offset=buddy_offset,
                                              original_buddy_count=buddy_count))
                    if len(mover_records) < 2:
                        continue
                    earlier, later = sorted(mover_records, key=lambda item: item['entity_number'])
                    definitions = [
                        ('SelfActivationChain', {earlier['chain_offset']: earlier['object_reference']}, 'cyclic activation chain'),
                        ('PartialActivation', {later['master_offset']: earlier['object_reference'], later['buddy_offset']: -1}, 'invalid buddy count -1'),
                        ('MissingActivationRoot', {earlier['master_offset']: 0}, 'invalid activation root'),
                        ('DetachedActivationMember', {later['master_offset']: earlier['object_reference']}, 'is absent from its activation team'),
                        ('CyclicMoveMaster', {earlier['master_offset']: later['object_reference'], later['master_offset']: earlier['object_reference']}, 'cyclic move master'),
                    ]
                    fixtures = {}
                    for fixture_name, edits, expected in definitions:
                        changed = bytearray(data)
                        for offset, value in edits.items():
                            struct.pack_into('<i', changed, offset, value)
                        struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
                        fixtures[fixture_name] = bytes(changed), dict(movers=[earlier, later], edits=edits, expected=expected)
                    return fixtures, metadata
                # Reuse the bounded registration parser to select another real
                # entity reference, instead of assuming an object-table index.
                registration_cases, _ = damaged_fixtures(data)
                other_reference = registration_cases['DuplicateEntity'][1]['replacement']
                assert self_reference != other_reference
                definitions = [
                    ('PartialTeam', {binding_offset+12: self_reference, pvs_offset: 5}, 'invalid PVS area count 5'),
                    ('SelfBind', {binding_offset: self_reference}, 'cyclic bind master'),
                    ('SelfChain', {binding_offset+16: self_reference}, 'cyclic team chain'),
                    ('MissingTeam', {binding_offset+16: other_reference}, 'missing team master'),
                    ('MissingTail', {binding_offset+12: self_reference}, 'invalid team root'),
                ]
                fixtures = {}
                for fixture_name, edits, expected in definitions:
                    changed = bytearray(data)
                    for offset, value in edits.items():
                        struct.pack_into('<i', changed, offset, value)
                    struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
                    detail = dict(entity=name, entity_number=number, entity_class=args['classname'],
                                  object_reference=self_reference, class_offset=candidate.start(),
                                  binding_offset=binding_offset, original_links=original_links,
                                  pvs_offset=pvs_offset, original_pvs_count=pvs_count,
                                  edits=edits, expected=expected)
                    fixtures[fixture_name] = bytes(changed), detail
                return fixtures, metadata
            changed = bytearray(data)
            struct.pack_into('<i', changed, handle_offset, 2147483647)
            struct.pack_into('<I', changed, sound_sync, 0)
            struct.pack_into('<I', changed, len(changed)-4, zlib.crc32(changed[:-16]))
            detail = dict(entity=name, entity_number=number, entity_class=args['classname'], model=model,
                          class_offset=candidate.start(), handle_offset=handle_offset, original_handle=handle,
                          replacement_handle=2147483647, broken_sound_sync=sound_sync,
                          expected='marker mismatch while reading ReadRefSound')
            return {'ForeignModelHandle': (bytes(changed), detail)}, metadata
        except (AssertionError, UnicodeError, struct.error):
            continue
    raise AssertionError('No fully decoded v4 entity with empty GUI slots was found.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-save', type=Path, required=True)
    parser.add_argument('--basepath', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ui', choices=('retained', 'classic'), default='retained')
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--cases', choices=('registrations', 'render-handles', 'bindings', 'spawn-refs', 'movers', 'factories', 'threads', 'classes', 'guis', 'physics', 'animation', 'af', 'events', 'legacy-events'), default='registrations')
    parser.add_argument('--unavailable-pause-menu', action='store_true',
                        help='Also exercise optional retained-menu fallback during valid save loading.')
    parser.add_argument('--timeout', type=int, default=240)
    args = parser.parse_args()
    if args.unavailable_pause_menu and args.ui != 'retained':
        parser.error('The optional retained-menu fallback probe requires --ui retained.')
    if os.name != 'nt':
        parser.error('Runtime probe targets staged Windows x64; native recovery tests also run on Linux.')
    output, source, runtime = args.output.resolve(), args.source_save.resolve(), ROOT/'.install'
    if (ROOT/'.tmp').resolve() not in output.parents:
        parser.error('Output must be a new directory below project .tmp.')
    for relative in ('openQ4-client_x64.exe', 'baseoq4/game-sp_x64.dll', 'baseoq4/game-mp_x64.dll'):
        if sha(ROOT/'builddir'/relative) != sha(runtime/relative):
            parser.error('Stage the current build before running this probe: '+relative)
    original = source.read_bytes()
    fixtures, metadata = (af_fixtures(original) if args.cases == 'af'
                         else animation_fixtures(original) if args.cases == 'animation'
                         else gui_fixtures(original) if args.cases == 'guis'
                         else object_class_fixtures(original) if args.cases == 'classes'
                         else factory_fixtures(original) if args.cases == 'factories'
                         else render_handle_fixture(original, bindings=args.cases == 'bindings', movers=args.cases == 'movers',
                                                    physics=args.cases == 'physics')
                         if args.cases in ('render-handles', 'bindings', 'movers', 'physics')
                         else damaged_fixtures(original, spawn_refs=args.cases == 'spawn-refs', threads=args.cases == 'threads',
                                               events=args.cases == 'events', legacy_events=args.cases == 'legacy-events'))
    output.mkdir(parents=True, exist_ok=False)
    save = output/'save'
    # A separate test mod gives the local fault fixture precedence over staged
    # baseoq4 packages without modifying that shared runtime or retail assets.
    game_name = 'restore-gui-probe' if args.unavailable_pause_menu else 'baseoq4'
    game = save/game_name
    (game/'savegames').mkdir(parents=True)
    if args.unavailable_pause_menu:
        manifest = json.loads((runtime/'baseoq4/mod.json').read_text())
        manifest['name'] = 'Save GUI fallback probe'
        (game/'mod.json').write_text(json.dumps(manifest, indent=2)+'\n')
        menu = game/'guis/menu/pause.q4ui'
        menu.parent.mkdir(parents=True)
        menu.write_text('{ invalid document for optional-menu fallback probe\n')
    saves = [game/'savegames/Good.save']
    saves[0].write_bytes(original)
    for name, (data, _) in fixtures.items():
        path = game/'savegames'/f'{name}.save'
        path.write_bytes(data)
        saves.append(path)
    markers = ['RESTORE_GOOD_INITIAL']
    script = ['echo RESTORE_GOOD_INITIAL', 'screenshot screenshots/initial.tga',
              'set g_autoExecAfterMapLoad ""']
    for name in fixtures:
        marker = 'RESTORE_REJECTED_'+name
        markers.append(marker)
        script += ['loadGame '+name, 'waitMsec 2000', 'echo '+marker,
                   'openq4_retainedGui report', f'screenshot screenshots/{name}.tga']
    script += ['set g_autoExecAfterMapLoad final.cfg', 'loadGame Good']
    (game/'initial.cfg').write_text('\n'.join(script)+'\n')
    markers.append('RESTORE_GOOD_FINAL')
    (game/'final.cfg').write_text('echo RESTORE_GOOD_FINAL\nscreenshot screenshots/final.tga\nquit\n')
    settings = dict(fs_game=game_name, fs_basepath=str(args.basepath.resolve()), fs_savepath=str(save), fs_devpath=str(save),
        com_gameMode='SP', si_gameType='singleplayer', ui_retained='1' if args.ui == 'retained' else '0', ui_autoJoin='0',
        win_allowMultipleInstances='1', sys_consoleWindow='0', in_tty='0', in_mouse='0', in_joystick='0', s_noSound='1',
        r_fullscreen='0', r_fullscreenDesktop='0', r_borderless='0', r_borderlessDefaultMigrated='1', r_hiddenWindow='1',
        r_mode='-1', r_customWidth='1280', r_customHeight='720', r_windowWidth='1280', r_windowHeight='720',
        r_renderApi=args.renderer, r_swapInterval='0', com_maxfps='60', com_skipLoadingContinue='1', com_loadingContinueAutoAdvance='1',
        g_autoSkipCinematics='1', g_autoExecAfterMapLoad='initial.cfg', g_autoExecAfterMapLoadDelayMs='750',
        logFile='2', logFileName='logs/openq4.log', developer='1')
    executable = runtime/'openQ4-client_x64.exe'
    command = [str(executable), *[part for key, value in settings.items() for part in ('+set', key, value)], '+loadGame', 'Good']
    report = dict(source_save=dict(path=str(source), sha256=sha(source)), fixture=metadata,
        cases={name: details for name, (_, details) in fixtures.items()}, command=command,
        harness_sha256=sha(Path(__file__)), ui=args.ui, renderer=args.renderer, fixture_set=args.cases,
        unavailable_pause_menu=args.unavailable_pause_menu, game_directory=game_name,
        runtime={relative: sha(runtime/relative) for relative in
                 ('openQ4-client_x64.exe', 'baseoq4/game-sp_x64.dll', 'baseoq4/game-mp_x64.dll', 'baseoq4/pak0.pk4', 'baseoq4/pak1.pk4')},
        before={path.name: sha(path) for path in saves},
        scope='Current v5 semantic restore rejection, real recovery UI, good reload. Hidden window, input disabled, engine screenshots.')
    (output/'plan.json').write_text(json.dumps(report, indent=2)+'\n')
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    begin = time.monotonic()
    with (output/'stdout.log').open('w') as stream:
        process = subprocess.Popen(command, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, startupinfo=startup)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            code = process.wait(timeout=15)
            report['timed_out'] = True
    report.update(exit_code=code, elapsed=time.monotonic()-begin, after={path.name: sha(path) for path in saves})
    log = game/'logs/openq4.log'
    text = log.read_text(errors='replace') if log.is_file() else ''
    report['markers'] = {marker: text.count(marker) for marker in markers}
    report['restore_attempts'] = text.count('Game Map Init SaveGame')
    report['expected_errors'] = {name: details['expected'] in text for name, (_, details) in fixtures.items()}
    report['optional_menu_fallback'] = (
        "retained UI: 'guis/menu/pause.q4ui' could not be loaded; the stock screen stays in use" in text)
    report['screenshots'] = {name: sha(game/f'screenshots/{name}.tga')
        for name in ['initial', *fixtures, 'final'] if (game/f'screenshots/{name}.tga').is_file()}
    report['engine_log_sha256'] = sha(log) if log.is_file() else None
    report['recovery_ui'] = dict(retained_pages=text.count('RETAINED_GUI path=guis/menu/loadgame.q4ui'),
                                classic_dialogs=text.count('MessageBox: LOAD FAILED -'))
    ui_passed = (report['recovery_ui']['retained_pages'] == len(fixtures) and report['recovery_ui']['classic_dialogs'] == 0
                 if args.ui == 'retained' else report['recovery_ui']['classic_dialogs'] == len(fixtures))
    positions = [text.find(marker) for marker in markers]
    report['passed'] = (code == 0 and not report.get('timed_out') and all(report['markers'][m] == 1 for m in markers)
        and positions == sorted(positions) and report['restore_attempts'] == len(fixtures)+2
        and all(report['expected_errors'].values()) and report['before'] == report['after']
        and (not args.unavailable_pause_menu or report['optional_menu_fallback'])
        and len(report['screenshots']) == len(fixtures)+2 and 'ERROR:' not in text and ui_passed)
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ('passed', 'exit_code', 'elapsed', 'markers', 'expected_errors', 'recovery_ui')}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

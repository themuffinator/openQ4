#!/usr/bin/env python3
"""Regression checks for per-channel speech evidence and campaign progression plans."""
import unittest
from unittest.mock import patch

import audio_scene_audit as audit


def channel(source=7, dialogue=1, sample='sound/shared.wav', shader='spoken', transcript='text'):
    return (f'SOUND_DIALOGUE emitter=2 channel=1 shader={shader} sample={sample} '
            f'dialogue={dialogue} transcript={transcript}\n'
            f'SOUND_DISTANCE_BACKEND available=1 source={source} playing=1 gain=0.4 listenerGain=1\n')


def audible(source=7, sample='sound/shared.wav'):
    return f'  0 {source} 0.400 1.000 {sample}\n'


class SceneAuditTest(unittest.TestCase):
    @staticmethod
    def timing(total=1234, sound=41, level='game/test'):
        return (f'  {total} msec to load {level}\n'
                f'Map load phases: renderWorld=20 gameInit=900 playerSpawn=1 cacheJoin=0 mediaFinish=50 settle=100 total={total} msec\n'
                f'Map media phases: render=5 sound={sound} decl=1 bse=1 loadSize=1 ui=1 total=50 msec\n')

    def test_loading_requires_complete_consistent_phase_evidence(self):
        good = self.timing()
        self.assertEqual(audit.load_timing(good)['sound_finalize_ms'], 41)
        for invalid in ('', good.split('Map media')[0], good + good,
                        good.replace('total=1234', 'total=1235'), good.replace('total=50', 'total=51'),
                        good.replace('cacheJoin=0 ', ''), good.replace('bse=1 ', '')):
            self.assertEqual(audit.load_timing(invalid), {})

    def test_load_times_stay_with_their_map_and_exclude_gameplay_reloads(self):
        log = (self.timing() + 'SCENE_BEGIN game/test\n' + self.timing(9999) + 'SCENE_END game/test\n' +
               self.timing(500, level='game/next') + 'SCENE_BEGIN game/next\nSCENE_END game/next\n')
        result = audit.audit(log, ['game/test', 'game/next', 'game/missing'])
        self.assertEqual(result['game/test']['loading']['total_ms'], 1234)
        self.assertEqual(result['game/next']['loading']['total_ms'], 500)
        self.assertEqual(result['game/missing']['loading'], {})

    def test_load_cost_preserves_noise_and_requires_matching_maps(self):
        loud = audit.load_timing(self.timing(100))
        quiet = audit.load_timing(self.timing(200))
        self.assertEqual(audit.load_cost(loud, quiet)['audio_load_ms'], -100)
        self.assertEqual(audit.load_cost(loud, {}), {})
        quiet['map'] = 'game/different'
        self.assertEqual(audit.load_cost(loud, quiet), {})

    def test_shared_sample_is_classified_per_current_source(self):
        state = audit.probe_state(audible(7) + audible(8) + channel(7) +
                                  channel(8, dialogue=0, shader='effect', transcript='undeclared') +
                                  'SOUND_DIALOGUE_END channels=2\n')
        self.assertTrue(state['routing_complete'])
        self.assertEqual(state['dialogue'], ['sound/shared'])
        self.assertEqual(state['effects'], ['sound/shared'])

    def test_reused_source_uses_current_routing(self):
        loud = audit.probe_state(audible() + channel() + 'SOUND_DIALOGUE_END channels=1\n')
        later = audit.probe_state(audible() + channel(dialogue=0) + 'SOUND_DIALOGUE_END channels=1\n')
        self.assertEqual(loud['dialogue'], ['sound/shared'])
        self.assertEqual(later['dialogue'], [])
        self.assertTrue(later['routing_complete'])

    def test_missing_partial_and_mismatched_diagnostics_fail_closed(self):
        for body in (audible(), audible() + channel(),
                     audible(8) + channel() + 'SOUND_DIALOGUE_END channels=1\n',
                     audible() + channel(sample='sound/another.wav') + 'SOUND_DIALOGUE_END channels=1\n',
                     channel() + 'SOUND_DIALOGUE_END channels=2\n'):
            self.assertFalse(audit.probe_state(body)['routing_complete'])
        self.assertTrue(audit.probe_state('SOUND_DIALOGUE_END channels=0\n')['routing_complete'])

    def test_muted_dialogue_leak_is_still_rejected(self):
        log = ('SCENE_BEGIN game/test\nSCENE_PROBE game/test 0 full\n' + audible() + channel() +
               'SOUND_DIALOGUE_END channels=1\nSCENE_PROBE game/test 0 mute\n' + audible() + channel() +
               'SOUND_DIALOGUE_END channels=1\nSCENE_END game/test\n')
        scene = audit.audit(log, ['game/test'])['game/test']
        self.assertEqual(scene['mute_leaks'], ['sound/shared'])
        self.assertTrue(scene['voice_routing_recorded'])

    def test_probe_does_not_consume_next_step_or_memory_rows(self):
        log = ('SCENE_BEGIN game/test\nSCENE_PROBE game/test 0 full\nSOUND_DIALOGUE_END channels=0\n'
               'SCENE_PROBE game/test 0 mute\nSOUND_DIALOGUE_END channels=0\n'
               'SCENE_STEP game/test 0 trigger\n' + audible() + channel() +
               'SOUND_DIALOGUE_END channels=1\nSCENE_END game/test\n')
        scene = audit.audit(log, ['game/test'])['game/test']
        self.assertEqual(scene['mute_leaks'], [])
        self.assertTrue(scene['voice_routing_recorded'])

    def test_invalid_origins_are_not_route_points(self):
        for origin in ('', '1', '1 2', '1 2 3 4', 'nan 2 3', 'inf 2 3', 'a 2 3'):
            self.assertIsNone(audit.origin_of({'origin': origin}))
        self.assertEqual(audit.origin_of({'origin': '0 -2 3'}), (0, -2, 3))

    def test_stream_evidence_survives_an_authored_stop_before_the_final_snapshot(self):
        log = ('SCENE_BEGIN game/test\nSCENE_PROBE game/test 0 full\n'
               'SOUND_VOICE source=7 state=playing routed=0 queued=1 serial=1 offsetMs=200.0 sample=sound/music/test.ogg\n'
               'SCENE_MEMORY game/test\nSOUND_VOICES_END voices=0\nSCENE_END game/test\n')
        scene = audit.audit(log, ['game/test'])['game/test']
        self.assertEqual(scene['streams_observed_playing'], ['sound/music/test'])
        self.assertEqual(audit.audit(log.replace('state=playing', 'state=stopped'), ['game/test'])['game/test']['streams_observed_playing'], [])

    def test_route_is_nearest_first_and_deterministic(self):
        entities = [{'origin': '9 0 0'}, {'origin': '1 0 0'}, {'origin': '2 0 0'}]
        self.assertEqual(audit.nearest_route((0, 0, 0), entities), [entities[1], entities[2], entities[0]])

    def test_nonplayer_and_activation_only_triggers_are_skipped(self):
        for extra in ({'classname': 'trigger_count'}, {'notouch': '1'}, {'noclient': '1'},
                      {'onlyvehicle': '1'}, {'flashlight_trigger': '1'},
                      {'classname': 'trigger_once_entityname', 'entityname': 'strogg'}):
            self.assertFalse(audit.player_touchable({'classname': 'trigger_once', **extra}))
        self.assertTrue(audit.player_touchable({'classname': 'trigger_once'}))
        self.assertTrue(audit.player_touchable({'classname': 'trigger_once_entityname', 'entityname': 'player1'}))

    def test_exit_chains_are_excluded_and_end_targets_suppressed(self):
        def entity(index, **fields):
            return f'// entity {index}\n{{\n' + ''.join(f'"{k}" "{v}"\n' for k, v in fields.items()) + '}\n'
        text = (entity(0, classname='info_player_start', name='start', origin='0 0 0') +
                entity(1, classname='trigger_once', name='safe', origin='1 0 0') +
                entity(2, classname='trigger_once', name='exit_trigger', origin='2 0 0', target='relay') +
                entity(3, classname='trigger_relay', name='relay', target='end') +
                entity(4, classname='target_endlevel', name='end') +
                entity(5, classname='item_objectivefailed', name='late_gate'))
        exits = {}
        with patch.object(audit, 'content_files', side_effect=[{}, {'maps/game/test.map': text}]):
            steps = audit.scene_steps([], ['game/test'], progress=True, blocked_exits=exits)
        self.assertEqual(steps, {'game/test': [('trigger_once', 'safe')]})
        # A failed timed objective ends the level as well; it is removed, not routed around.
        self.assertEqual(exits, {'game/test': ['end', 'late_gate']})
        cfg = audit.scene_cfgs([('game/test', 'game/test')], 3, 1000, steps,
                               move='teleport', blocked_exits=exits)[0]
        self.assertLess(cfg.index('remove "end"'), cfg.index('teleport "safe"'))
        self.assertIn('s_dialogueVoices\n', cfg)
        self.assertIn('set g_autoExecAfterMapLoad ""', cfg)

    def test_entity_commands_preserve_leading_digit_names(self):
        cfg = audit.scene_cfgs([('game/test', 'game/test')], 3, 1000,
                               {'game/test': [('trigger_once', '2door')]},
                               move='teleport', blocked_exits={'game/test': ['3end']})[0]
        self.assertIn('echo SCENE_STEP game/test 0 "2door"\nteleport "2door"\n', cfg)
        self.assertIn('remove "3end"\n', cfg)

    def test_fight_kills_halfway_through_each_step_and_before_the_tail(self):
        steps = {'game/test': [('trigger_once', 'a'), ('trigger_once', 'b')]}
        cfg = audit.scene_cfgs([('game/test', 'game/test')], 3, 1000, steps, step_msec=4001, tail=2,
                               move='teleport', fight=True)[0]
        self.assertEqual(cfg.count('openq4_killHostiles\n'), 3)
        self.assertIn('teleport "a"\nwaitMsec 2000\nopenq4_killHostiles\nwaitMsec 2001\n', cfg)
        last_step = cfg.index('teleport "b"')
        self.assertLess(cfg.index('openq4_killHostiles\n', cfg.index('openq4_killHostiles\n', last_step) + 1),
                        cfg.index('SCENE_MEMORY'))
        quiet = audit.scene_cfgs([('game/test', 'game/test')], 3, 1000, steps, move='teleport')[0]
        self.assertNotIn('openq4_killHostiles', quiet)

    def test_markers_after_unterminated_script_output_still_split_probes(self):
        # process2 prints "colour swap left" without a newline before a probe marker.
        log = ('SCENE_BEGIN game/test\nSCENE_PROBE game/test 0 full\n' + audible() + channel() +
               'SOUND_DIALOGUE_END channels=1\ncolour swap leftSCENE_PROBE game/test 0 mute\n' +
               'SOUND_DIALOGUE_END channels=0\nSCENE_END game/test\n')
        scene = audit.audit(log, ['game/test'])['game/test']
        self.assertEqual(scene['probes'], 1)
        self.assertTrue(scene['voice_routing_recorded'])
        self.assertEqual(scene['mute_leaks'], [])

    def test_map_ending_inside_its_scene_is_a_failure(self):
        shutdown = '------------ Game Map Shutdown --------------\n'
        log = ('SCENE_BEGIN game/test\nSCENE_STEP game/test 0 "a"\nSCENE_STEP game/test 1 "b"\n' + shutdown +
               'SCENE_STEP game/test 2 "c"\nSCENE_END game/test\n' + shutdown + 'SCENE_BEGIN game/next\nSCENE_END game/next\n')
        result = audit.audit(log, ['game/test', 'game/next'])
        self.assertEqual(result['game/test']['unexpected_level_changes'], ['shutdown after step 1'])
        # The shutdown between maps belongs to the planned level change.
        self.assertEqual(result['game/next']['unexpected_level_changes'], [])

    def test_player_death_is_reported_at_its_step(self):
        log = ('SCENE_BEGIN game/test\nSCENE_STEP game/test 0 "a"\nSCENE_STEP game/test 1 "b"\n'
               'You must be alive to use this command.\nSCENE_STEP game/test 2 "c"\n'
               'You must be alive to use this command.\nSCENE_END game/test\n')
        scene = audit.audit(log, ['game/test'])['game/test']
        self.assertEqual((scene['player_dead_at_step'], scene['player_dead_commands']), (1, 2))
        alive = audit.audit(log.replace('You must be alive to use this command.\n', ''), ['game/test'])
        self.assertIsNone(alive['game/test']['player_dead_at_step'])

    def test_fight_results_are_counted_per_scene(self):
        log = ('SCENE_BEGIN game/test\nSCENE_STEP game/test 0 "a"\n'
               'KILL_HOSTILE name=grunt_1 class=monster_grunt result=killed\n'
               'KILL_HOSTILE name=boss class=monster_makron result=survived\n'
               'KILL_HOSTILE name=intro class=monster_gladiator result=protected\n'
               'KILL_HOSTILES killed=1 survived=1 protected=1\n'
               'KILL_HOSTILES killed=2 survived=0 protected=0\n'
               'SCENE_END game/test\n')
        scene = audit.audit(log, ['game/test'])['game/test']
        self.assertEqual((scene['fight_passes'], scene['hostiles_killed'], scene['hostiles_survived'],
                          scene['hostiles_protected']), (2, 3, 1, 1))
        self.assertEqual(scene['hostile_survivor_classes'],
                         {'monster_gladiator protected': 1, 'monster_makron survived': 1})


if __name__ == '__main__':
    unittest.main()

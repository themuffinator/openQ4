#!/usr/bin/env python3
"""Negative and positive controls for the MP-02 impulse-death exclusion policy.

Runs the classifier and replay decision in tools/tests/mp_bot_impulse_replay.py
on synthetic match logs and replay outcomes, with no game assets. A death is
excluded only for a logged enemy impulse followed by a replay in which every
decisive trial armed and fell; every other shape - recoverable, missing or
incomplete replays, no impulse, self/team/world impulses, stale impulses,
unknown teams - must remain a navigation death; later pushes are reported.
"""
from __future__ import annotations

import sys
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mp_bot_impulse_replay as policy  # noqa: E402

ROSTER = """  cl  name             character        style        skill
   1  Cortez           Cortez           sniper       4.0
   2  Rhodes           Rhodes           ambusher     4.0
   3  Sledge           Sledge           rusher       4.0
"""


def state(time: int, teams: dict[int, int]) -> str:
    lines = [f"MP_STATE time={time} state=GAMEON"]
    lines += [f"MP_PLAYER slot={slot} bot=1 team={team} health=100 spectating=0" for slot, team in teams.items()]
    return "\n".join(lines) + "\n"


def impulse(client: int, time: int, attacker: int, origin=(800.0, 300.0, 544.25), velocity=(170.0, -533.0, -83.0)) -> str:
    return (f"BOT_IMPULSE client={client} time={time} attacker={attacker} damage=20 "
            f"origin={origin[0]} {origin[1]} {origin[2]} kick=1 2 3 "
            f"velocity={velocity[0]} {velocity[1]} {velocity[2]} def=damage_rocketSplash health=80 knock=50 armor=20 armorSave=10 preKnock=0\n")


def death(name: str, time: int) -> str:
    return f"{name} was killed by world\nbot {name}: hit by world for 1000 at 570 -329 -2529 velocity 23 -32 58 direction 0 0 0 (time {time})\n"


def classify(log: str, gametype: str = "CTF") -> list[dict]:
    return policy.classify(ROSTER + log, {"Cortez", "Rhodes", "Sledge"}, gametype)


def outcomes(died: bool = True, survivor: str | None = None, unarmed: str | None = None,
             missing: str | None = None, airborne: str | None = None,
             before: str | None = "preNone") -> list[dict]:
    """Replay outcomes; `before` names the counterfactual trial that lands (None: none do)."""
    rows = []
    for trial in policy.trials_for({"origin": [0, 0, 0], "velocity": [0, 0, 0], "kick": [0, 0, 0]}):
        if trial["name"] == missing:
            continue
        armed = trial["name"] != unarmed
        survived = trial["name"] in (survivor, before if trial.get("counterfactual") else None)
        fell = died and not survived and trial["name"] != airborne
        rows.append({**trial, "armed": armed, "died": fell if armed else None, "grounded": survived,
                     "survived": survived and armed})
    return rows


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"mp_bot_death_policy: FAILED: {message}")


def main() -> None:
    teams = {1: 0, 2: 1, 3: 0}
    pellet = ("BOT_IMPULSE client=1 time=2000 attacker=2 damage=10 origin=1 2 3 "
              "kick=50 0 0 velocity={velocity} 0 0 def=damage_pellet health={health} knock=50 armor={armor} armorSave=5 preKnock={pre}\n")
    shot = pellet.format(velocity=100, health=90, armor=45, pre=0) + pellet.format(velocity=150, health=80, armor=40, pre=50)
    [burst] = classify(state(1000, teams) + shot + death("Cortez", 4500))
    check(burst["impulse"]["damage"] == 20 and burst["impulse"]["kick"] == [100, 0, 0] and
          len(burst["impulse"]["pellets"]) == 2 and not burst["earlier_candidates"],
          "one accounted shotgun burst is one cause, retaining both pellets")
    before = [t for t in policy.trials_for(burst["impulse"]) if t.get("counterfactual")]
    check("50.000 0.000 0.000" in before[0]["command"] and before[0]["setup"] == ["bot_probeHealth 1 100 0 0 0 0 50"],
          "counterfactual removes the entire shotgun kick, health and armour damage")
    for changed in (shot.replace("velocity=150", "velocity=151"),
                    shot.replace("health=80", "health=79"),
                    shot.replace("armor=40", "armor=39"),
                    shot.replace(" armorSave=5", ""),
                    shot.replace("preKnock=50", "preKnock=49"),
                    shot.replace("velocity=150 0 0 def=damage_pellet", "velocity=150 0 0 def=damage_railgun"),
                    shot.replace("origin=1 2 3 kick=50 0 0 velocity=150", "origin=2 2 3 kick=50 0 0 velocity=150")):
        [separate] = classify(state(1000, teams) + changed + death("Cortez", 4500))
        check(len(separate["earlier_candidates"]) == 1 and "pellets" not in separate["impulse"],
              "unaccounted or different shots must remain separate causes")
    [chain] = classify(state(1000, teams) + impulse(1, 2000, 2) + impulse(1, 2600, 2) + death("Cortez", 4500))
    check(chain["earlier_candidates"][0]["impulse"]["time"] == 2000, "retain earlier enemy causes")
    for first, second, expected, count in [
        (outcomes(before=None), outcomes(), "excluded-impulse", 2),
        (outcomes(before=None), outcomes(survivor="air11"), "navigation", 2),
        (outcomes(survivor="controller"), outcomes(), "navigation", 1),
        (outcomes(before=None, unarmed="preBrake"), outcomes(), "navigation", 1),
        (outcomes(before=None, missing="air03"), outcomes(), "navigation", 1),
        (outcomes(before=None, airborne="controller"), outcomes(), "navigation", 1),
    ]:
        result = policy.replay_chain(chain, lambda c, i: policy.decide(c, [first, second][i], True))
        check(result["status"] == expected and len(result["causal_replays"]) == count,
              "earlier-cause search requires complete already-doomed trials and unchanged exclusion proof")
    [taps] = classify(state(1000, teams) + impulse(1, 2000, 2) +
                      "".join(impulse(1, time, 2) for time in range(2100, 3200, 100)) + death("Cortez", 4500))
    taps["earlier_candidates"][-1]["impulse"]["kick"] = [500, 0, 0]
    visited = []
    def strongest(choice, attempt):
        visited.append(choice["impulse"]["time"])
        return policy.decide(choice, outcomes() if choice["impulse"]["time"] == 2000 else outcomes(before=None), True)
    check(policy.replay_chain(taps, strongest)["status"] == "excluded-impulse" and visited == [3100, 2000],
          "only after a complete doomed replay, small taps cannot exhaust the bounded search before a strong cause")
    # Equal machine-gun kicks logged to three decimals differ in squared
    # magnitude by roundoff. Do not skip the newer hit for the largest error.
    equal = dict(chain)
    equal["earlier_candidates"] = [
        {"impulse": {**chain["impulse"], "time": 2200, "kick": [7.835, 34.045, -2.137]}},
        {"impulse": {**chain["impulse"], "time": 2100, "kick": [17.299, 30.311, -2.648]}},
    ]
    visited = []
    def same_strength(choice, attempt):
        visited.append(choice["impulse"]["time"])
        return policy.decide(choice, outcomes(survivor="controller") if attempt else outcomes(before=None), True)
    check(policy.replay_chain(equal, same_strength)["status"] == "navigation" and visited == [2600, 2200],
          "rounded equal-strength kicks use recency and still stop at a survivor")
    # Positive control: enemy impulse, every decisive trial armed and fell.
    [candidate] = classify(state(1000, teams) + impulse(1, 2000, 2) + death("Cortez", 4500))
    check(candidate["status"] == "impulse-candidate" and candidate["impulse"]["attacker"] == 2,
          "an enemy impulse inside the window makes a replay candidate")
    check(policy.decide(candidate, outcomes(), True)["status"] == "excluded-impulse",
          "an unrecoverable replay excludes the death when the moment before the impulse landed")
    # Counterfactual negative controls: a bot already doomed before the
    # impulse - a machine gun tapping a falling bot - is a navigation death.
    check(policy.decide(candidate, outcomes(before=None), True)["status"] == "navigation",
          "a fall already unrecoverable without the impulse stays a navigation death")
    check(policy.decide(candidate, outcomes(before="preBrake", unarmed="preBrake"), True)["status"] == "navigation",
          "an unarmed counterfactual trial cannot show the impulse made the difference")
    check(policy.decide(candidate, [o for o in outcomes() if not o.get("counterfactual")], True)["status"] ==
          "navigation", "no counterfactual trials fail closed")
    pre = [t for t in policy.trials_for({"origin": [1, 2, 3], "velocity": [300, 0, 100], "kick": [250, 0, 50],
                                         "health": 40, "damage": 20, "knock_msec": 36, "pre_knock_msec": 36,
                                         "armor": 17, "armor_save": 8}) if t.get("counterfactual")]
    check(len(pre) == 3 and "50.000 0.000 50.000" in pre[0]["command"] and
          pre[0]["setup"] == ["bot_probeHealth 1 60 36 0 0 0 25"],
          "the counterfactual removes this hit's damage and kick while retaining an earlier knock timer")
    # Negative controls: the same impulse with a recoverable replay still fails.
    check(policy.decide(candidate, outcomes(survivor="air05"), True)["status"] == "navigation",
          "a surviving fixed-input trial keeps the death a navigation failure")
    check(policy.decide(candidate, outcomes(survivor="controller"), True)["status"] == "navigation",
          "a surviving controller trial keeps the death a navigation failure")
    for variant in (outcomes(missing="controller"), outcomes(unarmed="controller"), outcomes(airborne="controller")):
        check(policy.decide(candidate, variant, True)["status"] == "navigation",
              "an incomplete controller trial cannot qualify an impulse exclusion")
    check(policy.decide(candidate, outcomes(survivor="none"), True)["status"] == "navigation",
          "surviving with no input keeps the death a navigation failure")
    check(policy.decide(candidate, outcomes(unarmed="air11"), True)["status"] == "navigation",
          "an unarmed trial fails closed")
    check(policy.decide(candidate, outcomes(missing="air03"), True)["status"] == "navigation",
          "a missing trial fails closed")
    check(policy.decide(candidate, outcomes(airborne="air07"), True)["status"] == "navigation",
          "a trial that neither landed nor died fails closed")
    check(policy.decide(candidate, outcomes(), False)["status"] == "navigation",
          "an incomplete replay session fails closed")
    check(policy.decide(candidate, None, True)["status"] == "navigation", "no replay fails closed")
    # Deaths with no qualifying enemy impulse never reach a replay.
    shapes = {
        "no impulse": state(1000, teams) + death("Cortez", 4500),
        "self impulse": state(1000, teams) + impulse(1, 2000, 1) + death("Cortez", 4500),
        "team-mate impulse": state(1000, teams) + impulse(1, 2000, 3) + death("Cortez", 4500),
        "world impulse": state(1000, teams) + impulse(1, 2000, 1022) + death("Cortez", 4500),
        "stale impulse": state(1000, teams) + impulse(1, 2000, 2) + death("Cortez", 2001 + policy.IMPULSE_WINDOW_MSEC),
        "other victim's impulse": state(1000, teams) + impulse(3, 2000, 2) + death("Cortez", 4500),
        "unknown teams": impulse(1, 2000, 2) + death("Cortez", 4500),
    }
    for name, log in shapes.items():
        [record] = classify(log)
        check(record["status"] == "navigation", f"{name} must stay a navigation death")
        check(policy.decide(record, outcomes(), True)["status"] == "navigation",
              f"{name} cannot be excluded even by an unrecoverable replay")
    # A team-mate's push after the enemy's is the last impulse: not excluded.
    # A team-mate's push after the enemy's is reported, and the replay starts
    # from the enemy's state: only an unrecoverable replay excludes it.
    [later] = classify(state(1000, teams) + impulse(1, 2000, 2) + impulse(1, 2600, 3) + death("Cortez", 4500))
    check(later["status"] == "impulse-candidate" and later["impulse"]["time"] == 2000 and
          [i["attacker"] for i in later["later_impulses"]] == [3],
          "a later team-mate impulse is reported and the enemy's state is replayed")
    check(policy.decide(later, outcomes(survivor="air09"), True)["status"] == "navigation",
          "a recoverable enemy state stays a navigation death whatever followed")
    # The impulse must precede this death, not an earlier one of the same bot.
    [first, second] = classify(state(1000, teams) + impulse(1, 2000, 2) + death("Cortez", 4500) + death("Cortez", 9000))
    check(first["status"] == "impulse-candidate" and second["status"] == "navigation",
          "an impulse before an earlier death does not carry to the next")
    # Free-for-all: any other client is an enemy, teams or not.
    [ffa] = classify(impulse(1, 2000, 3) + death("Cortez", 4500), gametype="DM")
    check(ffa["status"] == "impulse-candidate", "every other client is an enemy in DM")
    # The replay restores the health the hit left, so fall damage counts.
    [hurt] = classify(state(1000, teams) + impulse(1, 2000, 2).replace("health=80", "health=9") + death("Cortez", 4500))
    check(hurt["impulse"]["health"] == 9 and all(t.get("setup") == ["bot_probeHealth 1 9 50 0 0 0 20"]
          for t in policy.trials_for(hurt["impulse"]) if not t.get("counterfactual")),
          "replays run at the victim's post-hit health")
    [slid] = classify(state(1000, teams) + impulse(1, 2000, 2).replace("health=80 knock=50",
                      "health=76 knock=200") + death("Cortez", 4500))
    check(all(t.get("setup") == ["bot_probeHealth 1 76 200 0 0 0 20"] for t in policy.trials_for(slid["impulse"])
              if not t.get("counterfactual")),
          "replays restore the knockback time that removes ground friction")
    for field in ("health", "knock_msec", "pre_knock_msec", "armor", "armor_save", "damage"):
        for value in (None, -1, 1001):
            incomplete = {**candidate, "impulse": {**candidate["impulse"], field: value}}
            check(policy.decide(incomplete, outcomes(), True)["status"] == "navigation",
                  "missing or invalid exact replay state must never use spawn defaults")
    legacy = impulse(1, 2000, 2).replace(" armor=20 armorSave=10", "")
    with patch.object(policy, "replay_session", side_effect=AssertionError("legacy state must not launch")):
        result = policy.classify_log(ROSTER + state(1000, teams) + legacy + death("Cortez", 4500),
            {"Cortez"}, "CTF", "mp/q4ctf5", Path("runtime"), "engine", Path("assets"), Path("output"), 28973)
    check(result["navigation_world_deaths"] == 1 and result["policy_version"] == 2,
          "old logs without armour remain failures without running misleading replays")
    # Trial assessment reads markers, arming, deaths and landings.
    trials = policy.trials_for({"origin": [1, 2, 3], "velocity": [4, 5, 6]})
    check(len([t for t in trials if t["decisive"]]) == policy.AIR_HEADINGS + 1, "16 headings plus no input decide")
    sample = ""
    for t in trials:
        sample += f"MP_BOT_TRIAL_BEGIN {t['name']}\nBOT_AIR_PROBE client=1 time=1 origin=1 2 3\n"
        sample += "Rhodes was killed by world\n" if t["name"] != "air04" else \
            ("BOT_PROBE_GROUNDED client=1 time=2 origin=1 2 3 velocity=0 0 0 health=90\n"
             "MP_PHYSICS slot=1 grounded=1 health=90 velocity=0 0 0\n")
        sample += f"MP_BOT_TRIAL_END {t['name']}\n"
    assessed = {o["name"]: o for o in policy.assess_trials(sample, trials)}
    check(assessed["air04"]["survived"] and not assessed["air05"]["survived"] and assessed["air05"]["died"],
          "trial assessment separates landings from falls")
    check(policy.decide(candidate, list(assessed.values()), True)["status"] == "navigation",
          "one landing heading makes the fall recoverable")
    prefix_trials = [{"name": "air0", "decisive": True}, {"name": "air04", "decisive": True}]
    prefix = {o["name"]: o for o in policy.assess_trials(sample, prefix_trials)}
    check(not prefix["air0"]["armed"] and prefix["air04"]["survived"],
          "trial markers match the entire name, never a prefix")
    duplicate = sample + sample[sample.index("MP_BOT_TRIAL_BEGIN air04"):sample.index("MP_BOT_TRIAL_END air04") + len("MP_BOT_TRIAL_END air04")] + "\n"
    check(not policy.assess_trials(duplicate, [prefix_trials[1]])[0]["armed"],
          "duplicate trial markers fail closed")
    terminal = "MP_PHYSICS slot=1 grounded=1 health=90 velocity=0 0 0\n"
    for replacement in ("", "MP_PHYSICS slot=1 grounded=0 health=90 velocity=0 0 -300\n",
                        "MP_PHYSICS slot=1 grounded=1 health=0 velocity=0 0 0\n",
                        terminal + "MP_PHYSICS slot=1 grounded=0 health=90 velocity=0 0 -300\n"):
        rows = {o["name"]: o for o in policy.assess_trials(sample.replace(terminal, replacement), trials)}
        check(not rows["air04"]["survived"], "an earlier touchdown cannot replace a live grounded final state")
    for obituary in ("drowned", "dissolved in slime", "burned in lava"):
        rows = {o["name"]: o for o in policy.assess_trials(
            sample.replace(terminal, f"Rhodes {obituary}\n" + terminal), trials)}
        check(rows["air04"]["died"] and not rows["air04"]["survived"],
              "a respawn after a liquid death cannot masquerade as recovery")
    settle_trial = dict(name="settle", decisive=True, release_input_after_msec=3000)
    settled = ("MP_BOT_TRIAL_BEGIN settle\nBOT_MOVE_PROBE client=1 time=1\n"
               "BOT_PROBE_GROUNDED client=1 time=2 health=14\n"
               "MP_PHYSICS slot=1 grounded=1 health=14 velocity=0 0 0\n"
               "MP_BOT_RELEASE_INPUT settle\n"
               "MP_PHYSICS slot=1 grounded=1 health=14 velocity=0 0 0\nMP_BOT_TRIAL_END settle\n")
    check(policy.assess_trials(settled, [settle_trial])[0]["survived"], "settled controller survives input release")
    jumped = settled.replace("BOT_MOVE_PROBE", "BOT_JUMP_PROBE")
    check(not policy.assess_trials(jumped, [settle_trial])[0]["armed"], "causal envelopes reject jump markers")
    jump_trial = {**settle_trial, "probe_type": "JUMP"}
    check(policy.assess_trials(jumped, [jump_trial])[0]["survived"], "positive jump regression requires its explicit marker")
    check(not policy.assess_trials(settled, [jump_trial])[0]["armed"], "wrong positive marker rejected")
    mixed = jumped.replace("BOT_JUMP_PROBE client=1 time=1", "BOT_JUMP_PROBE client=1 time=1\nBOT_MOVE_PROBE client=1 time=1")
    check(not policy.assess_trials(mixed, [jump_trial])[0]["armed"], "mixed duplicate probes rejected")
    for error in ("usage: bot_probeHealth <client> <health>",
                  "bot_probeHealth: requires a local isolated server",
                  "Unknown command 'bot_probeHealth'"):
        rejected = settled.replace("MP_BOT_TRIAL_BEGIN settle\n", "MP_BOT_TRIAL_BEGIN settle\n" + error + "\n")
        outcome = policy.assess_trials(rejected, [settle_trial])[0]
        check(not outcome["armed"] and not outcome["survived"] and outcome["setup_errors"] == [error],
              "rejected setup commands cannot qualify a causal replay")
    for changed in (settled.replace("MP_BOT_RELEASE_INPUT settle\n", ""),
                    settled.replace("velocity=0 0 0", "velocity=50 0 0", 1),
                    settled.replace("grounded=1 health=14", "grounded=0 health=14", 1),
                    settled.replace("grounded=1 health=14", "grounded=1 health=0", 1),
                    settled.replace("MP_BOT_RELEASE_INPUT settle\n", "MP_BOT_RELEASE_INPUT settle\nRhodes died\n")):
        check(not policy.assess_trials(changed, [settle_trial])[0]["survived"],
              "input release cannot disguise an unsettled, missing or dead recovery")
    from mp_bot_air_recovery_replay import assess_recovery_trials
    resumed_trial = dict(name="resumed", decisive=True, expect_survival=True, allow_resumed_navigation=True)
    resumed = ("MP_BOT_TRIAL_BEGIN resumed\nBOT_MOVE_PROBE client=1 time=1\n"
               "BOT_PROBE_GROUNDED client=1 time=2 health=18\n"
               "bot Rhodes: completed drop from 1 2 3 to 4 5 6\n"
               "bot Rhodes: completed teleport from 4 5 6 to 7 8 9\n"
               "MP_PHYSICS slot=1 grounded=0 health=18 velocity=10 20 100\nMP_BOT_TRIAL_END resumed\n")
    accepted = assess_recovery_trials(resumed, [resumed_trial])[0]
    check(accepted["recovery_passed"] and not accepted["survived"],
          "resumed navigation is isolated positive recovery evidence, not grounded causal survival")
    rejected = resumed.replace("BOT_MOVE_PROBE", "usage: bot_probeHealth <client> <health>\nBOT_MOVE_PROBE")
    check(not assess_recovery_trials(rejected, [resumed_trial])[0]["recovery_passed"],
          "resumed navigation cannot qualify rejected positive setup")
    for changed in (resumed.replace("completed teleport", "starting teleport"),
                    resumed.replace("bot Rhodes:", "bot Voss:"),
                    resumed.replace("health=18 velocity", "health=0 velocity"),
                    resumed.replace("MP_PHYSICS", "Rhodes died\nMP_PHYSICS"),
                    resumed.replace("BOT_PROBE_GROUNDED client=1 time=2 health=18\n", ""),
                    resumed.replace("MP_BOT_TRIAL_BEGIN resumed\n", "")):
        check(not assess_recovery_trials(changed, [resumed_trial])[0]["recovery_passed"],
              "new motion, missing state, other players and death cannot qualify recovery")
    summary = policy.summarise([policy.decide(candidate, outcomes(), True),
                                policy.decide(candidate, outcomes(survivor="air00"), True)])
    check(summary["navigation_world_deaths"] == 1 and summary["excluded_impulse_deaths"] == 1,
          "the summary counts excluded deaths separately and keeps the rest")
    # A stale-runtime refusal or process-launch failure still returns a
    # complete fail-closed classification instead of losing the match report.
    log = ROSTER + state(1000, teams) + impulse(1, 2000, 2) + death("Cortez", 4500)
    for failure in (SystemExit("stale runtime"), OSError("process launch failed")):
        with patch.object(policy, "replay_session", side_effect=failure):
            failed = policy.classify_log(log, {"Cortez", "Rhodes", "Sledge"}, "CTF", "mp/q4ctf5",
                Path("runtime"), "engine", Path("assets"), Path("output"), 28973)
        check(failed["navigation_world_deaths"] == 1 and failed["excluded_impulse_deaths"] == 0,
              "a refused replay must count the world death")
        check(failed["deaths"][0].get("replay_failure") == str(failure), "retain replay refusal diagnostics")
    for failed_batch in (None, 1):
        calls = []
        def session(*args):
            batch = args[6]
            calls.append(batch)
            sample = "\nMP_BOT_REPLAY_BEGIN\n"
            for trial in batch:
                sample += f"MP_BOT_TRIAL_BEGIN {trial['name']}\nBOT_AIR_PROBE client=1 time=1\n"
                sample += (("BOT_PROBE_GROUNDED client=1 time=2 health=90\n"
                            "MP_PHYSICS slot=1 grounded=1 health=90 velocity=0 0 0\n") if trial.get("counterfactual") else
                           "Rhodes was killed by world\n")
                sample += f"MP_BOT_TRIAL_END {trial['name']}\n"
            return len(calls) - 1 != failed_batch, sample, 0
        with patch.object(policy, "replay_session", side_effect=session):
            result = policy.classify_log(log, {"Cortez", "Rhodes", "Sledge"}, "CTF", "mp/q4ctf5",
                Path("runtime"), "engine", Path("assets"), Path("output"), 28973)
        check(all(len(batch) <= 8 for batch in calls), "replay process lifetimes are bounded")
        if failed_batch is None:
            check(result["excluded_impulse_deaths"] == 1 and len(calls) == 3,
                  "every independent batch and counterfactual must complete")
        else:
            check(result["excluded_impulse_deaths"] == 0 and result["navigation_world_deaths"] == 1,
                  "a failed middle batch cannot borrow completion from earlier trials")
    print("mp_bot_death_policy: OK (positive control excluded; replay/log/launch-failure controls fail closed)")


if __name__ == "__main__":
    main()

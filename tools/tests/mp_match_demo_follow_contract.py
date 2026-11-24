#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Compile the offline demo-camera exception and prove it cannot authorize live POVs."""
from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_disclosure_policy_contract import function_body

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    source = (ROOT / "src/game/MultiplayerGame.cpp").read_text(encoding="utf-8")
    body = function_body(source, "static bool CanLocalServerDemoFollow")
    player_source = (ROOT / "src/game/Player.cpp").read_text(encoding="utf-8")
    prediction = function_body(player_source, "void idPlayer::LocalClientPredictionThink")
    eye_height = function_body(player_source, "void idPlayer::UpdateEyeHeight")
    filters = prediction[prediction.index("buttonMask &= usercmd.buttons;"):prediction.index("walkIK.ClearJointMods();")]
    network = (ROOT / "src/game/Game_network.cpp").read_text(encoding="utf-8")
    initialize = function_body(network, "void idGameLocal::InitLocalClient")
    set_follow = function_body(network, "bool idGameLocal::SetDemoFollowClient")
    interface = (ROOT / "src/game/Game.h").read_text(encoding="utf-8")
    follow_targets = function_body(interface, "enum demoFollowTarget_t")
    # The offline viewer that InitLocalClient spawns lives in ENTITYNUM_NONE, so
    # RegisterEntity's spawn_entnum guard must admit that slot in both game
    # trees (the 9 November guard refused it and every offline MVD playback
    # failed at map load with "spawn_entnum 4095 out of range").
    assert "SpawnPlayer( ENTITYNUM_NONE );" in initialize
    for tree in ("game",):
        register = function_body((ROOT / f"src/{tree}/Game_local.cpp").read_text(encoding="latin-1"),
                                 "void idGameLocal::RegisterEntity")
        assert "spawn_entnum < 0 || spawn_entnum >= MAX_GENTITIES" in register, f"{tree}: RegisterEntity must admit ENTITYNUM_NONE"
        assert "spawn_entnum >= ENTITYNUM_NONE" not in register, f"{tree}: RegisterEntity refuses the demo viewer's slot"
    harness = r'''
#include <stdio.h>
#include <cassert>
#include <cmath>
struct CVar { float value; float GetFloat() const { return value; } };
static CVar pm_normalviewheight{68}, pm_crouchviewheight{40}, pm_deadviewheight{10}, pm_crouchrate{0.87f};
static const int MAX_CLIENTS = 32, ENTITYNUM_NONE = 4095, DEMO_PLAYING = 1;
enum demoFollowTarget_t {
@FOLLOW_TARGETS@
};
static bool CanLocalServerDemoFollow(int observerSlot, int targetSlot);
struct idEntity { bool player = true; bool IsType(int) const { return player; } };
struct idPlayer : idEntity {
    int entityNumber = 0; bool fake = false, spectating = false, wantSpectate = false;
    int spectator = -1, instance = 0, spawns = 0;
    int health = 100;
    bool vehicle = false;
    float eyeHeight = 68;
    struct { bool crouching = false; bool IsCrouching() const { return crouching; } } physicsObj;
    bool IsInVehicle() const { return vehicle; }
    float EyeHeight() const { return eyeHeight; }
    void SetEyeHeight(float value) { eyeHeight = value; }
    void UpdateEyeHeight(bool snap) {
@EYE_HEIGHT@
    }
    bool IsFakeClient() const { return fake; }
    static int GetClassType() { return 0; }
    int GetInstance() const { return instance; }
    void SetInstance(int value) { instance = value; }
    void SpawnFromSpawnSpot() { ++spawns; }
    void SpectateFreeFly(bool force) { assert(force); spectator = entityNumber; }
};
struct State { bool locked = false; bool WeaponsLocked() const { return locked; } };
struct MP {
    bool ingame = true, hasState = true; State state;
    bool IsInGame(int) const { return ingame; }
    State *GetGameState() { return hasState ? &state : nullptr; }
    void ServerSetInstance(int) {}
    bool CanSpectatorFollow(int observer, int target) const { return CanLocalServerDemoFollow(observer, target); }
};
struct Game {
    bool isServer = false, isClient = true, isRepeater = false, isMultiplayer = true;
    bool serverDemo = true, repeaterDemo = false;
    int mode = DEMO_PLAYING, localClientNum = MAX_CLIENTS, numClients = 1;
    idPlayer *local = nullptr; idEntity *entities[ENTITYNUM_NONE + 1] = {}; MP mpGame;
    int followPlayer = -1, created = 0;
    int GetDemoState() const { return mode; }
    bool IsServerDemoPlaying() const { return serverDemo && mode == DEMO_PLAYING; }
    bool IsRepeaterDemoPlaying() const { return repeaterDemo; }
    idPlayer *GetLocalPlayer() const { return local; }
    int GetDemoFollowClient() const { return IsServerDemoPlaying() ? followPlayer : -1; }
    bool SetDemoFollowClient(int clientNum);
    void InitLocalClient(int clientNum);
    void SpawnPlayer(int entity) {
        ++created;
        entities[entity] = local;
        local->entityNumber = entity;
        local->spectator = entity;
    }
} gameLocal;
bool Game::SetDemoFollowClient(int clientNum) {
@SET_FOLLOW@
}
void Game::InitLocalClient(int clientNum) {
@INITIALIZE@
}
static bool CanLocalServerDemoFollow(int observerSlot, int targetSlot) {
@BODY@
}
static const int BUTTON_ATTACK = 1, BUTTON_SCORES = 2;
static void ApplyWeaponWheelInputMask() {}
static void ApplyQuickChatInputMask() {}
static int FilterPredictionButtons(bool spectating, int idealWeapon, int currentWeapon, int buttonMask) {
    struct { int buttons; } usercmd = { BUTTON_ATTACK | BUTTON_SCORES };
@FILTERS@
    return usercmd.buttons;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed line %d\n", __LINE__); return 1; } } while(0)
int main() {
    idPlayer observer, target;
    observer.entityNumber = ENTITYNUM_NONE; observer.fake = true; observer.spectating = true;
    Game valid; valid.local = &observer; valid.entities[0] = &target;
    // Every combination of transport/demo context must require offline server-demo playback.
    for (int mask = 0; mask < 128; ++mask) {
        gameLocal = valid;
        gameLocal.isServer = (mask & 1) != 0;
        gameLocal.isClient = (mask & 2) != 0;
        gameLocal.isRepeater = (mask & 4) != 0;
        gameLocal.serverDemo = (mask & 8) != 0;
        gameLocal.repeaterDemo = (mask & 16) != 0;
        gameLocal.mode = (mask & 32) != 0 ? DEMO_PLAYING : 0;
        gameLocal.localClientNum = (mask & 64) != 0 ? MAX_CLIENTS : 0;
        CHECK(CanLocalServerDemoFollow(ENTITYNUM_NONE, 0) == (mask == (2|8|32|64)));
    }
    gameLocal = valid;
    CHECK(CanLocalServerDemoFollow(ENTITYNUM_NONE, 0));
    for (int slot = -1; slot <= MAX_CLIENTS; ++slot) {
        CHECK(!CanLocalServerDemoFollow(slot, 0));
        CHECK(CanLocalServerDemoFollow(ENTITYNUM_NONE, slot) == (slot == 0));
    }
    observer.fake = false; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); observer.fake = true;
    observer.spectating = false; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); observer.spectating = true;
    observer.entityNumber = 0; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); observer.entityNumber = ENTITYNUM_NONE;
    gameLocal.local = nullptr; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); gameLocal = valid;
    gameLocal.entities[0] = nullptr; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); gameLocal = valid;
    gameLocal.numClients = 0; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); gameLocal = valid;
    gameLocal.mpGame.ingame = false; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); gameLocal = valid;
    target.player = false; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); target.player = true;
    target.fake = true; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); target.fake = false;
    target.spectating = true; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0)); target.spectating = false;
    target.wantSpectate = true; CHECK(!CanLocalServerDemoFollow(ENTITYNUM_NONE, 0));
    // Spectator cycling must survive a weapon switch and every weapon-lock phase,
    // while the same gates continue to suppress active-player firing.
    for (int mask = 0; mask < 32; ++mask) {
        const bool spectating = (mask & 1) != 0, switching = (mask & 2) != 0;
        gameLocal.mpGame.state.locked = (mask & 4) != 0;
        gameLocal.isMultiplayer = (mask & 8) != 0;
        gameLocal.mpGame.hasState = (mask & 16) != 0;
        const bool attack = spectating || !(switching || (gameLocal.isMultiplayer &&
            gameLocal.mpGame.hasState && gameLocal.mpGame.state.locked));
        CHECK(FilterPredictionButtons(spectating, switching ? 1 : 0, 0, 0) ==
            (BUTTON_SCORES | (attack ? BUTTON_ATTACK : 0)));
        CHECK(FilterPredictionButtons(spectating, 0, 0, BUTTON_ATTACK) == BUTTON_SCORES);
    }
    // Round/map replay recreates only the fake viewer. Keep its selected slot,
    // fall back for a missing target, and never reset a surviving viewer.
    target.wantSpectate = false;
    for (int followed = -1; followed < MAX_CLIENTS; ++followed) {
        gameLocal = valid;
        gameLocal.followPlayer = followed;
        target.instance = 7;
        gameLocal.InitLocalClient(MAX_CLIENTS);
        CHECK(gameLocal.created == 1);
        CHECK(gameLocal.localClientNum == MAX_CLIENTS && !gameLocal.isServer && gameLocal.isClient);
        CHECK(gameLocal.followPlayer == (followed == 0 ? 0 : -1));
        CHECK(observer.spectator == (followed == 0 ? 0 : ENTITYNUM_NONE));
        if (followed == 0) { CHECK(observer.instance == 7); }
        observer.spectator = 0;
        gameLocal.InitLocalClient(MAX_CLIENTS);
        CHECK(gameLocal.created == 1 && observer.spectator == 0);
    }
    gameLocal = valid;
    gameLocal.InitLocalClient(0);
    CHECK(gameLocal.created == 0 && gameLocal.localClientNum == 0);
    gameLocal = valid;
    gameLocal.followPlayer = 0;
    target.spectating = true; // The round's reset precedes the next active snapshot.
    gameLocal.InitLocalClient(MAX_CLIENTS);
    CHECK(gameLocal.followPlayer == 0 && observer.spectator == 0);
    target.spectating = false;
    // Native replay controls change the selected POV without simulated attack,
    // jump, or a game-time debounce. This also crosses Tourney instances.
    gameLocal = valid;
    idPlayer second;
    second.instance = 9;
    gameLocal.entities[1] = &second;
    gameLocal.numClients = 2;
    observer.spectator = ENTITYNUM_NONE;
    auto cycle = []() { return gameLocal.SetDemoFollowClient(DEMO_FOLLOW_NEXT); };
    CHECK(cycle() && gameLocal.followPlayer == 0);
    // A paused restart seek has no later prediction frames to blend an eye
    // height left at zero by the inactive snapshot. Restore each recorded stance.
    target.eyeHeight = 0;
    CHECK(gameLocal.SetDemoFollowClient(0) && target.EyeHeight() == 68);
    target.physicsObj.crouching = true;
    CHECK(gameLocal.SetDemoFollowClient(0) && target.EyeHeight() == 40);
    target.health = 0;
    CHECK(gameLocal.SetDemoFollowClient(0) && target.EyeHeight() == 10);
    target.spectating = true;
    CHECK(gameLocal.SetDemoFollowClient(0) && target.EyeHeight() == 0);
    target.spectating = false; target.health = 100; target.physicsObj.crouching = false;
    target.vehicle = true;
    CHECK(gameLocal.SetDemoFollowClient(0) && target.EyeHeight() == 0);
    target.vehicle = false;
    // Ordinary movement still blends standing/crouching and snaps spectators.
    target.UpdateEyeHeight(false);
    CHECK(std::fabs(target.EyeHeight() - 8.84f) < 0.001f);
    target.eyeHeight = 68; target.physicsObj.crouching = true;
    target.UpdateEyeHeight(false);
    CHECK(std::fabs(target.EyeHeight() - 64.36f) < 0.001f);
    target.physicsObj.crouching = false; target.spectating = true;
    target.UpdateEyeHeight(false);
    CHECK(target.EyeHeight() == 0);
    target.spectating = false;
    CHECK(cycle() && gameLocal.followPlayer == 1 && observer.instance == 9);
    CHECK(cycle() && gameLocal.followPlayer == 0 && observer.instance == 7);
    // Previous walks the same eligible slots backwards: from free flight it
    // starts after the last slot, and it wraps past slot 0.
    auto back = []() { return gameLocal.SetDemoFollowClient(DEMO_FOLLOW_PREV); };
    CHECK(gameLocal.SetDemoFollowClient(DEMO_FOLLOW_FREE));
    CHECK(back() && gameLocal.followPlayer == 1 && observer.instance == 9);
    CHECK(back() && gameLocal.followPlayer == 0 && observer.instance == 7);
    CHECK(back() && gameLocal.followPlayer == 1);
    CHECK(!gameLocal.SetDemoFollowClient(DEMO_FOLLOW_PREV - 1));
    CHECK(gameLocal.followPlayer == 1);
    CHECK(gameLocal.SetDemoFollowClient(DEMO_FOLLOW_FREE));
    CHECK(gameLocal.followPlayer == -1 && observer.spectator == ENTITYNUM_NONE);
    CHECK(cycle() && gameLocal.followPlayer == 0);
    second.spectating = true;
    CHECK(cycle() && gameLocal.followPlayer == 0);
    target.wantSpectate = true;
    CHECK(cycle() && gameLocal.followPlayer == -1);
    target.wantSpectate = false;
    gameLocal.isServer = true;
    target.eyeHeight = 19;
    CHECK(!gameLocal.SetDemoFollowClient(0) && !gameLocal.SetDemoFollowClient(-1));
    CHECK(target.EyeHeight() == 19);
    gameLocal.isServer = false;
    gameLocal.mode = 0;
    CHECK(!cycle() && !gameLocal.SetDemoFollowClient(DEMO_FOLLOW_FREE));
    CHECK(gameLocal.followPlayer == -1);
    return 0;
}
'''.replace("@BODY@", body).replace("@FILTERS@", filters).replace("@SET_FOLLOW@", set_follow).replace("@INITIALIZE@", initialize).replace("@FOLLOW_TARGETS@", follow_targets).replace("@EYE_HEIGHT@", eye_height)
    compiler = next((p for n in ("clang++", "g++", "c++") if (p := shutil.which(n))), None)
    if compiler is None:
        raise AssertionError("a native C++ compiler is required")
    temporary = ROOT / ".tmp"
    temporary.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="demo-follow-", dir=temporary) as temp:
        folder = Path(temp)
        cpp, exe = folder / "test.cpp", folder / "test.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print("mp_match_demo_follow_contract: PASS")


if __name__ == "__main__":
    main()

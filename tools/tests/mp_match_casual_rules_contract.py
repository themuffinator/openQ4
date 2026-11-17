#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Compile the live casual-rule import against the production rule core/table."""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_match_vote_electorate_contract import function

ROOT = Path(__file__).resolve().parents[2]
STUBS = r'''
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#define BIT(x) (1 << (x))
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
template<class T> T Max(T a, T b) { return std::max(a,b); }
template<class T> T Min(T a, T b) { return std::min(a,b); }
class idStr : public std::string {
public:
    using std::string::string; using std::string::operator=;
    void Clear() { clear(); } void Append(const char* s) { append(s); }
    int Length() const { return int(size()); }
    static int Icmp(const char* a, const char* b) {
        while (*a && *b && std::tolower((unsigned char)*a) == std::tolower((unsigned char)*b)) { ++a; ++b; }
        return std::tolower((unsigned char)*a) - std::tolower((unsigned char)*b);
    }
    template<class... A> static int snPrintf(char* out, int n, const char* format, A... args) {
        return std::snprintf(out, n, format, args...);
    }
};
struct idDict {
    std::map<std::string, std::string> values;
    void Set(const char* key, const char* value) { values[key] = value; }
    const char* GetString(const char* key) const { auto it=values.find(key); return it==values.end()?"":it->second.c_str(); }
    int GetInt(const char* key) const { return std::atoi(GetString(key)); }
    float GetFloat(const char* key) const { return float(std::atof(GetString(key))); }
    bool GetBool(const char* key) const { return GetInt(key)!=0; }
};
struct idMath {
    static int ClampInt(int a,int b,int v) { return std::max(a,std::min(b,v)); }
    static int Ftoi(float v) { return int(v); }
};
#include "mpgame/mp/GameTypes.h"
#include "mpgame/mp/match/MatchRules.h"
struct CVar {
    std::string value="casual";
    const char* GetString() const { return value.c_str(); }
    void SetString(const char* s) { value=s; }
    bool IsModified() const { return false; }
    void ClearModified() {}
} g_matchProfile;
struct idMultiplayerGame {
    bool competitiveRulesValidForSession=false, competitiveRulesInitialized=false;
    mpRuleValidationReason_t competitiveRulesFailure=MP_RULE_VALID;
    mpCompetitiveRules matchRules;
    bool InitializeCompetitiveRules();
    mpMatchRulesValidationContext_t BuildCompetitiveRuleValidationContext() const { return {}; }
    void MirrorCompetitiveRulesToLegacy() {}
    void PublishCompetitiveRulesIdentity() {}
};
struct Game {
    int gameType=GAME_DM;
    bool isClient=false;
    idDict serverInfo;
    void Warning(const char* text) { std::fprintf(stderr,"%s\n",text); }
    template<class A, class... B> void Warning(const char* text, A first, B... rest) { std::fprintf(stderr,text,first,rest...); std::fputc('\n',stderr); }
} gameLocal;
'''

TESTS = r'''
int main() {
    for (int mode=GAME_DM; mode<NUM_GAME_TYPES; ++mode) {
        if (!MPGameTypeIsSelectable(mode)) continue;
        for (int warmup=0; warmup<=1; ++warmup) {
            for (int useReady=0; useReady<=1; ++useReady) {
                for (const char* percentage : {"0", "0.51", "1"}) {
                    Defaults(); gameLocal.gameType=mode;
                    gameLocal.serverInfo.Set("si_warmup",warmup?"1":"0");
                    gameLocal.serverInfo.Set("si_useReady",useReady?"1":"0");
                    gameLocal.serverInfo.Set("si_warmupReadyPercentage",percentage);
                    idMultiplayerGame mp;
                    CHECK(mp.InitializeCompetitiveRules());
                    const auto& rules=mp.matchRules.Committed();
                    const bool enabled=warmup && useReady && (mode==GAME_DUEL || std::atof(percentage)>0);
                    CHECK(rules.GetInteger(MP_RULE_READINESS_POLICY)==(enabled?MP_READY_INDIVIDUAL:MP_READY_DISABLED));
                    const int threshold=enabled?(mode==GAME_DUEL?10000:int(std::atof(percentage)*10000+0.5)):0;
                    CHECK(rules.GetInteger(MP_RULE_READY_THRESHOLD_BASIS_POINTS)==threshold);
                    CHECK(mp.competitiveRulesValidForSession);
                }
            }
        }
    }
    std::puts("production casual import: all modes, warmup/ready switches and zero/full thresholds: PASS");
}
'''


def main() -> None:
    mp = (ROOT / "src/mpgame/MultiplayerGame.cpp").read_text(encoding="utf-8")
    rules = (ROOT / "src/mpgame/mp/match/MatchRules.cpp").read_text(encoding="utf-8")
    rules = re.sub(r"^#(?:include|pragma).*$", "", rules, flags=re.MULTILINE)
    types = (ROOT / "src/mpgame/mp/GameTypes.cpp").read_text(encoding="utf-8")
    begin = types.index("static const mpGameTypeInfo_t mpGameTypeInfoTable[]")
    end = types.index("\n/*", types.index("const int mpNumGameTypeInfo", begin))
    definitions = types[begin:end] + "\n".join(function(types, signature) for signature in (
        "const mpGameTypeInfo_t *MPGameType(", "int MPGameTypeFlags(",
        "const char *MPGameTypeName(", "bool MPGameTypeIsSelectable(", "bool MPGameTypeHasAny(",
    ))
    cvars = (ROOT / "src/mpgame/gamesys/SysCvar.cpp").read_text(encoding="utf-8")
    defaults = re.findall(r'idCVar\s+\w+\s*\(\s*"(si_[^"]+)"\s*,\s*"([^"]*)"', cvars)
    setters = "\n".join(f'gameLocal.serverInfo.Set("{key}","{value}");' for key, value in defaults)
    program = (STUBS + definitions + rules + function(mp, "bool idMultiplayerGame::InitializeCompetitiveRules(") +
               "\nvoid Defaults() { gameLocal.serverInfo=idDict();\n" + setters + "\n}\n" + TESTS)
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for casual-rule regressions")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="casual-rules-", dir=ROOT / ".tmp") as directory:
        path = Path(directory)
        cpp, exe = path / "rules.cpp", path / ("rules.exe" if os.name == "nt" else "rules")
        cpp.write_text(program, encoding="utf-8")
        command = [compiler, "-std=c++17", f"-I{ROOT / 'src'}", str(cpp), "-o", str(exe)]
        if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print("mp_match_casual_rules_contract: PASS")


if __name__ == "__main__":
    main()

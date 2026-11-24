// Team-order parsing for the multiplayer bots.  mp_bot_team_orders.py splices
// BotOrderWordsAt, the phrase tables and BotParseTeamOrder verbatim from
// src/game/bots/Bot.cpp, and botOrderKind_t from Bot.h, into this file.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

// PRODUCTION_ORDER_KINDS

// PRODUCTION_FUNCTIONS

struct Result {
    int kind;
    bool everyone, here;
    bool addressed[8];
};

static Result Parse(const char *text, const char *const *names, int numNames) {
    Result r;
    std::memset(&r, 0, sizeof(r));
    r.kind = BotParseTeamOrder(text, names, numNames, r.addressed, r.everyone, r.here);
    return r;
}

int main() {
    const char *names[] = { "kane", "sorg", "bagby", "sorg two" };
    const int numNames = 4;
    int cases = 0;

    // The ordinary forms.
    Result r = Parse("kane defend", names, numNames);
    CHECK(r.kind == BOTORDER_DEFEND && r.addressed[0] && !r.addressed[1] && !r.everyone && !r.here); cases++;
    r = Parse("everyone follow me", names, numNames);
    CHECK(r.kind == BOTORDER_FOLLOW && r.everyone); cases++;
    r = Parse("sorg and bagby hold here please", names, numNames);
    CHECK(r.kind == BOTORDER_DEFEND && r.here && r.addressed[1] && r.addressed[2] && !r.addressed[0]); cases++;
    r = Parse("kane defend the base", names, numNames);
    CHECK(r.kind == BOTORDER_DEFEND && !r.here && r.addressed[0]); cases++;
    r = Parse("kane please defend here", names, numNames);
    CHECK(r.kind == BOTORDER_DEFEND && r.here); cases++;
    r = Parse("all attack", names, numNames);
    CHECK(r.kind == BOTORDER_ATTACK && r.everyone); cases++;
    r = Parse("kane get the flag", names, numNames);
    CHECK(r.kind == BOTORDER_ATTACK && r.addressed[0]); cases++;
    r = Parse("bagby defence", names, numNames);
    CHECK(r.kind == BOTORDER_DEFEND && r.addressed[2]); cases++;
    r = Parse("kane dismissed", names, numNames);
    CHECK(r.kind == BOTORDER_NONE && r.addressed[0]); cases++;
    r = Parse("team cover me", names, numNames);
    CHECK(r.kind == BOTORDER_FOLLOW && r.everyone); cases++;

    // The longest name wins, so one name starting another addresses the right bot.
    r = Parse("sorg two follow", names, numNames);
    CHECK(r.kind == BOTORDER_FOLLOW && r.addressed[3] && !r.addressed[1]); cases++;
    r = Parse("sorg follow", names, numNames);
    CHECK(r.kind == BOTORDER_FOLLOW && r.addressed[1] && !r.addressed[3]); cases++;

    // Team talk that mentions an order word is not an order.
    CHECK(Parse("i will defend", names, numNames).kind == -1); cases++;
    CHECK(Parse("kane is following me", names, numNames).kind == -1); cases++;
    CHECK(Parse("defend kane", names, numNames).kind == -1); cases++;
    CHECK(Parse("kanes defend", names, numNames).kind == -1); cases++;
    CHECK(Parse("kane", names, numNames).kind == -1); cases++;
    CHECK(Parse("team", names, numNames).kind == -1); cases++;
    CHECK(Parse("kane and", names, numNames).kind == -1); cases++;
    CHECK(Parse("kane defending", names, numNames).kind == -1); cases++;
    CHECK(Parse("", names, numNames).kind == -1); cases++;
    CHECK(Parse("kane defend", names, 0).kind == -1); cases++;

    std::printf("Team orders: %d parses, addressees anchored before the phrase: PASS\n", cases);
    return 0;
}

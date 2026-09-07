// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../../src/ui/ChatHistory.h"
#include <cassert>
#include <iostream>

static std::vector<oq4chat::Row> Rows(const oq4chat::History &history, bool split = false) {
    std::vector<oq4chat::Row> rows;
    for (const auto &message : history.messages) {
        rows.push_back({message.serial, 0, message.text, message.team});
        if (split) { rows.push_back({message.serial, 3, message.text, message.team}); }
    }
    return rows;
}
int main() {
    oq4chat::History history;
    for (int i = 0; i < 64; ++i) { history.Append(std::to_string(i), i % 2 == 0); }
    auto rows = Rows(history);
    assert(history.First(rows, 6) == 58);
    history.Scroll(rows, 6, -10);
    const auto anchor = rows[history.First(rows, 6)].serial;
    history.Append("newest", false);
    rows = Rows(history);
    assert(history.messages.size() == 64);
    assert(rows[history.First(rows, 6)].serial == anchor);
    assert(!history.Following());
    rows = Rows(history, true);
    assert(rows[history.First(rows, 3)].serial == anchor);
    history.Scroll(rows, 3, 1000);
    assert(history.Following());
    history.Append("arrived while minimized", true);
    rows = Rows(history, true);
    assert(history.First(rows, 8) == rows.size() - 8);
    history.Scroll(rows, 8, -1000);
    for (int i = 0; i < 70; ++i) { history.Append("eviction", false); }
    rows = Rows(history);
    assert(history.First(rows, 6) == 0);
    history.Clear();
    assert(history.First({}, 6) == 0 && history.Following());
    history.Append(std::string(4095, 'x') + "\xc3\xa9", false);
    assert(history.messages.front().text.size() == 4095);
    history.Append("", false);
    assert(history.messages.size() == 1);

    oq4chat::SentHistory sent;
    sent.Commit("first"); sent.Commit("second"); sent.Commit("second");
    assert(sent.Recall(-1, "unfinished draft") == "second");
    assert(sent.Recall(-1, "second") == "first");
    assert(sent.Recall(-1, "first") == "first");
    assert(sent.Recall(1, "first") == "second");
    assert(sent.Recall(1, "second") == "unfinished draft");
    sent.Commit(" \t\n");
    assert(sent.Recall(-1, "") == "second");
    for (int i = 0; i < 40; ++i) { sent.Commit(std::to_string(i)); }
    for (int i = 39; i >= 8; --i) { assert(sent.Recall(-1, "draft") == std::to_string(i)); }
    assert(sent.Recall(-1, "oldest") == "8");
    oq4chat::SentHistory team;
    assert(team.Recall(-1, "private draft") == "private draft");
    std::cout << "Chat history: follow, eviction, reflow, UTF-8 bounds, recall and drafts passed\n";
}

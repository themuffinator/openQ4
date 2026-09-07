// Copyright (C) 2026 DarkMatter Productions
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENQ4_CHAT_HISTORY_H
#define OPENQ4_CHAT_HISTORY_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// Presentation state only. Routing and permission to receive team messages stay
// in the game module. Nothing here is serialized into demos or saved games.
namespace oq4chat {
struct Message {
    std::uint64_t serial;
    std::string text;
    bool team;
};
struct Row {
    std::uint64_t serial;
    std::size_t offset;
    std::string text;
    bool team;
};

class History {
public:
    static constexpr std::size_t Capacity = 64;
    static constexpr std::size_t MaxMessageBytes = 4096;
    std::deque<Message> messages;
    std::uint64_t revision = 0;

    void Clear() { messages.clear(); ++revision; Follow(); }
    void Append(const std::string &text, bool team) {
        if (text.empty()) { return; }
        std::size_t size = (std::min)(text.size(), MaxMessageBytes);
        // A byte limit must not split a UTF-8 character.
        if (size < text.size()) {
            while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xc0) == 0x80) { --size; }
        }
        if (messages.size() == Capacity) { messages.pop_front(); }
        messages.push_back({++revision, text.substr(0, size), team});
    }
    void Follow() { following = true; }
    bool Following() const { return following; }

    std::size_t First(const std::vector<Row> &rows, std::size_t visible) {
        const std::size_t maximum = rows.size() > visible ? rows.size() - visible : 0;
        std::size_t first = maximum;
        if (!following && !rows.empty()) {
            first = 0;
            // Keep the same message and source character at the top across
            // arrival, eviction, font changes and word reflow.
            while (first + 1 < rows.size() &&
                (rows[first + 1].serial < anchorSerial ||
                 (rows[first + 1].serial == anchorSerial && rows[first + 1].offset <= anchorOffset))) { ++first; }
            first = (std::min)(first, maximum);
        }
        Remember(rows, first);
        return first;
    }
    void Scroll(const std::vector<Row> &rows, std::size_t visible, int delta) {
        if (rows.empty() || visible == 0) { return; }
        const int maximum = static_cast<int>(rows.size() > visible ? rows.size() - visible : 0);
        const int first = static_cast<int>(First(rows, visible));
        const int next = (std::max)(0, (std::min)(maximum, first + delta));
        following = next == maximum;
        Remember(rows, static_cast<std::size_t>(next));
    }
private:
    bool following = true;
    std::uint64_t anchorSerial = 0;
    std::size_t anchorOffset = 0;
    void Remember(const std::vector<Row> &rows, std::size_t first) {
        if (first < rows.size()) { anchorSerial = rows[first].serial; anchorOffset = rows[first].offset; }
    }
};

// Separate recall per channel prevents an All-channel recall from exposing a
// team draft. The live draft is restored after navigating past the newest item.
class SentHistory {
public:
    void Begin() { cursor = entries.size(); draft.clear(); }
    void Commit(const std::string &text) {
        if (text.find_first_not_of(" \t\r\n") != std::string::npos &&
            (entries.empty() || entries.back() != text)) {
            if (entries.size() == 32) { entries.pop_front(); }
            entries.push_back(text);
        }
        Begin();
    }
    std::string Recall(int direction, const std::string &current) {
        if (cursor == entries.size()) { draft = current; }
        if (direction < 0 && cursor > 0) { --cursor; }
        if (direction > 0 && cursor < entries.size()) { ++cursor; }
        return cursor == entries.size() ? draft : entries[cursor];
    }
private:
    std::deque<std::string> entries;
    std::size_t cursor = 0;
    std::string draft;
};
}
#endif

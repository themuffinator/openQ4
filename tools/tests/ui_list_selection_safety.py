#!/usr/bin/env python3
"""Execute production list selection and bounded copy methods with a GUI stub.

The fixture checks dictionary-driven row bounds, literal labels and the existing
selected-ID output contract without launching the engine or controlling input.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body


ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

struct Common {
    void Warning(const char*, ...) { assert(false && "unexpected string warning"); }
} commonObject;
namespace idLib { Common* common = &commonObject; }
struct idStr { static void Copynz(char*, const char*, int); };

const char* va(const char* format, ...) {
    static char buffer[256];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    assert(length >= 0 && length < static_cast<int>(sizeof(buffer)));
    return buffer;
}

struct Dictionary {
    std::map<std::string, std::string> values;
    int GetInt(const char* key, const char* fallback) const {
        const auto found = values.find(key);
        return std::stoi(found == values.end() ? fallback : found->second);
    }
    const char* GetString(const char* key, const char* fallback) const {
        const auto found = values.find(key);
        return found == values.end() ? fallback : found->second.c_str();
    }
};
struct GUI {
    Dictionary state;
    int writes = 0;
    const Dictionary& State() const { return state; }
    void SetStateInt(const char* key, int value) {
        assert(std::string(key) == "servers_selid_0");
        state.values[key] = std::to_string(value);
        ++writes;
    }
};
struct Ids {
    std::vector<int> rows;
    mutable int reads = 0;
    int Num() const { return static_cast<int>(rows.size()); }
    int operator[](int row) const {
        assert(row >= 0 && row < Num());
        ++reads;
        return rows.at(row);
    }
};
struct idListGUILocal {
    GUI* m_pGUI;
    std::string m_name = "servers";
    Ids m_ids;
    int GetSelection(char*, int, int = 0) const;
};
'''

MAIN = r'''
int main() {
    GUI gui;
    idListGUILocal list{&gui, "servers", {{42, 9001}}};
    gui.state.values["servers_selid_0"] = "777";
    auto invalid = [&]() {
        char text[] = "sentinel";
        const int writes = gui.writes, reads = list.m_ids.reads;
        const auto previous = gui.state.values;
        assert(list.GetSelection(text, sizeof(text)) == -1);
        assert(text[0] == 0 && text[1] == 'e');
        assert(list.GetSelection(nullptr, 0) == -1);
        assert(gui.writes == writes && list.m_ids.reads == reads);
        assert(gui.state.values == previous);
    };
    invalid(); // Missing selection defaults to no selection.
    for (int row : {-1, -2, std::numeric_limits<int>::min(), 2,
                    std::numeric_limits<int>::max()}) {
        gui.state.values["servers_sel_0"] = std::to_string(row);
        invalid();
    }
    list.m_ids.rows.clear();
    gui.state.values["servers_sel_0"] = "0";
    invalid();
    list.m_ids.rows = {42, 9001};

    const std::string literal = "100% %s %n %08x %% caf\xc3\xa9";
    gui.state.values["servers_item_0"] = literal;
    gui.state.values["servers_item_1"] = "last row";
    char text[128];
    int before = gui.writes;
    assert(list.GetSelection(text, sizeof(text)) == 42);
    assert(text == literal && gui.writes == before + 1);
    assert(gui.state.values.at("servers_selid_0") == "42");

    // Output capacity bounds the copy; guards on both sides stay untouched.
    std::array<char, 12> guarded;
    guarded.fill('!');
    assert(list.GetSelection(guarded.data() + 2, 5) == 42);
    assert(std::memcmp(guarded.data() + 2, "100%\0", 5) == 0);
    for (int i : {0, 1, 7, 8, 9, 10, 11}) assert(guarded[i] == '!');
    guarded.fill('!');
    assert(list.GetSelection(guarded.data() + 2, 1) == 42);
    for (int i = 0; i < 12; ++i) assert(guarded[i] == (i == 2 ? 0 : '!'));

    // Optional/unwritable text outputs still perform valid ID selection.
    for (int capacity : {0, -1, std::numeric_limits<int>::min()}) {
        guarded.fill('!');
        before = gui.writes;
        assert(list.GetSelection(guarded.data() + 2, capacity) == 42);
        for (char c : guarded) assert(c == '!');
        assert(gui.writes == before + 1);
        gui.state.values["servers_sel_0"] = "-2";
        assert(list.GetSelection(guarded.data() + 2, capacity) == -1);
        for (char c : guarded) assert(c == '!');
        assert(gui.writes == before + 1);
        gui.state.values["servers_sel_0"] = "0";
    }
    assert(list.GetSelection(nullptr, 0) == 42);
    assert(list.GetSelection(nullptr, 128) == 42);

    // A secondary selection slot still publishes the existing selid_0 key.
    gui.state.values["servers_sel_1"] = "1";
    assert(list.GetSelection(text, sizeof(text), 1) == 9001);
    assert(std::string(text) == "last row");
    assert(gui.state.values.at("servers_selid_0") == "9001");
    gui.state.values.erase("servers_item_1");
    assert(list.GetSelection(text, sizeof(text), 1) == 9001 && text[0] == 0);
    assert(gui.state.values.at("servers_selid_0") == "9001");
    std::puts("list selection: row bounds, literal labels, buffer limits and selected-ID contract passed");
}
'''


def main():
    source = (ROOT / 'src/ui/ListGUI.cpp').read_text(encoding='utf-8')
    # Str.cpp retains legacy byte tables outside this ASCII method.
    strings = (ROOT / 'src/idlib/Str.cpp').read_text(encoding='latin-1')
    code = (SUPPORT + function_body(strings, 'void idStr::Copynz(') + '\n' +
            function_body(source, 'int idListGUILocal::GetSelection(') + MAIN)
    compiler = next((found for name in ('clang++', 'g++', 'c++')
                     if (found := shutil.which(name))), None)
    if not compiler:
        raise RuntimeError('C++ compiler required')
    (ROOT / '.tmp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ui-list-selection-', dir=ROOT / '.tmp') as temp:
        test_source = Path(temp) / 'selection.cpp'
        binary = Path(temp) / 'selection.exe'
        test_source.write_text(code, encoding='utf-8')
        subprocess.run([compiler, '-std=c++17', str(test_source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()

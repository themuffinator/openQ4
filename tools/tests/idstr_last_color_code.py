#!/usr/bin/env python3
"""Run production idStr::GetLastColorCode on wrapped-chat cases, with mutants.

The chat window carries each wrapped line's last color code onto the next
line. An icon or command escape sets no color and must not be carried, and a
^r reset leaves the default color in effect. Both single reversals (the old
`|| S_ESCAPE_COLOR` test that accepted every escape, and no reset rule) must
fail.
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
FIXED_TYPE = "escapeType == S_ESCAPE_COLORINDEX || escapeType == S_ESCAPE_COLOR"
RESET = re.compile(r"\n\t\t// \^r resets.*?\n\t\t\}\n", re.S)

HARNESS = r'''
#include <cstdio>
#include <cstdlib>
#include <string>
@CONSTANTS@
class idStr {
public:
	idStr() = default;
	idStr( const char *text ) : text( text ) {}
	int Length( void ) const { return static_cast<int>( text.size() ); }
	void Append( const char *s, int count ) { text.append( s, static_cast<size_t>( count ) ); }
	const char *c_str( void ) const { return text.c_str(); }
	static int IsEscape( const char *s, int *type );
	idStr GetLastColorCode( void ) const;
private:
	std::string text;
};
// The production code indexes idStr's buffer as `data`.
#define data text
@PRODUCTION@
#undef data
static int failures = 0;
static void Expect( const char *line, const char *expected ) {
	const std::string actual = idStr( line ).GetLastColorCode().c_str();
	if ( actual != expected ) {
		std::fprintf( stderr, "\"%s\": got \"%s\", expected \"%s\"\n", line, actual.c_str(), expected );
		++failures;
	}
}
int main() {
	Expect( "^1red ^iwpn1 tail", "^1" );     // an icon after a color sets no color
	Expect( "^1red ^nX tail", "^1" );        // nor does another command
	Expect( "^1red^r plain", "" );           // a reset leaves the default color
	Expect( "^1red^r ^2green", "^2" );
	Expect( "^c487orange", "^c487" );
	Expect( "^2a^3b", "^3" );
	Expect( "^iwpn1 only an icon", "" );
	Expect( "plain text", "" );
	Expect( "", "" );
	if ( failures ) return 1;
	std::puts( "idstr_last_color_code: color codes, resets, icons and commands passed" );
	return 0;
}
'''


def constants(header: str) -> str:
    lines = [f"constexpr int {name} = 1 << {bit};"
             for name, bit in re.findall(r"#define\s+(S_ESCAPE_\w+)\s+BIT\((\d+)\)", header)]
    escape = re.search(r"const int C_COLOR_ESCAPE\s*=\s*('.');", header).group(1)
    return "\n".join(lines + [f"constexpr int C_COLOR_ESCAPE = {escape};"])


def run(compiler: str, work: Path, name: str, production: str, header: str) -> subprocess.CompletedProcess:
    source = work / f"{name}.cpp"
    source.write_text(HARNESS.replace("@CONSTANTS@", constants(header)).replace("@PRODUCTION@", production),
                      encoding="latin-1")
    executable = work / (f"{name}.exe" if os.name == "nt" else name)
    subprocess.run([compiler, "-std=c++17", str(source), "-o", str(executable)], cwd=work, check=True,
                   env=dict(os.environ, TEMP=str(work), TMP=str(work)), timeout=120)
    return subprocess.run([str(executable)], cwd=work, capture_output=True, text=True, timeout=60)


def main() -> None:
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++ compiler is required")
    source = (ROOT / "src/idlib/Str.cpp").read_text(encoding="latin-1")
    header = (ROOT / "src/idlib/Str.h").read_text(encoding="latin-1")
    production = (function_body(source, "int idStr::IsEscape( const char *s, int* type )") + "\n" +
                  function_body(source, "idStr idStr::GetLastColorCode( void ) const"))
    if FIXED_TYPE not in production or not RESET.search(production):
        raise SystemExit("GetLastColorCode no longer has the color-type test and the reset rule this test reverses")
    mutants = {
        "any-escape": production.replace(FIXED_TYPE, "escapeType == S_ESCAPE_COLORINDEX || S_ESCAPE_COLOR"),
        "no-reset": RESET.sub("\n", production, count=1),
    }
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="idstr-color-", dir=ROOT / ".tmp") as directory:
        work = Path(directory)
        result = run(compiler, work, "production", production, header)
        if result.returncode:
            raise SystemExit("production failed:\n" + result.stderr)
        print(result.stdout.strip())
        for name, mutant in mutants.items():
            if run(compiler, work, name, mutant, header).returncode == 0:
                raise SystemExit(f"mutant {name} was not rejected")
        print(f"idstr_last_color_code: {len(mutants)} single-reversal controls fail")


if __name__ == "__main__":
    main()

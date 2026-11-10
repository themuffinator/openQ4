#!/usr/bin/env python3
"""Run the touch layout editor's Java model and the engine's C++ placement over one matrix.

The Android setup previews the touch controls with TouchLayout.java, a port of
src/sys/android/android_touch_controls.h. Both read the same layout record and
must place every control the same way: this compiles the production header and
the production Java class, feeds them the same windows, densities, sizes,
presets and edit records, and compares every placement, flag and opacity. It
also checks the record grammar on both sides (accepted, refused and canonical
forms) and the editor's drop rule: a drop lands on the 8 dp grid when it fits
and on the spot placement chooses when it does not, and the result always fits.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PRODUCTION = ROOT / 'mobile/android/app/src/main/java/com/darkmatter/openq4'

# Windows in dp (landscape; portrait swaps them), densities, player sizes.
WINDOWS = [(640, 360), (732, 412), (832, 384), (915, 412), (1024, 768), (1280, 800), (960, 600)]
DENSITIES = [1.0, 2.0, 2.75]
SCALES = [0.75, 1.0, 1.5]

DEFAULT = ';'.join(['*,*,100,35,0'] * 9)


def orientation(entries: dict) -> str:
    parts = []
    for i in range(9):
        parts.append(entries.get(i, '*,*,100,35,0'))
    return ';'.join(parts)


# Edit records exercised on both sides: none, the explicit default, single and
# combined edits, overlaps that must resolve, and sizes that cannot fit.
RECORDS = [
    '',
    f'1|{DEFAULT}|{DEFAULT}',
    '1|' + orientation({0: '-160,-120,100,35,0'}) + '|' + orientation({0: '-120,-200,100,35,0'}),
    '1|' + orientation({0: '*,*,150,60,0', 1: '*,*,100,35,1'}) + '|' + orientation({3: '*,*,75,10,0', 5: '*,*,100,35,1'}),
    '1|' + orientation({1: '-128,-120,100,35,0'}) + '|' + orientation({6: '-40,40,100,35,0', 7: '40,40,100,35,0'}),
    '1|' + orientation({6: '200,-80,125,80,0', 8: '-200,180,100,35,1', 4: '96,96,100,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({i: '*,*,150,80,0' for i in range(9)}) + '|' + orientation({i: '*,*,150,80,0' for i in range(9)}),
    '1|' + orientation({0: '24,24,150,35,0', 1: '40,40,150,35,0', 2: '56,56,150,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({i: '*,*,75,20,0' for i in range(9)}) + '|' + orientation({2: '-1,-1,100,35,0', 0: '8191,8191,100,35,0'}),
]

VALID_GRAMMAR = RECORDS[1:] + [
    '1|' + orientation({0: '0100,-0016,0150,080,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '-8192,8192,100,35,0'}) + '|' + DEFAULT,
]
INVALID_GRAMMAR = [
    '', '1', '2|' + DEFAULT + '|' + DEFAULT, '1|' + DEFAULT, '1|' + DEFAULT + '|' + DEFAULT + '|',
    '1|' + DEFAULT + '|' + DEFAULT + ';*,*,100,35,0',
    '1|' + orientation({0: '0,10,100,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '10,*,100,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '*,*,70,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '*,*,155,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '*,*,101,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '*,*,100,5,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '*,*,100,85,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '*,*,100,36,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '*,*,100,35,2'}) + '|' + DEFAULT,
    '1|' + orientation({6: '*,*,100,35,1'}) + '|' + DEFAULT,
    '1|' + orientation({0: '8193,10,100,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '10000,10,100,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '+10,10,100,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '10 ,10,100,35,0'}) + '|' + DEFAULT,
    '1|' + orientation({0: '--10,10,100,35,0'}) + '|' + DEFAULT,
    '1|' + DEFAULT + '|' + DEFAULT + '\n',
    '1|' + DEFAULT.replace(';', ',', 1) + '|' + DEFAULT,
    '1|' + '|'.join([DEFAULT + ';' + ';'.join(['*,*,100,35,0'] * 30)] * 2),
]

CPP = r'''
#include "src/sys/android/android_touch_controls.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace openq4::touch;
static void dump(const char* tag,const Layout& l){
    std::printf("%s %d %d %d %u %.5f %.5f",tag,l.available?1:0,l.edited?1:0,l.editsRejected?1:0,l.hidden,l.stickTop,l.stickHome.y);
    for(int i=0;i<BUTTON_COUNT;i++)std::printf(" %.5f %.5f %.5f %.4f",l.buttons[i].x,l.buttons[i].y,l.buttons[i].radius,RestOpacity(l,i));
    std::printf("\n");
}
int main(int argc,char** argv){
    char line[2048];
    while(std::fgets(line,sizeof line,stdin)){
        line[std::strcspn(line,"\r\n")]=0;
        if(!std::strncmp(line,"P ",2)){OrientationEdits e;std::printf("P %d\n",ParseLayoutEdits(line+2,e)?1:0);continue;}
        if(line[0]!='L')return 2;
        char* cursor=line+1;
        const float w=std::strtof(cursor,&cursor),h=std::strtof(cursor,&cursor),d=std::strtof(cursor,&cursor),s=std::strtof(cursor,&cursor);
        const int preset=static_cast<int>(std::strtol(cursor,&cursor,10));
        const char* record=std::strchr(line,'|')?std::strstr(line," R ")+3:"";
        OrientationEdits edits;const bool ok=*record&&ParseLayoutEdits(record,edits);
        dump("L",ComputeLayout(w,h,d,s,preset,ok?&edits.For(w,h):nullptr));
    }
    (void)argc;(void)argv;return 0;
}
'''

JAVA = r'''
package com.darkmatter.openq4;
import java.io.*;
import java.util.*;
public class TouchLayoutCheck {
    static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    static String dump(String tag,TouchLayout.Result l){
        StringBuilder b=new StringBuilder(tag);
        b.append(String.format(Locale.ROOT," %d %d %d %d %.5f %.5f",l.available?1:0,l.edited?1:0,l.editsRejected?1:0,l.hidden,l.stickTop,l.stickHomeY));
        for(int i=0;i<TouchLayout.COUNT;i++)b.append(String.format(Locale.ROOT," %.5f %.5f %.5f %.4f",l.x[i],l.y[i],l.radius[i],l.opacity(i)));
        return b.toString();
    }
    public static void main(String[] args)throws IOException{
        BufferedReader in=new BufferedReader(new InputStreamReader(System.in,"UTF-8"));
        PrintStream out=new PrintStream(new FileOutputStream(FileDescriptor.out),true,"UTF-8");
        String line;int drops=0,snapped=0;
        while((line=in.readLine())!=null){
            if(line.startsWith("P ")){out.println("P "+(TouchLayout.Record.decode(line.substring(2))!=null?1:0));continue;}
            if(line.startsWith("C ")){
                TouchLayout.Record r=TouchLayout.Record.decode(line.substring(2));
                check(r!=null,"valid record refused: "+line);
                TouchLayout.Record again=TouchLayout.Record.decode(r.encode());
                check(again!=null && again.landscape.equals(r.landscape) && again.portrait.equals(r.portrait),"record round trip");
                out.println("C "+r.encode());continue;
            }
            String[] f=line.split(" ");
            float w=Float.parseFloat(f[1]),h=Float.parseFloat(f[2]),d=Float.parseFloat(f[3]),s=Float.parseFloat(f[4]);
            int preset=Integer.parseInt(f[5]);
            int at=line.indexOf(" R ");String record=at<0?"":line.substring(at+3);
            TouchLayout.Record r=record.isEmpty()?null:TouchLayout.Record.decode(record);
            TouchLayout.Edits edits=r==null?null:r.get(h>w);
            TouchLayout.Result result=TouchLayout.compute(w,h,d,s,preset,edits);
            out.println(dump("L",result));
            // The editor's drop rule: every control dropped across a coarse
            // grid either lands on the snapped point or where placement puts
            // it, and the stored record reproduces what the editor shows.
            if(result.available && preset==0 && Math.abs(s-1)<1e-6 && (edits==null||edits.isDefault())){
                TouchLayout.Edits base=TouchLayout.Edits.DEFAULT;
                for(int b=0;b<TouchLayout.COUNT;b++)for(float x=w*.1f;x<w;x+=w*.2f)for(float y=h*.1f;y<h;y+=h*.27f){
                    TouchLayout.Edits next=TouchLayout.drop(w,h,d,s,preset,base,b,x,y);
                    drops++;if(next==null)continue;
                    TouchLayout.Result placed=TouchLayout.compute(w,h,d,s,preset,next);
                    check(placed.edited && !placed.editsRejected,"a stored drop does not fit");
                    TouchLayout.Edit e=next.get(b);
                    float ex=e.x>0?e.x*d:w+e.x*d,ey=e.y>0?e.y*d:h+e.y*d;
                    check(Math.abs(placed.x[b]-ex)<=.5f*d+.01f && Math.abs(placed.y[b]-ey)<=.5f*d+.01f,
                          "stored drop differs from the shown position: "+b+" "+placed.x[b]+","+placed.y[b]+" vs "+ex+","+ey);
                    if(Math.abs(e.x)%TouchLayout.GRID_DP==0 && Math.abs(e.y)%TouchLayout.GRID_DP==0)snapped++;
                }
            }
        }
        out.println("D "+drops+" "+snapped);
    }
}
'''


def compare(cpp_lines: list[str], java_lines: list[str], cases: list[str]) -> int:
    assert len(cpp_lines) == len(java_lines) == len(cases), (len(cpp_lines), len(java_lines), len(cases))
    for case, a, b in zip(cases, cpp_lines, java_lines):
        fa, fb = a.split(), b.split()
        assert len(fa) == len(fb), (case, a, b)
        assert fa[:5] == fb[:5], f'flags differ for {case}:\n  C++  {a}\n  Java {b}'
        for index, (x, y) in enumerate(zip(fa[5:], fb[5:])):
            # Positions and radii agree to 0.02 px; opacities are exact
            # percentages.
            exact = index >= 2 and (index - 2) % 4 == 3
            assert (x == y) if exact else abs(float(x) - float(y)) <= 0.02, \
                f'placement differs for {case}:\n  C++  {a}\n  Java {b}'
    return len(cases)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--javac', default=shutil.which('javac') or str(Path(os.environ.get('JAVA_HOME', '')) / 'bin/javac.exe'))
    parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++'))
    args = parser.parse_args()
    if not args.cxx:
        parser.error('C++17 compiler required')
    out = ROOT / '.tmp/android-touch-layout'
    out.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, 'TMP': str(out), 'TEMP': str(out), 'TMPDIR': str(out)}

    cases = []
    for (w, h) in WINDOWS:
        for portrait in (False, True):
            ww, hh = (h, w) if portrait else (w, h)
            for density in DENSITIES:
                for scale in SCALES:
                    for preset in (0, 1, 2):
                        for index, record in enumerate(RECORDS):
                            line = f'L {ww * density:g} {hh * density:g} {density:g} {scale:g} {preset} {index}'
                            cases.append(line + (f' R {record}' if record else ''))
    grammar = [f'P {record}' for record in VALID_GRAMMAR + INVALID_GRAMMAR if '\n' not in record]
    canonical = [f'C {record}' for record in VALID_GRAMMAR]

    cpp = out / 'layout.cpp'
    binary = out / ('layout.exe' if os.name == 'nt' else 'layout')
    cpp.write_text(CPP, encoding='utf-8')
    # -ffp-contract=off, as meson.build gives every engine source: a compiler
    # that fuses a*b+c by default (Apple's ARM64 clang) otherwise rounds the
    # two sides of an equal-distance choice in PlaceButton differently and
    # places a control 80 px from where the engine and the Java model put it.
    subprocess.run([args.cxx, '-std=c++17', '-O1', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(cpp),
                    '-o', str(binary)], cwd=out, env=env, check=True)
    cpp_out = subprocess.run([str(binary)], input='\n'.join(cases + grammar) + '\n', capture_output=True, text=True,
                             check=True).stdout.splitlines()

    unit = out / 'TouchLayoutCheck.java'
    unit.write_text(JAVA, encoding='utf-8')
    classes = out / 'classes'
    classes.mkdir(exist_ok=True)
    javac = Path(args.javac).resolve()
    subprocess.run([str(javac), '--release', '17', '-d', str(classes), str(PRODUCTION / 'TouchLayout.java'), str(unit)], check=True)
    java = javac.with_name('java.exe' if os.name == 'nt' else 'java')
    java_out = subprocess.run([str(java), '-cp', str(classes), 'com.darkmatter.openq4.TouchLayoutCheck'],
                              input='\n'.join(cases + grammar + canonical) + '\n', capture_output=True, text=True,
                              encoding='utf-8', check=True).stdout.splitlines()

    layouts = compare(cpp_out[:len(cases)], java_out[:len(cases)], cases)
    cpp_grammar = cpp_out[len(cases):]
    java_grammar = java_out[len(cases):len(cases) + len(grammar)]
    assert cpp_grammar == java_grammar, 'record grammar differs between C++ and Java'
    expected = ['P 1'] * len([r for r in VALID_GRAMMAR]) + ['P 0'] * len([r for r in INVALID_GRAMMAR if '\n' not in r])
    assert cpp_grammar == expected, f'record grammar: {cpp_grammar} vs {expected}'
    canon = java_out[len(cases) + len(grammar):-1]
    assert len(canon) == len(canonical)
    # Canonical records the editor writes must parse in the engine.
    check = subprocess.run([str(binary)], input='\n'.join('P ' + line[2:] for line in canon) + '\n', capture_output=True,
                           text=True, check=True).stdout.splitlines()
    assert check == ['P 1'] * len(canon), 'an encoded record is refused by the engine'
    drops, snapped = map(int, java_out[-1].split()[1:])
    assert drops > 1000 and snapped > drops // 2, (drops, snapped)

    # Every layout the matrix produces keeps the engine's spacing rules.
    rejected = sum(1 for line in cpp_out[:len(cases)] if line.split()[3] == '1')
    edited = sum(1 for line in cpp_out[:len(cases)] if line.split()[2] == '1')
    assert rejected > 0 and edited > rejected, (rejected, edited)
    print(f'PASS: {layouts} C++/Java layouts agree ({edited} edited, {rejected} rejected to the preset), '
          f'{len(grammar)} record grammar cases, {len(canon)} canonical records, {drops} editor drops ({snapped} on the 8 dp grid)')


if __name__ == '__main__':
    main()

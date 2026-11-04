#!/usr/bin/env python3
"""Execute the production Vulkan cache eviction policy against fake resources.

The GPU/source compiler are replaced only at the release boundary. Selection,
frame retirement, hole reuse and byte-budget admission use the actual methods.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

from renderer_display_presentation import method

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <map>
#include <string>
constexpr int MaxPrograms = 256, VK_FRAMES_IN_FLIGHT = 2;
constexpr size_t MaxCompilerBytes = 32 * 1024 * 1024;
struct Program { uint64_t lastUseFrame=0; size_t bytes=0; int identity=0; int pipelines=0; };
std::vector<std::unique_ptr<Program>> programs;
std::map<std::string,int> currentPrograms;
uint64_t frameSerial=100;
size_t compilerBytes=0;
int evictions=0;
int pipelines=0;
bool VK_Exec_ProgramPipelineCapacityExhausted() { return pipelines>=512; }
std::vector<int> released;
bool IsCurrent(int index);
void Release(Program &program) {
    assert(!IsCurrent(program.identity));
    assert(program.lastUseFrame + VK_FRAMES_IN_FLIGHT <= frameSerial);
    assert(compilerBytes >= program.bytes);
    compilerBytes -= program.bytes;
    pipelines -= program.pipelines;
    released.push_back(program.identity);
}
void add(size_t bytes, uint64_t use, bool current) {
    int index=int(programs.size());
    programs.push_back(std::make_unique<Program>(Program{use,bytes,index}));
    compilerBytes += bytes;
    if(current) currentPrograms[std::to_string(index)]=index;
}
void reset() { programs.clear();currentPrograms.clear();released.clear();compilerBytes=0;evictions=0;frameSerial=100;pipelines=0; }
'''
CASES = r'''
int main() {
    assert(AcquireSlot()==0);
    for(int i=0;i<MaxPrograms;++i) add(100,0,true);
    assert(AcquireSlot()==-1 && released.empty()); // live sources cannot be evicted
    currentPrograms.erase("15"); programs[15]->lastUseFrame=99;
    assert(AcquireSlot()==-1); // reload does not retire a recorded draw
    ++frameSerial;
    assert(AcquireSlot()==15 && !programs[15] && evictions==1);
    assert(AcquireSlot()==15 && evictions==1); // reuse the vacant slot
    assert(compilerBytes==25500);
    reset();
    // Byte pressure before the version limit: current and in-flight versions
    // survive, but enough retired stale storage must be reclaimed.
    add(8*1024*1024,0,true);
    add(8*1024*1024,99,false);
    add(8*1024*1024,10,false);
    add(8*1024*1024,20,false);
    assert(ReserveCompilerStorage(12*1024*1024));
    assert((released==std::vector<int>{2,3}));
    assert(programs[0] && programs[1] && compilerBytes==16*1024*1024);
    assert(AcquireSlot()==2);
    assert(!ReserveCompilerStorage(17*1024*1024)); // no unsafe victim
    assert(released.size()==2 && compilerBytes==16*1024*1024);
    ++frameSerial;
    assert(ReserveCompilerStorage(17*1024*1024) && !programs[1]);
    assert(!ReserveCompilerStorage(MaxCompilerBytes+1) && programs[0]);
    assert(ReserveCompilerStorage(MaxCompilerBytes-compilerBytes)); // exact fit
    reset();
    // A full table chooses its oldest eligible version, skipping holes and
    // current/in-flight records rather than treating them as reusable.
    for(int i=0;i<MaxPrograms;++i) add(100,20+i%5,false);
    programs[40]->lastUseFrame=1;
    programs[41]->lastUseFrame=0; currentPrograms["pinned"]=41;
    assert(AcquireSlot()==40 && released.back()==40);
    programs[40]=std::make_unique<Program>(Program{100,200,40});compilerBytes+=200;
    assert(AcquireSlot()==0 && programs[40] && programs[41]);
    reset();
    // Pipeline capacity can fill with far fewer than 256 source versions.
    // Ignore ordinary driver allocation failure; preserve current and in-flight
    // owners, and scan past a retired version that owns no pipelines.
    add(100,0,true); programs[0]->pipelines=504;
    add(100,99,false); programs[1]->pipelines=8;
    add(100,0,false);
    pipelines=511;
    assert(!ReclaimPipelineCapacity() && released.empty());
    pipelines=512;
    assert(!ReclaimPipelineCapacity() && released==std::vector<int>{2});
    assert(programs[0] && programs[1] && pipelines==512);
    ++frameSerial;
    assert(ReclaimPipelineCapacity() && released==std::vector<int>({2,1}));
    assert(programs[0] && !programs[1] && pipelines==504 && compilerBytes==100);
    assert(!ReclaimPipelineCapacity() && evictions==2);
    reset();
    // Exhausting all 256 stale, pipeline-free records terminates without
    // claiming that native or otherwise unowned pipeline slots were reclaimed.
    for(int i=0;i<MaxPrograms;++i) add(100,0,false);
    pipelines=512;
    assert(!ReclaimPipelineCapacity() && evictions==MaxPrograms && pipelines==512);
}
'''


def main():
    production = (ROOT / 'src/renderer/Vulkan/vk_MaterialPrograms.cpp').read_text()
    source = SUPPORT + ''.join(method(production, signature) for signature in (
        'bool IsCurrent( int index )', 'int EvictionCandidate()', 'void Evict( int slot )',
        'bool ReclaimPipelineCapacity()', 'int AcquireSlot()', 'bool ReserveCompilerStorage( size_t bytes )')) + CASES
    compiler = next((path for name in ('clang++', 'g++', 'c++') if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError('a C++ compiler is required')
    scratch = ROOT / '.tmp'
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='material-cache-', dir=scratch) as directory:
        root = Path(directory)
        cpp, exe = root / 'cache.cpp', root / ('cache.exe' if os.name == 'nt' else 'cache')
        cpp.write_text(source)
        subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print('Vulkan material cache: production version/storage/pipeline eviction, in-flight protection and slot reuse passed')


if __name__ == '__main__':
    main()

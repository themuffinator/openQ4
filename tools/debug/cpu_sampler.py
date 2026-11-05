#!/usr/bin/env python3
"""Sample the CPU stacks of a running openQ4 client on Windows, without admin rights.

ETW sampling (xperf/WPR) needs an elevated shell. This profiler instead
suspends the busiest thread(s) of a process for a few hundred microseconds,
copies the register context and the live stack, resumes the thread, and walks
the copy offline with dbghelp's StackWalk64. Symbols come from the PDBs that
staging places beside the executable and game modules.

    python tools/debug/cpu_sampler.py record <pid> <seconds> <out.json> [--hz 150]
    python tools/debug/cpu_sampler.py report <out.json> [--top 40]
    python tools/debug/cpu_sampler.py tree <out.json> <function> [--depth 3] [--min 0.5]
    python tools/debug/cpu_sampler.py frames <out.json> <frame-work.csv> [--threshold 6.67]

`frames` finds what long frames do differently: run `profileFrameWork <n>` in
the client while `record` samples it, then pass the CSV it writes. Each sample
carries a QueryPerformanceCounter time, the clock behind the CSV's startMs, so
main-thread samples land in the frame that was running.

Keep the target alive until `record` finishes: it walks the copied stacks
after sampling and still reads code and unwind data from the process. Copying
the stack keeps each suspension short; walking a live stack takes about 20 ms,
and the engine answers a stall that long by running catch-up game tics, which
skews the profile toward the game. Sample intervals are jittered so they do not
alias with the 60 Hz tic. Driver code has no public symbols; time inside the
graphics driver is attributed to its nearest export (for example
vkGetInstanceProcAddr).
"""
import argparse
import bisect
import collections
import csv
import ctypes
import json
import random
import sys
import time

CONTEXT_SIZE = 1232          # x64 CONTEXT
CONTEXT_FLAGS_OFFSET = 0x30
CONTEXT_RSP_OFFSET = 0x98
CONTEXT_RBP_OFFSET = 0xA0
CONTEXT_RIP_OFFSET = 0xF8
CONTEXT_AMD64_CONTROL_INTEGER = 0x100003
IMAGE_FILE_MACHINE_AMD64 = 0x8664
ADDR_MODE_FLAT = 3


def record(pid: int, seconds: float, out: str, hz: float) -> None:
    import ctypes.wintypes as wt
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    dbg = ctypes.WinDLL("dbghelp", use_last_error=True)

    class THREADENTRY32(ctypes.Structure):
        _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                    ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG),
                    ("dwFlags", wt.DWORD)]

    class ADDRESS64(ctypes.Structure):
        _fields_ = [("Offset", ctypes.c_uint64), ("Segment", wt.WORD), ("Mode", ctypes.c_int)]

    class KDHELP64(ctypes.Structure):
        _fields_ = [("Thread", ctypes.c_uint64), ("ThCallbackStack", wt.DWORD), ("ThCallbackBStore", wt.DWORD),
                    ("NextCallback", wt.DWORD), ("FramePointer", wt.DWORD), ("KiCallUserMode", ctypes.c_uint64),
                    ("KeUserCallbackDispatcher", ctypes.c_uint64), ("SystemRangeStart", ctypes.c_uint64),
                    ("KiUserExceptionDispatcher", ctypes.c_uint64), ("StackBase", ctypes.c_uint64),
                    ("StackLimit", ctypes.c_uint64), ("BuildVersion", wt.DWORD),
                    ("RetpolineStubFunctionTableSize", wt.DWORD), ("RetpolineStubFunctionTable", ctypes.c_uint64),
                    ("RetpolineStubOffset", wt.DWORD), ("RetpolineStubSize", wt.DWORD),
                    ("Reserved0", ctypes.c_uint64 * 2)]

    class STACKFRAME64(ctypes.Structure):
        _fields_ = [("AddrPC", ADDRESS64), ("AddrReturn", ADDRESS64), ("AddrFrame", ADDRESS64),
                    ("AddrStack", ADDRESS64), ("AddrBStore", ADDRESS64), ("FuncTableEntry", ctypes.c_void_p),
                    ("Params", ctypes.c_uint64 * 4), ("Far", wt.BOOL), ("Virtual", wt.BOOL),
                    ("Reserved", ctypes.c_uint64 * 3), ("KdHelp", KDHELP64)]

    class SYMBOL_INFO(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                    ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
                    ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                    ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
                    ("Name", ctypes.c_char * 1024)]

    class MEMORY_BASIC_INFORMATION(ctypes.Structure):
        _fields_ = [("BaseAddress", ctypes.c_uint64), ("AllocationBase", ctypes.c_uint64),
                    ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD), ("RegionSize", ctypes.c_uint64),
                    ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]

    k32.OpenProcess.restype = wt.HANDLE
    k32.OpenThread.restype = wt.HANDLE
    k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
    k32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
    k32.SuspendThread.argtypes = [wt.HANDLE]
    k32.ResumeThread.argtypes = [wt.HANDLE]
    k32.GetThreadTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(ctypes.c_uint64)] * 4
    k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(MEMORY_BASIC_INFORMATION), ctypes.c_size_t]
    k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t,
                                      ctypes.POINTER(ctypes.c_size_t)]
    dbg.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]
    dbg.SymSetOptions.argtypes = [wt.DWORD]
    dbg.SymRefreshModuleList.argtypes = [wt.HANDLE]
    dbg.SymFunctionTableAccess64.restype = ctypes.c_void_p
    dbg.SymFunctionTableAccess64.argtypes = [wt.HANDLE, ctypes.c_uint64]
    dbg.SymGetModuleBase64.restype = ctypes.c_uint64
    dbg.SymGetModuleBase64.argtypes = [wt.HANDLE, ctypes.c_uint64]
    dbg.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]
    dbg.StackWalk64.argtypes = [wt.DWORD, wt.HANDLE, wt.HANDLE, ctypes.POINTER(STACKFRAME64), ctypes.c_void_p,
                                ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    FunctionTableAccess = ctypes.WINFUNCTYPE(ctypes.c_void_p, wt.HANDLE, ctypes.c_uint64)
    GetModuleBase = ctypes.WINFUNCTYPE(ctypes.c_uint64, wt.HANDLE, ctypes.c_uint64)
    ReadMemory = ctypes.WINFUNCTYPE(wt.BOOL, wt.HANDLE, ctypes.c_uint64, ctypes.c_void_p, wt.DWORD,
                                    ctypes.POINTER(wt.DWORD))

    process = k32.OpenProcess(0x0400 | 0x0010, False, pid)  # QUERY_INFORMATION | VM_READ
    if not process:
        raise SystemExit(f"OpenProcess({pid}) failed: {ctypes.get_last_error()}")
    dbg.SymSetOptions(0x2 | 0x4)  # SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS
    if not dbg.SymInitialize(process, None, True):
        raise SystemExit(f"SymInitialize failed: {ctypes.get_last_error()}")

    snapshot = k32.CreateToolhelp32Snapshot(0x4, 0)  # TH32CS_SNAPTHREAD
    entry = THREADENTRY32()
    entry.dwSize = ctypes.sizeof(entry)
    thread_ids = []
    ok = k32.Thread32First(snapshot, ctypes.byref(entry))
    while ok:
        if entry.th32OwnerProcessID == pid:
            thread_ids.append(entry.th32ThreadID)
        ok = k32.Thread32Next(snapshot, ctypes.byref(entry))
    k32.CloseHandle(snapshot)
    # SUSPEND_RESUME | GET_CONTEXT | QUERY_INFORMATION
    threads = {tid: k32.OpenThread(0x0002 | 0x0008 | 0x0040, False, tid) for tid in thread_ids}
    threads = {tid: handle for tid, handle in threads.items() if handle}

    def cpu_time(handle) -> int:
        times = [ctypes.c_uint64() for _ in range(4)]
        k32.GetThreadTimes(handle, *(ctypes.byref(t) for t in times))
        return times[2].value + times[3].value

    before = {tid: cpu_time(handle) for tid, handle in threads.items()}
    time.sleep(1.0)
    busy = sorted(((cpu_time(handle) - before[tid]) / 1e7, tid) for tid, handle in threads.items())[::-1]
    targets = [tid for share, tid in busy[:4] if share > 0.15]
    print("busiest threads (CPU seconds per second):", [(round(s, 3), t) for s, t in busy[:6]], flush=True)
    if not targets:
        raise SystemExit("no thread used more than 15% of a core")

    # Sample times share the engine's presentation clock (QueryPerformanceCounter),
    # so `frames` can place each sample in a profileFrameWork frame.
    qpc, qpc_frequency = ctypes.c_int64(), ctypes.c_int64()
    k32.QueryPerformanceFrequency(ctypes.byref(qpc_frequency))

    def qpc_ms() -> float:
        k32.QueryPerformanceCounter(ctypes.byref(qpc))
        return qpc.value * 1000.0 / qpc_frequency.value

    context_buffer = ctypes.create_string_buffer(CONTEXT_SIZE + 16)
    context = (ctypes.addressof(context_buffer) + 15) & ~15
    stack_buffer = ctypes.create_string_buffer(8 * 1024 * 1024)
    samples = []
    suspended = 0.0
    start = time.perf_counter()
    while time.perf_counter() - start < seconds:
        for tid in targets:
            handle = threads[tid]
            began = time.perf_counter()
            if k32.SuspendThread(handle) == 0xFFFFFFFF:
                continue
            taken = qpc_ms()
            try:
                ctypes.memset(context, 0, CONTEXT_SIZE)
                ctypes.c_uint32.from_address(context + CONTEXT_FLAGS_OFFSET).value = CONTEXT_AMD64_CONTROL_INTEGER
                if not k32.GetThreadContext(handle, ctypes.c_void_p(context)):
                    continue
                rsp = ctypes.c_uint64.from_address(context + CONTEXT_RSP_OFFSET).value
                region = MEMORY_BASIC_INFORMATION()
                k32.VirtualQueryEx(process, rsp, ctypes.byref(region), ctypes.sizeof(region))
                size = min(region.BaseAddress + region.RegionSize - rsp, len(stack_buffer))
                read = ctypes.c_size_t()
                k32.ReadProcessMemory(process, rsp, stack_buffer, size, ctypes.byref(read))
                samples.append((tid, ctypes.string_at(context, CONTEXT_SIZE), rsp,
                                ctypes.string_at(stack_buffer, read.value), taken))
            finally:
                k32.ResumeThread(handle)
                suspended += time.perf_counter() - began
        time.sleep(random.uniform(0.2, 1.8) / hz)
    sampled = time.perf_counter() - start
    print(f"{len(samples)} samples in {sampled:.1f} s, {1e6 * suspended / max(1, len(samples)):.0f} us suspended each",
          flush=True)

    current = {}

    def read_memory(_process, address, buffer, size, bytes_read):
        low, data = current["rsp"], current["stack"]
        if low <= address and address + size <= low + len(data):
            ctypes.memmove(buffer, data[address - low:address - low + size], size)
            if bytes_read:
                bytes_read[0] = size
            return True
        got = ctypes.c_size_t()
        ok = k32.ReadProcessMemory(process, address, buffer, size, ctypes.byref(got))
        if bytes_read:
            bytes_read[0] = got.value
        return bool(ok)

    read_callback = ReadMemory(read_memory)
    table_callback = FunctionTableAccess(dbg.SymFunctionTableAccess64)
    base_callback = GetModuleBase(dbg.SymGetModuleBase64)
    stacks = {tid: collections.Counter() for tid in targets}
    walked = []  # (tid, time, address tuple) per sample, in sampling order
    for tid, context_bytes, rsp, stack, taken in samples:
        current["rsp"], current["stack"] = rsp, stack
        ctypes.memmove(context, context_bytes, CONTEXT_SIZE)
        frame = STACKFRAME64()
        frame.AddrPC.Offset = ctypes.c_uint64.from_address(context + CONTEXT_RIP_OFFSET).value
        frame.AddrStack.Offset = rsp
        frame.AddrFrame.Offset = ctypes.c_uint64.from_address(context + CONTEXT_RBP_OFFSET).value
        frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = ADDR_MODE_FLAT
        addresses = []
        for _ in range(64):
            if not dbg.StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, threads[tid], ctypes.byref(frame),
                                   ctypes.c_void_p(context), read_callback, table_callback, base_callback, None):
                break
            if frame.AddrPC.Offset == 0:
                break
            addresses.append(frame.AddrPC.Offset)
        stacks[tid][tuple(addresses)] += 1
        walked.append((tid, taken, tuple(addresses)))

    dbg.SymRefreshModuleList(process)
    symbol = SYMBOL_INFO()
    symbol.SizeOfStruct = 88
    symbol.MaxNameLen = 1000
    names = {}

    def name_of(address: int) -> str:
        if address not in names:
            displacement = ctypes.c_uint64()
            if dbg.SymFromAddr(process, address, ctypes.byref(displacement), ctypes.byref(symbol)):
                names[address] = symbol.Name.decode(errors="replace")
            else:
                names[address] = f"0x{address:x}"
        return names[address]

    result = {"pid": pid, "seconds": sampled, "samples": len(samples), "threads": {}}
    for tid, counter in stacks.items():
        named = collections.Counter()
        for addresses, count in counter.items():
            named[" <- ".join(name_of(a) for a in addresses)] += count
        result["threads"][str(tid)] = [[count, stack] for stack, count in named.most_common()]
    # Per-sample timeline: [thread, QPC milliseconds, index into stackTable].
    table, index = [], {}
    result["timeline"] = []
    for tid, taken, addresses in walked:
        text = " <- ".join(name_of(a) for a in addresses)
        if text not in index:
            index[text] = len(table)
            table.append(text)
        result["timeline"].append([tid, round(taken, 4), index[text]])
    result["stackTable"] = table
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=0)


def load(path: str) -> dict:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def report(path: str, top: int) -> None:
    data = load(path)
    for tid, rows in data["threads"].items():
        total = sum(count for count, _ in rows)
        own, inclusive = collections.Counter(), collections.Counter()
        for count, stack in rows:
            frames = stack.split(" <- ") if stack else ["?"]
            own[frames[0]] += count
            for frame in set(frames):
                inclusive[frame] += count
        print(f"== thread {tid}: {total} samples over {data['seconds']:.1f} s")
        for title, counter in (("self", own), ("inclusive", inclusive)):
            print(f"-- {title}")
            for frame, count in counter.most_common(top):
                print(f"{100 * count / total:6.2f}% {frame[:150]}")


def tree(path: str, function: str, depth: int, minimum: float) -> None:
    data = load(path)
    for tid, rows in data["threads"].items():
        total = sum(count for count, _ in rows)
        paths = collections.Counter()
        for count, stack in rows:
            frames = stack.split(" <- ")[::-1]
            if function not in frames:
                continue
            index = frames.index(function)
            chain = tuple(frames[index:index + 1 + depth])
            for length in range(1, len(chain) + 1):
                paths[chain[:length]] += count
        print(f"== thread {tid}")
        for chain, count in sorted(paths.items()):
            if 100 * count / total >= minimum:
                print(f"{100 * count / total:6.2f}% {'  ' * (len(chain) - 1)}{chain[-1][:110]}")


def frames(path: str, csv_path: str, threshold: float, top: int) -> None:
    """Attribute main-thread samples to profileFrameWork frames and compare long frames with the rest.

    The CSV's startMs is when each frame's Wait phase began on the presentation clock; a frame runs
    for its totalMs minus the gap before it. Samples outside every recorded frame are ignored.
    """
    data = load(path)
    if "timeline" not in data:
        raise SystemExit(f"{path} has no per-sample timeline; record it with this version")
    with open(csv_path, encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows or "startMs" not in rows[0]:
        raise SystemExit(f"{csv_path} has no startMs column; capture it with a current engine")
    spans = sorted((float(r["startMs"]), float(r["startMs"]) + float(r["totalMs"]) - float(r["gapMs"]),
                    float(r["totalMs"]), int(r["frame"]), r) for r in rows)
    starts = [span[0] for span in spans]
    counts = collections.Counter(tid for tid, _, _ in data["timeline"])
    main_tid = counts.most_common(1)[0][0]
    table = data["stackTable"]
    long_frames, groups = collections.defaultdict(list), {"long": collections.Counter(), "other": collections.Counter()}
    totals = {"long": 0, "other": 0}
    for tid, taken, stack in data["timeline"]:
        if tid != main_tid:
            continue
        at = bisect.bisect_right(starts, taken) - 1
        if at < 0 or taken >= spans[at][1]:
            continue
        group = "long" if spans[at][2] > threshold else "other"
        totals[group] += 1
        names = table[stack].split(" <- ") if table[stack] else ["?"]
        for name in set(names):
            groups[group][name] += 1
        if group == "long":
            long_frames[at].append(names)
    long_count = sum(1 for span in spans if span[2] > threshold)
    print(f"thread {main_tid}: {len(spans)} frames, {long_count} over {threshold} ms; "
          f"samples in long frames {totals['long']}, in other frames {totals['other']}")
    if not totals["long"]:
        return
    print("-- inclusive share in long frames vs other frames (largest excess first)")
    excess = []
    for name, count in groups["long"].items():
        share = count / totals["long"]
        other = groups["other"][name] / max(1, totals["other"])
        excess.append((share - other, share, other, name))
    for delta, share, other, name in sorted(excess, reverse=True)[:top]:
        print(f"{100 * share:6.2f}% long {100 * other:6.2f}% other  {name[:120]}")
    print("-- longest sampled frames (frame, total ms, session/draw ms): innermost game/engine frames of each sample")
    for at in sorted(long_frames, key=lambda i: -spans[i][2])[:12]:
        row = spans[at][4]
        print(f"frame {spans[at][3]} {spans[at][2]:.2f} ms (session {float(row['sessionMs']):.2f}, draw {float(row['drawMs']):.2f})")
        for names in long_frames[at][:6]:
            print("    " + " <- ".join(names[:6])[:200])


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    sample = commands.add_parser("record", help="sample a running process")
    sample.add_argument("pid", type=int)
    sample.add_argument("seconds", type=float)
    sample.add_argument("out")
    sample.add_argument("--hz", type=float, default=150.0)
    summary = commands.add_parser("report", help="self and inclusive time per function")
    summary.add_argument("json")
    summary.add_argument("--top", type=int, default=40)
    callees = commands.add_parser("tree", help="callee breakdown under one function")
    callees.add_argument("json")
    callees.add_argument("function")
    callees.add_argument("--depth", type=int, default=3)
    callees.add_argument("--min", type=float, default=0.5)
    per_frame = commands.add_parser("frames", help="attribute samples to profileFrameWork frames; compare long frames")
    per_frame.add_argument("json")
    per_frame.add_argument("csv", help="diagnostics/frame-work.csv from the same run (needs its startMs column)")
    per_frame.add_argument("--threshold", type=float, default=6.67, help="a frame longer than this many ms is long")
    per_frame.add_argument("--top", type=int, default=30)
    args = parser.parse_args(argv)
    if args.command == "record":
        if sys.platform != "win32":
            parser.error("record needs Windows (kernel32/dbghelp)")
        record(args.pid, args.seconds, args.out, args.hz)
    elif args.command == "report":
        report(args.json, args.top)
    elif args.command == "frames":
        frames(args.json, args.csv, args.threshold, args.top)
    else:
        tree(args.json, args.function, args.depth, args.min)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

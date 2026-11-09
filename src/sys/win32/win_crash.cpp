/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#include "win_local.h"
#include "win_crash.h"

#include <dbghelp.h>
#include <exception>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(
	HANDLE hProcess,
	DWORD ProcessId,
	HANDLE hFile,
	MINIDUMP_TYPE DumpType,
	PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	PMINIDUMP_CALLBACK_INFORMATION CallbackParam
);

static volatile LONG g_crashHandlerEntered = 0;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevExceptionFilter = NULL;

static const DWORD OPENQ4_EXCEPTION_INVALID_PARAMETER = 0xE0000001;
static const DWORD OPENQ4_EXCEPTION_PURECALL = 0xE0000002;
static const DWORD OPENQ4_EXCEPTION_ABORT = 0xE0000003;
static const DWORD OPENQ4_EXCEPTION_TERMINATE = 0xE0000004;

/*
====================
Sys_StringPrintf
====================
*/
static bool Sys_StringPrintf(char* outBuffer, size_t outBufferSize, const char* fmt, ...) {
	if (!outBuffer || outBufferSize == 0 || !fmt) {
		return false;
	}

	va_list args;
	va_start(args, fmt);
	const int written = _vsnprintf_s(outBuffer, outBufferSize, _TRUNCATE, fmt, args);
	va_end(args);

	return written >= 0;
}

/*
====================
Sys_StringAppendf
====================
*/
static bool Sys_StringAppendf(char* outBuffer, size_t outBufferSize, const char* fmt, ...) {
	if (!outBuffer || outBufferSize == 0 || !fmt) {
		return false;
	}

	const size_t curLen = strlen(outBuffer);
	if (curLen >= outBufferSize) {
		return false;
	}

	va_list args;
	va_start(args, fmt);
	const int written = _vsnprintf_s(outBuffer + curLen, outBufferSize - curLen, _TRUNCATE, fmt, args);
	va_end(args);

	return written >= 0;
}

/*
====================
Sys_GetExceptionCodeName
====================
*/
static const char* Sys_GetExceptionCodeName(DWORD code) {
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT";
	case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
	case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
	case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
	case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
	case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
	case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP";
	case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
	case OPENQ4_EXCEPTION_INVALID_PARAMETER: return "OPENQ4_INVALID_PARAMETER";
	case OPENQ4_EXCEPTION_PURECALL: return "OPENQ4_PURECALL";
	case OPENQ4_EXCEPTION_ABORT: return "OPENQ4_ABORT";
	case OPENQ4_EXCEPTION_TERMINATE: return "OPENQ4_TERMINATE";
	default: return "EXCEPTION_UNKNOWN";
	}
}

/*
====================
Sys_RaiseSyntheticException
====================
*/
static void Sys_RaiseSyntheticException(DWORD exceptionCode) {
	RaiseException(exceptionCode, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

/*
====================
Sys_InvalidParameterHandler
====================
*/
static void __cdecl Sys_InvalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
	Sys_RaiseSyntheticException(OPENQ4_EXCEPTION_INVALID_PARAMETER);
}

/*
====================
Sys_PurecallHandler
====================
*/
static void __cdecl Sys_PurecallHandler(void) {
	Sys_RaiseSyntheticException(OPENQ4_EXCEPTION_PURECALL);
}

/*
====================
Sys_AbortSignalHandler
====================
*/
static void __cdecl Sys_AbortSignalHandler(int) {
	Sys_RaiseSyntheticException(OPENQ4_EXCEPTION_ABORT);
}

/*
====================
Sys_TerminateHandler
====================
*/
static void __cdecl Sys_TerminateHandler(void) {
	Sys_RaiseSyntheticException(OPENQ4_EXCEPTION_TERMINATE);
}

/*
====================
Sys_GetExecutableDir
====================
*/
static bool Sys_GetExecutableDir(char* outDir, size_t outDirSize) {
	if (!outDir || outDirSize < 4) {
		return false;
	}

	DWORD len = GetModuleFileNameA(NULL, outDir, (DWORD)outDirSize);
	if (len == 0 || len >= outDirSize) {
		return false;
	}

	for (size_t i = len; i > 0; i--) {
		if (outDir[i - 1] == '\\' || outDir[i - 1] == '/') {
			outDir[i - 1] = '\0';
			return true;
		}
	}

	return false;
}

/*
====================
Sys_GetAddressModuleInfo

Resolve an instruction address without loading a new module or taking a
reference on an existing one. This stays deliberately small and loader-safe so
the crash path can identify driver and injected-hook faults even when symbol
files are unavailable.
====================
*/
static bool Sys_GetAddressModuleInfo(
	const void* address,
	char* outModulePath,
	size_t outModulePathSize,
	uintptr_t& outModuleBase,
	uintptr_t& outModuleOffset
) {
	outModuleBase = 0;
	outModuleOffset = 0;
	if (!address || !outModulePath || outModulePathSize < 2) {
		return false;
	}

	outModulePath[0] = '\0';
	MEMORY_BASIC_INFORMATION memoryInfo;
	memset(&memoryInfo, 0, sizeof(memoryInfo));
	if (VirtualQuery(address, &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo) || !memoryInfo.AllocationBase) {
		return false;
	}

	const HMODULE module = (HMODULE)memoryInfo.AllocationBase;
	const DWORD pathLength = GetModuleFileNameA(module, outModulePath, (DWORD)outModulePathSize);
	if (pathLength == 0 || pathLength >= outModulePathSize) {
		outModulePath[0] = '\0';
		return false;
	}

	outModuleBase = (uintptr_t)module;
	const uintptr_t instruction = (uintptr_t)address;
	outModuleOffset = instruction >= outModuleBase ? instruction - outModuleBase : 0;
	return true;
}

/*
====================
Sys_GetCrashBaseName
====================
*/
static bool Sys_GetCrashBaseName(char* outBaseName, size_t outBaseNameSize) {
	if (!outBaseName || outBaseNameSize == 0) {
		return false;
	}

	SYSTEMTIME localTime;
	GetLocalTime(&localTime);

	return Sys_StringPrintf(
		outBaseName,
		outBaseNameSize,
		"openq4_crash_%04u%02u%02u_%02u%02u%02u_%lu_%lu",
		(unsigned)localTime.wYear,
		(unsigned)localTime.wMonth,
		(unsigned)localTime.wDay,
		(unsigned)localTime.wHour,
		(unsigned)localTime.wMinute,
		(unsigned)localTime.wSecond,
		(unsigned long)GetCurrentProcessId(),
		(unsigned long)GetCurrentThreadId()
	);
}

/*
====================
Sys_BuildCrashPaths
====================
*/
static bool Sys_BuildCrashPaths(char* outLogPath, size_t outLogPathSize, char* outDumpPath, size_t outDumpPathSize) {
	char exeDir[MAX_PATH];
	char baseName[128];
	char crashDir[MAX_PATH];

	if (!Sys_GetExecutableDir(exeDir, sizeof(exeDir))) {
		return false;
	}

	if (!Sys_GetCrashBaseName(baseName, sizeof(baseName))) {
		return false;
	}

	if (!Sys_StringPrintf(crashDir, sizeof(crashDir), "%s\\crashes", exeDir)) {
		return false;
	}

	CreateDirectoryA(crashDir, NULL);

	if (!Sys_StringPrintf(outLogPath, outLogPathSize, "%s\\%s.log", crashDir, baseName)) {
		return false;
	}

	if (!Sys_StringPrintf(outDumpPath, outDumpPathSize, "%s\\%s.dmp", crashDir, baseName)) {
		return false;
	}

	return true;
}

/*
====================
Sys_WriteMinidump
====================
*/
static bool Sys_WriteMinidump(const char* dumpPath, LPEXCEPTION_POINTERS exceptionInfo, DWORD& outError) {
	outError = ERROR_SUCCESS;

	HMODULE dbghelpModule = LoadLibraryA("dbghelp.dll");
	if (!dbghelpModule) {
		outError = GetLastError();
		return false;
	}

	MiniDumpWriteDumpFn pMiniDumpWriteDump = (MiniDumpWriteDumpFn)GetProcAddress(dbghelpModule, "MiniDumpWriteDump");
	if (!pMiniDumpWriteDump) {
		outError = GetLastError();
		FreeLibrary(dbghelpModule);
		return false;
	}

	HANDLE dumpFile = CreateFileA(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (dumpFile == INVALID_HANDLE_VALUE) {
		outError = GetLastError();
		FreeLibrary(dbghelpModule);
		return false;
	}

	MINIDUMP_EXCEPTION_INFORMATION dumpExceptionInfo;
	dumpExceptionInfo.ThreadId = GetCurrentThreadId();
	dumpExceptionInfo.ExceptionPointers = exceptionInfo;
	dumpExceptionInfo.ClientPointers = FALSE;

	const MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(
		MiniDumpNormal |
		MiniDumpWithDataSegs |
		MiniDumpWithThreadInfo |
		MiniDumpWithUnloadedModules |
		MiniDumpWithIndirectlyReferencedMemory
	);

	BOOL result = pMiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		dumpFile,
		dumpType,
		(exceptionInfo != NULL) ? &dumpExceptionInfo : NULL,
		NULL,
		NULL
	);

	if (!result) {
		outError = GetLastError();
	}

	CloseHandle(dumpFile);
	FreeLibrary(dbghelpModule);

	return result == TRUE;
}

/*
====================
Sys_WriteCrashLog
====================
*/
static void Sys_WriteCrashLog(
	const char* logPath,
	const char* dumpPath,
	bool dumpCreated,
	DWORD dumpError,
	LPEXCEPTION_POINTERS exceptionInfo
) {
	HANDLE logFile = CreateFileA(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (logFile == INVALID_HANDLE_VALUE) {
		return;
	}

	SYSTEMTIME localTime;
	GetLocalTime(&localTime);

	char logBuffer[8192];
	logBuffer[0] = '\0';

	EXCEPTION_RECORD* er = (exceptionInfo != NULL) ? exceptionInfo->ExceptionRecord : NULL;
	CONTEXT* ctx = (exceptionInfo != NULL) ? exceptionInfo->ContextRecord : NULL;

	DWORD exceptionCode = er ? er->ExceptionCode : 0;
	const void* exceptionAddress = er ? er->ExceptionAddress : NULL;
	char exceptionModulePath[MAX_PATH];
	uintptr_t exceptionModuleBase = 0;
	uintptr_t exceptionModuleOffset = 0;
	const bool exceptionModuleResolved = Sys_GetAddressModuleInfo(
		exceptionAddress,
		exceptionModulePath,
		ARRAYSIZE(exceptionModulePath),
		exceptionModuleBase,
		exceptionModuleOffset);

	Sys_StringPrintf(
		logBuffer,
		ARRAYSIZE(logBuffer),
		"openQ4 crash report\r\n"
		"Timestamp: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n"
		"ProcessId: %lu\r\n"
		"ThreadId: %lu\r\n"
		"CommandLine: %s\r\n"
		"\r\n"
		"ExceptionCode: 0x%08lX (%s)\r\n"
		"ExceptionAddress: 0x%p\r\n"
		"DumpCreated: %s\r\n"
		"DumpPath: %s\r\n"
		"DumpError: 0x%08lX\r\n",
		(unsigned)localTime.wYear,
		(unsigned)localTime.wMonth,
		(unsigned)localTime.wDay,
		(unsigned)localTime.wHour,
		(unsigned)localTime.wMinute,
		(unsigned)localTime.wSecond,
		(unsigned)localTime.wMilliseconds,
		(unsigned long)GetCurrentProcessId(),
		(unsigned long)GetCurrentThreadId(),
		GetCommandLineA(),
		(unsigned long)exceptionCode,
		Sys_GetExceptionCodeName(exceptionCode),
		exceptionAddress,
		dumpCreated ? "yes" : "no",
		dumpPath,
		(unsigned long)dumpError
	);

	if (exceptionModuleResolved) {
		Sys_StringAppendf(
			logBuffer,
			ARRAYSIZE(logBuffer),
			"ExceptionModule: %s\r\n"
			"ExceptionModuleBase: 0x%p\r\n"
			"ExceptionModuleOffset: 0x%llX\r\n",
			exceptionModulePath,
			(void*)exceptionModuleBase,
			(unsigned long long)exceptionModuleOffset
		);
	}

	if (er && er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
		const char* accessType = "unknown";
		if (er->ExceptionInformation[0] == 0) {
			accessType = "read";
		}
		else if (er->ExceptionInformation[0] == 1) {
			accessType = "write";
		}
		else if (er->ExceptionInformation[0] == 8) {
			accessType = "execute";
		}

		Sys_StringAppendf(
			logBuffer,
			ARRAYSIZE(logBuffer),
			"AccessViolationType: %s\r\nAccessViolationAddress: 0x%p\r\n",
			accessType,
			(void*)er->ExceptionInformation[1]
		);
	}

	if (ctx) {
#if defined(_M_X64)
		Sys_StringAppendf(
			logBuffer,
			ARRAYSIZE(logBuffer),
			"\r\n"
			"RAX=0x%016llX RBX=0x%016llX RCX=0x%016llX RDX=0x%016llX\r\n"
			"RSI=0x%016llX RDI=0x%016llX RBP=0x%016llX RSP=0x%016llX\r\n"
			"R8 =0x%016llX R9 =0x%016llX R10=0x%016llX R11=0x%016llX\r\n"
			"R12=0x%016llX R13=0x%016llX R14=0x%016llX R15=0x%016llX\r\n"
			"RIP=0x%016llX EFlags=0x%08lX\r\n",
			(unsigned long long)ctx->Rax, (unsigned long long)ctx->Rbx, (unsigned long long)ctx->Rcx, (unsigned long long)ctx->Rdx,
			(unsigned long long)ctx->Rsi, (unsigned long long)ctx->Rdi, (unsigned long long)ctx->Rbp, (unsigned long long)ctx->Rsp,
			(unsigned long long)ctx->R8, (unsigned long long)ctx->R9, (unsigned long long)ctx->R10, (unsigned long long)ctx->R11,
			(unsigned long long)ctx->R12, (unsigned long long)ctx->R13, (unsigned long long)ctx->R14, (unsigned long long)ctx->R15,
			(unsigned long long)ctx->Rip, (unsigned long)ctx->EFlags
		);
#elif defined(_M_IX86)
		Sys_StringAppendf(
			logBuffer,
			ARRAYSIZE(logBuffer),
			"\r\n"
			"EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX\r\n"
			"ESI=0x%08lX EDI=0x%08lX EBP=0x%08lX ESP=0x%08lX\r\n"
			"EIP=0x%08lX EFlags=0x%08lX\r\n",
			(unsigned long)ctx->Eax, (unsigned long)ctx->Ebx, (unsigned long)ctx->Ecx, (unsigned long)ctx->Edx,
			(unsigned long)ctx->Esi, (unsigned long)ctx->Edi, (unsigned long)ctx->Ebp, (unsigned long)ctx->Esp,
			(unsigned long)ctx->Eip, (unsigned long)ctx->EFlags
		);
#elif defined(_M_ARM64)
		Sys_StringAppendf(
			logBuffer,
			ARRAYSIZE(logBuffer),
			"\r\n"
			"PC =0x%016llX SP =0x%016llX FP =0x%016llX LR =0x%016llX\r\n"
			"Cpsr=0x%08lX Fpcr=0x%08lX Fpsr=0x%08lX\r\n",
			(unsigned long long)ctx->Pc, (unsigned long long)ctx->Sp,
			(unsigned long long)ctx->Fp, (unsigned long long)ctx->Lr,
			(unsigned long)ctx->Cpsr, (unsigned long)ctx->Fpcr, (unsigned long)ctx->Fpsr
		);
		// X0-X28. X29/X30 are reported above as FP/LR.
		for (int reg = 0; reg < 29; reg++) {
			Sys_StringAppendf(
				logBuffer,
				ARRAYSIZE(logBuffer),
				"X%-2d=0x%016llX%s",
				reg,
				(unsigned long long)ctx->X[reg],
				((reg % 4) == 3 || reg == 28) ? "\r\n" : " "
			);
		}
#endif
	}

	DWORD bytesWritten = 0;
	WriteFile(logFile, logBuffer, (DWORD)strlen(logBuffer), &bytesWritten, NULL);
	CloseHandle(logFile);
}

/*
====================
Sys_UnhandledExceptionFilter
====================
*/
static LONG WINAPI Sys_UnhandledExceptionFilter(LPEXCEPTION_POINTERS exceptionInfo) {
	if (g_prevExceptionFilter) {
		const LONG prevAction = g_prevExceptionFilter(exceptionInfo);
		if (prevAction != EXCEPTION_CONTINUE_SEARCH) {
			return prevAction;
		}
	}

	if (IsDebuggerPresent()) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if (InterlockedCompareExchange(&g_crashHandlerEntered, 1, 0) != 0) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	char logPath[MAX_PATH];
	char dumpPath[MAX_PATH];
	if (!Sys_BuildCrashPaths(logPath, sizeof(logPath), dumpPath, sizeof(dumpPath))) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	DWORD dumpError = ERROR_SUCCESS;
	const bool dumpCreated = Sys_WriteMinidump(dumpPath, exceptionInfo, dumpError);
	Sys_WriteCrashLog(logPath, dumpPath, dumpCreated, dumpError, exceptionInfo);

	char message[1024];
	if (dumpCreated) {
		Sys_StringPrintf(
			message,
			ARRAYSIZE(message),
			"openQ4 encountered an unhandled exception.\n\n"
			"Crash log:\n%s\n\n"
			"Crash dump:\n%s\n\n"
			"Please attach both files when reporting this issue.",
			logPath,
			dumpPath
		);
	}
	else {
		Sys_StringPrintf(
			message,
			ARRAYSIZE(message),
			"openQ4 encountered an unhandled exception.\n\n"
			"Crash log:\n%s\n\n"
			"Failed to create crash dump (error 0x%08lX).",
			logPath,
			(unsigned long)dumpError
		);
	}

	MessageBoxA(NULL, message, "openQ4 Crash", MB_ICONERROR | MB_OK | MB_SYSTEMMODAL);

	return EXCEPTION_EXECUTE_HANDLER;
}

/*
====================
Sys_InstallCrashHandler
====================
*/
void Sys_InstallCrashHandler(void) {
	// A stack overflow runs the exception filter in the few KB past the guard
	// page. The 8 KB log buffer, the path buffers, LoadLibrary and
	// MiniDumpWriteDump need more, so the report faulted again and the process
	// died with no log or dump. Reserve headroom on this (the main) thread;
	// its 16 MB stack makes 128 KB a small price.
	ULONG stackGuarantee = 128 * 1024;
	SetThreadStackGuarantee(&stackGuarantee);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_set_invalid_parameter_handler(Sys_InvalidParameterHandler);
	_set_purecall_handler(Sys_PurecallHandler);
	signal(SIGABRT, Sys_AbortSignalHandler);
	std::set_terminate(Sys_TerminateHandler);

	g_prevExceptionFilter = SetUnhandledExceptionFilter(Sys_UnhandledExceptionFilter);
}

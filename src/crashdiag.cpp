#include "crashdiag.h"

#ifdef _WIN32

#include "utils.h"

#include <windows.h>
#include <dbghelp.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace
{

const char kBuildId[] = "OpenUA built " __DATE__ " " __TIME__;
const char kInitialPhase[] = "diagnostics initialized";
const char kNoOperation[] = "none";

volatile LONG g_enabled = 0;
volatile LONG g_handlingCrash = 0;
volatile LONG g_screenMode = 0;
volatile LONG g_levelId = -1;
volatile LONG g_timeStamp = 0;
volatile LONG g_dTime = 0;
volatile LONG g_mainThreadId = 0;
PVOID volatile g_lastPhase = const_cast<char *>(kInitialPhase);
PVOID volatile g_lastOperation = const_cast<char *>(kNoOperation);

HANDLE g_sessionFile = INVALID_HANDLE_VALUE;
char g_sessionPath[1024] = {};
char g_crashDir[1024] = {};

LPTOP_LEVEL_EXCEPTION_FILTER g_previousExceptionFilter = nullptr;
std::terminate_handler g_previousTerminateHandler = nullptr;

bool Enabled()
{
    return InterlockedCompareExchange(&g_enabled, 0, 0) != 0;
}

const char *ReadLabel(PVOID volatile *label)
{
    return static_cast<const char *>(InterlockedCompareExchangePointer(label, nullptr, nullptr));
}

void StoreLabel(PVOID volatile *destination, const char *label)
{
    InterlockedExchangePointer(destination, const_cast<char *>(label ? label : "unknown"));
}

void WriteRaw(HANDLE file, const char *text)
{
    if (file == INVALID_HANDLE_VALUE || !text)
        return;

    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

void WriteFormatted(HANDLE file, const char *format, ...)
{
    char line[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    line[sizeof(line) - 1] = '\0';
    WriteRaw(file, line);
}

void MakeAbsolutePath(const std::string &path, char *output, size_t outputSize)
{
    if (!output || outputSize == 0)
        return;

    DWORD length = GetFullPathNameA(path.c_str(), static_cast<DWORD>(outputSize), output, nullptr);
    if (length == 0 || length >= outputSize)
    {
        std::strncpy(output, path.c_str(), outputSize - 1);
        output[outputSize - 1] = '\0';
    }
}

void PreparePaths()
{
    MakeAbsolutePath(uaDataFirstResolvedWritePath("env/OpenUA_session.log"),
                     g_sessionPath, sizeof(g_sessionPath));

    std::strncpy(g_crashDir, g_sessionPath, sizeof(g_crashDir) - 1);
    g_crashDir[sizeof(g_crashDir) - 1] = '\0';

    char *slash = std::strrchr(g_crashDir, '\\');
    char *forwardSlash = std::strrchr(g_crashDir, '/');
    if (!slash || (forwardSlash && forwardSlash > slash))
        slash = forwardSlash;

    if (slash)
        *slash = '\0';
    else
        std::strcpy(g_crashDir, ".");
}

uintptr_t InstructionPointer(const CONTEXT *context)
{
    if (!context)
        return 0;

#if defined(_M_X64) || defined(__x86_64__)
    return static_cast<uintptr_t>(context->Rip);
#elif defined(_M_IX86) || defined(__i386__)
    return static_cast<uintptr_t>(context->Eip);
#elif defined(_M_ARM64) || defined(__aarch64__)
    return static_cast<uintptr_t>(context->Pc);
#else
    return 0;
#endif
}

const char *AccessType(const EXCEPTION_RECORD *record)
{
    if (!record
        || (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION
            && record->ExceptionCode != EXCEPTION_IN_PAGE_ERROR)
        || record->NumberParameters < 2)
        return "not available";

    switch (record->ExceptionInformation[0])
    {
    case 0:
        return "read";
    case 1:
        return "write";
    case 8:
        return "execute";
    default:
        return "unknown";
    }
}

void MakeCrashPaths(const SYSTEMTIME &time, char *txtPath, size_t txtSize,
                    char *dumpPath, size_t dumpSize)
{
    std::snprintf(txtPath, txtSize,
                  "%s\\OpenUA_crash_%04u%02u%02u_%02u%02u%02u.txt",
                  g_crashDir, time.wYear, time.wMonth, time.wDay,
                  time.wHour, time.wMinute, time.wSecond);
    txtPath[txtSize - 1] = '\0';

    std::snprintf(dumpPath, dumpSize,
                  "%s\\OpenUA_crash_%04u%02u%02u_%02u%02u%02u.dmp",
                  g_crashDir, time.wYear, time.wMonth, time.wDay,
                  time.wHour, time.wMinute, time.wSecond);
    dumpPath[dumpSize - 1] = '\0';
}

bool WriteMiniDump(const char *dumpPath, EXCEPTION_POINTERS *exceptionPointers,
                   MINIDUMP_TYPE *writtenType, DWORD *error)
{
    HANDLE dumpFile = CreateFileA(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dumpFile == INVALID_HANDLE_VALUE)
    {
        *error = GetLastError();
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo = {};
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = exceptionPointers;
    exceptionInfo.ClientPointers = FALSE;

    MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo);
    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                                dumpType, &exceptionInfo, nullptr, nullptr);
    if (!ok)
    {
        SetFilePointer(dumpFile, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(dumpFile);
        dumpType = MiniDumpNormal;
        ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                               dumpType, &exceptionInfo, nullptr, nullptr);
    }

    *error = ok ? ERROR_SUCCESS : GetLastError();
    *writtenType = dumpType;
    CloseHandle(dumpFile);
    return ok != FALSE;
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS *exceptionPointers)
{
    if (InterlockedCompareExchange(&g_handlingCrash, 1, 0) != 0)
        TerminateProcess(GetCurrentProcess(), 3);

    SYSTEMTIME now = {};
    GetLocalTime(&now);

    char txtPath[1200];
    char dumpPath[1200];
    MakeCrashPaths(now, txtPath, sizeof(txtPath), dumpPath, sizeof(dumpPath));

    MINIDUMP_TYPE dumpType = MiniDumpNormal;
    DWORD dumpError = ERROR_SUCCESS;
    bool dumpWritten = WriteMiniDump(dumpPath, exceptionPointers, &dumpType, &dumpError);

    HANDLE report = CreateFileA(txtPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (report != INVALID_HANDLE_VALUE)
    {
        const EXCEPTION_RECORD *record = exceptionPointers ? exceptionPointers->ExceptionRecord : nullptr;
        const CONTEXT *context = exceptionPointers ? exceptionPointers->ContextRecord : nullptr;
        const DWORD exceptionCode = record ? record->ExceptionCode : 0;
        const uintptr_t exceptionAddress = record
            ? reinterpret_cast<uintptr_t>(record->ExceptionAddress) : 0;
        const uintptr_t instructionPointer = InstructionPointer(context);
        const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const bool positiveOffset = instructionPointer >= moduleBase;
        const uintptr_t relativeOffset = positiveOffset
            ? instructionPointer - moduleBase : moduleBase - instructionPointer;
        const bool hasAccessInfo = record
            && (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
                || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
            && record->NumberParameters >= 2;
        const ULONG_PTR accessAddress = hasAccessInfo ? record->ExceptionInformation[1] : 0;

        WriteFormatted(report, "OpenUA crash diagnostics\r\n");
        WriteFormatted(report, "Date/time: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n",
                       now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                       now.wSecond, now.wMilliseconds);
        WriteFormatted(report, "Build: %s\r\n", kBuildId);
        WriteFormatted(report, "Exception code: 0x%08lX\r\n", static_cast<unsigned long>(exceptionCode));
        WriteFormatted(report, "Access type: %s\r\n", AccessType(record));
        WriteFormatted(report, "Access address: 0x%llX\r\n", static_cast<unsigned long long>(accessAddress));
        WriteFormatted(report, "Exception address: 0x%llX\r\n", static_cast<unsigned long long>(exceptionAddress));
        WriteFormatted(report, "Instruction pointer: 0x%llX\r\n", static_cast<unsigned long long>(instructionPointer));
        WriteFormatted(report, "OpenUA.exe base: 0x%llX\r\n", static_cast<unsigned long long>(moduleBase));
        WriteFormatted(report, "Relative offset: %s0x%llX\r\n", positiveOffset ? "" : "-",
                       static_cast<unsigned long long>(relativeOffset));
        WriteFormatted(report, "Process ID: %lu\r\n", static_cast<unsigned long>(GetCurrentProcessId()));
        WriteFormatted(report, "Crash thread ID: %lu\r\n", static_cast<unsigned long>(GetCurrentThreadId()));
        WriteFormatted(report, "Main thread ID: %lu\r\n",
                       static_cast<unsigned long>(InterlockedCompareExchange(&g_mainThreadId, 0, 0)));
        WriteFormatted(report, "Last phase: %s\r\n", ReadLabel(&g_lastPhase));
        WriteFormatted(report, "Last load/save event: %s\r\n", ReadLabel(&g_lastOperation));
        WriteFormatted(report, "GameScreenMode: %ld\r\n",
                       static_cast<long>(InterlockedCompareExchange(&g_screenMode, 0, 0)));
        WriteFormatted(report, "Level ID: %ld\r\n",
                       static_cast<long>(InterlockedCompareExchange(&g_levelId, 0, 0)));
        WriteFormatted(report, "Simulation timestamp: %lu\r\n",
                       static_cast<unsigned long>(InterlockedCompareExchange(&g_timeStamp, 0, 0)));
        WriteFormatted(report, "Simulation delta: %lu\r\n",
                       static_cast<unsigned long>(InterlockedCompareExchange(&g_dTime, 0, 0)));
        WriteFormatted(report, "Minidump: %s\r\n", dumpPath);
        if (dumpWritten)
        {
            WriteFormatted(report, "Minidump type: %s\r\n",
                           dumpType == MiniDumpNormal ? "MiniDumpNormal"
                                                      : "MiniDumpNormal | MiniDumpWithThreadInfo");
        }
        else
        {
            WriteFormatted(report, "Minidump error: %lu\r\n", static_cast<unsigned long>(dumpError));
        }
        WriteFormatted(report,
                       "Analysis note: use this dump with the exact same OpenUA.exe build.\r\n");
        FlushFileBuffers(report);
        CloseHandle(report);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

void TerminateHandler()
{
    CONTEXT context = {};
    RtlCaptureContext(&context);

    EXCEPTION_RECORD record = {};
    record.ExceptionCode = 0xE0000001UL;
    record.ExceptionAddress = reinterpret_cast<PVOID>(InstructionPointer(&context));

    EXCEPTION_POINTERS exceptionPointers = {};
    exceptionPointers.ExceptionRecord = &record;
    exceptionPointers.ContextRecord = &context;
    CrashHandler(&exceptionPointers);
    TerminateProcess(GetCurrentProcess(), record.ExceptionCode);
}

}

namespace CrashDiag
{

bool Initialize(bool enabled)
{
    if (!enabled)
        return false;

    PreparePaths();
    g_sessionFile = CreateFileA(g_sessionPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    InterlockedExchange(&g_mainThreadId, static_cast<LONG>(GetCurrentThreadId()));
    InterlockedExchange(&g_enabled, 1);
    g_previousExceptionFilter = SetUnhandledExceptionFilter(CrashHandler);
    g_previousTerminateHandler = std::set_terminate(TerminateHandler);

    Breadcrumb("process started");
    return true;
}

bool IsEnabled()
{
    return Enabled();
}

void Breadcrumb(const char *phase)
{
    if (!Enabled())
        return;

    StoreLabel(&g_lastPhase, phase);

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char line[1400];
    std::snprintf(line, sizeof(line),
                  "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [phase] %s\r\n",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  now.wSecond, now.wMilliseconds, phase ? phase : "unknown");
    line[sizeof(line) - 1] = '\0';
    WriteRaw(g_sessionFile, line);
    if (g_sessionFile != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_sessionFile);
}

void SetLastOperation(const char *operation)
{
    if (Enabled())
        StoreLabel(&g_lastOperation, operation);
}

void UpdateRuntimeState(int32_t screenMode, int32_t levelId, uint32_t timeStamp, uint32_t dTime)
{
    if (!Enabled())
        return;

    InterlockedExchange(&g_screenMode, static_cast<LONG>(screenMode));
    InterlockedExchange(&g_levelId, static_cast<LONG>(levelId));
    InterlockedExchange(&g_timeStamp, static_cast<LONG>(timeStamp));
    InterlockedExchange(&g_dTime, static_cast<LONG>(dTime));
}

void ShutdownComplete()
{
    if (!Enabled())
        return;

    Breadcrumb("shutdown clean completed");
    SetUnhandledExceptionFilter(g_previousExceptionFilter);
    std::set_terminate(g_previousTerminateHandler);
    InterlockedExchange(&g_enabled, 0);

    if (g_sessionFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_sessionFile);
        g_sessionFile = INVALID_HANDLE_VALUE;
    }
}

}

#else

namespace CrashDiag
{

bool Initialize(bool)
{
    return false;
}

bool IsEnabled()
{
    return false;
}

void Breadcrumb(const char *)
{
}

void SetLastOperation(const char *)
{
}

void UpdateRuntimeState(int32_t, int32_t, uint32_t, uint32_t)
{
}

void ShutdownComplete()
{
}

}

#endif

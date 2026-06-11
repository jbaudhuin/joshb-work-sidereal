#include "crashhandler.h"

#include <QDir>
#include <QStandardPaths>

#ifdef Q_OS_WIN

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>

#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>

// dbghelp is linked via the build files (CMake/qmake); on MSVC the pragma below
// is a redundant belt-and-suspenders that other toolchains simply ignore.
#if defined(_MSC_VER)
#pragma comment(lib, "dbghelp.lib")
#endif

namespace
{
// --- State captured at install() time, used (read-only) at crash time. ------
// Everything the crash-time path touches is preallocated here so the handler
// itself performs no Qt calls and no heap allocation.
wchar_t g_crashDir[MAX_PATH]   = {0};   // %LOCALAPPDATA%\Zodiac\crashes
char    g_versionLine[256]     = {0};   // app version, ASCII, for the .txt header
LONG    g_handling             = 0;     // re-entrancy guard

LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

// Build "<g_crashDir>\crash-YYYYMMDD-HHMMSS.<ext>" into out. No allocation.
void
buildCrashPath(wchar_t* out, size_t cap, const SYSTEMTIME& st, const wchar_t* ext)
{
    wsprintfW(out,
              L"%s\\crash-%04u%02u%02u-%02u%02u%02u.%s",
              g_crashDir,
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond,
              ext);
    (void)cap;
}

// Write a minidump for the current process state.
void
writeMinidump(const wchar_t* path, EXCEPTION_POINTERS* ep)
{
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    MINIDUMP_EXCEPTION_INFORMATION mei = {};
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers    = FALSE;

    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules |
        MiniDumpWithIndirectlyReferencedMemory);

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                      type, ep ? &mei : nullptr, nullptr, nullptr);

    CloseHandle(hFile);
}

// Append a NUL-terminated ASCII line to an open file handle.
void
writeLine(HANDLE h, const char* s)
{
    DWORD written = 0;
    WriteFile(h, s, static_cast<DWORD>(lstrlenA(s)), &written, nullptr);
}

// Walk the stack from the given context and write a symbolized text trace.
void
writeTextTrace(const wchar_t* path, CONTEXT* ctx, const EXCEPTION_RECORD* rec)
{
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    char buf[1024];

    writeLine(hFile, "=== Zodiac crash report ===\r\n");
    if (g_versionLine[0]) {
        wsprintfA(buf, "Version: %s\r\n", g_versionLine);
        writeLine(hFile, buf);
    }
    if (rec) {
        wsprintfA(buf, "Exception: 0x%08X at 0x%p\r\n",
                  rec->ExceptionCode, rec->ExceptionAddress);
        writeLine(hFile, buf);
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            rec->NumberParameters >= 2) {
            wsprintfA(buf, "Access violation %s address 0x%p\r\n",
                      rec->ExceptionInformation[0] ? "writing" : "reading",
                      reinterpret_cast<void*>(rec->ExceptionInformation[1]));
            writeLine(hFile, buf);
        }
    }
    writeLine(hFile, "\r\nCall stack (most recent first):\r\n");

    HANDLE hProc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(hProc, nullptr, TRUE);

    // SYMBOL_INFO needs trailing room for the undecorated name.
    alignas(SYMBOL_INFO) char symbolMem[sizeof(SYMBOL_INFO) + 512] = {};
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolMem);

    // Symbolize one program-counter address and write the formatted frame.
    // We deliberately do NOT call SymGetLineFromAddr64 here: it faults inside
    // DbgHelp on clang/lld-produced PDBs. Function name + module + the raw
    // address are enough -- exact file:line comes from opening the .dmp with
    // the matching zodiac.pdb, or `llvm-symbolizer -e zodiac.exe <module-offset>`.
    auto emitFrame = [&](int i, DWORD64 addr) {
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen   = 511;

        const char* baseName  = "<unknown>";
        DWORD64     modBase   = SymGetModuleBase64(hProc, addr);
        char        modName[MAX_PATH];
        if (modBase) {
            if (GetModuleFileNameA(reinterpret_cast<HMODULE>(modBase), modName,
                                   MAX_PATH)) {
                baseName = modName;
                for (const char* p = modName; *p; ++p)
                    if (*p == '\\' || *p == '/')
                        baseName = p + 1;
            }
        }

        DWORD64     dispSym = 0;
        const char* sym     = "<no symbol>";
        if (SymFromAddr(hProc, addr, &dispSym, symbol))
            sym = symbol->Name;

        // module-relative offset (RVA) makes offline llvm-symbolizer trivial.
        // NB: wsprintfA needs %I64x for 64-bit values; %llx misparses and faults.
        const DWORD64 rva = modBase ? addr - modBase : 0;
        wsprintfA(buf, "#%-2d %s+0x%I64x  %s+0x%I64x  (0x%p)\r\n",
                  i, baseName, rva, sym, dispSym,
                  reinterpret_cast<void*>(addr));
        writeLine(hFile, buf);
    };

#if defined(_M_X64)
    // Walk via RtlVirtualUnwind -- the same mechanism the OS used to deliver
    // this exception, so it is always compatible with the compiler's unwind
    // info (unlike DbgHelp's StackWalk64, which faults on clang/lld output).
    CONTEXT walk = *ctx;
    for (int i = 0; i < 128 && walk.Rip; ++i) {
        emitFrame(i, walk.Rip);

        DWORD64           imageBase = 0;
        PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(walk.Rip, &imageBase, nullptr);
        if (!rf) {
            // Leaf function with no unwind data: return address is at [rsp].
            if (!walk.Rsp)
                break;
            walk.Rip = *reinterpret_cast<DWORD64*>(walk.Rsp);
            walk.Rsp += 8;
        } else {
            PVOID   handlerData      = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, walk.Rip, rf, &walk,
                             &handlerData, &establisherFrame, nullptr);
        }
    }
#else
    // x86 / ARM64: DbgHelp's StackWalk64 (these toolchains emit info it reads).
    STACKFRAME64 frame = {};
    DWORD        machine;
#if defined(_M_IX86)
    machine                = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = ctx->Eip;
    frame.AddrFrame.Offset = ctx->Ebp;
    frame.AddrStack.Offset = ctx->Esp;
#elif defined(_M_ARM64)
    machine                = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset    = ctx->Pc;
    frame.AddrFrame.Offset = ctx->Fp;
    frame.AddrStack.Offset = ctx->Sp;
#else
#error "Unsupported architecture for crash stack walking"
#endif
    frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 128; ++i) {
        if (!StackWalk64(machine, hProc, GetCurrentThread(), &frame, ctx,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                         nullptr))
            break;
        if (!frame.AddrPC.Offset)
            break;
        emitFrame(i, frame.AddrPC.Offset);
    }
#endif

    SymCleanup(hProc);
    CloseHandle(hFile);
}

// Common sink. ep may be null (CRT-originated fatal); in that case we capture
// the current context so the stack walk still has something to start from.
void
handleFatal(EXCEPTION_POINTERS* ep)
{
    // Only the first thread to get here writes a report; never re-enter.
    if (InterlockedCompareExchange(&g_handling, 1, 0) != 0)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    CONTEXT  localCtx = {};
    CONTEXT* ctx;
    if (ep && ep->ContextRecord) {
        ctx = ep->ContextRecord;
    } else {
        RtlCaptureContext(&localCtx);
        ctx = &localCtx;
    }

    wchar_t dmpPath[MAX_PATH];
    wchar_t txtPath[MAX_PATH];
    buildCrashPath(dmpPath, MAX_PATH, st, L"dmp");
    buildCrashPath(txtPath, MAX_PATH, st, L"txt");

    writeMinidump(dmpPath, ep);
    writeTextTrace(txtPath, ctx, ep ? ep->ExceptionRecord : nullptr);
}

LONG WINAPI
topLevelFilter(EXCEPTION_POINTERS* ep)
{
    handleFatal(ep);
    // Let the OS finish terminating (and chain to any previous filter).
    if (g_prevFilter)
        return g_prevFilter(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

void
terminateHandler()
{
    handleFatal(nullptr);
    // std::terminate must not return; fall through to abort.
}

void
purecallHandler()
{
    handleFatal(nullptr);
}

void
invalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*,
                        unsigned int, uintptr_t)
{
    handleFatal(nullptr);
}

void
signalHandler(int)
{
    handleFatal(nullptr);
}
} // namespace

namespace CrashHandler
{
QString
crashDir()
{
    // %LOCALAPPDATA%\Zodiac\crashes -- per-user, writable, survives reinstall.
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath("Zodiac/crashes");
}

void
install(const QString& appVersion)
{
    const QString dir = crashDir();
    QDir().mkpath(dir);

    const QString native = QDir::toNativeSeparators(dir);
    lstrcpynW(g_crashDir,
              reinterpret_cast<const wchar_t*>(native.utf16()),
              MAX_PATH);

    const QByteArray ver = appVersion.toLatin1();
    lstrcpynA(g_versionLine, ver.constData(), sizeof(g_versionLine));

    g_prevFilter = SetUnhandledExceptionFilter(topLevelFilter);

    // Catch the CRT-level fatal paths that bypass the SEH filter.
    std::set_terminate(terminateHandler);
    _set_purecall_handler(purecallHandler);
    _set_invalid_parameter_handler(invalidParameterHandler);
    signal(SIGABRT, signalHandler);

    // Don't pop the Windows "abort/retry/ignore" dialog -- we handle it.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

void
checkForPendingReports()
{
    QDir dir(crashDir());
    if (!dir.exists())
        return;

    const QStringList dumps =
        dir.entryList(QStringList() << "crash-*.dmp" << "crash-*.txt",
                      QDir::Files, QDir::Time);
    if (dumps.isEmpty())
        return;

    QMessageBox box;
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QObject::tr("Zodiac closed unexpectedly"));
    box.setText(QObject::tr("Zodiac ran into a problem and closed unexpectedly "
                            "the last time it was used."));
    box.setInformativeText(
        QObject::tr("A crash report was saved. You can bundle it into a single "
                    "file and send it to the developer to help fix the problem.\n\n"
                    "Click \"Show report\" to open the folder with the report "
                    "file selected, then attach it to an email."));

    QPushButton* show =
        box.addButton(QObject::tr("Show report"), QMessageBox::AcceptRole);
    QPushButton* discard =
        box.addButton(QObject::tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(QObject::tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(show);
    box.exec();

    if (box.clickedButton() == discard) {
        for (const QString& f : dir.entryList(
                 QStringList() << "crash-*.dmp" << "crash-*.txt", QDir::Files))
            dir.remove(f);
        return;
    }
    if (box.clickedButton() != show)
        return; // "Later" -- leave files, prompt again next launch.

    // Bundle every pending artifact into one zip using the bundled tar.exe
    // (bsdtar, present on Windows 10 1803+ and Windows 11).
    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString zipName = QString("zodiac-crash-%1.zip").arg(stamp);
    const QString zipPath = dir.filePath(zipName);

    const QStringList files = dir.entryList(
        QStringList() << "crash-*.dmp" << "crash-*.txt", QDir::Files);

    QStringList args;
    args << "-a" << "-c" << "-f" << QDir::toNativeSeparators(zipPath)
         << "-C" << QDir::toNativeSeparators(dir.absolutePath());
    args << files;

    const bool zipped =
        QProcess::execute("tar", args) == 0 && QFileInfo::exists(zipPath);

    if (zipped) {
        // Originals are now inside the zip -- remove them so we don't re-prompt.
        for (const QString& f : files)
            dir.remove(f);
        QProcess::startDetached(
            "explorer.exe",
            QStringList() << ("/select," + QDir::toNativeSeparators(zipPath)));
    } else {
        // tar unavailable/failed: just reveal the folder with raw files.
        QProcess::startDetached(
            "explorer.exe",
            QStringList() << QDir::toNativeSeparators(dir.absolutePath()));
    }
}
} // namespace CrashHandler

#else // !Q_OS_WIN -- no-ops on other platforms for now.

namespace CrashHandler
{
QString crashDir() { return QString(); }
void    install(const QString&) {}
void    checkForPendingReports() {}
} // namespace CrashHandler

#endif

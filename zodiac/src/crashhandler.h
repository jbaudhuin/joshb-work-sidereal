#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <QString>

// Post-mortem crash capture for release builds (no debugger required).
//
// On Windows this installs a top-level exception filter plus CRT hooks so that
// when the process is about to die unexpectedly it writes two artifacts to the
// crash folder:
//   * crash-<timestamp>.dmp  -- a minidump (open in Visual Studio / WinDbg with
//                               the matching zodiac.pdb for a full stack)
//   * crash-<timestamp>.txt  -- a symbolized text stack trace, pasteable into an
//                               analysis tool (Claude / Copilot) or an email
//
// On other platforms the functions are no-ops.
namespace CrashHandler
{
// Install the handler. Call once, as early as possible in main() (ideally
// before QApplication so a crash during startup is still captured). Only Win32
// APIs are used at crash time -- no Qt, no heap churn -- so the path is computed
// and the crash folder created here, up front.
void install(const QString& appVersion);

// Look in the crash folder for unsent reports. If any are found, prompt the
// user (friendly themed dialog) to bundle them into a .zip and reveal it in
// Explorer so they can send it on. Call AFTER QApplication is constructed --
// this is the "next launch" phase and is free to use widgets, processes, etc.
void checkForPendingReports();

// Absolute path to %LOCALAPPDATA%\Zodiac\crashes.
QString crashDir();
} // namespace CrashHandler

#endif // CRASHHANDLER_H

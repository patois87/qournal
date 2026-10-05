#include "CrashHandler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <atomic>
#include <csignal>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif __has_include(<execinfo.h>) && !defined(Q_OS_ANDROID)
// Android has it only from API level 33
#include <execinfo.h>
#define HAVE_EXECINFO
#endif

namespace {

std::function<void()> emergencySave;
std::atomic_bool crashed{false};
constexpr int MAX_FRAMES = 64;

const char* signalName(int signal) {
    switch (signal) {
        case SIGSEGV:
            return "SIGSEGV (invalid memory access)";
        case SIGABRT:
            return "SIGABRT (abort)";
        case SIGFPE:
            return "SIGFPE (arithmetic error)";
        case SIGILL:
            return "SIGILL (illegal instruction)";
        default:
            return "unknown signal";
    }
}

/// The calls that led to the crash, one per line. Names where the system knows them; on Windows the module and
/// the offset in it, which a debugger or addr2line turns into a place in the source
QByteArray stackTrace() {
    QByteArray result;
#if defined(HAVE_EXECINFO)
    void* frames[MAX_FRAMES];
    const int count = backtrace(frames, MAX_FRAMES);
    char** symbols = backtrace_symbols(frames, count);
    for (int i = 0; i < count; ++i) {
        result += "  ";
        result += symbols ? QByteArray(symbols[i]) : QByteArray::number(quintptr(frames[i]), 16);
        result += '\n';
    }
    free(symbols);
#elif defined(Q_OS_WIN)
    void* frames[MAX_FRAMES];
    const USHORT count = CaptureStackBackTrace(0, MAX_FRAMES, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module)) {
            GetModuleFileNameA(module, name, MAX_PATH);
        }
        const quintptr offset = quintptr(frames[i]) - quintptr(module);
        result += "  " + QByteArray(name) + " +0x" + QByteArray::number(offset, 16) + '\n';
    }
#else
    result = "  (not available on this system)\n";
#endif
    return result;
}

/// Not everything here is allowed in a signal handler. The process is lost anyway: saving the document and
/// writing the log are worth the attempt, and a second crash while trying ends the process at once
void writeCrashLog(const char* reason) {
    if (emergencySave) {
        emergencySave();
    }
    const QString folder = CrashHandler::logFolder();
    QDir().mkpath(folder);
    const QDateTime now = QDateTime::currentDateTime();
    QFile log(folder + QStringLiteral("/errorlog.%1.log").arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    if (log.open(QIODevice::WriteOnly)) {
        log.write(QStringLiteral("Date: %1\nVersion: %2\nQt: %3\nSignal: %4\n"
                                 "The document was saved for recovery, if it had unsaved changes.\n\nStack:\n")
                          .arg(now.toString(Qt::ISODate), QCoreApplication::applicationVersion(),
                               QString::fromLatin1(qVersion()), QString::fromLatin1(reason))
                          .toUtf8());
        log.write(stackTrace());
    }
}

void onCrash(int signal) {
    if (!crashed.exchange(true)) {
        writeCrashLog(signalName(signal));
    }
    // Let the system do what it does with a crash
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

#ifdef Q_OS_WIN
/// Crashes on Windows are exceptions of the system, which do not always arrive as signals
LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    if (!crashed.exchange(true)) {
        const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
        writeCrashLog(code == EXCEPTION_ACCESS_VIOLATION    ? "access violation" :
                      code == EXCEPTION_STACK_OVERFLOW      ? "stack overflow" :
                      code == EXCEPTION_INT_DIVIDE_BY_ZERO  ? "division by zero" :
                      code == EXCEPTION_ILLEGAL_INSTRUCTION ? "illegal instruction" :
                                                              "unhandled exception");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

}  // namespace

void CrashHandler::install() {
    for (int signal: {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) {
        std::signal(signal, onCrash);
    }
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(onException);
#endif
    // Loads what the stack trace needs now: in the crash it may not be possible any more
    stackTrace();
}

void CrashHandler::setEmergencySave(std::function<void()> save) { emergencySave = std::move(save); }

QString CrashHandler::logFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/errorlogs");
}

QStringList CrashHandler::takeNewLogs() {
    // The names have the time in them: newer ones sort later
    QSettings settings;
    const QString lastReported = settings.value(QStringLiteral("crash/lastReportedLog")).toString();
    const QStringList names = QDir(logFolder()).entryList({QStringLiteral("errorlog.*.log")}, QDir::Files, QDir::Name);
    QStringList result;
    for (const QString& name: names) {
        if (name > lastReported) {
            result.append(logFolder() + u'/' + name);
        }
    }
    if (!names.isEmpty()) {
        settings.setValue(QStringLiteral("crash/lastReportedLog"), names.last());
    }
    return result;
}

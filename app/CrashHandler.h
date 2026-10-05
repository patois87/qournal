/*
 * Qournal
 *
 * What happens when the application crashes: the document is saved where the recovery of the next start finds
 * it, and a note about the crash with the stack is written to a log file, as Xournal++ does it. The next start
 * tells about it.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QString>
#include <QStringList>
#include <functional>

namespace CrashHandler {

/// Takes over the signals of a crash (invalid memory access, abort, ...), and the unhandled exceptions on Windows
void install();

/// What saves the document in an emergency; nothing if there is no document to save
void setEmergencySave(std::function<void()> save);

/// The folder of the log files
QString logFolder();

/// The logs of crashes that were not reported yet, full paths. Each is reported once: the next call does not
/// return them again
QStringList takeNewLogs();

}  // namespace CrashHandler

#include "ksyntaxhighlighting_logging.h"

namespace KSyntaxHighlighting {
// Only what goes wrong: the languages that are not built in, which some definitions include, are warned about
Q_LOGGING_CATEGORY(Log, "kf.syntaxhighlighting", QtCriticalMsg)
}

#include "LatexRunner.h"

#include <QFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#if QT_CONFIG(process)
#include <QProcess>
#endif

#include "BuiltinLatex.h"
#include "LatexTemplate.h"

namespace {
constexpr qsizetype MAX_LOG_LENGTH = 4000;
}

LatexRunner::LatexRunner(QObject* parent): QObject(parent), m_command(Latex::defaultCommand()) {}

LatexRunner::~LatexRunner() { stop(); }

void LatexRunner::setCommand(const QString& newCommand) {
    // Without a command the one of Xournal++ is used
    const QString command = newCommand.trimmed().isEmpty() ? Latex::defaultCommand() : newCommand.trimmed();
    if (m_command != command) {
        m_command = command;
        emit commandChanged();
    }
}

void LatexRunner::setTemplateFile(const QString& newFile) {
    const QString file = newFile.startsWith(u"file:") ? QUrl(newFile).toLocalFile() : newFile.trimmed();
    if (m_templateFile != file) {
        m_templateFile = file;
        emit templateFileChanged();
    }
}

bool LatexRunner::available() const { return installed() || BuiltinLatex::available(); }

bool LatexRunner::builtin() const { return !installed() && BuiltinLatex::available(); }

bool LatexRunner::installed() const {
#if QT_CONFIG(process)
    const QStringList arguments = QProcess::splitCommand(m_command);
    return !arguments.isEmpty() && !QStandardPaths::findExecutable(arguments.first()).isEmpty();
#else
    return false;
#endif
}

void LatexRunner::stop() {
#if QT_CONFIG(process)
    if (m_process) {
        QProcess* process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        process->kill();
        process->deleteLater();
        emit runningChanged();
    }
#endif
}

void LatexRunner::run(const QString& formula, const QColor& color) {
    if (builtin()) {
        stop();
        // Done at once, but answered as LaTeX answers: after this call
        QString error;
        const QByteArray pdf = BuiltinLatex::render(formula, color, &error);
        QTimer::singleShot(0, this, [this, pdf, error] {
            if (pdf.isEmpty()) {
                emit failed(error);
            } else {
                emit finished(pdf);
            }
        });
        return;
    }
#if QT_CONFIG(process)
    stop();

    QStringList arguments = QProcess::splitCommand(m_command);
    if (arguments.isEmpty()) {
        emit failed(tr("No command to run LaTeX is set"));
        return;
    }
    const QString program = QStandardPaths::findExecutable(arguments.takeFirst());
    if (program.isEmpty()) {
        emit failed(tr("The program to run LaTeX was not found: %1").arg(QProcess::splitCommand(m_command).first()));
        return;
    }

    QString templateText = Latex::defaultTemplate();
    if (!m_templateFile.isEmpty()) {
        QFile templateFile(m_templateFile);
        if (!templateFile.open(QIODevice::ReadOnly)) {
            emit failed(
                    tr("Could not read the LaTeX template \"%1\": %2").arg(m_templateFile, templateFile.errorString()));
            return;
        }
        templateText = QString::fromUtf8(templateFile.readAll());
    }

    // A fresh directory for the files LaTeX writes
    m_dir = std::make_unique<QTemporaryDir>();
    const QString texPath = m_dir->filePath(QStringLiteral("tex.tex"));
    QFile texFile(texPath);
    if (!m_dir->isValid() || !texFile.open(QIODevice::WriteOnly) ||
        texFile.write(Latex::substitute(formula, templateText, color).toUtf8()) < 0) {
        emit failed(tr("Could not write the LaTeX file: %1").arg(texFile.errorString()));
        return;
    }
    texFile.close();
    for (QString& argument: arguments) {
        argument.replace(QStringLiteral("{}"), texPath);
    }

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_dir->path());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    auto done = [this](const QString& error) {
        QProcess* process = m_process;
        if (!process) {
            return;
        }
        m_process = nullptr;
        const QString log = QString::fromLocal8Bit(process->readAll()).right(MAX_LOG_LENGTH);
        process->deleteLater();
        emit runningChanged();

        QFile pdf(m_dir->filePath(QStringLiteral("tex.pdf")));
        if (error.isEmpty() && pdf.open(QIODevice::ReadOnly)) {
            emit finished(pdf.readAll());
        } else {
            const QString reason = error.isEmpty() ? tr("LaTeX did not write a PDF") : error;
            emit failed(log.isEmpty() ? reason : reason + QStringLiteral("\n\n") + log);
        }
    };
    connect(m_process, &QProcess::finished, this, [done](int exitCode, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && exitCode == 0 ? QString() : tr("LaTeX could not render the formula"));
    });
    connect(m_process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            done(tr("LaTeX could not be started"));
        }
    });
    m_process->start(program, arguments);
    emit runningChanged();
#else
    Q_UNUSED(formula)
    Q_UNUSED(color)
    emit failed(tr("This platform cannot run LaTeX"));
#endif
}

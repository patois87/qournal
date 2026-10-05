/*
 * Qournal
 *
 * Runs LaTeX to turn a formula into a PDF, where a LaTeX installation is available (not on mobile platforms)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QColor>
#include <QObject>
#include <QTemporaryDir>
#include <memory>

#include <QtQml/qqmlregistration.h>

class QProcess;

class LatexRunner: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// The command that is run; "{}" stands for the .tex file. The PDF is expected next to it
    Q_PROPERTY(QString command READ command WRITE setCommand NOTIFY commandChanged)
    /// A file with the LaTeX document the formula is put into, see Latex::substitute(); empty for the one of
    /// Xournal++
    Q_PROPERTY(QString templateFile READ templateFile WRITE setTemplateFile NOTIFY templateFileChanged)
    /// Whether formulas can be rendered: the program of the command is installed, or the application sets them
    /// itself
    Q_PROPERTY(bool available READ available NOTIFY commandChanged)
    /// Whether the application sets the formulas itself (BuiltinLatex), because the program is not installed:
    /// formulas only, without packages and without the template
    Q_PROPERTY(bool builtin READ builtin NOTIFY commandChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit LatexRunner(QObject* parent = nullptr);
    ~LatexRunner() override;

    QString command() const { return m_command; }
    void setCommand(const QString& newCommand);
    QString templateFile() const { return m_templateFile; }
    void setTemplateFile(const QString& file);
    bool available() const;
    bool builtin() const;
    /// Whether the program of the command is installed
    bool installed() const;
    bool running() const { return m_process != nullptr; }

    /// Renders a formula in the given colour. finished() or failed() follows; a run in progress is stopped
    Q_INVOKABLE void run(const QString& formula, const QColor& color);
    Q_INVOKABLE void stop();

signals:
    void commandChanged();
    void templateFileChanged();
    void runningChanged();
    /// The PDF with the formula
    void finished(const QByteArray& pdf);
    /// @param message what went wrong, with the output of LaTeX
    void failed(const QString& message);

private:
    QString m_command;
    QString m_templateFile;
    std::unique_ptr<QTemporaryDir> m_dir;
    QProcess* m_process = nullptr;
};

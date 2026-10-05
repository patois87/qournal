#include "LatexTemplate.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

QString Latex::defaultCommand() { return QStringLiteral("pdflatex -halt-on-error -interaction=nonstopmode '{}'"); }

QString Latex::defaultTemplate() {
    return QStringLiteral(R"TEX(% This template uses the scontents package of recent TeX distributions.
\documentclass[varwidth=0.999\maxdimen, crop, border=5pt]{standalone}
% The upper limit value of 'varwidth' can be referenced by \hsize.
\newcommand*{\setTextWidthReference}{%
  \setlength{\textwidth}{345.0pt}% Same value when you use 'varwidth=true'.
  \setlength{\linewidth}{\textwidth}%
  \setlength{\columnwidth}{\textwidth}%
}

% Packages
\usepackage{amsmath}
\usepackage{amssymb}

% for storing in memory verbatim content to be reused later
\usepackage{scontents}

% Blank formula checking
\usepackage{ifthen}
\newlength{\pheight}

% Color support
\usepackage{xcolor}
\definecolor{xpp_font_color}{HTML}{%%XPP_TEXT_COLOR%%}

% User input
\begin{scontents}[store-env=preview]
	\(
	\displaystyle
    %%XPP_TOOL_INPUT%%
	\)
\end{scontents}

\begin{document}
  \setTextWidthReference
  % Check if the formula is empty
  \settoheight{\pheight}{\getstored[1]{preview}}%
  \ifthenelse{\pheight=0}{\GenericError{}{xournalpp:blankformula}{}{}}

  % Render the user input
  \textcolor{xpp_font_color}{\getstored[1]{preview}}
\end{document}
)TEX");
}

QString Latex::substitute(const QString& formula, const QString& templ, const QColor& color) {
    static const QRegularExpression directive(QStringLiteral(R"(^%xpp:([A-Za-z_][A-Za-z0-9_]*)=(.*)$)"));
    static const QRegularExpression placeholder(QStringLiteral(R"(%%XPP_([A-Z][A-Z0-9_]*)%%)"));

    // Directives define variables and are not part of the formula
    QHash<QString, QString> variables;
    QStringList body;
    for (const QString& line: formula.split(u'\n')) {
        const auto match = directive.match(line);
        if (match.hasMatch()) {
            const QString key = match.captured(1).toUpper();
            if (key != u"TOOL_INPUT" && key != u"TEXT_COLOR") {
                variables.insert(key, match.captured(2).trimmed());
            }
        } else {
            body.append(line);
        }
    }
    variables.insert(QStringLiteral("TOOL_INPUT"), body.join(u'\n'));
    variables.insert(QStringLiteral("TEXT_COLOR"), color.name(QColor::HexRgb).mid(1));

    // Unknown placeholders are removed
    QString output;
    qsizetype pos = 0;
    auto it = placeholder.globalMatch(templ);
    while (it.hasNext()) {
        const auto match = it.next();
        output += QStringView(templ).mid(pos, match.capturedStart() - pos);
        output += variables.value(match.captured(1));
        pos = match.capturedEnd();
    }
    output += QStringView(templ).mid(pos);
    return output;
}

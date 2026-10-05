/*
 * Qournal
 *
 * The LaTeX document a formula is put into, ported from Xournal++ (LatexGenerator, default_template.tex)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QColor>
#include <QString>

namespace Latex {

/// The command Xournal++ runs; "{}" stands for the .tex file
QString defaultCommand();

/// The template of Xournal++: a formula in display style, cropped to its size
QString defaultTemplate();

/**
 * Puts a formula into a template. The placeholder %%XPP_TOOL_INPUT%% is replaced by the formula and
 * %%XPP_TEXT_COLOR%% by the colour (hexadecimal, without "#"). Lines of the formula of the form
 * "%xpp:NAME=value" are taken out of it and define the placeholder %%XPP_NAME%%.
 */
QString substitute(const QString& formula, const QString& templ, const QColor& color);

}  // namespace Latex

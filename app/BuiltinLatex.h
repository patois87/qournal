/*
 * Qournal
 *
 * Sets a formula without a LaTeX installation, with MicroTeX, which is built into the application: for mobile
 * platforms, where LaTeX cannot run, and for desktops without LaTeX. It knows the formulas of LaTeX (amsmath,
 * amssymb), but no packages and no template
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>

namespace BuiltinLatex {

/// Whether the application was built with MicroTeX
bool available();

/**
 * The formula as a PDF of one page, as LaTeX makes it with the template of Xournal++: 10 pt, as vectors, with a
 * border of 5 pt. The glyphs are outlines, so that the PDF needs no fonts
 *
 * @param error what is wrong with the formula, if the result is empty
 */
QByteArray render(const QString& formula, const QColor& color, QString* error = nullptr);

}  // namespace BuiltinLatex

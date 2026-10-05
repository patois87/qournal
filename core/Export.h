/*
 * Qournal
 *
 * Export of a document as PDF, PNG or SVG, with the options of Xournal++: page and layer ranges, with or without
 * background, layers one by one
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QList>
#include <QPainter>
#include <QString>
#include <QStringList>
#include <functional>

#include "Document.h"

namespace ElementRange {

/**
 * Parses a range like "1-3,5,7-": numbers from 1, parts separated by comma, semicolon or colon, open at
 * either end. The result has the numbers from 0, in the order given.
 * @param count the largest number that exists
 * @return false if the range is not valid; error tells why
 */
bool parse(const QString& range, int count, QList<int>& result, QString* error = nullptr);

}  // namespace ElementRange

struct ExportOptions {
    enum class Background { All, NoRuling, None };

    QList<int> pages;   ///< indices of the pages to export; all if empty
    QList<int> layers;  ///< indices of the layers to export; the visible ones if empty
    Background background = Background::All;
    /// "pdf", "png" or "svg". If empty, the extension of the file tells: locations that are not files
    /// (content:// on Android) need not have one
    QString format;
    bool progressiveLayers = false;  ///< one page for each layer, showing the layers up to it

    // Images
    double dpi = 300;  ///< resolution of PNG files
    int width = 0;     ///< width of PNG files in pixels; replaces dpi if positive
    int height = 0;    ///< height of PNG files in pixels; replaces dpi if positive and no width is given
    /// Resolution at which PDF backgrounds are put into SVG files, and into PDF files if their pages cannot be
    /// kept as they are (see rasterizePdfPages)
    double backgroundDpi = 200;
    /// A PDF export normally is the background PDF with the annotations added. With this set, or if the PDF is
    /// encrypted or damaged, its pages are put in as images instead
    bool rasterizePdfPages = false;
};

namespace Export {

/// What is exported as one page each: the pages of the document with the layers to show
struct Sheet {
    int page;
    Page content;  ///< the page with only the layers that are exported
};
std::vector<Sheet> sheets(const Document& doc, const ExportOptions& options);

/// Paints a sheet in page coordinates, with its background as the options ask for
void paintSheet(QPainter& p, const Document& doc, const Sheet& sheet, const ExportOptions& options,
                double backgroundDpi);

/// A PDF with one page per sheet; strokes and texts are vectors
bool toPdf(const Document& doc, const QString& path, const ExportOptions& options, QString* error = nullptr);

/**
 * PNG or SVG files, by the extension of the path. Several sheets give several files, numbered
 * ("name-1.png", ...).
 * @param written receives the files that were written
 */
bool toImages(const Document& doc, const QString& path, const ExportOptions& options, QStringList* written = nullptr,
              QString* error = nullptr);

/// Whether this build can write SVG files
bool svgSupported();

}  // namespace Export

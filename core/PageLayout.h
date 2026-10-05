/*
 * Qournal
 *
 * Arrangement of the pages in the view: a grid of rows and columns, optionally with paired pages.
 * The mapping between pages and grid cells is ported from LayoutMapper of Xournal++.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>

#include <QList>
#include <QRectF>
#include <QSizeF>

struct LayoutSettings {
    bool pairedPages = false;
    int pairsOffset = 1;  ///< pages before the first pair: 1 shows the first page alone, like a book cover

    int columns = 1;
    int rows = 1;
    bool fixedRows = false;  ///< the number of rows is given and the columns follow from it; else the other way round

    bool vertical = false;  ///< fill columns first instead of rows
    bool rightToLeft = false;
    bool bottomToTop = false;

    double padding = 12.0;   ///< around all pages
    double gap = 12.0;       ///< between rows and between columns
    double pairGap = 4.0;    ///< between the two pages of a pair

    bool operator==(const LayoutSettings& other) const;
    bool operator!=(const LayoutSettings& other) const { return !(*this == other); }
};

class PageLayout {
public:
    struct Cell {
        int column = 0;
        int row = 0;
    };

    PageLayout() = default;
    PageLayout(const std::vector<QSizeF>& pageSizes, const LayoutSettings& settings);

    /// Positions of the pages; the origin is the top left corner of the padding
    const QList<QRectF>& pageRects() const { return m_pageRects; }
    QSizeF size() const { return m_size; }

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    Cell cellOf(int page) const { return m_cells[static_cast<size_t>(page)]; }
    /// @return the page in a cell, -1 if the cell is empty
    int pageAt(int column, int row) const;

private:
    QList<QRectF> m_pageRects;
    QSizeF m_size;
    int m_columns = 1;
    int m_rows = 1;
    std::vector<Cell> m_cells;
};

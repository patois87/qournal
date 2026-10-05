#include "PageLayout.h"

#include <algorithm>

bool LayoutSettings::operator==(const LayoutSettings& other) const {
    return pairedPages == other.pairedPages && pairsOffset == other.pairsOffset && columns == other.columns &&
           rows == other.rows && fixedRows == other.fixedRows && vertical == other.vertical &&
           rightToLeft == other.rightToLeft && bottomToTop == other.bottomToTop && padding == other.padding &&
           gap == other.gap && pairGap == other.pairGap;
}

PageLayout::PageLayout(const std::vector<QSizeF>& pageSizes, const LayoutSettings& settings) {
    const int pages = static_cast<int>(pageSizes.size());
    if (pages == 0) {
        m_size = QSizeF(2 * settings.padding, 2 * settings.padding);
        return;
    }

    // Size of the grid
    const bool paired = settings.pairedPages;
    const int firstPageOffset = paired ? std::max(settings.pairsOffset, 0) : 0;
    if (settings.fixedRows) {
        m_rows = std::clamp(settings.rows, 1, pages);
        m_columns = std::max(1, (pages + firstPageOffset + (m_rows - 1)) / m_rows);
        if (paired) {
            m_columns += m_columns % 2;
        }
    } else {
        m_columns = std::clamp(settings.columns, 1, pages);
        if (paired) {
            m_columns += m_columns % 2;
        }
        m_rows = std::max(1, (pages + firstPageOffset + (m_columns - 1)) / m_columns);
    }

    int offset;
    if (settings.vertical) {
        offset = firstPageOffset % (paired ? 2 * m_rows : m_rows);
    } else {
        offset = firstPageOffset % m_columns;
    }

    // Cell of every page
    m_cells.resize(pageSizes.size());
    for (int i = 0; i < pages; ++i) {
        const int number = i + offset;
        auto gridPosition = [&](int n, int columns) {
            return settings.vertical ? Cell{n / m_rows, n % m_rows} : Cell{n % columns, n / columns};
        };
        Cell cell;
        if (paired) {
            cell = gridPosition(number / 2, m_columns / 2);
            cell.column = cell.column * 2 + number % 2;
        } else {
            cell = gridPosition(number, m_columns);
        }
        // The offset can push the last pages out of the grid computed above
        m_columns = std::max(m_columns, cell.column + 1);
        m_rows = std::max(m_rows, cell.row + 1);
        m_cells[static_cast<size_t>(i)] = cell;
    }
    for (Cell& cell: m_cells) {
        if (settings.rightToLeft) {
            cell.column = m_columns - 1 - cell.column;
        }
        if (settings.bottomToTop) {
            cell.row = m_rows - 1 - cell.row;
        }
    }

    // Columns are as wide as their widest page, rows as high as their highest page
    std::vector<double> columnWidths(static_cast<size_t>(m_columns), 0.0);
    std::vector<double> rowHeights(static_cast<size_t>(m_rows), 0.0);
    for (int i = 0; i < pages; ++i) {
        const Cell& cell = m_cells[static_cast<size_t>(i)];
        const QSizeF& size = pageSizes[static_cast<size_t>(i)];
        double& width = columnWidths[static_cast<size_t>(cell.column)];
        double& height = rowHeights[static_cast<size_t>(cell.row)];
        width = std::max(width, size.width());
        height = std::max(height, size.height());
    }

    std::vector<double> columnX(columnWidths.size());
    double x = settings.padding;
    for (int c = 0; c < m_columns; ++c) {
        columnX[static_cast<size_t>(c)] = x;
        const bool withinPair = paired && c % 2 == 0;
        x += columnWidths[static_cast<size_t>(c)] + (withinPair ? settings.pairGap : settings.gap);
    }
    const double totalWidth = x - (paired && m_columns % 2 == 1 ? settings.pairGap : settings.gap) + settings.padding;

    std::vector<double> rowY(rowHeights.size());
    double y = settings.padding;
    for (int r = 0; r < m_rows; ++r) {
        rowY[static_cast<size_t>(r)] = y;
        y += rowHeights[static_cast<size_t>(r)] + settings.gap;
    }
    const double totalHeight = y - settings.gap + settings.padding;
    m_size = QSizeF(totalWidth, totalHeight);

    m_pageRects.reserve(pages);
    for (int i = 0; i < pages; ++i) {
        const Cell& cell = m_cells[static_cast<size_t>(i)];
        const QSizeF& size = pageSizes[static_cast<size_t>(i)];
        const double freeWidth = columnWidths[static_cast<size_t>(cell.column)] - size.width();
        const double freeHeight = rowHeights[static_cast<size_t>(cell.row)] - size.height();

        double pageX = columnX[static_cast<size_t>(cell.column)];
        if (!paired) {
            pageX += freeWidth / 2;
        } else if (cell.column % 2 == 0) {
            pageX += freeWidth;  // the pages of a pair touch each other
        }
        m_pageRects.append(QRectF(QPointF(pageX, rowY[static_cast<size_t>(cell.row)] + freeHeight / 2), size));
    }
}

int PageLayout::pageAt(int column, int row) const {
    for (size_t i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].column == column && m_cells[i].row == row) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

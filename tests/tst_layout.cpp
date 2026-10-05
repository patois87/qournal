/*
 * Qournal
 *
 * Tests for the arrangement of the pages in the view
 *
 * @license GNU GPLv2 or later
 */

#include <QTest>

#include "PageLayout.h"

namespace {

const QSizeF A4(595, 842);

LayoutSettings plainSettings() {
    LayoutSettings settings;
    settings.padding = 10;
    settings.gap = 20;
    settings.pairGap = 4;
    return settings;
}

}  // namespace

class TestLayout: public QObject {
    Q_OBJECT

private slots:
    void singleColumn() {
        const PageLayout layout({A4, QSizeF(300, 400), A4}, plainSettings());
        QCOMPARE(layout.columns(), 1);
        QCOMPARE(layout.rows(), 3);
        QCOMPARE(layout.pageRects()[0], QRectF(10, 10, 595, 842));
        // Narrow pages are centered in their column
        QCOMPARE(layout.pageRects()[1], QRectF(10 + (595 - 300) / 2.0, 10 + 842 + 20, 300, 400));
        QCOMPARE(layout.pageRects()[2].top(), 10 + 842 + 20 + 400 + 20.0);
        QCOMPARE(layout.size(), QSizeF(595 + 20, 842 + 400 + 842 + 2 * 20 + 2 * 10));
    }

    void empty() {
        const PageLayout layout({}, plainSettings());
        QVERIFY(layout.pageRects().isEmpty());
        QCOMPARE(layout.size(), QSizeF(20, 20));
    }

    void pairedPages() {
        LayoutSettings settings = plainSettings();
        settings.pairedPages = true;
        const PageLayout layout(std::vector<QSizeF>(4, A4), settings);

        // The first page stands alone on the right, like the cover of a book
        QCOMPARE(layout.columns(), 2);
        QCOMPARE(layout.rows(), 3);
        QCOMPARE(layout.cellOf(0).column, 1);
        QCOMPARE(layout.cellOf(0).row, 0);
        QCOMPARE(layout.cellOf(1).column, 0);
        QCOMPARE(layout.cellOf(1).row, 1);
        QCOMPARE(layout.cellOf(2).column, 1);
        QCOMPARE(layout.cellOf(2).row, 1);
        QCOMPARE(layout.cellOf(3).column, 0);
        QCOMPARE(layout.cellOf(3).row, 2);
        QCOMPARE(layout.pageAt(0, 0), -1);
        QCOMPARE(layout.pageAt(1, 1), 2);

        // The two pages of a pair are close to each other
        QCOMPARE(layout.pageRects()[2].left() - layout.pageRects()[1].right(), 4.0);
        QCOMPARE(layout.size().width(), 2 * 595 + 4 + 2 * 10.0);
    }

    void pairedPagesWithoutOffset() {
        LayoutSettings settings = plainSettings();
        settings.pairedPages = true;
        settings.pairsOffset = 0;
        const PageLayout layout(std::vector<QSizeF>(4, A4), settings);
        QCOMPARE(layout.rows(), 2);
        QCOMPARE(layout.cellOf(0).column, 0);
        QCOMPARE(layout.cellOf(1).column, 1);
        QCOMPARE(layout.cellOf(1).row, 0);
        QCOMPARE(layout.cellOf(2).row, 1);
    }

    void pairedPagesAlignTowardsEachOther() {
        LayoutSettings settings = plainSettings();
        settings.pairedPages = true;
        settings.pairsOffset = 0;
        const PageLayout layout({QSizeF(300, 400), QSizeF(200, 400), A4, A4}, settings);
        // The narrow pages share the columns with the A4 pages below them
        QCOMPARE(layout.pageRects()[0].right(), 10 + 595.0);
        QCOMPARE(layout.pageRects()[1].left(), 10 + 595 + 4.0);
    }

    void columns() {
        LayoutSettings settings = plainSettings();
        settings.columns = 3;
        const PageLayout layout(std::vector<QSizeF>(7, A4), settings);
        QCOMPARE(layout.columns(), 3);
        QCOMPARE(layout.rows(), 3);
        QCOMPARE(layout.cellOf(4).column, 1);
        QCOMPARE(layout.cellOf(4).row, 1);
        QCOMPARE(layout.cellOf(6).column, 0);
        QCOMPARE(layout.cellOf(6).row, 2);
        QCOMPARE(layout.pageRects()[1].left(), 10 + 595 + 20.0);
    }

    void fixedRowsFillColumnsFirst() {
        LayoutSettings settings = plainSettings();
        settings.fixedRows = true;
        settings.rows = 2;
        settings.vertical = true;
        const PageLayout layout(std::vector<QSizeF>(5, A4), settings);
        QCOMPARE(layout.rows(), 2);
        QCOMPARE(layout.columns(), 3);
        QCOMPARE(layout.cellOf(1).column, 0);
        QCOMPARE(layout.cellOf(1).row, 1);
        QCOMPARE(layout.cellOf(2).column, 1);
        QCOMPARE(layout.cellOf(2).row, 0);
    }

    void reversedDirections() {
        LayoutSettings settings = plainSettings();
        settings.columns = 2;
        settings.rightToLeft = true;
        settings.bottomToTop = true;
        const PageLayout layout(std::vector<QSizeF>(4, A4), settings);
        QCOMPARE(layout.cellOf(0).column, 1);
        QCOMPARE(layout.cellOf(0).row, 1);
        QCOMPARE(layout.cellOf(3).column, 0);
        QCOMPARE(layout.cellOf(3).row, 0);
        QVERIFY(layout.pageRects()[0].left() > layout.pageRects()[1].left());
        QVERIFY(layout.pageRects()[0].top() > layout.pageRects()[2].top());
    }

    void morePagesThanCells() {
        // More columns requested than there are pages
        LayoutSettings settings = plainSettings();
        settings.columns = 8;
        const PageLayout layout(std::vector<QSizeF>(3, A4), settings);
        QCOMPARE(layout.columns(), 3);
        QCOMPARE(layout.rows(), 1);
    }

    void pagesDoNotOverlap_data() {
        QTest::addColumn<bool>("paired");
        QTest::addColumn<int>("columns");
        QTest::addColumn<bool>("vertical");
        for (bool paired: {false, true}) {
            for (int columns: {1, 2, 3, 5}) {
                for (bool vertical: {false, true}) {
                    QTest::addRow("paired=%d columns=%d vertical=%d", paired, columns, vertical)
                            << paired << columns << vertical;
                }
            }
        }
    }

    void pagesDoNotOverlap() {
        QFETCH(bool, paired);
        QFETCH(int, columns);
        QFETCH(bool, vertical);
        LayoutSettings settings = plainSettings();
        settings.pairedPages = paired;
        settings.columns = columns;
        settings.vertical = vertical;

        std::vector<QSizeF> sizes;
        for (int i = 0; i < 11; ++i) {
            sizes.emplace_back(200 + 37 * (i % 4), 300 + 53 * (i % 3));
        }
        const PageLayout layout(sizes, settings);
        const QRectF world(QPointF(0, 0), layout.size());
        for (int i = 0; i < 11; ++i) {
            QCOMPARE(layout.pageRects()[i].size(), sizes[static_cast<size_t>(i)]);
            QVERIFY(world.adjusted(9.99, 9.99, -9.99, -9.99).contains(layout.pageRects()[i]));
            QCOMPARE(layout.pageAt(layout.cellOf(i).column, layout.cellOf(i).row), i);
            for (int j = 0; j < i; ++j) {
                QVERIFY2(!layout.pageRects()[i].intersects(layout.pageRects()[j]),
                         qPrintable(QStringLiteral("pages %1 and %2 overlap").arg(i).arg(j)));
            }
        }
    }
};

QTEST_MAIN(TestLayout)
#include "tst_layout.moc"

/*
 * Qournal
 *
 * Qt Quick item showing a small rendering of one page, for the sidebar
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "PageCanvas.h"

class PagePreview: public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(PageCanvas* canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)

public:
    explicit PagePreview(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    PageCanvas* canvas() const { return m_canvas; }
    void setCanvas(PageCanvas* canvas);
    int page() const { return m_page; }
    void setPage(int page);

signals:
    void canvasChanged();
    void pageChanged();

private:
    QPointer<PageCanvas> m_canvas;
    int m_page = -1;
    QTimer m_refresh;  ///< collects the changes of a short time, e.g. while erasing
};

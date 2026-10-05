#include "PagePreview.h"

#include <QPainter>

namespace {
constexpr int REFRESH_DELAY_MS = 300;
}

PagePreview::PagePreview(QQuickItem* parent): QQuickPaintedItem(parent) {
    setAntialiasing(true);
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(REFRESH_DELAY_MS);
    connect(&m_refresh, &QTimer::timeout, this, [this] { update(); });
}

void PagePreview::setCanvas(PageCanvas* canvas) {
    if (m_canvas == canvas) {
        return;
    }
    if (m_canvas) {
        m_canvas->disconnect(this);
    }
    m_canvas = canvas;
    if (m_canvas) {
        connect(m_canvas, &PageCanvas::pageChanged, this, [this](int page) {
            if (page == m_page) {
                m_refresh.start();
            } else if (page < 0) {
                update();
            }
        });
    }
    emit canvasChanged();
    update();
}

void PagePreview::setPage(int page) {
    if (m_page != page) {
        m_page = page;
        emit pageChanged();
        update();
    }
}

void PagePreview::paint(QPainter* painter) {
    if (m_canvas) {
        m_canvas->paintPage(painter, m_page, size());
    }
}

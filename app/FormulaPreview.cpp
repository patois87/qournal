#include "FormulaPreview.h"

#include <QBuffer>
#include <QPainter>

#ifdef HAVE_QTPDF
#include <QPdfDocument>
#endif

FormulaPreview::FormulaPreview(QQuickItem* parent): QQuickPaintedItem(parent) {}

void FormulaPreview::setPdf(const QByteArray& pdf) {
    if (m_pdf != pdf) {
        m_pdf = pdf;
        update();
        emit pdfChanged();
    }
}

void FormulaPreview::setPaper(const QColor& paper) {
    if (m_paper != paper) {
        m_paper = paper;
        update();
        emit paperChanged();
    }
}

void FormulaPreview::paint(QPainter* painter) {
    painter->fillRect(boundingRect(), m_paper);
#ifdef HAVE_QTPDF
    if (m_pdf.isEmpty()) {
        return;
    }
    QBuffer buffer(&m_pdf);
    buffer.open(QIODevice::ReadOnly);
    QPdfDocument document;
    document.load(&buffer);
    if (document.pageCount() < 1) {
        return;
    }
    // As large as it is on the page, or smaller if it does not fit
    const QSizeF natural = document.pagePointSize(0);
    const double margin = 8;
    const double scale =
            std::min({1.5, (width() - 2 * margin) / natural.width(), (height() - 2 * margin) / natural.height()});
    if (scale <= 0) {
        return;
    }
    const QSizeF shown = natural * scale;
    const qreal dpr = painter->device()->devicePixelRatioF();
    QPdfDocumentRenderOptions options;
    QImage image = document.render(0, (shown * dpr).toSize(), options);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(QRectF(QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown), image);
#endif
}

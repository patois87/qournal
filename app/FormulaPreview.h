/*
 * Qournal
 *
 * Shows the PDF LaTeX made of a formula, in the dialog that edits it
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QColor>
#include <QQuickPaintedItem>

#include <QtQml/qqmlregistration.h>

class FormulaPreview: public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    /// The PDF file with the formula; empty for none
    Q_PROPERTY(QByteArray pdf READ pdf WRITE setPdf NOTIFY pdfChanged)
    /// What is behind the formula
    Q_PROPERTY(QColor paper READ paper WRITE setPaper NOTIFY paperChanged)
    /// How much larger than on the page the formula is shown, if there is room for that
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged)

public:
    explicit FormulaPreview(QQuickItem* parent = nullptr);

    QByteArray pdf() const { return m_pdf; }
    void setPdf(const QByteArray& pdf);
    QColor paper() const { return m_paper; }
    void setPaper(const QColor& paper);
    double zoom() const { return m_zoom; }
    void setZoom(double zoom);

    void paint(QPainter* painter) override;

signals:
    void pdfChanged();
    void paperChanged();
    void zoomChanged();

private:
    QByteArray m_pdf;
    QColor m_paper = Qt::white;
    double m_zoom = 1.5;
};

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

public:
    explicit FormulaPreview(QQuickItem* parent = nullptr);

    QByteArray pdf() const { return m_pdf; }
    void setPdf(const QByteArray& pdf);
    QColor paper() const { return m_paper; }
    void setPaper(const QColor& paper);

    void paint(QPainter* painter) override;

signals:
    void pdfChanged();
    void paperChanged();

private:
    QByteArray m_pdf;
    QColor m_paper = Qt::white;
};

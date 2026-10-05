/*
 * Qournal
 *
 * A document with everything that only format 5 of the files can hold: links, turned and scaled texts and images
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <cmath>

#include "Document.h"

inline Matrix matrixOf(const QTransform& t) { return {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()}; }

inline Document format5Document() {
    Document doc = Document::createEmpty();
    Page& page = doc.pages.front();
    page.width = 400;
    page.height = 500;
    page.background.style = QStringLiteral("plain");
    std::vector<Element>& elements = page.layers.front().elements;

    LinkElement link;
    link.text = QStringLiteral("A link");
    link.url = QStringLiteral("https://xournalpp.github.io");
    link.font = QStringLiteral("Sans");
    link.size = 16;
    link.matrix = {1, 0, 0, 1, 40, 40};
    link.color = QColor(0x00, 0x40, 0xc0);
    elements.emplace_back(link);

    TextElement turned;
    turned.text = QStringLiteral("Turned text");
    turned.font = QStringLiteral("Sans");
    turned.size = 18;
    turned.matrix = matrixOf(QTransform().translate(220, 60).rotate(30));
    elements.emplace_back(turned);

    TextElement scaled;
    scaled.text = QStringLiteral("Scaled text");
    scaled.font = QStringLiteral("Sans");
    scaled.size = 12;
    scaled.matrix = matrixOf(QTransform().translate(40, 120).scale(2, 2));
    elements.emplace_back(scaled);

    QImage picture(80, 40, QImage::Format_RGB32);
    picture.fill(QColor(0x20, 0x90, 0x40));
    {
        QPainter p(&picture);
        p.fillRect(0, 0, 20, 40, QColor(0xd0, 0x30, 0x30));
    }
    ImageElement image;
    image.image = picture;
    QBuffer buffer(&image.data);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    image.naturalSize = QSizeF(80, 40);
    image.matrix = matrixOf(QTransform().translate(40, 220).scale(1.5, 2));
    image.rect = QRectF(40, 220, 120, 80);
    elements.emplace_back(image);

    ImageElement turnedImage = image;
    turnedImage.matrix = matrixOf(QTransform().translate(260, 260).rotate(-40));
    elements.emplace_back(turnedImage);
    return doc;
}

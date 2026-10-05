/*
 * Qournal
 *
 * Reader for .xopp/.xoj files (gzipped XML)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QString>

#include "Document.h"

/**
 * Loads strokes, texts, images and page backgrounds. Other elements (e.g. audio attachments) are ignored.
 * @return false on failure, with a message in *error
 */
bool loadXopp(const QString& path, Document& doc, QString* error);

/**
 * Reads a document from memory: the content of a .xopp file, compressed or not.
 * @param path the place of the file, to find the files it refers to and for messages; can be empty
 */
bool loadXoppData(const QByteArray& data, const QString& path, Document& doc, QString* error);

/**
 * Decodes the embedded file of an image or formula (PNG, JPEG, ..., or PDF) for showing it, and sets its natural
 * size if it is not known yet. The image stays empty if the data cannot be decoded.
 */
void decodeImageData(ImageElement& image);

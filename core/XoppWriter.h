/*
 * Qournal
 *
 * Writer for .xopp files (gzipped XML)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QString>

#include "Document.h"

/// The uncompressed XML of the document
QByteArray writeXoppXml(const Document& doc);

/**
 * Writes the document to path. Files attached to the document (background PDF or images stored next
 * to the .xopp file) are copied along if the document is saved to a new location.
 * @return false on failure, with a message in *error
 */
bool saveXopp(const QString& path, const Document& doc, QString* error);

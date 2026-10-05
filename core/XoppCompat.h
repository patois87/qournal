/*
 * Qournal
 *
 * Documents for Xournal++ 1.3.8 and earlier, which refuse files of format 5: what only format 5 can hold is turned
 * into what looks the same in format 4
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QStringList>

#include "Document.h"

namespace XoppCompat {

/// Whether the document has something that only format 5 can hold: links, turned texts and images
bool needsFormat5(const Document& doc);

/// What toFormat4() would change in the document: a line for each kind of change, for telling the user
QStringList format4Changes(const Document& doc);

/**
 * The document as format 4 can hold it. Links become texts (their address is lost); texts that are only moved or
 * scaled become plain texts; images that are only moved or scaled become plain images. Turned or mirrored texts
 * and images become images of how they look, which cannot be edited as texts or formulas any more.
 */
Document toFormat4(const Document& doc);

}  // namespace XoppCompat

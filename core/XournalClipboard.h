/*
 * Qournal
 *
 * The clipboard format of Xournal++ ("application/xournal"), so that selections can be copied between the two
 * applications. Ported from Xournal++ 1.3 (ObjectOutputStream, ObjectInputStream and the serialize functions of
 * the elements and of EditSelection).
 *
 * The format is a dump of the memory of Xournal++: numbers in the byte order and with the sizes of the machine
 * (64 bit, little endian on everything Xournal++ is released for), without a version of its own. Xournal++ changes
 * it between versions: what is written here is read by 1.3, and what 1.3 writes is read here. Other versions are
 * refused if they are recognised, and may give nonsense if not.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <vector>

#include "Document.h"

namespace XournalClipboard {

/// The name of the format on the clipboard
QString mimeType();

/**
 * The elements as Xournal++ 1.3 puts them on the clipboard. Links are left out, and so is what 1.3 does not
 * know of an element: the alignment and wrapping of texts, the rotation of images.
 */
QByteArray write(const std::vector<Element>& elements);

/// Reads what Xournal++ 1.3 put on the clipboard. Returns false, with a reason, if the data cannot be read
bool read(const QByteArray& data, std::vector<Element>& elements, QString* error = nullptr);

}  // namespace XournalClipboard

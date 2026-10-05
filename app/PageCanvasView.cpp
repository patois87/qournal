/*
 * Qournal
 *
 * The part of PageCanvas with the settings of the view that Xournal++ has: scroll bars, space around the pages,
 * unlimited scrolling, the resolution of the screen, pages that are appended by themselves, colours
 *
 * @license GNU GPLv2 or later
 */

#include <QDateTime>
#include <QFileInfo>

#include "PageCanvas.h"

QRectF PageCanvas::scrollView() const {
    const QRectF bounds = scrollBounds();
    if (bounds.isEmpty() || m_scale <= 0) {
        return QRectF(0, 0, 1, 1);
    }
    const QRectF view(m_origin, size() / m_scale);
    return QRectF((view.x() - bounds.x()) / bounds.width(), (view.y() - bounds.y()) / bounds.height(),
                  std::min(view.width() / bounds.width(), 1.0), std::min(view.height() / bounds.height(), 1.0));
}

void PageCanvas::scrollToFraction(double x, double y) {
    const QRectF bounds = scrollBounds();
    m_origin = bounds.topLeft() + QPointF(x * bounds.width(), y * bounds.height());
    clampView();
    updateCurrentPage();
    viewMoved();
}

namespace {

template <typename T>
bool change(T& member, const T& value) {
    if (member == value) {
        return false;
    }
    member = value;
    return true;
}

}  // namespace

void PageCanvas::setSpaceAbove(double space) {
    if (change(m_space, QMarginsF(m_space.left(), std::max(space, 0.0), m_space.right(), m_space.bottom()))) {
        clampView();
        viewMoved();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setSpaceBelow(double space) {
    if (change(m_space, QMarginsF(m_space.left(), m_space.top(), m_space.right(), std::max(space, 0.0)))) {
        clampView();
        viewMoved();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setSpaceLeft(double space) {
    if (change(m_space, QMarginsF(std::max(space, 0.0), m_space.top(), m_space.right(), m_space.bottom()))) {
        clampView();
        viewMoved();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setSpaceRight(double space) {
    if (change(m_space, QMarginsF(m_space.left(), m_space.top(), std::max(space, 0.0), m_space.bottom()))) {
        clampView();
        viewMoved();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setUnlimitedScrolling(bool unlimited) {
    if (change(m_unlimitedScrolling, unlimited)) {
        clampView();
        viewMoved();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setDisplayDpi(double dpi) {
    dpi = std::clamp(dpi, 20.0, 1000.0);
    const double zoomBefore = zoom();
    if (change(m_displayDpi, dpi)) {
        // The zoom stays what it was called: the pages change their size
        zoomTo(zoomBefore);
        emit viewSettingsChanged();
    }
}

void PageCanvas::setAppendPage(AppendPage mode) {
    if (change(m_appendPage, mode)) {
        emit viewSettingsChanged();
    }
}

void PageCanvas::setSelectionColor(const QColor& color) {
    if (color.isValid() && change(m_selectionColor, color)) {
        updateView();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setPositionColor(const QColor& color) {
    if (color.isValid() && change(m_positionColor, color)) {
        updateView();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setPositionRadius(double radius) {
    if (change(m_positionRadius, std::clamp(radius, 1.0, 500.0))) {
        updateView();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setPositionBorderColor(const QColor& color) {
    if (color.isValid() && change(m_positionBorderColor, color)) {
        updateView();
        emit viewSettingsChanged();
    }
}

void PageCanvas::setPositionBorderWidth(double width) {
    if (change(m_positionBorderWidth, std::clamp(width, 0.0, 100.0))) {
        updateView();
        emit viewSettingsChanged();
    }
}

// Pages that are appended by themselves, as in Xournal++

void PageCanvas::appendPageIfWritten(EditGroup& group) {
    if (m_appendPage != AppendWhenWritten || m_appending || m_doc.pages.empty()) {
        return;
    }
    const int last = pageCount() - 1;
    const bool written = std::any_of(group.begin(), group.end(), [last](const Edit& edit) {
        const auto* element = std::get_if<ElementEdit>(&edit);
        return element && element->added && element->page == last;
    });
    if (!written) {
        return;
    }
    // Part of the same step: undoing the writing takes the page away again
    m_appending = true;
    const PageEdit edit{true, last + 1, newPageAfter(last)};
    std::vector<EditEffect> effects{applyEdit(edit, false)};
    group.push_back(edit);
    applyEffects(effects);
    m_appending = false;
}

void PageCanvas::appendPageIfScrolledToEnd() {
    if (m_appendPage != AppendWhenScrolledToEnd || m_appending || m_presentationMode || m_doc.pages.empty() ||
        m_scale <= 0) {
        return;
    }
    const Page& last = m_doc.pages.back();
    const bool empty = std::all_of(last.layers.begin(), last.layers.end(),
                                   [](const Layer& layer) { return layer.elements.empty(); });
    const QRectF bounds = scrollBounds();
    const double viewBottom = m_origin.y() + height() / m_scale;
    if (empty || bounds.height() <= height() / m_scale || viewBottom < bounds.bottom() - 1) {
        return;
    }
    m_appending = true;
    insertPage(pageCount());
    m_appending = false;
}

// Files, as the settings of Xournal++ name them

QUrl PageCanvas::annotationFileFor(const QUrl& pdf) const {
    if (!pdf.isLocalFile()) {
        return {};
    }
    const QFileInfo info(pdf.toLocalFile());
    const QString withoutSuffix = info.path() + u'/' + info.completeBaseName();
    for (const QString& candidate: {info.filePath() + QStringLiteral(".xopp"), withoutSuffix + QStringLiteral(".xopp"),
                                    info.filePath() + QStringLiteral(".xoj"), withoutSuffix + QStringLiteral(".xoj")}) {
        if (QFileInfo::exists(candidate)) {
            return QUrl::fromLocalFile(candidate);
        }
    }
    return {};
}

QString PageCanvas::nameFromPattern(const QString& pattern) const {
    const QDateTime now = QDateTime::currentDateTime();
    // The name of the PDF that is annotated, else that of the document
    QString name = QFileInfo(m_doc.pdfPath.isEmpty() ? m_filePath : m_doc.pdfPath).completeBaseName();
    if (name.isEmpty()) {
        name = m_title;
    }
    QString result;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        if (c != u'%' || i + 1 >= pattern.size()) {
            result += c;
            continue;
        }
        const QChar field = pattern[++i];
        if (field == u'{') {
            const qsizetype end = pattern.indexOf(u'}', i);
            const QString key = end > i ? pattern.mid(i + 1, end - i - 1) : QString();
            result += key == u"name" ? name : QString();
            i = end > i ? end : i;
            continue;
        }
        switch (field.unicode()) {
            case 'Y':
                result += now.toString(QStringLiteral("yyyy"));
                break;
            case 'y':
                result += now.toString(QStringLiteral("yy"));
                break;
            case 'm':
                result += now.toString(QStringLiteral("MM"));
                break;
            case 'd':
                result += now.toString(QStringLiteral("dd"));
                break;
            case 'H':
                result += now.toString(QStringLiteral("HH"));
                break;
            case 'M':
                result += now.toString(QStringLiteral("mm"));
                break;
            case 'S':
                result += now.toString(QStringLiteral("ss"));
                break;
            case 'F':
                result += now.toString(QStringLiteral("yyyy-MM-dd"));
                break;
            case 'T':
                result += now.toString(QStringLiteral("HH:mm:ss"));
                break;
            case 'b':
                result += now.toString(QStringLiteral("MMM"));
                break;
            case 'B':
                result += now.toString(QStringLiteral("MMMM"));
                break;
            case 'a':
                result += now.toString(QStringLiteral("ddd"));
                break;
            case 'A':
                result += now.toString(QStringLiteral("dddd"));
                break;
            case '%':
                result += u'%';
                break;
            default:
                result += u'%';
                result += field;
                break;
        }
    }
    // Not a path: characters that are not allowed in file names on some systems are replaced
    for (const QChar bad: {u'/', u'\\', u':', u'*', u'?', u'"', u'<', u'>', u'|'}) {
        result.replace(bad, u'-');
    }
    return result;
}

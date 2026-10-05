/*
 * Qournal
 *
 * The part of PageCanvas that creates and edits texts, images, links and LaTeX formulas
 *
 * @license GNU GPLv2 or later
 */

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QQuickTextDocument>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>

#include "PageCanvas.h"
#include "Renderer.h"
#include "TextBlock.h"
#include "XoppLoader.h"

namespace {

/// The editor lays out the text at this size, as the renderer does; the matrix of the editor scales it
constexpr double EDITOR_FONT_SIZE = 100;

// Indices of the kinds of elements in the variant
constexpr int KIND_TEXT = 1;
constexpr int KIND_IMAGE = 2;
constexpr int KIND_LINK = 3;

constexpr double MAX_IMAGE_PAGE_FRACTION = 0.9;

TextStyle styleOf(const TextElement& text) {
    return TextStyle{text.font, text.size, text.align, text.wrap, text.justify, false};
}

bool sameText(const TextElement& a, const TextElement& b) {
    return a.text == b.text && a.font == b.font && a.size == b.size && a.color == b.color && a.align == b.align &&
           a.wrap == b.wrap;
}

}  // namespace

// The style of texts

QString PageCanvas::textFamily() const { return TextFont::family(m_textFont); }

bool PageCanvas::textBold() const { return TextFont::isBold(m_textFont); }

bool PageCanvas::textItalic() const { return TextFont::isItalic(m_textFont); }

void PageCanvas::setTextFamily(const QString& family) {
    applyTextStyle([&](QString& font, double&, QString&) {
        font = TextFont::toDescription(family, TextFont::isBold(font), TextFont::isItalic(font));
    });
}

void PageCanvas::setTextBold(bool bold) {
    applyTextStyle([&](QString& font, double&, QString&) {
        font = TextFont::toDescription(TextFont::family(font), bold, TextFont::isItalic(font));
    });
}

void PageCanvas::setTextItalic(bool italic) {
    applyTextStyle([&](QString& font, double&, QString&) {
        font = TextFont::toDescription(TextFont::family(font), TextFont::isBold(font), italic);
    });
}

void PageCanvas::setTextSize(double size) {
    if (size > 0) {
        applyTextStyle([&](QString&, double& s, QString&) { s = size; });
    }
}

void PageCanvas::setTextAlign(const QString& align) {
    applyTextStyle([&](QString&, double&, QString& a) { a = align; });
}

void PageCanvas::setTextJustify(bool justify) {
    m_textJustify = justify;
    emit textStyleChanged();
    setTextEditElement([&](TextElement& text) { text.justify = justify; });
    if (m_selection.active) {
        const auto& elements = m_doc.pages[static_cast<size_t>(m_selection.page)]
                                       .layers[static_cast<size_t>(m_selection.layer)]
                                       .elements;
        const bool hasText = std::any_of(m_selection.indices.begin(), m_selection.indices.end(),
                                         [&](size_t index) { return elements[index].index() == KIND_TEXT; });
        if (hasText) {
            modifySelection([&](Element& element) {
                if (auto* text = std::get_if<TextElement>(&element)) {
                    text->justify = justify;
                }
            });
        }
    }
}

/// Changes the style of new texts, of the text that is being edited and of the selected texts and links
void PageCanvas::applyTextStyle(const std::function<void(QString& font, double& size, QString& align)>& modify) {
    modify(m_textFont, m_textSize, m_textAlign);
    emit textStyleChanged();

    setTextEditElement([&](TextElement& text) { modify(text.font, text.size, text.align); });

    if (m_selection.active) {
        const auto& layers = m_doc.pages[static_cast<size_t>(m_selection.page)].layers;
        const auto& elements = layers[static_cast<size_t>(m_selection.layer)].elements;
        const bool hasText = std::any_of(m_selection.indices.begin(), m_selection.indices.end(), [&](size_t index) {
            return elements[index].index() == KIND_TEXT || elements[index].index() == KIND_LINK;
        });
        if (hasText) {
            modifySelection([&](Element& element) {
                if (auto* text = std::get_if<TextElement>(&element)) {
                    modify(text->font, text->size, text->align);
                } else if (auto* link = std::get_if<LinkElement>(&element)) {
                    modify(link->font, link->size, link->align);
                }
            });
        }
    }
}

// Editing a text

QFont PageCanvas::textEditFont() const {
    QFont font = TextFont::fromDescription(m_textEdit.element.font);
    font.setPixelSize(static_cast<int>(EDITOR_FONT_SIZE));
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

double PageCanvas::textEditWrap() const {
    const TextElement& text = m_textEdit.element;
    return text.wrap < 0 || text.size <= 0 ? -1 : text.wrap * EDITOR_FONT_SIZE / text.size;
}

double PageCanvas::textEditLineHeight() const {
    const TextElement& text = m_textEdit.element;
    if (text.size <= 0) {
        return EDITOR_FONT_SIZE;
    }
    return TextBlock(QString(), styleOf(text)).lineHeight() * EDITOR_FONT_SIZE / text.size;
}

QMatrix4x4 PageCanvas::textEditMatrix() const {
    if (!m_textEdit.active || m_textEdit.page >= m_pageRects.size()) {
        return {};
    }
    const TextElement& text = m_textEdit.element;
    const double k = text.size / EDITOR_FONT_SIZE;
    const QTransform onPage =
            text.matrix ? toTransform(*text.matrix) : QTransform::fromTranslate(text.pos.x(), text.pos.y());
    const QPointF offset = m_pageRects[m_textEdit.page].topLeft() - m_origin;
    // From the units of the editor to the page, then to the view
    return QMatrix4x4(QTransform::fromScale(k, k) * onPage * QTransform::fromTranslate(offset.x(), offset.y()) *
                      QTransform::fromScale(m_scale, m_scale));
}

QRectF PageCanvas::textEditViewRect() const {
    if (!m_textEdit.active) {
        return {};
    }
    const TextElement& text = m_textEdit.element;
    const QSizeF size = TextBlock(text.text, styleOf(text)).size();
    const QRectF local(0, 0, std::max(size.width(), 40 * text.size / EDITOR_FONT_SIZE) * EDITOR_FONT_SIZE / text.size,
                       size.height() * EDITOR_FONT_SIZE / text.size);
    constexpr double MARGIN_PX = 12;  // the frame and the handle for the width
    return textEditMatrix().toTransform().mapRect(local).adjusted(-MARGIN_PX, -MARGIN_PX, MARGIN_PX, MARGIN_PX);
}

void PageCanvas::setTextEditText(const QString& text) {
    // No notification: the editor is where the text comes from
    m_textEdit.element.text = text;
}

void PageCanvas::setTextEditWrap(double width) {
    setTextEditElement([&](TextElement& text) { text.wrap = width < 0 ? -1 : width * text.size / EDITOR_FONT_SIZE; });
}

void PageCanvas::setTextEditElement(const std::function<void(TextElement&)>& modify) {
    if (m_textEdit.active) {
        modify(m_textEdit.element);
        emit textEditChanged();
        emit textEditMatrixChanged();
    }
}

void PageCanvas::prepareTextDocument(QQuickTextDocument* document) const {
    if (!document || !document->textDocument()) {
        return;
    }
    // The distance of the lines Pango would use, see TextBlock
    QTextBlockFormat format;
    format.setLineHeight(textEditLineHeight(), QTextBlockFormat::FixedHeight);
    QTextCursor cursor(document->textDocument());
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(format);
}

std::optional<std::pair<int, size_t>> PageCanvas::elementOfKindAt(int pageIndex, const QPointF& pagePos,
                                                                  int kind) const {
    const Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];
    // On the layer that is drawn on, or on all visible layers from the top down
    const int active = page.activeLayer();
    for (int l = m_selectAllLayers ? static_cast<int>(page.layers.size()) - 1 : active; l >= 0; --l) {
        const Layer& layer = page.layers[static_cast<size_t>(l)];
        for (size_t i = layer.elements.size(); layer.visible && i-- > 0;) {
            if (static_cast<int>(layer.elements[i].index()) == kind &&
                Selection::distanceTo(layer.elements[i], pagePos) == 0) {
                return std::make_pair(l, i);
            }
        }
        if (!m_selectAllLayers) {
            break;
        }
    }
    return std::nullopt;
}

void PageCanvas::textPress(const PointerInput& input) {
    const QPointF pagePos = viewToPage(m_curPage, input.pos);
    const Page& page = m_doc.pages[static_cast<size_t>(m_curPage)];

    // On a text: edit it
    if (const auto found = elementOfKindAt(m_curPage, pagePos, KIND_TEXT)) {
        const auto& element = page.layers[static_cast<size_t>(found->first)].elements[found->second];
        startTextEdit(m_curPage, found->first, found->second, std::get<TextElement>(element));
        return;
    }

    // Else a new text, whose first line is where the pointer is
    TextElement text;
    text.font = m_textFont;
    text.size = m_textSize;
    text.align = m_textAlign;
    text.justify = m_textJustify;
    text.color = m_toolStates[Text].color;
    text.audio = currentAudio();
    text.pos = pagePos - QPointF(0, TextBlock(QString(), styleOf(text)).lineHeight() / 2);
    startTextEdit(m_curPage, activeLayer(m_curPage), std::nullopt, text);
}

void PageCanvas::startTextEdit(int page, int layer, std::optional<size_t> index, const TextElement& element) {
    m_textEdit.active = true;
    m_textEdit.page = page;
    m_textEdit.layer = layer;
    m_textEdit.index = index;
    m_textEdit.element = element;
    if (index) {
        // The tiles are rendered without the text while the editor shows it
        markDirty(page, {});
    }
    emit textEditChanged();
    emit textEditMatrixChanged();
}

void PageCanvas::finishTextEdit() {
    if (!m_textEdit.active) {
        return;
    }
    const TextEditState edit = m_textEdit;
    m_textEdit = TextEditState();
    emit textEditChanged();
    if (edit.page < 0 || edit.page >= pageCount()) {
        return;
    }

    Page& page = m_doc.pages[static_cast<size_t>(edit.page)];
    if (!edit.index) {
        // A new text; without content nothing happens
        if (edit.element.text.isEmpty()) {
            return;
        }
        if (page.layers.empty()) {
            page.layers.emplace_back();
        }
        const int layer = std::clamp(edit.layer, 0, static_cast<int>(page.layers.size()) - 1);
        const size_t index = page.layers[static_cast<size_t>(layer)].elements.size();
        perform({ElementEdit{true, edit.page, layer, index, edit.element}});
        return;
    }

    const Element& before = page.layers[static_cast<size_t>(edit.layer)].elements[*edit.index];
    if (edit.element.text.isEmpty()) {
        // A text without content is removed
        perform({ElementEdit{false, edit.page, edit.layer, *edit.index, before}});
    } else if (!sameText(std::get<TextElement>(before), edit.element)) {
        perform({ElementReplaceEdit{edit.page, {{edit.layer, *edit.index, before, edit.element}}}});
    } else {
        markDirty(edit.page, {});  // show it again
    }
}

// Images

ImageElement PageCanvas::imageElement(const QImage& image, const QByteArray& data, const Page& page) {
    ImageElement element;
    // Files that every version of Xournal++ can read are embedded as they are, others as PNG
    const bool png = data.startsWith("\x89PNG");
    const bool jpeg = data.startsWith("\xff\xd8");
    if (png || jpeg) {
        element.data = data;
    } else {
        QBuffer buffer(&element.data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    element.image = image;
    element.naturalSize = image.size();
    // One pixel per point, but not larger than the page
    const double scale = std::min({1.0, MAX_IMAGE_PAGE_FRACTION * page.width / image.width(),
                                   MAX_IMAGE_PAGE_FRACTION * page.height / image.height()});
    element.rect = QRectF(QPointF(0, 0), QSizeF(image.size()) * scale);
    return element;
}

bool PageCanvas::insertImage(const QUrl& url) { return insertImageAt(url, QPointF(-1, -1)); }

bool PageCanvas::insertImageAt(const QUrl& url, const QPointF& viewPos) {
    if (m_doc.pages.empty()) {
        return false;
    }
    // Non-file URLs (e.g. content:// on Android) are passed on as they are: QFile understands them there
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit loadFailed(tr("Could not open \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray data = file.readAll();
    const QImage image = QImage::fromData(data);
    if (image.isNull()) {
        emit loadFailed(tr("\"%1\" is not an image that can be read").arg(path));
        return false;
    }
    finishInput();

    const int page = contains(viewPos) ? pageAt(viewToWorld(viewPos)) : -1;
    const int target = page >= 0 ? page : std::clamp(m_currentPage, 0, pageCount() - 1);
    std::vector<Element> elements;
    elements.emplace_back(imageElement(image, data, m_doc.pages[static_cast<size_t>(target)]));
    if (page >= 0) {
        pasteElements(std::move(elements), page, viewToPage(page, viewPos));
    } else {
        pasteElements(std::move(elements));
    }
    return true;
}

bool PageCanvas::insertPicture(const QImage& image) {
    if (m_doc.pages.empty() || image.isNull()) {
        return false;
    }
    finishInput();
    const Page& page = m_doc.pages[static_cast<size_t>(std::clamp(m_currentPage, 0, pageCount() - 1))];
    ImageElement element = imageElement(image, QByteArray(), page);
    const QSizeF shown = QSizeF(image.size()) / std::max(1.0, image.devicePixelRatio());
    if (shown.width() < element.rect.width()) {
        element.rect.setSize(shown);
    }
    std::vector<Element> elements;
    elements.emplace_back(std::move(element));
    pasteElements(std::move(elements));
    return true;
}

void PageCanvas::insertTextAt(const QString& text, const QPointF& viewPos) {
    if (text.isEmpty() || m_doc.pages.empty()) {
        return;
    }
    finishInput();
    TextElement element;
    element.text = text;
    element.font = m_textFont;
    element.size = m_textSize;
    element.align = m_textAlign;
    element.color = m_toolStates[Text].color;
    std::vector<Element> elements;
    elements.emplace_back(std::move(element));

    const int page = contains(viewPos) ? pageAt(viewToWorld(viewPos)) : -1;
    if (page >= 0) {
        pasteElements(std::move(elements), page, viewToPage(page, viewPos));
    } else {
        pasteElements(std::move(elements));
    }
}

// Links

void PageCanvas::linkPress(const PointerInput& input) {
    const QPointF pagePos = viewToPage(m_curPage, input.pos);
    const Page& page = m_doc.pages[static_cast<size_t>(m_curPage)];
    m_insertTarget = InsertTarget{m_curPage, activeLayer(m_curPage), std::nullopt, pagePos};

    if (const auto found = elementOfKindAt(m_curPage, pagePos, KIND_LINK)) {
        m_insertTarget.layer = found->first;
        m_insertTarget.index = found->second;
        const auto& elements = page.layers[static_cast<size_t>(found->first)].elements;
        const auto& link = std::get<LinkElement>(elements[found->second]);
        emit linkRequested(link.text, link.url, true);
    } else {
        emit linkRequested(QString(), QString(), false);
    }
}

void PageCanvas::applyLink(const QString& text, const QString& url) {
    const InsertTarget target = m_insertTarget;
    m_insertTarget = InsertTarget();
    if (target.page < 0 || target.page >= pageCount() || url.trimmed().isEmpty()) {
        return;
    }
    finishInput();
    Page& page = m_doc.pages[static_cast<size_t>(target.page)];
    const QString shownText = text.isEmpty() ? url.trimmed() : text;

    if (target.index) {
        const Element& before = page.layers[static_cast<size_t>(target.layer)].elements[*target.index];
        LinkElement link = std::get<LinkElement>(before);
        if (link.text == shownText && link.url == url.trimmed()) {
            return;
        }
        link.text = shownText;
        link.url = url.trimmed();
        perform({ElementReplaceEdit{target.page, {{target.layer, *target.index, before, link}}}});
        return;
    }

    LinkElement link;
    link.text = shownText;
    link.url = url.trimmed();
    link.font = m_textFont;
    link.size = m_textSize;
    link.align = m_textAlign;
    link.color = m_toolStates[Link].color;
    link.matrix = {1, 0, 0, 1, target.pos.x(), target.pos.y()};
    if (page.layers.empty()) {
        page.layers.emplace_back();
    }
    const int layer = std::clamp(target.layer, 0, static_cast<int>(page.layers.size()) - 1);
    perform({ElementEdit{true, target.page, layer, page.layers[static_cast<size_t>(layer)].elements.size(), link}});
}

void PageCanvas::removeLink() {
    const InsertTarget target = m_insertTarget;
    m_insertTarget = InsertTarget();
    if (target.page < 0 || target.page >= pageCount() || !target.index) {
        return;
    }
    finishInput();
    const auto& layers = m_doc.pages[static_cast<size_t>(target.page)].layers;
    const auto& elements = layers[static_cast<size_t>(target.layer)].elements;
    perform({ElementEdit{false, target.page, target.layer, *target.index, elements[*target.index]}});
}

// LaTeX formulas

void PageCanvas::latexPress(const PointerInput& input) {
    const QPointF pagePos = viewToPage(m_curPage, input.pos);
    const Page& page = m_doc.pages[static_cast<size_t>(m_curPage)];
    m_insertTarget = InsertTarget{m_curPage, activeLayer(m_curPage), std::nullopt, pagePos};

    if (const auto found = elementOfKindAt(m_curPage, pagePos, KIND_IMAGE)) {
        const auto& elements = page.layers[static_cast<size_t>(found->first)].elements;
        const auto& image = std::get<ImageElement>(elements[found->second]);
        if (image.tex) {
            m_insertTarget.layer = found->first;
            m_insertTarget.index = found->second;
            emit latexRequested(image.texSource, true);
            return;
        }
    }
    emit latexRequested(QStringLiteral("x^2"), false);
}

void PageCanvas::applyLatex(const QString& source, const QByteArray& pdf) {
    const InsertTarget target = m_insertTarget;
    m_insertTarget = InsertTarget();
    if (target.page < 0 || target.page >= pageCount()) {
        return;
    }

    ImageElement formula;
    formula.tex = true;
    formula.texSource = source;
    formula.data = pdf;
    decodeImageData(formula);
    if (!formula.naturalSize.isValid() || formula.naturalSize.isEmpty()) {
        emit loadFailed(tr("The formula that LaTeX produced could not be read"));
        return;
    }
    finishInput();
    Page& page = m_doc.pages[static_cast<size_t>(target.page)];

    if (target.index) {
        // The new formula takes the place of the old one, at the same scale
        const Element& before = page.layers[static_cast<size_t>(target.layer)].elements[*target.index];
        const ImageElement& old = std::get<ImageElement>(before);
        if (old.matrix) {
            formula.matrix = old.matrix;
            formula.rect = toTransform(*old.matrix).mapRect(QRectF(QPointF(0, 0), formula.naturalSize));
        } else {
            const bool known = old.naturalSize.isValid() && !old.naturalSize.isEmpty();
            const double sx = known ? old.rect.width() / old.naturalSize.width() : 1.0;
            const double sy = known ? old.rect.height() / old.naturalSize.height() : 1.0;
            formula.rect = QRectF(old.rect.topLeft(),
                                  QSizeF(formula.naturalSize.width() * sx, formula.naturalSize.height() * sy));
        }
        perform({ElementReplaceEdit{target.page, {{target.layer, *target.index, before, formula}}}});
        return;
    }

    formula.rect = QRectF(target.pos, formula.naturalSize);
    if (page.layers.empty()) {
        page.layers.emplace_back();
    }
    const int layer = std::clamp(target.layer, 0, static_cast<int>(page.layers.size()) - 1);
    perform({ElementEdit{true, target.page, layer, page.layers[static_cast<size_t>(layer)].elements.size(), formula}});
}

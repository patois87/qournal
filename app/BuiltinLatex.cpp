#include "BuiltinLatex.h"

#ifdef HAVE_MICROTEX

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QRawFont>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtMath>
#include <memory>

#include "core/formula.h"
#include "core/parser.h"
#include "graphic/graphic.h"

#include "latex.h"
#include "render.h"

namespace {
// The formula is laid out with 100 units for the font size and drawn with 720 units per inch: 10 pt, and its
// size, which MicroTeX gives in whole units, is exact to a tenth of a point
constexpr float TEXT_SIZE = 100;
constexpr int UNITS_PER_INCH = 720;
constexpr int BORDER = 50;
// The widest line, as the text width of the template of Xournal++ (345 pt)
constexpr int MAX_WIDTH = 3450;
// The size the outlines of glyphs are taken at, without hinting; they are scaled to the size that is drawn
constexpr qreal OUTLINE_SIZE = 1000;
// Where the files of MicroTeX are in the resources of the application
const QString RESOURCES = QStringLiteral(":/microtex");

QString textOf(const std::wstring& text) {
    QString result = QString::fromStdWString(text);
    // Some texts come with a zero at the end
    const qsizetype end = result.indexOf(QChar(u'\0'));
    return end < 0 ? result : result.left(end);
}

/** A font of MicroTeX: one of its own font files, or a font of the system for text it has no glyphs for */
class FormulaFont: public tex::Font {
public:
    FormulaFont(const std::string& file, float size): m_file(QString::fromStdString(file)), m_size(size) {}
    FormulaFont(const QString& family, int style, float size): m_family(family), m_style(style), m_size(size) {}

    float getSize() const override { return m_size; }
    tex::sptr<tex::Font> deriveFont(int style) const override {
        auto font = std::make_shared<FormulaFont>(m_family, style, m_size);
        font->m_file = m_file;
        return font;
    }
    bool operator==(const tex::Font& other) const override {
        const auto& o = static_cast<const FormulaFont&>(other);
        return m_file == o.m_file && m_family == o.m_family && m_style == o.m_style && m_size == o.m_size;
    }
    bool operator!=(const tex::Font& other) const override { return !(*this == other); }

    QFont systemFont() const {
        QFont font(m_family.isEmpty() ? QStringLiteral("serif") : m_family);
        font.setPixelSize(int(OUTLINE_SIZE));
        font.setBold(m_style & tex::BOLD);
        font.setItalic(m_style & tex::ITALIC);
        font.setHintingPreference(QFont::PreferNoHinting);
        return font;
    }

    /// The outline of a text at the origin, in the units of the size of the font
    QPainterPath outline(const QString& text) const {
        QPainterPath path;
        if (m_file.isEmpty()) {
            path.addText(0, 0, systemFont(), text);
        } else {
            // The font files are read once. They are not made fonts of the application: they would show in the
            // list of fonts for texts
            static QHash<QString, QRawFont> fonts;
            auto found = fonts.find(m_file);
            if (found == fonts.end()) {
                found = fonts.insert(m_file, QRawFont(m_file, OUTLINE_SIZE, QFont::PreferNoHinting));
            }
            const QRawFont& font = found.value();
            const QList<quint32> glyphs = font.glyphIndexesForString(text);
            const QList<QPointF> advances = font.advancesForGlyphIndexes(glyphs);
            QPointF pen;
            for (qsizetype i = 0; i < glyphs.size(); ++i) {
                path.addPath(font.pathForGlyph(glyphs[i]).translated(pen));
                pen += advances.value(i);
            }
        }
        const qreal scale = m_size / OUTLINE_SIZE;
        return QTransform::fromScale(scale, scale).map(path);
    }

    QRectF bounds(const QString& text) const {
        if (!m_file.isEmpty()) {
            return outline(text).boundingRect();
        }
        const QRectF rect = QFontMetricsF(systemFont()).boundingRect(text);
        const qreal scale = m_size / OUTLINE_SIZE;
        return QRectF(rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
    }

private:
    QString m_file;
    QString m_family;
    int m_style = tex::PLAIN;
    float m_size;
};

/** Draws what MicroTeX lays out with a QPainter. Glyphs are filled outlines */
class FormulaGraphics: public tex::Graphics2D {
public:
    /// @param bounds if given, nothing is drawn: it gets the area that would be drawn on, in the units of the
    /// device
    explicit FormulaGraphics(QPainter* painter, QRectF* bounds = nullptr):
            m_painter(painter), m_base(painter->transform()), m_bounds(bounds) {
        setColor(tex::black);
        setStroke(tex::Stroke());
        m_font = &m_defaultFont;
    }

    void setColor(tex::color c) override {
        m_color = c;
        applyPen();
    }
    tex::color getColor() const override { return m_color; }
    void setStroke(const tex::Stroke& s) override {
        m_stroke = s;
        applyPen();
    }
    const tex::Stroke& getStroke() const override { return m_stroke; }
    void setStrokeWidth(float w) override {
        m_stroke.lineWidth = w;
        applyPen();
    }
    const tex::Font* getFont() const override { return m_font; }
    void setFont(const tex::Font* font) override { m_font = static_cast<const FormulaFont*>(font); }

    void translate(float dx, float dy) override { m_painter->translate(dx, dy); }
    void scale(float sx, float sy) override {
        m_sx *= sx;
        m_sy *= sy;
        m_painter->scale(sx, sy);
    }
    void rotate(float angle) override { m_painter->rotate(qRadiansToDegrees(angle)); }
    void rotate(float angle, float px, float py) override {
        m_painter->translate(px, py);
        m_painter->rotate(qRadiansToDegrees(angle));
        m_painter->translate(-px, -py);
    }
    void reset() override {
        m_painter->setTransform(m_base);
        m_sx = m_sy = 1;
    }
    float sx() const override { return m_sx; }
    float sy() const override { return m_sy; }

    void drawChar(wchar_t c, float x, float y) override { drawText(std::wstring(1, c), x, y); }
    void drawText(const std::wstring& text, float x, float y) override {
        fill(m_font->outline(textOf(text)).translated(x, y));
    }
    void drawLine(float x1, float y1, float x2, float y2) override {
        if (!measured(QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized(), true)) {
            m_painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }
    }
    void drawRect(float x, float y, float w, float h) override {
        if (!measured(QRectF(x, y, w, h), true)) {
            m_painter->drawRect(QRectF(x, y, w, h));
        }
    }
    void fillRect(float x, float y, float w, float h) override {
        if (!measured(QRectF(x, y, w, h), false)) {
            m_painter->fillRect(QRectF(x, y, w, h), brush());
        }
    }
    void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        if (!measured(QRectF(x, y, w, h), true)) {
            m_painter->drawRoundedRect(QRectF(x, y, w, h), rx, ry);
        }
    }
    void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override {
        QPainterPath path;
        path.addRoundedRect(QRectF(x, y, w, h), rx, ry);
        fill(path);
    }

private:
    QBrush brush() const {
        return QColor(tex::color_r(m_color), tex::color_g(m_color), tex::color_b(m_color), tex::color_a(m_color));
    }
    void fill(const QPainterPath& path) {
        if (!measured(path.boundingRect(), false)) {
            m_painter->fillPath(path, brush());
        }
    }
    /// When the area is measured: adds a rectangle to it, and says that nothing is to be drawn
    bool measured(const QRectF& rect, bool stroked) {
        if (!m_bounds) {
            return false;
        }
        const double pen = stroked ? m_stroke.lineWidth / 2 : 0;
        if (rect.isValid() || stroked) {
            *m_bounds |= m_painter->transform().mapRect(rect.adjusted(-pen, -pen, pen, pen));
        }
        return true;
    }
    void applyPen() {
        const Qt::PenCapStyle cap = m_stroke.cap == tex::CAP_ROUND  ? Qt::RoundCap :
                                    m_stroke.cap == tex::CAP_SQUARE ? Qt::SquareCap :
                                                                      Qt::FlatCap;
        const Qt::PenJoinStyle join = m_stroke.join == tex::JOIN_BEVEL ? Qt::BevelJoin :
                                      m_stroke.join == tex::JOIN_ROUND ? Qt::RoundJoin :
                                                                         Qt::MiterJoin;
        QPen pen(brush(), m_stroke.lineWidth, Qt::SolidLine, cap, join);
        pen.setMiterLimit(m_stroke.miterLimit);
        m_painter->setPen(pen);
    }

    QPainter* m_painter;
    QTransform m_base;
    QRectF* m_bounds;
    FormulaFont m_defaultFont{QStringLiteral("sans-serif"), tex::PLAIN, 20};
    const FormulaFont* m_font;
    tex::color m_color = tex::black;
    tex::Stroke m_stroke;
    float m_sx = 1;
    float m_sy = 1;
};

/** A text in a font of the system */
class FormulaText: public tex::TextLayout {
public:
    FormulaText(const std::wstring& text, const tex::sptr<FormulaFont>& font): m_text(textOf(text)), m_font(font) {}

    void getBounds(tex::Rect& r) override {
        const QRectF bounds = m_font->bounds(m_text);
        r.x = float(bounds.x());
        r.y = float(bounds.y());
        r.w = float(bounds.width());
        r.h = float(bounds.height());
    }
    void draw(tex::Graphics2D& g2, float x, float y) override {
        const tex::Font* before = g2.getFont();
        g2.setFont(m_font.get());
        g2.drawText(m_text.toStdWString(), x, y);
        g2.setFont(before);
    }

private:
    QString m_text;
    tex::sptr<FormulaFont> m_font;
};

/**
 * MicroTeX reads its fonts and tables from files: they are copied from the resources of the application to a
 * folder, once for each version of the application
 */
QString resourceFolder() {
    static QString folder;
    if (!folder.isEmpty()) {
        return folder;
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    static QTemporaryDir temporary;
    if (base.isEmpty() || !QDir().mkpath(base)) {
        base = temporary.path();
    }
    const QString target = base + QStringLiteral("/microtex-") + QCoreApplication::applicationVersion();
    QDirIterator files(RESOURCES, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString source = files.next();
        const QString copy = target + source.mid(RESOURCES.size());
        if (QFileInfo(copy).size() == files.fileInfo().size() && QFileInfo::exists(copy)) {
            continue;
        }
        QDir().mkpath(QFileInfo(copy).path());
        QFile::remove(copy);
        if (!QFile::copy(source, copy)) {
            return {};
        }
        // Resources are read-only, and so are their copies: they must be replaceable by a later version
        QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner);
    }
    folder = target;
    return folder;
}

bool start(QString* error) {
    static bool started = false;
    static QString failure;
    if (!started && failure.isEmpty()) {
        const QString folder = resourceFolder();
        if (folder.isEmpty()) {
            failure = QCoreApplication::translate("BuiltinLatex", "The fonts for formulas could not be unpacked");
        } else {
            try {
                tex::LaTeX::init(QFile::encodeName(folder).toStdString());
                started = true;
            } catch (const std::exception& e) {
                failure = QString::fromUtf8(e.what());
            }
        }
    }
    if (!started && error) {
        *error = failure;
    }
    return started;
}
}  // namespace

// What MicroTeX asks its platform for
namespace tex {
Font* Font::create(const std::string& file, float size) { return new FormulaFont(file, size); }

sptr<Font> Font::_create(const std::string& name, int style, float size) {
    return std::make_shared<FormulaFont>(QString::fromStdString(name), style, size);
}

sptr<TextLayout> TextLayout::create(const std::wstring& src, const sptr<Font>& font) {
    return std::make_shared<FormulaText>(src, std::static_pointer_cast<FormulaFont>(font));
}
}  // namespace tex

bool BuiltinLatex::available() { return true; }

QByteArray BuiltinLatex::render(const QString& formula, const QColor& color, QString* error) {
    if (formula.trimmed().isEmpty()) {
        if (error) {
            *error = QCoreApplication::translate("BuiltinLatex", "The formula is empty");
        }
        return {};
    }
    if (!start(error)) {
        return {};
    }
    QByteArray pdf;
    try {
        // Not with tex::LaTeX::parse(): it shows a command it does not know in red instead of reporting it,
        // and that would go into the document
        tex::Formula parsed;
        tex::TeXParser parser(false, formula.toStdWString(), &parsed);
        parser.parse();
        const std::unique_ptr<tex::TeXRender> render(
                tex::TeXRenderBuilder()
                        .setStyle(tex::TexStyle::display)
                        .setTextSize(TEXT_SIZE)
                        .setWidth(tex::UnitType::pixel, MAX_WIDTH, tex::Alignment::left)
                        .setIsMaxWidth(true)
                        .setLineSpace(tex::UnitType::pixel, TEXT_SIZE / 3)
                        .setForeground(tex::argb(color.alpha(), color.red(), color.green(), color.blue()))
                        .build(parsed));
        if (!render || render->getWidth() <= 0 || render->getHeight() <= 0) {
            if (error) {
                *error = QCoreApplication::translate("BuiltinLatex", "The formula is empty");
            }
            return {};
        }
        // Measured first, to see how wide it is: an environment like align takes the whole width of the line and
        // puts the formula into its middle. The page gets the width of what is drawn, and the height of the
        // formula (MicroTeX cuts it to whole units)
        QRectF drawn;
        {
            QImage nothing(1, 1, QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&nothing);
            FormulaGraphics graphics(&painter, &drawn);
            render->draw(graphics, 0, 0);
        }
        if (drawn.isEmpty()) {
            if (error) {
                *error = QCoreApplication::translate("BuiltinLatex", "The formula is empty");
            }
            return {};
        }
        const QSizeF size(drawn.width() + 2 * BORDER, render->getHeight() + 1 + 2 * BORDER);
        QBuffer buffer(&pdf);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setResolution(UNITS_PER_INCH);
        writer.setPageSize(QPageSize(size * 72.0 / UNITS_PER_INCH, QPageSize::Point));
        writer.setPageMargins(QMarginsF());
        QPainter painter(&writer);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(BORDER - drawn.left(), 0);
        FormulaGraphics graphics(&painter);
        render->draw(graphics, 0, BORDER);
        painter.end();
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return {};
    }
    return pdf;
}

#else

bool BuiltinLatex::available() { return false; }

QByteArray BuiltinLatex::render(const QString&, const QColor&, QString* error) {
    if (error) {
        *error = QStringLiteral("Built without MicroTeX");
    }
    return {};
}

#endif

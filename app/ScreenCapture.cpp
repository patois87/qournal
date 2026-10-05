#include "ScreenCapture.h"

#include <QFile>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRasterWindow>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QtMath>

#include "Platform.h"

#ifdef HAVE_QTDBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

namespace {
// The desktop needs a moment to take the window off the screen (it may animate that)
constexpr int HIDE_DELAY_MS = 350;
// How long the desktop may take to capture the screen on Wayland
constexpr int PORTAL_TIMEOUT_MS = 60000;
// A smaller rectangle is a click, not a region
constexpr double MIN_REGION = 4;

bool onWayland() { return QGuiApplication::platformName().startsWith(u"wayland"); }

/** Shows the picture of one screen over that screen, and the rectangle the user drags in it */
class SelectionWindow: public QRasterWindow {
public:
    SelectionWindow(ScreenCapture* owner, QScreen* screen, const QImage& shot): m_owner(owner), m_shot(shot) {
        setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setScreen(screen);
        setGeometry(screen->geometry());
        setCursor(Qt::CrossCursor);
        setTitle(ScreenCapture::tr("Select a region of the screen"));
    }

    /// The part of the picture under a region of the window
    QImage cropped(const QRectF& region) const {
        const double sx = m_shot.width() / double(std::max(1, width()));
        const double sy = m_shot.height() / double(std::max(1, height()));
        const QRect pixels = QRectF(region.x() * sx, region.y() * sy, region.width() * sx, region.height() * sy)
                                     .toAlignedRect()
                                     .intersected(m_shot.rect());
        QImage image = m_shot.copy(pixels);
        image.setDevicePixelRatio(sx);
        return image;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRectF all(0, 0, width(), height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(all, m_shot);
        // What is not selected is darker
        const QColor dim(0, 0, 0, 96);
        if (m_region.isEmpty()) {
            painter.fillRect(all, dim);
            const QString hint = ScreenCapture::tr("Drag over the region that goes onto the page. Esc cancels.");
            QFont font = painter.font();
            font.setPointSizeF(font.pointSizeF() * 1.4);
            painter.setFont(font);
            QRectF box = painter.fontMetrics().boundingRect(hint);
            box.adjust(-16, -10, 16, 10);
            box.moveCenter(QPointF(all.center().x(), all.height() * 0.2));
            painter.fillRect(box, QColor(0, 0, 0, 200));
            painter.setPen(Qt::white);
            painter.drawText(box, Qt::AlignCenter, hint);
            return;
        }
        painter.fillRect(QRectF(0, 0, all.width(), m_region.top()), dim);
        painter.fillRect(QRectF(0, m_region.bottom(), all.width(), all.height() - m_region.bottom()), dim);
        painter.fillRect(QRectF(0, m_region.top(), m_region.left(), m_region.height()), dim);
        painter.fillRect(QRectF(m_region.right(), m_region.top(), all.width() - m_region.right(), m_region.height()),
                         dim);
        // A border that shows on light and on dark
        painter.setPen(QPen(Qt::white, 1));
        painter.drawRect(m_region);
        painter.setPen(QPen(Qt::black, 1, Qt::DashLine));
        painter.drawRect(m_region);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            m_owner->cancel();
            return;
        }
        m_origin = event->position();
        m_dragging = true;
        m_region = QRectF();
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging) {
            m_region = QRectF(m_origin, event->position()).normalized() & QRectF(0, 0, width(), height());
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!m_dragging || event->button() != Qt::LeftButton) {
            return;
        }
        m_dragging = false;
        if (m_region.width() < MIN_REGION || m_region.height() < MIN_REGION) {
            m_region = QRectF();
            update();
            return;
        }
        m_owner->select(this, m_region);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            m_owner->cancel();
        }
    }

private:
    ScreenCapture* m_owner;
    QImage m_shot;
    QPointF m_origin;
    QRectF m_region;
    bool m_dragging = false;
};
/**
 * A small bar at the top of the screen, shown while the window of the application is hidden: the user brings
 * the window to the front that is to be captured, and then says so
 */
class PrepareBar: public QRasterWindow {
public:
    PrepareBar(ScreenCapture* owner, QScreen* screen): m_owner(owner) {
        setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setTitle(ScreenCapture::tr("Capture a region of the screen"));
        m_message = ScreenCapture::tr("Bring to the front what you want to capture.");
        m_capture = ScreenCapture::tr("Capture");
        m_cancel = ScreenCapture::tr("Cancel");
        const QFontMetricsF metrics{QGuiApplication::font()};
        const double height = std::max(44.0, metrics.height() * 2.4);
        const double button = height - 2 * MARGIN;
        double x = MARGIN + metrics.horizontalAdvance(m_message) + 2 * MARGIN;
        m_captureRect = QRectF(x, MARGIN, metrics.horizontalAdvance(m_capture) + 3 * MARGIN, button);
        x = m_captureRect.right() + MARGIN;
        m_cancelRect = QRectF(x, MARGIN, metrics.horizontalAdvance(m_cancel) + 3 * MARGIN, button);
        const QSize size(qCeil(m_cancelRect.right() + MARGIN), qCeil(height));
        if (screen) {
            setScreen(screen);
            const QRect available = screen->availableGeometry();
            setGeometry(QRect(QPoint(available.center().x() - size.width() / 2, available.top() + 24), size));
        } else {
            resize(size);
        }
    }

    QRectF captureRect() const { return m_captureRect; }
    QRectF cancelRect() const { return m_cancelRect; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QPalette palette = QGuiApplication::palette();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(QRectF(0, 0, width(), height()), palette.color(QPalette::Window));
        painter.setPen(palette.color(QPalette::Mid));
        painter.drawRect(QRectF(0.5, 0.5, width() - 1, height() - 1));
        painter.setPen(palette.color(QPalette::WindowText));
        painter.drawText(QRectF(2 * MARGIN, 0, m_captureRect.left(), height()), Qt::AlignVCenter | Qt::AlignLeft,
                         m_message);
        auto button = [&](const QRectF& rect, const QString& text, bool primary) {
            painter.setPen(palette.color(QPalette::Mid));
            painter.setBrush(palette.color(primary ? QPalette::Highlight : QPalette::Button));
            painter.drawRoundedRect(rect, 4, 4);
            painter.setPen(palette.color(primary ? QPalette::HighlightedText : QPalette::ButtonText));
            painter.drawText(rect, Qt::AlignCenter, text);
        };
        button(m_captureRect, m_capture, true);
        button(m_cancelRect, m_cancel, false);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_captureRect.contains(event->position())) {
            m_owner->captureNow();
        } else if (m_cancelRect.contains(event->position())) {
            m_owner->cancel();
        }
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            m_owner->cancel();
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            m_owner->captureNow();
        }
    }

private:
    static constexpr double MARGIN = 8;

    ScreenCapture* m_owner;
    QString m_message;
    QString m_capture;
    QString m_cancel;
    QRectF m_captureRect;
    QRectF m_cancelRect;
};
}  // namespace

ScreenCapture::ScreenCapture(QObject* parent): QObject(parent) {}

ScreenCapture::~ScreenCapture() {
    qDeleteAll(m_selection);
    delete m_prepare;
}

bool ScreenCapture::available() {
    if (Platform::isMobile()) {
        return false;
    }
#ifndef HAVE_QTDBUS
    if (onWayland()) {
        return false;  // applications cannot look at the screen there, only the desktop can
    }
#endif
    return true;
}

void ScreenCapture::start(QWindow* window) {
    if (m_running || !available()) {
        return;
    }
    m_running = true;
    emit runningChanged();
    m_window = window;
    if (window) {
        m_visibility = window->visibility();
        window->hide();
    }
    // The screens are captured as they are at that moment. Before that the user can bring to the front what is
    // to be captured: a small bar waits for it
    auto* bar = new PrepareBar(this, window ? window->screen() : QGuiApplication::primaryScreen());
    m_prepare = bar;
    bar->show();
    bar->requestActivate();
}

void ScreenCapture::captureNow() {
    if (!m_running || !m_prepare) {
        return;
    }
    m_prepare->hide();
    m_prepare->deleteLater();
    m_prepare = nullptr;
    QTimer::singleShot(HIDE_DELAY_MS, this, &ScreenCapture::capture);
}

void ScreenCapture::capture() {
    if (!m_running) {
        return;
    }
    if (onWayland()) {
        captureWithPortal();
        return;
    }
    QList<QImage> shots;
    for (QScreen* screen: QGuiApplication::screens()) {
        const QImage shot = screen->grabWindow(0).toImage();
        if (shot.isNull()) {
            fail(tr("The screen could not be captured"));
            return;
        }
        shots.append(shot);
    }
    showSelection(shots);
}

void ScreenCapture::captureWithPortal() {
#ifdef HAVE_QTDBUS
    // The portal answers with a signal of a request, whose path is made of the name of this connection and a
    // token: listening starts before the call, so that the answer cannot be missed
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("qournal") + QUuid::createUuid().toString(QUuid::Id128);
    const QString sender = bus.baseService().mid(1).replace(u'.', u'_');
    m_portalRequest = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    // Without the name of the service: Qt would first ask who owns it and drop signals until it knows, and the
    // portal answers at once. The path belongs to this request alone
    bus.connect(QString(), m_portalRequest, QStringLiteral("org.freedesktop.portal.Request"),
                QStringLiteral("Response"), this, SLOT(portalResponse(uint, QVariantMap)));
    QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.Screenshot"), QStringLiteral("Screenshot"));
    call << QString() << QVariantMap{{QStringLiteral("handle_token"), token}, {QStringLiteral("interactive"), false}};
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* finished) {
        finished->deleteLater();
        if (finished->isError() && m_running) {
            fail(tr("The desktop did not capture the screen: %1").arg(finished->error().message()));
        }
    });
    // The window of the application is hidden: it must come back if the desktop never answers. The desktop may
    // ask the user for permission first, which takes a while
    QTimer::singleShot(PORTAL_TIMEOUT_MS, this, [this, request = m_portalRequest] {
        if (m_running && m_portalRequest == request) {
            m_portalRequest.clear();
            fail(tr("The desktop did not answer the request to capture the screen"));
        }
    });
#else
    fail(tr("The screen could not be captured"));
#endif
}

void ScreenCapture::portalResponse(uint response, const QVariantMap& results) {
#ifdef HAVE_QTDBUS
    QDBusConnection::sessionBus().disconnect(QString(), m_portalRequest,
                                             QStringLiteral("org.freedesktop.portal.Request"),
                                             QStringLiteral("Response"), this, SLOT(portalResponse(uint, QVariantMap)));
    m_portalRequest.clear();
    if (!m_running) {
        return;
    }
    if (response == 1) {
        cancel();  // the user said no
        return;
    }
    // The picture is a file the desktop wrote for this: it is not needed after it was read
    const QString path = QUrl(results.value(QStringLiteral("uri")).toString()).toLocalFile();
    const QImage all(path);
    if (response != 0 || all.isNull()) {
        fail(tr("The screen could not be captured"));
        return;
    }
    QFile::remove(path);
    // One picture of all screens: each screen gets its part
    const QRect desktop = QGuiApplication::primaryScreen()->virtualGeometry();
    const double scale = all.width() / double(std::max(1, desktop.width()));
    QList<QImage> shots;
    for (const QScreen* screen: QGuiApplication::screens()) {
        const QRect geometry = screen->geometry().translated(-desktop.topLeft());
        shots.append(all.copy(
                QRectF(geometry.x() * scale, geometry.y() * scale, geometry.width() * scale, geometry.height() * scale)
                        .toAlignedRect()));
    }
    showSelection(shots);
#else
    Q_UNUSED(response)
    Q_UNUSED(results)
#endif
}

void ScreenCapture::showSelection(const QList<QImage>& shots) {
    if (!m_running) {
        m_running = true;
        emit runningChanged();
    }
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (qsizetype i = 0; i < screens.size() && i < shots.size(); ++i) {
        auto* window = new SelectionWindow(this, screens[i], shots[i]);
        m_selection.append(window);
        window->showFullScreen();
    }
    if (m_selection.isEmpty()) {
        fail(tr("The screen could not be captured"));
        return;
    }
    m_selection.first()->requestActivate();
}

QList<QWindow*> ScreenCapture::selectionWindows() const { return m_selection; }

void ScreenCapture::select(QWindow* window, const QRectF& region) {
    if (!m_selection.contains(window)) {
        return;
    }
    const QImage image = static_cast<SelectionWindow*>(window)->cropped(region);
    finish();
    if (image.isNull()) {
        emit failed(tr("The screen could not be captured"));
    } else {
        emit captured(image);
    }
}

void ScreenCapture::cancel() {
    if (m_running) {
        finish();
    }
}

void ScreenCapture::fail(const QString& message) {
    finish();
    emit failed(message);
}

void ScreenCapture::finish() {
    if (m_prepare) {
        m_prepare->hide();
        m_prepare->deleteLater();
        m_prepare = nullptr;
    }
    // Not deleted at once: this is called from the events of these windows
    for (QWindow* window: std::as_const(m_selection)) {
        window->hide();
        window->deleteLater();
    }
    m_selection.clear();
    if (m_window) {
        m_window->setVisibility(m_visibility);
        m_window->raise();
        m_window->requestActivate();
    }
    m_window.clear();
    m_running = false;
    emit runningChanged();
}

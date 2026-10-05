/*
 * Qournal
 *
 * Takes a picture of a region of the screen: the window of the application gets out of the way, the screens are
 * shown as they were, and the user drags a rectangle over what should go onto the page
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QWindow>

#include <QtQml/qqmlregistration.h>

class ScreenCapture: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// Whether the screen can be captured here: not on mobile platforms, and on Wayland only through the portal
    /// of the desktop
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit ScreenCapture(QObject* parent = nullptr);
    ~ScreenCapture() override;

    static bool available();
    bool running() const { return m_running; }

    /**
     * Hides the window and shows a small bar: the user brings to the front what is to be captured and presses
     * "Capture" there (captureNow()). Then the screens are captured and a region is selected in them. The window
     * comes back afterwards
     */
    Q_INVOKABLE void start(QWindow* window);
    /// Captures the screens as they are now, see start()
    Q_INVOKABLE void captureNow();
    /// The bar that waits for captureNow(), while it is shown
    QWindow* prepareWindow() const { return m_prepare; }
    /// Ends the selection without a picture
    Q_INVOKABLE void cancel();

    /**
     * Shows the pictures of the screens, one for each of QGuiApplication::screens(), to select a region in
     * (start() does this with what it captured)
     */
    void showSelection(const QList<QImage>& shots);
    /// The windows of the selection, one for each screen
    QList<QWindow*> selectionWindows() const;
    /// The region of a window of the selection was chosen, in the coordinates of that window
    void select(QWindow* window, const QRectF& region);

signals:
    void runningChanged();
    /// The region the user selected. Its devicePixelRatio() is the one of the screen it was on
    void captured(const QImage& image);
    void failed(const QString& message);

private slots:
    /// The answer of the portal of the desktop (Wayland)
    void portalResponse(uint response, const QVariantMap& results);

private:
    void capture();
    void captureWithPortal();
    /// Closes the selection and brings the window of the application back
    void finish();
    void fail(const QString& message);

    QPointer<QWindow> m_window;
    QWindow::Visibility m_visibility = QWindow::Windowed;
    QList<QWindow*> m_selection;
    QWindow* m_prepare = nullptr;
    QString m_portalRequest;
    bool m_running = false;
};

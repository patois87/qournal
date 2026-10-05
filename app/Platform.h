/*
 * Qournal
 *
 * What differs between the platforms and has no place in Qt: access to documents on Android and iOS, and the
 * document another application asks to be opened
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QColor>
#include <QRect>
#include <QString>
#include <QUrl>
#include <functional>

class QWindow;

namespace Platform {

/**
 * Keeps the access to a document the user has chosen, so that it can be opened again in a later session (the list
 * of recent files). Needed on Android, where a document is a content:// URI the application is only lent; does
 * nothing elsewhere.
 */
void keepAccess(const QString& location);

/// The document the application was started for by another application (Android: the intent), if any
QUrl startupDocument();

/// Whether this is a platform where fingers and a pen are the usual input
bool isMobile();

/**
 * Tells the system whether the window is light behind its status bar and navigation bar, so that it draws the
 * clock and the icons there dark, or light on a dark window (Android; nothing elsewhere)
 */
void setSystemBarsOnLight(bool light);

/**
 * Whether the screen is electronic paper, as far as that can be known: Android does not tell, so the makers of
 * such devices are looked for (Onyx Boox, Bigme, Supernote, ...). QOURNAL_EINK=1 or =0 in the environment
 * says it for a device that is not known, and for trying
 */
bool isEinkDevice();

/**
 * The pen of tablets with electronic paper. Such a screen redraws slowly, so that a stroke the application draws
 * follows the pen late. The firmware of Onyx Boox devices can draw the stroke itself, at once, in a part of the
 * screen, while the application gets the events of the pen as always (android.onyx.ViewUpdateHelper, a part of
 * their Android; nothing of it is in this application). What the application draws in that part is not shown while
 * this is on: it is paused to show it.
 */
namespace EinkPen {
/// Whether this device can draw strokes itself
bool available();
/// Lets the device draw what the pen writes within a rectangle of the screen (in its pixels)
void start(const QRect& screenRect, double width, const QColor& color);
/// Shows what the application draws again; the device stops drawing strokes
void pause();
/// Ends it for good, when the application goes to the background or quits
void stop();
}  // namespace EinkPen

/// What the double tap on the Apple Pencil is to do, as chosen in the settings of iOS
enum class PencilTap { SwitchEraser, SwitchPrevious, ShowColorPalette, Ignore };

/// Calls back when the Apple Pencil is tapped twice on the window (iOS); does nothing elsewhere
void watchPencilTaps(QWindow* window, std::function<void(PencilTap)> callback);

}  // namespace Platform

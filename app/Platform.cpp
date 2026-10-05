#include "Platform.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>

namespace {

// Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
constexpr jint READ_PERMISSION = 1;
constexpr jint WRITE_PERMISSION = 2;

QJniObject context() { return QJniObject(QNativeInterface::QAndroidApplication::context()); }

/// A SecurityException is how Android says no; it must not stay pending
bool cleared() { return !QJniEnvironment().checkAndClearExceptions(); }

}  // namespace
#endif

void Platform::keepAccess(const QString& location) {
#ifdef Q_OS_ANDROID
    if (!location.startsWith(u"content://")) {
        return;
    }
    const QJniObject uri =
            QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                               QJniObject::fromString(location).object<jstring>());
    const QJniObject resolver = context().callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    if (!uri.isValid() || !resolver.isValid()) {
        cleared();
        return;
    }
    // Not every document can be written, and not every provider lends for longer than the session
    resolver.callMethod<void>("takePersistableUriPermission", "(Landroid/net/Uri;I)V", uri.object(),
                              READ_PERMISSION | WRITE_PERMISSION);
    if (!cleared()) {
        resolver.callMethod<void>("takePersistableUriPermission", "(Landroid/net/Uri;I)V", uri.object(),
                                  READ_PERMISSION);
        cleared();
    }
#else
    Q_UNUSED(location)
#endif
}

QUrl Platform::startupDocument() {
#ifdef Q_OS_ANDROID
    // "Open with" in a file manager starts the activity with the document in its intent
    const QJniObject intent = context().callObjectMethod("getIntent", "()Landroid/content/Intent;");
    if (!cleared() || !intent.isValid()) {
        return {};
    }
    const QJniObject data = intent.callObjectMethod("getDataString", "()Ljava/lang/String;");
    if (!cleared() || !data.isValid()) {
        return {};
    }
    return QUrl(data.toString());
#else
    return {};
#endif
}

#ifdef Q_OS_ANDROID
namespace {
/**
 * Android hides what is not a part of its public interface from applications, also the functions of the firmware
 * of electronic paper that are used here. HiddenApiBypass, which is in the package if it was built with it, lifts
 * that for them
 */
void allowFirmwareFunctions() {
    static const bool done = [] {
        QJniEnvironment env;
        if (const jclass bypass = env.findClass("org/lsposed/hiddenapibypass/HiddenApiBypass")) {
            const jobjectArray prefixes =
                    env->NewObjectArray(2, env->FindClass("java/lang/String"), env->NewStringUTF("Landroid/onyx/"));
            const jmethodID add = env->GetStaticMethodID(bypass, "addHiddenApiExemptions", "([Ljava/lang/String;)Z");
            if (add && prefixes) {
                env->SetObjectArrayElement(prefixes, 1, env->NewStringUTF("Lcom/android/internal/policy/DecorView;"));
                env->CallStaticBooleanMethod(bypass, add, prefixes);
            }
        }
        env.checkAndClearExceptions();
        return true;
    }();
    Q_UNUSED(done)
}
}  // namespace
#endif

void Platform::setSystemBarsOnLight(bool light) {
#ifdef Q_OS_ANDROID
    const bool eink = isEinkDevice();
    // The window is changed in the thread of the user interface of Android
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([light, eink] {
        const QJniObject window = context().callObjectMethod("getWindow", "()Landroid/view/Window;");
        const QJniObject controller =
                window.isValid() ?
                        window.callObjectMethod("getInsetsController", "()Landroid/view/WindowInsetsController;") :
                        QJniObject();
        if (controller.isValid()) {
            // APPEARANCE_LIGHT_STATUS_BARS | APPEARANCE_LIGHT_NAVIGATION_BARS (Android 11 and newer)
            constexpr jint LIGHT_BARS = 8 | 16;
            controller.callMethod<void>("setSystemBarsAppearance", "(II)V", light ? LIGHT_BARS : jint(0), LIGHT_BARS);
            cleared();
            if (eink) {
                // The firmware of Onyx Boox keeps the look the window had when it was created, in a field of its
                // own, and sets it again whenever the window is laid out: the call above is undone, and so is
                // what the theme of the window says. The clock stayed white on the white window. That field
                // gets the new value as well
                allowFirmwareFunctions();
                const QJniObject decor = window.callObjectMethod("getDecorView", "()Landroid/view/View;");
                QJniEnvironment env;
                const jclass decorClass = decor.isValid() ? env->GetObjectClass(decor.object()) : nullptr;
                const jfieldID kept = decorClass ? env->GetFieldID(decorClass, "orgAppearance", "I") : nullptr;
                if (kept) {
                    const jint before = env->GetIntField(decor.object(), kept);
                    env->SetIntField(decor.object(), kept, (before & ~LIGHT_BARS) | (light ? LIGHT_BARS : jint(0)));
                }
            }
        }
        cleared();
    });
#else
    Q_UNUSED(light)
#endif
}

bool Platform::isEinkDevice() {
    if (qEnvironmentVariableIsSet("QOURNAL_EINK")) {
        return qEnvironmentVariableIntValue("QOURNAL_EINK") != 0;
    }
#ifdef Q_OS_ANDROID
    static const bool eink = [] {
        QString maker;
        for (const char* field: {"MANUFACTURER", "BRAND"}) {
            maker +=
                    QJniObject::getStaticObjectField("android/os/Build", field, "Ljava/lang/String;").toString() + u' ';
        }
        cleared();
        maker = maker.toLower();
        for (const char* name: {"onyx", "boox", "bigme", "supernote", "ratta", "remarkable", "dasung", "meebook",
                                "boyue", "likebook", "pocketbook", "tolino", "kobo"}) {
            if (maker.contains(QLatin1StringView(name))) {
                return true;
            }
        }
        return false;
    }();
    return eink;
#else
    return false;
#endif
}

// The pen of electronic paper

#ifdef Q_OS_ANDROID
namespace {

const char* const EPD_CLASS = "android/onyx/ViewUpdateHelper";
// The states of the pen, as the firmware knows them
constexpr int PEN_STOP = 0;
constexpr int PEN_DRAWING = 2;
constexpr int PEN_PAUSE = 3;

/// The view of the window of the application, which the firmware takes rectangles relative to
QJniObject decorView() {
    const QJniObject window = context().callObjectMethod("getWindow", "()Landroid/view/Window;");
    return window.isValid() ? window.callObjectMethod("getDecorView", "()Landroid/view/View;") : QJniObject();
}

void setPenState(int state) {
    QJniObject::callStaticMethod<void>(EPD_CLASS, "setScreenHandWritingPenState", "(I)V", jint(state));
    if (!cleared()) {
        qWarning("EinkPen: setScreenHandWritingPenState(%d) failed", state);
    }
}

}  // namespace
#endif

bool Platform::EinkPen::available() {
#ifdef Q_OS_ANDROID
    static const bool found = [] {
        if (!isEinkDevice()) {
            return false;
        }
        QJniEnvironment env;
        const jclass epd = env.findClass(EPD_CLASS);
        env.checkAndClearExceptions();
        if (!epd) {
            qInfo("EinkPen: no android.onyx.ViewUpdateHelper");
            return false;
        }
        allowFirmwareFunctions();
        const bool ok = env->GetStaticMethodID(epd, "setScreenHandWritingPenState", "(I)V") != nullptr;
        env.checkAndClearExceptions();
        qInfo("EinkPen: %s", ok ? "the firmware can draw strokes" : "the functions of the firmware are not allowed");
        return ok;
    }();
    return found;
#else
    return false;
#endif
}

void Platform::EinkPen::start(const QRect& screenRect, double width, const QColor& color) {
#ifdef Q_OS_ANDROID
    if (!available()) {
        return;
    }
    const QJniObject view = decorView();
    if (!view.isValid()) {
        cleared();
        return;
    }
    QJniObject::callStaticMethod<void>(EPD_CLASS, "setStrokeWidth", "(F)V", jfloat(width));
    QJniObject::callStaticMethod<void>(EPD_CLASS, "setStrokeColor", "(I)V", jint(color.rgba()));
    QJniObject::callStaticMethod<void>(EPD_CLASS, "setStrokeStyle", "(I)V", jint(0));  // a pencil: one width
    QJniObject::callStaticMethod<void>(EPD_CLASS, "setScreenHandWritingRegionLimit", "(Landroid/view/View;IIII)V",
                                       view.object<jobject>(), jint(screenRect.left()), jint(screenRect.top()),
                                       jint(screenRect.right()), jint(screenRect.bottom()));
    if (!cleared()) {
        qWarning("EinkPen: setting the stroke or its region failed");
        return;
    }
    setPenState(PEN_DRAWING);
#else
    Q_UNUSED(screenRect)
    Q_UNUSED(width)
    Q_UNUSED(color)
#endif
}

void Platform::EinkPen::pause() {
#ifdef Q_OS_ANDROID
    if (available()) {
        setPenState(PEN_PAUSE);
    }
#endif
}

void Platform::EinkPen::stop() {
#ifdef Q_OS_ANDROID
    if (available()) {
        setPenState(PEN_STOP);
    }
#endif
}

bool Platform::isMobile() {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return true;
#else
    return false;
#endif
}

#ifndef Q_OS_IOS
// See Platform_ios.mm
void Platform::watchPencilTaps(QWindow*, std::function<void(PencilTap)>) {}
#endif

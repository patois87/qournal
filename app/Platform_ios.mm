/*
 * Qournal
 *
 * The parts of Platform that need UIKit: the double tap (and the squeeze) of the Apple Pencil. Not tried on a
 * device yet
 *
 * @license GNU GPLv2 or later
 */

#include "Platform.h"

#include <QWindow>
#include <objc/runtime.h>

#import <UIKit/UIKit.h>

API_AVAILABLE(ios(12.1))
@interface XojPencilDelegate: NSObject <UIPencilInteractionDelegate> {
@public
    std::function<void(Platform::PencilTap)> callback;
}
@end

@implementation XojPencilDelegate

/// What the user chose in the settings of iOS for the double tap
- (void)pencilInteractionDidTap:(UIPencilInteraction*)interaction {
    Platform::PencilTap tap = Platform::PencilTap::Ignore;
    switch (UIPencilInteraction.preferredTapAction) {
        case UIPencilPreferredActionSwitchEraser:
            tap = Platform::PencilTap::SwitchEraser;
            break;
        case UIPencilPreferredActionSwitchPrevious:
            tap = Platform::PencilTap::SwitchPrevious;
            break;
        case UIPencilPreferredActionShowColorPalette:
            tap = Platform::PencilTap::ShowColorPalette;
            break;
        default:
            break;
    }
    if (callback) {
        callback(tap);
    }
}

@end

namespace {
/// The interaction keeps its delegate only weakly: it is attached to the interaction to live as long as it
char delegateKey;
}  // namespace

void Platform::watchPencilTaps(QWindow* window, std::function<void(PencilTap)> callback) {
    if (!window) {
        return;
    }
    if (@available(iOS 12.1, *)) {
        UIView* view = reinterpret_cast<UIView*>(window->winId());
        if (!view) {
            return;
        }
        XojPencilDelegate* delegate = [[XojPencilDelegate alloc] init];
        delegate->callback = std::move(callback);
        UIPencilInteraction* interaction = [[UIPencilInteraction alloc] init];
        interaction.delegate = delegate;
        objc_setAssociatedObject(interaction, &delegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [view addInteraction:interaction];
    }
}

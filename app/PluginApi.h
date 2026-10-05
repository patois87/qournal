/*
 * Qournal
 *
 * The functions plugins call: the table "app" of Xournal++ (luapi_application.h there), on top of PageCanvas.
 * See plugins/luapi_application.def.lua of Xournal++ for what each function takes and returns.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QString>
#include <QVariant>
#include <functional>

#include "PluginController.h"

class PluginApi {
public:
    /// Puts the table "app" into the Lua state of a plugin
    static void open(lua_State* L, PluginController* controller, int plugin);

    /// What a function reports instead of a result: it becomes an error in Lua
    struct Error {
        QString message;
    };
    using Function = QVariantList (PluginApi::*)(const QVariantList&);
    struct Entry {
        const char* name;
        Function function;
    };
    static const Entry FUNCTIONS[];
    static const int FUNCTION_COUNT;

    PluginApi(PluginController& controller, int plugin): c(controller), m_plugin(plugin) {}

    /// Runs an action by the name it has in Xournal++. @param state its new state; invalid to activate or toggle it
    void runAction(const QString& name, const QVariant& state);
    /// The state of an action, invalid if it has none
    QVariant actionState(const QString& name) const;

private:
    PageCanvas& canvas() const;
    PluginController::Plugin& plugin() const { return c.m_plugins[static_cast<size_t>(m_plugin)]; }
    QVariant ref(const PageCanvas::ElementRef& element);
    QVariantList refs(const std::vector<PageCanvas::ElementRef>& elements);
    /// Calls a function for the elements a plugin asks for: "selection", "layer", "page" or "all"
    void forElements(const QString& scope,
                     const std::function<void(const PageCanvas::ElementRef&, const Element&, QVariantMap&)>& function,
                     QVariantList& result);
    void legacyAction(const QString& name, bool enabled);

    // Dialogs
    QVariantList msgbox(const QVariantList& args);
    QVariantList openDialog(const QVariantList& args);
    QVariantList saveAs(const QVariantList& args);
    QVariantList fileDialogSave(const QVariantList& args);
    QVariantList getFilePath(const QVariantList& args);
    QVariantList fileDialogOpen(const QVariantList& args);
    // User interface
    QVariantList registerUi(const QVariantList& args);
    QVariantList registerPlaceholder(const QVariantList& args);
    QVariantList setPlaceholderValue(const QVariantList& args);
    QVariantList getActionState(const QVariantList& args);
    QVariantList changeActionState(const QVariantList& args);
    QVariantList activateAction(const QVariantList& args);
    QVariantList uiAction(const QVariantList& args);
    QVariantList sidebarAction(const QVariantList& args);
    QVariantList layerAction(const QVariantList& args);
    QVariantList getSidebarPageNo(const QVariantList& args);
    QVariantList setSidebarPageNo(const QVariantList& args);
    QVariantList showFloatingToolbox(const QVariantList& args);
    // Tools
    QVariantList changeToolColor(const QVariantList& args);
    QVariantList getColorPalette(const QVariantList& args);
    QVariantList getToolInfo(const QVariantList& args);
    QVariantList getFonts(const QVariantList& args);
    QVariantList getFont(const QVariantList& args);
    QVariantList setFont(const QVariantList& args);
    // Document and view
    QVariantList getDocumentStructure(const QVariantList& args);
    QVariantList getPageLabel(const QVariantList& args);
    QVariantList changeCurrentPageBackground(const QVariantList& args);
    QVariantList changeBackgroundPdfPageNr(const QVariantList& args);
    QVariantList scrollToPage(const QVariantList& args);
    QVariantList scrollToPos(const QVariantList& args);
    QVariantList getScrollPos(const QVariantList& args);
    QVariantList setCurrentPage(const QVariantList& args);
    QVariantList setPageSize(const QVariantList& args);
    QVariantList setCurrentLayer(const QVariantList& args);
    QVariantList setLayerVisibility(const QVariantList& args);
    QVariantList setCurrentLayerName(const QVariantList& args);
    QVariantList setBackgroundName(const QVariantList& args);
    QVariantList getDisplayDpi(const QVariantList& args);
    QVariantList getZoom(const QVariantList& args);
    QVariantList setZoom(const QVariantList& args);
    QVariantList refreshPage(const QVariantList& args);
    // Files
    QVariantList exportDocument(const QVariantList& args);
    QVariantList openFile(const QVariantList& args);
    QVariantList getFolder(const QVariantList& args);
    QVariantList rename(const QVariantList& args);
    // Elements
    QVariantList addStrokes(const QVariantList& args);
    QVariantList addSplines(const QVariantList& args);
    QVariantList addTexts(const QVariantList& args);
    QVariantList addLinks(const QVariantList& args);
    QVariantList addImages(const QVariantList& args);
    QVariantList getStrokes(const QVariantList& args);
    QVariantList getTexts(const QVariantList& args);
    QVariantList getLinks(const QVariantList& args);
    QVariantList getImages(const QVariantList& args);
    QVariantList addToSelection(const QVariantList& args);
    QVariantList clearSelection(const QVariantList& args);

    PluginController& c;
    int m_plugin;
};

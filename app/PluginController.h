/*
 * Qournal
 *
 * Plugins written in Lua, as Xournal++ has them: a folder with a plugin.ini and a Lua file that registers menu
 * entries and uses the functions of the table "app" (see PluginApi). The plugins of Xournal++ run unchanged as
 * far as the functions they use are there.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QStringList>
#include <QVariant>
#include <memory>
#include <optional>
#include <vector>

#include <QtQml/qqmlregistration.h>

#include "PageCanvas.h"

struct lua_State;
class QEventLoop;

class PluginController: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// Whether this build runs plugins (it needs Lua)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(PageCanvas* canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
    /// The colours the plugins are told about: entries with "name" and "color", as ColorPalette has them
    Q_PROPERTY(QVariantList palette READ palette WRITE setPalette NOTIFY paletteChanged)
    /// The plugins that were found: name, description, author, version, path, enabled, valid, error
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    /// What the plugins put into the menu: id, text, path (submenus, separated by "/"), shortcut, plugin
    Q_PROPERTY(QVariantList menuEntries READ menuEntries NOTIFY pluginsChanged)
    /// What the plugins offer for the toolbars: id, toolbarId ("Plugin::..."), text, icon (URL, may be empty)
    Q_PROPERTY(QVariantList toolbarEntries READ toolbarEntries NOTIFY pluginsChanged)
    /// The folder of the user where plugins can be put
    Q_PROPERTY(QString userFolder READ userFolder CONSTANT)
    /// Texts the plugins show in the toolbars: toolbarId ("Plugin::...") to a map with "description" and "value"
    Q_PROPERTY(QVariantMap placeholders READ placeholders NOTIFY placeholdersChanged)

public:
    explicit PluginController(QObject* parent = nullptr);
    ~PluginController() override;

    bool available() const;
    PageCanvas* canvas() const { return m_canvas; }
    void setCanvas(PageCanvas* canvas);
    QVariantList palette() const { return m_palette; }
    void setPalette(const QVariantList& palette);
    QVariantList plugins() const;
    QVariantList menuEntries() const;
    QVariantList toolbarEntries() const;
    QVariantMap placeholders() const { return m_placeholders; }
    static QString userFolder();

    /// The folders that are searched for plugins: next to the application and in the data of the user
    QStringList searchPaths() const;
    /// Replaces the folders, for tests
    void setSearchPaths(const QStringList& paths) { m_searchPaths = paths; }

    /// Looks for plugins and starts those that are enabled. Plugins that run already are started anew
    Q_INVOKABLE void load();
    /// Enables or disables a plugin, for this and the next sessions, and loads the plugins anew
    Q_INVOKABLE void setPluginEnabled(const QString& name, bool enabled);
    /// Runs what a menu or toolbar entry stands for
    Q_INVOKABLE void trigger(int entry);
    /// The answer to dialogRequested(): the number of the button, 0 or less if the dialog was closed without one
    Q_INVOKABLE void dialogFinished(int request, int button);
    /// The answer to fileDialogRequested(): the chosen path or URL, empty if none
    Q_INVOKABLE void fileDialogFinished(int request, const QString& path);
    /// Runs Lua code in a plugin, for tests. @return the error message, empty if there was none
    QString run(const QString& plugin, const QString& code);

    /// What a plugin printed; see pluginPrinted()
    void notifyPrinted(int plugin, const QString& text);

signals:
    void canvasChanged();
    void paletteChanged();
    void pluginsChanged();
    void placeholdersChanged();
    /// A plugin shows a message with buttons. dialogFinished() has to follow
    void dialogRequested(int request, const QString& plugin, const QString& message, const QVariantList& buttons,
                         bool error);
    /// A plugin asks for a file to open or to save to. fileDialogFinished() has to follow
    void fileDialogRequested(int request, bool save, const QString& suggestion, const QStringList& filters);
    /// A plugin triggers something of the user interface, by the name the action has in Xournal++
    void actionRequested(const QString& action, const QVariant& state);
    void floatingToolboxRequested(const QPointF& windowPos);
    /// A plugin could not be loaded or a function of it failed
    void pluginFailed(const QString& plugin, const QString& message);
    /// A plugin printed something (Lua's print). Xournal++ shows that only in the terminal, where many plugins
    /// tell why they did nothing
    void pluginPrinted(const QString& plugin, const QString& text);

private:
    friend class PluginApi;

    struct Plugin {
        QString name;  ///< the name of its folder
        QString path;
        QString author;
        QString description;
        QString version;
        QString mainfile;
        bool defaultEnabled = false;
        bool enabled = false;
        bool valid = false;
        bool inInitUi = false;
        QString error;
        lua_State* lua = nullptr;
    };
    struct Entry {
        int plugin;
        QString callback;
        std::optional<qint64> mode;  ///< passed to the callback, if given
        QString menu;
        QString parentPath;
        QString accelerator;
        QString toolbarId;
        QString iconName;
    };
    /// A dialog a plugin waits for
    struct Request {
        int plugin;
        QString callback;            ///< called with the result; none if the plugin waits in a loop
        QEventLoop* loop = nullptr;  ///< the loop the plugin waits in
        QVariant result;
    };

    void unload();
    void readPlugin(const QString& folder);
    void start(int index);
    /// Calls a global function of a plugin. Errors are reported with pluginFailed()
    bool call(int plugin, const QString& function, const QVariantList& arguments);
    void fail(Plugin& plugin, const QString& message);
    int addRequest(int plugin, const QString& callback);
    /// Waits for the answer to a request, while the user interface goes on
    QVariant waitFor(int request);

    QPointer<PageCanvas> m_canvas;
    QVariantList m_palette;
    QStringList m_searchPaths;
    std::vector<Plugin> m_plugins;
    std::vector<Entry> m_entries;
    QVariantMap m_placeholders;
    QHash<int, Request> m_requests;
    int m_callDepth = 0;  ///< calls into plugins that have not returned yet
    int m_nextRequest = 1;
    /// What the references to elements stand for that were given to the plugins since the last call into one
    std::vector<PageCanvas::ElementRef> m_refs;
};

#include "PluginController.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <cstdio>
#include <iterator>

#ifdef HAVE_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "LuaBridge.h"
#include "PluginApi.h"
#endif

namespace {

/// The version of Xournal++ whose plugins come with this application (the folder plugins/ of the sources)
const QString XOURNALPP_PLUGINS_VERSION = QStringLiteral("1.3.8");

/// The shortcut of a menu entry in the notation of Qt ("Ctrl+Shift+T") from the one of GTK ("<Control><Shift>t")
QString shortcutFromAccelerator(const QString& accelerator) {
    static const QRegularExpression modifier(QStringLiteral("<([A-Za-z]+)>"));
    QStringList parts;
    auto it = modifier.globalMatch(accelerator);
    qsizetype end = 0;
    while (it.hasNext()) {
        const auto match = it.next();
        const QString name = match.captured(1).toLower();
        if (name == u"control" || name == u"ctrl" || name == u"primary") {
            parts.append(QStringLiteral("Ctrl"));
        } else if (name == u"shift") {
            parts.append(QStringLiteral("Shift"));
        } else if (name == u"alt" || name == u"mod1") {
            parts.append(QStringLiteral("Alt"));
        } else if (name == u"super" || name == u"meta") {
            parts.append(QStringLiteral("Meta"));
        }
        end = match.capturedEnd();
    }
    QString key = accelerator.mid(end).trimmed();
    if (key.isEmpty()) {
        return {};
    }
    // The names of keys GTK and Qt do not share
    static const QHash<QString, QString> keys = {
            {QStringLiteral("plus"), QStringLiteral("+")},
            {QStringLiteral("minus"), QStringLiteral("-")},
            {QStringLiteral("comma"), QStringLiteral(",")},
            {QStringLiteral("period"), QStringLiteral(".")},
            {QStringLiteral("space"), QStringLiteral("Space")},
            {QStringLiteral("Page_Up"), QStringLiteral("PgUp")},
            {QStringLiteral("Page_Down"), QStringLiteral("PgDown")},
            {QStringLiteral("Escape"), QStringLiteral("Esc")},
            {QStringLiteral("BackSpace"), QStringLiteral("Backspace")},
            {QStringLiteral("Delete"), QStringLiteral("Del")},
            {QStringLiteral("dollar"), QStringLiteral("$")},
            {QStringLiteral("slash"), QStringLiteral("/")},
    };
    key = keys.value(key, key.size() == 1 ? key.toUpper() : key);
    parts.append(key);
    return parts.join(u'+');
}

#ifdef HAVE_LUA
/// Loads a Lua file through Qt and leaves it as a function on the stack, or an error message
bool loadChunk(lua_State* L, const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        const QByteArray message = QStringLiteral("cannot open %1: %2").arg(path, file.errorString()).toUtf8();
        lua_pushlstring(L, message.constData(), static_cast<size_t>(message.size()));
        return false;
    }
    const QByteArray code = file.readAll();
    const QByteArray name = '@' + path.toUtf8();
    // Text only: compiled Lua code is not checked by Lua and could do anything
    return luaL_loadbufferx(L, code.constData(), static_cast<size_t>(code.size()), name.constData(), "t") == LUA_OK;
}

/// What require calls to find a module: a Lua file of the folder of the plugin
/// Lua modules the user installed with luarocks for this version of Lua (lua-vips for ImageActions, lgi, ...),
/// in addition to the folders of the system, which Lua searches by itself
void addModulePaths(lua_State* L) {
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    const QString version = QStringLiteral(LUA_VERSION_MAJOR "." LUA_VERSION_MINOR);
#ifdef Q_OS_WIN
    const QString rocks = qEnvironmentVariable("APPDATA") + QStringLiteral("/luarocks");
    const QString library = QStringLiteral("dll");
#else
    const QString rocks = QDir::homePath() + QStringLiteral("/.luarocks");
    const QString library = QStringLiteral("so");
#endif
    const QString path = QStringLiteral("%1/share/lua/%2/?.lua;%1/share/lua/%2/?/init.lua").arg(rocks, version);
    const QString cpath = QStringLiteral("%1/lib/lua/%2/?.%3").arg(rocks, version, library);
    lua_getglobal(L, "package");
    for (const auto& [field, extra]: {std::pair{"path", path}, std::pair{"cpath", cpath}}) {
        lua_getfield(L, -1, field);
        const QByteArray value = QString::fromUtf8(lua_tostring(L, -1)).append(u';').append(extra).toUtf8();
        lua_pop(L, 1);
        lua_pushlstring(L, value.constData(), static_cast<size_t>(value.size()));
        lua_setfield(L, -2, field);
    }
    lua_pop(L, 1);
#endif
    // lua-vips and ImageActions use the FFI of LuaJIT. With the Lua of the application the module cffi-lua does
    // that, under another name
    luaL_dostring(L, "package.preload.ffi = function() return require('cffi') end");
}

/// print() of Lua, which also tells the window: upvalues are the controller and the plugin
int printOfPlugin(lua_State* L) {
    auto* controller = static_cast<PluginController*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int plugin = static_cast<int>(lua_tointeger(L, lua_upvalueindex(2)));
    const int count = lua_gettop(L);
    QByteArray text;
    for (int i = 1; i <= count; ++i) {
        size_t length = 0;
        const char* piece = luaL_tolstring(L, i, &length);
        if (i > 1) {
            text += '\t';
        }
        text.append(piece, static_cast<qsizetype>(length));
        lua_pop(L, 1);
    }
    std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    controller->notifyPrinted(plugin, QString::fromUtf8(text));
    return 0;
}

int searchPluginModule(lua_State* L) {
    const char* requested = luaL_checkstring(L, 1);
    int results = 1;
    bool failed = false;
    {
        // Nothing of this block may be alive when lua_error() leaves the function
        const QString module = QString::fromUtf8(requested);
        lua_getfield(L, LUA_REGISTRYINDEX, "xournalpp_plugin_path");
        const QString folder = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        // "a.b" is the file a/b.lua; nothing outside of the folder
        const QString file = folder + u'/' + QString(module).replace(u'.', u'/') + QStringLiteral(".lua");
        if (!module.contains(u"..") && !module.contains(u'/') && QFile::exists(file)) {
            if (loadChunk(L, file)) {
                const QByteArray name = file.toUtf8();
                lua_pushlstring(L, name.constData(), static_cast<size_t>(name.size()));
                results = 2;
            } else {
                failed = true;  // the message is on the stack
            }
        } else {
            const QByteArray message = QStringLiteral("\n\tno file '%1'").arg(file).toUtf8();
            lua_pushlstring(L, message.constData(), static_cast<size_t>(message.size()));
        }
    }
    if (failed) {
        return lua_error(L);
    }
    return results;
}
#endif

QString normalizedMenuPath(const QString& path) { return path.split(u'/', Qt::SkipEmptyParts).join(u'/'); }

}  // namespace

PluginController::PluginController(QObject* parent): QObject(parent) {}

PluginController::~PluginController() { unload(); }

bool PluginController::available() const {
#ifdef HAVE_LUA
    return true;
#else
    return false;
#endif
}

void PluginController::setCanvas(PageCanvas* canvas) {
    if (m_canvas != canvas) {
        m_canvas = canvas;
        emit canvasChanged();
    }
}

void PluginController::setPalette(const QVariantList& palette) {
    if (m_palette != palette) {
        m_palette = palette;
        emit paletteChanged();
    }
}

QString PluginController::userFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
}

QStringList PluginController::searchPaths() const {
    if (!m_searchPaths.isEmpty()) {
        return m_searchPaths;
    }
    // Those of the user come first: a plugin there replaces one of the same name that came with the application
    QStringList paths = {userFolder()};
    const QString fromEnvironment = qEnvironmentVariable("QOURNAL_PLUGINS");
    if (!fromEnvironment.isEmpty()) {
        paths += fromEnvironment.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    // The last one is in the application itself: on Android and iOS the plugins are built into it
    paths += {appDir + QStringLiteral("/plugins"), appDir + QStringLiteral("/../share/qournal/plugins")};
    // In the bundle of macOS they are resources
    paths += appDir + QStringLiteral("/../Resources/plugins");
    // In an AppImage the program is started from the top folder of the image, which is its folder then, and the
    // plugins are in usr/share below it: they were not found there
    paths += appDir + QStringLiteral("/usr/share/qournal/plugins");
    const QString image = qEnvironmentVariable("APPDIR");
    if (!image.isEmpty()) {
        paths += image + QStringLiteral("/usr/share/qournal/plugins");
    }
    paths += QStringLiteral(":/plugins");
    return paths;
}

QVariantList PluginController::plugins() const {
    QVariantList list;
    for (const Plugin& plugin: m_plugins) {
        list.append(QVariantMap{{QStringLiteral("name"), plugin.name},
                                {QStringLiteral("description"), plugin.description},
                                {QStringLiteral("author"), plugin.author},
                                {QStringLiteral("version"), plugin.version},
                                {QStringLiteral("path"), plugin.path},
                                {QStringLiteral("enabled"), plugin.enabled},
                                {QStringLiteral("valid"), plugin.valid},
                                {QStringLiteral("error"), plugin.error}});
    }
    return list;
}

QVariantList PluginController::menuEntries() const {
    QVariantList list;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const Entry& entry = m_entries[i];
        if (!entry.menu.isEmpty()) {
            list.append(QVariantMap{{QStringLiteral("id"), static_cast<int>(i)},
                                    {QStringLiteral("text"), entry.menu},
                                    {QStringLiteral("path"), normalizedMenuPath(entry.parentPath)},
                                    {QStringLiteral("shortcut"), shortcutFromAccelerator(entry.accelerator)},
                                    {QStringLiteral("plugin"), m_plugins[static_cast<size_t>(entry.plugin)].name}});
        }
    }
    return list;
}

QVariantList PluginController::toolbarEntries() const {
    QVariantList list;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const Entry& entry = m_entries[i];
        if (entry.toolbarId.isEmpty()) {
            continue;
        }
        const Plugin& plugin = m_plugins[static_cast<size_t>(entry.plugin)];
        // The icon is a file of the plugin
        const QString iconFile = plugin.path + u'/' + entry.iconName + QStringLiteral(".svg");
        const bool hasIcon = !entry.iconName.isEmpty() && QFileInfo::exists(iconFile);
        list.append(QVariantMap{{QStringLiteral("id"), static_cast<int>(i)},
                                {QStringLiteral("toolbarId"), QStringLiteral("Plugin::") + entry.toolbarId},
                                {QStringLiteral("text"), entry.menu.isEmpty() ? entry.toolbarId : entry.menu},
                                {QStringLiteral("icon"), hasIcon ? QUrl::fromLocalFile(iconFile) : QUrl()},
                                {QStringLiteral("plugin"), plugin.name}});
    }
    return list;
}

void PluginController::unload() {
    // Plugins that wait for a dialog go on without an answer
    const auto requests = m_requests;
    m_requests.clear();
    for (const Request& request: requests) {
        if (request.loop) {
            request.loop->quit();
        }
    }
#ifdef HAVE_LUA
    for (Plugin& plugin: m_plugins) {
        if (plugin.lua) {
            lua_close(plugin.lua);
            plugin.lua = nullptr;
        }
    }
#endif
    m_plugins.clear();
    m_entries.clear();
    m_refs.clear();
    if (!m_placeholders.isEmpty()) {
        m_placeholders.clear();
        emit placeholdersChanged();
    }
}

void PluginController::load() {
    if (m_callDepth > 0) {
        // A plugin is running, e.g. it waits for a dialog: its Lua state cannot be closed under it. The dialogs
        // end without an answer, and the plugins are loaded once it has returned
        for (Request& request: m_requests) {
            if (request.loop) {
                request.loop->quit();
            }
        }
        QMetaObject::invokeMethod(this, &PluginController::load, Qt::QueuedConnection);
        return;
    }
    unload();
    for (const QString& path: searchPaths()) {
        const QDir dir(path);
        const QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& folder: folders) {
            if (QFileInfo::exists(dir.filePath(folder + QStringLiteral("/plugin.ini")))) {
                readPlugin(dir.absoluteFilePath(folder));
            }
        }
    }
    for (size_t i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].enabled && m_plugins[i].valid) {
            start(static_cast<int>(i));
        }
    }
    emit pluginsChanged();
}

void PluginController::readPlugin(const QString& folder) {
    Plugin plugin;
    plugin.name = QFileInfo(folder).fileName();
    for (const Plugin& known: m_plugins) {
        if (known.name == plugin.name) {
            return;  // one of an earlier folder has the same name
        }
    }
    plugin.path = folder;

    const QSettings ini(folder + QStringLiteral("/plugin.ini"), QSettings::IniFormat);
    auto text = [&](const QString& key) {
        // A comma makes a list of a value
        return ini.value(key).toStringList().join(QStringLiteral(", ")).trimmed();
    };
    plugin.author = text(QStringLiteral("about/author"));
    plugin.description = text(QStringLiteral("about/description"));
    plugin.version = text(QStringLiteral("about/version"));
    if (plugin.version == u"<xournalpp>") {
        // The plugins that come with Xournal++ have its version. They are taken from it unchanged, so they keep
        // that one and do not get the version of this application
        plugin.version = XOURNALPP_PLUGINS_VERSION;
    }
    plugin.mainfile = text(QStringLiteral("plugin/mainfile"));
    plugin.defaultEnabled = text(QStringLiteral("default/enabled")) == u"true";
    plugin.enabled =
            QSettings().value(QStringLiteral("plugins/%1/enabled").arg(plugin.name), plugin.defaultEnabled).toBool();

    if (plugin.mainfile.isEmpty()) {
        plugin.error = tr("plugin.ini does not name the Lua file of the plugin");
    } else if (plugin.mainfile.contains(u"..")) {
        plugin.error = tr("The path \"%1\" is not allowed").arg(plugin.mainfile);
    } else if (!QFileInfo::exists(folder + u'/' + plugin.mainfile)) {
        plugin.error = tr("The file \"%1\" is missing").arg(plugin.mainfile);
    } else {
        plugin.valid = true;
    }
    m_plugins.push_back(plugin);
}

void PluginController::setPluginEnabled(const QString& name, bool enabled) {
    QSettings().setValue(QStringLiteral("plugins/%1/enabled").arg(name), enabled);
    load();
}

void PluginController::notifyPrinted(int index, const QString& text) {
    if (index >= 0 && index < static_cast<int>(m_plugins.size())) {
        emit pluginPrinted(m_plugins[static_cast<size_t>(index)].name, text);
    }
}

void PluginController::fail(Plugin& plugin, const QString& message) {
    plugin.error = message;
    qWarning("Plugin \"%s\": %s", qPrintable(plugin.name), qPrintable(message));
    emit pluginFailed(plugin.name, message);
}

void PluginController::start(int index) {
#ifdef HAVE_LUA
    Plugin& plugin = m_plugins[static_cast<size_t>(index)];
    lua_State* L = luaL_newstate();
    plugin.lua = L;
    luaL_openlibs(L);
    addModulePaths(L);
    PluginApi::open(L, this, index);
    lua_pushlightuserdata(L, this);
    lua_pushinteger(L, index);
    lua_pushcclosure(L, printOfPlugin, 2);
    lua_setglobal(L, "print");
    const QByteArray pluginPath = plugin.path.toUtf8();

    // The plugin finds its own Lua files with require. They are read through Qt, like the main file: plugins
    // can be in the resources of the application (":/plugins") or in the assets of an Android package
    lua_pushlstring(L, pluginPath.constData(), static_cast<size_t>(pluginPath.size()));
    lua_setfield(L, LUA_REGISTRYINDEX, "xournalpp_plugin_path");
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "searchers");
    const auto count = static_cast<lua_Integer>(lua_rawlen(L, -1));
    // After the searcher for modules that are loaded already, before those of Lua that read files
    for (lua_Integer k = count; k >= 2; --k) {
        lua_rawgeti(L, -1, k);
        lua_rawseti(L, -2, k + 1);
    }
    lua_pushcfunction(L, searchPluginModule);
    lua_rawseti(L, -2, 2);
    lua_pop(L, 2);

    const QString file = plugin.path + u'/' + plugin.mainfile;
    if (!loadChunk(L, file) || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const QString message = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        plugin.valid = false;
        fail(plugin, message);
        return;
    }
    // The plugin registers its menu entries
    lua_getglobal(L, "initUi");
    const bool hasInitUi = lua_isfunction(L, -1);
    lua_pop(L, 1);
    if (hasInitUi) {
        plugin.inInitUi = true;
        call(index, QStringLiteral("initUi"), {});
        m_plugins[static_cast<size_t>(index)].inInitUi = false;
    }
#else
    Q_UNUSED(index)
#endif
}

bool PluginController::call(int index, const QString& function, const QVariantList& arguments) {
#ifdef HAVE_LUA
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) {
        return false;
    }
    lua_State* L = m_plugins[static_cast<size_t>(index)].lua;
    if (!L) {
        return false;
    }
    lua_getglobal(L, function.toUtf8().constData());
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        fail(m_plugins[static_cast<size_t>(index)], tr("The plugin has no function \"%1\"").arg(function));
        return false;
    }
    for (const QVariant& argument: arguments) {
        LuaBridge::push(L, argument);
    }
    ++m_callDepth;
    const bool ok = lua_pcall(L, static_cast<int>(arguments.size()), 0, 0) == LUA_OK;
    --m_callDepth;
    if (!ok) {
        const QString message = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        fail(m_plugins[static_cast<size_t>(index)], message);
    }
    return ok;
#else
    Q_UNUSED(index)
    Q_UNUSED(function)
    Q_UNUSED(arguments)
    return false;
#endif
}

void PluginController::trigger(int entry) {
    if (entry < 0 || entry >= static_cast<int>(m_entries.size())) {
        return;
    }
    const Entry e = m_entries[static_cast<size_t>(entry)];
    m_refs.clear();
    call(e.plugin, e.callback, e.mode ? QVariantList{*e.mode} : QVariantList());
}

QString PluginController::run(const QString& name, const QString& code) {
#ifdef HAVE_LUA
    for (const Plugin& plugin: m_plugins) {
        if (plugin.name == name && plugin.lua) {
            lua_State* L = plugin.lua;
            m_refs.clear();
            ++m_callDepth;
            const bool ok = luaL_loadstring(L, code.toUtf8().constData()) == LUA_OK && lua_pcall(L, 0, 0, 0) == LUA_OK;
            --m_callDepth;
            if (!ok) {
                const QString message = QString::fromUtf8(lua_tostring(L, -1));
                lua_pop(L, 1);
                return message;
            }
            return {};
        }
    }
#else
    Q_UNUSED(code)
#endif
    return tr("The plugin \"%1\" is not running").arg(name);
}

// Dialogs

int PluginController::addRequest(int plugin, const QString& callback) {
    const int id = m_nextRequest++;
    m_requests.insert(id, Request{plugin, callback, nullptr, {}});
    return id;
}

QVariant PluginController::waitFor(int request) {
    // Without a user interface nobody would answer
    static const QMetaMethod dialogSignal = QMetaMethod::fromSignal(&PluginController::dialogRequested);
    static const QMetaMethod fileSignal = QMetaMethod::fromSignal(&PluginController::fileDialogRequested);
    if (!isSignalConnected(dialogSignal) && !isSignalConnected(fileSignal)) {
        m_requests.remove(request);
        return {};
    }
    QEventLoop loop;
    m_requests[request].loop = &loop;
    loop.exec();
    // Not there any more if the plugins were unloaded meanwhile
    return m_requests.take(request).result;
}

void PluginController::dialogFinished(int id, int button) {
    if (!m_requests.contains(id)) {
        return;
    }
    Request& request = m_requests[id];
    if (request.loop) {
        request.result = button;
        request.loop->quit();
        return;
    }
    const Request done = m_requests.take(id);
    // As in Xournal++, the plugin is not told if the dialog was closed without a button
    if (!done.callback.isEmpty() && button > 0) {
        m_refs.clear();
        call(done.plugin, done.callback, {button});
    }
}

void PluginController::fileDialogFinished(int id, const QString& chosen) {
    if (!m_requests.contains(id)) {
        return;
    }
    const QString path = chosen.startsWith(u"file:") ? QUrl(chosen).toLocalFile() : chosen;
    Request& request = m_requests[id];
    if (request.loop) {
        request.result = path;
        request.loop->quit();
        return;
    }
    const Request done = m_requests.take(id);
    if (!done.callback.isEmpty()) {
        m_refs.clear();
        call(done.plugin, done.callback, {path});
    }
}

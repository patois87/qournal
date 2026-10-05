/*
 * Qournal
 *
 * Tests for the plugins: values between Lua and Qt, loading plugins, their menu entries and dialogs, the
 * functions of the table "app", and the plugins of Xournal++ themselves. They run without a display
 * (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <QFontDatabase>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "CanvasFixture.h"
#include "PluginController.h"

#ifdef HAVE_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "LuaBridge.h"

namespace {

void writeFile(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(content);
}

/// A plugin in a folder of its own
void writePlugin(const QString& dir, const QString& name, const QByteArray& lua, bool enabled = true) {
    writeFile(dir + u'/' + name + QStringLiteral("/plugin.ini"),
              "[about]\nauthor=A, B and C\ndescription=A plugin for the tests\nversion=1.2\n\n[default]\nenabled=" +
                      QByteArray(enabled ? "true" : "false") + "\n\n[plugin]\nmainfile=main.lua\n");
    writeFile(dir + u'/' + name + QStringLiteral("/main.lua"), lua);
}

QVariantMap entryNamed(const QVariantList& entries, const QString& text) {
    for (const QVariant& entry: entries) {
        if (entry.toMap().value(QStringLiteral("text")).toString() == text) {
            return entry.toMap();
        }
    }
    return {};
}

/// A canvas with a plugin "test" whose Lua code is run piece by piece
struct PluginFixture {
    explicit PluginFixture(const QByteArray& lua = "function initUi() end") {
        writePlugin(dir.path(), QStringLiteral("test"), lua);
        plugins.setCanvas(f.canvas);
        plugins.setSearchPaths({dir.path()});
        QObject::connect(&plugins, &PluginController::pluginFailed, &plugins,
                         [this](const QString&, const QString& message) { failures.append(message); });
        plugins.load();
    }

    /// Runs Lua code; an error fails the test with its message
    bool run(const QByteArray& code) {
        const QString error = plugins.run(QStringLiteral("test"), QString::fromUtf8(code));
        if (!error.isEmpty()) {
            qWarning("%s", qPrintable(error));
        }
        return error.isEmpty();
    }

    QString error(const QByteArray& code) { return plugins.run(QStringLiteral("test"), QString::fromUtf8(code)); }

    QTemporaryDir dir;
    Fixture f;
    PluginController plugins;
    QStringList failures;
};

}  // namespace
#endif

class TestPlugins: public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Nothing of the user is read or written
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("qournal-test"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_plugins"));
        QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QVERIFY(m_settingsDir.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }

    void init() { QSettings().clear(); }

#ifndef HAVE_LUA
    void withoutLua() {
        PluginController plugins;
        QVERIFY(!plugins.available());
        plugins.setSearchPaths({QStringLiteral(PLUGINS_DIR)});
        plugins.load();
        // They are listed, but none runs
        QVERIFY(!plugins.plugins().isEmpty());
        QVERIFY(plugins.menuEntries().isEmpty());
        QSKIP("Built without Lua");
    }
#else
    void valuesBetweenLuaAndQt() {
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        auto eval = [&](const char* expression) {
            const QByteArray code = QByteArray("return ") + expression;
            if (luaL_dostring(L, code.constData()) != LUA_OK) {
                qWarning("%s", lua_tostring(L, -1));
                lua_pop(L, 1);
                return QVariant(QStringLiteral("<error>"));
            }
            const QVariant value = LuaBridge::toVariant(L, -1);
            lua_pop(L, 1);
            return value;
        };

        QCOMPARE(eval("nil"), QVariant());
        QCOMPARE(eval("true"), QVariant(true));
        QCOMPARE(eval("42").typeId(), QMetaType::LongLong);
        QCOMPARE(eval("42").toInt(), 42);
        QCOMPARE(eval("0xff00ff").toInt(), 0xff00ff);
        QCOMPARE(eval("1.5").typeId(), QMetaType::Double);
        QCOMPARE(eval("'grüezi'"), QVariant(QStringLiteral("grüezi")));
        // Not text: kept as bytes
        QCOMPARE(eval("'\\x89PNG\\0\\xff'"), QVariant(QByteArray("\x89PNG\0\xff", 6)));
        QCOMPARE(eval("{10, 'b', true}"), QVariant(QVariantList{10, QStringLiteral("b"), true}));
        QCOMPARE(eval("{}"), QVariant(QVariantMap()));
        const QVariantMap map = eval("{name = 'n', list = {1, 2}, nested = {x = 0.5}, [5] = 'five'}").toMap();
        QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("n"));
        QCOMPARE(map.value(QStringLiteral("list")).toList().size(), 2);
        QCOMPARE(map.value(QStringLiteral("nested")).toMap().value(QStringLiteral("x")).toDouble(), 0.5);
        QCOMPARE(map.value(QStringLiteral("5")).toString(), QStringLiteral("five"));
        // A table with a gap is not a list
        QCOMPARE(eval("{[1] = 'a', [3] = 'c'}").typeId(), QMetaType::QVariantMap);
        // A table that contains itself does not loop forever
        QVERIFY(eval("(function() local t = {}; t.self = t; return t end)()").isValid());

        // The other way: what Qt gives is what Lua sees
        auto check = [&](const QVariant& value, const char* condition) {
            LuaBridge::push(L, value);
            lua_setglobal(L, "v");
            const QByteArray code = QByteArray("return ") + condition;
            if (luaL_dostring(L, code.constData()) != LUA_OK) {
                qWarning("%s", lua_tostring(L, -1));
                lua_pop(L, 1);
                return false;
            }
            const bool ok = lua_toboolean(L, -1);
            lua_pop(L, 1);
            return ok;
        };
        QVERIFY(check(QVariant(), "v == nil"));
        QVERIFY(check(true, "v == true"));
        QVERIFY(check(7, "math.type(v) == 'integer' and v == 7"));
        QVERIFY(check(7.0, "math.type(v) == 'float'"));
        QVERIFY(check(QStringLiteral("grüezi"), "v == 'grüezi'"));
        QVERIFY(check(QByteArray("a\0b", 3), "#v == 3 and v:byte(2) == 0"));
        QVERIFY(check(QColor(0x12, 0x34, 0x56), "v == 0x123456"));
        QVERIFY(check(QVariantList{1, QStringLiteral("two")}, "#v == 2 and v[1] == 1 and v[2] == 'two'"));
        QVERIFY(check(QStringList{QStringLiteral("a"), QStringLiteral("b")}, "#v == 2 and v[2] == 'b'"));
        QVERIFY(check(QVariantMap{{QStringLiteral("k"), QVariantList{QVariantMap{{QStringLiteral("deep"), 1}}}}},
                      "v.k[1].deep == 1"));
        QVERIFY(check(QVariant::fromValue(LuaIntTable{{0, QStringLiteral("zero")}, {1, QStringLiteral("one")}}),
                      "v[0] == 'zero' and v[1] == 'one' and #v == 1"));
        int object = 0;
        QVERIFY(check(QVariant::fromValue(static_cast<void*>(&object)), "type(v) == 'userdata'"));
        lua_getglobal(L, "v");
        QCOMPARE(LuaBridge::toVariant(L, -1).value<void*>(), static_cast<void*>(&object));
        lua_close(L);
    }

    void findsAndLoadsPlugins() {
        QTemporaryDir first;
        QTemporaryDir second;
        writePlugin(first.path(), QStringLiteral("alpha"),
                    "loaded = 'first'\nfunction initUi() app.registerUi({menu = 'Alpha', callback = 'run'}) end\n"
                    "function run() end\n");
        writePlugin(first.path(), QStringLiteral("off"),
                    "function initUi() app.registerUi({menu = 'Off', callback = 'x'}) end", false);
        writePlugin(first.path(), QStringLiteral("broken"), "function initUi( -- no end");
        writePlugin(first.path(), QStringLiteral("failing"), "function initUi() error('no luck') end");
        writeFile(first.filePath(QStringLiteral("nofile/plugin.ini")),
                  "[plugin]\nmainfile=missing.lua\n[default]\nenabled=true\n");
        writeFile(first.filePath(QStringLiteral("outside/plugin.ini")), "[plugin]\nmainfile=../alpha/main.lua\n");
        writeFile(first.filePath(QStringLiteral("notaplugin/readme.txt")), "nothing");
        // Of the same name in a later folder: the first one counts
        writePlugin(second.path(), QStringLiteral("alpha"),
                    "function initUi() app.registerUi({menu = 'Second', callback = 'x'}) end");
        writePlugin(second.path(), QStringLiteral("beta"),
                    "local helper = require 'helper'\nfunction initUi() app.registerUi({menu = helper.name, callback = "
                    "'x'}) end");
        writeFile(second.filePath(QStringLiteral("beta/helper.lua")), "return {name = 'Beta'}");

        PluginController plugins;
        QVERIFY(plugins.available());
        plugins.setSearchPaths({first.path(), second.path()});
        QSignalSpy failed(&plugins, &PluginController::pluginFailed);
        QSignalSpy changed(&plugins, &PluginController::pluginsChanged);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Plugin \"broken\"")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Plugin \"failing\".*no luck")));
        plugins.load();
        QCOMPARE(changed.count(), 1);

        QHash<QString, QVariantMap> byName;
        for (const QVariant& plugin: plugins.plugins()) {
            byName.insert(plugin.toMap().value(QStringLiteral("name")).toString(), plugin.toMap());
        }
        QCOMPARE(byName.size(), 7);
        const QVariantMap alpha = byName.value(QStringLiteral("alpha"));
        QCOMPARE(alpha.value(QStringLiteral("path")).toString(), first.filePath(QStringLiteral("alpha")));
        QCOMPARE(alpha.value(QStringLiteral("author")).toString(), QStringLiteral("A, B and C"));
        QCOMPARE(alpha.value(QStringLiteral("description")).toString(), QStringLiteral("A plugin for the tests"));
        QCOMPARE(alpha.value(QStringLiteral("version")).toString(), QStringLiteral("1.2"));
        QVERIFY(alpha.value(QStringLiteral("enabled")).toBool());
        QVERIFY(alpha.value(QStringLiteral("valid")).toBool());
        QVERIFY(!byName.value(QStringLiteral("off")).value(QStringLiteral("enabled")).toBool());
        QVERIFY(!byName.value(QStringLiteral("broken")).value(QStringLiteral("valid")).toBool());
        QVERIFY(!byName.value(QStringLiteral("broken")).value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(byName.value(QStringLiteral("failing"))
                        .value(QStringLiteral("error"))
                        .toString()
                        .contains(QStringLiteral("no luck")));
        QVERIFY(!byName.value(QStringLiteral("nofile")).value(QStringLiteral("valid")).toBool());
        QVERIFY(!byName.value(QStringLiteral("outside")).value(QStringLiteral("valid")).toBool());
        QCOMPARE(failed.count(), 2);

        // The menu has what the running plugins registered
        QStringList menu;
        for (const QVariant& entry: plugins.menuEntries()) {
            menu.append(entry.toMap().value(QStringLiteral("text")).toString());
        }
        QCOMPARE(menu, (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta")}));

        // Switching a plugin on is remembered. The plugins are loaded anew each time
        for (int i = 0; i < 2; ++i) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Plugin \"broken\"")));
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Plugin \"failing\"")));
        }
        plugins.setPluginEnabled(QStringLiteral("off"), true);
        plugins.setPluginEnabled(QStringLiteral("alpha"), false);
        QVERIFY(!entryNamed(plugins.menuEntries(), QStringLiteral("Off")).isEmpty());
        QVERIFY(entryNamed(plugins.menuEntries(), QStringLiteral("Alpha")).isEmpty());
        PluginController next;
        next.setSearchPaths({first.path(), second.path()});
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Plugin \"broken\"")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Plugin \"failing\"")));
        next.load();
        QVERIFY(!entryNamed(next.menuEntries(), QStringLiteral("Off")).isEmpty());
        QVERIFY(entryNamed(next.menuEntries(), QStringLiteral("Alpha")).isEmpty());
    }

    void menuEntriesAndCallbacks() {
        PluginFixture p(R"(
            calls = {}
            function initUi()
                first = app.registerUi({menu = "Plain", callback = "plain", accelerator = "<Control><Shift>c"})
                app.registerUi({menu = "With mode", callback = "withMode", mode = 7, accelerator = "<Alt>F5",
                                parentPath = "/Tools/" .. "/Custom/"})
                app.registerUi({menu = "Button", callback = "button", toolbarId = "MY_BUTTON", iconName = "icon"})
                app.registerUi({menu = "Fails", callback = "fails"})
                app.registerUi({menu = "Missing", callback = "doesNotExist"})
                app.registerPlaceholder("mode", "Current mode")
            end
            function plain(mode) calls[#calls + 1] = "plain:" .. tostring(mode) end
            function withMode(mode) calls[#calls + 1] = "mode:" .. tostring(mode) end
            function button() calls[#calls + 1] = "button" end
            function fails() error("it broke") end
        )");
        writeFile(p.dir.filePath(QStringLiteral("test/icon.svg")), "<svg xmlns='http://www.w3.org/2000/svg'/>");
        QVERIFY(p.failures.isEmpty());
        QVERIFY(p.run("assert(first.menuId == 0)"));

        const QVariantList menu = p.plugins.menuEntries();
        QCOMPARE(menu.size(), 5);
        const QVariantMap plain = entryNamed(menu, QStringLiteral("Plain"));
        QCOMPARE(plain.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Shift+C"));
        QCOMPARE(plain.value(QStringLiteral("path")).toString(), QString());
        QCOMPARE(plain.value(QStringLiteral("plugin")).toString(), QStringLiteral("test"));
        const QVariantMap withMode = entryNamed(menu, QStringLiteral("With mode"));
        QCOMPARE(withMode.value(QStringLiteral("shortcut")).toString(), QStringLiteral("Alt+F5"));
        QCOMPARE(withMode.value(QStringLiteral("path")).toString(), QStringLiteral("Tools/Custom"));

        const QVariantList toolbar = p.plugins.toolbarEntries();
        QCOMPARE(toolbar.size(), 1);
        QCOMPARE(toolbar[0].toMap().value(QStringLiteral("toolbarId")).toString(), QStringLiteral("Plugin::MY_BUTTON"));
        QCOMPARE(toolbar[0].toMap().value(QStringLiteral("icon")).toUrl(),
                 QUrl::fromLocalFile(p.dir.filePath(QStringLiteral("test/icon.svg"))));

        // The entries run their functions; the mode is passed if there is one
        p.plugins.trigger(plain.value(QStringLiteral("id")).toInt());
        p.plugins.trigger(withMode.value(QStringLiteral("id")).toInt());
        p.plugins.trigger(toolbar[0].toMap().value(QStringLiteral("id")).toInt());
        p.plugins.trigger(99);  // nothing
        QVERIFY(p.run("assert(table.concat(calls, ' ') == 'plain:nil mode:7 button', table.concat(calls, ' '))"));

        // An error in a plugin is reported and does no harm
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("it broke")));
        p.plugins.trigger(entryNamed(menu, QStringLiteral("Fails")).value(QStringLiteral("id")).toInt());
        QCOMPARE(p.failures.size(), 1);
        QVERIFY(p.failures[0].contains(QStringLiteral("it broke")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("doesNotExist")));
        p.plugins.trigger(entryNamed(menu, QStringLiteral("Missing")).value(QStringLiteral("id")).toInt());
        QCOMPARE(p.failures.size(), 2);
        p.plugins.trigger(plain.value(QStringLiteral("id")).toInt());
        QVERIFY(p.run("assert(#calls == 4)"));

        // Menu entries can only be registered while the plugin starts
        QVERIFY(p.error("app.registerUi({menu = 'Late', callback = 'plain'})").contains(QStringLiteral("initUi")));

        // A text for the toolbar
        QSignalSpy placeholders(&p.plugins, &PluginController::placeholdersChanged);
        QCOMPARE(p.plugins.placeholders()
                         .value(QStringLiteral("Plugin::mode"))
                         .toMap()
                         .value(QStringLiteral("description"))
                         .toString(),
                 QStringLiteral("Current mode"));
        QVERIFY(p.run("app.setPlaceholderValue('mode', 'INSERT')"));
        QCOMPARE(placeholders.count(), 1);
        QCOMPARE(p.plugins.placeholders()
                         .value(QStringLiteral("Plugin::mode"))
                         .toMap()
                         .value(QStringLiteral("value"))
                         .toString(),
                 QStringLiteral("INSERT"));
    }

    void dialogs() {
        PluginFixture p(R"(
            answers = {}
            function initUi() end
            function answered(button) answers[#answers + 1] = button end
            function chosen(path) answers[#answers + 1] = path end
        )");
        QSignalSpy dialog(&p.plugins, &PluginController::dialogRequested);
        QSignalSpy fileDialog(&p.plugins, &PluginController::fileDialogRequested);

        // A message with buttons: the plugin goes on and is called when a button is pressed
        QVERIFY(p.run("app.openDialog('Proceed?', {'Cancel', 'Proceed'}, 'answered')"));
        QCOMPARE(dialog.count(), 1);
        QCOMPARE(dialog[0][1].toString(), QStringLiteral("test"));
        QCOMPARE(dialog[0][2].toString(), QStringLiteral("Proceed?"));
        const QVariantList buttons = dialog[0][3].toList();
        QCOMPARE(buttons.size(), 2);
        QCOMPARE(buttons[1].toMap().value(QStringLiteral("id")).toInt(), 2);
        QCOMPARE(buttons[1].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Proceed"));
        QVERIFY(!dialog[0][4].toBool());
        p.plugins.dialogFinished(dialog[0][0].toInt(), 2);
        QVERIFY(p.run("assert(#answers == 1 and answers[1] == 2)"));
        p.plugins.dialogFinished(dialog[0][0].toInt(), 1);  // answered already
        QVERIFY(p.run("assert(#answers == 1)"));

        // Numbered buttons, an error, no callback; closing the dialog calls nothing
        QVERIFY(p.run("app.openDialog('Broken', {[3] = 'Three', [7] = 'Seven'}, '', true)"));
        QCOMPARE(dialog[1][3].toList()[1].toMap().value(QStringLiteral("id")).toInt(), 7);
        QVERIFY(dialog[1][4].toBool());
        p.plugins.dialogFinished(dialog[1][0].toInt(), 7);
        QVERIFY(p.run("app.openDialog('Again?', {'Yes'}, 'answered')"));
        p.plugins.dialogFinished(dialog[2][0].toInt(), 0);
        QVERIFY(p.run("assert(#answers == 1)"));
        QVERIFY(p.failures.isEmpty());

        // The old message box waits for the answer
        QObject::connect(
                &p.plugins, &PluginController::dialogRequested, &p.plugins,
                [&](int request, const QString&, const QString& message) {
                    if (message == u"Wait") {
                        p.plugins.dialogFinished(request, 2);
                    }
                },
                Qt::QueuedConnection);
        QVERIFY(p.run("assert(app.msgbox('Wait', {[1] = 'Yes', [2] = 'No'}) == 2)"));

        // Files
        QVERIFY(p.run("app.fileDialogSave('chosen', 'drawing.png')"));
        QCOMPARE(fileDialog.count(), 1);
        QVERIFY(fileDialog[0][1].toBool());
        QCOMPARE(fileDialog[0][2].toString(), QStringLiteral("drawing.png"));
        p.plugins.fileDialogFinished(fileDialog[0][0].toInt(), QStringLiteral("file:///tmp/a b.png"));
        QVERIFY(p.run("assert(answers[2] == '/tmp/a b.png', answers[2])"));

        QVERIFY(p.run("app.fileDialogOpen('chosen', {'*.bmp', '*.png'})"));
        QVERIFY(!fileDialog[1][1].toBool());
        QCOMPARE(fileDialog[1][3].toStringList(), (QStringList{QStringLiteral("*.bmp"), QStringLiteral("*.png")}));
        p.plugins.fileDialogFinished(fileDialog[1][0].toInt(), QString());  // cancelled: an empty path
        QVERIFY(p.run("assert(#answers == 3 and answers[3] == '')"));

        QObject::connect(
                &p.plugins, &PluginController::fileDialogRequested, &p.plugins,
                [&](int request, bool save) {
                    p.plugins.fileDialogFinished(request, save ? QStringLiteral("/s") : QStringLiteral("/o"));
                },
                Qt::QueuedConnection);
        QVERIFY(p.run("assert(app.saveAs('x') == '/s'); assert(app.getFilePath({'*.png'}) == '/o')"));
    }

    void strokes() {
        PluginFixture p;
        PageCanvas* c = p.f.canvas;
        QVERIFY(p.run(R"(
            refs = app.addStrokes({strokes = {
                {x = {10, 20, 30}, y = {40, 50, 60}, pressure = {1, 2, 3}, tool = "pen", width = 3.5, color = 0xa000f0,
                 fill = 40, lineStyle = "dashdot"},
                {x = {100, 200}, y = {100, 100}, tool = "highlighter"},
                {x = {5}, y = {5}},
            }})
            assert(#refs == 3 and type(refs[1]) == "userdata")
        )"));
        QList<Stroke> strokes = p.f.strokes();
        QCOMPARE(strokes.size(), 3);
        QCOMPARE(strokes[0].points, (QList<QPointF>{{10, 40}, {20, 50}, {30, 60}}));
        QCOMPARE(strokes[0].widths, (QList<double>{1, 2}));
        QCOMPARE(strokes[0].tool, Stroke::Tool::Pen);
        QCOMPARE(strokes[0].width, 3.5);
        QCOMPARE(strokes[0].color, QColor(0xa0, 0x00, 0xf0));
        QCOMPARE(strokes[0].fill, 40);
        QCOMPARE(strokes[0].style, QStringLiteral("dashdot"));
        QVERIFY(!strokes[0].dashes.isEmpty());
        QCOMPARE(strokes[0].bounds.isValid(), true);
        // What is not given comes from the tool
        QCOMPARE(strokes[1].tool, Stroke::Tool::Highlighter);
        QCOMPARE(strokes[1].color.alpha(), 0x7f);
        QCOMPARE(strokes[2].color, PEN_COLOR);
        QCOMPARE(strokes[2].width, c->thickness());
        QCOMPARE(strokes[2].fill, -1);
        QVERIFY(strokes[2].style.isEmpty());

        // One step of undo for all of them, or one for each
        c->undo();
        QCOMPARE(p.f.strokes().size(), 0);
        c->redo();
        QCOMPARE(p.f.strokes().size(), 3);
        QVERIFY(p.run("app.addStrokes({strokes = {{x = {1, 2}, y = {1, 2}}, {x = {3, 4}, y = {3, 4}}}, "
                      "allowUndoRedoAction = 'individual'})"));
        QCOMPARE(p.f.strokes().size(), 5);
        c->undo();
        QCOMPARE(p.f.strokes().size(), 4);
        c->undo();
        QCOMPARE(p.f.strokes().size(), 3);

        // Reading them gives what was added
        QVERIFY(p.run(R"(
            local strokes = app.getStrokes("layer")
            assert(#strokes == 3)
            local s = strokes[1]
            assert(#s.x == 3 and s.x[2] == 20 and s.y[3] == 60)
            assert(#s.pressure == 3 and s.pressure[1] == 1 and s.pressure[3] == 2)
            assert(s.tool == "pen" and s.width == 3.5 and s.color == 0xa000f0 and s.fill == 40)
            assert(s.lineStyle == "dashdot")
            assert(s.ref ~= nil and s.page == nil and s.layer == nil)
            assert(strokes[2].tool == "highlighter" and strokes[2].pressure == nil and strokes[2].lineStyle == "plain")
            assert(strokes[3].fill == -1)
            local onPage = app.getStrokes("page")
            assert(#onPage == 3 and onPage[1].layer == 1 and onPage[1].page == nil)
            local all = app.getStrokes("all")
            assert(#all == 3 and all[1].layer == 1 and all[1].page == 1)
        )"));

        // The selection
        QVERIFY(p.error("app.getStrokes('selection')").contains(QStringLiteral("selection")));
        QVERIFY(p.run(R"(
            local strokes = app.getStrokes("layer")
            app.addToSelection({strokes[1].ref, strokes[3].ref})
            local selected = app.getStrokes("selection")
            assert(#selected == 2 and selected[1].width == 3.5 and #selected[2].x == 1)
            local info = app.getToolInfo("selection")
            assert(info.boundingBox.width > 0 and info.rotation == 0)
        )"));
        QVERIFY(c->hasSelection());
        QVERIFY(p.run("app.addToSelection({app.getStrokes('layer')[2].ref})\n"
                      "assert(#app.getStrokes('selection') == 3)\napp.clearSelection()"));
        QVERIFY(!c->hasSelection());

        // Splines become strokes that pass through their knots
        QVERIFY(p.run("app.addSplines({splines = {{coordinates = {100, 300, 150, 250, 250, 250, 300, 300, "
                      "300, 300, 350, 350, 400, 350, 450, 300}, width = 2, color = 0x00ff00}}})"));
        strokes = p.f.strokes();
        QCOMPARE(strokes.size(), 4);
        QVERIFY(strokes[3].points.size() > 10);
        QCOMPARE(strokes[3].points.first(), QPointF(100, 300));
        QCOMPARE(strokes[3].points.last(), QPointF(450, 300));
        QVERIFY(strokes[3].points.contains(QPointF(300, 300)));
        QVERIFY(strokes[3].bounds.top() < 280);  // the curve bulges towards the control points

        // What does not fit is an error of the function, and nothing is added
        QVERIFY(p.error("app.addStrokes({strokes = {{x = {1, 2}, y = {1}}}})")
                        .contains(QStringLiteral("app.addStrokes")));
        QVERIFY(!p.error("app.addStrokes({strokes = {{x = {1}, y = {1}, tool = 'brush'}}})").isEmpty());
        QVERIFY(!p.error("app.addStrokes({})").isEmpty());
        QVERIFY(!p.error("app.addSplines({splines = {{coordinates = {1, 2, 3}}}})").isEmpty());
        QVERIFY(!p.error("app.getStrokes('everything')").isEmpty());
        QVERIFY(!p.error("app.addToSelection({'x'})").isEmpty());
        QCOMPARE(p.f.strokes().size(), 4);
        QVERIFY(p.failures.isEmpty());
    }

    void textsLinksAndImages() {
        PluginFixture p;
        PageCanvas* c = p.f.canvas;
        const QString imagePath = QStringLiteral(TEST_DATA_DIR "/pages.xopp.bg_1.png");
        const QImage picture(imagePath);
        QVERIFY(!picture.isNull());

        // (moc takes two slashes in a raw string for a comment: the URL is set outside of them)
        QVERIFY(p.run("URL = 'https://xournalpp.github.io'"));
        QVERIFY(p.run(R"(
            app.addTexts({texts = {
                {text = "Hello World", font = {name = "Sans Bold", size = 8}, color = 0x1259b9, x = 50, y = 60},
                {text = "Some long text that needs wrapping", x = 150, y = 60, wrap = 80},
            }})
            app.addLinks({links = {
                {text = "Website", url = URL, alignment = app.C.Alignment_center, x = 50, y = 200,
                 font = {name = "Serif", size = 10}, color = 0x00ff00},
            }})
        )"));
        const auto& elements = c->document().pages[0].layers[0].elements;
        QCOMPARE(elements.size(), size_t(3));
        const auto& text = std::get<TextElement>(elements[0]);
        QCOMPARE(text.text, QStringLiteral("Hello World"));
        QCOMPARE(text.font, QStringLiteral("Sans Bold"));
        QCOMPARE(text.size, 8.0);
        QCOMPARE(text.color, QColor(0x12, 0x59, 0xb9));
        QCOMPARE(text.pos, QPointF(50, 60));
        QVERIFY(text.wrap < 0);
        const auto& wrapped = std::get<TextElement>(elements[1]);
        QCOMPARE(wrapped.wrap, 80.0);
        QCOMPARE(wrapped.font, c->textFontDescription());
        QCOMPARE(wrapped.size, c->textSize());
        const auto& link = std::get<LinkElement>(elements[2]);
        QCOMPARE(link.url, QStringLiteral("https://xournalpp.github.io"));
        QCOMPARE(link.align, QStringLiteral("center"));
        QCOMPARE(link.color, QColor(0x00, 0xff, 0x00));

        QVERIFY(p.run(R"(
            local texts = app.getTexts("layer")
            assert(#texts == 2)
            assert(texts[1].text == "Hello World" and texts[1].font.name == "Sans Bold" and texts[1].font.size == 8)
            assert(texts[1].color == 0x1259b9 and texts[1].x == 50 and texts[1].y == 60 and texts[1].wrap == nil)
            assert(texts[1].width > 20 and texts[1].height > 5)
            assert(texts[2].wrap == 80 and texts[2].width <= 81 and texts[2].height > texts[1].height)
            local links = app.getLinks("all")
            assert(#links == 1 and links[1].url == URL and links[1].text == "Website")
            assert(links[1].alignment == 1 and links[1].page == 1 and links[1].layer == 1 and links[1].width > 10)
            assert(#app.getStrokes("layer") == 0 and #app.getImages("layer") == 0)
        )"));

        // Images: from a file or from data, scaled as asked for, never larger than the page
        const QByteArray script = R"(
            local file = assert(io.open(PATH, "rb"))
            local data = file:read("a")
            file:close()
            results = app.addImages({images = {
                {path = PATH, x = 10, y = 20, maxWidth = 100},
                {data = data, x = 0, y = 300, maxWidth = 50, maxHeight = 50, aspectRatio = false, scale = 2},
                {path = "/no/such/file.png"},
                {data = "this is no image"},
                {path = PATH, scale = 100},
            }})
            assert(#results == 5)
            assert(type(results[1]) == "userdata" and type(results[2]) == "userdata" and type(results[5]) == "userdata")
            assert(type(results[3]) == "string" and type(results[4]) == "string")
            local images = app.getImages("layer")
            assert(#images == 3)
            assert(images[1].x == 10 and images[1].y == 20 and images[1].width == 100)
            assert(images[1].format == "png" and images[1].data == data)
            assert(images[1].imageWidth == WIDTH and images[1].imageHeight == HEIGHT)
            assert(images[2].width == 100 and images[2].height == 100)
        )";
        QVERIFY(p.run("PATH = '" + imagePath.toUtf8() + "'; WIDTH = " + QByteArray::number(picture.width()) +
                      "; HEIGHT = " + QByteArray::number(picture.height())));
        QVERIFY(p.run(script));
        QCOMPARE(elements.size(), size_t(6));
        const auto& first = std::get<ImageElement>(elements[3]);
        QCOMPARE(first.image.size(), picture.size());
        QVERIFY(std::abs(first.rect.height() - 100.0 * picture.height() / picture.width()) < 1e-6);
        const auto& huge = std::get<ImageElement>(elements[5]);
        const QSizeF page = c->pageSize(0);
        QVERIFY(huge.rect.width() <= page.width() + 1e-6 && huge.rect.height() <= page.height() + 1e-6);
        QVERIFY(std::abs(huge.rect.width() / huge.rect.height() - double(picture.width()) / picture.height()) < 1e-6);
        // One step of undo for the three images
        c->undo();
        QCOMPARE(elements.size(), size_t(3));

        QVERIFY(!p.error("app.addTexts({texts = {{text = 'no place'}}})").isEmpty());
        QVERIFY(!p.error("app.addLinks({links = {{text = 'no url', x = 1, y = 1}}})").isEmpty());
        QVERIFY(!p.error("app.addImages({images = {{x = 1}}})").isEmpty());
        QVERIFY(p.failures.isEmpty());
    }

    void documentPagesAndLayers() {
        PluginFixture p;
        PageCanvas* c = p.f.canvas;
        c->insertPage(1);
        c->insertPage(2);
        c->setCurrentPage(0);
        c->addLayer();
        c->renameLayer(1, QStringLiteral("Notes"));
        p.f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        c->setPageProperties(1, {{QStringLiteral("style"), QStringLiteral("graph")},
                                 {QStringLiteral("color"), QColor(0xff, 0xee, 0xdd)}});

        QVERIFY(p.run(R"(
            local doc = app.getDocumentStructure()
            assert(#doc.pages == 3 and doc.currentPage == 1)
            assert(doc.xoppFilename == "" and doc.pdfBackgroundFilename == "")
            local page = doc.pages[1]
            assert(math.abs(page.pageWidth - 595.27559) < 1e-6 and math.abs(page.pageHeight - 841.88976) < 1e-6)
            assert(page.isAnnotated and not doc.pages[2].isAnnotated)
            assert(page.pageTypeFormat == "plain" and page.backgroundColor == 0xffffff)
            assert(page.pdfBackgroundPageNo == 0)
            assert(doc.pages[2].pageTypeFormat == "graph" and doc.pages[2].backgroundColor == 0xffeedd)
            -- The background is the layer 0; the layers are counted from 1
            assert(#page.layers == 2 and page.layers[0].isVisible)
            assert(page.layers[2].name == "Notes" and page.layers[2].isVisible and page.layers[2].isAnnotated)
            assert(not page.layers[1].isAnnotated)
            assert(page.currentLayer == 2)
        )"));

        // Pages
        QVERIFY(p.run("app.setCurrentPage(2)"));
        QCOMPARE(c->currentPage(), 1);
        QVERIFY(p.run("app.setCurrentPage(99)"));
        QCOMPARE(c->currentPage(), 2);
        QVERIFY(p.run("app.scrollToPage(1); assert(app.getDocumentStructure().currentPage == 1)"));
        QVERIFY(p.run("app.scrollToPage(2, true)"));
        QCOMPARE(c->currentPage(), 2);
        QVERIFY(p.run("app.scrollToPage(-1, true)"));
        QCOMPARE(c->currentPage(), 1);

        QVERIFY(p.run("app.setPageSize(400, 300)"));
        QCOMPARE(c->pageSize(1), QSizeF(400, 300));
        QVERIFY(p.run("app.setPageSize(0, 50, true)"));
        QCOMPARE(c->pageSize(1), QSizeF(400, 350));
        c->undo();
        QCOMPARE(c->pageSize(1), QSizeF(400, 300));
        QVERIFY(!p.error("app.setPageSize(-5, 10)").isEmpty());

        QVERIFY(p.run("app.changeCurrentPageBackground('lined')"));
        QCOMPARE(c->document().pages[1].background.style, QStringLiteral("lined"));
        QVERIFY(!p.error("app.changeCurrentPageBackground('marble')").isEmpty());
        QVERIFY(p.run("app.setBackgroundName('Paper'); assert(app.getDocumentStructure().pages[2].layers[0].name == "
                      "'Paper')"));
        // Without a PDF
        QVERIFY(!p.error("app.changeBackgroundPdfPageNr(1, true)").isEmpty());
        QVERIFY(!p.error("app.changeBackgroundPdfPageNr(1, false)").isEmpty());
        QVERIFY(p.run("local label, message = app.getPageLabel(99); assert(label == nil and message ~= nil)"));
        QVERIFY(p.run("assert(app.getPageLabel(1) == '')"));

        // Layers
        c->setCurrentPage(0);
        QVERIFY(p.run("app.setCurrentLayer(1)"));
        QCOMPARE(c->currentLayer(), 0);
        QVERIFY(c->document().pages[0].layers[1].visible);
        QVERIFY(p.run("app.setCurrentLayer(1, true)"));
        QVERIFY(!c->document().pages[0].layers[1].visible);
        QVERIFY(p.run("app.setCurrentLayer(2); app.setLayerVisibility(true); app.setCurrentLayerName('Renamed')"));
        QVERIFY(c->document().pages[0].layers[1].visible);
        QCOMPARE(c->document().pages[0].layers[1].name, QStringLiteral("Renamed"));
        QVERIFY(p.error("app.setCurrentLayer(5)").contains(QStringLiteral("5")));

        // The view
        QVERIFY(p.run("app.setZoom(2.5); assert(app.getZoom() == 2.5)"));
        QCOMPARE(c->zoom(), 2.5);
        QVERIFY(p.run(R"(
            local before = app.getScrollPos()
            assert(before.width == 800 and before.height == 600)
            app.scrollToPos(30, 40)
            local after = app.getScrollPos()
            assert(math.abs(after.x - before.x - 30) < 1e-6 and math.abs(after.y - before.y - 40) < 1e-6)
            app.scrollToPos(100, 200, false)
            local absolute = app.getScrollPos()
            assert(math.abs(absolute.x - 100) < 1e-6 and math.abs(absolute.y - 200) < 1e-6)
            assert(app.getDisplayDpi() > 0)
            app.refreshPage()
        )"));
        QVERIFY(p.failures.isEmpty());
    }

    void actions() {
        PluginFixture p;
        PageCanvas* c = p.f.canvas;
        QSignalSpy requested(&p.plugins, &PluginController::actionRequested);

        // Tools, with the constants of Xournal++
        QVERIFY(p.run(R"(
            assert(app.C.Tool_pen == 1 and app.C.Tool_text == 4 and app.C.Tool_latex == 26)
            assert(app.getActionState("select-tool") == app.C.Tool_pen)
            app.changeActionState("select-tool", app.C.Tool_text)
            assert(app.getActionState("select-tool") == app.C.Tool_text)
        )"));
        QCOMPARE(c->tool(), PageCanvas::Text);
        QVERIFY(p.run("app.changeActionState('select-tool', app.C.Tool_selectMultiLayerRect)"));
        QCOMPARE(c->tool(), PageCanvas::SelectRect);
        QVERIFY(c->selectAllLayers());
        QVERIFY(p.run("assert(app.getActionState('select-tool') == app.C.Tool_selectMultiLayerRect)\n"
                      "app.uiAction({action = 'ACTION_TOOL_SELECT_REGION'})"));
        QCOMPARE(c->tool(), PageCanvas::SelectRegion);
        QVERIFY(!c->selectAllLayers());
        QVERIFY(p.run(
                "app.uiAction({action = 'ACTION_TOOL_HIGHLIGHTER'}); app.uiAction({action = 'ACTION_SIZE_FINE'})"));
        QCOMPARE(c->tool(), PageCanvas::Highlighter);
        QCOMPARE(c->toolSize(), PageCanvas::Fine);
        QVERIFY(p.run("app.uiAction({action = 'ACTION_TOOL_PEN'}); app.uiAction({action = "
                      "'ACTION_TOOL_PEN_SIZE_VERY_THICK'})"));
        QCOMPARE(c->toolSize(), PageCanvas::VeryThick);
        QVERIFY(!p.error("app.changeActionState('select-tool', 99)").isEmpty());

        // Settings of the tools
        QVERIFY(p.run(R"(
            app.changeActionState("tool-color", 0xff0000)
            assert(app.getActionState("tool-color") == 0xff0000)
            app.changeActionState("tool-pen-line-style", "dash")
            assert(app.getActionState("tool-pen-line-style") == "dash")
            app.activateAction("tool-fill")
            assert(app.getActionState("tool-fill") == true)
            app.changeActionState("tool-eraser-type", app.C.EraserType_whiteout)
            assert(app.getActionState("tool-eraser-type") == app.C.EraserType_whiteout)
            app.changeActionState("tool-highlighter-size", app.C.ToolSize_thick)
            assert(app.getActionState("tool-highlighter-size") == app.C.ToolSize_thick)
        )"));
        QCOMPARE(c->color(), QColor(0xff, 0x00, 0x00));
        QCOMPARE(c->lineStyle(), QStringLiteral("dash"));
        QVERIFY(c->fill());
        QCOMPARE(c->eraserType(), PageCanvas::EraseWhiteout);
        QCOMPARE(c->tool(), PageCanvas::Pen);  // changing the highlighter did not select it

        // Shapes and the geometry tools are switched on and off
        QVERIFY(p.run("app.uiAction({action = 'ACTION_TOOL_DRAW_ELLIPSE'}); "
                      "assert(app.getActionState('tool-draw-ellipse'))"));
        QCOMPARE(c->drawingType(), PageCanvas::Ellipse);
        QVERIFY(p.run("app.uiAction({action = 'ACTION_TOOL_DRAW_ELLIPSE', enabled = false})"));
        QCOMPARE(c->drawingType(), PageCanvas::Freehand);
        QVERIFY(p.run("app.uiAction({action = 'ACTION_RULER'})"));
        QCOMPARE(c->drawingType(), PageCanvas::Line);
        QVERIFY(p.run("app.activateAction('setsquare'); assert(app.getActionState('setsquare'))"));
        QCOMPARE(c->geometryTool(), PageCanvas::Setsquare);
        QVERIFY(p.run("app.activateAction('setsquare')"));
        QCOMPARE(c->geometryTool(), PageCanvas::NoGeometryTool);

        // The view
        QVERIFY(p.run(R"(
            app.changeActionState("grid-snapping", false)
            assert(app.getActionState("grid-snapping") == false)
            app.changeActionState("set-layout-vertical", true)
            app.changeActionState("set-columns-or-rows", -3)
            assert(app.getActionState("set-columns-or-rows") == -3 and app.getActionState("set-layout-vertical"))
            app.changeActionState("set-columns-or-rows", 2)
            app.changeActionState("zoom", 2.25)
            assert(app.getActionState("zoom") == 2.25)
            app.activateAction("zoom-100")
        )"));
        QVERIFY(!c->input()->snapGrid);
        QVERIFY(c->layoutVertical());
        QCOMPARE(c->layoutColumns(), 2);
        QCOMPARE(c->zoom(), 1.0);

        // Pages and layers
        QVERIFY(p.run("app.activateAction('new-page-after'); app.activateAction('new-page-at-end'); "
                      "app.activateAction('goto-first')"));
        QCOMPARE(c->pageCount(), 3);
        QCOMPARE(c->currentPage(), 0);
        QVERIFY(p.run("app.activateAction('goto-next'); app.activateAction('delete-page'); app.sidebarAction('COPY')"));
        QCOMPARE(c->pageCount(), 3);
        QVERIFY(p.run("app.activateAction('goto-last'); app.activateAction('layer-new-above-current'); "
                      "app.activateAction('layer-new-below-current')"));
        QCOMPARE(c->currentPage(), 2);
        QCOMPARE(c->layers().size(), 3);
        QCOMPARE(c->currentLayer(), 1);
        QVERIFY(p.run("app.activateAction('layer-goto-top'); assert(app.getActionState('layer-active') == 3)\n"
                      "app.layerAction('ACTION_DELETE_LAYER')"));
        QCOMPARE(c->layers().size(), 2);

        // Editing
        p.f.strokeOnPage(c->currentPage(), {QPointF(100, 100), QPointF(300, 100)});
        QCOMPARE(p.f.strokes(c->currentPage()).size(), 1);
        QVERIFY(p.run("app.activateAction('select-all'); app.activateAction('delete')"));
        QCOMPARE(p.f.strokes(c->currentPage()).size(), 0);
        QVERIFY(p.run("app.uiAction({action = 'ACTION_UNDO'})"));
        QCOMPARE(p.f.strokes(c->currentPage()).size(), 1);

        // What belongs to the window is passed on
        QCOMPARE(requested.count(), 0);
        QVERIFY(p.run("app.changeActionState('position-highlighting', true); app.activateAction('fullscreen'); "
                      "app.uiAction({action = 'ACTION_SETTINGS'}); app.showFloatingToolbox(100, 200)"));
        QCOMPARE(requested.count(), 3);
        QCOMPARE(requested[0][0].toString(), QStringLiteral("position-highlighting"));
        QCOMPARE(requested[0][1], QVariant(true));
        QCOMPARE(requested[1][0].toString(), QStringLiteral("fullscreen"));
        QVERIFY(!requested[1][1].isValid());
        QCOMPARE(requested[2][0].toString(), QStringLiteral("preferences"));
        QVERIFY(p.run("assert(app.getActionState('position-highlighting') == nil)"));
        QVERIFY(p.failures.isEmpty());
    }

    void toolsColoursAndFonts() {
        PluginFixture p;
        PageCanvas* c = p.f.canvas;
        p.plugins.setPalette({QVariantMap{{QStringLiteral("name"), QStringLiteral("White")},
                                          {QStringLiteral("color"), QColor(Qt::white)}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("Rosewater")},
                                          {QStringLiteral("color"), QColor(0xdc, 0x8a, 0x78)}}});
        c->setFill(true);
        c->setFillAlpha(99);
        c->setDrawingType(PageCanvas::Arrow);
        c->setLineStyle(QStringLiteral("dot"));
        c->setToolSize(PageCanvas::Fine);

        QVERIFY(p.run(R"(
            local palette = app.getColorPalette()
            assert(#palette == 2 and palette[1].color == 0xffffff and palette[2].name == "Rosewater")
            assert(palette[2].color == 0xdc8a78)

            local pen = app.getToolInfo("pen")
            assert(pen.size.name == "thin" and math.abs(pen.size.value - 0.85) < 1e-9)
            assert(pen.color == 0x002e99 and pen.filled == true and pen.fillOpacity == 99)
            assert(pen.drawingType == "arrow" and pen.lineStyle == "dot")
            local active = app.getToolInfo("active")
            assert(active.type == "pen" and active.size.name == "thin" and active.drawingType == "arrow")
            assert(math.abs(active.thickness - 0.85) < 1e-9 and active.fillOpacity == 99)
            local highlighter = app.getToolInfo("highlighter")
            assert(highlighter.size.name == "medium" and highlighter.filled == false and highlighter.lineStyle == nil)
            local eraser = app.getToolInfo("eraser")
            assert(eraser.type == "default" and eraser.size.name == "medium" and eraser.size.value > 0)
            local text = app.getToolInfo("text")
            assert(text.font.name == "Sans" and text.font.size == 12 and text.color == 0)
        )"));
        QVERIFY(!p.error("app.getToolInfo('hammer')").isEmpty());
        QVERIFY(!p.error("app.getToolInfo('selection')").isEmpty());

        // Colours: of a tool, or of the selected one together with the selection
        QVERIFY(p.run("app.changeToolColor({color = 0xff00ff, tool = 'HIGHLIGHTER'})"));
        QCOMPARE(c->toolInfo(PageCanvas::Highlighter).value(QStringLiteral("color")).value<QColor>(),
                 QColor(0xff, 0x00, 0xff));
        QCOMPARE(c->color(), PEN_COLOR);
        c->setDrawingType(PageCanvas::Freehand);
        p.f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        c->selectAll();
        QVERIFY(p.run("app.changeToolColor({color = 0x00ff00, selection = true})"));
        QCOMPARE(c->color(), QColor(0x00, 0xff, 0x00));
        QCOMPARE(p.f.strokes()[0].color, QColor(0x00, 0xff, 0x00));
        QVERIFY(!p.error("app.changeToolColor({tool = 'pen'})").isEmpty());
        QVERIFY(!p.error("app.changeToolColor({color = 1, tool = 'eraser'})").isEmpty());

        // Fonts
        const QString family = QFontDatabase::families().first();
        QVERIFY(p.run(
                "local fonts = app.getFonts(); assert(#fonts.families > 0 and type(fonts.families[1]) == 'string')\n"
                "local font = app.getFont(); assert(font.name == 'Sans' and font.size == 12)\n"
                "app.setFont({size = 14}); assert(app.getFont().size == 14 and app.getFont().name == 'Sans')"));
        QVERIFY(p.run("app.setFont({name = '" + family.toUtf8() + "'})"));
        QCOMPARE(c->textFamily(), family);
        QVERIFY(p.run("app.setFont('" + family.toUtf8() + " 20.5')"));
        QCOMPARE(c->textSize(), 20.5);
        QCOMPARE(c->textFamily(), family);
        QVERIFY(p.error("app.setFont('No Such Font Anywhere 12')").contains(QStringLiteral("No Such Font Anywhere")));
        QCOMPARE(c->textSize(), 20.5);
        QVERIFY(p.failures.isEmpty());
    }

    void files() {
        PluginFixture p;
        PageCanvas* c = p.f.canvas;
        QTemporaryDir out;
        p.f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        c->insertPage(1);
        QVERIFY(p.run("DIR = '" + out.path().toUtf8() + "'"));

        QVERIFY(p.run(R"(
            assert(app.export({outputFile = DIR .. "/all.pdf"}))
            assert(app.export({outputFile = DIR .. "/one.png", range = "1", background = "none", pngWidth = 200}))
            assert(app.export({outputFile = DIR .. "/pages.png", background = "unruled", pngDpi = 36}))
        )"));
        QVERIFY(QFileInfo(out.filePath(QStringLiteral("all.pdf"))).size() > 0);
        const QImage one(out.filePath(QStringLiteral("one.png")));
        QCOMPARE(one.width(), 200);
        QCOMPARE(one.pixelColor(5, 5).alpha(), 0);
        QVERIFY(QFileInfo::exists(out.filePath(QStringLiteral("pages-1.png"))));
        QVERIFY(QFileInfo::exists(out.filePath(QStringLiteral("pages-2.png"))));
        QCOMPARE(QImage(out.filePath(QStringLiteral("pages-1.png"))).width(), 298);
        QVERIFY(!p.error("app.export({})").isEmpty());
        QSignalSpy exportFailed(c, &PageCanvas::exportFailed);
        QVERIFY(p.run("assert(app.export({outputFile = DIR .. '/x.png', range = '7'}) == false)"));
        QCOMPARE(exportFailed.count(), 1);

        // A folder of its own for each plugin
        QVERIFY(p.run(R"(
            local config, data, state = app.getFolder("config"), app.getFolder("data"), app.getFolder("state")
            assert(config:match("plugin%-settings/test$") and data:match("plugin%-data/test$") and state:match("test$"))
            for _, folder in ipairs({config, data, state}) do
                local file = assert(io.open(folder .. "/probe", "w"))
                file:close()
                assert(os.remove(folder .. "/probe"))
            end
        )"));
        QVERIFY(!p.error("app.getFolder('cache')").isEmpty());

        writeFile(out.filePath(QStringLiteral("from.txt")), "content");
        writeFile(out.filePath(QStringLiteral("to.txt")), "old");
        QVERIFY(p.run("assert(app.glib_rename(DIR .. '/from.txt', DIR .. '/to.txt') == 1)\n"
                      "local result, message = app.glib_rename(DIR .. '/from.txt', DIR .. '/other.txt')\n"
                      "assert(result == nil and message ~= nil)"));
        QFile renamed(out.filePath(QStringLiteral("to.txt")));
        QVERIFY(renamed.open(QIODevice::ReadOnly));
        QCOMPARE(renamed.readAll(), QByteArray("content"));

        // Opening a document: the window asks first if there are changes, unless told otherwise
        const QByteArray document = QByteArray(TEST_DATA_DIR "/pages.xopp");
        QSignalSpy requested(&p.plugins, &PluginController::actionRequested);
        QVERIFY(c->modified());
        QVERIFY(p.run("assert(app.openFile('" + document + "', 2) == true)"));
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested[0][0].toString(), QStringLiteral("open-file"));
        QCOMPARE(requested[0][1].toMap().value(QStringLiteral("page")).toInt(), 2);
        QVERIFY(!c->hasFile());
        QVERIFY(p.run("assert(app.openFile('" + document + "', 3, true) == true)"));
        QVERIFY(c->hasFile());
        QCOMPARE(c->currentPage(), 2);
        QVERIFY(p.run("assert(app.getDocumentStructure().xoppFilename == '" + document + "')"));
        QVERIFY(p.run("assert(app.openFile(DIR .. '/missing.xopp') == false)"));
        QVERIFY(p.run("assert(app.openFile(DIR .. '/to.txt', 1, true) == false)"));
        QVERIFY(p.failures.isEmpty());
    }

#ifdef LUA_TEST_MODULE
    /// Lua modules written in C, as luarocks installs them, can be loaded
    void modulesWrittenInC() {
        PluginFixture p;
        QVERIFY(p.run("package.cpath = '" LUA_TEST_MODULE "' .. ';' .. package.cpath"));
        QVERIFY(p.run("local m = require 'xoj_test_module'; assert(m.answer() == 42, 'wrong answer')"));
        // Where luarocks puts the modules of the user is searched
        QVERIFY(p.run("assert(package.cpath:find('luarocks', 1, true), package.cpath)"));
        QVERIFY(p.run("assert(package.path:find('luarocks', 1, true), package.path)"));
        // The FFI of LuaJIT, which lua-vips needs, is taken from cffi-lua where that is installed
        const QString error = p.error("require 'ffi'");
        QVERIFY2(error.contains(QStringLiteral("cffi")), qPrintable(error));
    }
#endif

    /// What a plugin prints reaches the window: plugins tell that way why they did nothing
    void printing() {
        PluginFixture p;
        QSignalSpy printed(&p.plugins, &PluginController::pluginPrinted);
        QVERIFY(p.run("print('Missing screenshot utility.', 42, nil, true)"));
        QCOMPARE(printed.count(), 1);
        QCOMPARE(printed[0][0].toString(), QStringLiteral("test"));
        QCOMPARE(printed[0][1].toString(), QStringLiteral("Missing screenshot utility.\t42\tnil\ttrue"));
    }

    void pluginsInTheApplication() {
        // On Android and iOS the plugins are resources of the application: Lua cannot read them as files
        PluginController plugins;
        plugins.setSearchPaths({QStringLiteral(":/plugins")});
        {
            QSettings settings;
            settings.setValue(QStringLiteral("plugins/Example/enabled"), true);
        }
        QStringList failures;
        connect(&plugins, &PluginController::pluginFailed, this,
                [&](const QString&, const QString& message) { failures.append(message); });
        plugins.load();
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
        QCOMPARE(plugins.plugins().size(), 1);
        // The plugin has loaded its second file with require, and registered its menu entry
        QCOMPARE(plugins.menuEntries().size(), 1);
        QCOMPARE(plugins.menuEntries()[0].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Test123"));

        // A module that is not there is an ordinary Lua error, and nothing outside of the folder is read
        const QString missing = plugins.run(QStringLiteral("Example"), QStringLiteral("require 'nothing'"));
        QVERIFY2(missing.contains(QStringLiteral("nothing")), qPrintable(missing));
        QVERIFY(!plugins.run(QStringLiteral("Example"), QStringLiteral("require '../ColorCycle/main'")).isEmpty());
    }

    void pluginsOfXournalpp() {
        // All of them start
        PluginController plugins;
        plugins.setSearchPaths({QStringLiteral(PLUGINS_DIR)});
        plugins.load();
        QStringList names;
        for (const QVariant& plugin: plugins.plugins()) {
            names.append(plugin.toMap().value(QStringLiteral("name")).toString());
            QVERIFY2(plugin.toMap().value(QStringLiteral("valid")).toBool(), qPrintable(names.last()));
            // They are unchanged and keep the version of the Xournal++ they are from
            QCOMPARE(plugin.toMap().value(QStringLiteral("version")).toString(), QStringLiteral("1.3.8"));
        }
        QVERIFY2(names.size() >= 11, qPrintable(names.join(u' ')));
        {
            QSettings settings;
            for (const QString& name: std::as_const(names)) {
                settings.setValue(QStringLiteral("plugins/%1/enabled").arg(name), true);
            }
        }
        Fixture f;
        PageCanvas* c = f.canvas;
        plugins.setCanvas(c);
        QStringList failures;
        connect(&plugins, &PluginController::pluginFailed, this, [&](const QString& name, const QString& message) {
            failures.append(name + QStringLiteral(": ") + message);
        });
        plugins.load();
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
        const QVariantList menu = plugins.menuEntries();
        QVERIFY2(menu.size() >= 25, qPrintable(QString::number(menu.size())));
        QCOMPARE(entryNamed(menu, QStringLiteral("Clone non-background layers to next page"))
                         .value(QStringLiteral("shortcut"))
                         .toString(),
                 QStringLiteral("Ctrl+Shift+C"));
        auto trigger = [&](const QString& text) {
            const QVariantMap entry = entryNamed(plugins.menuEntries(), text);
            if (entry.isEmpty()) {
                failures.append(QStringLiteral("no menu entry \"%1\"").arg(text));
                return;
            }
            plugins.trigger(entry.value(QStringLiteral("id")).toInt());
        };

        // ToggleGrid: the paper and the snapping
        trigger(QStringLiteral("Toggle Grid Paper"));
        QCOMPARE(c->document().pages[0].background.style, QStringLiteral("plain"));
        QVERIFY(!c->input()->snapGrid);
        trigger(QStringLiteral("Toggle Grid Paper"));
        QCOMPARE(c->document().pages[0].background.style, QStringLiteral("graph"));
        QVERIFY(c->input()->snapGrid);

        // ColorCycle: the colour of the pen, and of what is selected
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        c->selectAll();
        trigger(QStringLiteral("Cycle through color list"));
        trigger(QStringLiteral("Cycle through color list"));
        QCOMPARE(c->color(), QColor(0x00, 0x80, 0x00));
        QCOMPARE(f.strokes()[0].color, QColor(0x00, 0x80, 0x00));
        c->clearSelection();

        // FitToContent: the page shrinks to the strokes, which move to a new layer
        f.strokeOnPage(0, {QPointF(200, 300), QPointF(250, 320)});
        trigger(QStringLiteral("Fit page to layer"));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
        QCOMPARE(c->layers().size(), 2);
        QCOMPARE(c->layers()[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("moved for fitting"));
        const double width = f.strokes()[0].width;
        QVERIFY2(std::abs(c->pageSize(0).width() - (200 + width)) < 1.0,
                 qPrintable(QString::number(c->pageSize(0).width())));
        QVERIFY(std::abs(c->pageSize(0).height() - (220 + width)) < 1.0);
        QCOMPARE(f.strokes().size(), 4);

        // LayerActions
        c->insertPage(1);
        trigger(QStringLiteral("Add new top layer on each page"));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
        QCOMPARE(c->document().pages[0].layers.size(), size_t(3));
        QCOMPARE(c->document().pages[1].layers.size(), size_t(2));
        trigger(QStringLiteral("Hide all layers except first layers and backgrounds"));
        QVERIFY(c->document().pages[0].layers[0].visible);
        QVERIFY(!c->document().pages[0].layers[1].visible);
        QVERIFY(!c->document().pages[0].layers[2].visible);
        QVERIFY(!c->document().pages[1].layers[1].visible);

        // SpaceForNotes: an empty page after every page
        const int pages = c->pageCount();
        trigger(QStringLiteral("Add empty pages after every page"));
        QCOMPARE(c->pageCount(), 2 * pages);

        // Export: next to the document
        QTemporaryDir dir;
        QVERIFY(c->saveAs(QUrl::fromLocalFile(dir.filePath(QStringLiteral("notes.xopp")))));
        trigger(QStringLiteral("Export to pdf"));
        trigger(QStringLiteral("Export to png"));
        QVERIFY(QFileInfo(dir.filePath(QStringLiteral("notes_export.pdf"))).size() > 0);
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("notes_export-1.png"))));

        // LayerActions asks with a dialog when there is nothing to clone to
        QSignalSpy dialog(&plugins, &PluginController::dialogRequested);
        c->setCurrentPage(c->pageCount() - 1);
        trigger(QStringLiteral("Clone non-background layers to next page"));
        QCOMPARE(dialog.count(), 1);
        QVERIFY(dialog[0][4].toBool());
        plugins.dialogFinished(dialog[0][0].toInt(), 1);

        // ImageActions needs the Lua module "vips", which is not part of Lua, and says so
        trigger(QStringLiteral("Invert selected images"));
        QCOMPARE(dialog.count(), 2);
        QVERIFY(dialog[1][2].toString().contains(QStringLiteral("lua-vips")));
        plugins.dialogFinished(dialog[1][0].toInt(), 1);

        // HighlightPosition and the example use the window
        QSignalSpy requested(&plugins, &PluginController::actionRequested);
        trigger(QStringLiteral("Toggle Highlight Position"));
        QCOMPARE(requested.count(), 1);
        trigger(QStringLiteral("Test123"));
        QCOMPARE(dialog.count(), 3);
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    }
#endif

private:
    QTemporaryDir m_settingsDir;
};

QTEST_MAIN(TestPlugins)
#include "tst_plugins.moc"

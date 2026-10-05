/*
 * Qournal
 *
 * Tests of the toolbar configurations and of the toolbar.ini of Xournal++
 *
 * @license GNU GPLv2 or later
 */

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "ToolbarIni.h"
#include "ToolbarModel.h"

using namespace Qt::StringLiterals;

class TestToolbars: public QObject {
    Q_OBJECT

private:
    static QByteArray predefined() {
        QFile file(QStringLiteral(TOOLBAR_INI));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private slots:
    /// The toolbar.ini that comes with Xournal++ is read: every configuration, and every item is known here
    void readsThePredefinedFile() {
        const QList<ToolbarIni::Config> configs = ToolbarIni::parse(predefined(), true);
        QStringList ids;
        for (const ToolbarIni::Config& config: configs) {
            ids.append(config.id);
            QVERIFY(config.predefined);
            for (const QStringList& items: config.bars) {
                for (const QString& item: items) {
                    QVERIFY2(!item.startsWith(u"xournalpp:"), qPrintable(config.id + u": "_s + item));
                }
            }
        }
        QCOMPARE(ids, (QStringList{u"All in"_s, u"Portrait"_s, u"Minimal Left"_s, u"Minimal Top"_s, u"Xournal++"_s,
                                   u"Right hand Note Taking"_s, u"Toolbar Left"_s, u"Toolbar Right"_s,
                                   u"Floating Toolbox (experimental)"_s, u"Empty Toolbar"_s}));

        const ToolbarIni::Config& allIn = configs[0];
        QCOMPARE(allIn.name, u"All Tools"_s);
        QCOMPARE(allIn.bars.keys(), (QStringList{u"bottom1"_s, u"left1"_s, u"left2"_s, u"top1"_s, u"top2"_s}));
        QCOMPARE(allIn.bars[u"top1"_s].mid(0, 5),
                 (QStringList{u"save"_s, u"new"_s, u"open"_s, u"separator"_s, u"cut"_s}));
        // An old name of an item, and a colour of the palette
        QVERIFY(allIn.bars[u"top2"_s].contains(u"highlighter"_s));
        QVERIFY(allIn.bars[u"top2"_s].contains(u"color:10"_s));
        // Blanks around the names, as in "Portrait"
        QCOMPARE(configs[1].bars[u"top1"_s].last(), u"font"_s);
        QCOMPARE(configs[8].bars[u"float4"_s],
                 (QStringList{u"color:10"_s, u"color:1"_s, u"color:8"_s, u"color:4"_s, u"drawArrow"_s}));
        QVERIFY(configs[9].bars.isEmpty());
    }

    void names() {
        QCOMPARE(ToolbarIni::toId(u"RULER"_s), u"drawLine"_s);
        QCOMPARE(ToolbarIni::fromId(u"drawLine"_s), u"RULER"_s);
        QCOMPARE(ToolbarIni::toId(u"DASH-/ DOTTED"_s), u"lineDashDotted"_s);
        QCOMPARE(ToolbarIni::toId(u"TWO_PAGES"_s), u"pairedPages"_s);
        QCOMPARE(ToolbarIni::toId(u"COLOR(3)"_s), u"color:3"_s);
        QCOMPARE(ToolbarIni::fromId(u"color:3"_s), u"COLOR(3)"_s);
        // What only one of the two applications has is kept
        QCOMPARE(ToolbarIni::toId(u"SOMETHING_NEW"_s), u"xournalpp:SOMETHING_NEW"_s);
        QCOMPARE(ToolbarIni::fromId(u"xournalpp:SOMETHING_NEW"_s), u"SOMETHING_NEW"_s);
        QCOMPARE(ToolbarIni::fromId(u"zoomLabel"_s), u"QT(zoomLabel)"_s);
        QCOMPARE(ToolbarIni::toId(u"QT(zoomLabel)"_s), u"zoomLabel"_s);
        QCOMPARE(ToolbarIni::toId(u"Plugin::Export"_s), u"Plugin::Export"_s);
        QCOMPARE(ToolbarIni::fromId(u"Plugin::Export"_s), u"Plugin::Export"_s);
    }

    void parsesAndWrites() {
        const QByteArray data = "# a comment\n"
                                "[Mine]\n"
                                "toolbarTop1 = PEN, ERASER ,SEPARATOR,COLOR(0xff0000),COLOR(0x00ff00),UNKNOWN_ITEM\n"
                                "toolbarRight2=QT(colors)\n"
                                "toolbarNowhere=PEN\n"
                                "name=My toolbars\n"
                                "name[de]=Meine Leisten\n"
                                "name[fr]=Mes barres\n"
                                "[Second]\n"
                                "toolbarFloat1=UNDO\n";
        QList<ToolbarIni::Config> configs = ToolbarIni::parse(data, false, u"de_CH"_s);
        QCOMPARE(configs.size(), 2);
        QCOMPARE(configs[0].id, u"Mine"_s);
        QCOMPARE(configs[0].name, u"Meine Leisten"_s);
        // Colours by value stand for the colours of the palette, in their order
        QCOMPARE(configs[0].bars[u"top1"_s], (QStringList{u"pen"_s, u"eraser"_s, u"separator"_s, u"color:0"_s,
                                                          u"color:1"_s, u"xournalpp:UNKNOWN_ITEM"_s}));
        QCOMPARE(configs[0].bars[u"right2"_s], QStringList{u"colors"_s});
        QCOMPARE(configs[0].bars.size(), 2);
        QCOMPARE(configs[1].name, u"Second"_s);  // without a name: the id
        QCOMPARE(ToolbarIni::parse(data, false)[0].name, u"My toolbars"_s);

        // Written and read again, nothing is lost; predefined ones are not written
        configs[1].predefined = true;
        const QByteArray written = ToolbarIni::write(configs);
        QVERIFY(written.contains("toolbarTop1=PEN,ERASER,SEPARATOR,COLOR(0),COLOR(1),UNKNOWN_ITEM\n"));
        const QList<ToolbarIni::Config> again = ToolbarIni::parse(written, false);
        QCOMPARE(again.size(), 1);
        QCOMPARE(again[0].id, configs[0].id);
        QCOMPARE(again[0].name, configs[0].name);
        QCOMPARE(again[0].bars, configs[0].bars);
    }

    /// The model: choosing, changing (a copy of a predefined configuration), renaming, deleting, keeping
    void model() {
        QTemporaryDir dir;
        const QString file = dir.filePath(u"sub/toolbar.ini"_s);
        ToolbarModel model;
        model.setFile(file);
        model.setPredefinedFile(QStringLiteral(TOOLBAR_INI));
        model.load();
        QCOMPARE(model.configs().size(), 12);  // two of ours, ten of Xournal++
        QCOMPARE(model.shown(), ToolbarModel::DESKTOP_ID);
        QVERIFY(model.shownPredefined());
        QVERIFY(model.bars()[u"top1"_s].toStringList().contains(u"pen"_s));
        QVERIFY(model.bars()[u"left1"_s].toStringList().isEmpty());

        // The fallback follows the layout; an unknown id falls back to it
        model.setFallback(ToolbarModel::TABLET_ID);
        QCOMPARE(model.shown(), ToolbarModel::TABLET_ID);
        model.setCurrent(u"does not exist"_s);
        QCOMPARE(model.shown(), ToolbarModel::TABLET_ID);

        QSignalSpy barsChanged(&model, &ToolbarModel::barsChanged);
        model.setCurrent(u"Toolbar Left"_s);
        QCOMPARE(barsChanged.count(), 1);
        QCOMPARE(model.shownName(), u"Toolbar Left"_s);
        QVERIFY(model.bars()[u"left1"_s].toStringList().contains(u"zoomSlider"_s));

        // Changing a predefined configuration changes a copy of it
        model.setItems(u"right1"_s, {u"pen"_s, u"zoomLabel"_s});
        QCOMPARE(model.shown(), u"Toolbar Left Copy"_s);
        QVERIFY(!model.shownPredefined());
        QCOMPARE(model.configs().size(), 13);
        QCOMPARE(model.bars()[u"right1"_s].toStringList(), (QStringList{u"pen"_s, u"zoomLabel"_s}));
        QVERIFY(model.bars()[u"left1"_s].toStringList().contains(u"zoomSlider"_s));
        // ... and the next change stays in that copy
        model.setItems(u"left1"_s, {});
        QCOMPARE(model.shown(), u"Toolbar Left Copy"_s);
        QCOMPARE(model.configs().size(), 13);
        model.renameShown(u"Mine"_s);
        QCOMPARE(model.shownName(), u"Mine"_s);

        // It is kept in the file, which Xournal++ could read
        QVERIFY(QFile::exists(file));
        ToolbarModel second;
        second.setFile(file);
        second.setPredefinedFile(QStringLiteral(TOOLBAR_INI));
        second.load();
        second.setCurrent(u"Toolbar Left Copy"_s);
        QCOMPARE(second.shownName(), u"Mine"_s);
        QCOMPARE(second.bars(), model.bars());
        QVERIFY(!second.shownPredefined());

        // Predefined ones cannot be renamed or deleted
        second.setCurrent(u"Portrait"_s);
        second.renameShown(u"Other"_s);
        second.removeShown();
        QCOMPARE(second.shownName(), u"Portrait"_s);
        QCOMPARE(second.configs().size(), 13);
        second.setCurrent(u"Toolbar Left Copy"_s);
        second.removeShown();
        QCOMPARE(second.configs().size(), 12);
        QCOMPARE(second.shown(), ToolbarModel::DESKTOP_ID);

        const QString added = second.add(u"Xournal++"_s, {{u"top1"_s, QStringList{u"pen"_s}}});
        QCOMPARE(added, u"Xournal++ 2"_s);  // the id of a predefined one is not taken
        QCOMPARE(second.shown(), added);
        QCOMPARE(second.bars()[u"top1"_s].toStringList(), QStringList{u"pen"_s});
    }

    /// Configurations made with Xournal++ are taken over from its toolbar.ini
    void import() {
        QTemporaryDir dir;
        const QString theirs = dir.filePath(u"theirs.ini"_s);
        QFile out(theirs);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("[Portrait Copy]\ntoolbarTop1=PEN,HILIGHTER\ntoolbarLeft1=UNDO\nname=Portrait Copy\n"
                  "[Portrait]\ntoolbarBottom1=PAGE_SPIN\nname=Mine\n"
                  "[Nothing]\nname=Nothing\n");
        out.close();

        ToolbarModel model;
        model.setFile(dir.filePath(u"toolbar.ini"_s));
        model.setPredefinedFile(QStringLiteral(TOOLBAR_INI));
        model.load();
        QCOMPARE(model.importFile(QUrl::fromLocalFile(theirs)), 2);  // not the empty one
        QCOMPARE(model.configs().size(), 14);
        model.setCurrent(u"Portrait Copy"_s);
        QCOMPARE(model.bars()[u"top1"_s].toStringList(), (QStringList{u"pen"_s, u"highlighter"_s}));
        // The id of a predefined configuration was taken
        model.setCurrent(u"Portrait 2"_s);
        QCOMPARE(model.shownName(), u"Mine"_s);
        QVERIFY(!model.shownPredefined());

        QCOMPARE(model.importFile(QUrl::fromLocalFile(theirs)), 0);  // nothing new
        QCOMPARE(model.importFile(QUrl::fromLocalFile(dir.filePath(u"missing.ini"_s))), 0);
    }
};

QTEST_MAIN(TestToolbars)
#include "tst_toolbars.moc"

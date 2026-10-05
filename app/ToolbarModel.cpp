#include "ToolbarModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

const QString ToolbarModel::DESKTOP_ID = u"Qournal"_s;
const QString ToolbarModel::TABLET_ID = u"Qournal Tablet"_s;

namespace {

/// The configurations of this application: one toolbar at the top, and the floating toolbox
QList<ToolbarIni::Config> ownConfigs() {
    const QStringList toolbox = {u"pen"_s,          u"highlighter"_s, u"eraser"_s, u"hand"_s, u"selectRect"_s,
                                 u"selectRegion"_s, u"text"_s,        u"undo"_s,   u"redo"_s, u"colors"_s};

    ToolbarIni::Config desktop;
    desktop.id = ToolbarModel::DESKTOP_ID;
    desktop.name = ToolbarModel::tr("Qournal");
    desktop.predefined = true;
    desktop.bars[u"top1"_s] = {
            u"new"_s,          u"open"_s,         u"save"_s,        u"separator"_s, u"undo"_s,      u"redo"_s,
            u"separator"_s,    u"pen"_s,          u"highlighter"_s, u"eraser"_s,    u"hand"_s,      u"selectRect"_s,
            u"selectRegion"_s, u"selectObject"_s, u"text"_s,        u"image"_s,     u"separator"_s, u"drawingType"_s,
            u"eraserType"_s,   u"separator"_s,    u"colors"_s,      u"separator"_s, u"size"_s,      u"lineStyle"_s,
            u"fill"_s,         u"font"_s,         u"separator"_s,   u"zoomOut"_s,   u"zoomLabel"_s, u"zoomIn"_s,
            u"zoom100"_s,      u"fitWidth"_s,     u"separator"_s,   u"present"_s,
    };
    desktop.bars[u"float1"_s] = toolbox;

    // Fewer items, so that they fit a tablet; the rest is in the menu
    ToolbarIni::Config tablet;
    tablet.id = ToolbarModel::TABLET_ID;
    tablet.name = ToolbarModel::tr("Tablet");
    tablet.predefined = true;
    tablet.bars[u"top1"_s] = {
            u"undo"_s,      u"redo"_s,        u"separator"_s,    u"pen"_s,         u"highlighter"_s,
            u"eraser"_s,    u"hand"_s,        u"selectRegion"_s, u"text"_s,        u"image"_s,
            u"separator"_s, u"drawingType"_s, u"eraserType"_s,   u"size"_s,        u"font"_s,
            u"separator"_s, u"colors"_s,      u"separator"_s,    u"fingerDraws"_s,
    };
    tablet.bars[u"float1"_s] = toolbox;
    return {desktop, tablet};
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace

ToolbarModel::ToolbarModel(QObject* parent):
        QObject(parent),
        m_fallback(DESKTOP_ID),
        m_file(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + u"/toolbar.ini"_s),
        m_predefinedFile(u":/toolbar.ini"_s) {
    m_configs = ownConfigs();
}

QVariantList ToolbarModel::configs() const {
    QVariantList result;
    for (const ToolbarIni::Config& config: m_configs) {
        result.append(
                QVariantMap{{u"id"_s, config.id}, {u"name"_s, config.name}, {u"predefined"_s, config.predefined}});
    }
    return result;
}

const ToolbarIni::Config* ToolbarModel::find(const QString& id) const {
    for (const ToolbarIni::Config& config: m_configs) {
        if (config.id == id) {
            return &config;
        }
    }
    return nullptr;
}

QString ToolbarModel::shown() const {
    if (find(m_current)) {
        return m_current;
    }
    return find(m_fallback) ? m_fallback : DESKTOP_ID;
}

ToolbarIni::Config* ToolbarModel::shownConfig() { return const_cast<ToolbarIni::Config*>(find(shown())); }

QString ToolbarModel::shownName() const { return find(shown())->name; }

bool ToolbarModel::shownPredefined() const { return find(shown())->predefined; }

QVariantMap ToolbarModel::bars() const {
    const ToolbarIni::Config* config = find(shown());
    QVariantMap result;
    for (const QString& bar: ToolbarIni::barNames()) {
        result[bar] = config->bars.value(bar);
    }
    return result;
}

void ToolbarModel::setCurrent(const QString& id) {
    if (id == m_current) {
        return;
    }
    m_current = id;
    emit currentChanged();
    emit barsChanged();
}

void ToolbarModel::setFallback(const QString& id) {
    if (id == m_fallback) {
        return;
    }
    m_fallback = id;
    emit currentChanged();
    emit barsChanged();
}

void ToolbarModel::setFile(const QString& file) {
    if (file != m_file) {
        m_file = file;
        emit fileChanged();
    }
}

void ToolbarModel::setPredefinedFile(const QString& file) {
    if (file != m_predefinedFile) {
        m_predefinedFile = file;
        emit fileChanged();
    }
}

void ToolbarModel::load() {
    const QString language = QLocale().name();
    m_configs = ownConfigs();
    // A configuration of the user must not hide one of ours: ids are unique
    for (bool predefined: {true, false}) {
        const QByteArray data = readFile(predefined ? m_predefinedFile : m_file);
        for (ToolbarIni::Config& config: ToolbarIni::parse(data, predefined, language)) {
            if (!find(config.id)) {
                m_configs.append(std::move(config));
            }
        }
    }
    emit configsChanged();
    emit currentChanged();
    emit barsChanged();
}

QString ToolbarModel::uniqueId(const QString& wanted) const {
    QString id = wanted;
    for (int i = 2; find(id); ++i) {
        id = u"%1 %2"_s.arg(wanted).arg(i);
    }
    return id;
}

void ToolbarModel::save() const {
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile file(m_file);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(ToolbarIni::write(m_configs));
        file.commit();
    }
}

QString ToolbarModel::copyShown() {
    ToolbarIni::Config copy = *find(shown());
    copy.predefined = false;
    //: The name of a copy of a toolbar configuration; %1 is the name of the original
    copy.name = uniqueId(tr("%1 Copy").arg(copy.name));
    copy.id = uniqueId(copy.id + u" Copy"_s);
    const QString id = copy.id;
    m_configs.append(std::move(copy));
    save();
    emit configsChanged();
    setCurrent(id);
    return id;
}

void ToolbarModel::setItems(const QString& bar, const QStringList& items) {
    if (!ToolbarIni::barNames().contains(bar)) {
        return;
    }
    if (find(shown())->bars.value(bar) == items) {
        return;
    }
    if (shownPredefined()) {
        copyShown();
    }
    ToolbarIni::Config* config = shownConfig();
    if (items.isEmpty()) {
        config->bars.remove(bar);
    } else {
        config->bars[bar] = items;
    }
    save();
    emit barsChanged();
}

void ToolbarModel::removeShown() {
    const QString id = shown();
    if (find(id)->predefined) {
        return;
    }
    m_configs.removeIf([&id](const ToolbarIni::Config& config) { return config.id == id; });
    save();
    m_current.clear();
    emit configsChanged();
    emit currentChanged();
    emit barsChanged();
}

void ToolbarModel::renameShown(const QString& name) {
    ToolbarIni::Config* config = shownConfig();
    if (config->predefined || name.trimmed().isEmpty() || name == config->name) {
        return;
    }
    config->name = name.trimmed();
    save();
    emit configsChanged();
    emit barsChanged();
}

QString ToolbarModel::add(const QString& name, const QVariantMap& bars) {
    ToolbarIni::Config config;
    config.name = name;
    config.id = uniqueId(name);
    for (const QString& bar: ToolbarIni::barNames()) {
        const QStringList items = bars.value(bar).toStringList();
        if (!items.isEmpty()) {
            config.bars[bar] = items;
        }
    }
    const QString id = config.id;
    m_configs.append(std::move(config));
    save();
    emit configsChanged();
    setCurrent(id);
    return id;
}

int ToolbarModel::importFile(const QUrl& url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    int added = 0;
    for (ToolbarIni::Config& config: ToolbarIni::parse(readFile(path), false, QLocale().name())) {
        // Importing the same file again must not add everything again
        const bool known = std::any_of(m_configs.cbegin(), m_configs.cend(), [&config](const ToolbarIni::Config& c) {
            return c.name == config.name && c.bars == config.bars;
        });
        if (known || config.bars.isEmpty()) {
            continue;
        }
        config.id = uniqueId(config.id);
        m_configs.append(std::move(config));
        ++added;
    }
    if (added > 0) {
        save();
        emit configsChanged();
    }
    return added;
}

QUrl ToolbarModel::xournalppFile() const {
    const QString path = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, u"xournalpp/toolbar.ini"_s);
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_controller.h"
#include "effect_names.h"
#include <KApplicationTrader>
#include <KConfigGroup>
#include <KService>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QIcon>
#include <QPointer>
#include <QUuid>
#include <algorithm>

namespace Ambient
{
SettingsController::SettingsController(QObject* parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig("kwinrc"))
    , m_effect(new EffectControl(this))
{
    connect(m_effect, &EffectControl::changed, this, &SettingsController::changed);
    connect(&m_layerKeys, &LayerKeyTracker::stateChanged, this, [this] {
        updatePreview();
        Q_EMIT pressedKeysChanged();
    });
    connect(this, &SettingsController::contentChanged, this, &SettingsController::pressedKeysChanged);
    connect(this, &SettingsController::contentChanged, this, [this] { updatePreview(true); });
    m_fallback = defaultCollection(m_driver.description().id);
    load();
}
SettingsController::~SettingsController()
{
    // Apply may outlive the settings window, as it does in standard KCMs.
    if (m_effect->busy()) {
        m_effect->setParent(QCoreApplication::instance());
        connect(m_effect, &EffectControl::finished, m_effect, &QObject::deleteLater);
    }
}
QVariantList SettingsController::presets() const
{
    QVariantList result;
    for (const auto& p : m_collection.presets)
        result << QVariantMap { { "id", p.id }, { "name", p.name }, { "deviceId", p.keyboardDeviceId } };
    return result;
}
QString SettingsController::applicationIcon(const QString& windowClass) const
{
    if (!m_applicationIcons) {
        m_applicationIcons.emplace();
        for (const auto& service : KApplicationTrader::query([](const auto&) { return true; })) {
            if (service->icon().isEmpty())
                continue;
            for (const auto& key :
                { service->desktopEntryName(), service->desktopEntryName().section('.', -1),
                    service->property<QString>("StartupWMClass") })
                if (!key.isEmpty())
                    m_applicationIcons->insert(key.toCaseFolded(), service->icon());
        }
    }
    auto key = windowClass.trimmed().toCaseFolded();
    if (key.endsWith(".desktop"))
        key.chop(8);
    if (m_applicationIcons->contains(key))
        return m_applicationIcons->value(key);
    if (key.startsWith("steam_app_")) {
        const auto icon = QStringLiteral("steam_icon_") + key.mid(10);
        if (QIcon::hasThemeIcon(icon))
            return icon;
    }
    return QStringLiteral("application-x-executable");
}
QVariantList SettingsController::rules() const
{
    QVariantList result;
    for (const auto& r : m_collection.rules)
        result << QVariantMap { { "name", r.name }, { "windowClass", r.windowClass },
            { "presetId", r.presetId }, { "enabled", r.enabled },
            { "icon", applicationIcon(r.windowClass) } };
    return result;
}
QVariantMap SettingsController::preset() const
{
    const auto& p = current();
    QVariantList layers;
    for (const auto& l : p.layers)
        layers << QVariantMap { { "name", l.name }, { "conditions", l.conditions },
            { "summary", layerLabel(l.conditions) } };
    return { { "id", p.id }, { "name", p.name }, { "deviceId", p.keyboardDeviceId }, { "layers", layers },
        { "defaultEffectId", p.defaultEffectId } };
}
QVariantList SettingsController::keys() const
{
    QVariantList result;
    for (const auto& k : m_driver.description().keys)
        result << QVariantMap { { "id", k.id }, { "label", k.label }, { "x", k.rect.x() },
            { "y", k.rect.y() }, { "width", k.rect.width() }, { "height", k.rect.height() } };
    return result;
}
QVariantMap SettingsController::device() const
{
    const auto& d = m_driver.description();
    QRectF bounds;
    for (const auto& k : d.keys)
        bounds |= k.rect;
    for (const auto& strip : d.strips) {
        const qreal margin = strip.pathWidth / 2 + 5;
        for (const auto& p : strip.path)
            bounds |= QRectF(p.position.x() - margin, p.position.y() - margin, margin * 2, margin * 2);
    }
    return { { "id", d.id }, { "name", d.name }, { "error", m_driver.error() },
        { "bodyColor", d.bodyColor.isValid() ? d.bodyColor.name() : QString() },
        { "keyColor", d.keyColor.isValid() ? d.keyColor.name() : QString() },
        { "color", d.capabilities.testFlag(DeviceCapability::Color) },
        { "perKey", d.capabilities.testFlag(DeviceCapability::PerKeyColor) },
        { "lightbar", d.capabilities.testFlag(DeviceCapability::Lightbar) || !d.strips.isEmpty() },
        { "perStrip", d.capabilities.testFlag(DeviceCapability::PerStripColor) },
        { "lightbarPower", d.capabilities.testFlag(DeviceCapability::LightbarPower) }, { "x", bounds.x() },
        { "y", bounds.y() }, { "width", bounds.width() }, { "height", bounds.height() } };
}
QVariantList SettingsController::conditions() const
{
    QVariantList result;
    for (const auto& [id, label] : availableConditions())
        result << QVariantMap { { "id", id }, { "label", label },
            { "available", id != "fn" && id != "fn-lock" } };
    return result;
}
bool SettingsController::editableKeys() const
{
    return m_driver.description().capabilities.testFlag(DeviceCapability::PerKeyColor)
        && current().keyboardDeviceId == m_driver.description().id;
}
bool SettingsController::editableLightbar() const
{
    return current().keyboardDeviceId == m_driver.description().id
        && (m_driver.description().capabilities.testFlag(DeviceCapability::Lightbar)
            || !m_driver.description().strips.isEmpty())
        && (m_driver.description().capabilities.testFlag(DeviceCapability::PerKeyColor)
            || m_driver.description().capabilities.testFlag(DeviceCapability::PerStripColor));
}
bool SettingsController::dirty() const
{
    return m_collection != m_saved || m_effect->dirty()
        || std::any_of(m_controls.begin(), m_controls.end(),
            [](const auto& entry) { return entry.second->enabled != entry.second->saved; });
}
bool SettingsController::representsDefaults() const
{
    const auto& p = current();
    if (m_section == "base")
        return (!lightbarAvailable() || lightbarEnabled())
            && !deviceSettings().value("pauseOnBattery").toBool()
            && !deviceSettings().value("pauseOnLowBattery").toBool()
            && !deviceSettings().value("lightbarOffOnBattery").toBool()
            && !deviceSettings().value("lightbarOffOnLowBattery").toBool();
    if (m_section == "effects")
        return m_selectedEffect < 0
            || (!m_collection.effects[m_selectedEffect].pauseOnBattery
                && !m_collection.effects[m_selectedEffect].pauseOnLowBattery
                && (m_collection.effects[m_selectedEffect].type == "notifications"
                        ? m_collection.effects[m_selectedEffect].notification == NotificationOptions()
                        : m_collection.effects[m_selectedEffect].type == "solid"
                        ? m_collection.effects[m_selectedEffect].solidColor == QColor(Qt::white)
                        : m_collection.effects[m_selectedEffect].ambient == AmbientOptions()));
    if (m_section == "applications")
        return m_collection.rules.isEmpty() && m_collection.defaultId == m_collection.presets.first().id;
    const auto defaultEffect = [&]() {
        for (const auto& e : m_collection.effects)
            if (e.type == "ambient")
                return e.id;
        return QString();
    }();
    return p.defaultEffectId == defaultEffect && p.base.isEmpty() && p.groups.isEmpty()
        && p.layers == KeyboardLayers { KeyboardLayer() } && p.effects.isEmpty()
        && p.effectOverrides.isEmpty() && p.gradients.isEmpty();
}
void SettingsController::pruneLocalEffects()
{
    auto references = current();
    references.localEffects.clear();
    const auto used = referencedEffects(references);
    current().localEffects.removeIf([&](const auto& effect) { return !used.contains(effect.id); });
}
void SettingsController::touch()
{
    pruneLocalEffects();
    ++m_revision;
    Q_EMIT contentChanged();
    m_error.clear();
    Q_EMIT changed();
}
void SettingsController::selectPreset(int index)
{
    if (index < 0 || index >= m_collection.presets.size() || index == m_selected)
        return;
    m_selected = index;
    if (current().keyboardDeviceId != m_driver.description().id) {
        for (const auto& d : configured())
            if (d.modelId == current().keyboardDeviceId) {
                m_selectedDevice = d.identity();
                m_driver.select(m_selectedDevice);
                loadLightbar();
                break;
            }
    }
    touch();
}
void SettingsController::setDefaultId(const QString& id)
{
    for (const auto& p : m_collection.presets)
        if (p.id == id) {
            m_collection.defaultId = id;
            touch();
            return;
        }
}
void SettingsController::load()
{
    if (busy())
        return;
    m_config->reparseConfiguration();
    const auto& fallback = m_fallback;
    const auto collection = loadPresets(fallback, &m_error);
    m_readable = collection.has_value();
    m_saved = m_collection = collection.value_or(fallback);
    m_selected = std::max(0, matchingPreset(m_collection, QString()));
    m_selectedEffect = m_collection.effects.isEmpty() ? -1 : 0;
    ++m_revision;
    m_effect->reset();
    m_controls.clear();
    const auto entries = configured();
    if (selectedDevice() < 0)
        m_selectedDevice = entries.isEmpty() ? QString() : entries.first().identity();
    m_driver.select(m_selectedDevice);
    const int deviceIndex = selectedDevice();
    if (deviceIndex >= 0) {
        const int preset = devicePreset(m_collection, entries[deviceIndex], { });
        if (preset >= 0)
            m_selected = preset;
    }
    loadLightbar();
    Q_EMIT contentChanged();
    Q_EMIT changed();
}
void SettingsController::save()
{
    if (busy())
        return;
    m_config->reparseConfiguration();
    if (!m_readable) {
        m_error = tr("Cannot save unreadable preset files. Repair them and reset.");
        Q_EMIT changed();
        return;
    }
    QString error;
    if (!savePresets(m_collection, &error, &m_saved, &m_fallback)) {
        m_error = error;
        Q_EMIT changed();
        return;
    }
    m_saved = m_collection;
    m_error.clear();
    saveLightbar();
    m_effect->apply();
    notifyEffects();
    Q_EMIT changed();
}
void SettingsController::defaults()
{
    if (m_section == "base") {
        if (lightbarAvailable())
            setLightbarEnabled(true);
        setDevice("pauseOnBattery", false);
        setDevice("pauseOnLowBattery", false);
        setDevice("lightbarOffOnBattery", false);
        setDevice("lightbarOffOnLowBattery", false);
    } else if (m_section == "effects") {
        if (m_selectedEffect >= 0) {
            auto& effect = m_collection.effects[m_selectedEffect];
            effect.pauseOnBattery = effect.pauseOnLowBattery = false;
            if (effect.type == "notifications")
                effect.notification = NotificationOptions();
            else if (effect.type == "solid")
                effect.solidColor = Qt::white;
            else
                effect.ambient = AmbientOptions();
        }
    } else if (m_section == "applications") {
        m_collection.rules.clear();
        m_collection.defaultId = m_collection.presets.first().id;
    } else {
        current().base.clear();
        current().gradients.clear();
        current().effects.clear();
        current().effectOverrides.clear();
        current().localEffects.clear();
        current().groups.clear();
        current().layers = { KeyboardLayer() };
        current().defaultEffectId = [&]() {
            for (const auto& e : m_collection.effects)
                if (e.type == "ambient")
                    return e.id;
            return QString();
        }();
        if (m_driver.driver())
            current().keyboardDeviceId = m_driver.description().id;
    }
    touch();
}
void SettingsController::setName(const QString& name)
{
    if (name.trimmed().isEmpty() || name.size() > 128)
        return;
    current().name = name;
    touch();
}
bool SettingsController::addPreset(bool duplicate)
{
    if (m_collection.presets.size() >= 64)
        return false;
    auto p = duplicate ? current() : LightingPreset();
    if (!duplicate) {
        p.defaultEffectId = [&]() {
            for (const auto& e : m_collection.effects)
                if (e.type == "ambient")
                    return e.id;
            return QString();
        }();
        if (m_driver.driver())
            p.keyboardDeviceId = m_driver.description().id;
    }
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = duplicate ? tr("%1 copy").arg(p.name) : tr("New preset");
    m_collection.presets << p;
    m_selected = m_collection.presets.size() - 1;
    touch();
    return true;
}
bool SettingsController::removePreset()
{
    if (m_collection.presets.size() <= 1) {
        m_error = tr("Keep at least one preset.");
        Q_EMIT changed();
        return false;
    }
    for (const auto& r : m_collection.rules)
        if (r.presetId == current().id) {
            m_error = tr("Remove this preset's application bindings first.");
            Q_EMIT changed();
            return false;
        }
    for (const auto& device : configured())
        if (device.presetId == current().id) {
            m_error = tr("Select another preset for device \"%1\" first.").arg(device.name);
            Q_EMIT changed();
            return false;
        }
    const bool wasDefault = current().id == m_collection.defaultId;
    m_collection.presets.removeAt(m_selected);
    m_selected = 0;
    if (wasDefault)
        m_collection.defaultId = current().id;
    touch();
    return true;
}
void SettingsController::importFile(const QUrl& path)
{
    if (!path.isLocalFile() || m_collection.presets.size() >= 64)
        return;
    auto bundle = importPreset(path.toLocalFile(), &m_error);
    if (bundle) {
        auto effects = m_collection.effects;
        QMap<QString, QString> remap;
        for (auto effect : bundle->effects) {
            const auto existing = std::find_if(
                effects.begin(), effects.end(), [&](const auto& e) { return e.id == effect.id; });
            if (existing != effects.end() && *existing == effect)
                continue;
            const auto oldId = effect.id;
            if (existing != effects.end())
                effect.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            remap.insert(oldId, effect.id);
            effects.append(effect);
        }
        if (effects.size() > 64) {
            m_error = tr("Import would exceed the limit of 64 effect presets.");
            Q_EMIT changed();
            return;
        }
        auto& keyboard = bundle->keyboard;
        keyboard.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        keyboard.defaultEffectId = remap.value(keyboard.defaultEffectId, keyboard.defaultEffectId);
        auto update = [&](KeyboardStyles& styles) {
            for (auto& style : styles)
                style.effectId = remap.value(style.effectId, style.effectId);
        };
        update(keyboard.base);
        const auto updateGradients = [&](StripGradients& gradients) {
            for (auto& gradient : gradients)
                gradient.effectId = remap.value(gradient.effectId, gradient.effectId);
        };
        updateGradients(keyboard.gradients);
        auto remapAssignments = [&](EffectAssignments& assignments) {
            for (auto& list : assignments)
                for (auto& effect : list)
                    effect.id = remap.value(effect.id, effect.id);
        };
        remapAssignments(keyboard.effects);
        remapAssignments(keyboard.effectOverrides);
        for (auto& layer : keyboard.layers) {
            update(layer.styles);
            updateGradients(layer.gradients);
            remapAssignments(layer.effects);
        }
        m_collection.effects = effects;
        m_collection.presets << keyboard;
        m_selected = m_collection.presets.size() - 1;
        if (m_selectedEffect < 0 && !effects.isEmpty())
            m_selectedEffect = 0;
        touch();
    }
    Q_EMIT changed();
}
void SettingsController::exportFile(const QUrl& path)
{
    if (!path.isLocalFile())
        return;
    exportPreset(current(), m_collection.effects, path.toLocalFile(), &m_error);
    Q_EMIT changed();
}
QString SettingsController::layerLabel(const QStringList& conditions) const
{
    QStringList labels;
    for (const auto& id : conditions) {
        const auto family = id.section('-', 0, 0);
        QString label;
        if (family == "shift")
            label = QStringLiteral("Shift");
        else if (family == "ctrl")
            label = QStringLiteral("Ctrl");
        else if (family == "alt")
            label = QStringLiteral("Alt");
        else if (family == "super")
            label = QStringLiteral("Meta");
        if (label.isEmpty())
            label = conditionName(id);
        else if (id.endsWith("-left"))
            label = tr("%1 (Left)").arg(label);
        else if (id.endsWith("-right"))
            label = tr("%1 (Right)").arg(label);
        labels << label;
    }
    return labels.join(" + ");
}
bool SettingsController::validateLayerConditions(const QStringList& conditions, int except)
{
    if (conditions.isEmpty() || conditions.size() > 8)
        return false;
    const auto known = availableConditions();
    QSet<QString> unique;
    for (const auto& id : conditions) {
        if (unique.contains(id)
            || !std::any_of(known.begin(), known.end(), [&](const auto& p) { return p.first == id; }))
            return false;
        unique.insert(id);
    }
    for (int i = 0; i < current().layers.size(); ++i) {
        const auto& existing = current().layers[i].conditions;
        if (i != except && unique == QSet<QString>(existing.cbegin(), existing.cend())) {
            m_error = tr("A layer with this combination already exists.");
            Q_EMIT changed();
            return false;
        }
    }
    return true;
}
int SettingsController::addLayer(const QStringList& conditions)
{
    if ((!editableKeys() && !editableLightbar()) || current().layers.size() >= 32
        || !validateLayerConditions(conditions))
        return -1;
    // Keep the serialized name for the file format; the UI derives its localized label.
    current().layers << KeyboardLayer { conditions.join('+'), conditions, { } };
    touch();
    return current().layers.size() - 1;
}
bool SettingsController::setLayer(int index, const QStringList& conditions)
{
    if ((!editableKeys() && !editableLightbar()) || index < 0 || index >= current().layers.size()
        || !validateLayerConditions(conditions, index))
        return false;
    auto& l = current().layers[index];
    l.name = conditions.join('+');
    l.conditions = conditions;
    touch();
    return true;
}
void SettingsController::removeLayer(int index)
{
    if ((!editableKeys() && !editableLightbar()) || index < 0 || index >= current().layers.size())
        return;
    current().layers.removeAt(index);
    touch();
}
int SettingsController::moveLayer(int index, int direction)
{
    const int target = index + direction;
    if ((!editableKeys() && !editableLightbar()) || std::abs(direction) != 1 || index < 0
        || index >= current().layers.size() || target < 0 || target >= current().layers.size())
        return index;
    current().layers.swapItemsAt(index, target);
    touch();
    return target;
}
QVariantList SettingsController::pressedKeys() const
{
    QVariantList result;
    const auto codes = m_driver.instance().options.value("InputKeyCodes").toMap();
    const auto& pressed = m_layerKeys.state().pressed;
    for (const auto& key : m_driver.description().keys) {
        const auto code = codes.value(QString::number(key.id)).toUInt();
        if (code && pressed.contains(code))
            result.append(key.id);
    }
    return result;
}
void SettingsController::watchLayerKeys(QWindow* window) { m_layerKeys.watch(window); }
void SettingsController::updatePreview(bool force)
{
    const int layer = activeLayer(current().layers, m_layerKeys.state());
    if (!force && layer == m_previewLayer)
        return;
    m_previewLayer = layer;
    Q_EMIT previewChanged();
}
QVariantList SettingsController::liveKeyPreview() const { return keyPreview(m_previewLayer); }
QVariantList SettingsController::keyPreview(int layer) const
{
    QVariantList result;
    const bool overlay = layer >= 0 && layer < current().layers.size();
    const auto& d = m_driver.description();
    const auto base = expandStyles(current().base, current().gradients, d);
    const auto styles
        = overlay ? expandStyles(current().layers[layer].styles, current().layers[layer].gradients, d) : base;
    auto append = [&](int id, bool editable) {
        auto style = editable ? keyStyle(styles, id, overlay) : KeyStyle();
        const bool colorInherited = style.mode == KeyMode::Inherit;
        const bool inherited = colorInherited
            && (!overlay
                || !current().layers[layer].effects.contains(
                    effectSource(current().layers[layer].effects, id)));
        if (colorInherited)
            style = keyStyle(base, id);
        const auto list = selectionEffects(layer, { id })["items"].toList();
        QString notificationId;
        bool above = false, ambient = false;
        for (const auto& value : list) {
            const auto entry = value.toMap();
            if (entry["type"] == "notifications") {
                notificationId = entry["id"].toString();
                above |= entry["above"].toBool();
            } else if (entry["type"] == "ambient")
                ambient = true;
            else if (entry["type"] == "solid") {
                auto color = entry["color"].toString();
                for (const auto& effect : current().localEffects)
                    if (effect.id == entry["id"].toString())
                        color = effect.solidColor.name();
                for (const auto& effect : m_collection.effects)
                    if (effect.id == entry["id"].toString())
                        color = effect.solidColor.name();
                style.color = QColor(color);
                style.color.setAlpha((entry["opacity"].toInt() * 255 + 50) / 100);
            }
        }
        if (style.mode != KeyMode::Off || !list.isEmpty())
            style.mode = ambient ? KeyMode::Ambient : KeyMode::Static;
        result << QVariantMap { { "id", id }, { "notificationId", notificationId },
            { "notificationAbove", above }, { "mode", int(style.mode) },
            { "assignmentMode", inherited ? int(KeyMode::Inherit) : int(style.mode) },
            { "color", style.color.name() }, { "opacity", qRound(style.color.alphaF() * 100) },
            { "effectId", style.effectId }, { "inherited", inherited } };
    };
    for (const auto& k : m_driver.description().keys)
        append(k.id, editableKeys());
    for (const auto& strip : d.strips) {
        for (const auto& element : strip.elements)
            append(element.id, editableLightbar());
        append(strip.id, editableLightbar());
    }
    if (d.strips.isEmpty() && d.capabilities.testFlag(DeviceCapability::Lightbar))
        append(LightbarId, editableLightbar());
    return result;
}
QList<int> SettingsController::validIds(const QVariantList& ids) const
{
    QSet<int> known;
    if (editableKeys())
        for (const auto& k : m_driver.description().keys)
            known.insert(k.id);
    if (editableLightbar()) {
        if (m_driver.description().capabilities.testFlag(DeviceCapability::Lightbar))
            known.insert(LightbarId);
        for (const auto& strip : m_driver.description().strips) {
            known.insert(strip.id);
            if (m_driver.description().capabilities.testFlag(DeviceCapability::PerStripColor))
                for (const auto& element : strip.elements)
                    known.insert(element.id);
        }
    }
    QList<int> result;
    for (const auto& value : ids) {
        bool ok = false;
        int id = value.toInt(&ok);
        if (ok && known.contains(id) && !result.contains(id))
            result << id;
    }
    std::sort(result.begin(), result.end());
    return result;
}
QVariantList SettingsController::groups() const
{
    QVariantList result;
    auto add = [&](const QString& name, const QList<int>& ids, bool custom) {
        QVariantList values;
        for (int id : ids)
            values << id;
        result << QVariantMap { { "name", name }, { "ids", values }, { "custom", custom } };
    };
    QList<int> all;
    for (const auto& k : m_driver.description().keys)
        all << k.id;
    add(tr("All keys"), all, false);
    if (editableLightbar()) {
        for (const auto& strip : m_driver.description().strips) {
            add(strip.name, { strip.id }, false);
            QList<int> ids;
            for (const auto& element : strip.elements)
                ids.append(element.id);
            add(tr("%1 elements").arg(strip.name), ids, false);
        }
        if (m_driver.description().strips.isEmpty())
            add(tr("Lightbar"), { LightbarId }, false);
    }
    for (auto i = m_driver.description().groups.cbegin(); i != m_driver.description().groups.cend(); ++i)
        add(i.key(), i.value(), false);
    for (auto i = current().groups.cbegin(); i != current().groups.cend(); ++i)
        add(i.key(), i.value(), true);
    return result;
}
void SettingsController::assignKeys(
    int layer, const QVariantList& ids, int mode, const QString& color, const QString& effectId, int opacity)
{
    if ((!editableKeys() && !editableLightbar()) || layer < -1 || layer >= current().layers.size() || mode < 0
        || mode > 3 || (mode == int(KeyMode::Inherit) && layer < 0))
        return;
    QColor c(color);
    if ((mode == int(KeyMode::Static) || mode == int(KeyMode::Ambient)) && !c.isValid())
        return;
    if (mode == int(KeyMode::Ambient) && !effectId.isEmpty()
        && std::none_of(m_collection.effects.begin(), m_collection.effects.end(),
            [&](const auto& e) { return e.id == effectId && e.type == "ambient"; }))
        return;
    const int percent = std::clamp(opacity < 0 ? (mode == int(KeyMode::Static) ? 100 : 0) : opacity, 0, 100);
    c.setAlpha((percent * 255 + 50) / 100);
    auto& styles = layer < 0 ? current().base : current().layers[layer].styles;
    for (int id : validIds(ids)) {
        for (const auto& strip : m_driver.description().strips)
            if (strip.id == id) {
                for (const auto& element : strip.elements)
                    styles.remove(element.id);
                auto& gradients = layer < 0 ? current().gradients : current().layers[layer].gradients;
                gradients.removeIf([&](const auto& g) { return g.strip == id; });
            }
        const bool stripElement = std::any_of(m_driver.description().strips.begin(),
            m_driver.description().strips.end(), [&](const auto& strip) {
                return std::any_of(
                    strip.elements.begin(), strip.elements.end(), [&](const auto& e) { return e.id == id; });
            });
        if (mode == int(KeyMode::Inherit)
            || (!stripElement && layer < 0 && mode == int(KeyMode::Ambient) && effectId.isEmpty()
                && c.alpha() == 0))
            styles.remove(id);
        else
            styles[id] = { KeyMode(mode), mode == int(KeyMode::Off) ? QColor(Qt::transparent) : c,
                mode == int(KeyMode::Ambient) ? effectId : QString() };
    }
    touch();
}
void SettingsController::saveGroup(const QString& name, const QVariantList& ids)
{
    const auto label = name.trimmed();
    const auto keys = validIds(ids);
    if ((!editableKeys() && !editableLightbar()) || label.isEmpty() || label.size() > 64 || keys.isEmpty()
        || (current().groups.size() >= 32 && !current().groups.contains(label)))
        return;
    current().groups[label] = keys;
    touch();
}
void SettingsController::removeGroup(const QString& name)
{
    if (editableKeys() || editableLightbar()) {
        current().groups.remove(name);
        touch();
    }
}
int SettingsController::addRule()
{
    if (m_collection.rules.size() >= 128)
        return -1;
    m_collection.rules << ApplicationRule { QString(), QString(), m_collection.defaultId, true };
    touch();
    return m_collection.rules.size() - 1;
}
void SettingsController::setRule(int index, const QString& key, const QVariant& value)
{
    if (index < 0 || index >= m_collection.rules.size())
        return;
    auto& r = m_collection.rules[index];
    if (key == "name")
        r.name = value.toString().left(128);
    else if (key == "windowClass")
        r.windowClass = value.toString().trimmed().left(512);
    else if (key == "enabled")
        r.enabled = value.toBool();
    else if (key == "presetId") {
        bool found = false;
        for (const auto& p : m_collection.presets)
            found |= p.id == value.toString();
        if (!found)
            return;
        r.presetId = value.toString();
    } else
        return;
    touch();
}
void SettingsController::removeRule(int index)
{
    if (index >= 0 && index < m_collection.rules.size()) {
        m_collection.rules.removeAt(index);
        touch();
    }
}
int SettingsController::moveRule(int index, int direction)
{
    const int target = index + direction;
    if (std::abs(direction) != 1 || index < 0 || index >= m_collection.rules.size() || target < 0
        || target >= m_collection.rules.size())
        return index;
    m_collection.rules.swapItemsAt(index, target);
    touch();
    return target;
}
void SettingsController::selectWindow(int index)
{
    if (m_pickingWindow || index < 0 || index >= m_collection.rules.size())
        return;
    const int revision = m_revision;
    m_pickingWindow = true;
    m_windowStatus = tr("Click an application window. Press Esc to cancel.");
    Q_EMIT changed();
    auto call = QDBusMessage::createMethodCall("org.kde.KWin", "/KWin", "org.kde.KWin", "queryWindowInfo");
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, index, revision](auto* watcher) {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        m_pickingWindow = false;
        m_windowStatus.clear();
        if (reply.isError()) {
            if (reply.error().name() != "org.kde.KWin.Error.UserCancel")
                m_windowStatus = tr("Could not select a window: %1").arg(reply.error().message());
        } else if (revision == m_revision) {
            const auto info = reply.value();
            const auto windowClass = info.value("resourceClass").toString().trimmed();
            if (windowClass.isEmpty())
                m_windowStatus = tr("This window has no application class.");
            else {
                auto& r = m_collection.rules[index];
                r.windowClass = windowClass;
                if (r.name.trimmed().isEmpty())
                    r.name = info.value("resourceName").toString().trimmed();
                if (r.name.isEmpty())
                    r.name = windowClass;
                ++m_revision;
                Q_EMIT contentChanged();
            }
        }
        Q_EMIT changed();
    });
}
void SettingsController::notifyEffects()
{
    auto call = QDBusMessage::createMethodCall(
        "org.kde.KWin", "/Effects", "org.freedesktop.DBus.Properties", "Get");
    call << QStringLiteral("org.kde.kwin.Effects") << QStringLiteral("loadedEffects");
    auto* watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call, 1500), QCoreApplication::instance());
    const QPointer<SettingsController> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [self](auto* watcher) {
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            if (self) {
                self->m_error = tr("Settings saved, but KWin could not be reached.");
                Q_EMIT self->changed();
            }
            return;
        }
        for (const auto& name : qdbus_cast<QStringList>(reply.value().variant())) {
            if (!isAmbientEffectName(name))
                continue;
            auto call = QDBusMessage::createMethodCall(
                "org.kde.KWin", "/Effects", "org.kde.kwin.Effects", "reconfigureEffect");
            call << name;
            auto* update = new QDBusPendingCallWatcher(
                QDBusConnection::sessionBus().asyncCall(call, 1500), QCoreApplication::instance());
            connect(update, &QDBusPendingCallWatcher::finished, update, [self](auto* update) {
                const QDBusPendingReply<> reply = *update;
                update->deleteLater();
                if (reply.isError() && self) {
                    self->m_error = tr("Settings saved, but could not be applied to the running effect.");
                    Q_EMIT self->changed();
                }
            });
        }
    });
}
}

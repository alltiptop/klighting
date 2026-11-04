// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_controller.h"
#include "color.h"
#include <KApplicationTrader>
#include <KService>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QIcon>
#include <QUuid>
#include <algorithm>

namespace Ambient
{
SettingsController::SettingsController(QObject* parent)
    : QObject(parent)
    , m_document(std::make_shared<Document>())
    , m_driver(m_document->driver)
    , m_effect(new EffectControl(this))
    , m_service(new ServiceControl(this))
    , m_collection(m_document->collection)
    , m_saved(m_document->saved)
    , m_fallback(m_document->fallback)
    , m_revision(m_document->revision)
    , m_error(m_document->error)
{
    connect(this, &SettingsController::contentChanged, this, &SettingsController::syncProfileEditors);
    connect(m_effect, &EffectControl::changed, this, &SettingsController::changed);
    connect(m_service, &ServiceControl::changed, this, &SettingsController::changed);
    connect(m_service, &ServiceControl::finished, this, [this] {
        if (!m_service->error().isEmpty()) {
            m_error = m_service->error();
            Q_EMIT documentController()->changed();
        }
    });
    connect(&m_layerKeys, &LayerKeyTracker::stateChanged, this, [this] {
        updatePreview();
        Q_EMIT pressedKeysChanged();
    });
    connect(this, &SettingsController::contentChanged, this, &SettingsController::pressedKeysChanged);
    connect(this, &SettingsController::contentChanged, this, [this] { updatePreview(true); });
    m_fallback = defaultCollection(editingDescription().id);
    load();
    m_discoveryTimer.setSingleShot(true);
    connect(&m_discoveryTimer, &QTimer::timeout, this, &SettingsController::refreshDevices);
    if (m_driver.discoveryPending())
        m_discoveryTimer.start(50);
}
SettingsController::~SettingsController()
{
    // Apply may outlive the settings window, as it does in standard KCMs.
    if (!m_owner && m_effect->busy()) {
        m_effect->setParent(QCoreApplication::instance());
        connect(m_effect, &EffectControl::finished, m_effect, &QObject::deleteLater);
    }
}
QVariantList SettingsController::presets() const
{
    QVariantList result;
    for (const auto& p : m_collection.presets) {
        QStringList targets;
        for (const auto& d : configured())
            if (profileFor(p, d))
                targets.append(d.identity());
        QVariantList layers;
        for (const auto& conditions : presetLayerConditions(p))
            layers.append(layerLabel(conditions));
        result << QVariantMap { { "id", p.id }, { "name", p.name }, { "deviceId", p.keyboardDeviceId },
            { "devices", targets }, { "layers", layers } };
    }
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
    const auto& scene = currentPreset();
    QVariantList layers;
    for (const auto& conditions : presetLayerConditions(scene))
        layers << QVariantMap { { "name", conditions.join('+') }, { "conditions", conditions },
            { "summary", layerLabel(conditions) } };
    return { { "id", m_owner ? p.id : scene.id }, { "name", scene.name }, { "deviceId", p.keyboardDeviceId },
        { "layers", layers }, { "defaultEffectId", p.defaultEffectId } };
}
QVariantList SettingsController::keys() const
{
    QVariantList result;
    for (const auto& k : editingDescription().keys)
        result << QVariantMap { { "id", k.id }, { "label", k.label }, { "x", k.rect.x() },
            { "y", k.rect.y() }, { "width", k.rect.width() }, { "height", k.rect.height() } };
    return result;
}
QVariantMap SettingsController::device() const
{
    const auto& d = editingDescription();
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
        { "brightness", d.capabilities.testFlag(DeviceCapability::Brightness) },
        { "brightnessSteps", deviceBrightnessSteps(editingInstance()) },
        { "ambient",
            d.capabilities.testFlag(DeviceCapability::Color)
                || (d.capabilities.testFlag(DeviceCapability::Brightness)
                    && editingInstance().options.value("BrightnessSteps").toInt() >= 4) },
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
    return editingDescription().capabilities.testFlag(DeviceCapability::PerKeyColor)
        && current().keyboardDeviceId == editingDescription().id;
}
bool SettingsController::editableLightbar() const
{
    return current().keyboardDeviceId == editingDescription().id
        && (editingDescription().capabilities.testFlag(DeviceCapability::Lightbar)
            || !editingDescription().strips.isEmpty())
        && (editingDescription().capabilities.testFlag(DeviceCapability::PerKeyColor)
            || editingDescription().capabilities.testFlag(DeviceCapability::PerStripColor));
}
bool SettingsController::dirty() const
{
    return m_collection != m_saved || m_effect->dirty()
        || std::any_of(m_controls.begin(), m_controls.end(),
            [](const auto& entry) { return entry.second->enabled != entry.second->saved; });
}
bool SettingsController::representsDefaults() const
{
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
        return m_collection.rules.isEmpty();
    const auto defaultEffect = [&]() {
        for (const auto& e : m_collection.effects)
            if (e.type == "ambient")
                return e.id;
        return QString();
    }();
    if (currentPreset().multiDevice && !currentPreset().layerConditions.isEmpty())
        return false;
    const auto profiles
        = m_owner ? QList<const DeviceProfile*> { &current() } : deviceProfiles(currentPreset());
    return std::all_of(profiles.begin(), profiles.end(), [&](const auto* profile) {
        const auto& p = *profile;
        if (currentPreset().multiDevice)
            return p.defaultEffectId.isEmpty() && p.base.isEmpty() && p.groups.isEmpty() && p.layers.isEmpty()
                && p.effects == EffectAssignments { { DefaultAssignment, { } } }
            && p.effectOverrides.isEmpty() && p.gradients.isEmpty();
        return p.defaultEffectId == defaultEffect && p.base.isEmpty() && p.groups.isEmpty()
            && p.layers == KeyboardLayers { KeyboardLayer() } && p.effects.isEmpty()
            && p.effectOverrides.isEmpty() && p.gradients.isEmpty();
    });
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
    if (selectedLayer() >= presetLayerConditions(currentPreset()).size())
        selectLayer(-1);
    ++m_revision;
    Q_EMIT documentController()->contentChanged();
    m_error.clear();
    Q_EMIT documentController()->changed();
}
int SettingsController::selectedLayer() const { return documentController()->m_selectedLayer; }
void SettingsController::selectLayer(int index)
{
    auto* owner = documentController();
    if (index < -1 || index >= presetLayerConditions(currentPreset()).size()
        || index == owner->m_selectedLayer)
        return;
    owner->m_selectedLayer = index;
    Q_EMIT owner->layerSelectionChanged();
}
void SettingsController::selectPreset(int index)
{
    if (index < 0 || index >= m_collection.presets.size() || index == m_selected)
        return;
    m_selected = index;
    selectLayer(-1);
    if (current().keyboardDeviceId != editingDescription().id) {
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
void SettingsController::load()
{
    if (busy())
        return;
    const auto& fallback = m_fallback;
    const auto collection = loadPresets(fallback, &m_error);
    m_readable = collection.has_value();
    m_saved = m_collection = collection.value_or(fallback);
    m_selected = std::max(0, matchingPreset(m_collection, QString()));
    selectLayer(-1);
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
    Q_EMIT documentController()->contentChanged();
    Q_EMIT documentController()->changed();
}
void SettingsController::save()
{
    if (busy())
        return;
    if (!m_readable) {
        m_error = tr("Cannot save unreadable preset files. Repair them and reset.");
        Q_EMIT documentController()->changed();
        return;
    }
    QString error;
    if (!savePresets(m_collection, &error, &m_saved, &m_fallback)) {
        m_error = error;
        Q_EMIT documentController()->changed();
        return;
    }
    m_saved = m_collection;
    m_error.clear();
    saveLightbar();
    m_effect->apply();
    m_service->reload();
    Q_EMIT documentController()->changed();
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
        setDevice("brightnessChannel", "average");
    } else if (m_section == "effects") {
        if (m_selectedEffect >= 0) {
            auto& effect = m_collection.effects[m_selectedEffect];
            effect.pauseOnBattery = effect.pauseOnLowBattery = false;
            if (effect.type == "notifications")
                effect.notification = NotificationOptions();
            else if (effect.type == "solid")
                effect.solidColor = Qt::white;
            else if (effect.type == "brightness")
                effect.brightness = 100;
            else
                effect.ambient = AmbientOptions();
        }
    } else if (m_section == "applications") {
        m_collection.rules.clear();
    } else {
        if (!m_owner)
            currentPreset().layerConditions.clear();
        const auto profiles
            = m_owner ? QList<DeviceProfile*> { &current() } : deviceProfiles(currentPreset());
        for (auto* profile : profiles) {
            const auto id = profile->id, name = profile->name, target = profile->targetDevice;
            const auto model = profile->keyboardDeviceId;
            *profile = DeviceProfile();
            profile->id = id;
            profile->name = name;
            profile->targetDevice = target;
            profile->keyboardDeviceId = model;
            profile->defaultEffectId.clear();
            if (currentPreset().multiDevice) {
                profile->layers.clear();
                profile->effects.insert(DefaultAssignment, { });
            } else {
                for (const auto& effect : m_collection.effects)
                    if (effect.type == "ambient") {
                        profile->defaultEffectId = effect.id;
                        break;
                    }
            }
        }
        mergePresetLayers(currentPreset());
    }
    touch();
}
void SettingsController::setName(const QString& name)
{
    if (name.trimmed().isEmpty() || name.size() > 128)
        return;
    currentPreset().name = name;
    touch();
}
bool SettingsController::addPreset(bool duplicate)
{
    if (m_collection.presets.size() >= 64)
        return false;
    auto p = duplicate ? currentPreset() : LightingPreset();
    if (!duplicate) {
        p.multiDevice = true;
        for (const auto& device : configured())
            if (device.enabled)
                p.devices.append(newProfile(device));
    } else if (p.multiDevice) {
        for (auto& profile : p.devices)
            profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = duplicate ? tr("%1 copy").arg(p.name) : tr("New preset");
    m_collection.presets << p;
    m_selected = m_collection.presets.size() - 1;
    selectLayer(-1);
    touch();
    return true;
}
bool SettingsController::removePreset()
{
    if (currentPreset().id == m_collection.defaultId) {
        m_error = tr("The default preset cannot be removed.");
        Q_EMIT documentController()->changed();
        return false;
    }
    for (const auto& r : m_collection.rules)
        if (r.presetId == currentPreset().id) {
            m_error = tr("Remove this preset's application bindings first.");
            Q_EMIT documentController()->changed();
            return false;
        }
    selectLayer(-1);
    m_collection.presets.removeAt(m_selected);
    m_selected = matchingPreset(m_collection, { });
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
            Q_EMIT documentController()->changed();
            return;
        }
        auto& keyboard = bundle->keyboard;
        keyboard.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (auto* profile : deviceProfiles(keyboard)) {
            profile->id
                = keyboard.multiDevice ? QUuid::createUuid().toString(QUuid::WithoutBraces) : keyboard.id;
            auto& keyboard = *profile;
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
        }
        m_collection.effects = effects;
        m_collection.presets << keyboard;
        m_selected = m_collection.presets.size() - 1;
        selectLayer(-1);
        if (m_selectedEffect < 0 && !effects.isEmpty())
            m_selectedEffect = 0;
        touch();
    }
    Q_EMIT documentController()->changed();
}
void SettingsController::exportFile(const QUrl& path)
{
    if (!path.isLocalFile())
        return;
    exportPreset(currentPreset(), m_collection.effects, path.toLocalFile(), &m_error);
    Q_EMIT documentController()->changed();
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
    const auto layers = presetLayerConditions(currentPreset());
    for (int i = 0; i < layers.size(); ++i) {
        const auto& existing = layers[i];
        if (i != except && unique == QSet<QString>(existing.cbegin(), existing.cend())) {
            m_error = tr("A layer with this combination already exists.");
            Q_EMIT documentController()->changed();
            return false;
        }
    }
    return true;
}
int SettingsController::addLayer(const QStringList& conditions)
{
    auto& scene = currentPreset();
    if (presetLayerConditions(scene).size() >= 32 || !validateLayerConditions(conditions))
        return -1;
    if (scene.multiDevice)
        scene.layerConditions.append(conditions);
    for (auto* profile : deviceProfiles(scene))
        profile->layers.append({ conditions.join('+'), conditions, { } });
    touch();
    return presetLayerConditions(scene).size() - 1;
}
bool SettingsController::setLayer(int index, const QStringList& conditions)
{
    auto& scene = currentPreset();
    if (index < 0 || index >= presetLayerConditions(scene).size()
        || !validateLayerConditions(conditions, index))
        return false;
    if (scene.multiDevice)
        scene.layerConditions[index] = conditions;
    for (auto* profile : deviceProfiles(scene)) {
        auto& layer = profile->layers[index];
        layer.name = conditions.join('+');
        layer.conditions = conditions;
    }
    touch();
    return true;
}
void SettingsController::removeLayer(int index)
{
    auto& scene = currentPreset();
    if (index < 0 || index >= presetLayerConditions(scene).size())
        return;
    const int selected = selectedLayer() >= index ? selectedLayer() - 1 : selectedLayer();
    if (scene.multiDevice)
        scene.layerConditions.removeAt(index);
    for (auto* profile : deviceProfiles(scene))
        profile->layers.removeAt(index);
    selectLayer(selected);
    touch();
}
int SettingsController::moveLayer(int index, int direction)
{
    auto& scene = currentPreset();
    const int target = index + direction, count = presetLayerConditions(scene).size();
    if (std::abs(direction) != 1 || index < 0 || index >= count || target < 0 || target >= count)
        return index;
    if (scene.multiDevice)
        scene.layerConditions.swapItemsAt(index, target);
    for (auto* profile : deviceProfiles(scene))
        profile->layers.swapItemsAt(index, target);
    if (selectedLayer() == index)
        selectLayer(target);
    else if (selectedLayer() == target)
        selectLayer(index);
    touch();
    return target;
}
QVariantList SettingsController::pressedKeys() const
{
    QVariantList result;
    const auto codes = editingInstance().options.value("InputKeyCodes").toMap();
    const auto& pressed = documentController()->m_layerKeys.state().pressed;
    for (const auto& key : editingDescription().keys) {
        const auto code = codes.value(QString::number(key.id)).toUInt();
        if (code && pressed.contains(code))
            result.append(key.id);
    }
    return result;
}
void SettingsController::watchLayerKeys(QWindow* window) { documentController()->m_layerKeys.watch(window); }
void SettingsController::updatePreview(bool force)
{
    const int layer = activePresetLayer(currentPreset(), documentController()->m_layerKeys.state());
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
    const auto& d = editingDescription();
    const auto base = expandStyles(current().base, current().gradients, d);
    const auto styles
        = overlay ? expandStyles(current().layers[layer].styles, current().layers[layer].gradients, d) : base;
    auto append = [&](int id, bool editable) {
        auto style = keyStyle(styles, editable ? id : DefaultAssignment, overlay);
        const bool colorInherited = style.mode == KeyMode::Inherit;
        const bool inherited = colorInherited
            && (!overlay
                || !current().layers[layer].effects.contains(
                    effectSource(current().layers[layer].effects, id)));
        if (colorInherited)
            style = keyStyle(base, editable ? id : DefaultAssignment);
        const auto list
            = selectionEffects(layer, editable ? QVariantList { id } : QVariantList())["items"].toList();
        QString notificationId;
        bool above = false, ambient = false;
        double brightness = 1.0;
        bool hasColor = style.mode == KeyMode::Off || style.color.alpha() > 0;
        for (const auto& value : list) {
            const auto entry = value.toMap();
            if (entry["opacity"].toInt() <= 0)
                continue;
            if (entry["type"] == "notifications") {
                notificationId = entry["id"].toString();
                above |= entry["above"].toBool();
            } else if (entry["type"] == "brightness") {
                if (!hasColor)
                    style.color = Qt::white;
                hasColor = true;
                const auto options = assignedEffectOptions(entry["id"].toString());
                brightness *= 1.0
                    - (100 - options.value("brightness", 100).toInt()) * entry["opacity"].toInt() / 10000.0;
            } else if (entry["type"] == "ambient") {
                ambient = true;
                hasColor = true;
                brightness = 1.0;
            } else if (entry["type"] == "solid") {
                hasColor = true;
                brightness = 1.0;
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
        if (style.mode == KeyMode::Static
            && (!d.capabilities.testFlag(DeviceCapability::Color)
                || !editingInstance().options.value("ColorLevels").toMap().isEmpty())) {
            const auto color = blendColor(Qt::black, style.color);
            style.color = deviceColor(QColor::fromRgbF(color.redF() * brightness, color.greenF() * brightness,
                                          color.blueF() * brightness),
                editingInstance(), deviceSettings().value("brightnessChannel").toString());
            brightness = 1.0;
        }
        result << QVariantMap { { "id", id }, { "notificationId", notificationId },
            { "notificationAbove", above }, { "mode", int(style.mode) },
            { "assignmentMode", inherited ? int(KeyMode::Inherit) : int(style.mode) },
            { "color", style.color.name() }, { "opacity", qRound(style.color.alphaF() * 100) },
            { "brightness", brightness }, { "effectId", style.effectId }, { "inherited", inherited } };
    };
    for (const auto& k : editingDescription().keys)
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
        for (const auto& k : editingDescription().keys)
            known.insert(k.id);
    if (editableLightbar()) {
        if (editingDescription().capabilities.testFlag(DeviceCapability::Lightbar))
            known.insert(LightbarId);
        for (const auto& strip : editingDescription().strips) {
            known.insert(strip.id);
            if (editingDescription().capabilities.testFlag(DeviceCapability::PerStripColor))
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
    for (const auto& k : editingDescription().keys)
        all << k.id;
    add(tr("All keys"), all, false);
    if (editableLightbar()) {
        for (const auto& strip : editingDescription().strips)
            add(strip.name, { strip.id }, false);
        if (editingDescription().strips.isEmpty())
            add(tr("Lightbar"), { LightbarId }, false);
    }
    for (auto i = editingDescription().groups.cbegin(); i != editingDescription().groups.cend(); ++i)
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
        for (const auto& strip : editingDescription().strips)
            if (strip.id == id) {
                for (const auto& element : strip.elements)
                    styles.remove(element.id);
                auto& gradients = layer < 0 ? current().gradients : current().layers[layer].gradients;
                gradients.removeIf([&](const auto& g) { return g.strip == id; });
            }
        const bool stripElement = std::any_of(
            editingDescription().strips.begin(), editingDescription().strips.end(), [&](const auto& strip) {
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
    Q_EMIT documentController()->changed();
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
                Q_EMIT documentController()->contentChanged();
            }
        }
        Q_EMIT documentController()->changed();
    });
}
}

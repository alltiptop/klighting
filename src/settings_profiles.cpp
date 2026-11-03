// SPDX-License-Identifier: GPL-3.0-or-later
#include "preset_transfer.h"
#include "settings_controller.h"
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace Ambient
{
SettingsController::SettingsController(SettingsController* owner, const QString& profileId)
    : QObject(owner)
    , m_document(owner->m_document)
    , m_owner(owner)
    , m_profileId(profileId)
    , m_config(owner->m_config)
    , m_driver(m_document->driver)
    , m_effect(owner->m_effect)
    , m_service(owner->m_service)
    , m_collection(m_document->collection)
    , m_saved(m_document->saved)
    , m_fallback(m_document->fallback)
    , m_revision(m_document->revision)
    , m_error(m_document->error)
{
    connect(owner, &SettingsController::contentChanged, this, &SettingsController::contentChanged);
    connect(owner, &SettingsController::changed, this, &SettingsController::changed);
    connect(
        owner, &SettingsController::layerSelectionChanged, this, &SettingsController::layerSelectionChanged);
    connect(this, &SettingsController::contentChanged, this, [this] {
        updatePreview(true);
        Q_EMIT pressedKeysChanged();
    });
    connect(owner, &SettingsController::previewChanged, this, [this] { updatePreview(true); });
    connect(owner, &SettingsController::pressedKeysChanged, this, &SettingsController::pressedKeysChanged);
}
DeviceProfile& SettingsController::current()
{
    auto& scene = currentPreset();
    if (!scene.multiDevice)
        return scene;
    if (!m_owner) {
        for (const auto& device : configured())
            if (device.identity() == m_selectedDevice)
                for (auto& profile : scene.devices)
                    if (profile.targetDevice == device.identity())
                        return profile;
    }
    for (auto& profile : scene.devices)
        if (m_profileId.isEmpty() || profile.id == m_profileId)
            return profile;
    // A removed card can live until the next event loop iteration.
    return m_emptyProfile;
}
const DeviceProfile& SettingsController::current() const
{
    return const_cast<SettingsController*>(this)->current();
}
const DeviceInstance& SettingsController::editingInstance() const
{
    if (!m_owner)
        return m_driver.instance();
    const auto& profile = current();
    if (const auto* found = m_driver.find(profile.targetDevice);
        found && found->instance.description.id == profile.keyboardDeviceId)
        m_profileDevice = found->instance;
    else if (m_profileDevice.description.id.isEmpty()) {
        // A portable/legacy profile can use a connected model's layout. Explicit
        // local instances retain their own discovered capabilities on disconnect.
        for (const auto& found : m_driver.devices())
            if (found.instance.description.id == profile.keyboardDeviceId) {
                m_profileDevice = found.instance;
                break;
            }
    }
    return m_profileDevice;
}
void SettingsController::syncProfileEditors()
{
    if (m_owner || m_collection.presets.isEmpty())
        return;
    QStringList wanted;
    for (const auto* profile : deviceProfiles(currentPreset()))
        wanted.append(profile->id);
    const bool newPreset = m_editorsPresetId != currentPreset().id;
    bool changed = newPreset || wanted.size() != m_editors.size();
    for (auto it = m_editors.begin(); it != m_editors.end();) {
        if (newPreset || !wanted.contains(it.key())) {
            it.value()->deleteLater();
            it = m_editors.erase(it);
            changed = true;
        } else
            ++it;
    }
    m_editorsPresetId = currentPreset().id;
    for (const auto& id : wanted)
        if (!m_editors.contains(id)) {
            m_editors.insert(id, new SettingsController(this, id));
            changed = true;
        }
    // Never rebuild delegates on color edits or hardware polling.
    if (changed)
        Q_EMIT presetDevicesChanged();
}
QVariantList SettingsController::presetDevices() const
{
    QVariantList result;
    for (const auto* profile : deviceProfiles(currentPreset()))
        if (m_editors.contains(profile->id))
            result.append(QVariant::fromValue(m_editors.value(profile->id)));
    return result;
}
QVariantList SettingsController::addablePresetDevices() const
{
    QVariantList result;
    if (deviceProfiles(currentPreset()).size() >= 32)
        return result;
    bool legacyShown = false;
    for (const auto& device : configured()) {
        const bool present = profileFor(currentPreset(), device);
        if (present && (currentPreset().multiDevice || !legacyShown)) {
            legacyShown = true;
            continue;
        }
        result.append(QVariantMap { { "id", device.identity() }, { "name", device.name } });
    }
    return result;
}
QVariantMap SettingsController::profileInfo() const
{
    const auto& profile = current();
    auto name = editingDescription().name;
    bool enabled = false, connected = false;
    for (const auto& device : configured())
        if (profile.keyboardDeviceId == device.modelId
            && (profile.targetDevice.isEmpty() || profile.targetDevice == device.identity())) {
            name = device.name;
            enabled = device.enabled;
            const auto* found = m_driver.find(device.identity());
            connected = found && found->instance.description.id == device.modelId;
            break;
        }
    return { { "id", profile.id }, { "name", name.isEmpty() ? profile.keyboardDeviceId : name },
        { "enabled", enabled }, { "connected", connected },
        { "layoutAvailable", !editingDescription().id.isEmpty() } };
}
DeviceProfile SettingsController::newProfile(const DeviceConfiguration& device) const
{
    DeviceProfile profile;
    profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile.targetDevice = device.identity();
    profile.keyboardDeviceId = device.modelId;
    profile.name = device.name;
    profile.layers.clear();
    profile.defaultEffectId.clear();
    // An empty profile is intentional: no screen capture is requested until an
    // effect is assigned. This also works on devices with only an on/off switch.
    profile.effects.insert(DefaultAssignment, { });
    return profile;
}
void SettingsController::makeMultiDevice(LightingPreset& scene)
{
    if (scene.multiDevice)
        return;
    const auto legacy = static_cast<const DeviceProfile&>(scene);
    for (const auto& device : configured())
        if (device.modelId == legacy.keyboardDeviceId) {
            auto profile = legacy;
            profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            profile.targetDevice = device.identity();
            profile.name = device.name;
            scene.devices.append(profile);
        }
    if (scene.devices.isEmpty())
        scene.devices.append(legacy);
    const auto id = scene.id, name = scene.name;
    static_cast<DeviceProfile&>(scene) = DeviceProfile();
    scene.id = id;
    scene.name = name;
    scene.multiDevice = true;
    mergePresetLayers(scene);
}
bool SettingsController::addPresetDevice(const QString& identity)
{
    const auto available = addablePresetDevices();
    if (std::none_of(available.begin(), available.end(),
            [&](const auto& entry) { return entry.toMap().value("id").toString() == identity; }))
        return false;
    for (const auto& device : configured())
        if (device.identity() == identity) {
            makeMultiDevice(currentPreset());
            // Splitting a legacy model-wide profile already creates one copy for
            // each matching instance. New models start with an empty profile.
            if (!profileFor(currentPreset(), device))
                currentPreset().devices.append(newProfile(device));
            mergePresetLayers(currentPreset());
            touch();
            return true;
        }
    return false;
}
void SettingsController::removePresetDevice(const QString& profileId)
{
    const auto devices = configured();
    auto& scene = currentPreset();
    if (!scene.multiDevice) {
        if (scene.id != profileId)
            return;
        const auto id = scene.id, name = scene.name;
        scene.layerConditions = presetLayerConditions(scene);
        static_cast<DeviceProfile&>(scene) = DeviceProfile();
        scene.id = id;
        scene.name = name;
        scene.multiDevice = true;
    } else if (!scene.devices.removeIf([&](const auto& p) { return p.id == profileId; }))
        return;
    // Removing the last legacy profile must not remove its device configuration.
    if (!m_collection.devices)
        m_collection.devices = devices;
    touch();
}
void SettingsController::copyProfile()
{
    m_document->clipboard = current();
    m_document->clipboardDevice = editingInstance();
    m_document->clipboardEffects = m_collection.effects;
    Q_EMIT documentController()->contentChanged();
}
bool SettingsController::canPasteProfile() const
{
    if (!m_document->clipboard || editingDescription().id.isEmpty())
        return false;
    auto references = referencedEffects(*m_document->clipboard);
    for (const auto& effect : m_collection.effects)
        references.remove(effect.id);
    return references.isEmpty();
}
QStringList SettingsController::pasteWarnings() const
{
    if (!canPasteProfile())
        return { };
    return transferProfile(
        *m_document->clipboard, m_document->clipboardDevice, editingInstance(), m_document->clipboardEffects)
        .warnings;
}
bool SettingsController::pasteProfile()
{
    if (!canPasteProfile())
        return false;
    auto result = transferProfile(
        *m_document->clipboard, m_document->clipboardDevice, editingInstance(), m_document->clipboardEffects);
    // The destination's identity and local-effect namespace never come from the clipboard.
    result.profile.id = current().id;
    result.profile.name = current().name;
    result.profile.targetDevice = current().targetDevice;
    if (currentPreset().multiDevice) {
        auto scene = currentPreset();
        for (auto& profile : scene.devices)
            if (profile.id == result.profile.id)
                profile = result.profile;
        if (!mergePresetLayers(scene))
            return false;
        currentPreset() = std::move(scene);
    } else {
        current() = std::move(result.profile);
    }
    touch();
    return true;
}
}

namespace Ambient
{
namespace
{
    bool selectionContainsEffect(SettingsController* editor, int layer, const QVariantList& keys,
        const QString& id, std::optional<bool> above = { })
    {
        const auto rows = editor->selectionEffects(layer, keys).value("items").toList();
        return std::any_of(rows.begin(), rows.end(), [&](const auto& value) {
            const auto row = value.toMap();
            return row["id"].toString() == id && (!above || row["above"].toBool() == *above);
        });
    }
}
QList<SettingsController::SelectionTarget> SettingsController::selectionTargets(
    const QVariantList& targets) const
{
    QList<SelectionTarget> result;
    QSet<QString> seen;
    for (const auto& value : targets) {
        const auto target = value.toMap();
        const auto id = target.value("profile").toString();
        auto* editor = documentController()->m_editors.value(id);
        const int layer = target.value("layer", -1).toInt();
        const auto keys = target.value("keys").toList();
        if (!editor || seen.contains(id) || layer < -1 || layer >= editor->current().layers.size()
            || editor->effectIds(keys).isEmpty())
            return { };
        seen.insert(id);
        result.append({ editor, layer, keys });
    }
    return result;
}
QVariantMap SettingsController::deviceSelectionCapabilities(const QVariantList& targets) const
{
    const auto selected = selectionTargets(targets);
    bool color = !selected.isEmpty(), brightness = color, ambient = color;
    int steps = selected.isEmpty() ? 0 : selected.first().editor->device()["brightnessSteps"].toInt();
    for (const auto& target : selected) {
        const auto d = target.editor->device();
        color &= d["color"].toBool();
        brightness &= d["brightness"].toBool() || d["color"].toBool();
        ambient &= d["ambient"].toBool();
        if (steps != d["brightnessSteps"].toInt())
            steps = 0;
    }
    return { { "id", selected.isEmpty() ? QString() : QStringLiteral("selection") }, { "color", color },
        { "brightness", brightness }, { "ambient", ambient }, { "brightnessSteps", steps } };
}
QVariantMap SettingsController::deviceSelectionEffects(const QVariantList& targets) const
{
    QVariantList rows, first;
    QVariantMap options;
    bool mixed = false, defaults = true;
    int count = 0;
    const auto selected = selectionTargets(targets);
    for (int i = 0; i < selected.size(); ++i) {
        const auto& target = selected[i];
        const auto state = target.editor->selectionEffects(target.layer, target.keys);
        const auto items = state["items"].toList();
        QVariantList signature;
        for (const auto& value : items) {
            auto row = value.toMap();
            row.remove("count");
            signature.append(row);
        }
        if (!i)
            first = signature;
        else
            mixed |= signature != first;
        mixed |= state["mixed"].toBool();
        defaults &= state["defaults"].toBool();
        count += state["count"].toInt();
        for (const auto& value : items) {
            auto row = value.toMap();
            const auto id = row["id"].toString();
            const auto definition = target.editor->assignedEffectOptions(id);
            if (options.contains(id) && options[id].toMap() != definition)
                mixed = true;
            else
                options.insert(id, definition);
            auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& existing) {
                const auto r = existing.toMap();
                return r["id"] == row["id"] && r["above"] == row["above"];
            });
            if (found == rows.end())
                rows.append(row);
            else {
                auto merged = found->toMap();
                merged["count"] = merged["count"].toInt() + row["count"].toInt();
                if (merged["opacity"] != row["opacity"])
                    merged["opacity"] = -1;
                if (merged["color"] != row["color"])
                    merged["color"] = QString();
                *found = merged;
            }
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.toMap()["above"].toBool() < b.toMap()["above"].toBool();
    });
    return { { "items", rows }, { "mixed", mixed }, { "count", count },
        { "defaults", defaults && !selected.isEmpty() } };
}
QVariantMap SettingsController::deviceSelectionEffectOptions(
    const QVariantList& targets, const QString& id) const
{
    for (const auto& target : selectionTargets(targets)) {
        if (!target.editor->localEffectOptions(id).isEmpty()
            && !selectionContainsEffect(target.editor, target.layer, target.keys, id))
            continue;
        const auto options = target.editor->assignedEffectOptions(id);
        if (!options.isEmpty())
            return options;
    }
    return { };
}
QString SettingsController::editDeviceSelection(
    const QVariantList& targets, const QString& action, const QVariantMap& arguments)
{
    const auto selected = selectionTargets(targets);
    if (selected.isEmpty())
        return { };
    const auto id = arguments.value("id").toString();
    const auto options = deviceSelectionEffectOptions(targets, id);
    const auto type = action == "new"                             ? arguments.value("type").toString()
        : id == QLatin1String(InlineColorId) || action == "color" ? QStringLiteral("solid")
                                                                  : options.value("type").toString();
    const bool supported
        = type == "solid" || type == "brightness" || type == "ambient" || type == "notifications";
    if (!supported && action != "remove" && action != "clear")
        return { };
    const bool above = arguments.value("above").toBool();
    // Preflight the whole selection so a full stack or local-effect limit cannot
    // leave only some devices updated.
    for (const auto& target : selected) {
        auto* editor = target.editor;
        if ((action == "new" || action == "localize") && editor->current().localEffects.size() >= 128) {
            m_error = tr("Use at most 128 local effects per preset.");
            Q_EMIT documentController()->changed();
            return { };
        }
        if (action == "new" || action == "add"
            || (action == "update" && above != arguments.value("wasAbove").toBool()))
            for (int key : editor->effectIds(target.keys)) {
                const auto list = above ? editor->current().effectOverrides.value(
                                              editor->effectSource(editor->current().effectOverrides, key))
                                        : editor->localEffects(target.layer, key);
                if (list.size() >= 32
                    && (action == "new" || std::none_of(list.begin(), list.end(), [&](const auto& entry) {
                           return entry.id == id;
                       }))) {
                    m_error = tr("Use at most 32 effects per element and layer.");
                    Q_EMIT documentController()->changed();
                    return { };
                }
            }
    }
    if (action == "new") {
        const auto& first = selected.first();
        const auto created = first.editor->addLocalEffect(first.layer, first.keys, type);
        if (created.isEmpty())
            return { };
        const auto definition = *first.editor->editableEffect(created);
        for (int i = 1; i < selected.size(); ++i) {
            const auto& target = selected[i];
            target.editor->current().localEffects.append(definition);
            target.editor->addAssignedEffect(target.layer, target.keys, created);
        }
        return created;
    }
    if (action == "localize") {
        QList<QPair<SettingsController*, QString>> localized;
        QString common = id;
        for (const auto& target : selected) {
            if (target.editor->localEffectOptions(id).isEmpty()
                || !selectionContainsEffect(target.editor, target.layer, target.keys, id, above))
                continue;
            const auto copy = target.editor->localizeEffect(target.layer, target.keys, id, above);
            if (copy.isEmpty())
                return { };
            localized.append({ target.editor, copy });
            if (copy != id)
                common = copy;
        }
        // Copies use one ID across the selected profiles, while each definition
        // remains owned by its device. Unselected assignments retain their ID.
        for (const auto& [editor, copy] : localized) {
            if (copy == common)
                continue;
            editor->editableEffect(copy)->id = common;
            const auto rename = [&](EffectAssignments& assignments) {
                for (auto& list : assignments)
                    for (auto& effect : list)
                        if (effect.id == copy)
                            effect.id = common;
            };
            auto& profile = editor->current();
            rename(profile.effects);
            rename(profile.effectOverrides);
            for (auto& layer : profile.layers)
                rename(layer.effects);
            editor->touch();
        }
        return localized.isEmpty() ? QString() : common;
    }
    bool sharedEdited = false;
    for (const auto& target : selected) {
        auto* editor = target.editor;
        if (action == "add")
            editor->addAssignedEffect(target.layer, target.keys, id, above);
        else if (action == "remove")
            editor->removeAssignedEffect(target.layer, target.keys, id, above);
        else if (action == "move")
            editor->moveAssignedEffect(
                target.layer, target.keys, id, above, arguments.value("offset").toInt());
        else if (action == "update")
            editor->updateAssignedEffect(target.layer, target.keys, id, arguments.value("wasAbove").toBool(),
                above, arguments.value("opacity").toInt());
        else if (action == "color")
            editor->setAssignedColor(target.layer, target.keys, arguments.value("value").toString());
        else if (action == "clear")
            editor->clearKeyLighting(target.layer, target.keys, arguments.value("inherit").toBool());
        else {
            if (editor->assignedEffectOptions(id).isEmpty())
                continue;
            if (editor->localEffectOptions(id).isEmpty()) {
                if (sharedEdited)
                    continue;
                sharedEdited = true;
            } else if (!selectionContainsEffect(editor, target.layer, target.keys, id))
                continue;
            const auto key = arguments.value("key").toString();
            const auto value = arguments.value("value");
            if (action == "solid")
                editor->setSolidColor(value.toString(), id);
            else if (action == "brightness")
                editor->setBrightness(value.toInt(), id);
            else if (action == "ambient")
                editor->setAmbient(key, value.toInt(), id);
            else if (action == "notifications")
                editor->setNotification(key, value, id);
        }
    }
    return { };
}
}

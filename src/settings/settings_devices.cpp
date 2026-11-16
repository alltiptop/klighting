// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_controller.h"
#include <QDebug>
#include <QPointer>
#include <algorithm>

namespace Ambient
{
QList<DeviceConfiguration>& SettingsController::editDevices()
{
    if (!m_collection.devices)
        m_collection.devices = configured();
    return *m_collection.devices;
}
QVariantList SettingsController::devices() const
{
    QVariantList result;
    for (const auto& d : configured()) {
        const auto* found = m_driver.find(d.identity());
        result.append(QVariantMap { { "id", d.identity() }, { "name", d.name }, { "model", d.modelId },
            { "enabled", d.enabled },
            { "connected", found && found->instance.description.id == d.modelId } });
    }
    return result;
}
QVariantList SettingsController::availableDevices() const
{
    QVariantList result;
    const auto saved = configured();
    for (const auto& d : m_driver.devices())
        if (std::none_of(
                saved.begin(), saved.end(), [&](const auto& c) { return c.identity() == d.identity(); }))
            result.append(QVariantMap { { "id", d.identity() }, { "name", d.instance.description.name },
                { "instance", d.instance.id }, { "driver", d.driverId } });
    return result;
}
int SettingsController::selectedDevice() const
{
    const auto entries = configured();
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].identity() == m_selectedDevice)
            return i;
    return -1;
}
QVariantMap SettingsController::deviceSettings() const
{
    const int i = selectedDevice();
    if (i < 0)
        return { };
    const auto d = configured()[i];
    const auto conflict = conflictingDevice(d, configured(), m_driver);
    return { { "id", d.identity() }, { "name", d.name }, { "enabled", d.enabled }, { "driver", d.driverId },
        { "enableBlockedBy", conflict }, { "instance", d.instanceId }, { "model", d.modelId },
        { "brightnessChannel", d.brightnessChannel }, { "pauseOnBattery", d.pauseOnBattery },
        { "pauseOnLowBattery", d.pauseOnLowBattery }, { "lightbarOffOnBattery", d.lightbarOffOnBattery },
        { "lightbarOffOnLowBattery", d.lightbarOffOnLowBattery } };
}
void SettingsController::selectDevice(int index)
{
    const auto entries = configured();
    if (index < 0 || index >= entries.size())
        return;
    m_selectedDevice = entries[index].identity();
    m_driver.select(m_selectedDevice);
    const int preset = devicePreset(m_collection, entries[index], { });
    if (preset >= 0)
        m_selected = preset;
    loadLightbar();
    touch();
}
void SettingsController::refreshDevices()
{
    // Freeze the legacy single-device choice before discovery changes order.
    const auto previous = configured();
    m_driver.refresh();
    if (m_driver.discoveryPending())
        m_discoveryTimer.start(50);
    if (!m_collection.devices && previous != configured())
        m_collection.devices = previous;
    loadLightbar(true);
    Q_EMIT documentController()->contentChanged();
    Q_EMIT documentController()->changed();
}
bool SettingsController::addDevice(const QString& identity)
{
    const auto* found = m_driver.find(identity);
    if (!found || configured().size() >= 32)
        return false;
    auto entries = configured();
    if (std::any_of(entries.begin(), entries.end(), [&](const auto& d) { return d.identity() == identity; }))
        return false;
    const auto& model = found->instance.description;
    DeviceConfiguration d { found->driverId, found->instance.id, model.id, model.name, true };
    d.enabled = conflictingDevice(d, entries, m_driver).isEmpty();
    const auto base = std::find_if(m_collection.presets.begin(), m_collection.presets.end(),
        [&](const auto& p) { return p.id == m_collection.defaultId; });
    if (base == m_collection.presets.end())
        return false;
    auto updated = *base;
    makeMultiDevice(updated);
    if (!profileFor(updated, d)) {
        if (updated.devices.size() >= 32)
            return false;
        updated.devices.append(newProfile(d));
        if (!mergePresetLayers(updated))
            return false;
    }
    *base = std::move(updated);
    entries.append(d);
    m_collection.devices = entries;
    selectDevice(entries.size() - 1);
    return true;
}
void SettingsController::removeDevice()
{
    const int i = selectedDevice();
    if (i < 0 || busy())
        return;
    const auto identity = m_selectedDevice;
    auto& entries = editDevices();
    entries.removeAt(i);
    m_controls.erase(identity);
    m_selectedDevice.clear();
    m_driver.select({ });
    if (!entries.isEmpty())
        selectDevice(std::min<int>(i, entries.size() - 1));
    touch();
}
void SettingsController::setDevice(const QString& key, const QVariant& value)
{
    const int i = selectedDevice();
    if (i < 0)
        return;
    auto d = configured()[i];
    if (key == "enabled") {
        if (value.toBool() && !conflictingDevice(d, configured(), m_driver).isEmpty())
            return;
        d.enabled = value.toBool();
    } else if (key == "brightnessChannel") {
        if (!QStringList { "average", "red", "green", "blue" }.contains(value.toString()))
            return;
        d.brightnessChannel = value.toString();
    } else if (key == "pauseOnBattery")
        d.pauseOnBattery = value.toBool();
    else if (key == "pauseOnLowBattery")
        d.pauseOnLowBattery = value.toBool();
    else if (key == "lightbarOffOnBattery")
        d.lightbarOffOnBattery = value.toBool();
    else if (key == "lightbarOffOnLowBattery")
        d.lightbarOffOnLowBattery = value.toBool();
    else if (key == "name") {
        const auto name = value.toString().trimmed();
        if (name.isEmpty() || name.size() > 128)
            return;
        d.name = name;
    } else
        return;
    editDevices()[i] = d;
    touch();
}
QVariantList SettingsController::strips() const
{
    QVariantList result;
    for (const auto& strip : editingDescription().strips) {
        QVariantList elements;
        for (const auto& element : strip.elements)
            elements.append(QVariantMap { { "id", element.id }, { "label", element.label } });
        QVariantList path;
        for (const auto& point : strip.path)
            path.append(QVariantMap {
                { "id", point.elementId }, { "x", point.position.x() }, { "y", point.position.y() } });
        result.append(QVariantMap { { "id", strip.id }, { "name", strip.name }, { "elements", elements },
            { "path", path }, { "pathWidth", strip.pathWidth } });
    }
    return result;
}
void SettingsController::setLightbarEnabled(bool value)
{
    const auto state = controls();
    if (!state || !state->available || state->saving)
        return;
    state->enabled = value;
    touch();
}
std::shared_ptr<SettingsController::Controls> SettingsController::controls() const
{
    const auto it = m_controls.find(m_selectedDevice);
    return it == m_controls.end() ? nullptr : it->second;
}
bool SettingsController::lightbarAvailable() const
{
    const auto s = controls();
    return s && s->available;
}
bool SettingsController::lightbarEnabled() const
{
    const auto s = controls();
    return s && s->enabled;
}
QString SettingsController::lightbarStatus() const
{
    const auto s = controls();
    return s ? s->status : QString();
}
bool SettingsController::busy() const
{
    return m_effect->busy() || m_service->busy()
        || std::any_of(m_controls.begin(), m_controls.end(), [](const auto& entry) {
               return entry.second->saving
                   || (entry.second->reading && entry.second->enabled != entry.second->saved);
           });
}
void SettingsController::loadLightbar(bool refresh)
{
    if (!m_driver.driver() || !m_driver.description().capabilities.testFlag(DeviceCapability::LightbarPower))
        return;
    auto state = controls();
    if (state && (!refresh || state->reading || state->saving))
        return;
    if (!state) {
        state = std::make_shared<Controls>();
        state->backend = m_driver.driver()->createControls(m_driver.instance());
        if (!state->backend)
            return;
        m_controls.emplace(m_selectedDevice, state);
    }
    state->reading = true;
    state->status = tr("Reading lightbar settings…");
    auto options = m_driver.instance().options;
    const QPointer<SettingsController> self(this);
    state->backend->readLightbar(
        options, [self, weak = std::weak_ptr<Controls>(state)](LightbarState result) {
            const auto state = weak.lock();
            if (!self || !state)
                return;
            state->reading = false;
            state->available = result.available;
            if (result.available) {
                if (state->enabled == state->saved)
                    state->enabled = result.enabled;
                state->saved = result.enabled;
            }
            state->status = result.available ? QString() : tr("Lightbar unavailable: %1").arg(result.error);
            if (self)
                Q_EMIT self->changed();
        });
}
void SettingsController::saveLightbar()
{
    for (const auto& [id, state] : m_controls) {
        if (!state->available || state->enabled == state->saved)
            continue;
        const bool enabled = state->enabled;
        state->saving = true;
        state->status = tr("Saving lightbar setting…");
        const QPointer<SettingsController> self(this);
        state->backend->writeLightbar(enabled, [self, state, enabled](const QString& error) {
            if (!error.isEmpty())
                qWarning().noquote() << "Ambient LED lightbar:" << error;
            state->saving = false;
            if (error.isEmpty())
                state->saved = enabled;
            state->status
                = error.isEmpty() ? QString() : tr("Could not save lightbar setting: %1").arg(error);
            if (self)
                Q_EMIT self->changed();
        });
    }
}
}

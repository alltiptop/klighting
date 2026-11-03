// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_driver.h"
#include <QPluginLoader>
#include <vector>

namespace Ambient
{
DeviceEnvironment deviceEnvironment();
QStringList driverDirectories();
QString validateDescription(const DeviceDescription& description);
struct DiscoveredDevice {
    QString driverId;
    DeviceInstance instance;
    DeviceDriver* driver = nullptr;
    QString identity() const { return driverId + QLatin1Char(':') + instance.id; }
};
QStringList deviceClaims(const DiscoveredDevice& device);
bool devicesConflict(const DiscoveredDevice& a, const DiscoveredDevice& b);
// The selected entry is only an editor/probe convenience. Runtime uses every entry.
class LoadedDriver
{
public:
    explicit LoadedDriver(const DeviceEnvironment& environment = deviceEnvironment(),
        const QStringList& directories = driverDirectories());
    void refresh();
    bool discoveryPending() const;
    bool hasDrivers() const { return !m_plugins.empty(); }
    const QList<DiscoveredDevice>& devices() const { return m_devices; }
    const DiscoveredDevice* find(const QString& identity) const;
    void select(const QString& identity) { m_selected = identity; }
    DeviceDriver* driver() const;
    const DeviceInstance& instance() const;
    const DeviceDescription& description() const { return instance().description; }
    QString error() const { return m_error; }

private:
    struct Plugin {
        QString id;
        std::unique_ptr<QPluginLoader> loader;
        DeviceDriver* driver;
    };
    DeviceEnvironment m_environment;
    std::vector<Plugin> m_plugins;
    QList<DiscoveredDevice> m_devices;
    QString m_selected, m_error;
    QStringList m_loadErrors;
};
}

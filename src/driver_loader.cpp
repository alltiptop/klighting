// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver_loader.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Ambient
{
QStringList deviceClaims(const DiscoveredDevice& device)
{
    auto claims = device.instance.options.value("DeviceClaims").toStringList();
    // API 4's original built-in drivers used this stable instance convention.
    // Keep their binaries and device-specific metadata unchanged.
    if (claims.isEmpty() && device.instance.id == "internal" && !device.instance.description.keys.isEmpty())
        claims.append(QStringLiteral("internal-keyboard"));
    return claims;
}
bool devicesConflict(const DiscoveredDevice& a, const DiscoveredDevice& b)
{
    if (a.identity() == b.identity())
        return false;
    // Separate LED-class zones are independently addressable by the same driver.
    if (a.driverId == b.driverId)
        return false;
    const auto left = deviceClaims(a), right = deviceClaims(b);
    return std::any_of(left.begin(), left.end(), [&](const auto& claim) { return right.contains(claim); });
}
DeviceEnvironment deviceEnvironment()
{
    QFile board(QStringLiteral("/sys/class/dmi/id/board_name"));
    return { board.open(QIODevice::ReadOnly) ? QString::fromUtf8(board.readAll().trimmed()) : QString() };
}
QStringList driverDirectories()
{
    if (qEnvironmentVariableIsSet("AMBIENT_LED_DRIVER_PATH"))
        return qEnvironmentVariable("AMBIENT_LED_DRIVER_PATH")
            .split(QDir::listSeparator(), Qt::SkipEmptyParts);
    auto paths = QCoreApplication::libraryPaths();
    paths.append(QCoreApplication::applicationDirPath() + QStringLiteral("/lib"));
    paths.removeDuplicates();
    for (auto& path : paths)
        path += QStringLiteral("/ambient-led/drivers/v4");
    return paths;
}
QString validateDescription(const DeviceDescription& d)
{
    static const QRegularExpression idPattern(QStringLiteral("\\A[a-zA-Z0-9_-]{1,64}\\z"));
    if (!idPattern.match(d.id).hasMatch() || d.name.isEmpty() || d.keys.size() > 1024 || d.strips.size() > 64)
        return QStringLiteral("Invalid device identity or keyboard size.");
    const auto has = [&d](DeviceCapability c) { return d.capabilities.testFlag(c); };
    if ((!has(DeviceCapability::Color) && !has(DeviceCapability::Brightness))
        || (has(DeviceCapability::PerKeyColor) && (!has(DeviceCapability::Color) || d.keys.isEmpty()))
        || (has(DeviceCapability::PerStripColor) && (!has(DeviceCapability::Color) || d.strips.isEmpty()))
        || (has(DeviceCapability::LightbarPower) && !has(DeviceCapability::Lightbar)))
        return QStringLiteral("Inconsistent device capabilities.");
    QSet<int> ids;
    for (const auto& key : d.keys) {
        const auto& r = key.rect;
        if (key.id < 0 || key.id > MaxKeyId || ids.contains(key.id) || key.label.isEmpty()
            || !std::isfinite(r.x()) || !std::isfinite(r.y()) || !std::isfinite(r.width())
            || !std::isfinite(r.height()) || r.width() <= 0 || r.height() <= 0 || std::abs(r.x()) > 100000
            || std::abs(r.y()) > 100000 || r.width() > 100000 || r.height() > 100000)
            return QStringLiteral("Invalid or duplicate keyboard key.");
        ids.insert(key.id);
    }
    QSet<int> stripIds;
    int elements = 0;
    int pathPoints = 0;
    for (const auto& strip : d.strips) {
        if (strip.id < FirstStripId || strip.id > MaxAssignmentId || stripIds.contains(strip.id)
            || strip.name.isEmpty() || strip.elements.isEmpty() || (elements += strip.elements.size()) > 1024)
            return QStringLiteral("Invalid or duplicate strip.");
        stripIds.insert(strip.id);
        QSet<int> elementIds;
        for (const auto& element : strip.elements) {
            if (element.id < 0 || element.id > MaxKeyId || ids.contains(element.id)
                || element.label.isEmpty())
                return QStringLiteral("Invalid or duplicate strip element.");
            ids.insert(element.id);
            elementIds.insert(element.id);
        }
        if (!strip.path.isEmpty()) {
            if (strip.path.size() < 2 || (pathPoints += strip.path.size()) > 8192
                || !std::isfinite(strip.pathWidth) || strip.pathWidth <= 0 || strip.pathWidth > 1000)
                return QStringLiteral("Invalid strip path size or width.");
            QSet<int> described;
            for (qsizetype i = 0; i < strip.path.size(); ++i) {
                const auto& p = strip.path[i];
                if (!elementIds.contains(p.elementId) || !std::isfinite(p.position.x())
                    || !std::isfinite(p.position.y()) || std::abs(p.position.x()) > 100000
                    || std::abs(p.position.y()) > 100000
                    || (i > 0 && (p.position - strip.path[i - 1].position).manhattanLength() < 0.000001))
                    return QStringLiteral("Invalid strip path point or element reference.");
                described.insert(p.elementId);
            }
            if (described != elementIds)
                return QStringLiteral("Strip path must include every element.");
        }
        ids.insert(strip.id);
    }
    for (auto it = d.groups.cbegin(); it != d.groups.cend(); ++it) {
        if (it.key().isEmpty() || it.value().isEmpty())
            return QStringLiteral("Invalid keyboard group.");
        for (int id : it.value())
            if (!ids.contains(id))
                return QStringLiteral("Keyboard group refers to an unknown key.");
    }
    return { };
}
LoadedDriver::LoadedDriver(const DeviceEnvironment& environment, const QStringList& directories)
    : m_environment(environment)
{
    QSet<QString> seen, duplicateIds;
    for (const auto& directory : directories) {
        for (const auto& file :
            QDir(directory).entryInfoList({ QStringLiteral("*.so") }, QDir::Files, QDir::Name)) {
            const auto path = file.canonicalFilePath();
            if (seen.contains(path))
                continue;
            seen.insert(path);
            auto loader = std::make_unique<QPluginLoader>(path);
            const auto metadata = loader->metaData();
            const auto info = metadata.value("MetaData").toObject();
            const auto id = info.value("Id").toString();
            static const QRegularExpression pattern(QStringLiteral("\\A[a-zA-Z0-9_-]{1,64}\\z"));
            if (metadata.value("IID") != QLatin1String(AMBIENT_DEVICE_DRIVER_IID)
                || info.value("ApiVersion").toInt() != DriverApiVersion || !pattern.match(id).hasMatch()) {
                m_loadErrors.append(
                    file.fileName() + QStringLiteral(": incompatible driver API or identity"));
                continue;
            }
            loader->setLoadHints(QLibrary::PreventUnloadHint);
            auto* driver = qobject_cast<DeviceDriver*>(loader->instance());
            if (!driver) {
                m_loadErrors.append(file.fileName() + QStringLiteral(": ") + loader->errorString());
                continue;
            }
            if (std::any_of(m_plugins.begin(), m_plugins.end(), [&](const auto& p) { return p.id == id; }))
                duplicateIds.insert(id);
            m_plugins.push_back({ id, std::move(loader), driver });
        }
    }
    for (const auto& id : duplicateIds) {
        m_loadErrors.append(
            QStringLiteral("Multiple lighting drivers with ID %1. Remove duplicate drivers.").arg(id));
        std::erase_if(m_plugins, [&](const auto& p) { return p.id == id; });
    }
    refresh();
    if (!m_devices.isEmpty())
        m_selected = m_devices.first().identity();
}
void LoadedDriver::refresh()
{
    QList<DiscoveredDevice> next;
    auto errors = m_loadErrors;
    for (const auto& plugin : m_plugins) {
        const auto instances = plugin.driver->discover(m_environment);
        if (instances.size() > 64) {
            errors.append(plugin.id + QStringLiteral(": too many device instances"));
            continue;
        }
        QSet<QString> ids, duplicates;
        for (const auto& instance : instances) {
            if (ids.contains(instance.id))
                duplicates.insert(instance.id);
            ids.insert(instance.id);
        }
        for (const auto& instance : instances) {
            const auto error = validateDescription(instance.description);
            if (instance.id.isEmpty() || instance.id.size() > 256 || instance.id.contains(QChar::Null)
                || duplicates.contains(instance.id) || !error.isEmpty()) {
                errors.append(plugin.id + QStringLiteral(": invalid or duplicate device instance. ") + error);
                continue;
            }
            next.append({ plugin.id, instance, plugin.driver });
        }
    }
    m_devices = std::move(next);
    if (m_devices.isEmpty() && errors.isEmpty())
        errors.append(QStringLiteral("No compatible lighting driver found for this device."));
    m_error = errors.join(QStringLiteral("; "));
}
const DiscoveredDevice* LoadedDriver::find(const QString& identity) const
{
    for (const auto& device : m_devices)
        if (device.identity() == identity)
            return &device;
    return nullptr;
}
bool LoadedDriver::discoveryPending() const
{
    return std::any_of(m_plugins.begin(), m_plugins.end(),
        [](const auto& plugin) { return plugin.loader->instance()->property("discoveryPending").toBool(); });
}
DeviceDriver* LoadedDriver::driver() const
{
    const auto* device = find(m_selected);
    return device ? device->driver : nullptr;
}
const DeviceInstance& LoadedDriver::instance() const
{
    static const DeviceInstance empty;
    const auto* device = find(m_selected);
    return device ? device->instance : empty;
}
}

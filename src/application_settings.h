// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QVariantMap>
#include <optional>

namespace Ambient
{
enum class SettingsAccess { ReadOnly, Migrate };
struct ApplicationSettings {
    QVariantMap capture;
    double brightness = 1.0, saturation = 1.0;
};
QString applicationSettingsPath();
QString driverSettingsPath(const QString& driverId);
// Only the service uses Migrate. Readers merge legacy values in memory until
// the service atomically publishes each independent settings file and marker.
std::optional<ApplicationSettings> loadApplicationSettings(SettingsAccess access, QString* error);
std::optional<QVariantMap> loadDriverSettings(
    const QString& driverId, const QJsonObject& legacyDefaults, SettingsAccess access, QString* error);
}

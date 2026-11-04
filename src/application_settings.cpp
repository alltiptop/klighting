// SPDX-License-Identifier: GPL-3.0-or-later
#include "application_settings.h"
#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <memory>

namespace Ambient
{
namespace
{
    constexpr qsizetype MaximumSettingsBytes = 64 * 1024;
    QString settingsDirectory()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/klighting");
    }
    std::optional<QJsonObject> readDocument(const QString& path, QString* error)
    {
        QFile file(path);
        if (!file.exists())
            return QJsonObject { { "version", 1 }, { "values", QJsonObject() } };
        if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumSettingsBytes) {
            *error = QStringLiteral("Cannot read settings: %1").arg(path);
            return std::nullopt;
        }
        const auto bytes = file.read(MaximumSettingsBytes + 1);
        if (bytes.size() > MaximumSettingsBytes) {
            *error = QStringLiteral("Settings document is too large: %1").arg(path);
            return std::nullopt;
        }
        const auto document = QJsonDocument::fromJson(bytes);
        const auto root = document.object();
        if (!document.isObject() || root.value("version") != QJsonValue(1)
            || (!root.value("values").isUndefined() && !root.value("values").isObject())
            || (!root.value("legacyImported").isUndefined() && !root.value("legacyImported").isBool())) {
            *error = QStringLiteral("Invalid settings document: %1").arg(path);
            return std::nullopt;
        }
        return root;
    }
    bool validateValues(const QJsonObject& values, const QJsonObject& defaults, QString* error)
    {
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            const auto value = values.value(it.key());
            if (value.type() != it.value().type() || (value.isString() && value.toString().size() > 4096)
                || (value.isDouble() && !std::isfinite(value.toDouble()))) {
                *error = QStringLiteral("Invalid setting: %1").arg(it.key());
                return false;
            }
        }
        return true;
    }
    std::optional<QVariantMap> readSettings(
        const QString& path, const QJsonObject& defaults, SettingsAccess access, QString* error)
    {
        error->clear();
        auto root = readDocument(path, error);
        if (!root)
            return std::nullopt;
        // Completed migrations are read-only, including when the directory is no longer writable.
        const bool needsMigration = !root->value("legacyImported").toBool();
        std::unique_ptr<QLockFile> lock;
        if (access == SettingsAccess::Migrate && needsMigration) {
            if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
                *error = QStringLiteral("Cannot create settings directory: %1").arg(path);
                return std::nullopt;
            }
            lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
            if (!lock->tryLock()) {
                *error = QStringLiteral("Settings migration is busy: %1").arg(path);
                return std::nullopt;
            }
            root = readDocument(path, error); // Another process may have published before the lock.
            if (!root)
                return std::nullopt;
        }
        auto values = root->value("values").toObject();
        const bool importLegacy = !root->value("legacyImported").toBool();
        // KConfig's parser preserves its escaping and typed conversion rules. Never write kwinrc.
        std::unique_ptr<KConfig> legacy;
        KConfigGroup group;
        if (importLegacy) {
            legacy = std::make_unique<KConfig>(QStringLiteral("kwinrc"), KConfig::NoGlobals);
            group = KConfigGroup(legacy.get(), "Effect-ambientled");
        }
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            if (values.contains(it.key()))
                continue; // Explicit new settings always win, including false, zero and empty strings.
            QJsonValue value = it.value();
            if (importLegacy && group.hasKey(it.key())) {
                if (value.isString())
                    value = group.readEntry(it.key(), value.toString());
                else if (value.isBool())
                    value = group.readEntry(it.key(), value.toBool());
                else if (value.isDouble()) {
                    const auto number = group.readEntry(it.key(), value.toDouble());
                    if (std::isfinite(number))
                        value = number;
                }
            }
            values.insert(it.key(), value);
        }
        if (!validateValues(values, defaults, error))
            return std::nullopt;
        if (access == SettingsAccess::Migrate && importLegacy) {
            root->insert("values", values);
            root->insert("legacyImported", true);
            const auto bytes = QJsonDocument(*root).toJson();
            if (bytes.size() > MaximumSettingsBytes) {
                *error = QStringLiteral("Settings document is too large: %1").arg(path);
                return std::nullopt;
            }
            QSaveFile file(path);
            file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly)
                || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
                || file.write(bytes) != bytes.size() || !file.commit()) {
                *error = QStringLiteral("Cannot publish settings migration: %1").arg(path);
                return std::nullopt;
            }
        }
        QVariantMap result;
        for (auto it = defaults.begin(); it != defaults.end(); ++it)
            result.insert(it.key(), values.value(it.key()).toVariant());
        return result;
    }
}
QString applicationSettingsPath() { return settingsDirectory() + QStringLiteral("/application.json"); }
QString driverSettingsPath(const QString& driverId)
{
    static const QRegularExpression id(QStringLiteral("\\A[A-Za-z0-9_-]{1,64}\\z"));
    if (!id.match(driverId).hasMatch())
        return { };
    return settingsDirectory() + QStringLiteral("/drivers/") + driverId + QStringLiteral(".json");
}
std::optional<ApplicationSettings> loadApplicationSettings(SettingsAccess access, QString* error)
{
    const auto values = readSettings(applicationSettingsPath(),
        { { "ScreenName", "" }, { "Region", "full" }, { "BottomPercent", 25 }, { "Brightness", 1.0 },
            { "Saturation", 1.0 } },
        access, error);
    if (!values)
        return std::nullopt;
    ApplicationSettings settings;
    settings.capture = { { "screen", values->value("ScreenName") },
        { "bottomOnly", values->value("Region").toString() == QLatin1String("bottom") },
        { "bottomPercent", int(std::clamp(values->value("BottomPercent").toDouble(), 1.0, 100.0)) } };
    settings.brightness = std::clamp(values->value("Brightness").toDouble(), 0.0, 1.0);
    settings.saturation = std::clamp(values->value("Saturation").toDouble(), 0.0, 2.0);
    return settings;
}
std::optional<QVariantMap> loadDriverSettings(
    const QString& driverId, const QJsonObject& legacyDefaults, SettingsAccess access, QString* error)
{
    error->clear();
    if (legacyDefaults.isEmpty())
        return QVariantMap();
    const auto path = driverSettingsPath(driverId);
    static const QRegularExpression key(QStringLiteral("\\A[A-Za-z0-9_-]{1,64}\\z"));
    bool valid = !path.isEmpty() && legacyDefaults.size() <= 32;
    for (auto it = legacyDefaults.begin(); it != legacyDefaults.end(); ++it)
        valid = valid && key.match(it.key()).hasMatch()
            && (it.value().isString() || it.value().isBool() || it.value().isDouble());
    if (!valid) {
        *error = QStringLiteral("Invalid legacy settings metadata for driver %1").arg(driverId);
        return std::nullopt;
    }
    return readSettings(path, legacyDefaults, access, error);
}
}

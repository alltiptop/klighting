// SPDX-License-Identifier: GPL-3.0-or-later
#include "asus_controls.h"
#include "aura_protocol.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace Ambient
{
AsusControls::AsusControls() { registerAuraTypes(); }
void AsusControls::readLightbar(const QVariantMap& options, std::function<void(LightbarState)> done)
{
    m_owner.clear();
    m_path.clear();
    const auto generation = ++m_generation;
    const auto preferredPath = options.value("AuraObjectPath").toString();
    auto call = QDBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    call << QString::fromLatin1(AsusdService);
    auto* owner = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 1000), this);
    connect(owner, &QDBusPendingCallWatcher::finished, this,
        [this, generation, preferredPath, done](auto* owner) {
            const QDBusPendingReply<QString> reply = *owner;
            owner->deleteLater();
            if (generation != m_generation)
                return;
            if (reply.isError()) {
                done({ false, false, tr("Could not connect to asusd.") });
                return;
            }
            const QString service = reply.value();
            const auto call = QDBusMessage::createMethodCall(
                service, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
            auto* watcher
                = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 1000), this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, generation, preferredPath, service, done](auto* watcher) {
                    const QDBusPendingReply<ManagedObjects> reply = *watcher;
                    watcher->deleteLater();
                    if (generation != m_generation)
                        return;
                    if (reply.isError()) {
                        done({ false, false, tr("Could not read lightbar power settings.") });
                        return;
                    }
                    QString selected;
                    QVariantMap props;
                    const auto objects = reply.value();
                    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
                        const auto path = it.key().path();
                        const auto p = it.value().value(AuraInterface);
                        if ((!preferredPath.isEmpty() && path != preferredPath) || !compatibleAura(path, p))
                            continue;
                        if (!selected.isEmpty()) {
                            done({ false, false,
                                tr("Multiple Aura devices found. Set AuraObjectPath first.") });
                            return;
                        }
                        selected = path;
                        props = p;
                    }
                    const auto power = decodeLightbarPower(props.value("LedPower"));
                    if (selected.isEmpty() || !power
                        || !qdbus_cast<QList<uint>>(props.value("SupportedPowerZones"))
                            .contains(LightbarZone)) {
                        done({ false, false, tr("No supported lightbar power settings.") });
                        return;
                    }
                    m_owner = service;
                    m_path = selected;
                    done({ true, power->awake, { } });
                });
        });
}
void AsusControls::writeLightbar(bool enabled, std::function<void(QString)> done)
{
    if (m_owner.isEmpty() || m_path.isEmpty()) {
        done(tr("Read lightbar settings before saving."));
        return;
    }
    // Read fresh flags and send only the Lightbar zone. asusd merges by zone.
    // Pin the unique daemon owner; never carry an old device state across a restart.
    const auto owner = m_owner;
    const auto path = m_path;
    auto call = QDBusMessage::createMethodCall(owner, path, "org.freedesktop.DBus.Properties", "Get");
    call << QString::fromLatin1(AuraInterface) << QStringLiteral("LedPower");
    // OK closes the module immediately; both calls must outlive the controls.
    auto* watcher = new QDBusPendingCallWatcher(
        QDBusConnection::systemBus().asyncCall(call, 1000), QCoreApplication::instance());
    connect(
        watcher, &QDBusPendingCallWatcher::finished, watcher, [owner, path, enabled, done](auto* watcher) {
            const QDBusPendingReply<QDBusVariant> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                done(reply.error().message());
                return;
            }
            auto power = decodeLightbarPower(reply.value().variant());
            if (!power) {
                done(tr("asusd returned no valid lightbar power state."));
                return;
            }
            if (power->awake == enabled) {
                done({ });
                return;
            }
            power->awake = enabled;
            auto call = QDBusMessage::createMethodCall(owner, path, "org.freedesktop.DBus.Properties", "Set");
            call << QString::fromLatin1(AuraInterface) << QStringLiteral("LedPower")
                 << QVariant::fromValue(QDBusVariant(QVariant::fromValue(AuraPower { { *power } })));
            auto* update = new QDBusPendingCallWatcher(
                QDBusConnection::systemBus().asyncCall(call, 1000), QCoreApplication::instance());
            connect(update, &QDBusPendingCallWatcher::finished, update, [done](auto* update) {
                const QDBusPendingReply<> result = *update;
                update->deleteLater();
                done(result.isError() ? result.error().message() : QString());
            });
        });
}
}

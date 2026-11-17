// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_trigger_source.h"
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>

namespace Ambient
{
namespace
{
    QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.read(512).trimmed() : QByteArray();
    }
    QDBusMessage method(
        const QString& service, const QString& path, const QString& interface, const QString& member)
    {
        auto message = QDBusMessage::createMethodCall(service, path, interface, member);
        message.setAutoStartService(false);
        return message;
    }
    QVariant argument(const QDBusMessage& reply)
    {
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
            return { };
        auto value = reply.arguments().first();
        if (value.metaType() == QMetaType::fromType<QDBusVariant>())
            value = qvariant_cast<QDBusVariant>(value).variant();
        return value;
    }
}
SystemTriggerSource::SystemTriggerSource(QObject* parent)
    : QObject(parent)
{
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &SystemTriggerSource::poll);
    m_systemOwners.setConnection(QDBusConnection::systemBus());
    m_systemOwners.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_sessionOwners.setConnection(QDBusConnection::sessionBus());
    m_sessionOwners.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    const auto invalidate = [this] {
        ++m_generation;
        m_pending = 0;
        publish("bluetooth", { });
        publish("touchpad", { });
        poll();
    };
    connect(&m_systemOwners, &QDBusServiceWatcher::serviceOwnerChanged, this, invalidate);
    connect(&m_sessionOwners, &QDBusServiceWatcher::serviceOwnerChanged, this, invalidate);
}
void SystemTriggerSource::configure(const QList<EffectPreset>& effects)
{
    QMap<QString, TriggerOptions> next;
    for (const auto& p : effects)
        if (QStringList { "power-profile", "display", "bluetooth", "network", "touchpad", "airplane",
                "keyboard-brightness", "dbus" }
                .contains(p.trigger.type))
            next.insert(p.id, p.trigger);
    if (next == m_options)
        return;
    ++m_generation;
    m_pending = 0;
    const auto needs = [&](const QString& type) {
        return std::any_of(next.cbegin(), next.cend(), [&](const auto& t) { return t.type == type; });
    };
    m_systemOwners.setWatchedServices(needs("bluetooth") ? QStringList { "org.bluez" } : QStringList());
    m_sessionOwners.setWatchedServices(needs("touchpad") ? QStringList { "org.kde.KWin" } : QStringList());
    m_options = next;
    m_observers.clear();
    bool polling = false;
    for (auto it = next.cbegin(); it != next.cend(); ++it) {
        Q_EMIT valueChanged(it.key(), { });
        auto t = it.value();
        if (t.type == "power-profile") {
            t.bus = "system";
            t.service = "org.freedesktop.UPower.PowerProfiles";
            t.path = "/org/freedesktop/UPower/PowerProfiles";
            t.interface = t.service;
            t.member = "ActiveProfile";
        } else if (t.type == "network") {
            t.bus = "system";
            t.service = "org.freedesktop.NetworkManager";
            t.path = "/org/freedesktop/NetworkManager";
            t.interface = t.service;
            t.member = "State";
        } else if (t.type != "dbus") {
            polling = true;
            continue;
        }
        auto observer = std::make_unique<DBusTriggerSource>(
            t, t.bus == "system" ? QDBusConnection::systemBus() : QDBusConnection::sessionBus());
        const auto id = it.key();
        connect(observer.get(), &DBusTriggerSource::valueChanged, this,
            [this, id](const QVariant& value) { Q_EMIT valueChanged(id, value); });
        connect(observer.get(), &DBusTriggerSource::triggered, this,
            [this, id](const QVariant& value) { Q_EMIT triggered(id, value); });
        m_observers.emplace(id, std::move(observer));
    }
    if (polling) {
        m_poll.start();
        poll();
    } else
        m_poll.stop();
}
void SystemTriggerSource::publish(const QString& type, const QVariant& value)
{
    for (auto it = m_options.cbegin(); it != m_options.cend(); ++it)
        if (it->type == type)
            Q_EMIT valueChanged(it.key(), value);
}
void SystemTriggerSource::call(
    const QDBusConnection& bus, QDBusMessage message, std::function<void(const QDBusMessage&)> done)
{
    ++m_pending;
    const auto generation = m_generation;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 750), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, watcher, generation, done = std::move(done)] {
            const auto reply = watcher->reply();
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            --m_pending;
            done(reply);
        });
}
void SystemTriggerSource::poll()
{
    if (m_pending || !m_poll.isActive())
        return;
    QSet<QString> types;
    for (const auto& t : m_options)
        types.insert(t.type);
    if (types.contains("display")) {
        int connected = 0, known = 0;
        for (const auto& name :
            QDir("/sys/class/drm").entryList({ "card*-*" }, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto status = read("/sys/class/drm/" + name + "/status");
            known += status == "connected" || status == "disconnected";
            connected += status == "connected";
        }
        publish("display", known ? QVariant(connected > 1) : QVariant());
    }
    if (types.contains("airplane")) {
        int known = 0;
        bool blocked = true, valid = true;
        for (const auto& name :
            QDir("/sys/class/rfkill").entryList({ "rfkill*" }, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto root = "/sys/class/rfkill/" + name + '/';
            const auto type = read(root + "type");
            if (!QList<QByteArray> { "wlan", "bluetooth", "wwan", "wimax" }.contains(type))
                continue;
            const auto soft = read(root + "soft"), hard = read(root + "hard");
            valid &= (soft == "0" || soft == "1") && (hard == "0" || hard == "1");
            blocked &= soft == "1" || hard == "1";
            ++known;
        }
        publish("airplane", known && valid ? QVariant(blocked) : QVariant());
    }
    if (types.contains("keyboard-brightness")) {
        double level = -1;
        for (const auto& name :
            QDir("/sys/class/leds").entryList({ "*kbd_backlight*" }, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto root = "/sys/class/leds/" + name + '/';
            bool a = false, b = false;
            const int current = read(root + "brightness").toInt(&a),
                      maximum = read(root + "max_brightness").toInt(&b);
            if (a && b && maximum > 0 && current >= 0)
                level = std::max(level, 100.0 * current / maximum);
        }
        publish("keyboard-brightness", level >= 0 ? QVariant(level) : QVariant());
    }
    if (types.contains("bluetooth")) {
        call(QDBusConnection::systemBus(),
            method("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects"),
            [this](const QDBusMessage& reply) {
                const auto value = argument(reply);
                if (!value.isValid()) {
                    publish("bluetooth", { });
                    return;
                }
                using Interfaces = QMap<QString, QVariantMap>;
                using Objects = QMap<QDBusObjectPath, Interfaces>;
                const auto objects = qdbus_cast<Objects>(value);
                bool found = false, powered = false;
                for (const auto& interfaces : objects) {
                    const auto props = interfaces.value("org.bluez.Adapter1");
                    if (props.contains("Powered")) {
                        found = true;
                        powered |= props.value("Powered").toBool();
                    }
                }
                publish("bluetooth", found ? QVariant(powered) : QVariant());
            });
    }
    if (types.contains("touchpad")) {
        auto query
            = method("org.kde.KWin", "/org/kde/KWin/InputDevice", "org.freedesktop.DBus.Properties", "Get");
        query << QStringLiteral("org.kde.KWin.InputDeviceManager") << QStringLiteral("devicesSysNames");
        call(QDBusConnection::sessionBus(), query, [this](const QDBusMessage& reply) {
            const auto names = qdbus_cast<QStringList>(argument(reply));
            struct Result {
                int pending;
                bool found = false, enabled = false, failed = false;
            };
            if (names.isEmpty() || names.size() > 128) {
                publish("touchpad", { });
                return;
            }
            auto result = std::make_shared<Result>();
            result->pending = names.size();
            for (const auto& name : names) {
                auto query = method("org.kde.KWin", "/org/kde/KWin/InputDevice/" + name,
                    "org.freedesktop.DBus.Properties", "GetAll");
                query << QStringLiteral("org.kde.KWin.InputDevice");
                call(QDBusConnection::sessionBus(), query, [this, result](const QDBusMessage& response) {
                    const auto value = argument(response);
                    result->failed |= !value.isValid();
                    const auto props = qdbus_cast<QVariantMap>(value);
                    if (props.value("touchpad").toBool() && props.contains("enabled")) {
                        result->found = true;
                        result->enabled |= props.value("enabled").toBool();
                    }
                    if (--result->pending == 0)
                        publish("touchpad",
                            result->found && !result->failed ? QVariant(result->enabled) : QVariant());
                });
            }
        });
    }
}
}

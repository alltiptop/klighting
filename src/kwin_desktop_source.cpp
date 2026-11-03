// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_desktop_source.h"
#include "lighting_ipc.h"
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <linux/input-event-codes.h>

namespace Ambient
{
namespace
{
    QDBusMessage method(const QString& owner, const char* path, const char* interface, const char* name)
    {
        auto call = QDBusMessage::createMethodCall(
            owner, QLatin1String(path), QLatin1String(interface), QLatin1String(name));
        call.setAutoStartService(false);
        return call;
    }
}
KWinDesktopSource::KWinDesktopSource(const QDBusConnection& bus, QObject* parent)
    : DesktopSource(parent)
    , m_bus(bus)
    , m_watcher(QStringLiteral("org.kde.KWin"), bus, QDBusServiceWatcher::WatchForOwnerChange)
    , m_trackedKeys((KEY_MAX + 8) / 8, '\0')
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString&, const QString&, const QString& owner) { ownerChanged(owner); });
    connect(&m_poll, &QTimer::timeout, this, &KWinDesktopSource::refresh);
    m_poll.start(2000);
    const int generation = m_generation;
    auto call
        = method("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    call << QStringLiteral("org.kde.KWin");
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation] {
        const QDBusPendingReply<QString> reply = *pending;
        pending->deleteLater();
        if (generation == m_generation)
            ownerChanged(reply.isError() ? QString() : reply.value());
    });
}
void KWinDesktopSource::ownerChanged(const QString& owner)
{
    if (!m_owner.isEmpty())
        m_bus.disconnect(m_owner, Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", this,
            SLOT(desktopState(QVariantMap)));
    ++m_generation;
    m_owner = owner;
    const bool wasAvailable = m_available;
    m_reading = m_available = m_triedIntegration = false;
    m_status = QStringLiteral("Desktop integration unavailable");
    if (owner.isEmpty() || wasAvailable)
        Q_EMIT unavailable();
    if (!owner.isEmpty()) {
        m_bus.connect(owner, Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", this,
            SLOT(desktopState(QVariantMap)));
        refresh();
    }
}
void KWinDesktopSource::refresh()
{
    if (m_owner.isEmpty() || m_reading)
        return;
    m_reading = true;
    const int generation = m_generation;
    auto* pending = new QDBusPendingCallWatcher(
        m_bus.asyncCall(method(m_owner, Ipc::DesktopPath, Ipc::DesktopInterface, "Snapshot"), 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation] {
        const QDBusPendingReply<QVariantMap> reply = *pending;
        pending->deleteLater();
        if (generation != m_generation)
            return;
        m_reading = false;
        if (!reply.isError() && reply.value().value("protocol").toInt() == Ipc::Version) {
            desktopState(reply.value());
            configure();
        } else {
            m_status = QStringLiteral("KWin desktop integration unavailable");
            m_available = false;
            Q_EMIT unavailable();
            if (!m_triedIntegration) {
                m_triedIntegration = true;
                auto load = method(m_owner, "/Plugins", "org.kde.KWin.Plugins", "LoadPlugin");
                load << QString::fromLatin1(Ipc::IntegrationPlugin);
                m_bus.asyncCall(load, 1500);
            }
        }
    });
}
void KWinDesktopSource::desktopState(const QVariantMap& state)
{
    if (state.value("protocol").toInt() != Ipc::Version)
        return;
    const auto keys = state.value("pressed").toByteArray();
    if (keys.size() != (KEY_MAX + 8) / 8 || state.value("windowClass").toString().size() > 256)
        return;
    ModifierState modifiers;
    for (int code = 1; code <= KEY_MAX; ++code)
        if (quint8(keys[code / 8]) & (1u << (code % 8)))
            modifiers.pressed.insert(code);
    const uint allowed = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier
        | Qt::GroupSwitchModifier;
    modifiers.effective = Qt::KeyboardModifiers(state.value("modifiers").toUInt() & allowed);
    modifiers.capsLock = state.value("capsLock").toBool();
    m_available = true;
    m_status = QStringLiteral("Connected to KWin desktop integration");
    Q_EMIT stateChanged(state.value("windowClass").toString(), modifiers, state.value("locked").toBool());
}
void KWinDesktopSource::setTrackedKeys(const QSet<quint32>& keys)
{
    m_trackedKeys.fill('\0');
    for (auto code : keys)
        if (code > 0 && code <= KEY_MAX)
            m_trackedKeys[code / 8] = char(quint8(m_trackedKeys[code / 8]) | (1u << (code % 8)));
    configure();
}
void KWinDesktopSource::configure()
{
    if (m_owner.isEmpty())
        return;
    auto call = method(m_owner, Ipc::DesktopPath, Ipc::DesktopInterface, "Configure");
    call << m_trackedKeys;
    m_bus.asyncCall(call, 1500);
}
}

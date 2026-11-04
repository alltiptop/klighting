// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_desktop_source.h"
#include "component_info.h"
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
    resetConfiguration();
    m_owner = owner;
    m_info.clear();
    const bool wasAvailable = m_available;
    m_reading = m_available = m_triedIntegration = false;
    m_loading = m_recovered = false;
    m_stable.invalidate();
    m_status = QStringLiteral("Desktop integration unavailable");
    if (owner.isEmpty() || wasAvailable)
        publish({ ProviderState::Unavailable });
    if (!owner.isEmpty()) {
        m_bus.connect(owner, Ipc::DesktopPath, Ipc::DesktopInterface, "StateChanged", this,
            SLOT(desktopState(QVariantMap)));
        refresh();
    }
}
void KWinDesktopSource::refresh()
{
    if (m_owner.isEmpty() || m_reading || m_loading)
        return;
    m_reading = true;
    const int generation = m_generation;
    const int revision = m_revision;
    auto* pending = new QDBusPendingCallWatcher(
        m_bus.asyncCall(method(m_owner, Ipc::DesktopPath, Ipc::DesktopInterface, "Snapshot"), 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation, revision] {
        const QDBusPendingReply<QVariantMap> reply = *pending;
        pending->deleteLater();
        if (generation != m_generation)
            return;
        m_reading = false;
        if (revision != m_revision) {
            configure(); // Use the newer signal state, but still retry an unacknowledged request.
            return;
        }
        if (reply.isError())
            m_info.clear();
        if (!reply.isError() && acceptState(reply.value())) {
            configure();
        } else {
            resetConfiguration();
            if (!reply.isError() && !compatibleComponent(Component::Desktop, reply.value())) {
                m_status = QStringLiteral(
                    "Incompatible KWin desktop integration; restart the session after updating");
                m_triedIntegration = true;
            } else if (m_available || !m_triedIntegration)
                m_status = QStringLiteral("KWin desktop integration unavailable");
            m_available = false;
            m_stable.invalidate();
            m_recovered = false;
            if (!m_triedIntegration)
                loadIntegration();
            publish({ ProviderState::Unavailable });
        }
    });
}
bool KWinDesktopSource::canRetry() const
{
    return !m_owner.isEmpty() && !m_available && m_triedIntegration && !m_loading;
}
QString KWinDesktopSource::retry()
{
    if (!canRetry())
        return QStringLiteral("Desktop integration cannot be retried right now");
    ++m_generation; // Ignore an outstanding snapshot from before this explicit retry.
    resetConfiguration();
    m_reading = false;
    return loadIntegration(true);
}
QString KWinDesktopSource::loadIntegration(bool manual)
{
    m_triedIntegration = true;
    const auto error = m_startup.prepare(m_owner, manual);
    if (!error.isEmpty()) {
        m_status = error;
        return error;
    }
    m_status = QStringLiteral("Loading KWin desktop integration");
    m_loading = true;
    const int generation = m_generation;
    const auto owner = m_owner;
    auto load = method(owner, "/Plugins", "org.kde.KWin.Plugins", "LoadPlugin");
    load << QString::fromLatin1(Ipc::IntegrationPlugin);
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(load, 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation, owner] {
        const QDBusPendingReply<bool> reply = *pending;
        pending->deleteLater();
        if (generation != m_generation)
            return;
        m_loading = false;
        m_startup.cancel(owner); // Expire an unused permit, including on timeout.
        if (m_available) {
            // A valid state signal can arrive before the load reply (or its timeout).
            configure();
            return;
        }
        if (reply.isError() || !reply.value()) {
            m_status
                = QStringLiteral("KWin could not load desktop integration; retry from Lighting settings");
            publish({ ProviderState::Unavailable });
        } else {
            m_status = QStringLiteral("Waiting for KWin desktop integration");
            refresh();
        }
    });
    return { };
}
void KWinDesktopSource::desktopState(const QVariantMap& state)
{
    ++m_revision;
    const bool wasAvailable = m_available;
    const auto instance = m_info.value("instanceId");
    if (acceptState(state)) {
        if (!wasAvailable || instance != m_info.value("instanceId"))
            configure();
    } else {
        resetConfiguration();
        m_available = false;
        m_stable.invalidate();
        m_recovered = false;
        m_status = QStringLiteral("Incompatible KWin desktop integration");
        publish({ ProviderState::Unavailable });
    }
}
bool KWinDesktopSource::acceptState(const QVariantMap& state)
{
    const auto info = identityInfo(state);
    if (m_info.value("instanceId") != info.value("instanceId")) {
        resetConfiguration();
        m_stable.invalidate();
        m_recovered = false;
    }
    m_info = info;
    if (!compatibleComponent(Component::Desktop, state))
        return false;
    const auto capabilities = qdbus_cast<QStringList>(state.value("capabilities"));
    const auto available = [&](const char* feature, const char* flag) {
        return (!state.contains("capabilities") || capabilities.contains(QLatin1String(feature)))
            && (!state.contains(QLatin1String(flag))
                || (state.value(QLatin1String(flag)).metaType().id() == QMetaType::Bool
                    && state.value(QLatin1String(flag)).toBool()));
    };
    DesktopState next { ProviderState::Available };
    const auto window = state.value("windowClass");
    if (available("window-class", "windowAvailable") && window.metaType().id() == QMetaType::QString
        && window.toString().size() <= 256)
        next.windowClass = window.toString();
    const auto keyData = state.value("pressed");
    const auto keys = keyData.toByteArray();
    bool validModifiers = false;
    const auto effective = state.value("modifiers").toUInt(&validModifiers);
    if (available("key-state", "keysAvailable") && keyData.metaType().id() == QMetaType::QByteArray
        && keys.size() == (KEY_MAX + 8) / 8 && validModifiers
        && state.value("capsLock").metaType().id() == QMetaType::Bool) {
        ModifierState modifiers;
        for (int code = 1; code <= KEY_MAX; ++code)
            if (quint8(keys[code / 8]) & (1u << (code % 8)))
                modifiers.pressed.insert(code);
        const uint allowed = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier
            | Qt::GroupSwitchModifier;
        modifiers.effective = Qt::KeyboardModifiers(effective & allowed);
        modifiers.capsLock = state.value("capsLock").toBool();
        next.modifiers = modifiers;
    }
    if (available("screen-lock", "lockAvailable") && state.value("locked").metaType().id() == QMetaType::Bool)
        next.locked = state.value("locked").toBool();
    if (!m_available || !m_stable.isValid())
        m_stable.start();
    m_available = true;
    m_status = QStringLiteral("Connected to KWin desktop integration");
    if (!m_recovered && m_stable.elapsed() >= IntegrationStartupGuard::RecoveryIntervalMs) {
        m_recovered = true;
        const auto error = m_startup.recovered(m_owner, m_stable.elapsed());
        if (!error.isEmpty())
            qWarning().noquote() << error;
    }
    publish(next);
    return true;
}
void KWinDesktopSource::setTrackedKeys(const QSet<quint32>& keys)
{
    QByteArray next(m_trackedKeys.size(), '\0');
    for (auto code : keys)
        if (code > 0 && code <= KEY_MAX)
            next[code / 8] = char(quint8(next[code / 8]) | (1u << (code % 8)));
    if (next == m_trackedKeys)
        return;
    m_trackedKeys = next;
    configure();
}
void KWinDesktopSource::configure()
{
    if (m_owner.isEmpty() || !m_available || m_configuring)
        return;
    if (!m_info.value("instanceId").toString().isEmpty() && m_configuredKeys == m_trackedKeys)
        return;
    m_configuring = true;
    const int epoch = m_configurationEpoch;
    const auto keys = m_trackedKeys;
    auto call = method(m_owner, Ipc::DesktopPath, Ipc::DesktopInterface, "Configure");
    call << keys;
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, epoch, keys] {
        const QDBusPendingReply<> reply = *pending;
        pending->deleteLater();
        if (epoch != m_configurationEpoch)
            return;
        m_configuring = false;
        m_configuredKeys = reply.isError() ? QByteArray() : keys;
        if (m_trackedKeys != keys)
            configure();
    });
}
void KWinDesktopSource::resetConfiguration()
{
    ++m_configurationEpoch;
    m_configuring = false;
    m_configuredKeys.clear();
}
}

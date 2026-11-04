// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_capture_source.h"
#include "component_info.h"
#include "effect_names.h"
#include "lighting_ipc.h"
#include "settings.h"
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <algorithm>

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
KWinCaptureSource::KWinCaptureSource(
    const std::shared_ptr<HardwareControlGuard>& guard, const QDBusConnection& bus, QObject* parent)
    : CaptureSource(parent)
    , m_bus(bus)
    , m_watcher(QStringLiteral("org.kde.KWin"), bus, QDBusServiceWatcher::WatchForOwnerChange)
    , m_guard(guard)
{
    Q_ASSERT(m_guard);
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString&, const QString&, const QString& owner) { ownerChanged(owner); });
    connect(&m_poll, &QTimer::timeout, this, &KWinCaptureSource::refresh);
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
KWinCaptureSource::~KWinCaptureSource()
{
    // Stop after any already queued Configure, even if its reply is still pending.
    if (!m_owner.isEmpty() && m_state.providerAvailable && m_guard->allowed()) {
        auto call = method(m_owner, Ipc::CapturePath, Ipc::CaptureInterface, "Configure");
        call << 0 << m_options;
        m_bus.asyncCall(call, 1500);
    }
}
void KWinCaptureSource::ownerChanged(const QString& owner)
{
    if (!m_owner.isEmpty()) {
        m_bus.disconnect(
            m_owner, Ipc::CapturePath, Ipc::CaptureInterface, "Sample", this, SLOT(screenSample(QByteArray)));
    }
    ++m_generation;
    resetConfiguration();
    m_owner = owner;
    m_reading = false;
    captureState(owner.isEmpty(),
        { .status = owner.isEmpty() ? QStringLiteral("KWin integration unavailable")
                                    : QStringLiteral("Checking KWin integration") });
    if (!owner.isEmpty()) {
        m_bus.connect(
            owner, Ipc::CapturePath, Ipc::CaptureInterface, "Sample", this, SLOT(screenSample(QByteArray)));
        refresh();
    }
}
void KWinCaptureSource::refresh()
{
    if (m_owner.isEmpty() || m_reading)
        return;
    m_reading = true;
    const int generation = m_generation;
    auto call = method(m_owner, "/Effects", "org.freedesktop.DBus.Properties", "Get");
    call << QStringLiteral("org.kde.kwin.Effects") << QStringLiteral("loadedEffects");
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation] {
        const QDBusPendingReply<QDBusVariant> reply = *pending;
        pending->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            m_reading = false;
            captureState(false, { .status = QStringLiteral("Cannot check for legacy lighting controllers") });
            return;
        }
        QStringList lighting;
        for (const auto& name : qdbus_cast<QStringList>(reply.value().variant()))
            if (isAmbientEffectName(name))
                lighting.append(name);
        if (lighting.isEmpty()) {
            m_reading = false;
            captureState(true, { .status = QStringLiteral("Ambient capture disabled") });
        } else if (lighting != QStringList { QStringLiteral("ambientled") }) {
            m_reading = false;
            captureState(false,
                { .status = QStringLiteral("Unload old ambientled effects before starting klightingd") });
        } else {
            auto* probe = new QDBusPendingCallWatcher(
                m_bus.asyncCall(method(m_owner, Ipc::CapturePath, Ipc::CaptureInterface, "Snapshot"), 1500),
                this);
            connect(probe, &QDBusPendingCallWatcher::finished, this, [this, probe, generation] {
                const QDBusPendingReply<QVariantMap> result = *probe;
                probe->deleteLater();
                if (generation != m_generation)
                    return;
                m_reading = false;
                const bool compatible
                    = !result.isError() && compatibleComponent(Component::Capture, result.value());
                const auto snapshot = result.value();
                CaptureState state;
                state.info = result.isError() ? QVariantMap() : identityInfo(snapshot);
                state.providerAvailable = compatible && snapshot.value("providerAvailable", true).toBool();
                state.screenAvailable = state.providerAvailable
                    && snapshot.value("screenAvailable", snapshot.value("available")).toBool();
                state.enabled = state.providerAvailable && snapshot.value("enabled", m_fps > 0).toBool();
                state.ready
                    = state.screenAvailable && snapshot.value("ready", snapshot.value("available")).toBool();
                state.status = compatible
                    ? snapshot.value("status").toString()
                    : QStringLiteral(
                          "Old ambientled is still loaded; unload it to let klightingd control lighting");
                captureState(compatible, state);
            });
        }
    });
}
void KWinCaptureSource::screenSample(const QByteArray& rgba)
{
    if (m_state.ready && m_guard->allowed() && m_fps > 0 && rgba.size() == Ipc::SampleBytes)
        Q_EMIT sample(rgba);
}
void KWinCaptureSource::captureState(bool safe, const CaptureState& state)
{
    if (!safe || !state.providerAvailable
        || m_state.info.value("instanceId") != state.info.value("instanceId"))
        resetConfiguration();
    const bool changed = m_state != state;
    m_state = state;
    m_guard->setState(safe, safe ? QString() : state.status);
    if (changed)
        Q_EMIT this->changed();
    configureCapture();
}
void KWinCaptureSource::requestCapture(int fps, const QVariantMap& options)
{
    fps = std::clamp(fps, 0, MaxFramesPerSecond);
    if (m_fps == fps && m_options == options)
        return;
    m_fps = fps;
    m_options = options;
    configureCapture();
}
void KWinCaptureSource::configureCapture()
{
    if (m_owner.isEmpty() || !m_state.providerAvailable || !m_guard->allowed() || m_configuring)
        return;
    // Original protocol-1 providers lack instance IDs; retain polling recovery for them.
    if (!m_state.info.value("instanceId").toString().isEmpty() && m_configuredFps == m_fps
        && m_configuredOptions == m_options)
        return;
    m_configuring = true;
    const int epoch = m_configurationEpoch, fps = m_fps;
    const auto options = m_options;
    auto call = method(m_owner, Ipc::CapturePath, Ipc::CaptureInterface, "Configure");
    call << fps << options;
    auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, epoch, fps, options] {
        const QDBusPendingReply<> reply = *pending;
        pending->deleteLater();
        if (epoch != m_configurationEpoch)
            return;
        m_configuring = false;
        m_configuredFps = reply.isError() ? -1 : fps;
        if (!reply.isError())
            m_configuredOptions = options;
        // Send a newer request immediately; otherwise retry errors at the next poll.
        if (m_fps != fps || m_options != options)
            configureCapture();
    });
}
void KWinCaptureSource::resetConfiguration()
{
    ++m_configurationEpoch;
    m_configuring = false;
    m_configuredFps = -1;
    m_configuredOptions.clear();
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_capture_source.h"
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
KWinCaptureSource::KWinCaptureSource(const QDBusConnection& bus, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_watcher(QStringLiteral("org.kde.KWin"), bus, QDBusServiceWatcher::WatchForOwnerChange)
{
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
    m_fps = 0;
    configureCapture();
}
void KWinCaptureSource::ownerChanged(const QString& owner)
{
    if (!m_owner.isEmpty()) {
        m_bus.disconnect(
            m_owner, Ipc::CapturePath, Ipc::CaptureInterface, "Sample", this, SLOT(screenSample(QByteArray)));
    }
    ++m_generation;
    m_owner = owner;
    m_reading = false;
    captureState(owner.isEmpty(), false,
        owner.isEmpty() ? QStringLiteral("KWin integration unavailable")
                        : QStringLiteral("Checking KWin integration"));
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
            captureState(false, false, QStringLiteral("Cannot check for legacy lighting controllers"));
            return;
        }
        QStringList lighting;
        for (const auto& name : qdbus_cast<QStringList>(reply.value().variant()))
            if (isAmbientEffectName(name))
                lighting.append(name);
        if (lighting.isEmpty()) {
            m_reading = false;
            captureState(true, false, QStringLiteral("Ambient capture disabled"));
        } else if (lighting != QStringList { QStringLiteral("ambientled") }) {
            m_reading = false;
            captureState(
                false, false, QStringLiteral("Unload old ambientled effects before starting klightingd"));
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
                    = !result.isError() && result.value().value("protocol").toInt() == Ipc::Version;
                captureState(compatible, compatible && result.value().value("available").toBool(),
                    compatible ? result.value().value("status").toString()
                               : QStringLiteral("Old ambientled is still loaded; unload it to let klightingd "
                                                "control lighting"));
            });
        }
    });
}
void KWinCaptureSource::screenSample(const QByteArray& rgba)
{
    if (m_capture && m_safe && m_fps > 0 && rgba.size() == Ipc::SampleBytes)
        Q_EMIT sample(rgba);
}
void KWinCaptureSource::captureState(bool safe, bool available, const QString& status)
{
    const bool changed = m_safe != safe || m_capture != available || m_status != status;
    m_safe = safe;
    m_capture = available;
    m_status = status;
    if (changed) {
        Q_EMIT this->changed();
    }
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
    if (m_owner.isEmpty() || !m_capture || !m_safe)
        return;
    auto call = method(m_owner, Ipc::CapturePath, Ipc::CaptureInterface, "Configure");
    call << m_fps << m_options;
    m_bus.asyncCall(call, 1500);
}
}

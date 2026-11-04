// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture_channel.h"
#include "component_info.h"
#include "lighting_ipc.h"
#include "settings.h"
#include <QDBusMessage>
#include <QUuid>
#include <algorithm>

namespace Ambient
{
CaptureChannel::CaptureChannel(QObject* parent)
    : QObject(parent)
    , m_instance(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_registered = QDBusConnection::sessionBus().registerObject(Ipc::CapturePath, this,
        QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
    if (!m_registered)
        m_error = QStringLiteral("Another Ambient capture provider is registered");
    connect(&m_peer, &DaemonPeer::changed, this, [this] {
        m_enabled = false;
        Q_EMIT configured(0, { });
    });
}
CaptureChannel::~CaptureChannel()
{
    if (m_registered)
        QDBusConnection::sessionBus().unregisterObject(Ipc::CapturePath);
}
QVariantMap CaptureChannel::Snapshot() const
{
    auto state = componentInfo(Component::Capture);
    state.insert("instanceId", m_instance);
    state.insert("providerAvailable", m_registered);
    state.insert("screenAvailable", m_screenAvailable);
    state.insert("enabled", m_enabled);
    state.insert("ready", m_registered && m_screenAvailable && !m_locked && m_error.isEmpty());
    // Preserve the original protocol-1 health flag for older daemons; it must
    // not prevent Configure when sampling is stopped or the selected screen is absent.
    state.insert("available", m_registered && m_error.isEmpty());
    state.insert("status",
        !m_error.isEmpty()       ? m_error
            : !m_screenAvailable ? QStringLiteral("Selected screen is unavailable")
            : m_locked           ? QStringLiteral("Screen capture paused while locked")
            : !m_enabled         ? QStringLiteral("Ambient capture is stopped")
                                 : QStringLiteral("Ambient capture available"));
    return state;
}
void CaptureChannel::Configure(int fps, const QVariantMap& options)
{
    if (!calledFromDBus())
        return;
    if (!m_registered || !m_peer.isOwner(message().service())) {
        sendErrorReply(
            QDBusError::AccessDenied, QStringLiteral("Only the lighting service may configure capture"));
        return;
    }
    Q_EMIT configured(std::clamp(fps, 0, MaxFramesPerSecond), options);
}
void CaptureChannel::sendSample(const QByteArray& rgba)
{
    if (m_registered && rgba.size() == Ipc::SampleBytes)
        m_peer.send(Ipc::CapturePath, Ipc::CaptureInterface, "Sample", { rgba });
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture_channel.h"
#include "lighting_ipc.h"
#include "settings.h"
#include <QDBusMessage>
#include <algorithm>

namespace Ambient
{
CaptureChannel::CaptureChannel(QObject* parent)
    : QObject(parent)
{
    m_registered = QDBusConnection::sessionBus().registerObject(Ipc::CapturePath, this,
        QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
    if (!m_registered)
        m_error = QStringLiteral("Another Ambient capture provider is registered");
    connect(&m_peer, &DaemonPeer::changed, this, [this] { Q_EMIT configured(0, { }); });
}
CaptureChannel::~CaptureChannel()
{
    if (m_registered)
        QDBusConnection::sessionBus().unregisterObject(Ipc::CapturePath);
}
QVariantMap CaptureChannel::Snapshot() const
{
    return { { "protocol", Ipc::Version }, { "available", m_registered && m_error.isEmpty() },
        { "status", m_error.isEmpty() ? QStringLiteral("Ambient capture available") : m_error } };
}
void CaptureChannel::Configure(int fps, const QVariantMap& options)
{
    if (!m_registered || !calledFromDBus() || !m_peer.isOwner(message().service()))
        return;
    Q_EMIT configured(std::clamp(fps, 0, MaxFramesPerSecond), options);
}
void CaptureChannel::sendSample(const QByteArray& rgba)
{
    if (m_registered && rgba.size() == Ipc::SampleBytes)
        m_peer.send(Ipc::CapturePath, Ipc::CaptureInterface, "Sample", { rgba });
}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "service_control.h"
#include "lighting_ipc.h"
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace Ambient
{
ServiceControl::ServiceControl(QObject* parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &ServiceControl::refresh);
    m_timer.start(2000);
    refresh();
}
void ServiceControl::refresh()
{
    if (m_busy || m_reading)
        return;
    m_reading = true;
    const int generation = m_generation;
    auto call = QDBusMessage::createMethodCall(Ipc::Service, Ipc::Path, Ipc::Interface, "Status");
    call.setAutoStartService(false);
    auto* pending = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1500), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending, generation] {
        const QDBusPendingReply<QVariantMap> reply = *pending;
        pending->deleteLater();
        m_reading = false;
        if (generation != m_generation)
            return;
        m_running = !reply.isError() && reply.value().value("protocol").toInt() == Ipc::Version;
        if (!m_running) {
            m_status = tr("Lighting service is stopped. Apply settings to start it.");
        } else if (!reply.value().value("error").toString().isEmpty()) {
            m_status = reply.value().value("error").toString();
        } else if (!reply.value().value("safeToControl").toBool()) {
            m_status = tr("Lighting is paused: %1").arg(reply.value().value("capture").toString());
        } else {
            m_status = tr("Lighting service is running.");
        }
        Q_EMIT changed();
    });
}
void ServiceControl::reload()
{
    if (m_busy)
        return;
    ++m_generation;
    m_busy = true;
    m_error.clear();
    Q_EMIT changed();
    auto call = QDBusMessage::createMethodCall(Ipc::Service, Ipc::Path, Ipc::Interface, "Reload");
    // Apply is the explicit activation boundary; merely opening settings never starts hardware I/O.
    auto* pending = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 10000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending] {
        const QDBusPendingReply<QString> reply = *pending;
        pending->deleteLater();
        m_busy = false;
        const auto error = reply.isError() ? reply.error().message() : reply.value();
        if (!error.isEmpty())
            m_status = m_error
                = tr("Settings saved, but the lighting service could not apply them: %1").arg(error);
        Q_EMIT changed();
        Q_EMIT finished();
        if (error.isEmpty())
            refresh();
    });
}
}

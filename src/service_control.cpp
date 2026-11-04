// SPDX-License-Identifier: GPL-3.0-or-later
#include "service_control.h"
#include "lighting_ipc.h"
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace Ambient
{
ServiceControl::ServiceControl(QObject* parent)
    : ServiceControl(installationPaths(), parent)
{
}
ServiceControl::ServiceControl(const InstallationPaths& paths, QObject* parent)
    : QObject(parent)
    , m_paths(paths)
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
        m_running = !reply.isError();
        m_compatible = m_running && compatibleComponent(Component::Service, reply.value());
        m_desktopCanRetry = m_compatible && reply.value().value("desktopCanRetry").toBool();
        updateDiagnostics(m_running ? reply.value() : QVariantMap());
        m_microphoneStatus.clear();
        if (m_compatible) {
            const auto status = reply.value();
            for (const auto* field :
                { "Backend", "Available", "MuteKnown", "Muted", "SourceName", "Description", "Reason" }) {
                auto key = QString::fromLatin1(field);
                key[0] = key[0].toLower();
                m_microphoneStatus.insert(
                    key, status.value(QStringLiteral("microphone") + QLatin1String(field)));
            }
        }
        if (!m_running) {
            m_status = tr("Lighting service is stopped. Apply settings to start it.");
        } else if (!m_compatible) {
            m_status = tr("Lighting service is running with an incompatible protocol or capabilities. "
                          "Restart it after updating.");
        } else if (!reply.value().value("error").toString().isEmpty()) {
            m_status = reply.value().value("error").toString();
        } else if (!reply.value().value("safeToControl").toBool()) {
            m_status
                = tr("Lighting is paused: %1")
                      .arg(reply.value().value("controlStatus", reply.value().value("capture")).toString());
        } else if (m_desktopCanRetry) {
            m_status = reply.value().value("desktop").toString();
        } else {
            m_status = tr("Lighting service is running.");
        }
        Q_EMIT changed();
    });
}
void ServiceControl::updateDiagnostics(const QVariantMap& status)
{
    QStringList messages;
    const auto compare = [&](Component component, const QString& name, const QString& path,
                             const QVariantMap& running, const QString& action) {
        if (running.isEmpty())
            return;
        if (component != Component::Service && !compatibleComponent(component, running))
            messages.append(tr("%1 has an incompatible protocol or capabilities. %2").arg(name, action));
        const auto installed = installedComponentInfo(path, component != Component::Service);
        const auto installedId = installed.value("buildId").toString();
        const auto runningId = identityInfo(running).value("buildId").toString();
        if (installedId.isEmpty())
            return; // Unknown installation is not evidence of a stale process.
        if (runningId.isEmpty())
            messages.append(tr("%1 does not report its build. %2").arg(name, action));
        else if (installedId != runningId)
            messages.append(tr("%1 is using build %2; installed build is %3. %4")
                    .arg(name, runningId.left(12), installedId.left(12), action));
    };
    compare(Component::Service, QStringLiteral("klightingd"), m_paths.service, identityInfo(status),
        tr("Restart the lighting service to use the installed build."));
    const auto session = tr("Log out and back in to use the installed KWin plugin.");
    compare(Component::Desktop, QStringLiteral("klighting_integration"), m_paths.desktop,
        qdbus_cast<QVariantMap>(status.value("desktopInfo")), session);
    compare(Component::Capture, QStringLiteral("ambientled"), m_paths.capture,
        qdbus_cast<QVariantMap>(status.value("captureInfo")), session);
    m_diagnostics = messages.join('\n');
}
void ServiceControl::retryDesktopIntegration()
{
    if (m_busy || !m_desktopCanRetry)
        return;
    ++m_generation;
    m_busy = true;
    Q_EMIT changed();
    auto call
        = QDBusMessage::createMethodCall(Ipc::Service, Ipc::Path, Ipc::Interface, "RetryDesktopIntegration");
    call.setAutoStartService(false);
    auto* pending = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 3000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this, pending] {
        const QDBusPendingReply<QString> reply = *pending;
        pending->deleteLater();
        m_busy = false;
        const auto error = reply.isError() ? reply.error().message() : reply.value();
        if (!error.isEmpty())
            m_status = tr("Could not retry desktop integration: %1").arg(error);
        Q_EMIT changed();
        if (error.isEmpty())
            refresh();
    });
}
void ServiceControl::reload()
{
    if (m_busy)
        return;
    if (m_running && !m_compatible) {
        m_error = m_status;
        Q_EMIT finished();
        return;
    }
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

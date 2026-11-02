// SPDX-License-Identifier: GPL-3.0-or-later
#include "effect_control.h"
#include "effect_names.h"
#include <KConfigGroup>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace Ambient
{
namespace
{
    QDBusMessage method(const QString& name)
    {
        return QDBusMessage::createMethodCall("org.kde.KWin", "/Effects", "org.kde.kwin.Effects", name);
    }
}
EffectControl::EffectControl(QObject* parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig("kwinrc"))
{
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &EffectControl::refresh);
    m_timer.start();
    refresh();
}
void EffectControl::setEnabled(bool enabled)
{
    if (m_busy || !available() || m_enabled == enabled)
        return;
    m_error.clear();
    m_enabled = enabled;
    m_pending = m_enabled != m_saved || m_enabled != m_loaded;
    Q_EMIT changed();
}
void EffectControl::reset()
{
    if (m_busy)
        return;
    m_pending = false;
    m_error.clear();
    refresh();
}
void EffectControl::refresh()
{
    if (m_busy || m_reading)
        return;
    m_reading = true;
    auto call = QDBusMessage::createMethodCall(
        "org.kde.KWin", "/Effects", "org.freedesktop.DBus.Properties", "GetAll");
    call << QStringLiteral("org.kde.kwin.Effects");
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1500), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](auto* watcher) {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        m_reading = false;
        if (m_busy)
            return;
        if (reply.isError()) {
            m_available = m_loaded = false;
            m_status = tr("KWin is unavailable: %1").arg(reply.error().message());
        } else {
            QStringList installed, loaded;
            for (const auto& name : qdbus_cast<QStringList>(reply.value().value("listOfEffects")))
                if (isAmbientEffectName(name))
                    installed << name;
            for (const auto& name : qdbus_cast<QStringList>(reply.value().value("loadedEffects")))
                if (isAmbientEffectName(name))
                    loaded << name;
            if (installed.size() != 1 || loaded.size() > 1) {
                m_available = m_loaded = false;
                m_status = installed.isEmpty() ? tr("Install the Ambient LED effect to enable lighting.")
                                               : tr("Multiple or outdated Ambient LED effects were found. "
                                                    "Reload the installed effect first.");
            } else {
                const auto name = installed.first();
                if (name != m_name)
                    m_pending = false;
                m_name = name;
                m_available = true;
                m_loadedName = loaded.value(0);
                m_loaded = !m_loadedName.isEmpty();
                m_config->reparseConfiguration();
                const bool saved = KConfigGroup(m_config, "Plugins").readEntry(name + "Enabled", false);
                if (!m_pending) {
                    m_saved = saved;
                    m_enabled = m_loaded;
                }
                m_status = m_loaded ? tr("Ambient LED is running.") : tr("Ambient LED is stopped.");
                if (!m_pending && saved != m_loaded)
                    m_status += tr(" The saved setting differs from the running state. Toggle the checkbox "
                                   "and apply to synchronize it.");
            }
        }
        if (m_pending && !m_error.isEmpty())
            m_status = m_error;
        Q_EMIT changed();
    });
}
void EffectControl::apply()
{
    if (m_busy || !m_pending)
        return;
    if (!available()) {
        complete(tr("The Ambient LED effect is unavailable."));
        return;
    }
    m_config->reparseConfiguration();
    KConfigGroup group(m_config, "Plugins");
    if (group.readEntry(m_name + "Enabled", false) != m_saved) {
        complete(tr("The effect setting changed elsewhere. Reset before applying."));
        return;
    }
    m_busy = true;
    m_status = tr("Applying the effect setting…");
    Q_EMIT changed();
    const bool requested = m_enabled;
    // Persist the same flag as Desktop Effects. Direct D-Bus loading alone is temporary.
    group.writeEntry(m_name + "Enabled", requested, KConfig::Notify);
    if (!group.sync()) {
        complete(tr("Could not save the KWin effect setting."));
        return;
    }
    m_saved = requested;
    if (!m_loadedName.isEmpty() && (!requested || m_loadedName != m_name))
        changeRunningState(m_loadedName, false, requested);
    else
        changeRunningState(m_name, requested);
}
void EffectControl::changeRunningState(const QString& name, bool requested, bool enableAfterUnload)
{
    auto call = method(requested ? "loadEffect" : "unloadEffect");
    call << name;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, name, requested, enableAfterUnload](auto* watcher) {
            const QDBusPendingReply<> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                complete(tr("Could not apply the effect setting: %1").arg(reply.error().message()));
                return;
            }
            verify(name, requested, enableAfterUnload);
        });
}
void EffectControl::verify(const QString& name, bool requested, bool enableAfterUnload)
{
    auto call = method("isEffectLoaded");
    call << name;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1500), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, name, requested, enableAfterUnload](auto* watcher) {
            const QDBusPendingReply<bool> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                complete(tr("Could not verify the running effect."));
                return;
            }
            m_loaded = reply.value();
            m_loadedName = m_loaded ? name : QString();
            if (!m_loaded && !requested && enableAfterUnload) {
                // Confirm the old alias is unloaded before starting the installed build.
                changeRunningState(m_name, true);
                return;
            }
            complete(m_loaded == requested ? QString()
                                           : tr("KWin could not change the effect state. Check that the "
                                                "effect supports this session."));
        });
}
void EffectControl::complete(const QString& error)
{
    m_busy = false;
    m_error = error;
    m_pending = !error.isEmpty();
    m_status = error.isEmpty() ? (m_loaded ? tr("Ambient LED is running.") : tr("Ambient LED is stopped."))
                               : error;
    Q_EMIT changed();
    Q_EMIT finished();
}
}

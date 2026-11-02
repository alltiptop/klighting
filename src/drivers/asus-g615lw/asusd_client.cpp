// SPDX-License-Identifier: GPL-3.0-or-later
#include "asusd_client.h"
#include "g615lw_layout.h"
#include "lamp_array.h"
#include "settings.h"
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(ambientAsusd, "kwin.effect.ambientled.asusd")
namespace Ambient
{
AsusdClient::AsusdClient(
    QObject* parent, const QDBusConnection& bus, QString service, std::unique_ptr<LedTransport> transport)
    : QObject(parent)
    , m_transport(transport ? std::move(transport) : std::make_unique<LampArrayTransport>())
    , m_bus(bus)
    , m_service(std::move(service))
{
    registerAuraTypes();
    connect(m_transport.get(), &LedTransport::finished, this, &AsusdClient::transferFinished);
    m_retry.setInterval(5000);
    connect(&m_retry, &QTimer::timeout, this, &AsusdClient::discover);
    m_sendTimer.setSingleShot(true);
    m_sendTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sendTimer, &QTimer::timeout, this, &AsusdClient::pump);
    auto* watcher = new QDBusServiceWatcher(m_service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        forget(QStringLiteral("asusd owner changed"));
        discover();
    });
}
AsusdClient::~AsusdClient()
{
    disconnect(m_transport.get(), nullptr, this, nullptr);
    m_transport->shutdown(); // Release USB before returning control to asusd.
    // Best effort on plugin unload, addressed to this exact daemon owner. No wait
    // and no callback into code that KWin is about to unload. Earlier messages
    // on this connection precede the restore message.
    if (m_touched && m_brightness > 0 && !m_restorePacket.isEmpty() && !m_owner.isEmpty()) {
        auto call = QDBusMessage::createMethodCall(m_owner, m_path, AuraInterface, "DirectAddressingRaw");
        call << QVariant::fromValue(Packets { m_restorePacket });
        m_bus.asyncCall(call, 1000);
        call.setArguments({ QVariant::fromValue(Packets { setPacket() }) });
        m_bus.asyncCall(call, 1000);
    }
}
void AsusdClient::start(const QString& preferredPath)
{
    // Called once; target changes take effect after unloading/reloading the effect.
    m_preferredPath = preferredPath;
    m_retry.start();
    discover();
}
bool AsusdClient::ready() const
{
    return m_enabled && !m_path.isEmpty() && m_brightness > 0 && (m_keyboard || m_lightbar);
}
void AsusdClient::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_pending.reset();
    if (!enabled)
        restore();
    Q_EMIT readinessChanged();
}
void AsusdClient::setLedFramesPerSecond(int framesPerSecond)
{
    m_ledIntervalMs = ledIntervalMs(framesPerSecond);
    m_sendTimer.stop();
    pump();
}
void AsusdClient::forget(const QString& reason)
{
    if (!m_path.isEmpty())
        m_bus.disconnect(m_owner, m_path, "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
            SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    ++m_generation;
    m_owner.clear();
    m_path.clear();
    m_restorePacket.clear();
    m_lastFrame.reset();
    m_pending.reset();
    m_sendTimer.stop();
    m_busy = false;
    m_discovering = false;
    m_touched = false;
    m_restorePending = false;
    m_brightness = 0;
    ++m_operation;
    m_releasePending = m_usbTouched;
    pump();
    if (m_status != reason)
        qCWarning(ambientAsusd) << reason;
    m_status = reason;
    Q_EMIT readinessChanged();
}
void AsusdClient::discover()
{
    if (!m_path.isEmpty() || m_discovering)
        return;
    m_discovering = true;
    const auto generation = m_generation;
    // Resolve the unique owner explicitly. Qt's local-call optimization may
    // leave QDBusMessage::service() empty in method replies.
    auto call = QDBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    call << m_service;
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](auto* watcher) {
        QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            forget(QStringLiteral("asusd unavailable: ") + reply.error().message());
            return;
        }
        discoverObjects(reply.value(), generation);
    });
}
void AsusdClient::discoverObjects(const QString& owner, quint64 generation)
{
    auto call = QDBusMessage::createMethodCall(
        owner, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation, owner](auto* watcher) {
        QDBusPendingReply<ManagedObjects> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_discovering = false;
        if (reply.isError()) {
            forget(QStringLiteral("Aura discovery: ") + reply.error().message());
            return;
        }
        const auto objects = reply.value();
        QString selected;
        QVariantMap props;
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            const auto p = it.value().value(AuraInterface);
            const auto path = it.key().path();
            if ((!m_preferredPath.isEmpty() && path != m_preferredPath) || !compatibleAura(path, p))
                continue;
            if (!selected.isEmpty()) {
                forget(QStringLiteral("Multiple compatible Aura devices; set AuraObjectPath"));
                return;
            }
            selected = path;
            props = p;
        }
        if (selected.isEmpty()) {
            forget(QStringLiteral("No compatible whole-device 19b6 Aura controller"));
            return;
        }
        m_owner = owner;
        m_path = selected;
        m_restorePacket = *effectPacket(*decodeEffect(props.value("LedModeData")));
        m_brightness = props.value("Brightness").toUInt();
        if (!readPower(props.value("LedPower"))) {
            forget(QStringLiteral("Missing or invalid keyboard/lightbar power settings"));
            return;
        }
        m_bus.connect(m_owner, m_path, "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
            SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
        m_status = QStringLiteral("Connected to ") + m_path;
        qCInfo(ambientAsusd) << m_status;
        Q_EMIT readinessChanged();
    });
}
void AsusdClient::propertiesChanged(
    const QString& interface, const QVariantMap& changed, const QStringList& invalidated)
{
    if (interface != QLatin1String(AuraInterface))
        return;
    if (invalidated.contains("Brightness") || invalidated.contains("LedModeData")
        || invalidated.contains("LedPower")) {
        forget(QStringLiteral("Aura properties invalidated; rediscovering"));
        discover();
        return;
    }
    if (changed.contains("LedModeData")) {
        const auto effect = decodeEffect(changed.value("LedModeData"));
        if (!effect) {
            forget(QStringLiteral("Unsupported Aura mode selected"));
            return;
        }
        m_restorePacket = *effectPacket(*effect); // Preserve the user's latest factory effect.
        restore();
    }
    if (changed.contains("Brightness")) {
        m_brightness = changed.value("Brightness").toUInt();
        m_pending.reset();
        m_lastFrame.reset();
        if (m_brightness == 0)
            restore();
        Q_EMIT readinessChanged();
    }
    if (changed.contains("LedPower")) {
        if (!readPower(changed.value("LedPower"))) {
            forget(QStringLiteral("Invalid keyboard/lightbar power settings"));
            return;
        }
        m_pending.reset();
        m_lastFrame.reset();
        if (!m_keyboard && !m_lightbar)
            restore();
        Q_EMIT readinessChanged();
    }
}
bool AsusdClient::readPower(const QVariant& value)
{
    const auto keyboard = decodeZonePower(value, 1);
    const auto lightbar = decodeZonePower(value, LightbarZone);
    if (!keyboard || !lightbar)
        return false;
    m_keyboard = keyboard->awake;
    m_lightbar = lightbar->awake;
    return true;
}
void AsusdClient::submit(
    const QColor& color, const QList<QColor>& keyColors, const QList<QColor>& stripColors)
{
    if (!ready() || !color.isValid())
        return;
    // Public strip order follows the chassis; USB reports use hardware lamp IDs.
    QList<QColor> hardware;
    if (!stripColors.isEmpty()) {
        if (stripColors.size() != 12)
            return;
        hardware = QList<QColor>(12, color);
        const auto& order = StripLampOrder;
        for (int i = 0; i < 12; ++i)
            hardware[order[i] - 88] = stripColors[i];
    }
    m_pending = LedFrame { color, m_brightness, m_keyboard, m_lightbar, keyColors, hardware };
    pump();
}
void AsusdClient::restore()
{
    m_pending.reset();
    m_releasePending = m_usbTouched;
    m_restorePending = m_touched;
    pump();
}
void AsusdClient::pump()
{
    if (m_busy)
        return;
    if (m_releasePending) {
        m_releasePending = false;
        m_busy = m_releasing = true;
        m_transport->release(++m_operation);
        return;
    }
    if (m_path.isEmpty())
        return;
    if (m_restorePending) {
        m_restorePending = false;
        if (m_brightness > 0) {
            sendRestore();
            return;
        }
        m_touched = false; // Never let asusd's raw method turn Off back to Med.
    }
    if (!ready() || !m_pending)
        return;
    if (m_lastWrite.isValid() && m_lastWrite.elapsed() < m_ledIntervalMs) {
        m_sendTimer.start(m_ledIntervalMs - int(m_lastWrite.elapsed()));
        return;
    }
    const auto frame = *m_pending;
    m_pending.reset();
    if (m_lastFrame == frame)
        return;
    m_busy = m_usbTouched = m_touched = true;
    m_releasing = false;
    m_lastFrame = frame;
    m_transport->submit(frame, ++m_operation);
}
void AsusdClient::transferFinished(quint64 operation, const QString& error)
{
    if (operation != m_operation)
        return;
    m_busy = false;
    if (m_releasing) {
        m_usbTouched = false;
        m_lastFrame.reset();
    } else {
        m_lastWrite.start(); // Leave the configured interval after completion, even on a slow bus.
    }
    if (!error.isEmpty()) {
        forget(QStringLiteral("LampArray USB failed: ") + error);
        return;
    }
    if (!m_releasing)
        ++m_completedFrames;
    pump();
}
void AsusdClient::sendRestore(bool setting)
{
    const auto generation = m_generation;
    m_busy = true;
    auto call = QDBusMessage::createMethodCall(m_owner, m_path, AuraInterface, "DirectAddressingRaw");
    call << QVariant::fromValue(Packets { setting ? setPacket() : m_restorePacket });
    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation, setting](auto* watcher) {
        QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            forget(QStringLiteral("Aura restore failed: ") + reply.error().message());
            return;
        }
        if (!setting && m_brightness > 0) {
            sendRestore(true);
            return;
        }
        m_busy = false;
        m_touched = false;
        m_lastFrame.reset();
        pump();
    });
}
}

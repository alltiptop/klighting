// SPDX-License-Identifier: GPL-3.0-or-later
#include "tuf_backend.h"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <algorithm>

namespace Ambient
{
namespace Tuf
{
    QByteArray rawPacket(const QColor& color)
    {
        // Legacy asusd maps RGB at offsets 9..11 to transient sysfs [0,0,R,G,B,0].
        QByteArray packet(64, '\0');
        packet[0] = char(0x5d);
        packet[1] = char(0xbc);
        packet[2] = char(1);
        packet[3] = char(1);
        packet[9] = char(color.red());
        packet[10] = char(color.green());
        packet[11] = char(color.blue());
        return packet;
    }

    std::optional<State> decodeState(const QVariantMap& properties, QString* error)
    {
        const auto reject = [error](const QString& reason) -> std::optional<State> {
            if (error)
                *error = reason;
            return std::nullopt;
        };
        const auto brightness = properties.value(QStringLiteral("Brightness"));
        if (brightness.metaType() != QMetaType::fromType<uint>() || brightness.toUInt() > 3)
            return reject(QStringLiteral("Unsupported TUF brightness state"));

        const auto mode = properties.value(QStringLiteral("LedModeData"));
        if (mode.metaType() != QMetaType::fromType<QDBusArgument>())
            return reject(QStringLiteral("Missing TUF saved Static color"));
        const auto argument = mode.value<QDBusArgument>();
        const auto signature = argument.currentSignature();
        // Legacy asusd can encode speed/direction as integers despite string introspection.
        if (signature != QLatin1String("(uu(yyy)(yyy)uu)") && signature != QLatin1String("(uu(yyy)(yyy)ss)"))
            return reject(QStringLiteral("Unsupported TUF saved color signature"));
        uint modeId = 0, zone = 0;
        uchar r = 0, g = 0, b = 0, unused = 0;
        argument.beginStructure();
        argument >> modeId >> zone;
        argument.beginStructure();
        argument >> r >> g >> b;
        argument.endStructure();
        argument.beginStructure();
        argument >> unused >> unused >> unused;
        argument.endStructure();
        if (signature.endsWith(QLatin1String("uu)"))) {
            uint speed = 0, direction = 0;
            argument >> speed >> direction;
        } else {
            QString speed, direction;
            argument >> speed >> direction;
        }
        argument.endStructure();
        if (modeId != 0 || zone != 0)
            return reject(QStringLiteral("TUF requires whole-keyboard Static mode"));

        const auto power = properties.value(QStringLiteral("LedPower"));
        if (power.metaType() != QMetaType::fromType<QDBusArgument>())
            return reject(QStringLiteral("Missing TUF keyboard power state"));
        const auto powerArgument = power.value<QDBusArgument>();
        if (powerArgument.currentSignature() != QLatin1String("(a(ubbbb))"))
            return reject(QStringLiteral("Unsupported TUF keyboard power signature"));
        std::optional<bool> awake;
        powerArgument.beginStructure();
        powerArgument.beginArray();
        while (!powerArgument.atEnd()) {
            uint powerZone = 0;
            bool boot = false, currentAwake = false, sleep = false, shutdown = false;
            powerArgument.beginStructure();
            powerArgument >> powerZone >> boot >> currentAwake >> sleep >> shutdown;
            powerArgument.endStructure();
            if (powerZone == 1) {
                if (awake)
                    return reject(QStringLiteral("Duplicate TUF keyboard power state"));
                awake = currentAwake;
            }
        }
        powerArgument.endArray();
        powerArgument.endStructure();
        if (!awake)
            return reject(QStringLiteral("Missing TUF keyboard power zone"));
        if (error)
            error->clear();
        return State { QColor(r, g, b), brightness.toUInt(), *awake };
    }
}

class TufWorker : public QObject
{
    Q_OBJECT

public:
    TufWorker(const QDBusConnection& bus, QString service, QString path)
        : m_bus(bus)
        , m_service(std::move(service))
        , m_path(std::move(path))
        , m_watcher(m_service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this)
    {
        qDBusRegisterMetaType<QList<QByteArray>>();
        m_sendTimer.setSingleShot(true);
        m_retry.setInterval(2000);
        connect(&m_sendTimer, &QTimer::timeout, this, [this] { pump(); });
        connect(&m_retry, &QTimer::timeout, this, [this] {
            m_probeNeeded = true;
            pump();
        });
        connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& owner) {
                ++m_generation;
                m_owner = owner;
                m_savedColor.reset();
                m_lastColor.reset();
                m_touched = false;
                m_probeNeeded = true;
                m_blocked = false;
                m_sendTimer.stop();
                setStatus(false, QStringLiteral("Waiting for TUF daemon"));
                pump();
            });
        // asusd state cannot show whether keyboard output changed during suspend, so resend
        // the current color on resume. logind shares the asusd bus (the system bus outside tests).
        m_bus.connect(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"), this,
            SLOT(prepareForSleep(bool)));
    }

    void start()
    {
        if (m_started)
            return;
        m_started = true;
        m_retry.start();
        m_probeNeeded = true;
        pump();
    }

    void setEnabled(bool enabled)
    {
        if (m_enabled == enabled)
            return;
        m_enabled = enabled;
        m_sendTimer.stop();
        m_probeNeeded = true;
        m_blocked = false;
        if (!enabled)
            setStatus(false, QStringLiteral("TUF output disabled"));
        pump();
    }

    void setFps(int fps)
    {
        // Writes follow submitted frames; fps only caps them at 1-20 Hz. Non-positive values use 20 Hz.
        const int limit = fps <= 0 ? 20 : std::clamp(fps, 1, 20);
        m_interval = (1000 + limit - 1) / limit;
        m_sendTimer.stop();
        pump();
    }

    void submit(const QColor& color)
    {
        if (!color.isValid())
            return;
        m_desiredColor = QColor(color.red(), color.green(), color.blue());
        pump();
    }

    // Restore with blocking calls: the host may start another controller right after
    // destruction, and no event loop runs after service shutdown.
    void finish()
    {
        m_finished = true;
        notify = { };
        m_retry.stop();
        m_sendTimer.stop();
        if (m_pending)
            m_pending->waitForFinished(); // Its handler updates state but starts no call.
        if (!m_touched || m_owner.isEmpty())
            return;
        auto read = QDBusMessage::createMethodCall(
            m_owner, m_path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
        read << QString::fromLatin1(Tuf::Interface);
        read.setAutoStartService(false);
        const QDBusReply<QVariantMap> reply = m_bus.call(read, QDBus::Block, FinishTimeoutMs);
        const auto state = reply.isValid() ? Tuf::decodeState(reply.value()) : std::nullopt;
        // The same preflight as asynchronous restoration.
        if (!state || state->brightness == 0 || !state->keyboardAwake
            || (m_savedColor && *m_savedColor != state->savedColor))
            return;
        m_bus.call(rawWrite(state->savedColor), QDBus::Block, FinishTimeoutMs);
    }

    bool ready = false;
    QString status = QStringLiteral("Waiting for TUF daemon");
    quint64 completedFrames = 0;
    std::function<void()> notify;

private Q_SLOTS:
    void prepareForSleep(bool sleeping)
    {
        if (sleeping)
            return;
        // Replies from before resume must not suppress the new write.
        ++m_generation;
        m_lastColor.reset();
        m_probeNeeded = true;
        pump();
    }

private:
    // With the 1 s pending call, destruction waits at most 2 s.
    static constexpr int FinishTimeoutMs = 500;

    void setStatus(bool value, const QString& message)
    {
        if (ready == value && status == message)
            return;
        ready = value;
        status = message;
        const auto callback = notify;
        if (callback)
            callback();
    }

    void call(QDBusMessage message, std::function<void(const QDBusMessage&)> callback)
    {
        message.setAutoStartService(false);
        m_busy = true;
        const auto generation = m_generation;
        auto* pending = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 1000), this);
        m_pending = pending;
        connect(pending, &QDBusPendingCallWatcher::finished, this,
            [this, pending, generation, callback = std::move(callback)] {
                const auto reply = pending->reply();
                pending->deleteLater();
                m_pending = nullptr;
                m_busy = false;
                m_dispatching = true;
                if (generation == m_generation)
                    callback(reply);
                m_dispatching = false;
                pump();
            });
    }

    void unavailable(const QString& reason)
    {
        m_blocked = true;
        m_probeNeeded = false;
        m_lastColor.reset();
        setStatus(false, reason);
    }

    void resolveOwner()
    {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("GetNameOwner"));
        message << m_service;
        call(message, [this](const QDBusMessage& reply) {
            const QDBusPendingReply<QString> result(reply);
            if (result.isError() || !result.value().startsWith(QLatin1Char(':'))) {
                unavailable(QStringLiteral("TUF daemon unavailable"));
                return;
            }
            m_owner = result.value();
            m_probeNeeded = true;
        });
    }

    void probe()
    {
        m_probeNeeded = false;
        auto message = QDBusMessage::createMethodCall(
            m_owner, m_path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
        message << QString::fromLatin1(Tuf::Interface);
        const auto generation = m_generation;
        call(message, [this, generation](const QDBusMessage& reply) {
            const QDBusPendingReply<QVariantMap> result(reply);
            if (result.isError()) {
                // Without current state, restoration could turn an Off keyboard back on.
                if (!m_enabled)
                    m_touched = false;
                unavailable(QStringLiteral("Cannot read TUF state: ") + result.error().message());
                return;
            }
            QString error;
            const auto state = Tuf::decodeState(result.value(), &error);
            if (!state) {
                m_touched = false;
                unavailable(error);
                return;
            }
            if (m_savedColor && *m_savedColor != state->savedColor) {
                // A changed saved color supersedes pending restoration.
                m_lastColor.reset();
                m_touched = false;
            }
            m_savedColor = state->savedColor;
            if (state->brightness == 0 || !state->keyboardAwake) {
                // Legacy asusd raises cached Off to Med on raw writes, including restores.
                m_touched = false;
                unavailable(QStringLiteral("TUF keyboard is off"));
                return;
            }
            m_blocked = false;
            setStatus(m_enabled,
                m_enabled ? QStringLiteral("TUF Static ready") : QStringLiteral("TUF output disabled"));
            if (generation != m_generation)
                return;
            if (!m_enabled && m_touched) {
                write(*m_savedColor, true);
            } else if (m_enabled && m_desiredColor && m_desiredColor != m_lastColor) {
                // A frame queued during an idle state read still needs a rate-limited preflight.
                if (m_lastWrite.isValid() && m_lastWrite.elapsed() < m_interval)
                    return;
                write(*m_desiredColor, false);
            }
        });
    }

    QDBusMessage rawWrite(const QColor& color) const
    {
        // Fn/power changes between GetAll and this write can still trigger asusd's
        // cached Off-to-Med behavior; the daemon provides no atomic guard.
        auto message = QDBusMessage::createMethodCall(
            m_owner, m_path, QString::fromLatin1(Tuf::Interface), QStringLiteral("DirectAddressingRaw"));
        message << QVariant::fromValue(QList<QByteArray> { Tuf::rawPacket(color) });
        message.setAutoStartService(false);
        return message;
    }

    void write(const QColor& color, bool restoring)
    {
        if (m_finished)
            return;
        m_lastWrite.restart();
        m_touched = true; // An error/timeout does not prove that hardware was untouched.
        call(rawWrite(color), [this, color, restoring](const QDBusMessage& reply) {
            if (restoring) {
                m_touched = false;
                m_lastColor.reset();
                if (reply.type() == QDBusMessage::ErrorMessage)
                    unavailable(QStringLiteral("TUF restore failed: ") + reply.errorMessage());
                return;
            }
            if (reply.type() == QDBusMessage::ErrorMessage) {
                unavailable(QStringLiteral("TUF color write failed: ") + reply.errorMessage());
                if (!m_enabled) {
                    // A timeout can follow a successful hardware write. Try one fresh restore
                    // preflight even if disable happened while this call was pending.
                    m_blocked = false;
                    m_probeNeeded = true;
                }
                return;
            }
            ++completedFrames;
            m_lastColor = color;
        });
    }

    void pump()
    {
        if (m_finished || m_busy || m_dispatching)
            return;
        if (!m_started || (!m_enabled && !m_touched))
            return;
        if (m_blocked && !m_probeNeeded)
            return;
        if (m_owner.isEmpty()) {
            resolveOwner();
            return;
        }
        const bool newColor = m_enabled && m_desiredColor && m_desiredColor != m_lastColor;
        if (newColor && m_lastWrite.isValid() && m_lastWrite.elapsed() < m_interval) {
            if (!m_sendTimer.isActive())
                m_sendTimer.start(m_interval - int(m_lastWrite.elapsed()));
            return;
        }
        if (m_probeNeeded || newColor || (!m_enabled && m_touched))
            probe();
    }

    QDBusConnection m_bus;
    QString m_service, m_path, m_owner;
    QDBusServiceWatcher m_watcher;
    QTimer m_retry, m_sendTimer;
    QElapsedTimer m_lastWrite;
    QPointer<QDBusPendingCallWatcher> m_pending;
    std::optional<QColor> m_savedColor, m_desiredColor, m_lastColor;
    quint64 m_generation = 0;
    int m_interval = 50;
    bool m_enabled = true, m_started = false, m_busy = false, m_dispatching = false, m_blocked = false;
    bool m_touched = false, m_probeNeeded = false, m_finished = false;
};

TufBackend::TufBackend(const QDBusConnection& bus, QString service, QString path)
    : m_worker(new TufWorker(bus, std::move(service), std::move(path)))
{
    m_worker->notify = [this] {
        // Invoke a copy: the callback may destroy this backend.
        if (const auto callback = readinessChanged)
            callback();
    };
}
TufBackend::~TufBackend()
{
    m_worker->finish();
    // A readiness callback can destroy the backend inside one of the worker's reply handlers.
    m_worker->deleteLater();
}
void TufBackend::start(const QVariantMap&) { m_worker->start(); }
void TufBackend::setEnabled(bool enabled) { m_worker->setEnabled(enabled); }
void TufBackend::setLedFramesPerSecond(int fps) { m_worker->setFps(fps); }
bool TufBackend::ready() const { return m_worker->ready; }
QString TufBackend::status() const { return m_worker->status; }
quint64 TufBackend::completedFrames() const { return m_worker->completedFrames; }
void TufBackend::submit(const DeviceFrame& frame) { m_worker->submit(frame.uniform); }
}
#include "tuf_backend.moc"

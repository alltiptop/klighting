// SPDX-License-Identifier: GPL-3.0-or-later
#include "tuf_backend.h"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QtTest>
#include <functional>
#include <type_traits>

using namespace Ambient;

// Independent wire fixtures: intentionally do not reuse production decoding types.
struct TestColor {
    uchar red = 12, green = 34, blue = 56;
};
struct NumericMode {
    uint mode = 0, zone = 0;
    TestColor first, second;
    uint speed = 235, direction = 0;
};
struct StringMode {
    uint mode = 0, zone = 0;
    TestColor first, second;
    QString speed = QStringLiteral("Med"), direction = QStringLiteral("Right");
};
struct TestPowerZone {
    uint zone = 1;
    bool boot = true, awake = true, sleep = false, shutdown = false;
};
struct TestPower {
    QList<TestPowerZone> zones { TestPowerZone { } };
};
Q_DECLARE_METATYPE(TestColor)
Q_DECLARE_METATYPE(NumericMode)
Q_DECLARE_METATYPE(StringMode)
Q_DECLARE_METATYPE(TestPowerZone)
Q_DECLARE_METATYPE(TestPower)

QDBusArgument& operator<<(QDBusArgument& argument, const TestColor& color)
{
    argument.beginStructure();
    argument << color.red << color.green << color.blue;
    argument.endStructure();
    return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, TestColor& color)
{
    argument.beginStructure();
    argument >> color.red >> color.green >> color.blue;
    argument.endStructure();
    return argument;
}
QDBusArgument& operator<<(QDBusArgument& argument, const NumericMode& mode)
{
    argument.beginStructure();
    argument << mode.mode << mode.zone << mode.first << mode.second << mode.speed << mode.direction;
    argument.endStructure();
    return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, NumericMode& mode)
{
    argument.beginStructure();
    argument >> mode.mode >> mode.zone >> mode.first >> mode.second >> mode.speed >> mode.direction;
    argument.endStructure();
    return argument;
}
QDBusArgument& operator<<(QDBusArgument& argument, const StringMode& mode)
{
    argument.beginStructure();
    argument << mode.mode << mode.zone << mode.first << mode.second << mode.speed << mode.direction;
    argument.endStructure();
    return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, StringMode& mode)
{
    argument.beginStructure();
    argument >> mode.mode >> mode.zone >> mode.first >> mode.second >> mode.speed >> mode.direction;
    argument.endStructure();
    return argument;
}
QDBusArgument& operator<<(QDBusArgument& argument, const TestPowerZone& zone)
{
    argument.beginStructure();
    argument << zone.zone << zone.boot << zone.awake << zone.sleep << zone.shutdown;
    argument.endStructure();
    return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, TestPowerZone& zone)
{
    argument.beginStructure();
    argument >> zone.zone >> zone.boot >> zone.awake >> zone.sleep >> zone.shutdown;
    argument.endStructure();
    return argument;
}
QDBusArgument& operator<<(QDBusArgument& argument, const TestPower& power)
{
    argument.beginStructure();
    argument << power.zones;
    argument.endStructure();
    return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, TestPower& power)
{
    argument.beginStructure();
    argument >> power.zones;
    argument.endStructure();
    return argument;
}

static const QString TestService = QStringLiteral("org.example.KlightingTufTest");
static const QString TestPath = QStringLiteral("/test/tuf");
static const QString LogindService = QStringLiteral("org.freedesktop.login1");

class FakeTuf : public QDBusVirtualObject
{
public:
    FakeTuf()
        : connectionName(QStringLiteral("tuf-fixture-%1").arg(++serial))
        , bus(QDBusConnection::connectToBus(
              QString::fromUtf8(qgetenv("DBUS_SESSION_BUS_ADDRESS")), connectionName))
    {
        if (qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS")
            || !bus.registerVirtualObject(TestPath, this) || !bus.registerService(TestService))
            qFatal("TUF tests require their private, activation-free D-Bus session");
        clock.start();
    }
    ~FakeTuf() override
    {
        bus.unregisterService(TestService);
        bus.unregisterObject(TestPath);
        QDBusConnection::disconnectFromBus(connectionName);
    }
    QString introspect(const QString&) const override { return { }; }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        QDBusMessage reply;
        int delay = 0;
        if (message.interface() == QLatin1String("org.freedesktop.DBus.Properties")
            && message.member() == QLatin1String("GetAll") && message.signature() == QLatin1String("s")
            && message.arguments().first().toString() == QLatin1String(Tuf::Interface)) {
            ++reads;
            calls.append(QStringLiteral("read"));
            reply = failReads ? message.createErrorReply(QDBusError::Failed, QStringLiteral("read failed"))
                              : message.createReply(QVariant::fromValue(properties));
            delay = readDelay;
        } else if (message.interface() == QLatin1String(Tuf::Interface)
            && message.member() == QLatin1String("DirectAddressingRaw")
            && message.signature() == QLatin1String("aay")) {
            calls.append(QStringLiteral("raw"));
            frames.append(qdbus_cast<QList<QByteArray>>(message.arguments().first()));
            destinations.append(message.service());
            times.append(clock.elapsed());
            reply = failWrites ? message.createErrorReply(QDBusError::Failed, QStringLiteral("write failed"))
                               : message.createReply();
            delay = writeDelay;
        } else {
            ++unexpected;
            reply = message.createErrorReply(QDBusError::UnknownMethod, QStringLiteral("unexpected call"));
        }
        maxInFlight = std::max(maxInFlight, ++inFlight);
        QTimer::singleShot(delay, this, [this, connection, reply] {
            --inFlight;
            connection.send(reply);
        });
        return true;
    }

    void power(bool awake)
    {
        TestPower power;
        power.zones.first().awake = awake;
        properties[QStringLiteral("LedPower")] = QVariant::fromValue(power);
    }
    void savedColor(uchar r, uchar g, uchar b)
    {
        NumericMode mode;
        mode.first = { r, g, b };
        properties[QStringLiteral("LedModeData")] = QVariant::fromValue(mode);
    }
    void prepareForSleep(bool sleeping)
    {
        auto signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"));
        signal << sleeping;
        bus.send(signal);
    }

    inline static int serial = 0;
    QString connectionName;
    QDBusConnection bus;
    QVariantMap properties { { QStringLiteral("Brightness"), uint(2) },
        { QStringLiteral("LedModeData"), QVariant::fromValue(NumericMode { }) },
        { QStringLiteral("LedPower"), QVariant::fromValue(TestPower { }) } };
    QList<QList<QByteArray>> frames;
    QList<qint64> times;
    QStringList calls, destinations;
    QElapsedTimer clock;
    int reads = 0, unexpected = 0, readDelay = 0, writeDelay = 0, inFlight = 0, maxInFlight = 0;
    bool failReads = false, failWrites = false;
};

// Serves the fake from its own thread: a backend being destroyed makes blocking calls,
// which must be answered while the test thread waits. State is accessed only there.
class TufServer
{
public:
    TufServer()
    {
        m_context.moveToThread(&m_thread);
        m_thread.start();
        run([this] { m_fake = std::make_unique<FakeTuf>(); });
    }
    ~TufServer()
    {
        run([this] { m_fake.reset(); });
        m_thread.quit();
        m_thread.wait();
    }
    template <typename Function> auto with(Function function)
    {
        using Result = std::invoke_result_t<Function, FakeTuf&>;
        if constexpr (std::is_void_v<Result>) {
            run([&] { function(*m_fake); });
        } else {
            Result result { };
            run([&] { result = function(*m_fake); });
            return result;
        }
    }
    template <typename T> T get(T FakeTuf::* member)
    {
        return with([member](FakeTuf& fake) { return fake.*member; });
    }
    template <typename T> void set(T FakeTuf::* member, std::type_identity_t<T> value)
    {
        with([member, value](FakeTuf& fake) { fake.*member = value; });
    }
    void setState(const QString& name, const QVariant& value)
    {
        with([&](FakeTuf& fake) { fake.properties[name] = value; });
    }
    void power(bool awake)
    {
        with([awake](FakeTuf& fake) { fake.power(awake); });
    }
    void savedColor(uchar r, uchar g, uchar b)
    {
        with([=](FakeTuf& fake) { fake.savedColor(r, g, b); });
    }
    int frames()
    {
        return with([](FakeTuf& fake) { return int(fake.frames.size()); });
    }
    QColor rgb(int index)
    {
        const auto packet = with([index](FakeTuf& fake) { return fake.frames.at(index).first(); });
        return QColor(uchar(packet.at(9)), uchar(packet.at(10)), uchar(packet.at(11)));
    }

private:
    void run(const std::function<void()>& function)
    {
        QMetaObject::invokeMethod(&m_context, function, Qt::BlockingQueuedConnection);
    }

    QThread m_thread;
    QObject m_context;
    std::unique_ptr<FakeTuf> m_fake;
};

class TufTest : public QObject
{
    Q_OBJECT
private:
    static DeviceFrame color(int r, int g, int b) { return { QColor(r, g, b), { }, { } }; }
    static std::unique_ptr<TufBackend> backend()
    {
        return std::make_unique<TufBackend>(QDBusConnection::sessionBus(), TestService, TestPath);
    }

private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<TestColor>();
        qDBusRegisterMetaType<NumericMode>();
        qDBusRegisterMetaType<StringMode>();
        qDBusRegisterMetaType<TestPowerZone>();
        qDBusRegisterMetaType<QList<TestPowerZone>>();
        qDBusRegisterMetaType<TestPower>();
        qDBusRegisterMetaType<QList<QByteArray>>();
    }
    void packetGolden()
    {
        const auto expected = QByteArray::fromHex("5dbc01010000000000123456"
                                                  "00000000000000000000000000000000"
                                                  "00000000000000000000000000000000"
                                                  "00000000000000000000000000000000"
                                                  "00000000");
        QCOMPARE(expected.size(), 64);
        QCOMPARE(Tuf::rawPacket(QColor(0x12, 0x34, 0x56)), expected);
    }
    void compatibleLayouts_data()
    {
        QTest::addColumn<bool>("strings");
        QTest::newRow("runtime-numeric") << false;
        QTest::newRow("introspection-strings") << true;
    }
    void compatibleLayouts()
    {
        QFETCH(bool, strings);
        TufServer fake;
        if (strings)
            fake.setState(QStringLiteral("LedModeData"), QVariant::fromValue(StringMode { }));
        auto client = backend();
        client->start({ });
        QTRY_VERIFY(client->ready());
        client->submit(color(100, 110, 120));
        QTRY_COMPARE(client->completedFrames(), 1);
        QCOMPARE(fake.with([](FakeTuf& f) { return int(f.frames.first().size()); }), 1);
        QCOMPARE(fake.rgb(0), QColor(100, 110, 120));
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 2);
        QCOMPARE(fake.rgb(1), QColor(12, 34, 56));
        QCOMPARE(client->completedFrames(), 1);
        QCOMPARE(fake.get(&FakeTuf::unexpected), 0);
        QCOMPARE(fake.get(&FakeTuf::maxInFlight), 1);
        QVERIFY(fake.get(&FakeTuf::destinations).first().startsWith(QLatin1Char(':')));
    }
    void malformedState_data()
    {
        QTest::addColumn<QString>("property");
        QTest::addColumn<QVariant>("value");
        QTest::newRow("brightness-signed") << QStringLiteral("Brightness") << QVariant(2);
        QTest::newRow("brightness-range") << QStringLiteral("Brightness") << QVariant(uint(4));
        QTest::newRow("mode-wrong-type") << QStringLiteral("LedModeData") << QVariant(QStringLiteral("x"));
        QTest::newRow("mode-wrong-signature")
            << QStringLiteral("LedModeData") << QVariant::fromValue(TestPowerZone { });
        auto mode = NumericMode { };
        mode.mode = 1;
        QTest::newRow("non-static") << QStringLiteral("LedModeData") << QVariant::fromValue(mode);
        mode.mode = 0;
        mode.zone = 1;
        QTest::newRow("zoned") << QStringLiteral("LedModeData") << QVariant::fromValue(mode);
        TestPower power;
        power.zones.clear();
        QTest::newRow("no-keyboard") << QStringLiteral("LedPower") << QVariant::fromValue(power);
        power.zones = { TestPowerZone { }, TestPowerZone { } };
        QTest::newRow("duplicate-keyboard") << QStringLiteral("LedPower") << QVariant::fromValue(power);
        QTest::newRow("power-wrong-type") << QStringLiteral("LedPower") << QVariant(true);
        QTest::newRow("power-wrong-signature")
            << QStringLiteral("LedPower") << QVariant::fromValue(TestPowerZone { });
    }
    void malformedState()
    {
        QFETCH(QString, property);
        QFETCH(QVariant, value);
        TufServer fake;
        fake.setState(property, value);
        auto client = backend();
        client->start({ });
        client->submit(color(200, 100, 0));
        QTRY_VERIFY(fake.get(&FakeTuf::reads) > 0);
        QTest::qWait(60);
        QVERIFY(!client->ready());
        QCOMPARE(fake.frames(), 0);
    }
    void latestFrameAndZeroFps()
    {
        TufServer fake;
        fake.set(&FakeTuf::writeDelay, 140);
        auto client = backend();
        client->setLedFramesPerSecond(0); // Non-positive rates keep the 20 Hz cap.
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(fake.frames(), 1);
        for (int i = 20; i <= 80; ++i)
            client->submit(color(i, i + 1, i + 2));
        QTRY_COMPARE(client->completedFrames(), 2);
        QCOMPARE(fake.frames(), 2);
        QCOMPARE(fake.rgb(1), QColor(80, 81, 82));
        client->submit(color(80, 81, 82));
        QTest::qWait(180);
        QCOMPARE(fake.frames(), 2);
        QCOMPARE(fake.get(&FakeTuf::maxInFlight), 1);
        const auto calls = fake.get(&FakeTuf::calls);
        for (int i = 0; i < calls.size(); ++i) {
            if (calls.at(i) == QLatin1String("raw"))
                QVERIFY(i > 0 && calls.at(i - 1) == QLatin1String("read"));
        }
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 3);
        QCOMPARE(fake.rgb(2), QColor(12, 34, 56));
    }
    void twentyFpsCeiling()
    {
        TufServer fake;
        auto client = backend();
        client->setLedFramesPerSecond(1000);
        client->start({ });
        QTRY_VERIFY_WITH_TIMEOUT(client->ready(), 2000);

        int submitted = 0;
        QTimer producer;
        producer.setTimerType(Qt::PreciseTimer);
        producer.setInterval(5);
        connect(&producer, &QTimer::timeout, this, [&] {
            ++submitted;
            client->submit(color(submitted & 0xff, (submitted >> 8) & 0xff, 77));
        });
        QEventLoop loop;
        QTimer::singleShot(600, &loop, &QEventLoop::quit);
        producer.start();
        loop.exec();
        producer.stop();

        // Drain the latest frame after the independent producer stops.
        const QColor latest(255, 254, 253);
        client->submit(color(latest.red(), latest.green(), latest.blue()));
        QTRY_VERIFY_WITH_TIMEOUT(fake.frames() > 0 && fake.rgb(fake.frames() - 1) == latest, 2000);
        const int frames = fake.frames();
        QTRY_COMPARE_WITH_TIMEOUT(client->completedFrames(), quint64(frames), 2000);
        const auto times = fake.get(&FakeTuf::times);
        QVERIFY(submitted >= 20);
        QVERIFY(times.size() >= 4);
        // Allow delivery jitter and one boundary frame, but reject sustained excess.
        const qint64 span = times.last() - times.first();
        QVERIFY2(qint64(times.size() - 1) * 45 <= span + 50,
            qPrintable(QStringLiteral("%1 frames arrived over %2 ms").arg(times.size()).arg(span)));
        client->setEnabled(false);
        QTRY_COMPARE_WITH_TIMEOUT(fake.frames(), frames + 1, 2000);
        QCOMPARE(fake.rgb(frames), QColor(12, 34, 56));
    }
    void frameArrivingDuringPeriodicProbeRespectsFps()
    {
        TufServer fake;
        auto client = backend();
        client->setLedFramesPerSecond(1);
        client->start({ });
        QTRY_VERIFY(client->ready());
        QTest::qWait(1450);
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(client->completedFrames(), 1);
        fake.set(&FakeTuf::readDelay, 150);
        const int reads = fake.get(&FakeTuf::reads);
        QTRY_VERIFY_WITH_TIMEOUT(fake.get(&FakeTuf::reads) > reads, 1200);
        client->submit(color(4, 5, 6));
        QTest::qWait(200);
        QCOMPARE(fake.frames(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(client->completedFrames(), 2, 1500);
        const auto times = fake.get(&FakeTuf::times);
        QVERIFY(times.at(1) - times.at(0) >= 995);
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 3);
    }
    void disabledBeforeStart()
    {
        TufServer fake;
        auto client = backend();
        client->setEnabled(false);
        client->setLedFramesPerSecond(0);
        client->start({ });
        client->submit(color(44, 55, 66));
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 0);
        QCOMPARE(fake.get(&FakeTuf::reads), 0);
        client->setEnabled(true);
        QTRY_COMPARE(client->completedFrames(), 1);
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 2);
    }
    void offAndRecovery_data()
    {
        QTest::addColumn<bool>("brightness");
        QTest::newRow("brightness-off") << true;
        QTest::newRow("keyboard-power-off") << false;
    }
    void offAndRecovery()
    {
        QFETCH(bool, brightness);
        TufServer fake;
        if (brightness)
            fake.setState(QStringLiteral("Brightness"), uint(0));
        else
            fake.power(false);
        auto client = backend();
        client->start({ });
        client->submit(color(7, 8, 9));
        QTRY_VERIFY(client->status().contains(QStringLiteral("off")));
        for (int i = 0; i < 200; ++i)
            client->submit(color(7, 8, 9));
        QTest::qWait(100);
        QCOMPARE(fake.get(&FakeTuf::reads), 1); // Blocked output must not poll at frame frequency.
        QCOMPARE(fake.frames(), 0);
        fake.setState(QStringLiteral("Brightness"), uint(2));
        fake.power(true);
        QTRY_COMPARE_WITH_TIMEOUT(client->completedFrames(), 1, 3000);
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 2);
    }
    void unsignalledOffPreventsFrameAndRestore()
    {
        TufServer fake;
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(client->completedFrames(), 1);
        fake.setState(QStringLiteral("Brightness"), uint(0)); // Fn change emits no signal.
        client->submit(color(99, 98, 97));
        QTRY_VERIFY(!client->ready());
        client->setEnabled(false);
        client.reset();
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 1);
    }
    void savedColorChangesArePreserved()
    {
        TufServer fake;
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(client->completedFrames(), 1);
        fake.savedColor(80, 90, 100);
        client->submit(color(4, 5, 6));
        QTRY_COMPARE(client->completedFrames(), 2);
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 3);
        QCOMPARE(fake.rgb(2), QColor(80, 90, 100));
    }
    void externalChangeWhileDisablingWins()
    {
        TufServer fake;
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(client->completedFrames(), 1);
        fake.savedColor(80, 90, 100);
        client->setEnabled(false);
        QTRY_VERIFY(fake.get(&FakeTuf::reads) >= 2);
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 1);
    }
    void disablePendingRestoresAfterReply()
    {
        TufServer fake;
        fake.set(&FakeTuf::writeDelay, 150);
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(fake.frames(), 1);
        client->submit(color(4, 5, 6));
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 2);
        QCOMPARE(fake.rgb(1), QColor(12, 34, 56));
        QCOMPARE(client->completedFrames(), 1);
        QCOMPARE(fake.get(&FakeTuf::maxInFlight), 1);
    }
    void destructionRestoresBeforeReturning()
    {
        TufServer fake;
        fake.set(&FakeTuf::writeDelay, 150);
        auto client = backend();
        int callbacks = 0;
        client->readinessChanged = [&callbacks] { ++callbacks; };
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(fake.frames(), 1);
        const int before = callbacks;
        client.reset(); // No event loop runs here, as during service shutdown.
        QCOMPARE(fake.frames(), 2);
        QCOMPARE(fake.rgb(1), QColor(12, 34, 56));
        QTest::qWait(180);
        QCOMPARE(callbacks, before);
        QCOMPARE(fake.frames(), 2);
        QCOMPARE(fake.get(&FakeTuf::maxInFlight), 1); // The pending frame finished first.
    }
    void destructionSkipsRestoreForUnsignalledOff()
    {
        TufServer fake;
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(client->completedFrames(), 1);
        fake.setState(QStringLiteral("Brightness"), uint(0));
        const int reads = fake.get(&FakeTuf::reads);
        client.reset();
        QCOMPARE(fake.get(&FakeTuf::reads), reads + 1);
        QCOMPARE(fake.frames(), 1);
    }
    void destructionDoesNotRepeatPendingRestore()
    {
        TufServer fake;
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(client->completedFrames(), 1);
        fake.set(&FakeTuf::writeDelay, 150);
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 2);
        client.reset();
        QCOMPARE(fake.frames(), 2);
        QCOMPARE(fake.rgb(1), QColor(12, 34, 56));
    }
    void replacementStartsAfterRestore()
    {
        TufServer fake;
        fake.set(&FakeTuf::writeDelay, 250);
        auto old = backend();
        old->start({ });
        old->submit(color(1, 2, 3));
        QTRY_COMPARE(fake.frames(), 1);
        old.reset();
        QCOMPARE(fake.frames(), 2);
        QCOMPARE(fake.rgb(1), QColor(12, 34, 56));
        fake.set(&FakeTuf::writeDelay, 0);
        auto replacement = backend();
        replacement->start({ });
        replacement->submit(color(70, 80, 90));
        QTRY_COMPARE(replacement->completedFrames(), 1);
        QTest::qWait(300);
        QCOMPARE(fake.frames(), 3);
        QCOMPARE(fake.rgb(2), QColor(70, 80, 90));
        replacement->submit(color(70, 80, 90));
        QTest::qWait(80);
        QCOMPARE(fake.frames(), 3);
        replacement->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 4);
        QCOMPARE(fake.rgb(3), QColor(12, 34, 56));
    }
    void readinessCallbackCanSubmit()
    {
        TufServer fake;
        auto client = backend();
        client->readinessChanged = [&client] {
            if (client->ready())
                client->submit(color(50, 60, 70));
        };
        client->start({ });
        QTRY_COMPARE(client->completedFrames(), 1);
        QCOMPARE(fake.frames(), 1);
        QCOMPARE(fake.get(&FakeTuf::maxInFlight), 1);
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 2);
    }
    void readinessCallbackCanDisable()
    {
        TufServer fake;
        auto client = backend();
        client->submit(color(50, 60, 70));
        client->readinessChanged = [&client] {
            if (client->ready())
                client->setEnabled(false);
        };
        client->start({ });
        QTRY_VERIFY(client->status().contains(QStringLiteral("disabled")));
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 0);
        QCOMPARE(fake.get(&FakeTuf::maxInFlight), 1);
    }
    void readinessCallbackCanDestroy()
    {
        TufServer fake;
        fake.set(&FakeTuf::failWrites, true);
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_VERIFY(client->status().contains(QStringLiteral("write failed")));
        client->readinessChanged = [&client] {
            if (client->ready())
                client.reset(); // Inside the reply handler of the recovery read.
        };
        fake.set(&FakeTuf::failWrites, false);
        QTRY_VERIFY_WITH_TIMEOUT(!client, 3000);
        QCOMPARE(fake.frames(), 2);
        QCOMPARE(fake.rgb(1), QColor(12, 34, 56));
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 2);
    }
    void timedOutFrameStillRestoresOnDestruction()
    {
        TufServer fake;
        fake.set(&FakeTuf::writeDelay, 1500);
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(fake.frames(), 1);
        fake.set(&FakeTuf::writeDelay, 0);
        client.reset(); // The frame call times out after 1 s; a timeout does not prove no write.
        QCOMPARE(fake.frames(), 2);
        QCOMPARE(fake.rgb(1), QColor(12, 34, 56));
        QTest::qWait(600); // The late frame reply must not reach the destroyed backend.
        QCOMPARE(fake.frames(), 2);
    }
    void offDuringPendingDisablePreventsRestore()
    {
        TufServer fake;
        fake.set(&FakeTuf::writeDelay, 150);
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(fake.frames(), 1);
        fake.setState(QStringLiteral("Brightness"), uint(0));
        client->setEnabled(false);
        QTRY_VERIFY(client->status().contains(QStringLiteral("off")));
        QCOMPARE(fake.frames(), 1);
        client.reset();
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 1);
    }
    void writeFailureRecovers()
    {
        TufServer fake;
        fake.set(&FakeTuf::failWrites, true);
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_VERIFY(client->status().contains(QStringLiteral("write failed")));
        QCOMPARE(client->completedFrames(), 0);
        for (int i = 0; i < 100; ++i)
            client->submit(color(4, 5, 6));
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 1);
        fake.set(&FakeTuf::failWrites, false);
        QTRY_COMPARE_WITH_TIMEOUT(client->completedFrames(), 1, 3000);
        QCOMPARE(fake.rgb(1), QColor(4, 5, 6));
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 3);
    }
    void readFailureNeverWrites()
    {
        TufServer fake;
        fake.set(&FakeTuf::failReads, true);
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_VERIFY(client->status().contains(QStringLiteral("read failed")));
        QCOMPARE(fake.frames(), 0);
        fake.set(&FakeTuf::failReads, false);
        QTRY_COMPARE_WITH_TIMEOUT(client->completedFrames(), 1, 3000);
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 2);
    }
    void failedRestorePreflightStopsSafely()
    {
        TufServer fake;
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(client->completedFrames(), 1);
        const int before = fake.get(&FakeTuf::reads);
        fake.set(&FakeTuf::failReads, true);
        client.reset();
        QCOMPARE(fake.get(&FakeTuf::reads), before + 1);
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 1);
        QCOMPARE(fake.get(&FakeTuf::reads), before + 1);
    }
    void resumeDuringPendingWriteResendsCurrentColor()
    {
        TufServer fake;
        QVERIFY(fake.with([](FakeTuf& f) { return f.bus.registerService(LogindService); }));
        fake.set(&FakeTuf::writeDelay, 700);
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(fake.frames(), 1);
        QCOMPARE(client->completedFrames(), 0);
        fake.with([](FakeTuf& f) {
            f.prepareForSleep(true);
            f.prepareForSleep(false);
        });
        QTest::qWait(150);
        QCOMPARE(client->completedFrames(), 0);
        fake.set(&FakeTuf::writeDelay, 0);
        QTRY_COMPARE_WITH_TIMEOUT(fake.frames(), 2, 3500);
        QCOMPARE(fake.rgb(1), QColor(1, 2, 3));
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 3);
    }
    void resumeRewritesCurrentColor()
    {
        TufServer fake;
        QVERIFY(fake.with([](FakeTuf& f) { return f.bus.registerService(LogindService); }));
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(client->completedFrames(), 1);
        fake.with([](FakeTuf& f) { f.prepareForSleep(true); });
        client->submit(color(1, 2, 3));
        QTest::qWait(100);
        QCOMPARE(fake.frames(), 1);
        fake.with([](FakeTuf& f) { f.prepareForSleep(false); });
        QTRY_COMPARE(client->completedFrames(), 2);
        QCOMPARE(fake.rgb(1), QColor(1, 2, 3));
        const auto calls = fake.get(&FakeTuf::calls);
        QCOMPARE(calls.at(calls.size() - 2), QStringLiteral("read"));
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 3);
    }
    void ownerRestartIgnoresStaleReply()
    {
        TufServer first;
        first.set(&FakeTuf::writeDelay, 300);
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_COMPARE(first.frames(), 1);
        QVERIFY(first.with([](FakeTuf& f) { return f.bus.unregisterService(TestService); }));
        TufServer second;
        second.savedColor(70, 80, 90);
        client->submit(color(4, 5, 6));
        QTRY_COMPARE_WITH_TIMEOUT(second.frames(), 1, 1500);
        QTRY_COMPARE(client->completedFrames(), 1); // The old successful reply is stale.
        QCOMPARE(second.rgb(0), QColor(4, 5, 6));
        QCOMPARE(first.frames(), 1);
        client->setEnabled(false);
        QTRY_COMPARE(second.frames(), 2);
        QCOMPARE(second.rgb(1), QColor(70, 80, 90));
        QCOMPARE(first.frames(), 1);
    }
    void missingServiceRecoversWithoutActivation()
    {
        auto client = backend();
        client->start({ });
        client->submit(color(1, 2, 3));
        QTRY_VERIFY(client->status().contains(QStringLiteral("unavailable")));
        TufServer fake;
        QTRY_COMPARE(client->completedFrames(), 1);
        client->setEnabled(false);
        QTRY_COMPARE(fake.frames(), 2);
        QCOMPARE(fake.get(&FakeTuf::unexpected), 0);
    }
};

QTEST_GUILESS_MAIN(TufTest)
#include "test_tuf.moc"

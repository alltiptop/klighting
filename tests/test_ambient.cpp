// SPDX-License-Identifier: GPL-3.0-or-later
#include "asusd_client.h"
#include "color.h"
#include "effect_names.h"
#include "effect_palette.h"
#include "g615lw_layout.h"
#include "keyboard_profile.h"
#include "lamp_array.h"
#include "settings.h"
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <QtTest>

using namespace Ambient;
static const QString Path = QStringLiteral("/xyz/ljones/aura/19b6_2_4");
static const QString Service = QStringLiteral("org.example.AmbientAuraTest");

static QByteArray colorSample(std::initializer_list<std::pair<QColor, int>> patches)
{
    QByteArray sample;
    for (const auto& [color, count] : patches) {
        const char rgba[] = { char(color.red()), char(color.green()), char(color.blue()), char(255) };
        sample += QByteArray(rgba, 4).repeated(count);
    }
    return sample;
}

static AuraEffect originalEffect()
{
    return { 1, 0, { 12, 34, 56 }, { 78, 90, 123 }, QStringLiteral("Low"), QStringLiteral("Left") };
}
static QVariantMap properties(uint brightness = 2)
{
    return { { "DeviceType", uint(0) }, { "Brightness", brightness },
        { "SupportedBasicModes", QVariant::fromValue(QList<uint> { 0, 1 }) },
        { "SupportedBasicZones", QVariant::fromValue(QList<uint> { }) },
        { "LedModeData", QVariant::fromValue(originalEffect()) },
        { "LedPower",
            QVariant::fromValue(
                AuraPower { { { 1, true, true, false, false }, { 2, false, true, false, false } } }) } };
}
class FakeAsusd : public QDBusVirtualObject
{
    Q_OBJECT
public:
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "ambient-fixture");
    QVariantMap props = properties();
    QList<Packets> frames;
    QByteArray stagedEffect;
    QList<QByteArray> displayedEffects;
    QList<Packets> displayedFrames;
    bool directMode = false;
    int directInitializations = 0;
    QList<qint64> times;
    QElapsedTimer clock;
    int replyDelay = 0, unexpected = 0, failAt = -1;
    bool failWrites = false;
    FakeAsusd()
    {
        clock.start();
        if (!bus.registerVirtualObject("/", this, QDBusConnection::SubPath) || !bus.registerService(Service))
            qFatal("Cannot register test-only D-Bus fixture");
    }
    ~FakeAsusd() override
    {
        bus.unregisterService(Service);
        bus.unregisterObject("/", QDBusConnection::UnregisterTree);
        QDBusConnection::disconnectFromBus("ambient-fixture");
    }
    QString introspect(const QString&) const override { return { }; }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        if (message.interface() == "org.freedesktop.DBus.ObjectManager"
            && message.member() == "GetManagedObjects") {
            const ManagedObjects objects { { QDBusObjectPath(Path), { { AuraInterface, props } } } };
            connection.send(message.createReply(QVariant::fromValue(objects)));
        } else if (message.interface() == AuraInterface && message.member() == "DirectAddressingRaw"
            && message.path() == Path && message.signature() == "aay") {
            frames.append(qdbus_cast<Packets>(message.arguments().first()));
            times.append(clock.elapsed());
            const bool failed = failWrites || frames.size() == failAt;
            // Model asusd 6.5.0's two raw paths independently: 0xbc forwards
            // all rows and initializes once; factory commands leave direct mode,
            // use only data[0], and append a short SET ignored by this controller.
            if (!failed && !frames.last().isEmpty()) {
                const auto& report = frames.last().first();
                if (report.size() == 64 && uchar(report[0]) == 0x5d) {
                    if (uchar(report[1]) == 0xbc) {
                        if (!directMode)
                            ++directInitializations;
                        directMode = true;
                        displayedFrames.append(frames.last());
                    } else {
                        directMode = false;
                        if (uchar(report[1]) == 0xb3)
                            stagedEffect = report;
                        else if (uchar(report[1]) == 0xb5 && !stagedEffect.isEmpty())
                            displayedEffects.append(stagedEffect);
                    }
                }
            }
            const auto reply = failed
                ? message.createErrorReply(QDBusError::Failed, "simulated hardware failure")
                : message.createReply();
            QTimer::singleShot(replyDelay, this, [connection, reply] { connection.send(reply); });
        } else {
            ++unexpected; // In particular, no Properties.Set or direct hardware calls are permitted.
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "unexpected test call"));
        }
        return true;
    }
    void brightness(uint value)
    {
        props["Brightness"] = value;
        auto signal
            = QDBusMessage::createSignal(Path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
        signal << QString::fromLatin1(AuraInterface) << QVariantMap { { "Brightness", value } }
               << QStringList { };
        bus.send(signal);
    }
};

struct UsbState {
    QList<LedFrame> frames;
    QList<qint64> times;
    QElapsedTimer clock;
    int delay = 0, failAt = -1, releases = 0, shutdowns = 0;
    bool active = false;
    UsbState() { clock.start(); }
};
class FakeUsb : public LedTransport
{
public:
    explicit FakeUsb(std::shared_ptr<UsbState> state)
        : state(std::move(state))
    {
    }
    void submit(const LedFrame& frame, quint64 operation) override
    {
        state->frames.append(frame);
        state->times.append(state->clock.elapsed());
        state->active = true;
        const auto error
            = state->frames.size() == state->failAt ? QStringLiteral("test USB error") : QString();
        QTimer::singleShot(
            state->delay, this, [this, operation, error] { Q_EMIT finished(operation, error); });
    }
    void release(quint64 operation) override
    {
        ++state->releases;
        QTimer::singleShot(state->delay, this, [this, operation] {
            state->active = false;
            Q_EMIT finished(operation, { });
        });
    }
    void shutdown() override
    {
        state->active = false;
        ++state->shutdowns;
    }
    std::shared_ptr<UsbState> state;
};
struct Fixture {
    FakeAsusd fake;
    std::shared_ptr<UsbState> usb = std::make_shared<UsbState>();
    AsusdClient client { nullptr, QDBusConnection::sessionBus(), Service, std::make_unique<FakeUsb>(usb) };
};

class AmbientTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { registerAuraTypes(); }
    void protocolGolden()
    {
        // Independent fixture from rog-aura 6.5.0 check_led_static_packet.
        AuraEffect effect { 0, 0, { 0xff, 0x11, 0xdd }, { 0xa6, 0, 0 }, "Med", "Right" };
        auto packet = effectPacket(effect);
        QVERIFY(packet);
        QCOMPARE(packet->size(), 64);
        QCOMPARE(packet->left(17), QByteArray::fromHex("5db30000ff11ddeb0000a6000000000000"));
        QCOMPARE(packet->mid(17), QByteArray(47, '\0'));
        QCOMPARE(setPacket(), QByteArray::fromHex("5db5") + QByteArray(62, '\0'));
        effect.zone = 6;
        QVERIFY(!effectPacket(effect));
        effect.zone = 0;
        effect.speed = "unknown";
        QVERIFY(!effectPacket(effect));
    }
    void deviceSelection()
    {
        auto props = properties();
        QVERIFY(compatibleAura(Path, props));
        QVERIFY(!compatibleAura("/xyz/ljones/aura/1abe_1_2", props));
        props["DeviceType"] = uint(4);
        QVERIFY(!compatibleAura(Path, props));
        props = properties();
        props["LedModeData"] = QStringLiteral("invalid");
        QVERIFY(!compatibleAura(Path, props));
        props = properties();
        props["SupportedBasicZones"] = QVariant::fromValue(QList<uint> { 1, 2, 3, 4 });
        QVERIFY(!compatibleAura(Path, props));
    }
    void lampArrayProtocolGolden()
    {
        QList<bool> layout(88, true);
        layout.append(QList<bool>(12, false));
        LedFrame frame { QColor(0x12, 0x34, 0x56), 3, true, true };
        // HID Lighting page, RangeUpdate: flags, start/end inclusive, RGBI.
        QCOMPARE(lampArrayPackets(frame, layout),
            QList<QByteArray> { QByteArray::fromHex("050100006300123456ff") });
        frame.lightbar = false;
        frame.brightness = 2;
        QCOMPARE(lampArrayPackets(frame, layout),
            (QList<QByteArray> {
                QByteArray::fromHex("050000005700123456ff"), QByteArray::fromHex("05015800630000000000") }));
        frame.keyboard = false;
        frame.lightbar = true;
        QCOMPARE(lampArrayPackets(frame, layout),
            (QList<QByteArray> {
                QByteArray::fromHex("05000000570000000000"), QByteArray::fromHex("050158006300123456ff") }));
        frame.brightness = 0;
        QCOMPARE(lampArrayPackets(frame, layout),
            QList<QByteArray> { QByteArray::fromHex("05010000630000000000") });
        frame.brightness = 4;
        QVERIFY(lampArrayPackets(frame, layout).isEmpty());
    }
    void lampArrayHonorsReportedPurposeOrder()
    {
        LedFrame frame { QColor(10, 20, 30), 1, true, false };
        const auto packets = lampArrayPackets(frame, { false, true, false, true });
        QCOMPARE(packets,
            (QList<QByteArray> { QByteArray::fromHex("05000000000000000000"),
                QByteArray::fromHex("0500010001000a141eff"), QByteArray::fromHex("05000200020000000000"),
                QByteArray::fromHex("0501030003000a141eff") }));
        // Only the final report commits; no partially black intermediate frame.
        for (int i = 0; i < packets.size(); ++i)
            QCOMPARE(uchar(packets[i][1]), i == packets.size() - 1 ? 1 : 0);
    }
    void lampArrayDoesNotApplyHardwareBrightnessTwice()
    {
        QList<bool> layout(88, true);
        layout.append(QList<bool>(12, false));
        // A black screen at a 25% floor must keep RGB 64 at full lamp intensity.
        // The independent hardware backlight already applies Low/Med/High.
        const auto floor = mapBrightness(Qt::black, 25, 50);
        QCOMPARE(floor, QColor(64, 64, 64));
        for (int brightness = 1; brightness <= 3; ++brightness) {
            QCOMPARE(lampArrayPackets({ floor, uint(brightness), true, true }, layout),
                QList<QByteArray> { QByteArray::fromHex("050100006300404040ff") });
        }
        QCOMPARE(lampArrayPackets({ floor, 0, true, true }, layout),
            QList<QByteArray> { QByteArray::fromHex("05010000630000000000") });
    }
    void keyboardProfileMatchesMeasuredLampMetadata()
    {
        QFile file(QStringLiteral(LAMP_FIXTURE_PATH));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lamps = QJsonDocument::fromJson(file.readAll()).object()["lamps"].toArray();
        QCOMPARE(lamps.size(), 100);
        for (int id = 0; id < KeyboardLampCount; ++id) {
            const auto lamp = lamps[id].toObject();
            QCOMPARE(lamp["id"].toInt(), id);
            QCOMPARE(lamp["purpose"].toInt(), 1);
            QCOMPARE(lamp["programmable"].toInt(), 1);
            QVERIFY(matchesKeyboardLamp(id, lamp["x"].toInt(), lamp["y"].toInt(), lamp["binding"].toInt()));
            QVERIFY(!matchesKeyboardLamp(
                id, lamp["x"].toInt() + 1000, lamp["y"].toInt(), lamp["binding"].toInt()));
        }
        for (int id = 88; id < 100; ++id) {
            const auto lamp = lamps[id].toObject();
            QVERIFY(matchesStripLamp(id, lamp["x"].toInt(), lamp["y"].toInt()));
            QVERIFY(!matchesStripLamp(id, lamp["x"].toInt() + 1000, lamp["y"].toInt()));
        }
        for (int id = 0; id < 5; ++id) {
            QCOMPARE(QString::fromLatin1(keyboardLayout()[id].label), QStringLiteral("M%1").arg(id + 1));
            QCOMPARE(keyboardLayout()[id].binding, 0);
        }
        QCOMPARE(QString::fromLatin1(keyboardLayout()[36].label), QStringLiteral("W"));
        QCOMPARE(QString::fromLatin1(keyboardLayout()[78].label), QStringLiteral("Fn"));
    }
    void keyAssignmentsRoundTripAndUseFinalEffectColors()
    {
        KeyboardStyles styles;
        const LightingPreset preset;
        const QMap<QString, QColor> palette { { DefaultEffectId, QColor(10, 20, 70) } };
        QVERIFY(lightingColors(preset, styles, g615lwDescription(), palette).keys.isEmpty());
        styles[0] = { KeyMode::Static, Qt::red };
        styles[4] = { KeyMode::Off, Qt::transparent };
        styles[36] = { KeyMode::Static, QColor(120, 100, 80) };
        QCOMPARE(parseKeyStyles(serializeKeyStyles(styles)), styles);
        const auto colors = lightingColors(preset, styles, g615lwDescription(), palette).keys;
        QCOMPARE(colors.size(), 88);
        QCOMPARE(colors[0], QColor(Qt::red)); // An explicit color is independent of effect settings.
        QCOMPARE(colors[1], QColor(10, 20, 70)); // Ambient is already calibrated.
        QCOMPARE(colors[4], QColor(Qt::black)); // Explicit Off bypasses the floor.
        QCOMPARE(colors[36], QColor(120, 100, 80));
        QCOMPARE(parseKeyStyles({ "-1:static:#ff0000", "65600:off:", "x:off:", "5:static:invalid",
                     "6:unknown:#ff0000" }),
            KeyboardStyles());
        const KeyGroups groups { { "Gaming", { 0, 4, 36, 50, 51, 52 } } };
        QCOMPARE(parseKeyGroups(serializeKeyGroups(groups)), groups);
        QCOMPARE(parseKeyGroups(QStringLiteral(
                     "{\"Valid\":[4,2,2,-1,65600,1.5,\"3\",true],\"Empty\":[],\"Bad\":false}")),
            (KeyGroups { { "Valid", { 2, 4 } } }));
    }
    void orderedSolidLocalAndDefaultEffects()
    {
        PresetCollection collection = defaultCollection();
        auto& p = collection.presets[0];
        EffectPreset solid;
        solid.id = "local-red";
        solid.type = "solid";
        solid.solidColor = Qt::red;
        p.localEffects = { solid };
        p.effects[DefaultAssignment] = { { DefaultEffectId }, { solid.id, 50 } };
        p.base[1] = { KeyMode::Off };
        const auto d = g615lwDescription();
        auto styles = deviceKeyStyles(p, d, { });
        auto assignments = deviceEffects(p, d, { });
        EffectPalette palette;
        palette.configure(runtimeEffects(collection), activeEffects(p, styles, d, assignments));
        auto colors = palette.colors();
        colors[DefaultEffectId] = Qt::blue;
        auto frame = lightingColors(p, styles, d, colors, assignments);
        QCOMPARE(frame.keys[0], QColor(128, 0, 127));
        QCOMPARE(frame.keys[1], QColor(Qt::black));
        p.effects[DefaultAssignment].move(1, 0);
        assignments = deviceEffects(p, d, { });
        frame = lightingColors(p, deviceKeyStyles(p, d, { }), d, colors, assignments);
        QCOMPARE(frame.keys[0], QColor(Qt::blue));
        // A solid-only default needs no screen captures, and survives palette reset.
        p.effects[DefaultAssignment] = { { solid.id } };
        assignments = deviceEffects(p, d, { });
        palette.configure(runtimeEffects(collection), activeEffects(p, styles, d, assignments));
        QCOMPARE(palette.samplingFps(), 0);
        palette.reset();
        QCOMPARE(palette.colors()[scopedEffectId(p, solid.id)], QColor(Qt::red));
        // Duplicated keyboard presets may edit their local effects independently.
        auto duplicate = p;
        duplicate.id = "copy";
        duplicate.localEffects[0].solidColor = Qt::green;
        collection.presets.append(duplicate);
        const auto definitions = runtimeEffects(collection);
        palette.configure(definitions, { "default:local-red", "copy:local-red" });
        QCOMPARE(palette.colors()["default:local-red"], QColor(Qt::red));
        QCOMPARE(palette.colors()["copy:local-red"], QColor(Qt::green));
        // The same default list also drives devices without per-key control.
        auto uniform = d;
        uniform.capabilities = DeviceCapability::Color;
        const auto& original = collection.presets.first();
        const auto resolved = deviceEffects(original, uniform, { });
        frame = lightingColors(
            original, deviceKeyStyles(original, uniform, { }), uniform, palette.colors(), resolved);
        QCOMPARE(frame.uniform, QColor(Qt::red));
    }
    void shiftLayerInheritsAndRestoresBase()
    {
        KeyboardStyles base;
        base[0] = { KeyMode::Static, Qt::red };
        base[1] = { KeyMode::Static, Qt::green };
        base[2] = { KeyMode::Off, Qt::transparent };
        auto shift = inheritedKeyStyles();
        QVERIFY(serializeKeyStyles(shift, true).isEmpty());
        QCOMPARE(parseKeyStyles({ }, true), shift); // Existing configurations have no Shift overrides.
        QCOMPARE(effectiveKeyStyles(base, shift, true), base);
        shift[1] = { KeyMode::Ambient, Qt::transparent }; // Explicit Ambient overrides Base's static color.
        shift[2] = { KeyMode::Static, Qt::blue };
        shift[3] = { KeyMode::Off, Qt::transparent };
        QCOMPARE(serializeKeyStyles(shift, true),
            (QStringList { "1:ambient:#00000000:", "2:static:#ff0000ff", "3:off:" }));
        QCOMPARE(parseKeyStyles(serializeKeyStyles(shift, true), true), shift);
        QCOMPARE(
            parseKeyStyles({ "65600:off:", "0:static:invalid", "1:unknown:" }, true), inheritedKeyStyles());
        auto expected = base;
        expected[1] = { KeyMode::Ambient, Qt::transparent };
        expected[2] = { KeyMode::Static, Qt::blue };
        expected[3] = { KeyMode::Off, Qt::transparent };
        QCOMPARE(effectiveKeyStyles(base, shift, true), expected);
        QCOMPARE(effectiveKeyStyles(base, shift, false), base);
        base[0].color = Qt::yellow;
        QCOMPARE(effectiveKeyStyles(base, shift, true)[0].color, QColor(Qt::yellow));
    }
    void perKeyPacketsCommitOnceAndKeepZonesIndependent()
    {
        QList<bool> layout(88, true);
        layout.append(QList<bool>(12, false));
        LedFrame frame { QColor(40, 50, 60), 1, true, true, QList<QColor>(88, Qt::red) };
        QCOMPARE(lampArrayPackets(frame, layout),
            (QList<QByteArray> {
                QByteArray::fromHex("050000005700ff0000ff"), QByteArray::fromHex("05015800630028323cff") }));
        for (int id = 0; id < 88; ++id)
            frame.keyColors[id] = QColor(id + 1, 40, 100);
        for (bool lightbar : { true, false }) {
            frame.lightbar = lightbar;
            const auto packets = lampArrayPackets(frame, layout);
            QCOMPARE(packets.size(), 13);
            int id = 0;
            for (int batch = 0; batch < packets.size(); ++batch) {
                const auto& report = packets[batch];
                QCOMPARE(report.size(), 51);
                QCOMPARE(uchar(report[0]), 4);
                QCOMPARE(uchar(report[1]), batch == 12 ? 4 : 8);
                QCOMPARE(uchar(report[2]), batch == 12 ? 1 : 0);
                for (int i = 0; i < uchar(report[1]); ++i, ++id) {
                    QCOMPARE(qFromLittleEndian<quint16>(report.data() + 3 + 2 * i), id);
                    const QColor expected = id < 88 ? frame.keyColors[id]
                        : lightbar                  ? frame.color
                                                    : QColor(Qt::black);
                    QCOMPARE(QColor(uchar(report[19 + 4 * i]), uchar(report[20 + 4 * i]),
                                 uchar(report[21 + 4 * i])),
                        expected);
                    QCOMPARE(uchar(report[22 + 4 * i]), id < 88 || lightbar ? 255 : 0);
                }
            }
            QCOMPARE(id, 100);
        }
        frame.keyboard = false;
        frame.lightbar = true;
        QCOMPARE(lampArrayPackets(frame, layout),
            (QList<QByteArray> {
                QByteArray::fromHex("05000000570000000000"), QByteArray::fromHex("05015800630028323cff") }));
        frame.brightness = 0;
        QCOMPARE(lampArrayPackets(frame, layout),
            QList<QByteArray> { QByteArray::fromHex("05010000630000000000") });
        frame.keyColors.removeLast();
        QVERIFY(lampArrayPackets(frame, layout).isEmpty());
        frame.keyColors.append(QColor());
        QVERIFY(lampArrayPackets(frame, layout).isEmpty());
        frame.keyColors.last() = Qt::red;
        layout[0] = false;
        QVERIFY(lampArrayPackets(frame, layout).isEmpty());
    }
    void individualStripPacketsCommitOnceAndKeepBrightnessPolicy()
    {
        QList<bool> layout(88, true);
        layout.append(QList<bool>(12, false));
        LedFrame frame { Qt::green, 1, true, true, { }, { } };
        for (int i = 0; i < 12; ++i)
            frame.stripColors.append(QColor(20 + i, 40, 60));
        const auto ranges = lampArrayPackets(frame, layout);
        QCOMPARE(ranges.size(), 13);
        QCOMPARE(ranges.first(), QByteArray::fromHex("05000000570000ff00ff"));
        for (int i = 0; i < 12; ++i) {
            const auto packet = ranges[i + 1];
            QCOMPARE(uchar(packet[0]), 5);
            QCOMPARE(uchar(packet[1]), i == 11 ? 1 : 0);
            QCOMPARE(qFromLittleEndian<quint16>(packet.data() + 2), 88 + i);
            QCOMPARE(qFromLittleEndian<quint16>(packet.data() + 4), 88 + i);
            QCOMPARE(QColor(uchar(packet[6]), uchar(packet[7]), uchar(packet[8])), QColor(20 + i, 40, 60));
            QCOMPARE(uchar(packet[9]), 255);
        }
        for (int i = 0; i < 88; ++i)
            frame.keyColors.append(QColor(1 + i, 2, 3));
        const auto packets = lampArrayPackets(frame, layout);
        QCOMPARE(packets.size(), 13);
        int id = 0;
        for (int batch = 0; batch < packets.size(); ++batch) {
            const auto packet = packets[batch];
            QCOMPARE(uchar(packet[0]), 4);
            QCOMPARE(uchar(packet[2]), batch == 12 ? 1 : 0);
            for (int i = 0; i < uchar(packet[1]); ++i, ++id) {
                QCOMPARE(qFromLittleEndian<quint16>(packet.data() + 3 + 2 * i), id);
                const QColor rgb(
                    uchar(packet[19 + 4 * i]), uchar(packet[20 + 4 * i]), uchar(packet[21 + 4 * i]));
                QCOMPARE(rgb, id < 88 ? QColor(1 + id, 2, 3) : QColor(20 + id - 88, 40, 60));
            }
        }
        frame.brightness = 0;
        QCOMPARE(lampArrayPackets(frame, layout),
            QList<QByteArray> { QByteArray::fromHex("05010000630000000000") });
        frame.stripColors.removeLast();
        QVERIFY(lampArrayPackets(frame, layout).isEmpty());
    }
    void stripChangesReachUsbInHardwareOrderWithoutControllerReset()
    {
        Fixture f;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        QList<QColor> strip;
        for (int i = 0; i < 12; ++i)
            strip.append(QColor(30 + i, 50, 70));
        f.client.submit(Qt::green, { }, strip);
        QTRY_COMPARE(f.client.completedFrames(), 1);
        const auto frame = f.usb->frames.last();
        // Independent expected index-to-lamp mapping from descriptor coordinates.
        const QList<int> hardware { 89, 91, 93, 95, 97, 99, 98, 96, 94, 92, 90, 88 };
        for (int i = 0; i < 12; ++i)
            QCOMPARE(frame.stripColors[hardware[i] - 88], strip[i]);
        QCOMPARE(frame.color, QColor(Qt::green));
        QVERIFY(frame.keyColors.isEmpty());
        strip[5] = Qt::red;
        f.client.submit(Qt::green, { }, strip);
        QTRY_COMPARE(f.client.completedFrames(), 2);
        QCOMPARE(f.usb->frames.last().stripColors[11], QColor(Qt::red));
        QCOMPARE(f.usb->releases, 0);
        QVERIFY(f.fake.frames.isEmpty());
        f.client.submit(Qt::green, { }, strip);
        QTest::qWait(100);
        QCOMPARE(f.client.completedFrames(), 2);
    }
    void duplicateEffectNames()
    {
        QVERIFY(!hasConflictingAmbientEffects({ "blur", "ambientled" }));
        QVERIFY(!hasConflictingAmbientEffects({ "ambientled_dev5", "ambientled_other", "ambientled_dev" }));
        QVERIFY(hasConflictingAmbientEffects({ "ambientled", "ambientled_dev4" }));
        QVERIFY(hasConflictingAmbientEffects({ "ambientled_dev5", "ambientled_dev6" }));
        QVERIFY(!isAmbientEffectName("ambientled_dev4_other"));
    }
    void screenColors()
    {
        QCOMPARE(averageColor(QByteArray::fromHex("ff0000ffff0000ff")), QColor(Qt::red));
        QCOMPARE(averageColor(QByteArray::fromHex("000000ff000000ff")), QColor(Qt::black));
        auto mixed = averageColor(QByteArray::fromHex("000000ffffffffff"));
        // A 50% linear-light mixture of white and black is sRGB 188, not 128.
        QVERIFY(qAbs(mixed.red() - 188) <= 1);
        QCOMPARE(mixed.red(), mixed.green());
        QCOMPARE(averageColor(QByteArray::fromHex("ffffffff"), 0.0), QColor(Qt::black));
        const auto gray = averageColor(QByteArray::fromHex("ff0000ff"), 1.0, 0.0);
        QCOMPARE(gray.red(), gray.green());
        QCOMPARE(gray.green(), gray.blue());
        QVERIFY(!averageColor({ }).isValid());
        QVERIFY(!averageColor(QByteArray("abc")).isValid());
    }
    void dominantColorKeepsTheLargestHue()
    {
        DominantColorPicker picker;
        QCOMPARE(picker.select(colorSample({ { Qt::red, 60 }, { Qt::blue, 40 } })), QColor(Qt::red));
        QCOMPARE(picker.select(colorSample({ { Qt::green, 100 } })), QColor(Qt::green));
        const auto shades = colorSample({ { QColor::fromHsv(0, 100, 200), 20 },
            { QColor::fromHsv(0, 150, 200), 20 }, { QColor::fromHsv(0, 250, 200), 20 }, { Qt::blue, 40 } });
        const auto selected = picker.select(shades);
        QVERIFY(selected.red() > selected.green());
        QCOMPARE(selected.green(), selected.blue());
    }
    void dominantColorIgnoresTinyAccentsAndHandlesNeutralScreens()
    {
        DominantColorPicker picker;
        const auto tiny = picker.select(colorSample({ { Qt::blue, 4 }, { QColor(80, 80, 80), 96 } }));
        QCOMPARE(tiny.red(), tiny.green());
        QCOMPARE(tiny.green(), tiny.blue());
        QCOMPARE(picker.select(colorSample({ { Qt::blue, 5 }, { Qt::black, 95 } })), QColor(Qt::blue));
        QCOMPARE(picker.select(colorSample({ { Qt::black, 100 } })), QColor(Qt::black));
        QCOMPARE(picker.select(colorSample({ { Qt::white, 100 } })), QColor(Qt::white));
        QCOMPARE(picker.select(colorSample({ { QColor(80, 80, 80), 100 } })), QColor(80, 80, 80));
        const auto dark = picker.select(colorSample({ { QColor(15, 0, 0), 100 } }));
        QCOMPARE(dark.red(), dark.green());
        QCOMPARE(dark.green(), dark.blue());
    }
    void dominantColorMergesHueWrapAndResistsNearTies()
    {
        DominantColorPicker picker;
        const auto red = picker.select(colorSample({ { QColor::fromHsv(359, 255, 255), 30 },
            { QColor::fromHsv(1, 255, 255), 30 }, { Qt::blue, 40 } }));
        QCOMPARE(red.red(), 255);
        QVERIFY(red.green() <= 5 && red.blue() <= 5);
        QCOMPARE(picker.select(colorSample({ { Qt::red, 51 }, { Qt::blue, 49 } })), QColor(Qt::red));
        const auto nearTie = colorSample({ { Qt::red, 49 }, { Qt::blue, 51 } });
        QCOMPARE(picker.select(nearTie), QColor(Qt::red));
        picker.reset();
        QCOMPARE(picker.select(nearTie), QColor(Qt::blue));
        QCOMPARE(picker.select(colorSample({ { Qt::red, 70 }, { Qt::blue, 30 } })), QColor(Qt::red));
    }
    void dominantColorPreservesColorAdjustments()
    {
        DominantColorPicker picker;
        const auto sample = colorSample({ { Qt::red, 100 } });
        QCOMPARE(picker.select(sample, 0.0), QColor(Qt::black));
        const auto gray = picker.select(sample, 1.0, 0.0);
        QCOMPARE(gray.red(), gray.green());
        QCOMPARE(gray.green(), gray.blue());
        QVERIFY(!picker.select({ }).isValid());
        QVERIFY(!picker.select(QByteArray("abc")).isValid());
    }
    void colorTemperaturePreservesPrimaryColorsAndBrightness()
    {
        for (int temperature = -100; temperature <= 100; ++temperature) {
            for (int value : { 1, 64, 127, 255 }) {
                for (const auto& primary : { QColor(value, 0, 0), QColor(0, value, 0), QColor(0, 0, value) })
                    QCOMPARE(adjustColorTemperature(primary, temperature), primary);
            }
            for (const auto& sample : { QColor(Qt::black), QColor(Qt::white), QColor(210, 180, 120),
                     QColor(10, 40, 120), QColor(90, 35, 70), QColor(64, 64, 64) }) {
                const auto corrected = adjustColorTemperature(sample, temperature);
                QCOMPARE(std::max({ corrected.red(), corrected.green(), corrected.blue() }),
                    std::max({ sample.red(), sample.green(), sample.blue() }));
            }
        }
        QVERIFY(!adjustColorTemperature(QColor(), -50).isValid());
        QCOMPARE(adjustColorTemperature(Qt::black, -100), QColor(Qt::black));
        const QColor sand(210, 180, 120);
        QCOMPARE(adjustColorTemperature(sand, 0), sand);
        QCOMPARE(adjustColorTemperature(sand, -999), adjustColorTemperature(sand, -100));
        QCOMPARE(adjustColorTemperature(sand, 999), adjustColorTemperature(sand, 100));
    }
    void colorTemperatureBalancesMixedColorsAndNightLight()
    {
        const QColor sand(210, 180, 120);
        const auto warm = adjustColorTemperature(sand, -50);
        const auto warmer = adjustColorTemperature(sand, -100);
        QCOMPARE(warm.red(), sand.red());
        QVERIFY(warm.green() < sand.green());
        QVERIFY(warm.blue() < sand.blue());
        QVERIFY(warmer.blue() < warm.blue());
        // Temperature applies after the black-screen floor, so the night light
        // has the requested white balance without dropping below the minimum.
        const auto floor = mapBrightness(Qt::black, 25, 50);
        const auto warmFloor = adjustColorTemperature(floor, -50);
        const auto coolFloor = adjustColorTemperature(floor, 50);
        QCOMPARE(warmFloor.red(), 64);
        QVERIFY(warmFloor.red() > warmFloor.green());
        QVERIFY(warmFloor.green() > warmFloor.blue());
        QCOMPARE(coolFloor.blue(), 64);
        QVERIFY(coolFloor.blue() > coolFloor.green());
        QVERIFY(coolFloor.green() > coolFloor.red());
        const QList<bool> layout { true, false };
        const auto packets = lampArrayPackets({ warmFloor, 1, true, true }, layout);
        QCOMPARE(packets.size(), 1); // Both zones share the calibrated color.
        QCOMPARE(uchar(packets[0][6]), warmFloor.red());
        QCOMPARE(uchar(packets[0][7]), warmFloor.green());
        QCOMPARE(uchar(packets[0][8]), warmFloor.blue());
        QCOMPARE(uchar(packets[0][9]), 255);
    }
    void brightnessMappingDimsMidtonesAndSharesRgbAcrossZones()
    {
        QCOMPARE(mapBrightness(Qt::black, 20, 60), QColor(51, 51, 51));
        QCOMPARE(mapBrightness(Qt::white, 20, 60), QColor(153, 153, 153));
        QCOMPARE(mapBrightness(QColor(100, 50, 0), 20, 60), QColor(91, 46, 0));
        QCOMPARE(mapBrightness(QColor(120, 60, 30), 0, 20), QColor(24, 12, 6));
        QCOMPARE(mapBrightness(QColor(120, 60, 30), 0, 100), QColor(120, 60, 30));
        QCOMPARE(mapBrightness(Qt::black, 90, 20), QColor(51, 51, 51));
        QCOMPARE(mapBrightness(Qt::white, 0, 0), QColor(Qt::black));
        QVERIFY(!mapBrightness(QColor(), 20, 60).isValid());
        // Both zones share RGB; the system's Off setting still disables all lamps.
        const auto rgb = mapBrightness(QColor(120, 60, 30), 0, 20);
        QList<bool> layout(88, true);
        layout.append(QList<bool>(12, false));
        QCOMPARE(lampArrayPackets({ rgb, 1, true, true }, layout),
            QList<QByteArray> { QByteArray::fromHex("050100006300180c06ff") });
        QCOMPARE(lampArrayPackets({ rgb, 0, true, true }, layout),
            QList<QByteArray> { QByteArray::fromHex("05010000630000000000") });
    }
    void brightnessLimits()
    {
        QCOMPARE(constrainBrightness(Qt::black, 50, 50), QColor(128, 128, 128));
        QCOMPARE(constrainBrightness(Qt::black, 20, 60), QColor(51, 51, 51));
        QCOMPARE(constrainBrightness(Qt::white, 20, 60), QColor(153, 153, 153));
        QCOMPARE(constrainBrightness(QColor(10, 5, 0), 20, 60), QColor(51, 26, 0));
        QCOMPARE(constrainBrightness(QColor(250, 100, 50), 20, 60), QColor(153, 61, 31));
        QCOMPARE(constrainBrightness(QColor(100, 50, 10), 20, 60), QColor(100, 50, 10));
        QCOMPARE(constrainBrightness(Qt::black, 0, 100), QColor(Qt::black));
        QCOMPARE(constrainBrightness(Qt::white, 0, 0), QColor(Qt::black));
        QCOMPARE(constrainBrightness(Qt::black, 90, 20), QColor(51, 51, 51));
        QVERIFY(!constrainBrightness(QColor(), 20, 60).isValid());
    }
    void brightnessFloorSurvivesHueTransitions()
    {
        ColorSmoother smoother;
        smoother.setTarget(constrainBrightness(Qt::red, 20, 20));
        smoother.step(17, 400);
        for (const auto& target : { QColor(Qt::blue), QColor(Qt::black), QColor(Qt::white) }) {
            smoother.setTarget(constrainBrightness(target, 20, 20));
            for (int i = 0; i < 100; ++i) {
                const auto output = constrainBrightness(*smoother.step(17, 400), 20, 20);
                QCOMPARE(std::max({ output.red(), output.green(), output.blue() }), 51);
            }
        }
    }
    void regionGeometry()
    {
        QCOMPARE(sampleRegion({ -1920, 80, 1920, 1080 }, true, 25), QRect(-1920, 890, 1920, 270));
        QCOMPARE(sampleRegion({ 100, 200, 500, 3 }, true, 1), QRect(100, 202, 500, 1));
        QCOMPARE(sampleRegion({ 100, 200, 500, 300 }, false, 25), QRect(100, 200, 500, 300));
    }
    void smoothingUsesElapsedTime()
    {
        ColorSmoother one, four;
        QVERIFY(!one.step(50, 200));
        for (auto* s : { &one, &four }) {
            s->setTarget(Qt::red);
            s->step(50, 200);
            s->setTarget(Qt::blue);
        }
        const auto a = one.step(200, 200);
        for (int i = 0; i < 3; ++i)
            four.step(50, 200);
        const auto b = four.step(50, 200);
        QCOMPARE(a, b);
        QVERIFY(qAbs(a->red() - 94) <= 1);
        QVERIFY(qAbs(a->blue() - 161) <= 1);
        one.reset();
        QVERIFY(!one.step(50, 200));
    }
    void smoothsBetweenSparseSnapshots()
    {
        constexpr int interval = 50; // Six intermediate colors between sparse snapshots at 20 FPS.
        ColorSmoother smoother;
        smoother.setTarget(Qt::red);
        QCOMPARE(smoother.step(interval, DefaultSmoothingMs), QColor(Qt::red));
        smoother.setTarget(Qt::blue); // The next snapshot; no more targets for 300 ms.
        QColor previous(Qt::red);
        int intermediateColours = 0;
        for (int elapsed = interval; elapsed <= 300; elapsed += interval) {
            const auto next = smoother.step(interval, DefaultSmoothingMs);
            QVERIFY(next);
            QVERIFY(next->red() < previous.red());
            QVERIFY(next->blue() > previous.blue());
            QVERIFY(next->red() > 0 && next->blue() < 255);
            ++intermediateColours;
            previous = *next;
        }
        QCOMPARE(intermediateColours, 6);
    }
    void asyncWriteAndRestore()
    {
        Fixture f;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        QVERIFY(f.usb->frames.isEmpty());
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.client.completedFrames(), 1);
        QCOMPARE(f.usb->frames.size(), 1);
        QCOMPARE(f.usb->frames[0].color, QColor(Qt::red));
        QCOMPARE(f.usb->frames[0].brightness, 2);
        QVERIFY(f.fake.frames.isEmpty()); // Streaming never uses Aura factory commands.
        f.client.submit(Qt::red);
        QTest::qWait(80);
        QCOMPARE(f.usb->frames.size(), 1);
        f.client.setEnabled(false);
        QTRY_COMPARE(f.fake.displayedEffects.size(), 1);
        QCOMPARE(f.usb->releases, 1);
        QVERIFY(!f.usb->active);
        QCOMPARE(f.fake.frames.size(), 2);
        QCOMPARE(f.fake.frames.last(), Packets { setPacket() });
        QCOMPARE(f.fake.displayedEffects.last(), *effectPacket(originalEffect()));
        QCOMPARE(f.client.completedFrames(), 1);
        QCOMPARE(f.fake.unexpected, 0);
    }
    void coalescesSlowReplies()
    {
        Fixture f;
        f.client.setLedFramesPerSecond(20);
        f.usb->delay = 160;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.usb->frames.size(), 1);
        for (int i = 0; i < 50; ++i)
            f.client.submit(Qt::green);
        f.client.submit(Qt::blue);
        QTRY_COMPARE(f.client.completedFrames(), 2);
        QCOMPARE(f.usb->frames.size(), 2);
        QCOMPARE(f.usb->frames[1].color, QColor(Qt::blue));
        QVERIFY(f.fake.frames.isEmpty());
        QVERIFY(f.usb->times[1] - f.usb->times[0] >= 200);
        f.client.setEnabled(false);
        QTest::qWait(80);
        QVERIFY(f.fake.frames.isEmpty()); // Restore must wait for the USB release.
        QTRY_COMPARE(f.fake.frames.size(), 2);
    }
    void keyChangesAreCoalescedEvenWhenAmbientColorIsUnchanged()
    {
        Fixture f;
        f.usb->delay = 80;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        QList<QColor> keys(88, Qt::blue);
        f.client.submit(Qt::blue, keys);
        QTRY_COMPARE(f.usb->frames.size(), 1);
        keys[0] = Qt::red;
        f.client.submit(Qt::blue, keys);
        keys[4] = Qt::green;
        f.client.submit(Qt::blue, keys);
        QTRY_COMPARE(f.client.completedFrames(), 2);
        QCOMPARE(f.usb->frames.size(), 2);
        QCOMPARE(f.usb->frames.last().keyColors, keys);
        QCOMPARE(f.usb->frames.first().keyColors[0], QColor(Qt::blue));
        f.client.submit(Qt::blue, keys);
        QTest::qWait(100);
        QCOMPARE(f.usb->frames.size(), 2);
    }
    void shiftLayerUpdatesOnStaticScreenWithoutResettingTransport()
    {
        Fixture f;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        KeyboardStyles base;
        auto shift = inheritedKeyStyles();
        shift[36] = { KeyMode::Static, Qt::red };
        int count = 0;
        for (bool held : { false, true, false }) {
            const auto colors = lightingColors(LightingPreset(), effectiveKeyStyles(base, shift, held),
                g615lwDescription(), { { DefaultEffectId, Qt::blue } });
            f.client.submit(colors.uniform, colors.keys);
            ++count;
            QTRY_COMPARE(f.client.completedFrames(), count);
            const auto& frame = f.usb->frames.last();
            QCOMPARE(frame.color, QColor(Qt::blue)); // Lightbar stays on the same ambient color.
            if (held) {
                QCOMPARE(frame.keyColors[36], QColor(Qt::red));
                QCOMPARE(frame.keyColors[0], QColor(Qt::blue));
            } else {
                QVERIFY(frame.keyColors.isEmpty()); // All-Ambient broadcast restored on release.
            }
            QCOMPARE(f.usb->releases, 0);
            QVERIFY(f.fake.frames.isEmpty());
        }
    }
    void transitionsNeverSendFactoryCommands()
    {
        Fixture f;
        f.client.setLedFramesPerSecond(20);
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        for (int frame = 0; frame < 6; ++frame) {
            const QColor color(20 + frame * 20, 80, 180);
            f.client.submit(color);
            QTRY_COMPARE(f.client.completedFrames(), frame + 1);
            QCOMPARE(f.usb->frames.size(), frame + 1);
            QCOMPARE(f.usb->frames.last().color, color);
            QVERIFY(f.usb->frames.last().keyboard && f.usb->frames.last().lightbar);
            QVERIFY(f.fake.frames.isEmpty());
            QCOMPARE(f.usb->releases, 0);
            if (frame > 0)
                QVERIFY(f.usb->times[frame] - f.usb->times[frame - 1] >= 50);
        }
    }
    void supportsFasterColorUpdates()
    {
        Fixture f;
        f.client.setLedFramesPerSecond(60);
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        QTimer producer;
        producer.setTimerType(Qt::PreciseTimer);
        int hue = 0;
        connect(&producer, &QTimer::timeout, this,
            [&] { f.client.submit(QColor::fromHsv(hue++ % 360, 255, 128)); });
        producer.start(1);
        QTest::qWait(600);
        producer.stop();
        // The old 20 FPS ceiling permits at most 13 writes in this window.
        QVERIFY2(f.usb->frames.size() >= 20, qPrintable(QString::number(f.usb->frames.size())));
        for (int i = 1; i < f.usb->times.size(); ++i)
            QVERIFY(f.usb->times[i] - f.usb->times[i - 1] >= 17);
        QVERIFY(f.fake.frames.isEmpty());
    }
    void updateRateChangeReschedulesPendingColor()
    {
        Fixture f;
        f.client.setLedFramesPerSecond(2);
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.client.completedFrames(), 1);
        f.client.submit(Qt::blue);
        QTest::qWait(100);
        QCOMPARE(f.usb->frames.size(), 1);
        f.client.setLedFramesPerSecond(60);
        QCOMPARE(f.usb->frames.size(), 2);
        QCOMPARE(f.usb->frames.last().color, QColor(Qt::blue));
    }
    void respectsBrightnessOff()
    {
        Fixture f;
        f.fake.props = properties(0);
        f.client.start();
        QTRY_VERIFY(f.client.status().startsWith("Connected"));
        QVERIFY(!f.client.ready());
        f.client.submit(Qt::red);
        QTest::qWait(80);
        QVERIFY(f.usb->frames.isEmpty());
        f.fake.brightness(2);
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::green);
        QTRY_COMPARE(f.client.completedFrames(), 1);
        f.fake.brightness(0);
        QTRY_VERIFY(!f.client.ready());
        f.client.submit(Qt::blue);
        QTRY_COMPARE(f.usb->releases, 1);
        QTest::qWait(80);
        QCOMPARE(f.usb->frames.size(), 1);
        QVERIFY(f.fake.frames.isEmpty());
    }
    void brightnessOffDuringStreamDropsPendingFrame()
    {
        Fixture f;
        f.usb->delay = 160;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.usb->frames.size(), 1);
        f.client.submit(Qt::blue);
        f.fake.brightness(0);
        QTRY_VERIFY(!f.client.ready());
        QTRY_VERIFY(!f.usb->active);
        QCOMPARE(f.usb->frames.size(), 1);
        QVERIFY(f.fake.frames.isEmpty());
        QCOMPARE(f.client.completedFrames(), 1);
    }
    void brightnessOffDuringRestoreSkipsSet()
    {
        Fixture f;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.client.completedFrames(), 1);
        f.fake.replyDelay = 160;
        f.client.setEnabled(false);
        QTRY_COMPARE(f.fake.frames.size(), 1);
        f.fake.brightness(0);
        QTest::qWait(200);
        QCOMPARE(f.fake.frames.size(), 1);
        QVERIFY(f.fake.displayedEffects.isEmpty());
    }
    void failedRestore_data()
    {
        QTest::addColumn<int>("failedCall");
        QTest::newRow("effect") << 1;
        QTest::newRow("set") << 2;
    }
    void failedRestore()
    {
        QFETCH(int, failedCall);
        Fixture f;
        f.fake.failAt = failedCall;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.client.completedFrames(), 1);
        f.client.setEnabled(false);
        QTRY_VERIFY(f.client.status().contains("Aura restore failed"));
        QVERIFY(!f.client.ready());
        QTest::qWait(100);
        QCOMPARE(f.fake.frames.size(), failedCall);
        QVERIFY(f.fake.displayedEffects.isEmpty());
        QCOMPARE(f.client.completedFrames(), 1);
    }
    void failedStreamDropsPendingFrame()
    {
        Fixture f;
        f.usb->failAt = 1;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::red);
        f.client.submit(Qt::blue);
        QTRY_VERIFY(f.client.status().contains("LampArray USB failed"));
        QVERIFY(!f.client.ready());
        QTRY_VERIFY(!f.usb->active);
        QCOMPARE(f.usb->frames.size(), 1);
        QCOMPARE(f.client.completedFrames(), 0);
    }
    void destructionRestoresWithSet()
    {
        FakeAsusd fake;
        auto usb = std::make_shared<UsbState>();
        {
            AsusdClient client(
                nullptr, QDBusConnection::sessionBus(), Service, std::make_unique<FakeUsb>(usb));
            client.start();
            QTRY_VERIFY(client.ready());
            client.submit(Qt::red);
            QTRY_COMPARE(client.completedFrames(), 1);
        }
        QVERIFY(!usb->active);
        QCOMPARE(usb->shutdowns, 1);
        QTRY_COMPARE(fake.displayedEffects.size(), 1);
        QCOMPARE(fake.frames.size(), 2);
        QCOMPARE(fake.displayedEffects.last(), *effectPacket(originalEffect()));
    }
    void missingTargetStaysIdle()
    {
        Fixture f;
        f.client.start("/xyz/ljones/aura/19b6_missing");
        QTRY_VERIFY(f.client.status().contains("No compatible"));
        QVERIFY(!f.client.ready());
        f.client.submit(Qt::red);
        QTest::qWait(60);
        QVERIFY(f.usb->frames.isEmpty());
        QVERIFY(f.fake.frames.isEmpty());
    }
    void missingPowerSettingsStayIdle()
    {
        Fixture f;
        f.fake.props.remove("LedPower");
        f.client.start();
        QTRY_VERIFY(f.client.status().contains("power settings"));
        QVERIFY(!f.client.ready());
        f.client.submit(Qt::red);
        QVERIFY(f.usb->frames.isEmpty());
    }
    void lightbarToggleChangesUsbFrame()
    {
        Fixture f;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.client.completedFrames(), 1);
        auto power = f.fake.props.value("LedPower").value<AuraPower>();
        power.states[1].awake = false;
        auto signal
            = QDBusMessage::createSignal(Path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
        signal << QString::fromLatin1(AuraInterface)
               << QVariantMap { { "LedPower", QVariant::fromValue(power) } } << QStringList { };
        QSignalSpy changed(&f.client, &AsusdClient::readinessChanged);
        f.fake.bus.send(signal);
        QTRY_COMPARE(changed.size(), 1);
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.client.completedFrames(), 2);
        QVERIFY(f.usb->frames.last().keyboard);
        QVERIFY(!f.usb->frames.last().lightbar);
        QCOMPARE(f.usb->frames.last().color, QColor(Qt::red));
        QVERIFY(f.fake.frames.isEmpty());
    }
    void daemonRestartDropsStaleFrames()
    {
        Fixture f;
        f.usb->delay = 250;
        f.client.start();
        QTRY_VERIFY(f.client.ready());
        f.client.submit(Qt::red);
        QTRY_COMPARE(f.usb->frames.size(), 1);
        f.client.submit(Qt::blue);
        QVERIFY(f.fake.bus.unregisterService(Service));
        QTRY_VERIFY(!f.client.ready());
        QVERIFY(f.fake.bus.registerService(Service));
        QTRY_VERIFY(f.client.ready());
        QTest::qWait(300);
        QCOMPARE(f.usb->frames.size(), 1);
        f.usb->delay = 0;
        f.client.submit(Qt::green);
        QTRY_COMPARE(f.client.completedFrames(), 1);
        QCOMPARE(f.usb->frames.size(), 2);
        QCOMPARE(f.usb->frames.last().color, QColor(Qt::green));
        QVERIFY(f.fake.frames.isEmpty());
        f.client.setEnabled(false);
        QTRY_COMPARE(f.fake.frames.size(), 2);
    }
};
QTEST_GUILESS_MAIN(AmbientTest)
#include "test_ambient.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver_loader.h"
#include "linux_leds.h"
#include "usb_lamps.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <atomic>

using namespace Ambient;
namespace
{
// Independent HID fixture: standard usages, deliberately non-default report IDs,
// two MultiUpdate slots and output reports rather than feature-only updates.
QByteArray descriptorBytes()
{
    return QByteArray::fromHex("05590901a101"
                               "85090902a1020903150027ffff000075109501b103"
                               "0904090509060907090827ffffff7f75209505b103c0"
                               "850a0920a102092127ffff000075109501b102c0"
                               "850b0922a102092175109501b102"
                               "0923092409250927092627ffffff7f75209505b102"
                               "09280929092a092b092c092d26ff0075089506b102c0"
                               "850c0950a102090309552502750895029102"
                               "092127ffff0000751095029102"
                               "0951095209530954095109520953095426ff00750895089102c0"
                               "850e0960a1020955250175089501b102"
                               "0961096227ffff000075109502b102"
                               "095109520953095426ff0075089504b102c0"
                               "850f0970a1020971250175089501b102c0c0");
}
QByteArray arrayBytes() { return QByteArray::fromHex("090300409c000030750000000000000100000010270000"); }
QByteArray lampBytes(int id, bool control)
{
    QByteArray bytes(29, '\0');
    bytes[0] = 11;
    qToLittleEndian<quint16>(id, bytes.data() + 1);
    qToLittleEndian<quint32>((id + 1) * 19000, bytes.data() + 3);
    qToLittleEndian<quint32>(control ? 1 : 2, bytes.data() + 19);
    bytes[23] = char(255);
    bytes[24] = 127;
    bytes[25] = 31;
    bytes[26] = 16;
    bytes[27] = 1;
    bytes[28] = control ? (id == 0 ? 26 : 4) : 0;
    return bytes;
}
void put(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Cannot write fixture");
}
struct Counters {
    std::atomic<int> writes = 0, closes = 0, lastRed = 0;
};
class SlowSession : public Generic::Session
{
public:
    explicit SlowSession(std::shared_ptr<Counters> counters)
        : m_counters(std::move(counters))
    {
    }
    QString open() override { return { }; }
    QString write(const DeviceFrame& frame) override
    {
        QThread::msleep(20);
        ++m_counters->writes;
        m_counters->lastRed = frame.uniform.red();
        return { };
    }
    void close() override { ++m_counters->closes; }

private:
    std::shared_ptr<Counters> m_counters;
};
}
class GenericDriversTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_runtime;
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_runtime.isValid());
        QFile::setPermissions(
            m_runtime.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("XDG_RUNTIME_DIR", m_runtime.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
    }
    void sysfsDiscoveryIsReadOnlyAndDoesNotInventPerKeyColor()
    {
        QTemporaryDir root;
        const auto path = root.path() + "/mock::kbd_backlight";
        put(path + "/max_brightness", "3\n");
        put(path + "/brightness", "2\n");
        put(root.path() + "/input3::capslock/max_brightness", "1");
        put(root.path() + "/input3::capslock/brightness", "1");
        const auto devices = LinuxLeds::discover(root.path(), { });
        QCOMPARE(devices.size(), 1);
        const auto& d = devices.first();
        QVERIFY(d.description.capabilities.testFlag(DeviceCapability::Brightness));
        QCOMPARE(d.options.value("BrightnessSteps").toInt(), 3);
        QVERIFY(!d.description.capabilities.testFlag(DeviceCapability::PerKeyColor));
        QVERIFY(d.description.keys.size() > 60);
        QVERIFY(validateDescription(d.description).isEmpty());
        QCOMPARE(Generic::readFile(path + "/brightness"), QByteArray("2"));
        QCOMPARE(LinuxLeds::discover(root.path(), { }).first().id, d.id);
    }
    void sysfsBrightnessPreservesOffAndRestoresOwnedValues()
    {
        QTemporaryDir root;
        put(root.path() + "/max_brightness", "3");
        put(root.path() + "/brightness", "3");
        LinuxLeds::Session session(root.path());
        QVERIFY(session.open().isEmpty());
        QVERIFY(session.write({ QColor(1, 1, 1), { }, { } }).isEmpty());
        QCOMPARE(Generic::readFile(root.path() + "/brightness"), QByteArray("0"));
        session.close();
        QCOMPARE(Generic::readFile(root.path() + "/brightness"), QByteArray("3"));
        LinuxLeds::Session second(root.path());
        QVERIFY(second.open().isEmpty());
        QVERIFY(second.write({ QColor(128, 128, 128), { }, { } }).isEmpty());
        QCOMPARE(Generic::readFile(root.path() + "/brightness"), QByteArray("2"));
        put(root.path() + "/brightness", "0"); // KDE switches hardware Off.
        QVERIFY(second.write({ Qt::white, { }, { } }).isEmpty());
        QCOMPARE(Generic::readFile(root.path() + "/brightness"), QByteArray("0"));
        second.close();
        QCOMPARE(Generic::readFile(root.path() + "/brightness"), QByteArray("0"));
    }
    void sysfsBrightnessUsesTheWholeRangeAndCanLeaveItsOwnOff()
    {
        QTemporaryDir root;
        put(root.path() + "/max_brightness", "3");
        put(root.path() + "/brightness", "1");
        LinuxLeds::Session session(root.path());
        QVERIFY(session.open().isEmpty());
        const QList<int> percentages { 0, 33, 67, 100, 67, 33, 0, 100 };
        const QList<int> levels { 0, 1, 2, 3, 2, 1, 0, 3 };
        for (int i = 0; i < percentages.size(); ++i) {
            const auto v = percentages[i] / 100.0;
            QVERIFY(session.write({ QColor::fromRgbF(v, v, v), { }, { } }).isEmpty());
            QCOMPARE(Generic::readFile(root.path() + "/brightness"), QByteArray::number(levels[i]));
        }
        session.close();
        QCOMPARE(Generic::readFile(root.path() + "/brightness"), QByteArray("1"));
    }
    void deniedBrightnessUsesSessionWriterAndRestoresOwnedValues()
    {
        QTemporaryDir root;
        const auto path = root.path() + "/brightness";
        put(root.path() + "/max_brightness", "3");
        put(path, "1");
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
        QList<uint> writes;
        LinuxLeds::Session session(root.path(), [&](const QString& device, uint value) {
            if (device != root.path())
                return QStringLiteral("Wrong device");
            writes.append(value);
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            put(path, QByteArray::number(value));
            QFile::setPermissions(path, QFileDevice::ReadOwner);
            return QString();
        });
        QVERIFY(session.open().isEmpty());
        QVERIFY(session.write({ Qt::white, { }, { } }).isEmpty());
        QCOMPARE(writes, QList<uint> { 3 });
        QCOMPARE(Generic::readFile(path), QByteArray("3"));
        QVERIFY(session.write({ Qt::white, { }, { } }).isEmpty());
        QCOMPARE(writes.size(), 1); // No redundant D-Bus requests.
        session.close();
        QCOMPARE(writes, (QList<uint> { 3, 1 }));
        QCOMPARE(Generic::readFile(path), QByteArray("1"));
    }
    void sessionWriterErrorsAreReportedWithoutClaimingTheWrite()
    {
        QTemporaryDir root;
        const auto path = root.path() + "/brightness";
        put(root.path() + "/max_brightness", "3");
        put(path, "1");
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
        int writes = 0;
        LinuxLeds::Session session(root.path(), [&](const QString&, uint) {
            ++writes;
            return QStringLiteral("Session is not active");
        });
        QVERIFY(session.open().isEmpty());
        const auto error = session.write({ Qt::white, { }, { } });
        QVERIFY2(error.contains("logind: Session is not active"), qPrintable(error));
        session.close();
        QCOMPARE(writes, 1);
        QCOMPARE(Generic::readFile(path), QByteArray("1"));
    }
    void multicolorDiscoversPartialRgbAndChannelResolution()
    {
        QTemporaryDir root;
        const auto path = root.path() + "/mock:multi:strip";
        put(path + "/max_brightness", "255");
        put(path + "/brightness", "100");
        put(path + "/multi_index", "blue red");
        put(path + "/multi_intensity", "2 3");
        put(path + "/multi_max_intensity", "7 15");
        const auto devices = LinuxLeds::discover(root.path(), { });
        QCOMPARE(devices.size(), 1);
        QVERIFY(devices[0].description.capabilities.testFlag(DeviceCapability::Color));
        QCOMPARE(devices[0].options["ColorLevels"].toMap(), (QVariantMap { { "blue", 7 }, { "red", 15 } }));
        LinuxLeds::Session session(path);
        QVERIFY(session.open().isEmpty());
        QVERIFY(session.write({ QColor(128, 255, 64), { }, { } }).isEmpty());
        QCOMPARE(Generic::readFile(path + "/multi_intensity"), QByteArray("2 8"));
        QCOMPARE(Generic::readFile(path + "/brightness"), QByteArray("100"));
        session.close();
        QCOMPARE(Generic::readFile(path + "/multi_intensity"), QByteArray("2 3"));
    }
    void multicolorUsesReportedChannelOrderAndLeavesBrightnessAlone()
    {
        QTemporaryDir root;
        put(root.path() + "/max_brightness", "255");
        put(root.path() + "/brightness", "7");
        put(root.path() + "/multi_index", "green blue red");
        put(root.path() + "/multi_intensity", "10 20 30");
        put(root.path() + "/multi_max_intensity", "100 200 255");
        LinuxLeds::Session session(root.path());
        QVERIFY(session.open().isEmpty());
        QVERIFY(session.write({ QColor(40, 80, 160), { }, { } }).isEmpty());
        QCOMPARE(Generic::readFile(root.path() + "/multi_intensity"), QByteArray("31 125 40"));
        QCOMPARE(Generic::readFile(root.path() + "/brightness"), QByteArray("7"));
        session.close();
        QCOMPARE(Generic::readFile(root.path() + "/multi_intensity"), QByteArray("10 20 30"));
    }
    void parsesStandardHidReportsAndCommitsOnlyCompleteFrames()
    {
        using namespace HidLampArray;
        const auto descriptor = parseDescriptor(descriptorBytes());
        QVERIFY(descriptor);
        QCOMPARE(descriptor->reports[0x50].id, 12);
        QCOMPARE(descriptor->reports[0x50].type, 2);
        QVERIFY(!descriptor->hasInput);
        auto array = arrayAttributes(*descriptor, arrayBytes());
        QVERIFY(array);
        QCOMPARE(array->count, 3);
        QCOMPARE(array->minimumIntervalUs, 10000u);
        for (int id = 0; id < 3; ++id) {
            const auto lamp = lampAttributes(*descriptor, lampBytes(id, id < 2), id);
            QVERIFY(lamp);
            array->lamps.append(*lamp);
        }
        const auto layout = description(*array, "fixture", "Fixture");
        QVERIFY(validateDescription(layout).isEmpty());
        QCOMPARE(layout.keys.size(), 2);
        QCOMPARE(layout.keys[0].label, QString("W"));
        QCOMPARE(layout.keys[1].label, QString("A"));
        QCOMPARE(layout.strips.first().elements.first().id, 2);
        DeviceFrame frame { Qt::blue, { Qt::red, Qt::green }, { } };
        const auto packets = updates(*descriptor, *array, layout, frame);
        QCOMPARE(packets.size(), 2);
        QCOMPARE(packets[0].bytes.toHex(), QByteArray("0c020000000100ff000010007f0010"));
        QCOMPARE(packets[1].bytes.toHex(), QByteArray("0c01010200000000001f1000000000"));
        auto rangeOnly = *descriptor;
        rangeOnly.reports.remove(0x50);
        const auto ranges = updates(rangeOnly, *array, layout, frame);
        QCOMPARE(ranges.size(), 3);
        QCOMPARE(ranges.first().bytes.toHex(), QByteArray("0e0000000000ff000010"));
        QCOMPARE(ranges.last().bytes.toHex(), QByteArray("0e010200020000001f10"));
        frame.keys.removeLast();
        QVERIFY(updates(*descriptor, *array, layout, frame).isEmpty());
    }
    void rejectsTruncatedDescriptorsReportsAndInvalidAttributes()
    {
        using namespace HidLampArray;
        const auto bytes = descriptorBytes();
        for (int i = 0; i < bytes.size(); ++i)
            QVERIFY(!parseDescriptor(bytes.left(i)));
        QVERIFY(!parseDescriptor(QByteArray(65537, '\0')));
        const auto d = *parseDescriptor(bytes);
        QVERIFY(!arrayAttributes(d, arrayBytes().chopped(1)));
        auto invalid = arrayBytes();
        invalid[1] = 0;
        invalid[2] = 0;
        QVERIFY(!arrayAttributes(d, invalid));
        QVERIFY(!lampAttributes(d, lampBytes(0, true), 1));
        invalid = lampBytes(0, true);
        invalid[27] = 2;
        QVERIFY(!lampAttributes(d, invalid, 0));
        auto report = d.reports[0x70];
        auto data = report.empty();
        QVERIFY(!report.set(data, 0x71, 2));
        QVERIFY(!readCachedArray(d, "{\"version\":1,\"lamps\":[]}"));
    }
    void usbDiscoveryNeedsNoDeviceAccessAndSurvivesHidUnbinding()
    {
        QTemporaryDir root;
        const auto usb = root.path() + "/usb/1-2";
        const auto iface = usb + "/1-2:1.3";
        put(usb + "/idVendor", "1234");
        put(usb + "/idProduct", "5678");
        put(usb + "/bcdDevice", "0100");
        put(usb + "/devnum", "4");
        put(usb + "/removable", "removable");
        put(usb + "/product", "Fixture keyboard");
        put(iface + "/bInterfaceClass", "03");
        put(iface + "/bInterfaceNumber", "03");
        put(iface + "/hid/report_descriptor", descriptorBytes());
        const auto bus = root.path() + "/bus";
        QVERIFY(QDir().mkpath(bus));
        QVERIFY(QFile::link(iface, bus + "/1-2:1.3"));
        const auto cache = root.path() + "/cache";
        const auto devices = HidLampArray::discover(bus, cache);
        QCOMPARE(devices.size(), 1);
        QVERIFY(!devices[0].description.capabilities.testFlag(DeviceCapability::PerKeyColor));
        QVERIFY(!QFile::exists(cache)); // Discovery must not create or alter files.
        const auto key = devices[0].options.value("DescriptorKey").toString();
        put(cache + "/descriptor-" + key + ".bin", descriptorBytes());
        QVERIFY(QFile::remove(iface + "/hid/report_descriptor"));
        const auto unbound = HidLampArray::discover(bus, cache);
        QCOMPARE(unbound.size(), 1);
        QCOMPARE(unbound[0].id, devices[0].id);
        QCOMPARE(unbound[0].description.id, devices[0].description.id);
    }
    void customGeometryCannotChangeHardwareAddressing()
    {
        DeviceDescription d { "fixture", "Fixture", DeviceCapability::Color | DeviceCapability::PerKeyColor,
            { { 7, "W", { 0, 0, 1, 1 } } }, { } };
        QTemporaryDir root;
        const auto path = root.path() + "/layout.json";
        put(path,
            "{\"version\":1,\"model\":\"fixture\",\"bodyColor\":\"#333333\",\"keys\":[{\"id\":7,\"label\":"
            "\"Key\",\"rect\":[2,3,4,5]}]}");
        QVERIFY(Generic::applyLayout(d, path));
        QCOMPARE(d.keys.first().id, 7);
        QCOMPARE(d.keys.first().rect, QRectF(2, 3, 4, 5));
        QCOMPARE(d.bodyColor, QColor("#333333"));
        put(path, "{\"version\":1,\"model\":\"fixture\",\"keys\":[{\"id\":8,\"rect\":[0,0,1,1]}]}");
        QVERIFY(!Generic::applyLayout(d, path));
        QCOMPARE(d.keys.first().id, 7);
        QCOMPARE(d.keys.first().rect, QRectF(2, 3, 4, 5));
    }
    void discoveryKeepsCompletedResultsPendingUntilCollected()
    {
        std::atomic<bool> scanned = false;
        Generic::DiscoveryCache cache([&](const QStringList&) {
            DeviceInstance device;
            device.id = QStringLiteral("fixture");
            scanned = true;
            return QList<DeviceInstance> { device };
        });
        QVERIFY(cache.discover({ }).isEmpty());
        QTRY_VERIFY(scanned.load());
        QVERIFY(cache.pending());
        QList<DeviceInstance> devices;
        QTRY_VERIFY(!(devices = cache.discover({ })).isEmpty());
        QCOMPARE(devices.first().id, QStringLiteral("fixture"));
        QVERIFY(!cache.pending());
    }
    void workerCoalescesFramesAndReleasesOnDisable()
    {
        auto counters = std::make_shared<Counters>();
        Generic::Backend backend([counters] { return std::make_unique<SlowSession>(counters); });
        backend.start({ });
        backend.setEnabled(true);
        backend.setLedFramesPerSecond(60);
        for (int red = 0; red <= 100; ++red)
            backend.submit({ QColor(red, 0, 0), { }, { } });
        QTRY_COMPARE(counters->lastRed.load(), 100);
        QCOMPARE(counters->writes.load(), 2);
        backend.setEnabled(false);
        QTRY_COMPARE(counters->closes.load(), 1);
        QVERIFY(!backend.ready());
        backend.submit({ Qt::white, { }, { } });
        QCOMPARE(counters->writes.load(), 2);
    }
};
QTEST_GUILESS_MAIN(GenericDriversTest)
#include "test_generic_drivers.moc"

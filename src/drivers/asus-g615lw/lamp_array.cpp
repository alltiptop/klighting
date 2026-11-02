// SPDX-License-Identifier: GPL-3.0-or-later
#include "lamp_array.h"
#include "g615lw_layout.h"
#include "settings.h"
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QtEndian>
#include <algorithm>
#include <libusb.h>
#include <optional>

Q_LOGGING_CATEGORY(ambientUsb, "kwin.effect.ambientled.usb")
namespace Ambient
{
namespace
{
    constexpr int LightingInterface = 1;
    constexpr unsigned TransferTimeoutMs = 250;
    QByteArray readFile(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    QString usbError(const char* operation, int result)
    {
        return QString::fromLatin1(operation) + QLatin1String(": ")
            + QString::fromLatin1(libusb_error_name(result));
    }
    QByteArray rangePacket(int first, int last, const QColor& color, uchar intensity)
    {
        QByteArray report(10, '\0');
        report[0] = 5;
        qToLittleEndian<quint16>(first, report.data() + 2);
        qToLittleEndian<quint16>(last, report.data() + 4);
        report[6] = char(color.red());
        report[7] = char(color.green());
        report[8] = char(color.blue());
        report[9] = char(intensity);
        return report;
    }
}
QList<QByteArray> lampArrayPackets(const LedFrame& frame, const QList<bool>& keyboardLamps)
{
    if (!frame.color.isValid() || frame.brightness > 3 || keyboardLamps.isEmpty()
        || keyboardLamps.size() > 512)
        return { };
    if (!frame.stripColors.isEmpty()) {
        if (frame.stripColors.size() != 12 || keyboardLamps.size() != 100)
            return { };
        for (int lamp = 0; lamp < 100; ++lamp)
            if (keyboardLamps[lamp] != (lamp < KeyboardLampCount)
                || (lamp >= KeyboardLampCount && !frame.stripColors[lamp - KeyboardLampCount].isValid()))
                return { };
    }
    if (!frame.keyColors.isEmpty()) {
        if (frame.keyColors.size() != KeyboardLampCount || keyboardLamps.size() != 100)
            return { };
        for (int lamp = 0; lamp < keyboardLamps.size(); ++lamp) {
            if (keyboardLamps[lamp] != (lamp < KeyboardLampCount)
                || (lamp < KeyboardLampCount && !frame.keyColors[lamp].isValid()))
                return { };
        }
    }
    QList<QByteArray> reports;
    const auto enabled = [&](int lamp) {
        return frame.brightness > 0 && (keyboardLamps[lamp] ? frame.keyboard : frame.lightbar);
    };
    const auto color = [&](int lamp) {
        if (!enabled(lamp))
            return QColor(Qt::black);
        if (lamp >= KeyboardLampCount && !frame.stripColors.isEmpty())
            return frame.stripColors[lamp - KeyboardLampCount];
        return !frame.keyColors.isEmpty() && lamp < KeyboardLampCount ? frame.keyColors[lamp] : frame.color;
    };
    int first = 0;
    for (int next = 1; next <= keyboardLamps.size(); ++next) {
        if (next < keyboardLamps.size() && enabled(next) == enabled(first) && color(next) == color(first))
            continue;
        const bool on = enabled(first);
        // asusd already applies Low/Med/High through the hardware backlight.
        // Scaling intensity again would dim the ambient RGB a second time.
        reports.append(rangePacket(first, next - 1, color(first), on ? 255 : 0));
        first = next;
    }
    // For scattered per-key colors, MultiUpdate carries eight lamps per report.
    // Retain RangeUpdate for uniform colors and small numbers of ranges.
    const int batches = (keyboardLamps.size() + 7) / 8;
    if ((!frame.keyColors.isEmpty() || !frame.stripColors.isEmpty()) && reports.size() > batches) {
        reports.clear();
        for (int first = 0; first < keyboardLamps.size(); first += 8) {
            QByteArray report(51, '\0');
            report[0] = 4;
            const int count = std::min(8, int(keyboardLamps.size()) - first);
            report[1] = char(count);
            for (int i = 0; i < count; ++i) {
                const int lamp = first + i;
                qToLittleEndian<quint16>(lamp, report.data() + 3 + 2 * i);
                const auto rgb = color(lamp);
                report[19 + 4 * i] = char(rgb.red());
                report[20 + 4 * i] = char(rgb.green());
                report[21 + 4 * i] = char(rgb.blue());
                report[22 + 4 * i] = enabled(lamp) ? char(255) : 0;
            }
            reports.append(report);
        }
        reports.last()[2] = 1;
        return reports;
    }
    // Commit the complete frame once, including black ranges for disabled zones.
    reports.last()[1] = 1;
    return reports;
}

// Owned and used only by the USB worker. Never opens a hidraw node: on affected
// kernels that starts interrupt-IN polling on this feature-only interface and
// repeatedly resets the entire N-KEY USB device.
class LampArrayDevice
{
public:
    ~LampArrayDevice() { close(); }
    QString write(const LedFrame& frame)
    {
        QString error;
        if (!m_handle)
            error = open();
        if (error.isEmpty()
            && ((!frame.keyColors.isEmpty() && !m_keyMappingMatches)
                || (!frame.stripColors.isEmpty() && !m_stripMappingMatches)))
            error = QStringLiteral("Per-element lighting requires the verified G615LW lamp mapping");
        if (error.isEmpty() && !m_controlled) {
            m_controlled = true; // A failed transfer may still have reached the MCU.
            auto control = QByteArray::fromHex("0600");
            error = feature(control);
            if (error.isEmpty())
                QThread::msleep(100);
        }
        if (error.isEmpty()) {
            const auto reports = lampArrayPackets(frame, m_keyboard);
            if (reports.isEmpty())
                error = QStringLiteral("Invalid LampArray frame");
            for (auto report : reports) {
                if (!error.isEmpty())
                    break;
                if (QThread::currentThread()->isInterruptionRequested()) {
                    error = QStringLiteral("USB operation cancelled");
                    break;
                }
                error = feature(report);
            }
        }
        if (!error.isEmpty()) {
            const auto cleanup = close();
            if (!cleanup.isEmpty())
                error += QLatin1String("; ") + cleanup;
        }
        return error;
    }
    QString close()
    {
        QStringList errors;
        if (m_handle) {
            if (m_controlled) {
                auto control = QByteArray::fromHex("0601");
                const auto error = feature(control);
                if (!error.isEmpty())
                    errors.append(error);
            }
            if (m_claimed) {
                const int rc = libusb_release_interface(m_handle, LightingInterface);
                if (rc < 0)
                    errors.append(usbError("Release lighting interface", rc));
            }
            if (m_detached) {
                const int rc = libusb_attach_kernel_driver(m_handle, LightingInterface);
                if (rc < 0 && rc != LIBUSB_ERROR_NO_DEVICE)
                    errors.append(usbError("Restore lighting driver", rc));
            }
            libusb_close(m_handle);
        }
        if (m_context)
            libusb_exit(m_context);
        m_handle = nullptr;
        m_context = nullptr;
        m_claimed = m_detached = m_controlled = false;
        m_keyMappingMatches = m_stripMappingMatches = true;
        m_keyboard.clear();
        if (!errors.isEmpty())
            qCWarning(ambientUsb) << errors.join(QLatin1String("; "));
        return errors.join(QLatin1String("; "));
    }

private:
    QString feature(QByteArray& report, bool get = false)
    {
        const int rc = libusb_control_transfer(m_handle, get ? 0xa1 : 0x21, get ? 1 : 9,
            0x300 | uchar(report[0]), LightingInterface, reinterpret_cast<unsigned char*>(report.data()),
            report.size(), TransferTimeoutMs);
        if (rc < 0)
            return usbError(get ? "Get lighting report" : "Set lighting report", rc);
        if (rc != report.size())
            return QStringLiteral("Short lighting report: %1/%2").arg(rc).arg(report.size());
        return { };
    }
    QString open()
    {
        if (readFile(QStringLiteral("/sys/class/dmi/id/board_name")).trimmed() != "G615LW")
            return QStringLiteral("LampArray transport supports only G615LW");
        QString usbPath;
        const QDir devices(QStringLiteral("/sys/bus/usb/devices"));
        for (const auto& name : devices.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto path = devices.filePath(name);
            if (readFile(path + "/idVendor").trimmed() != "0b05"
                || readFile(path + "/idProduct").trimmed() != "19b6")
                continue;
            if (!usbPath.isEmpty())
                return QStringLiteral("Multiple ASUS 19b6 USB devices; refusing ambiguous target");
            usbPath = path;
        }
        if (usbPath.isEmpty())
            return QStringLiteral("ASUS 19b6 USB device is unavailable");
        // Check the cached descriptor without opening the device. Only this
        // known G615LW feature-report layout is currently supported.
        bool descriptorMatches = false;
        const QString interfacePath = QFileInfo(usbPath).canonicalFilePath() + QLatin1Char('/')
            + QFileInfo(usbPath).fileName() + QLatin1String(":1.1/");
        const QDir hidraw(QStringLiteral("/sys/class/hidraw"));
        for (const auto& name : hidraw.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const auto path = QFileInfo(hidraw.filePath(name + "/device")).canonicalFilePath();
            if (!path.startsWith(interfacePath))
                continue;
            const auto descriptor = readFile(path + "/report_descriptor");
            descriptorMatches = descriptor
                == QByteArray::fromHex(
                    "05590901a10185010902a1020903150027ffff000075109501b10309040905090609070908150027ffffff7f"
                    "75209505b103c085020920a1020921150027ffff000075109501b102c085030922a1020921150027ffff0000"
                    "75109501b10209230924092509270926150027ffffff7f75209505b10209280929092a092b092c092d150026"
                    "ff0075089506b102c085040950a102090309551500250875089502b1020921150027ffff000075109508b102"
                    "0951095209530954095109520953095409510952095309540951095209530954095109520953095409510952"
                    "0953095409510952095309540951095209530954150026ff0075089520b102c085050960a102095515002508"
                    "75089501b10209610962150027ffff000075109502b1020951095209530954150026ff0075089504b102c085"
                    "060970a10209711500250175089501b102c0c0");
        }
        if (!descriptorMatches)
            return QStringLiteral("Unsupported or missing G615LW LampArray descriptor");
        const int bus = readFile(usbPath + "/busnum").trimmed().toInt();
        const int address = readFile(usbPath + "/devnum").trimmed().toInt();
        int rc = libusb_init(&m_context);
        if (rc < 0)
            return usbError("Initialize libusb", rc);
        libusb_device** list = nullptr;
        const auto count = libusb_get_device_list(m_context, &list);
        if (count < 0)
            return usbError("Enumerate USB devices", count);
        rc = LIBUSB_ERROR_NO_DEVICE;
        for (ssize_t i = 0; i < count; ++i) {
            if (libusb_get_bus_number(list[i]) == bus && libusb_get_device_address(list[i]) == address) {
                libusb_device_descriptor descriptor;
                rc = libusb_get_device_descriptor(list[i], &descriptor);
                if (rc == 0 && descriptor.idVendor == 0x0b05 && descriptor.idProduct == 0x19b6)
                    rc = libusb_open(list[i], &m_handle);
                break;
            }
        }
        libusb_free_device_list(list, 1);
        if (rc < 0 || !m_handle)
            return usbError("Open ASUS USB device (check uaccess permissions)", rc);
        rc = libusb_kernel_driver_active(m_handle, LightingInterface);
        if (rc != 1)
            return QStringLiteral("Lighting interface is already claimed or has no kernel driver");
        rc = libusb_detach_kernel_driver(m_handle, LightingInterface);
        if (rc < 0)
            return usbError("Detach lighting interface", rc);
        m_detached = true;
        rc = libusb_claim_interface(m_handle, LightingInterface);
        if (rc < 0)
            return usbError("Claim lighting interface", rc);
        m_claimed = true;
        QByteArray attributes(23, '\0');
        attributes[0] = 1;
        auto error = feature(attributes, true);
        if (!error.isEmpty())
            return error;
        if (uchar(attributes[0]) != 1 || qFromLittleEndian<quint16>(attributes.data() + 1) != 100
            || qFromLittleEndian<quint32>(attributes.data() + 19) > 1000000 / MaxLedFramesPerSecond)
            return QStringLiteral("Unexpected G615LW LampArray limits");
        for (int lamp = 0; lamp < 100; ++lamp) {
            if (QThread::currentThread()->isInterruptionRequested())
                return QStringLiteral("USB initialization cancelled");
            QByteArray request(3, '\0');
            request[0] = 2;
            qToLittleEndian<quint16>(lamp, request.data() + 1);
            error = feature(request);
            if (!error.isEmpty())
                return error;
            QByteArray details(29, '\0');
            details[0] = 3;
            error = feature(details, true);
            if (!error.isEmpty())
                return error;
            if (uchar(details[0]) != 3 || qFromLittleEndian<quint16>(details.data() + 1) != lamp)
                return QStringLiteral("Lamp attribute ID mismatch");
            m_keyboard.append(qFromLittleEndian<quint32>(details.data() + 19) & 1);
            if (lamp < KeyboardLampCount)
                m_keyMappingMatches &= m_keyboard.last() && uchar(details[27]) == 1
                    && matchesKeyboardLamp(lamp, qFromLittleEndian<quint32>(details.data() + 3),
                        qFromLittleEndian<quint32>(details.data() + 7), uchar(details[28]));
            else {
                m_keyMappingMatches &= !m_keyboard.last();
                m_stripMappingMatches &= !m_keyboard.last() && uchar(details[27]) == 1
                    && matchesStripLamp(lamp, qFromLittleEndian<quint32>(details.data() + 3),
                        qFromLittleEndian<quint32>(details.data() + 7));
            }
        }
        if (std::count(m_keyboard.cbegin(), m_keyboard.cend(), true) != 88)
            return QStringLiteral("Unexpected G615LW keyboard/lightbar mapping");
        qCInfo(ambientUsb) << "Claimed LampArray:" << bus << address << "88 keyboard, 12 lightbar lamps";
        return { };
    }
    libusb_context* m_context = nullptr;
    libusb_device_handle* m_handle = nullptr;
    QList<bool> m_keyboard;
    bool m_claimed = false, m_detached = false, m_controlled = false;
    bool m_keyMappingMatches = true, m_stripMappingMatches = true;
};

LampArrayTransport::LampArrayTransport()
    : m_worker(new QObject)
    , m_device(std::make_unique<LampArrayDevice>())
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("ambient-led-usb"));
    m_thread.start();
}
LampArrayTransport::~LampArrayTransport() { shutdown(); }
void LampArrayTransport::submit(const LedFrame& frame, quint64 operation) { dispatch(&frame, operation); }
void LampArrayTransport::release(quint64 operation) { dispatch(nullptr, operation); }
void LampArrayTransport::dispatch(const LedFrame* frame, quint64 operation)
{
    const std::optional<LedFrame> copy = frame ? std::optional(*frame) : std::nullopt;
    QMetaObject::invokeMethod(m_worker, [this, copy, operation] {
        const auto error = copy ? m_device->write(*copy) : m_device->close();
        QMetaObject::invokeMethod(this, [this, operation, error] { Q_EMIT finished(operation, error); });
    });
}
void LampArrayTransport::shutdown()
{
    if (!m_thread.isRunning())
        return;
    m_thread.requestInterruption();
    // Only teardown waits. Regular frame transfers never block the compositor.
    QMetaObject::invokeMethod(m_worker, [this] { m_device->close(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}
}

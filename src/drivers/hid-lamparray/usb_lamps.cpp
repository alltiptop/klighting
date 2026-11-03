// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_lamps.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <libusb.h>

namespace Ambient::HidLampArray
{
QString cacheDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + "/klighting/hid-lamparray";
}
std::optional<Array> readCachedArray(const Descriptor& d, const QByteArray& json)
{
    const auto object = QJsonDocument::fromJson(json).object();
    if (object.value("version").toInt() != 1)
        return std::nullopt;
    auto array = arrayAttributes(d, QByteArray::fromHex(object.value("array").toString().toLatin1()));
    const auto lamps = object.value("lamps").toArray();
    if (!array || lamps.size() != array->count)
        return std::nullopt;
    for (int i = 0; i < array->count; ++i) {
        auto lamp = lampAttributes(d, QByteArray::fromHex(lamps[i].toString().toLatin1()), i);
        if (!lamp)
            return std::nullopt;
        array->lamps.append(*lamp);
    }
    return array;
}
namespace
{
    QByteArray binaryFile(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return { };
        auto data = file.read(65537);
        return data.size() <= 65536 ? data : QByteArray();
    }
    void inputBindings(DeviceInstance& device, const Array& array)
    {
        QVariantMap codes;
        if (array.kind == 1)
            for (const auto& lamp : array.lamps)
                if ((lamp.purposes & 1) && Generic::inputCode(lamp.binding))
                    codes.insert(QString::number(lamp.id), Generic::inputCode(lamp.binding));
        device.options.insert("InputKeyCodes", codes);
    }
}
QList<DeviceInstance> discover(const QString& usbRoot, const QString& cacheRoot)
{
    QList<DeviceInstance> result;
    for (const auto& entry : QDir(usbRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (result.size() >= 64 || QThread::currentThread()->isInterruptionRequested())
            break;
        if (Generic::readFile(entry.filePath() + "/bInterfaceClass") != "03")
            continue;
        const auto usb = Generic::usbParent(entry.canonicalFilePath());
        const auto iface = entry.canonicalFilePath();
        if (usb.isEmpty())
            continue;
        bool valid = false;
        const int number = Generic::readFile(iface + "/bInterfaceNumber").toInt(&valid, 16);
        if (!valid || number < 0 || number > 255)
            continue;
        const auto vendor = Generic::readFile(usb + "/idVendor"),
                   product = Generic::readFile(usb + "/idProduct");
        const auto version = Generic::readFile(usb + "/bcdDevice");
        const auto serial = Generic::readFile(usb + "/serial");
        const auto descriptorKey
            = Generic::digest(vendor + ':' + product + ':' + version + ':' + QByteArray::number(number));
        QByteArray bytes;
        for (const auto& child : QDir(iface).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            bytes = binaryFile(child.filePath() + "/report_descriptor");
            if (!bytes.isEmpty())
                break;
        }
        // Claiming a dedicated lighting interface removes its HID sysfs child.
        // The USB interface and a validated descriptor cache remain discoverable.
        if (bytes.isEmpty())
            bytes = binaryFile(cacheRoot + "/descriptor-" + descriptorKey + ".bin");
        const auto descriptor = parseDescriptor(bytes);
        if (!descriptor)
            continue;
        DeviceInstance device;
        device.id = Generic::digest(vendor + ':' + product + ':' + (serial.isEmpty() ? usb.toUtf8() : serial)
            + ':' + QByteArray::number(number));
        const auto model = "hid-lamp-" + Generic::digest(vendor + product + version + bytes);
        const auto name = QString::fromUtf8(Generic::readFile(usb + "/product"));
        const auto cache = Generic::readFile(cacheRoot + '/' + model + ".json", 524288);
        const auto array = readCachedArray(*descriptor, cache);
        device.options = { { "UsbPath", usb }, { "Interface", number }, { "Descriptor", bytes },
            { "DescriptorKey", descriptorKey }, { "Generation", Generic::readFile(usb + "/devnum") },
            { "Vendor", vendor.toUInt(nullptr, 16) }, { "Product", product.toUInt(nullptr, 16) },
            { "Metadata", array ? Generic::digest(cache) : QString() } };
        if (array) {
            device.description
                = description(*array, model, name.isEmpty() ? QStringLiteral("USB LampArray") : name);
            inputBindings(device, *array);
            quint32 steps = 65535;
            for (const auto& lamp : array->lamps)
                steps = std::min(steps, lamp.levels[3]);
            device.options.insert("BrightnessSteps", steps);
        } else {
            // Listing hardware must not detach a kernel driver or send reports.
            device.description.id = model;
            device.description.name = name.isEmpty() ? QStringLiteral("USB LampArray") : name;
            device.description.capabilities = DeviceCapability::Brightness;
            device.description.keys = Generic::keyboardTemplate();
        }
        device.options.insert("DeviceClaims", Generic::deviceClaims(usb, !array || array->kind == 1));
        Generic::applyLayout(device.description);
        result.append(device);
    }
    return result;
}
namespace
{
    class UsbSession final : public Generic::Session
    {
    public:
        UsbSession(DeviceInstance device, QString cacheRoot)
            : m_device(std::move(device))
            , m_cacheRoot(std::move(cacheRoot))
        {
        }
        ~UsbSession() override { close(); }
        QString open() override
        {
            auto descriptor = parseDescriptor(m_device.options.value("Descriptor").toByteArray());
            if (!descriptor)
                return QStringLiteral("Invalid LampArray descriptor");
            if (descriptor->hasInput)
                return QStringLiteral(
                    "LampArray shares an input interface; a specialized driver is required");
            if (!descriptor->reports.contains(0x70))
                return QStringLiteral(
                    "LampArray has no autonomous mode control; a specialized driver is required");
            m_descriptor = *descriptor;
            const auto usb = m_device.options.value("UsbPath").toString();
            const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
            if (runtime.isEmpty())
                return QStringLiteral("No user runtime directory for device ownership");
            m_lock = std::make_unique<QLockFile>(runtime + "/klighting-usb-" + m_device.id + ".lock");
            m_lock->setStaleLockTime(0);
            if (!m_lock->tryLock())
                return QStringLiteral("USB device is already controlled by another klighting process");
            bool validBus = false, validAddress = false;
            const auto bus = Generic::readFile(usb + "/busnum").toUInt(&validBus);
            const auto address = Generic::readFile(usb + "/devnum").toUInt(&validAddress);
            if (!validBus || !validAddress)
                return QStringLiteral("USB device disconnected");
            int code = libusb_init(&m_context);
            if (code < 0)
                return failure(code);
            libusb_device** devices = nullptr;
            const auto count = libusb_get_device_list(m_context, &devices);
            if (count < 0)
                return failure(int(count));
            code = LIBUSB_ERROR_NO_DEVICE;
            for (ssize_t i = 0; i < count; ++i) {
                libusb_device_descriptor info { };
                if (libusb_get_bus_number(devices[i]) != bus
                    || libusb_get_device_address(devices[i]) != address
                    || libusb_get_device_descriptor(devices[i], &info) < 0
                    || info.idVendor != m_device.options.value("Vendor").toUInt()
                    || info.idProduct != m_device.options.value("Product").toUInt())
                    continue;
                code = libusb_open(devices[i], &m_handle);
                break;
            }
            libusb_free_device_list(devices, 1);
            if (code < 0)
                return failure(code);
            m_interface = m_device.options.value("Interface").toInt();
            if (libusb_kernel_driver_active(m_handle, m_interface) == 1) {
                code = libusb_detach_kernel_driver(m_handle, m_interface);
                if (code < 0)
                    return failure(code);
                m_detached = true;
            }
            code = libusb_claim_interface(m_handle, m_interface);
            if (code < 0)
                return failure(code);
            m_claimed = true;
            const auto error = interrogate();
            if (!error.isEmpty())
                return error;
            if (m_descriptor.reports.contains(0x70)) {
                const auto control = m_descriptor.reports[0x70];
                m_restore = control.empty();
                auto readError = transfer(control, m_restore, true);
                if (!readError.isEmpty())
                    return readError;
                const auto autonomous = control.get(m_restore, 0x71);
                if (!autonomous || *autonomous != 1)
                    return QStringLiteral("LampArray is already in host-controlled mode");
                auto command = m_restore;
                if (!control.set(command, 0x71, 0))
                    return QStringLiteral("Invalid LampArray control report");
                readError = transfer(control, command, false);
                if (!readError.isEmpty())
                    return readError;
                m_controlled = true;
                QThread::usleep(m_array.minimumIntervalUs);
            }
            // Honor Off when Linux exposes a matching keyboard brightness control.
            if (m_array.kind == 1)
                for (const auto& led :
                    QDir("/sys/class/leds").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                    if (led.fileName().endsWith(":kbd_backlight")
                        && Generic::conflicts(Generic::deviceClaims(led.canonicalFilePath(), true),
                            m_device.options.value("DeviceClaims").toStringList()))
                        m_brightness.append(led.filePath() + "/brightness");
            return { };
        }
        QString write(const DeviceFrame& frame) override
        {
            DeviceFrame output = frame;
            for (const auto& path : m_brightness)
                if (Generic::readFile(path) == "0") {
                    output.uniform = Qt::black;
                    for (auto& color : output.keys)
                        color = Qt::black;
                    for (auto& strip : output.strips)
                        for (auto& color : strip)
                            color = Qt::black;
                    break;
                }
            auto packets = updates(m_descriptor, m_array, m_layout, output);
            if (packets.isEmpty())
                return QStringLiteral("LampArray frame does not match the detected layout");
            QElapsedTimer deadline;
            deadline.start();
            for (auto& packet : packets) {
                if (QThread::currentThread()->isInterruptionRequested() || deadline.elapsed() > 2000)
                    return QStringLiteral("LampArray update interrupted or timed out");
                auto error = transfer(packet.report, packet.bytes, false);
                if (!error.isEmpty())
                    return error;
            }
            return { };
        }
        int minimumIntervalMs() const override
        {
            return std::max(1u, (m_array.minimumIntervalUs + 999) / 1000);
        }
        void close() override
        {
            if (m_handle) {
                if (m_controlled)
                    transfer(m_descriptor.reports[0x70], m_restore, false);
                if (m_claimed)
                    libusb_release_interface(m_handle, m_interface);
                if (m_detached)
                    libusb_attach_kernel_driver(m_handle, m_interface);
                libusb_close(m_handle);
            }
            if (m_context)
                libusb_exit(m_context);
            m_handle = nullptr;
            m_context = nullptr;
            m_claimed = m_detached = m_controlled = false;
            m_lock.reset();
        }

    private:
        QString failure(int code) const
        {
            return QStringLiteral("USB LampArray: %1").arg(QString::fromLatin1(libusb_error_name(code)));
        }
        QString transfer(const Report& report, QByteArray& bytes, bool read)
        {
            const int skip = report.id == 0 ? 1 : 0;
            const int length = int(bytes.size()) - skip;
            const int code = libusb_control_transfer(m_handle, read ? 0xa1 : 0x21, read ? 1 : 9,
                (report.type << 8) | report.id, m_interface,
                reinterpret_cast<unsigned char*>(bytes.data() + skip), length, 250);
            if (code < 0)
                return failure(code);
            if (code != length)
                return QStringLiteral("Truncated LampArray report");
            return { };
        }
        QString interrogate()
        {
            auto report = m_descriptor.reports[2];
            auto bytes = report.empty();
            auto error = transfer(report, bytes, true);
            if (!error.isEmpty())
                return error;
            auto array = arrayAttributes(m_descriptor, bytes);
            if (!array)
                return QStringLiteral("Unsupported LampArray attributes or update rate");
            QJsonObject cache { { "version", 1 }, { "array", QString::fromLatin1(bytes.toHex()) } };
            QJsonArray lamps;
            QElapsedTimer deadline;
            deadline.start();
            for (int id = 0; id < array->count; ++id) {
                if (QThread::currentThread()->isInterruptionRequested() || deadline.elapsed() > 5000)
                    return QStringLiteral("LampArray interrogation interrupted or timed out");
                report = m_descriptor.reports[0x20];
                bytes = report.empty();
                if (!report.set(bytes, 0x21, id))
                    return QStringLiteral("Unsupported LampArray ID range");
                error = transfer(report, bytes, false);
                if (!error.isEmpty())
                    return error;
                report = m_descriptor.reports[0x22];
                bytes = report.empty();
                error = transfer(report, bytes, true);
                if (!error.isEmpty())
                    return error;
                auto lamp = lampAttributes(m_descriptor, bytes, id);
                if (!lamp)
                    return QStringLiteral("Invalid LampArray lamp attributes");
                array->lamps.append(*lamp);
                lamps.append(QString::fromLatin1(bytes.toHex()));
            }
            m_array = *array;
            m_layout = description(m_array, m_device.description.id, m_device.description.name);
            Generic::applyLayout(m_layout);
            cache.insert("lamps", lamps);
            const auto encoded = QJsonDocument(cache).toJson(QJsonDocument::Compact);
            const auto path = m_cacheRoot + '/' + m_layout.id + ".json";
            QDir().mkpath(m_cacheRoot);
            const auto descriptorPath
                = m_cacheRoot + "/descriptor-" + m_device.options.value("DescriptorKey").toString() + ".bin";
            const auto descriptorBytes = m_device.options.value("Descriptor").toByteArray();
            if (binaryFile(descriptorPath) != descriptorBytes) {
                QSaveFile file(descriptorPath);
                if (!file.open(QIODevice::WriteOnly) || file.write(descriptorBytes) != descriptorBytes.size()
                    || !file.commit())
                    return QStringLiteral("Cannot cache LampArray descriptor");
            }
            if (Generic::readFile(path, 524288) != encoded) {
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size()
                    || !file.commit())
                    return QStringLiteral("Cannot cache LampArray layout");
            }
            return { };
        }
        DeviceInstance m_device;
        QString m_cacheRoot;
        Descriptor m_descriptor;
        Array m_array;
        DeviceDescription m_layout;
        libusb_context* m_context = nullptr;
        libusb_device_handle* m_handle = nullptr;
        std::unique_ptr<QLockFile> m_lock;
        int m_interface = 0;
        bool m_claimed = false, m_detached = false, m_controlled = false;
        QByteArray m_restore;
        QStringList m_brightness;
    };
}
std::unique_ptr<Generic::Session> createSession(const DeviceInstance& device, const QString& cacheRoot)
{
    return std::make_unique<UsbSession>(device, cacheRoot);
}
}

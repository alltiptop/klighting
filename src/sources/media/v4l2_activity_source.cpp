// SPDX-License-Identifier: GPL-3.0-or-later
#include "v4l2_activity_source.h"
#include <QFile>
#include <cerrno>
#include <condition_variable>
#include <dirent.h>
#include <libudev.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

namespace Ambient
{
namespace
{
    struct CloseDirectory {
        void operator()(DIR* directory) const { closedir(directory); }
    };
    using Directory = std::unique_ptr<DIR, CloseDirectory>;
    bool numeric(const char* name)
    {
        if (!*name)
            return false;
        for (; *name; ++name)
            if (*name < '0' || *name > '9')
                return false;
        return true;
    }
}
bool isV4l2CaptureDevice(const QByteArray& capabilities)
{
    const auto flags = capabilities.split(':');
    return flags.contains("capture") && !flags.contains("video_m2m") && !flags.contains("touch");
}
QVariant openCameraDevices(
    const QSet<quint64>& devices, const QString& procRoot, quint32 userId, std::stop_token stop)
{
    if (devices.isEmpty())
        return false;
    const auto root = QFile::encodeName(procRoot);
    Directory processes(opendir(root.constData()));
    if (!processes)
        return { };
    while (!stop.stop_requested()) {
        errno = 0;
        const auto* process = readdir(processes.get());
        if (!process)
            return errno ? QVariant() : QVariant(false);
        if (!numeric(process->d_name))
            continue;
        const auto path = root + '/' + process->d_name;
        struct stat status { };
        if (stat(path.constData(), &status) || status.st_uid != userId)
            continue;
        Directory descriptors(opendir((path + "/fd").constData()));
        if (!descriptors)
            continue; // Only observable processes of this user are in scope.
        while (!stop.stop_requested()) {
            errno = 0;
            const auto* descriptor = readdir(descriptors.get());
            if (!descriptor) {
                if (errno)
                    return { };
                break;
            }
            if (!numeric(descriptor->d_name))
                continue;
            // stat follows the descriptor without opening its device. Device numbers also
            // cover aliases and bind mounts; no model, device path or process name is fixed.
            if (!fstatat(dirfd(descriptors.get()), descriptor->d_name, &status, 0) && S_ISCHR(status.st_mode)
                && devices.contains(quint64(status.st_rdev)))
                return true;
        }
    }
    return { };
}
QVariant v4l2CameraInUse(std::stop_token stop)
{
    const std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), udev_unref);
    if (!context)
        return { };
    const std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)> enumeration(
        udev_enumerate_new(context.get()), udev_enumerate_unref);
    if (!enumeration || udev_enumerate_add_match_subsystem(enumeration.get(), "video4linux") < 0
        || udev_enumerate_scan_devices(enumeration.get()) < 0)
        return { };
    QSet<quint64> cameras;
    bool complete = true;
    udev_list_entry* entry;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumeration.get()))
    {
        if (stop.stop_requested())
            return { };
        const std::unique_ptr<udev_device, decltype(&udev_device_unref)> device(
            udev_device_new_from_syspath(context.get(), udev_list_entry_get_name(entry)), udev_device_unref);
        if (!device) {
            complete = false;
            continue;
        }
        const QByteArray capabilities(udev_device_get_property_value(device.get(), "ID_V4L_CAPABILITIES"));
        complete &= !capabilities.isEmpty();
        // Exclude metadata-only, output-only and radio nodes, even on the same hardware.
        if (isV4l2CaptureDevice(capabilities)) {
            const auto number = udev_device_get_devnum(device.get());
            if (number)
                cameras.insert(quint64(number));
        }
    }
    const auto value = openCameraDevices(cameras, QStringLiteral("/proc"), getuid(), stop);
    return complete || value.toBool() ? value : QVariant();
}
V4l2ActivitySource::V4l2ActivitySource(Probe probe, std::chrono::milliseconds interval)
    : m_probe(std::move(probe))
    , m_interval(interval)
{
}
V4l2ActivitySource::~V4l2ActivitySource()
{
    // Join before QObject destruction; no worker may queue another delivery afterwards.
    m_worker.request_stop();
    if (m_worker.joinable())
        m_worker.join();
}
void V4l2ActivitySource::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    const auto generation = ++m_generation;
    m_worker.request_stop();
    if (m_worker.joinable())
        m_worker.join();
    m_value.clear();
    if (enabled)
        m_worker = std::jthread([this, generation](std::stop_token stop) {
            std::mutex mutex;
            std::condition_variable_any wake;
            while (!stop.stop_requested()) {
                const auto value = m_probe(stop);
                if (stop.stop_requested())
                    break;
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, value] {
                        if (m_enabled && generation == m_generation && value != m_value) {
                            m_value = value;
                            Q_EMIT changed(
                                value.isValid() ? QVariantMap { { "camera-active", value } } : QVariantMap());
                        }
                    },
                    Qt::QueuedConnection);
                std::unique_lock lock(mutex);
                wake.wait_for(lock, stop, m_interval, [] { return false; });
            }
        });
    Q_EMIT changed({ });
}
}

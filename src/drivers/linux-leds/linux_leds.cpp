// SPDX-License-Identifier: GPL-3.0-or-later
#include "linux_leds.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <algorithm>

namespace Ambient::LinuxLeds
{
namespace
{
    QStringList words(const QByteArray& value)
    {
        return QString::fromLatin1(value.simplified()).split(' ', Qt::SkipEmptyParts);
    }
    bool rgb(const QStringList& channels)
    {
        return channels.size() == 3 && channels.contains("red") && channels.contains("green")
            && channels.contains("blue");
    }
    QByteArray activeTrigger(const QByteArray& text)
    {
        const auto start = text.indexOf('['), end = text.indexOf(']');
        return start >= 0 && end > start ? text.mid(start + 1, end - start - 1) : text;
    }
}
QList<DeviceInstance> discover(const QString& root, const QString& boardName)
{
    QList<DeviceInstance> devices;
    const auto entries = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& entry : entries) {
        if (devices.size() >= 64 || QThread::currentThread()->isInterruptionRequested())
            break;
        const auto path = entry.canonicalFilePath();
        const auto function = entry.fileName().section(':', -1);
        const bool keyboard = function == "kbd_backlight" || function.startsWith("kbd_zoned_backlight-");
        const auto channels = words(Generic::readFile(path + "/multi_index"));
        const bool color = rgb(channels);
        // Status indicators (Caps Lock, disks, network, etc.) are not backlights.
        if (!keyboard && !color)
            continue;
        bool valid = false;
        const int maximum = Generic::readFile(path + "/max_brightness").toInt(&valid);
        if (!valid || maximum < 1 || maximum > 65535 || !QFile::exists(path + "/brightness")
            || (!channels.isEmpty() && !color))
            continue;
        const auto claims = Generic::deviceClaims(path, keyboard);
        DeviceDescription d;
        // Strip transient input numbers from model IDs; physical identity is separate.
        const auto usb = Generic::usbParent(path);
        const auto hardware = usb.isEmpty()
            ? boardName.toUtf8() + ':' + entry.fileName().section(':', 0, 0).toUtf8()
            : Generic::readFile(usb + "/idVendor") + ':' + Generic::readFile(usb + "/idProduct");
        const auto model = QString::fromUtf8(hardware) + ':' + entry.fileName().section(':', 1)
            + (color ? ":rgb" : ":mono");
        d.id = "linux-led-" + Generic::digest(model.toUtf8());
        d.name = entry.fileName();
        d.capabilities = DeviceCapability::Brightness;
        if (color)
            d.capabilities |= DeviceCapability::Color;
        if (keyboard)
            d.keys = Generic::keyboardTemplate();
        else {
            d.capabilities |= DeviceCapability::Lightbar | DeviceCapability::PerStripColor;
            d.strips = { { FirstStripId, QStringLiteral("LED"), { { 0, "1", { } } } } };
        }
        Generic::applyLayout(d);
        auto identity = path;
        if (!usb.isEmpty()) {
            const auto serial = Generic::readFile(usb + "/serial");
            identity = (serial.isEmpty() ? usb : QString::fromUtf8(serial)) + ':' + model;
        }
        devices.append({ Generic::digest(identity.toUtf8()), d,
            { { "SysfsPath", path }, { "DeviceClaims", claims }, { "BrightnessSteps", maximum } } });
    }
    return devices;
}
Session::Session(QString path)
    : m_path(std::move(path))
{
}
Session::~Session() { close(); }
QString Session::open()
{
    m_limits.clear();
    m_lastColor.clear();
    m_lastBrightness = -1;
    const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        return QStringLiteral("No user runtime directory for device ownership");
    m_lock = std::make_unique<QLockFile>(
        runtime + "/klighting-led-" + Generic::digest(m_path.toUtf8()) + ".lock");
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock())
        return QStringLiteral("LED is already controlled by another klighting process");
    bool valid = false;
    m_maximum = Generic::readFile(m_path + "/max_brightness").toInt(&valid);
    if (!valid || m_maximum < 1 || m_maximum > 65535)
        return QStringLiteral("Invalid LED brightness range");
    m_base = Generic::readFile(m_path + "/brightness").toInt(&valid);
    if (!valid || m_base < 0 || m_base > m_maximum)
        return QStringLiteral("Cannot read LED brightness");
    m_channels = words(Generic::readFile(m_path + "/multi_index"));
    if (!m_channels.isEmpty() && !rgb(m_channels))
        return QStringLiteral("Unsupported LED color channels");
    m_originalColor = Generic::readFile(m_path + "/multi_intensity");
    if (!m_channels.isEmpty() && words(m_originalColor).size() != m_channels.size())
        return QStringLiteral("Cannot read LED color intensities");
    const auto limits = words(Generic::readFile(m_path + "/multi_max_intensity"));
    for (qsizetype i = 0; i < m_channels.size(); ++i) {
        int limit = m_maximum;
        if (!limits.isEmpty()) {
            if (limits.size() != m_channels.size())
                return QStringLiteral("Invalid LED channel limits");
            limit = limits[i].toInt(&valid);
            if (!valid || limit < 1 || limit > 65535)
                return QStringLiteral("Invalid LED channel limits");
        }
        m_limits.append(limit);
    }
    m_trigger = activeTrigger(Generic::readFile(m_path + "/trigger"));
    return { };
}
QString Session::put(const QString& attribute, const QByteArray& value)
{
    QFile file(m_path + '/' + attribute);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Cannot write %1: %2").arg(file.fileName(), file.errorString());
    const auto line = value + '\n';
    if (file.write(line) != line.size() || !file.flush())
        return QStringLiteral("Cannot update %1: %2").arg(file.fileName(), file.errorString());
    return { };
}
QString Session::write(const DeviceFrame& frame)
{
    bool valid = false;
    const int current = Generic::readFile(m_path + "/brightness").toInt(&valid);
    if (!valid || current < 0 || current > m_maximum)
        return QStringLiteral("LED disconnected or brightness unavailable");
    if (current != m_lastBrightness)
        m_base = current;
    if (!m_touched && !m_trigger.isEmpty() && m_trigger != "none") {
        const auto error = put("trigger", "none");
        if (!error.isEmpty())
            return error;
    }
    m_touched = true;
    const auto colors = frame.strips.value(FirstStripId);
    const auto color = colors.isEmpty() ? frame.uniform : colors.first();
    if (!color.isValid())
        return QStringLiteral("Invalid LED color");
    if (m_channels.isEmpty()) {
        // Keep a nonzero request visible even on keyboards with only 2–3 levels.
        const int value
            = m_base == 0 || color.value() == 0 ? 0 : std::max(1, qRound(m_base * color.valueF()));
        if (current == value) {
            m_lastBrightness = value;
            return { };
        }
        const auto error = put("brightness", QByteArray::number(value));
        if (error.isEmpty())
            m_lastBrightness = value;
        return error;
    }
    // RGB lives in multi_intensity. Never overwrite KDE's hardware brightness.
    QByteArray values;
    for (qsizetype i = 0; i < m_channels.size(); ++i) {
        const qreal channel = m_channels[i] == "red" ? color.redF()
            : m_channels[i] == "green"               ? color.greenF()
                                                     : color.blueF();
        if (i)
            values += ' ';
        values += QByteArray::number(qRound(channel * m_limits[i]));
    }
    if (values == m_lastColor)
        return { };
    const auto error = put("multi_intensity", values);
    if (error.isEmpty())
        m_lastColor = values;
    return error;
}
void Session::close()
{
    if (m_touched) {
        // Restore only values still owned by this session; preserve external edits.
        if (!m_lastColor.isEmpty() && Generic::readFile(m_path + "/multi_intensity") == m_lastColor)
            put("multi_intensity", m_originalColor);
        if (m_lastBrightness >= 0
            && Generic::readFile(m_path + "/brightness") == QByteArray::number(m_lastBrightness))
            put("brightness", QByteArray::number(m_base));
        if (!m_trigger.isEmpty() && m_trigger != "none"
            && activeTrigger(Generic::readFile(m_path + "/trigger")) == "none")
            put("trigger", m_trigger);
    }
    m_touched = false;
    m_lock.reset();
}
}

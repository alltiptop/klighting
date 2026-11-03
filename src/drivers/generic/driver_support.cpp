// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver_support.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
#include <utility>

namespace Ambient::Generic
{
QByteArray readFile(const QString& path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    auto data = file.read(limit + 1);
    return data.size() > limit ? QByteArray() : data.trimmed();
}
QString digest(const QByteArray& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex().left(32));
}
QString usbParent(const QString& path)
{
    QDir directory(QFileInfo(path).canonicalFilePath());
    if (directory.path().isEmpty())
        return { };
    for (int depth = 0; depth < 32; ++depth) {
        if (QFile::exists(directory.filePath("idVendor")) && QFile::exists(directory.filePath("idProduct")))
            return directory.path();
        if (!directory.cdUp())
            break;
    }
    return { };
}
QStringList deviceClaims(const QString& path, bool keyboard)
{
    const auto usb = usbParent(path);
    QStringList claims;
    if (!usb.isEmpty()) {
        claims.append("usb:" + usb);
        if (keyboard && readFile(usb + "/removable") == "fixed")
            claims.append("internal-keyboard");
    } else {
        claims.append("sysfs:" + QFileInfo(path).canonicalFilePath());
        if (keyboard)
            claims.append("internal-keyboard");
    }
    return claims;
}
bool conflicts(const QStringList& claims, const QStringList& context)
{
    return std::any_of(claims.begin(), claims.end(), [&](const auto& c) { return context.contains(c); });
}
namespace
{
    struct Key {
        int usage;
        const char* label;
        int code;
    };
    const Key specialKeys[] { { 40, "Enter", KEY_ENTER }, { 41, "Esc", KEY_ESC },
        { 42, "Backspace", KEY_BACKSPACE }, { 43, "Tab", KEY_TAB }, { 44, "Space", KEY_SPACE },
        { 45, "-", KEY_MINUS }, { 46, "=", KEY_EQUAL }, { 47, "[", KEY_LEFTBRACE },
        { 48, "]", KEY_RIGHTBRACE }, { 49, "\\", KEY_BACKSLASH }, { 51, ";", KEY_SEMICOLON },
        { 52, "'", KEY_APOSTROPHE }, { 53, "`", KEY_GRAVE }, { 54, ",", KEY_COMMA }, { 55, ".", KEY_DOT },
        { 56, "/", KEY_SLASH }, { 57, "Caps", KEY_CAPSLOCK }, { 70, "PrtSc", KEY_SYSRQ },
        { 71, "Scroll", KEY_SCROLLLOCK }, { 72, "Pause", KEY_PAUSE }, { 73, "Insert", KEY_INSERT },
        { 74, "Home", KEY_HOME }, { 75, "PgUp", KEY_PAGEUP }, { 76, "Delete", KEY_DELETE },
        { 77, "End", KEY_END }, { 78, "PgDn", KEY_PAGEDOWN }, { 79, "Right", KEY_RIGHT },
        { 80, "Left", KEY_LEFT }, { 81, "Down", KEY_DOWN }, { 82, "Up", KEY_UP },
        { 101, "Menu", KEY_COMPOSE }, { 224, "Ctrl", KEY_LEFTCTRL }, { 225, "Shift", KEY_LEFTSHIFT },
        { 226, "Alt", KEY_LEFTALT }, { 227, "Meta", KEY_LEFTMETA }, { 228, "Ctrl", KEY_RIGHTCTRL },
        { 229, "Shift", KEY_RIGHTSHIFT }, { 230, "Alt", KEY_RIGHTALT }, { 231, "Meta", KEY_RIGHTMETA } };
    const int letterCodes[] { KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K,
        KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y,
        KEY_Z };
}
QString keyLabel(int usage)
{
    if (usage >= 4 && usage <= 29)
        return QString(QChar('A' + usage - 4));
    if (usage >= 30 && usage <= 39)
        return QString::number((usage - 29) % 10);
    if (usage >= 58 && usage <= 69)
        return "F" + QString::number(usage - 57);
    for (const auto& key : specialKeys)
        if (key.usage == usage)
            return QString::fromLatin1(key.label);
    return { };
}
int inputCode(int usage)
{
    if (usage >= 4 && usage <= 29)
        return letterCodes[usage - 4];
    if (usage >= 30 && usage <= 39)
        return usage == 39 ? KEY_0 : KEY_1 + usage - 30;
    if (usage >= 58 && usage <= 67)
        return KEY_F1 + usage - 58;
    if (usage == 68 || usage == 69)
        return usage == 68 ? KEY_F11 : KEY_F12;
    for (const auto& key : specialKeys)
        if (key.usage == usage)
            return key.code;
    return 0;
}
QList<DeviceKey> keyboardTemplate()
{
    QList<DeviceKey> keys;
    const QList<QList<int>> rows { { 41, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69 },
        { 53, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 45, 46, 42 },
        { 43, 20, 26, 8, 21, 23, 28, 24, 12, 18, 19, 47, 48, 49 },
        { 57, 4, 22, 7, 9, 10, 11, 13, 14, 15, 51, 52, 40 },
        { 225, 29, 27, 6, 25, 5, 17, 16, 54, 55, 56, 229 }, { 224, 227, 226, 44, 230, 231, 101, 228 } };
    for (qsizetype row = 0; row < rows.size(); ++row) {
        double x = 0;
        for (int id : rows[row]) {
            double width = id == 44 ? 6 : (id == 225 || id == 229 ? 2 : 1);
            keys.append({ id, keyLabel(id), { x, double(row), width - .08, .92 } });
            x += width;
        }
    }
    return keys;
}
bool applyLayout(DeviceDescription& d, const QString& fileName)
{
    const auto path = fileName.isEmpty()
        ? QStandardPaths::locate(QStandardPaths::GenericConfigLocation, "klighting/layouts/" + d.id + ".json")
        : fileName;
    if (path.isEmpty())
        return false;
    const auto doc = QJsonDocument::fromJson(readFile(path, 262144));
    const auto root = doc.object();
    if (!doc.isObject() || root.value("model").toString() != d.id || root.value("version").toInt() != 1)
        return false;
    auto copy = d;
    for (const auto& name : { "bodyColor", "keyColor" }) {
        if (!root.contains(name))
            continue;
        QColor color(root.value(name).toString());
        if (!color.isValid())
            return false;
        (QLatin1String(name) == QLatin1String("bodyColor") ? copy.bodyColor : copy.keyColor) = color;
    }
    QSet<int> seen;
    for (const auto& value : root.value("keys").toArray()) {
        const auto item = value.toObject();
        const int id = item.value("id").toInt(-1);
        auto key
            = std::find_if(copy.keys.begin(), copy.keys.end(), [&](const auto& k) { return k.id == id; });
        const auto rect = item.value("rect").toArray();
        if (key == copy.keys.end() || seen.contains(id) || rect.size() != 4)
            return false;
        for (const auto& number : rect)
            if (!number.isDouble() || !std::isfinite(number.toDouble())
                || std::abs(number.toDouble()) > 100000)
                return false;
        if (rect[2].toDouble() <= 0 || rect[3].toDouble() <= 0)
            return false;
        key->rect = { rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble() };
        if (item.contains("label")) {
            if (item.value("label").toString().isEmpty())
                return false;
            key->label = item.value("label").toString();
        }
        seen.insert(id);
    }
    seen.clear();
    for (const auto& value : root.value("strips").toArray()) {
        const auto item = value.toObject();
        const int id = item.value("id").toInt(-1);
        auto strip
            = std::find_if(copy.strips.begin(), copy.strips.end(), [&](const auto& s) { return s.id == id; });
        if (strip == copy.strips.end() || seen.contains(id))
            return false;
        seen.insert(id);
        const auto points = item.value("path").toArray();
        if (points.size() < 2 || points.size() > 8192)
            return false;
        QSet<int> all, used;
        for (const auto& led : strip->elements)
            all.insert(led.id);
        strip->path.clear();
        strip->pathWidth = item.value("width").toDouble(4);
        if (!std::isfinite(strip->pathWidth) || strip->pathWidth <= 0 || strip->pathWidth > 1000)
            return false;
        for (const auto& point : points) {
            const auto p = point.toArray();
            const int led = p.size() == 3 ? p[0].toInt(-1) : -1;
            if (!all.contains(led) || !p[1].isDouble() || !p[2].isDouble())
                return false;
            const QPointF position(p[1].toDouble(), p[2].toDouble());
            if (!std::isfinite(position.x()) || !std::isfinite(position.y())
                || std::abs(position.x()) > 100000 || std::abs(position.y()) > 100000
                || (!strip->path.isEmpty()
                    && (position - strip->path.last().position).manhattanLength() < .000001))
                return false;
            strip->path.append({ led, position });
            used.insert(led);
        }
        if (used != all)
            return false;
    }
    d = std::move(copy);
    return true;
}
DiscoveryCache::DiscoveryCache(Scan scan)
    : m_worker(new QObject)
    , m_scan(std::move(scan))
{
    m_worker->moveToThread(&m_thread);
    QObject::connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}
DiscoveryCache::~DiscoveryCache()
{
    m_thread.requestInterruption();
    m_thread.quit();
    m_thread.wait();
}
QList<DeviceInstance> DiscoveryCache::discover(const QStringList& context)
{
    QMutexLocker lock(&m_mutex);
    if (context != m_context) {
        m_context = context;
        m_devices.clear();
        m_age.invalidate();
    }
    if (!m_pending && (!m_age.isValid() || m_age.elapsed() >= 2000)) {
        m_pending = true;
        QMetaObject::invokeMethod(m_worker, [this, context] {
            auto devices = m_scan(context);
            QMutexLocker lock(&m_mutex);
            if (context == m_context) {
                m_devices = std::move(devices);
                m_age.start();
                m_uncollected = true;
            }
            m_pending = false;
        });
    }
    m_uncollected = false;
    return m_devices;
}
bool DiscoveryCache::pending() const
{
    QMutexLocker lock(&m_mutex);
    return m_pending || m_uncollected;
}
Backend::Backend(Factory factory)
    : m_worker(new QObject)
    , m_factory(std::move(factory))
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Backend::dispatch);
    m_thread.start();
}
Backend::~Backend()
{
    m_timer.stop();
    m_thread.requestInterruption();
    QMetaObject::invokeMethod(
        m_worker,
        [this] {
            if (m_session)
                m_session->close();
            m_session.reset();
        },
        Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}
void Backend::start(const QVariantMap&)
{
    m_started = true;
    m_status = QStringLiteral("Waiting for lighting output");
}
void Backend::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
        m_pending.reset();
    dispatch();
    if (readinessChanged)
        readinessChanged();
}
void Backend::setLedFramesPerSecond(int fps) { m_fps = std::clamp(fps, 1, 60); }
bool Backend::ready() const { return m_started && m_enabled; }
void Backend::submit(const DeviceFrame& frame)
{
    if (!ready())
        return;
    m_pending = frame;
    dispatch();
}
void Backend::dispatch()
{
    if (m_busy || !m_started)
        return;
    if (m_enabled && !m_pending)
        return;
    if (!m_enabled && !m_open)
        return;
    const int delay = std::max((1000 + m_fps - 1) / m_fps, m_minimumInterval);
    if (m_enabled && m_lastWrite.isValid() && m_lastWrite.elapsed() < delay) {
        m_timer.start(delay - int(m_lastWrite.elapsed()));
        return;
    }
    m_timer.stop();
    auto frame = m_enabled ? std::exchange(m_pending, std::nullopt) : std::nullopt;
    m_busy = true;
    QMetaObject::invokeMethod(m_worker, [this, frame] {
        QString error;
        if (!frame) {
            if (m_session)
                m_session->close();
            m_session.reset();
        } else if (!m_thread.isInterruptionRequested()) {
            if (!m_session) {
                m_session = m_factory();
                error = m_session->open();
            }
            if (error.isEmpty())
                error = m_session->write(*frame);
            if (!error.isEmpty()) {
                m_session->close();
                m_session.reset();
            }
        }
        const bool opened = bool(m_session);
        const int interval = opened ? m_session->minimumIntervalMs() : 1000;
        QMetaObject::invokeMethod(this, [this, frame, error, opened, interval] {
            m_busy = false;
            m_open = opened;
            m_minimumInterval = interval;
            m_lastWrite.start();
            if (frame && error.isEmpty())
                ++m_frames;
            m_status = !error.isEmpty() ? error
                                        : (opened ? QStringLiteral("Connected") : QStringLiteral("Stopped"));
            if (readinessChanged)
                readinessChanged();
            dispatch();
        });
    });
}
}

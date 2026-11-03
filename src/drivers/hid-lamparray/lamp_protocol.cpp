// SPDX-License-Identifier: GPL-3.0-or-later
#include "lamp_protocol.h"
#include "driver_support.h"
#include <QSet>
#include <algorithm>
#include <limits>

namespace Ambient::HidLampArray
{
QByteArray Report::empty() const
{
    QByteArray bytes(1 + (bits + 7) / 8, '\0');
    bytes[0] = char(id);
    return bytes;
}
std::optional<quint32> Report::get(const QByteArray& data, int usage, int index) const
{
    const auto entries = fields.value(usage);
    if (index < 0 || index >= entries.size() || data.size() != 1 + (bits + 7) / 8 || uchar(data[0]) != id)
        return std::nullopt;
    const auto field = entries[index];
    quint32 value = 0;
    for (int i = 0; i < field.bits; ++i)
        value |= quint32((uchar(data[1 + (field.offset + i) / 8]) >> ((field.offset + i) % 8)) & 1) << i;
    return value;
}
bool Report::set(QByteArray& data, int usage, quint32 value, int index) const
{
    const auto entries = fields.value(usage);
    if (!get(data, usage, index) || index >= entries.size())
        return false;
    const auto field = entries[index];
    if (value > field.maximum || (field.bits < 32 && value >= (quint32(1) << field.bits)))
        return false;
    for (int i = 0; i < field.bits; ++i) {
        const int byte = 1 + (field.offset + i) / 8, bit = (field.offset + i) % 8;
        data[byte] = char((uchar(data[byte]) & ~(1 << bit)) | (((value >> i) & 1) << bit));
    }
    return true;
}
std::optional<Descriptor> parseDescriptor(const QByteArray& bytes)
{
    if (bytes.isEmpty() || bytes.size() > 65536)
        return std::nullopt;
    struct Globals {
        quint32 page = 0, maximum = 0;
        int size = 0, count = 0, id = 0;
    } global;
    QList<Globals> stack;
    QList<quint32> usages, collections;
    std::optional<quint32> usageMinimum;
    QMap<int, int> offsets, roleKeys;
    QSet<int> unrelatedReports;
    Descriptor result;
    const QSet<int> roles { 2, 0x20, 0x22, 0x50, 0x60, 0x70 };
    for (qsizetype at = 0; at < bytes.size();) {
        const auto prefix = uchar(bytes[at++]);
        if (prefix == 0xfe)
            return std::nullopt;
        const int size = (prefix & 3) == 3 ? 4 : prefix & 3;
        if (at + size > bytes.size())
            return std::nullopt;
        quint32 value = 0;
        for (int i = 0; i < size; ++i)
            value |= quint32(uchar(bytes[at++])) << (8 * i);
        const int type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) {
            switch (tag) {
            case 0:
                if (value > 65535)
                    return std::nullopt;
                global.page = value;
                break;
            case 2:
                global.maximum = value;
                break;
            case 7:
                if (value > 32)
                    return std::nullopt;
                global.size = int(value);
                break;
            case 8:
                if (value == 0 || value > 255)
                    return std::nullopt;
                global.id = int(value);
                break;
            case 9:
                if (value > 4096)
                    return std::nullopt;
                global.count = int(value);
                break;
            case 10:
                if (stack.size() >= 16)
                    return std::nullopt;
                stack.append(global);
                break;
            case 11:
                if (stack.isEmpty())
                    return std::nullopt;
                global = stack.takeLast();
                break;
            default:
                break;
            }
        } else if (type == 2) {
            const quint32 usage = size == 4 ? value : (global.page << 16) | value;
            if (tag == 0) {
                if (usages.size() >= 4096)
                    return std::nullopt;
                usages.append(usage);
            } else if (tag == 1) {
                usageMinimum = usage;
            } else if (tag == 2) {
                if (!usageMinimum || usage < *usageMinimum || usage - *usageMinimum > 4095
                    || !usages.isEmpty())
                    return std::nullopt;
                for (quint32 u = *usageMinimum; u <= usage; ++u) {
                    usages.append(u);
                    if (u == usage)
                        break;
                }
            } else {
                return std::nullopt;
            }
        } else if (type == 0) {
            if (tag == 10) {
                if (collections.size() >= 16)
                    return std::nullopt;
                collections.append(usages.value(0));
            } else if (tag == 12) {
                if (collections.isEmpty())
                    return std::nullopt;
                collections.removeLast();
            } else if (tag == 8 || tag == 9 || tag == 11) {
                const int reportType = tag == 8 ? 1 : tag == 9 ? 2 : 3;
                result.hasInput |= reportType == 1;
                const int key = (reportType << 8) | global.id;
                const int offset = offsets.value(key);
                const int length = global.size * global.count;
                if (global.size < 1 || global.count < 1 || offset + length > 32760)
                    return std::nullopt;
                offsets[key] = offset + length;
                int role = 0;
                if (collections.contains(0x590001))
                    for (auto it = collections.crbegin(); it != collections.crend(); ++it)
                        if ((*it >> 16) == 0x59 && roles.contains(*it & 0xffff)) {
                            role = *it & 0xffff;
                            break;
                        }
                if (role) {
                    if ((reportType != 3 && !(reportType == 2 && (role == 0x50 || role == 0x60)))
                        || (roleKeys.contains(role) && roleKeys[role] != key))
                        return std::nullopt;
                    for (auto it = roleKeys.cbegin(); it != roleKeys.cend(); ++it)
                        if (it.key() != role && it.value() == key)
                            return std::nullopt;
                    roleKeys[role] = key;
                    auto& report = result.reports[role];
                    report.id = global.id;
                    report.type = reportType;
                    // Constant attribute fields still carry values in this HID standard.
                    if (!(value & 2) && !usages.isEmpty())
                        return std::nullopt;
                    for (int i = 0; i < global.count && !usages.isEmpty(); ++i) {
                        const quint32 usage = usages[std::min<int>(i, usages.size() - 1)];
                        if ((usage >> 16) != 0x59)
                            return std::nullopt;
                        report.fields[usage & 0xffff].append(
                            { offset + i * global.size, global.size, global.maximum });
                    }
                } else
                    unrelatedReports.insert(key);
            } else {
                return std::nullopt;
            }
            usages.clear();
            usageMinimum.reset();
        } else {
            return std::nullopt;
        }
    }
    if (!collections.isEmpty() || !stack.isEmpty())
        return std::nullopt;
    for (auto it = result.reports.begin(); it != result.reports.end(); ++it) {
        if (unrelatedReports.contains(roleKeys[it.key()]))
            return std::nullopt;
        it->bits = offsets.value(roleKeys[it.key()]);
    }
    const auto has = [&](int role, std::initializer_list<int> fields) {
        if (!result.reports.contains(role))
            return false;
        for (int usage : fields)
            if (result.reports[role].fields.value(usage).size() != 1)
                return false;
        return true;
    };
    if (!has(2, { 3, 7, 8 }) || !has(0x20, { 0x21 })
        || !has(0x22, { 0x21, 0x23, 0x24, 0x26, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d }))
        return std::nullopt;
    const auto multi = result.reports.value(0x50);
    const int slotCount = multi.fields.value(0x21).size();
    bool validMulti = slotCount > 0 && slotCount <= 255 && has(0x50, { 3, 0x55 });
    for (int u : { 0x51, 0x52, 0x53, 0x54 })
        validMulti &= multi.fields.value(u).size() == slotCount;
    if (!validMulti)
        result.reports.remove(0x50);
    if (!has(0x60, { 0x55, 0x61, 0x62, 0x51, 0x52, 0x53, 0x54 }))
        result.reports.remove(0x60);
    if (!result.reports.contains(0x50) && !result.reports.contains(0x60))
        return std::nullopt;
    if (result.reports.contains(0x70) && !has(0x70, { 0x71 }))
        return std::nullopt;
    return result;
}
std::optional<Array> arrayAttributes(const Descriptor& d, const QByteArray& data)
{
    const auto r = d.reports.value(2);
    const auto count = r.get(data, 3), kind = r.get(data, 7), interval = r.get(data, 8);
    if (!count || !kind || !interval || *count == 0 || *count > 1024 || *interval > 1000000)
        return std::nullopt;
    return Array { int(*count), *kind, *interval, { } };
}
std::optional<Lamp> lampAttributes(const Descriptor& d, const QByteArray& data, int expectedId)
{
    const auto r = d.reports.value(0x22);
    const auto id = r.get(data, 0x21);
    if (!id || *id != quint32(expectedId))
        return std::nullopt;
    Lamp lamp;
    lamp.id = expectedId;
    const auto x = r.get(data, 0x23), y = r.get(data, 0x24), purpose = r.get(data, 0x26),
               binding = r.get(data, 0x2d);
    const auto programmable = r.get(data, 0x2c);
    if (!x || !y || !purpose || !binding || !programmable || *x > 100000000 || *y > 100000000
        || *programmable > 1)
        return std::nullopt;
    lamp.x = *x;
    lamp.y = *y;
    lamp.purposes = *purpose;
    lamp.binding = *binding;
    lamp.programmable = *programmable;
    for (int i = 0; i < 4; ++i) {
        const auto level = r.get(data, 0x28 + i);
        if (!level || *level > 65535)
            return std::nullopt;
        lamp.levels[i] = *level;
    }
    if (lamp.levels[3] == 0)
        return std::nullopt;
    return lamp;
}
DeviceDescription description(const Array& array, const QString& model, const QString& name)
{
    DeviceDescription d;
    d.id = model;
    d.name = name;
    d.capabilities = DeviceCapability::Brightness;
    bool color = false;
    DeviceStrip strip { FirstStripId, QStringLiteral("LEDs"), { } };
    for (const auto& lamp : array.lamps) {
        color |= lamp.programmable && (lamp.levels[0] || lamp.levels[1] || lamp.levels[2]);
        auto label = array.kind == 1 && (lamp.purposes & 1) ? Generic::keyLabel(lamp.binding) : QString();
        if (label.isEmpty())
            label = "LED " + QString::number(lamp.id + 1);
        if (array.kind == 1 && (lamp.purposes & 1))
            d.keys.append({ lamp.id, label, { lamp.x / 1000.0 - 7, lamp.y / 1000.0 - 7, 14, 14 } });
        else
            strip.elements.append({ lamp.id, QString::number(lamp.id + 1), { } });
    }
    // Firmware supplies LED centers, not keycap outlines. Bound the schematic
    // size by the smallest reported spacing; coincident points use a grid.
    bool coincident = false;
    double size = 14;
    for (qsizetype i = 0; i < d.keys.size(); ++i)
        for (qsizetype j = 0; j < i; ++j) {
            const auto delta = d.keys[i].rect.center() - d.keys[j].rect.center();
            const double distance = std::max(std::abs(delta.x()), std::abs(delta.y()));
            if (distance < .001)
                coincident = true;
            else
                size = std::min(size, distance * .85);
        }
    for (qsizetype i = 0; i < d.keys.size(); ++i) {
        const auto center = d.keys[i].rect.center();
        d.keys[i].rect = coincident ? QRectF((i % 16) * 16, (i / 16) * 16, 14, 14)
                                    : QRectF(center.x() - size / 2, center.y() - size / 2, size, size);
    }
    if (!strip.elements.isEmpty()) {
        d.strips.append(strip);
        d.capabilities |= DeviceCapability::Lightbar;
    }
    if (color) {
        d.capabilities |= DeviceCapability::Color;
        if (!d.keys.isEmpty())
            d.capabilities |= DeviceCapability::PerKeyColor;
        if (!d.strips.isEmpty())
            d.capabilities |= DeviceCapability::PerStripColor;
    }
    return d;
}
QList<Update> updates(
    const Descriptor& d, const Array& array, const DeviceDescription& layout, const DeviceFrame& frame)
{
    if (!frame.uniform.isValid() || array.lamps.size() != array.count
        || (!frame.keys.isEmpty() && frame.keys.size() != layout.keys.size()))
        return { };
    QList<QColor> colors(array.count, frame.uniform);
    for (qsizetype i = 0; i < frame.keys.size(); ++i) {
        if (layout.keys[i].id < 0 || layout.keys[i].id >= colors.size())
            return { };
        colors[layout.keys[i].id] = frame.keys[i];
    }
    for (const auto& strip : layout.strips) {
        const auto values = frame.strips.value(strip.id);
        if (!values.isEmpty() && values.size() != strip.elements.size())
            return { };
        for (qsizetype i = 0; i < values.size(); ++i) {
            if (strip.elements[i].id < 0 || strip.elements[i].id >= colors.size())
                return { };
            colors[strip.elements[i].id] = values[i];
        }
    }
    QList<std::array<quint32, 4>> values;
    for (int i = 0; i < array.count; ++i) {
        const auto& lamp = array.lamps[i];
        const auto c = colors[i];
        if (!c.isValid() || lamp.id != i)
            return { };
        std::array<quint32, 4> v { };
        if (lamp.programmable) {
            v = { quint32(qRound(c.redF() * lamp.levels[0])), quint32(qRound(c.greenF() * lamp.levels[1])),
                quint32(qRound(c.blueF() * lamp.levels[2])), c.value() ? lamp.levels[3] : 0 };
        } else {
            v[3] = quint32(qRound(c.valueF() * lamp.levels[3]));
        }
        values.append(v);
    }
    QList<Update> result;
    if (d.reports.contains(0x50)) {
        const auto r = d.reports[0x50];
        const int slotCount = std::min<int>(r.fields.value(0x21).size(), r.fields.value(3).first().maximum);
        if (slotCount < 1)
            return { };
        for (int first = 0; first < array.count; first += slotCount) {
            auto bytes = r.empty();
            const int count = std::min(slotCount, array.count - first);
            if (!r.set(bytes, 3, count) || !r.set(bytes, 0x55, first + count == array.count))
                return { };
            for (int i = 0; i < count; ++i) {
                if (!r.set(bytes, 0x21, first + i, i))
                    return { };
                for (int channel = 0; channel < 4; ++channel)
                    if (!r.set(bytes, 0x51 + channel, values[first + i][channel], i))
                        return { };
            }
            result.append({ r, bytes });
        }
    } else if (d.reports.contains(0x60)) {
        const auto r = d.reports[0x60];
        for (int first = 0; first < array.count;) {
            int last = first;
            while (last + 1 < array.count && values[last + 1] == values[first])
                ++last;
            auto bytes = r.empty();
            if (!r.set(bytes, 0x61, first) || !r.set(bytes, 0x62, last)
                || !r.set(bytes, 0x55, last + 1 == array.count))
                return { };
            for (int channel = 0; channel < 4; ++channel)
                if (!r.set(bytes, 0x51 + channel, values[first][channel]))
                    return { };
            result.append({ r, bytes });
            first = last + 1;
        }
    }
    return result;
}
}

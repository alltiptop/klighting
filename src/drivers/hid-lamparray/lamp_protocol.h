// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_driver.h"
#include <array>
#include <optional>

namespace Ambient::HidLampArray
{
struct Field {
    int offset = 0, bits = 0;
    quint32 maximum = 0;
};
struct Report {
    int id = 0, type = 3, bits = 0;
    QMap<int, QList<Field>> fields;
    QByteArray empty() const;
    std::optional<quint32> get(const QByteArray& data, int usage, int index = 0) const;
    bool set(QByteArray& data, int usage, quint32 value, int index = 0) const;
};
struct Descriptor {
    QMap<int, Report> reports;
    bool hasInput = false;
};
std::optional<Descriptor> parseDescriptor(const QByteArray& bytes);
struct Lamp {
    int id = 0;
    quint32 x = 0, y = 0, purposes = 0, binding = 0;
    std::array<quint32, 4> levels { };
    bool programmable = false;
};
struct Array {
    int count = 0;
    quint32 kind = 0, minimumIntervalUs = 0;
    QList<Lamp> lamps;
};
std::optional<Array> arrayAttributes(const Descriptor& descriptor, const QByteArray& data);
std::optional<Lamp> lampAttributes(const Descriptor& descriptor, const QByteArray& data, int expectedId);
DeviceDescription description(const Array& array, const QString& model, const QString& name);
struct Update {
    Report report;
    QByteArray bytes;
};
QList<Update> updates(const Descriptor& descriptor, const Array& array, const DeviceDescription& description,
    const DeviceFrame& frame);
}

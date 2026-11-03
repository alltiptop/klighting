// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "driver_support.h"
#include "lamp_protocol.h"

namespace Ambient::HidLampArray
{
QList<DeviceInstance> discover(const QString& usbRoot, const QString& cacheRoot);
std::optional<Array> readCachedArray(const Descriptor& descriptor, const QByteArray& json);
std::unique_ptr<Generic::Session> createSession(const DeviceInstance& device, const QString& cacheRoot);
QString cacheDirectory();
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
class IncompatibleDriver final : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.ambientled.DeviceDriver/99.0")
public:
    IncompatibleDriver() { qFatal("Incompatible driver must never be instantiated"); }
};
#include "incompatible_driver.moc"

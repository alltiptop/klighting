// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_driver.h"
#include <QCoreApplication>

class AlternativeBackend final : public Ambient::DeviceBackend
{
public:
    void start(const QVariantMap&) override
    {
        qApp->setProperty("test.alternative.started", qApp->property("test.alternative.started").toInt() + 1);
        qApp->setProperty(
            "test.alternative.previousStopped", qApp->property("test.conflict.destroyed").toInt() > 0);
    }
    void setEnabled(bool enabled) override { m_enabled = enabled; }
    void setLedFramesPerSecond(int) override { }
    bool ready() const override { return m_enabled; }
    QString status() const override { return "Test alternative"; }
    quint64 completedFrames() const override { return 0; }
    void submit(const Ambient::DeviceFrame&) override { }

private:
    bool m_enabled = false;
};
class AlternativeDriver final : public QObject, public Ambient::DeviceDriver
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AMBIENT_DEVICE_DRIVER_IID FILE "alternative_driver.json")
    Q_INTERFACES(Ambient::DeviceDriver)
public:
    QList<Ambient::DeviceInstance> discover(const Ambient::DeviceEnvironment&) const override
    {
        Ambient::DeviceDescription d { "test-strip", "Alternative", Ambient::DeviceCapability::Color,
            { { 0, "LED", { 0, 0, 1, 1 } } }, { } };
        return { { "alternative", d, { { "DeviceClaims", QStringList { "test:shared" } } } } };
    }
    std::unique_ptr<Ambient::DeviceBackend> createBackend(const Ambient::DeviceInstance&) override
    {
        return std::make_unique<AlternativeBackend>();
    }
    std::unique_ptr<Ambient::DeviceControls> createControls(const Ambient::DeviceInstance&) override
    {
        return { };
    }
};
#include "alternative_driver.moc"

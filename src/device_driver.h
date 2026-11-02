// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QList>
#include <QMap>
#include <QObject>
#include <QRectF>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <memory>

#define AMBIENT_DEVICE_DRIVER_IID "org.ambientled.DeviceDriver/4.0"

namespace Ambient
{
inline constexpr int DriverApiVersion = 4;
inline constexpr int MaxKeyId = 65535;
enum class DeviceCapability : quint32 {
    Brightness = 1,
    Color = 2,
    PerKeyColor = 4,
    Lightbar = 8,
    LightbarPower = 16,
    PerStripColor = 32,
};
Q_DECLARE_FLAGS(DeviceCapabilities, DeviceCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(DeviceCapabilities)

struct DeviceKey {
    int id; // Stable within this device ID; never a position in the keys list.
    QString label;
    QRectF rect; // Device-local units, shared by all keys.
};
inline constexpr int FirstStripId = MaxKeyId + 1;
inline constexpr int MaxAssignmentId = FirstStripId + 63;
struct StripPoint {
    int elementId; // Color at this point; must belong to this strip.
    QPointF position; // Same device-local coordinates as keyboard keys.
};
struct DeviceStrip {
    int id = FirstStripId; // Group ID; individual element IDs are in 0..MaxKeyId.
    QString name;
    QList<DeviceKey> elements; // Ordered along the strip, independent of hardware IDs.
    // Optional display polyline. Colors blend between consecutive points;
    // repeat an element ID to hold its color, repeat the first point to close.
    // Empty means a straight line below the keyboard. Does not change output.
    QList<StripPoint> path = { };
    qreal pathWidth = 4;
};
struct DeviceFrame {
    QColor uniform;
    QList<QColor> keys;
    QMap<int, QList<QColor>> strips;
};
struct DeviceDescription {
    QString id, name;
    DeviceCapabilities capabilities;
    QList<DeviceKey> keys; // Also defines the order of per-key frame colors.
    QMap<QString, QList<int>> groups;
    QList<DeviceStrip> strips = { };
    // Optional physical surface colors for the editor, never LED output.
    // Invalid colors leave the choice to the UI theme.
    QColor bodyColor = { };
    QColor keyColor = { };
};
struct DeviceInstance {
    QString id; // Stable within the driver; use serial/physical identity, never a bus address.
    DeviceDescription description;
    // Optional InputKeyCodes: decimal key ID -> Linux evdev code for input preview.
    // Omitted entries have no input indicator; no key-label guessing in the UI.
    QVariantMap options;
};
struct DeviceEnvironment {
    QString boardName;
};
struct LightbarState {
    bool available = false;
    bool enabled = false;
    QString error;
};

// All methods and callbacks run on the owning Qt thread. Constructors and
// description/detection methods must not write hardware or claim interfaces.
class DeviceBackend
{
public:
    virtual ~DeviceBackend() = default;
    std::function<void()> readinessChanged;
    virtual void start(const QVariantMap& options) = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual void setLedFramesPerSecond(int fps) = 0;
    virtual bool ready() const = 0;
    virtual QString status() const = 0;
    virtual quint64 completedFrames() const = 0;
    // Colors are final software RGB. Empty key/strip lists use uniform; lists
    // follow their description order. Hardware Off must remain off.
    virtual void submit(const DeviceFrame& frame) = 0;
};
class DeviceControls
{
public:
    virtual ~DeviceControls() = default;
    virtual void readLightbar(const QVariantMap& options, std::function<void(LightbarState)> done) = 0;
    // A submitted write must finish even if the settings dialog closes. The
    // caller's callback must guard its UI lifetime (e.g. with QPointer).
    virtual void writeLightbar(bool enabled, std::function<void(QString)> done) = 0;
};
class DeviceDriver
{
public:
    virtual ~DeviceDriver() = default;
    // Discovery is read-only and bounded. One plugin can return many instances.
    virtual QList<DeviceInstance> discover(const DeviceEnvironment& environment) const = 0;
    virtual std::unique_ptr<DeviceBackend> createBackend(const DeviceInstance& device) = 0;
    virtual std::unique_ptr<DeviceControls> createControls(const DeviceInstance& device) = 0;
};
}
Q_DECLARE_INTERFACE(Ambient::DeviceDriver, AMBIENT_DEVICE_DRIVER_IID)

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "device_manager.h"
#include "effect_control.h"
#include "layer_key_tracker.h"
#include "presets.h"
#include "service_control.h"
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <map>

namespace Ambient
{
// Pending edits are shared by every page. Only save() writes settings or hardware.
class SettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList effectPresets READ effectPresets NOTIFY contentChanged)
    Q_PROPERTY(QVariantMap effectPreset READ effectPreset NOTIFY contentChanged)
    Q_PROPERTY(int selectedEffect READ selectedEffect WRITE selectEffect NOTIFY contentChanged)
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY changed)
    Q_PROPERTY(bool editableLightbar READ editableLightbar NOTIFY contentChanged)
    Q_PROPERTY(QVariantList presetDevices READ presetDevices NOTIFY presetDevicesChanged)
    Q_PROPERTY(QVariantList addablePresetDevices READ addablePresetDevices NOTIFY contentChanged)
    Q_PROPERTY(QVariantMap profileInfo READ profileInfo NOTIFY contentChanged)
    Q_PROPERTY(bool canPasteProfile READ canPasteProfile NOTIFY contentChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY contentChanged)
    Q_PROPERTY(QVariantList rules READ rules NOTIFY contentChanged)
    Q_PROPERTY(QVariantMap preset READ preset NOTIFY contentChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY contentChanged)
    Q_PROPERTY(QVariantList availableDevices READ availableDevices NOTIFY contentChanged)
    Q_PROPERTY(int selectedDevice READ selectedDevice WRITE selectDevice NOTIFY contentChanged)
    Q_PROPERTY(QVariantMap deviceSettings READ deviceSettings NOTIFY contentChanged)
    Q_PROPERTY(QVariantList strips READ strips NOTIFY contentChanged)
    Q_PROPERTY(QVariantList keys READ keys NOTIFY contentChanged)
    Q_PROPERTY(QVariantMap device READ device NOTIFY contentChanged)
    Q_PROPERTY(QVariantList conditions READ conditions CONSTANT)
    Q_PROPERTY(int selected READ selected WRITE selectPreset NOTIFY contentChanged)
    Q_PROPERTY(int selectedLayer READ selectedLayer WRITE selectLayer NOTIFY layerSelectionChanged)
    Q_PROPERTY(QString defaultId READ defaultId NOTIFY contentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool representsDefaults READ representsDefaults NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool editableKeys READ editableKeys NOTIFY contentChanged)
    Q_PROPERTY(bool lightbarAvailable READ lightbarAvailable NOTIFY changed)
    Q_PROPERTY(bool lightbarEnabled READ lightbarEnabled WRITE setLightbarEnabled NOTIFY changed)
    Q_PROPERTY(QString lightbarStatus READ lightbarStatus NOTIFY changed)
    Q_PROPERTY(bool pickingWindow READ pickingWindow NOTIFY changed)
    Q_PROPERTY(QString windowStatus READ windowStatus NOTIFY changed)
    Q_PROPERTY(Ambient::EffectControl* effect READ effect CONSTANT)
    Q_PROPERTY(Ambient::ServiceControl* service READ service CONSTANT)
    Q_PROPERTY(int previewLayer READ previewLayer NOTIFY previewChanged)
    Q_PROPERTY(QVariantList pressedKeys READ pressedKeys NOTIFY pressedKeysChanged)
public:
    explicit SettingsController(QObject* parent = nullptr);
    ~SettingsController() override;
    QVariantList presetDevices() const;
    QVariantList addablePresetDevices() const;
    QVariantMap profileInfo() const;
    bool canPasteProfile() const;
    Q_INVOKABLE bool addPresetDevice(const QString& identity);
    Q_INVOKABLE void removePresetDevice(const QString& profileId);
    Q_INVOKABLE void copyProfile();
    Q_INVOKABLE QStringList pasteWarnings() const;
    Q_INVOKABLE bool pasteProfile();
    Q_INVOKABLE QVariantMap deviceSelectionCapabilities(const QVariantList& targets) const;
    Q_INVOKABLE QVariantMap deviceSelectionEffects(const QVariantList& targets) const;
    Q_INVOKABLE QVariantMap deviceSelectionEffectOptions(
        const QVariantList& targets, const QString& id) const;
    Q_INVOKABLE QString editDeviceSelection(
        const QVariantList& targets, const QString& action, const QVariantMap& arguments);
    QVariantList devices() const;
    QVariantList availableDevices() const;
    QVariantMap deviceSettings() const;
    int selectedDevice() const;
    void selectDevice(int index);
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE bool addDevice(const QString& identity);
    Q_INVOKABLE void removeDevice();
    Q_INVOKABLE void setDevice(const QString& key, const QVariant& value);
    QVariantList strips() const;
    Q_INVOKABLE QVariantList gradients(int layer) const;
    Q_INVOKABLE bool setGradient(int layer, int index, int strip, int from, int to, const QString& start,
        const QString& end, int opacity, bool ambient, const QString& effectId = { });
    Q_INVOKABLE void removeGradient(int layer, int index);
    QVariantList effectPresets() const;
    QVariantMap effectPreset() const;
    int selectedEffect() const { return m_selectedEffect; }
    void selectEffect(int index);
    QString section() const { return m_section; }
    void setSection(const QString& section);
    bool editableLightbar() const;
    Q_INVOKABLE void setDefaultEffect(const QString& id);
    Q_INVOKABLE void setEffectName(const QString& name);
    Q_INVOKABLE bool addEffect(const QString& type, bool duplicate = false);
    Q_INVOKABLE bool removeEffect();
    Q_INVOKABLE void importEffectFile(const QUrl& path);
    Q_INVOKABLE void exportEffectFile(const QUrl& path);
    QVariantList presets() const;
    QVariantList rules() const;
    QVariantMap preset() const;
    QVariantList keys() const;
    QVariantMap device() const;
    QVariantList conditions() const;
    int selected() const { return m_selected; }
    int selectedLayer() const;
    Q_INVOKABLE void selectLayer(int index);
    QString defaultId() const { return m_collection.defaultId; }
    bool dirty() const;
    bool representsDefaults() const;
    bool busy() const;
    QString error() const { return m_error; }
    bool editableKeys() const;
    bool lightbarAvailable() const;
    bool lightbarEnabled() const;
    QString lightbarStatus() const;
    bool pickingWindow() const { return m_pickingWindow; }
    QString windowStatus() const { return m_windowStatus; }
    EffectControl* effect() const { return m_effect; }
    ServiceControl* service() const { return m_service; }
    const PresetCollection& collection() const { return m_collection; }
    void selectPreset(int index);
    void setLightbarEnabled(bool value);
    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void defaults();
    Q_INVOKABLE void setName(const QString& name);
    Q_INVOKABLE bool addPreset(bool duplicate);
    Q_INVOKABLE bool removePreset();
    Q_INVOKABLE void importFile(const QUrl& path);
    Q_INVOKABLE void exportFile(const QUrl& path);
    Q_INVOKABLE void setEffectPower(const QString& key, bool value, const QString& localId = { });
    Q_INVOKABLE void setSolidColor(const QString& color, const QString& localId = { });
    Q_INVOKABLE QString addLocalEffect(int layer, const QVariantList& ids, const QString& type);
    Q_INVOKABLE QString localizeEffect(
        int layer, const QVariantList& ids, const QString& id, bool aboveLayers);
    Q_INVOKABLE QVariantMap localEffectOptions(const QString& id) const;
    Q_INVOKABLE QVariantMap assignedEffectOptions(const QString& id) const;
    Q_INVOKABLE void setAssignedColor(int layer, const QVariantList& ids, const QString& color);
    Q_INVOKABLE void setMicrophone(const QString& key, const QVariant& value, const QString& localId = { });
    Q_INVOKABLE void setNotification(const QString& key, const QVariant& value, const QString& localId = { });
    Q_INVOKABLE QVariantMap selectionEffects(int layer, const QVariantList& ids) const;
    Q_INVOKABLE void addAssignedEffect(
        int layer, const QVariantList& ids, const QString& effectId, bool aboveLayers = false);
    Q_INVOKABLE void updateAssignedEffect(int layer, const QVariantList& ids, const QString& effectId,
        bool wasAbove, bool aboveLayers, int opacity);
    Q_INVOKABLE void removeAssignedEffect(
        int layer, const QVariantList& ids, const QString& effectId, bool aboveLayers);
    Q_INVOKABLE void moveAssignedEffect(
        int layer, const QVariantList& ids, const QString& effectId, bool aboveLayers, int offset);
    Q_INVOKABLE void setKeyColor(int layer, const QVariantList& ids, const QString& color, int opacity);
    Q_INVOKABLE void clearKeyLighting(int layer, const QVariantList& ids, bool inherit);
    Q_INVOKABLE void addNotificationLayout(const QString& effectId = { });
    Q_INVOKABLE void setAmbient(const QString& key, int value, const QString& localId = { });
    Q_INVOKABLE void setBrightness(int value, const QString& localId = { });
    Q_INVOKABLE QString layerLabel(const QStringList& conditions) const;
    Q_INVOKABLE int addLayer(const QStringList& conditions);
    Q_INVOKABLE bool setLayer(int index, const QStringList& conditions);
    Q_INVOKABLE void removeLayer(int index);
    Q_INVOKABLE int moveLayer(int index, int direction);
    Q_INVOKABLE QVariantList keyPreview(int layer) const;
    Q_INVOKABLE void watchLayerKeys(QWindow* window);
    int previewLayer() const { return m_previewLayer; }
    QVariantList pressedKeys() const;
    Q_INVOKABLE QVariantList liveKeyPreview() const;
    Q_INVOKABLE QVariantList groups() const;
    Q_INVOKABLE void assignKeys(int layer, const QVariantList& ids, int mode, const QString& color,
        const QString& effectId = { }, int opacity = -1);
    Q_INVOKABLE void saveGroup(const QString& name, const QVariantList& ids);
    Q_INVOKABLE void removeGroup(const QString& name);
    Q_INVOKABLE int addRule();
    Q_INVOKABLE void setRule(int index, const QString& key, const QVariant& value);
    Q_INVOKABLE void removeRule(int index);
    Q_INVOKABLE int moveRule(int index, int direction);
    Q_INVOKABLE void selectWindow(int index);
Q_SIGNALS:
    void layerSelectionChanged();
    void presetDevicesChanged();
    void changed();
    void contentChanged();
    void previewChanged();
    void pressedKeysChanged();

private:
    struct SelectionTarget {
        SettingsController* editor;
        int layer;
        QVariantList keys;
    };
    QList<SelectionTarget> selectionTargets(const QVariantList& targets) const;
    bool validateLayerConditions(const QStringList& conditions, int except = -1);
    EffectPreset* editableEffect(const QString& localId);
    EffectList localEffects(int layer, int id) const;
    void storeLocalEffects(int layer, int id, const EffectList& list);
    int effectSource(const EffectAssignments& assignments, int id) const;
    QList<int> effectIds(const QVariantList& ids) const;
    struct Document {
        LoadedDriver driver;
        PresetCollection collection, saved, fallback;
        int revision = 0;
        QString error;
        std::optional<DeviceProfile> clipboard;
        DeviceInstance clipboardDevice;
        QList<EffectPreset> clipboardEffects;
    };
    SettingsController(SettingsController* owner, const QString& profileId);
    SettingsController* documentController() const
    {
        return m_owner ? m_owner : const_cast<SettingsController*>(this);
    }
    std::shared_ptr<Document> m_document;
    SettingsController* m_owner = nullptr;
    QString m_profileId;
    mutable DeviceProfile m_emptyProfile;
    mutable DeviceInstance m_profileDevice;
    QMap<QString, SettingsController*> m_editors;
    QString m_editorsPresetId;
    void syncProfileEditors();
    void makeMultiDevice(LightingPreset& scene);
    DeviceProfile newProfile(const DeviceConfiguration& device) const;
    const DeviceInstance& editingInstance() const;
    const DeviceDescription& editingDescription() const { return editingInstance().description; }
    LoadedDriver& m_driver;
    QTimer m_discoveryTimer;
    struct Controls {
        std::unique_ptr<DeviceControls> backend;
        bool available = false, enabled = false, saved = false, saving = false, reading = false;
        QString status;
    };
    std::map<QString, std::shared_ptr<Controls>> m_controls;
    QString m_selectedDevice;
    std::shared_ptr<Controls> controls() const;
    QList<DeviceConfiguration> configured() const { return configuredDevices(m_collection, m_driver); }
    QList<DeviceConfiguration>& editDevices();
    QString applicationIcon(const QString& windowClass) const;
    mutable std::optional<QMap<QString, QString>> m_applicationIcons;
    EffectControl* m_effect;
    ServiceControl* m_service;
    LayerKeyTracker m_layerKeys;
    int m_previewLayer = -1;
    void updatePreview(bool force = false);
    PresetCollection &m_collection, &m_saved, &m_fallback;
    int m_selected = 0, m_selectedEffect = 0;
    int& m_revision;
    int m_selectedLayer = -1;
    QString m_section = QStringLiteral("keyboard");
    bool m_readable = false, m_pickingWindow = false;
    QString& m_error;
    QString m_windowStatus;
    LightingPreset& currentPreset() { return m_collection.presets[documentController()->m_selected]; }
    const LightingPreset& currentPreset() const
    {
        return m_collection.presets[documentController()->m_selected];
    }
    DeviceProfile& current();
    const DeviceProfile& current() const;
    QList<int> validIds(const QVariantList& ids) const;
    void touch();
    void pruneLocalEffects();
    void loadLightbar(bool refresh = false);
    void saveLightbar();
};
}

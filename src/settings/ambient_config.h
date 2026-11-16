// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "condition_recorder.h"
#include "settings_controller.h"
#include "translations.h"
#include <KQuickConfigModule>
namespace Ambient
{
class AmbientConfig : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(Ambient::SettingsController* controller READ controller CONSTANT)
    Q_PROPERTY(Ambient::ConditionRecorder* recorder READ recorder CONSTANT)
    Q_PROPERTY(bool nativeSection READ nativeSection CONSTANT)
public:
    AmbientConfig(QObject* parent, const KPluginMetaData& data);
    bool nativeSection() const { return m_nativeSection; }
    SettingsController* controller() { return m_controller; }
    ConditionRecorder* recorder() { return m_recorder; }
public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private:
    Translations m_translations;
    bool m_nativeSection = false;
    SettingsController* m_controller;
    ConditionRecorder* m_recorder;
    void updateState();
};
}

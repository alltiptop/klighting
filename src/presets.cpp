// SPDX-License-Identifier: GPL-3.0-or-later
#include "presets.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <cmath>

namespace Ambient
{
namespace
{
    constexpr int MaxFileBytes = 1024 * 1024;
    bool validId(const QString& id)
    {
        static const QRegularExpression pattern(QStringLiteral("\\A[a-zA-Z0-9_-]{1,64}\\z"));
        return pattern.match(id).hasMatch();
    }
    std::optional<QByteArray> readFile(const QString& path, QString* error)
    {
        QFile file(path);
        if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > MaxFileBytes) {
            *error = QStringLiteral("Cannot read %1 (maximum size: 1 MiB).").arg(path);
            return std::nullopt;
        }
        const auto data = file.read(MaxFileBytes + 1);
        if (data.size() > MaxFileBytes || file.error() != QFileDevice::NoError) {
            *error = QStringLiteral("Could not read a complete file: %1").arg(path);
            return std::nullopt;
        }
        return data;
    }
    bool writeFile(const QString& path, const QByteArray& data, QString* error)
    {
        if (data.size() > MaxFileBytes) {
            *error = QStringLiteral("Cannot save %1: maximum file size is 1 MiB.").arg(path);
            return false;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            *error = QStringLiteral("Could not save %1: %2").arg(path, file.errorString());
            return false;
        }
        return true;
    }
    QStringList strings(const QJsonValue& value)
    {
        QStringList result;
        for (const auto& entry : value.toArray())
            result.append(entry.toString());
        return result;
    }
    QJsonObject assignmentsJson(const EffectAssignments& assignments)
    {
        QJsonObject result;
        for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
            QJsonArray list;
            for (const auto& effect : it.value()) {
                QJsonObject entry { { "id", effect.id }, { "opacity", effect.opacity } };
                if (effect.color.isValid())
                    entry.insert("color", effect.color.name());
                list.append(entry);
            }
            result.insert(QString::number(it.key()), list);
        }
        return result;
    }
    bool readAssignments(const QJsonValue& value, EffectAssignments& result)
    {
        if (value.isUndefined())
            return true;
        if (!value.isObject() || value.toObject().size() > 1024)
            return false;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            bool ok;
            const int key = it.key().toInt(&ok);
            if (!ok || key < DefaultAssignment || key > MaxAssignmentId || QString::number(key) != it.key()
                || !it.value().isArray() || it.value().toArray().size() > 32)
                return false;
            EffectList list;
            QSet<QString> seen;
            for (const auto& item : it.value().toArray()) {
                const auto o = item.toObject();
                const auto id = o["id"].toString();
                const int opacity = o["opacity"].toInt(-1);
                const bool localColor = id == QLatin1String(InlineColorId);
                const QColor color(o["color"].toString());
                if (!o["id"].isString() || (!id.isEmpty() && !localColor && !validId(id)) || seen.contains(id)
                    || (localColor ? !color.isValid() || color.alpha() != 255 : !o["color"].isUndefined())
                    || opacity < 0 || opacity > 100 || o["opacity"].toDouble() != opacity)
                    return false;
                seen.insert(id);
                list.append({ id, opacity, localColor ? color : QColor() });
            }
            result.insert(key, list);
        }
        return true;
    }
    // Version 3 used one notification per element. Preserve its Ambient underlay.
    bool readLegacyNotifications(
        const QJsonValue& value, EffectAssignments& result, const KeyboardStyles& styles, bool above = false)
    {
        if (value.isUndefined())
            return true;
        if (!value.isObject() || value.toObject().size() > 1024)
            return false;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            bool ok;
            const int key = it.key().toInt(&ok);
            const auto id = it.value().toString();
            if (!ok || key < 0 || key > MaxAssignmentId || QString::number(key) != it.key()
                || !it.value().isString() || (!id.isEmpty() && !validId(id)))
                return false;
            auto list = above ? EffectList() : implicitEffects(keyStyle(styles, key));
            if (!id.isEmpty())
                list.append({ id });
            result.insert(key, list);
        }
        return true;
    }
    void normalize(AmbientOptions& a)
    {
        a.fps = std::clamp(a.fps, 1, MaxFramesPerSecond);
        a.ledFps = std::clamp(a.ledFps, 1, MaxLedFramesPerSecond);
        a.smoothing = std::clamp(a.smoothing, 0, MaxSmoothingMs);
        a.maximum = std::clamp(a.maximum, 0, 100);
        a.minimum = std::clamp(a.minimum, 0, a.maximum);
        a.temperature = std::clamp(a.temperature, -100, 100);
    }
}
static QJsonObject effectDefinitionJson(const EffectPreset& p)
{
    if (p.type == "brightness")
        return { { "format", "ambient-led-effect" }, { "version", 1 }, { "id", p.id }, { "name", p.name },
            { "type", p.type }, { "options", QJsonObject { { "brightness", p.brightness } } } };
    if (p.type == "solid")
        return { { "format", "ambient-led-effect" }, { "version", 1 }, { "id", p.id }, { "name", p.name },
            { "type", p.type }, { "options", QJsonObject { { "color", p.solidColor.name() } } } };
    if (p.type == "notifications") {
        const auto& n = p.notification;
        return { { "format", "ambient-led-effect" }, { "version", 1 }, { "id", p.id }, { "name", p.name },
            { "type", p.type },
            { "options",
                QJsonObject { { "color", n.color.name() }, { "pattern", n.pattern }, { "period", n.period },
                    { "cycles", n.cycles }, { "urgency", n.urgency }, { "application", n.application },
                    { "category", n.category }, { "eventId", n.eventId }, { "trigger", n.trigger } } } };
    }
    const auto& a = p.ambient;
    return { { "format", "ambient-led-effect" }, { "version", 1 }, { "id", p.id }, { "name", p.name },
        { "type", p.type },
        { "options",
            QJsonObject { { "fps", a.fps }, { "ledFps", a.ledFps }, { "smoothing", a.smoothing },
                { "minimum", a.minimum }, { "maximum", a.maximum }, { "temperature", a.temperature } } } };
}
QJsonObject effectPresetJson(const EffectPreset& p)
{
    auto result = effectDefinitionJson(p);
    result.insert("power",
        QJsonObject { { "pauseOnBattery", p.pauseOnBattery }, { "pauseOnLowBattery", p.pauseOnLowBattery } });
    return result;
}
std::optional<EffectPreset> parseEffectPreset(const QByteArray& data, QString* error)
{
    const auto o = QJsonDocument::fromJson(data).object();
    if (data.size() > MaxFileBytes || o["format"] != QLatin1String("ambient-led-effect")
        || o["version"].toInt() != 1
        || (o["type"] != QLatin1String("ambient") && o["type"] != QLatin1String("notifications")
            && o["type"] != QLatin1String("solid") && o["type"] != QLatin1String("brightness"))
        || !o["options"].isObject()) {
        *error = QStringLiteral("Invalid effect preset or unsupported effect type/version.");
        return std::nullopt;
    }
    EffectPreset p;
    p.id = o["id"].toString();
    p.name = o["name"].toString();
    if (!validId(p.id) || p.name.trimmed().isEmpty() || p.name.size() > 128) {
        *error = QStringLiteral("Invalid effect preset ID or name.");
        return std::nullopt;
    }
    const auto power = o["power"].toObject();
    if ((!o["power"].isUndefined() && !o["power"].isObject())
        || (!power["pauseOnBattery"].isUndefined() && !power["pauseOnBattery"].isBool())
        || (!power["pauseOnLowBattery"].isUndefined() && !power["pauseOnLowBattery"].isBool())) {
        *error = QStringLiteral("Invalid effect power options.");
        return std::nullopt;
    }
    p.pauseOnBattery = power["pauseOnBattery"].toBool();
    p.pauseOnLowBattery = power["pauseOnLowBattery"].toBool();
    p.type = o["type"].toString();
    const auto a = o["options"].toObject();
    if (p.type == "brightness") {
        const auto value = a["brightness"];
        if (!value.isUndefined() && (!value.isDouble() || value.toInt(-1) < 0 || value.toInt(-1) > 100)) {
            *error = QStringLiteral("Brightness must be an integer from 0 to 100.");
            return std::nullopt;
        }
        p.brightness = value.toInt(p.brightness);
        return p;
    }
    if (p.type == "solid") {
        p.solidColor = QColor(a["color"].toString());
        if (!p.solidColor.isValid() || p.solidColor.alpha() != 255) {
            *error = QStringLiteral(
                "Invalid Solid Color. Use an opaque RGB color and effect intensity for opacity.");
            return std::nullopt;
        }
        return p;
    }
    if (p.type == "notifications") {
        auto& n = p.notification;
        n.color = QColor(a["color"].toString(n.color.name()));
        n.pattern = a["pattern"].toString(n.pattern);
        n.period = a["period"].toInt(n.period);
        n.cycles = a["cycles"].toInt(n.cycles);
        n.urgency = a["urgency"].toInt(n.urgency);
        n.application = a["application"].toString();
        n.category = a["category"].toString();
        n.eventId = a["eventId"].toString();
        n.trigger = a["trigger"].toString(n.trigger);
        if (!n.color.isValid() || n.color.alpha() != 255
            || (!a["trigger"].isUndefined() && !a["trigger"].isString())
            || !QStringList { "notification", "power-connected" }.contains(n.trigger)
            || !QStringList { "pulse", "blink", "double" }.contains(n.pattern) || n.period < 500
            || n.period > 5000 || n.cycles < 1 || n.cycles > 10 || n.urgency < -1 || n.urgency > 2
            || n.application.size() > 256 || n.category.size() > 256 || n.eventId.size() > 256) {
            *error = QStringLiteral("Invalid notification color, pattern, timing or metadata filter.");
            return std::nullopt;
        }
        return p;
    }
    auto& v = p.ambient;
    v.fps = a["fps"].toInt(v.fps);
    v.ledFps = a["ledFps"].toInt(v.ledFps);
    v.smoothing = a["smoothing"].toInt(v.smoothing);
    v.minimum = a["minimum"].toInt(v.minimum);
    v.maximum = a["maximum"].toInt(v.maximum);
    v.temperature = a["temperature"].toInt(v.temperature);
    normalize(v);
    return p;
}
namespace
{
    QJsonArray gradientsJson(const StripGradients& gradients)
    {
        QJsonArray result;
        for (const auto& g : gradients)
            result.append(QJsonObject { { "strip", g.strip }, { "from", g.from }, { "to", g.to },
                { "start", g.start.name(QColor::HexArgb) }, { "end", g.end.name(QColor::HexArgb) },
                { "ambient", g.ambient }, { "effect", g.effectId } });
        return result;
    }
    bool readGradients(const QJsonValue& value, StripGradients& gradients)
    {
        if (value.isUndefined())
            return true;
        if (!value.isArray() || value.toArray().size() > 32)
            return false;
        for (const auto& item : value.toArray()) {
            const auto o = item.toObject();
            StripGradient g { o["strip"].toInt(-1), o["from"].toInt(-1), o["to"].toInt(-1),
                QColor(o["start"].toString()), QColor(o["end"].toString()), o["ambient"].toBool(),
                o["effect"].toString() };
            if (g.strip < FirstStripId || g.strip > MaxAssignmentId || g.from < 0 || g.to < 0
                || g.from > MaxKeyId || g.to > MaxKeyId || !g.start.isValid() || !g.end.isValid()
                || (!g.effectId.isEmpty() && (!g.ambient || !validId(g.effectId))))
                return false;
            gradients.append(g);
        }
        return true;
    }
}
QJsonObject presetJson(const LightingPreset& p)
{
    QJsonArray local;
    for (const auto& effect : p.localEffects)
        local.append(effectPresetJson(effect));
    QJsonArray layers;
    for (const auto& layer : p.layers)
        layers.append(QJsonObject { { "name", layer.name },
            { "conditions", QJsonArray::fromStringList(layer.conditions) },
            { "keys", QJsonArray::fromStringList(serializeKeyStyles(layer.styles, true)) },
            { "effects", assignmentsJson(layer.effects) }, { "gradients", gradientsJson(layer.gradients) } });
    return { { "format", "kwin-ambient-led-preset" }, { "version", 5 }, { "id", p.id }, { "name", p.name },
        { "defaultEffect", p.defaultEffectId }, { "localEffects", local },
        { "keyboard",
            QJsonObject { { "device", p.keyboardDeviceId },
                { "base", QJsonArray::fromStringList(serializeKeyStyles(p.base)) }, { "layers", layers },
                { "effects", assignmentsJson(p.effects) },
                { "effectOverrides", assignmentsJson(p.effectOverrides) },
                { "gradients", gradientsJson(p.gradients) },
                { "groups", QJsonDocument::fromJson(serializeKeyGroups(p.groups).toUtf8()).object() } } } };
}
std::optional<LightingPreset> parsePreset(const QByteArray& data, QString* error)
{
    const auto o = QJsonDocument::fromJson(data).object();
    if (data.size() > MaxFileBytes || o["format"] != QLatin1String("kwin-ambient-led-preset")
        || (o["version"].toInt() != 3 && o["version"].toInt() != 4 && o["version"].toInt() != 5)
        || !o["keyboard"].isObject()) {
        *error
            = QStringLiteral("Invalid keyboard preset or unsupported version (expected version 3, 4 or 5).");
        return std::nullopt;
    }
    LightingPreset p;
    p.id = o["id"].toString();
    p.name = o["name"].toString();
    p.defaultEffectId = o["defaultEffect"].toString();
    const auto k = o["keyboard"].toObject();
    p.keyboardDeviceId = k["device"].toString();
    if (!validId(p.id) || p.name.trimmed().isEmpty() || p.name.size() > 128 || !validId(p.keyboardDeviceId)
        || (!p.defaultEffectId.isEmpty() && !validId(p.defaultEffectId))) {
        *error = QStringLiteral("Invalid keyboard preset ID, name, device or effect reference.");
        return std::nullopt;
    }
    if (!o["localEffects"].isUndefined()
        && (!o["localEffects"].isArray() || o["localEffects"].toArray().size() > 128)) {
        *error = QStringLiteral("Invalid local effects (maximum: 128).");
        return std::nullopt;
    }
    QSet<QString> localIds;
    for (const auto& value : o["localEffects"].toArray()) {
        const auto effect = parseEffectPreset(QJsonDocument(value.toObject()).toJson(), error);
        if (!effect || localIds.contains(effect->id)) {
            if (error->isEmpty())
                *error = QStringLiteral("Duplicate local effect ID.");
            return std::nullopt;
        }
        localIds.insert(effect->id);
        p.localEffects.append(*effect);
    }
    auto readStyles = [&](const QJsonValue& value, KeyboardStyles& styles) {
        if (!value.isArray())
            return false;
        const auto entries = strings(value);
        styles = parseKeyStyles(entries);
        if (styles.size() != entries.size())
            return false;
        for (const auto& style : styles)
            if (!style.effectId.isEmpty() && !validId(style.effectId))
                return false;
        return true;
    };
    const bool legacy = o["version"].toInt() == 3;
    if (!readStyles(k["base"], p.base) || !readGradients(k["gradients"], p.gradients)
        || !(legacy ? readLegacyNotifications(k["notifications"], p.effects, p.base)
                    : readAssignments(k["effects"], p.effects))
        || !(legacy ? readLegacyNotifications(k["notificationOverrides"], p.effectOverrides, p.base, true)
                    : readAssignments(k["effectOverrides"], p.effectOverrides))
        || !k["layers"].isArray() || k["layers"].toArray().size() > 32) {
        *error = QStringLiteral("Invalid key assignments or layers (maximum: 32).");
        return std::nullopt;
    }
    p.layers.clear();
    QSet<QString> allowed;
    for (const auto& entry : availableConditions())
        allowed.insert(entry.first);
    for (const auto& value : k["layers"].toArray()) {
        const auto l = value.toObject();
        KeyboardLayer layer;
        layer.name = l["name"].toString();
        layer.conditions = strings(l["conditions"]);
        const QSet<QString> unique(layer.conditions.begin(), layer.conditions.end());
        if (layer.name.trimmed().isEmpty() || layer.name.size() > 128 || layer.conditions.isEmpty()
            || layer.conditions.size() > 8 || unique.size() != layer.conditions.size()
            || !readStyles(l["keys"], layer.styles)
            || !(legacy ? readLegacyNotifications(l["notifications"], layer.effects, layer.styles)
                        : readAssignments(l["effects"], layer.effects))
            || !readGradients(l["gradients"], layer.gradients)
            || std::any_of(layer.conditions.begin(), layer.conditions.end(),
                [&](const auto& c) { return !allowed.contains(c); })) {
            *error = QStringLiteral("Invalid layer name, assignments or key conditions (maximum: 8).");
            return std::nullopt;
        }
        p.layers.append(layer);
    }
    bool validGroups = false;
    p.groups = parseKeyGroups(
        QString::fromUtf8(QJsonDocument(k["groups"].toObject()).toJson(QJsonDocument::Compact)),
        &validGroups);
    if ((!k["groups"].isUndefined() && !k["groups"].isObject()) || !validGroups) {
        *error = QStringLiteral("Invalid key groups (maximum: 32 groups, names up to 64 characters).");
        return std::nullopt;
    }
    return p;
}
QString presetDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kwin-effect-ambient-led");
}
namespace
{
    constexpr auto IndexFile = "settings-v2.json";
    bool managedName(const QString& name)
    {
        static const QRegularExpression pattern(
            QStringLiteral("\\A[a-zA-Z0-9_-]{1,64}-[0-9a-f]{64}\\.json\\z"));
        return pattern.match(name).hasMatch();
    }
    bool validCollection(const PresetCollection& c, QString* error)
    {
        QSet<QString> keyboards, effects;
        QMap<QString, QString> types;
        for (const auto& p : c.effects) {
            if (effects.contains(p.id)
                || !parseEffectPreset(QJsonDocument(effectPresetJson(p)).toJson(), error)) {
                if (error->isEmpty())
                    *error = QStringLiteral("Duplicate effect preset ID.");
                return false;
            }
            effects.insert(p.id);
            types.insert(p.id, p.type);
        }
        for (const auto& p : c.presets) {
            if (keyboards.contains(p.id) || !parsePreset(QJsonDocument(presetJson(p)).toJson(), error)) {
                if (error->isEmpty())
                    *error = QStringLiteral("Duplicate keyboard preset ID.");
                return false;
            }
            keyboards.insert(p.id);
            auto presetTypes = types;
            for (const auto& effect : p.localEffects) {
                if (presetTypes.contains(effect.id)) {
                    *error = QStringLiteral("Local effect ID conflicts with a saved effect.");
                    return false;
                }
                presetTypes.insert(effect.id, effect.type);
            }
            auto checkAssignments = [&](const EffectAssignments& assignments, bool above = false) {
                for (const auto& list : assignments)
                    for (const auto& effect : list)
                        if (above ? presetTypes.value(effect.id) != "notifications"
                                  : !effect.id.isEmpty() && effect.id != QLatin1String(InlineColorId)
                                    && !presetTypes.contains(effect.id))
                            return false;
                return true;
            };
            auto checkColors = [&](const KeyboardStyles& styles) {
                return std::all_of(styles.cbegin(), styles.cend(), [&](const auto& style) {
                    return style.effectId.isEmpty() || types.value(style.effectId) == "ambient";
                });
            };
            const auto checkGradients = [&](const StripGradients& gradients) {
                return std::all_of(gradients.begin(), gradients.end(), [&](const auto& g) {
                    return g.effectId.isEmpty() || types.value(g.effectId) == "ambient";
                });
            };
            if (std::any_of(p.localEffects.cbegin(), p.localEffects.cend(),
                    [&](const auto& effect) { return effect.id == p.defaultEffectId; })
                || !checkGradients(p.gradients)
                || (!p.defaultEffectId.isEmpty() && referencedEffects(p).contains(p.defaultEffectId)
                    && types.value(p.defaultEffectId) != "ambient")
                || !checkColors(p.base) || !checkAssignments(p.effects)
                || !checkAssignments(p.effectOverrides, true)
                || std::any_of(p.layers.cbegin(), p.layers.cend(), [&](const auto& layer) {
                       return !checkColors(layer.styles) || !checkAssignments(layer.effects)
                           || !checkGradients(layer.gradients);
                   })) {
                *error = QStringLiteral(
                    "Use Ambient for default colors and Notifications for effects above all layers.");
                return false;
            }
            if (!(referencedEffects(p) - effects).isEmpty()) {
                *error = QStringLiteral("A keyboard preset references a missing effect preset.");
                return false;
            }
        }
        if (c.presets.isEmpty() || c.presets.size() > 64 || c.effects.size() > 64 || c.rules.size() > 128
            || !keyboards.contains(c.defaultId)) {
            *error = QStringLiteral("Select a default keyboard preset. Limits: 64 keyboard presets, 64 "
                                    "effect presets and 128 application bindings.");
            return false;
        }
        for (const auto& r : c.rules)
            if (r.name.size() > 128 || r.windowClass.size() > 512
                || (r.enabled && (r.windowClass.trimmed().isEmpty() || !keyboards.contains(r.presetId)))) {
                *error = QStringLiteral(
                    "Every enabled application binding needs a window class and a saved keyboard preset.");
                return false;
            }
        QSet<QString> deviceIds;
        if (c.devices) {
            if (c.devices->size() > 32) {
                *error = QStringLiteral("Maximum: 32 devices.");
                return false;
            }
            for (const auto& d : *c.devices) {
                const auto preset = std::find_if(c.presets.begin(), c.presets.end(),
                    [&](const auto& p) { return p.id == d.presetId && p.keyboardDeviceId == d.modelId; });
                if (!validId(d.driverId) || !validId(d.modelId) || d.instanceId.isEmpty()
                    || d.instanceId.size() > 256 || d.instanceId.contains(QChar::Null) || d.name.isEmpty()
                    || d.name.size() > 128 || deviceIds.contains(d.identity())
                    || !QStringList { "average", "red", "green", "blue" }.contains(d.brightnessChannel)
                    || (!d.presetId.isEmpty() && preset == c.presets.end())) {
                    *error = QStringLiteral("Invalid device identity or incompatible device preset.");
                    return false;
                }
                deviceIds.insert(d.identity());
            }
        }
        return true;
    }
}
std::optional<PresetCollection> loadPresets(const PresetCollection& fallback, QString* error)
{
    error->clear();
    const QDir dir(presetDirectory());
    if (!dir.exists(QLatin1String(IndexFile)))
        return fallback;
    const auto bytes = readFile(dir.filePath(QLatin1String(IndexFile)), error);
    if (!bytes)
        return std::nullopt;
    const auto o = QJsonDocument::fromJson(*bytes).object();
    if (o["version"].toInt() != 2 || !o["presets"].isArray() || !o["effects"].isArray()
        || !o["rules"].isArray() || o["presets"].toArray().size() > 64 || o["effects"].toArray().size() > 64
        || o["rules"].toArray().size() > 128) {
        *error = QStringLiteral("Invalid preset settings or unsupported settings version.");
        return std::nullopt;
    }
    PresetCollection c;
    c.effects.clear();
    QSet<QString> automaticPresets;
    if (o.contains("devices")) {
        if (!o["devices"].isArray() || o["devices"].toArray().size() > 32) {
            *error = QStringLiteral("Invalid device list.");
            return std::nullopt;
        }
        c.devices = QList<DeviceConfiguration>();
        for (const auto& item : o["devices"].toArray()) {
            const auto d = item.toObject();
            if (!d["enabled"].isBool()
                || (!d["pauseOnBattery"].isUndefined() && !d["pauseOnBattery"].isBool())
                || (!d["pauseOnLowBattery"].isUndefined() && !d["pauseOnLowBattery"].isBool())
                || (!d["lightbarOffOnBattery"].isUndefined() && !d["lightbarOffOnBattery"].isBool())
                || (!d["lightbarOffOnLowBattery"].isUndefined() && !d["lightbarOffOnLowBattery"].isBool())
                || (!d["brightnessChannel"].isUndefined() && !d["brightnessChannel"].isString())) {
                *error = QStringLiteral("Invalid device options.");
                return std::nullopt;
            }
            c.devices->append({ d["driver"].toString(), d["instance"].toString(), d["model"].toString(),
                d["name"].toString(), d["preset"].toString(), d["enabled"].toBool(),
                d["pauseOnBattery"].toBool(false), d["pauseOnLowBattery"].toBool(false),
                d["lightbarOffOnBattery"].toBool(false), d["lightbarOffOnLowBattery"].toBool(false),
                d["brightnessChannel"].toString("average") });
            if (d["preset"].isString() && d["preset"].toString().isEmpty())
                automaticPresets.insert(c.devices->last().identity());
        }
    }
    c.defaultId = o["defaultPreset"].toString();
    for (const auto& section : { QStringLiteral("presets"), QStringLiteral("effects") }) {
        for (const auto& value : o[section].toArray()) {
            const QString name = value.toString();
            if (!managedName(name)) {
                *error = QStringLiteral("Invalid preset filename.");
                return std::nullopt;
            }
            const auto data = readFile(dir.filePath(section + '/' + name), error);
            if (!data)
                return std::nullopt;
            if (section == QLatin1String("presets")) {
                const auto p = parsePreset(*data, error);
                if (!p)
                    return std::nullopt;
                c.presets.append(*p);
            } else {
                const auto p = parseEffectPreset(*data, error);
                if (!p)
                    return std::nullopt;
                c.effects.append(*p);
            }
        }
    }
    for (const auto& value : o["rules"].toArray()) {
        const auto r = value.toObject();
        c.rules.append({ r["name"].toString(), r["windowClass"].toString(), r["preset"].toString(),
            r["enabled"].toBool(true) });
    }
    // Older files used an empty string for automatic selection; explicit None is now null.
    if (c.devices)
        for (auto& device : *c.devices)
            if (automaticPresets.contains(device.identity()))
                for (const auto& preset : c.presets)
                    if (preset.keyboardDeviceId == device.modelId) {
                        if (device.presetId.isEmpty() || preset.id == c.defaultId)
                            device.presetId = preset.id;
                        if (preset.id == c.defaultId)
                            break;
                    }
    return validCollection(c, error) ? std::optional(c) : std::nullopt;
}
bool savePresets(const PresetCollection& c, QString* error, const PresetCollection* expected,
    const PresetCollection* fallback)
{
    error->clear();
    if (!validCollection(c, error))
        return false;
    const QDir dir(presetDirectory());
    if (!QDir().mkpath(dir.filePath("presets")) || !QDir().mkpath(dir.filePath("effects"))) {
        *error = QStringLiteral("Could not create the preset directories.");
        return false;
    }
    QLockFile lock(dir.filePath(QStringLiteral("settings.lock")));
    if (!lock.tryLock()) {
        *error = QStringLiteral("Another settings window is saving. Try applying again.");
        return false;
    }
    if (expected) {
        const auto onDisk = loadPresets(fallback ? *fallback : defaultCollection(), error);
        if (!onDisk || *onDisk != *expected) {
            *error = QCoreApplication::translate("Ambient::SettingsController",
                "Presets changed on disk. Reset before saving to preserve those changes.");
            return false;
        }
    }
    QJsonObject previous;
    QFile old(dir.filePath(QLatin1String(IndexFile)));
    if (old.open(QIODevice::ReadOnly) && old.size() <= MaxFileBytes)
        previous = QJsonDocument::fromJson(old.readAll()).object();
    // Publish immutable files first; the atomic index commits both collections together.
    QJsonObject index { { "version", 2 }, { "defaultPreset", c.defaultId } };
    if (c.devices) {
        QJsonArray devices;
        for (const auto& d : *c.devices)
            devices.append(QJsonObject { { "driver", d.driverId }, { "instance", d.instanceId },
                { "model", d.modelId }, { "name", d.name },
                { "preset", d.presetId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(d.presetId) },
                { "enabled", d.enabled }, { "pauseOnBattery", d.pauseOnBattery },
                { "pauseOnLowBattery", d.pauseOnLowBattery },
                { "lightbarOffOnBattery", d.lightbarOffOnBattery },
                { "lightbarOffOnLowBattery", d.lightbarOffOnLowBattery },
                { "brightnessChannel", d.brightnessChannel } });
        index["devices"] = devices;
    }
    auto publish = [&](const QString& section, const auto& presets, auto serialize) {
        QJsonArray files;
        for (const auto& p : presets) {
            const auto bytes = QJsonDocument(serialize(p)).toJson();
            const auto hash
                = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            const auto name = p.id + '-' + hash + ".json";
            if (!writeFile(dir.filePath(section + '/' + name), bytes, error))
                return false;
            files.append(name);
        }
        index[section] = files;
        return true;
    };
    if (!publish("presets", c.presets, presetJson) || !publish("effects", c.effects, effectPresetJson))
        return false;
    QJsonArray rules;
    for (const auto& r : c.rules)
        rules.append(QJsonObject { { "name", r.name }, { "windowClass", r.windowClass },
            { "preset", r.presetId }, { "enabled", r.enabled } });
    index["rules"] = rules;
    if (!writeFile(dir.filePath(QLatin1String(IndexFile)), QJsonDocument(index).toJson(), error))
        return false;
    for (const auto& section : { QStringLiteral("presets"), QStringLiteral("effects") })
        for (const auto& value : previous[section].toArray())
            if (managedName(value.toString()) && !index[section].toArray().contains(value))
                QFile::remove(dir.filePath(section + '/' + value.toString()));
    return true;
}
bool exportPreset(
    const LightingPreset& p, const QList<EffectPreset>& effects, const QString& path, QString* error)
{
    error->clear();
    QJsonArray included;
    auto missing = referencedEffects(p);
    for (const auto& effect : effects)
        if (missing.remove(effect.id))
            included.append(effectPresetJson(effect));
    if (!missing.isEmpty()) {
        *error = QStringLiteral("Cannot export a keyboard preset with missing effects.");
        return false;
    }
    return writeFile(path,
        QJsonDocument(QJsonObject { { "format", "ambient-led-keyboard-bundle" }, { "version", 1 },
                          { "keyboard", presetJson(p) }, { "effects", included } })
            .toJson(),
        error);
}
std::optional<PresetBundle> importPreset(const QString& path, QString* error)
{
    error->clear();
    const auto data = readFile(path, error);
    if (!data)
        return std::nullopt;
    const auto o = QJsonDocument::fromJson(*data).object();
    if (o["format"] != QLatin1String("ambient-led-keyboard-bundle") || o["version"].toInt() != 1
        || !o["effects"].isArray() || o["effects"].toArray().size() > 64) {
        *error = QStringLiteral("Invalid keyboard preset bundle.");
        return std::nullopt;
    }
    const auto keyboard = parsePreset(QJsonDocument(o["keyboard"].toObject()).toJson(), error);
    if (!keyboard)
        return std::nullopt;
    PresetBundle bundle { *keyboard, { } };
    for (const auto& value : o["effects"].toArray()) {
        const auto effect = parseEffectPreset(QJsonDocument(value.toObject()).toJson(), error);
        if (!effect)
            return std::nullopt;
        bundle.effects.append(*effect);
    }
    if (!validCollection({ { *keyboard }, keyboard->id, { }, bundle.effects }, error))
        return std::nullopt;
    return bundle;
}
bool exportEffectPreset(const EffectPreset& p, const QString& path, QString* error)
{
    error->clear();
    return writeFile(path, QJsonDocument(effectPresetJson(p)).toJson(), error);
}
std::optional<EffectPreset> importEffectPreset(const QString& path, QString* error)
{
    error->clear();
    const auto data = readFile(path, error);
    return data ? parseEffectPreset(*data, error) : std::nullopt;
}
}

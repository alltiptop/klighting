// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_controller.h"
#include <QUuid>
#include <algorithm>

namespace Ambient
{
QVariantList SettingsController::gradients(int layer) const
{
    if (layer < -1 || layer >= current().layers.size())
        return { };
    QVariantList result;
    for (const auto& g : layer < 0 ? current().gradients : current().layers[layer].gradients)
        result.append(QVariantMap { { "strip", g.strip }, { "from", g.from }, { "to", g.to },
            { "start", g.start.name() }, { "end", g.end.name() },
            { "opacity", qRound(g.start.alphaF() * 100) }, { "ambient", g.ambient },
            { "effectId", g.effectId } });
    return result;
}
bool SettingsController::setGradient(int layer, int index, int strip, int from, int to, const QString& start,
    const QString& end, int opacity, bool ambient, const QString& effectId)
{
    if (!editableLightbar() || layer < -1 || layer >= current().layers.size())
        return false;
    const auto& strips = editingDescription().strips;
    const auto found
        = std::find_if(strips.begin(), strips.end(), [&](const auto& s) { return s.id == strip; });
    if (found == strips.end() || !editingDescription().capabilities.testFlag(DeviceCapability::PerStripColor))
        return false;
    int a = -1, b = -1;
    for (int i = 0; i < found->elements.size(); ++i) {
        if (found->elements[i].id == from)
            a = i;
        if (found->elements[i].id == to)
            b = i;
    }
    QColor first(start), last(end);
    if (a < 0 || b < 0 || !first.isValid() || !last.isValid() || opacity < 0 || opacity > 100
        || (!effectId.isEmpty()
            && (!ambient
                || std::none_of(m_collection.effects.begin(), m_collection.effects.end(),
                    [&](const auto& e) { return e.id == effectId && e.type == "ambient"; }))))
        return false;
    auto& gradients = layer < 0 ? current().gradients : current().layers[layer].gradients;
    if (index < -1 || index >= gradients.size() || (index < 0 && gradients.size() >= 32))
        return false;
    QMap<int, EffectList> remaining;
    auto definitions = m_collection.effects;
    definitions.append(current().localEffects);
    for (int i = std::min(a, b); i <= std::max(a, b); ++i) {
        const int id = found->elements[i].id;
        auto list = localEffects(layer, id);
        list.removeIf([&](const auto& e) {
            return std::none_of(definitions.begin(), definitions.end(),
                [&](const auto& p) { return p.id == e.id && p.type == "notifications"; });
        });
        if (ambient && list.size() >= 32) {
            m_error = tr("Use at most 32 effects per element and layer.");
            Q_EMIT documentController()->changed();
            return false;
        }
        if (ambient)
            list.prepend({ effectId.isEmpty() ? current().defaultEffectId : effectId });
        remaining.insert(id, list);
    }
    first.setAlpha((opacity * 255 + 50) / 100);
    last.setAlpha(first.alpha());
    const StripGradient g { strip, from, to, first, last, ambient, effectId };
    if (index < 0)
        gradients.append(g);
    else
        gradients[index] = g;
    auto& assignments = layer < 0 ? current().effects : current().layers[layer].effects;
    for (auto it = remaining.cbegin(); it != remaining.cend(); ++it)
        assignments.insert(it.key(), it.value());
    auto& styles = layer < 0 ? current().base : current().layers[layer].styles;
    for (int i = std::min(a, b); i <= std::max(a, b); ++i)
        styles.remove(found->elements[i].id);
    touch();
    return true;
}
void SettingsController::removeGradient(int layer, int index)
{
    if (!editableLightbar() || layer < -1 || layer >= current().layers.size())
        return;
    auto& gradients = layer < 0 ? current().gradients : current().layers[layer].gradients;
    if (index >= 0 && index < gradients.size()) {
        gradients.removeAt(index);
        touch();
    }
}
QVariantList SettingsController::effectPresets() const
{
    QVariantList result;
    for (const auto& p : m_collection.effects)
        result << QVariantMap { { "id", p.id }, { "name", p.name }, { "type", p.type },
            { "solidColor", p.solidColor.name() } };
    return result;
}
namespace
{
    QVariantMap effectOptions(const EffectPreset& p)
    {
        const auto& a = p.ambient;
        return { { "id", p.id }, { "name", p.name }, { "type", p.type }, { "fps", a.fps },
            { "ledFps", a.ledFps }, { "smoothing", a.smoothing }, { "minimum", a.minimum },
            { "maximum", a.maximum }, { "temperature", a.temperature }, { "brightness", p.brightness },
            { "notificationColor", p.notification.color.name() }, { "solidColor", p.solidColor.name() },
            { "pattern", p.notification.pattern }, { "period", p.notification.period },
            { "cycles", p.notification.cycles }, { "urgency", p.notification.urgency },
            { "application", p.notification.application }, { "category", p.notification.category },
            { "eventId", p.notification.eventId }, { "trigger", p.notification.trigger },
            { "pauseOnBattery", p.pauseOnBattery }, { "pauseOnLowBattery", p.pauseOnLowBattery } };
    }
}
QVariantMap SettingsController::effectPreset() const
{
    if (m_selectedEffect < 0 || m_selectedEffect >= m_collection.effects.size())
        return { };
    return effectOptions(m_collection.effects[m_selectedEffect]);
}
QVariantMap SettingsController::localEffectOptions(const QString& id) const
{
    for (const auto& effect : current().localEffects)
        if (effect.id == id)
            return effectOptions(effect);
    return { };
}
QVariantMap SettingsController::assignedEffectOptions(const QString& id) const
{
    const auto resolvedId = id.isEmpty() ? current().defaultEffectId : id;
    const auto local = localEffectOptions(resolvedId);
    if (!local.isEmpty())
        return local;
    for (const auto& effect : m_collection.effects)
        if (effect.id == resolvedId)
            return effectOptions(effect);
    return { };
}
EffectPreset* SettingsController::editableEffect(const QString& id)
{
    if (id.isEmpty())
        return m_selectedEffect >= 0 ? &m_collection.effects[m_selectedEffect] : nullptr;
    for (auto& effect : current().localEffects)
        if (effect.id == id)
            return &effect;
    for (auto& effect : m_collection.effects)
        if (effect.id == id)
            return &effect;
    return nullptr;
}
QString SettingsController::localizeEffect(
    int layer, const QVariantList& ids, const QString& id, bool aboveLayers)
{
    if (layer < -1 || layer >= current().layers.size())
        return { };
    pruneLocalEffects();
    auto* original = editableEffect(id);
    // Shared presets are intentionally edited in place; only local assignments are copied.
    if (!original || id.isEmpty() || localEffectOptions(id).isEmpty())
        return { };
    const auto selected = effectIds(ids);
    if (selected.isEmpty())
        return { };
    bool shared = false;
    const auto check = [&](const EffectAssignments& assignments, int sourceLayer, bool above) {
        for (auto it = assignments.cbegin(); it != assignments.cend(); ++it)
            if ((above != aboveLayers || (!above && sourceLayer != layer) || !selected.contains(it.key()))
                && std::any_of(it->begin(), it->end(), [&](const auto& e) { return e.id == id; }))
                shared = true;
    };
    check(current().effects, -1, false);
    check(current().effectOverrides, -1, true);
    for (int i = 0; i < current().layers.size(); ++i)
        check(current().layers[i].effects, i, false);
    if (!shared)
        return id;
    if (current().localEffects.size() >= 128) {
        m_error = tr("Use at most 128 local effects per preset.");
        Q_EMIT documentController()->changed();
        return { };
    }
    auto copy = *original;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMap<int, EffectList> lists;
    for (int key : selected) {
        auto list = aboveLayers
            ? current().effectOverrides.value(effectSource(current().effectOverrides, key))
            : localEffects(layer, key);
        bool replaced = false;
        for (auto& entry : list)
            if (entry.id == id) {
                entry.id = copy.id;
                replaced = true;
            }
        if (replaced)
            lists.insert(key, list);
    }
    if (lists.isEmpty())
        return { };
    current().localEffects.append(copy);
    for (auto it = lists.cbegin(); it != lists.cend(); ++it)
        if (aboveLayers)
            current().effectOverrides.insert(it.key(), it.value());
        else
            storeLocalEffects(layer, it.key(), it.value());
    touch();
    return copy.id;
}
QString SettingsController::addLocalEffect(int layer, const QVariantList& ids, const QString& type)
{
    if (layer < -1 || layer >= current().layers.size() || effectIds(ids).isEmpty()
        || !QStringList { "solid", "ambient", "notifications", "brightness" }.contains(type))
        return { };
    pruneLocalEffects();
    if (current().localEffects.size() >= 128) {
        m_error = tr("Use at most 128 local effects per preset.");
        Q_EMIT documentController()->changed();
        return { };
    }
    for (int id : effectIds(ids))
        if (localEffects(layer, id).size() >= 32) {
            m_error = tr("Use at most 32 effects per element and layer.");
            Q_EMIT documentController()->changed();
            return { };
        }
    EffectPreset effect;
    effect.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    effect.type = type;
    effect.name = type == "solid" ? tr("Solid Color")
        : type == "ambient"       ? tr("Ambient")
        : type == "brightness"    ? tr("Brightness")
                                  : tr("Notifications");
    current().localEffects.append(effect);
    addAssignedEffect(layer, ids, effect.id);
    return effect.id;
}
void SettingsController::selectEffect(int index)
{
    if (index >= 0 && index < m_collection.effects.size() && index != m_selectedEffect) {
        m_selectedEffect = index;
        touch();
    }
}
void SettingsController::setSection(const QString& section)
{
    if (m_section != section
        && QStringList { "base", "keyboard", "effects", "applications" }.contains(section)) {
        m_section = section;
        Q_EMIT documentController()->changed();
    }
}
void SettingsController::setDefaultEffect(const QString& id)
{
    if (id.isEmpty()
        || std::any_of(m_collection.effects.begin(), m_collection.effects.end(),
            [&](const auto& e) { return e.id == id && e.type == "ambient"; })) {
        current().defaultEffectId = id;
        touch();
    }
}
void SettingsController::setEffectName(const QString& name)
{
    if (m_selectedEffect < 0 || name.trimmed().isEmpty() || name.size() > 128)
        return;
    m_collection.effects[m_selectedEffect].name = name;
    touch();
}
bool SettingsController::addEffect(const QString& type, bool duplicate)
{
    if ((type != QLatin1String("ambient") && type != QLatin1String("notifications")
            && type != QLatin1String("solid") && type != QLatin1String("brightness"))
        || m_collection.effects.size() >= 64 || (duplicate && m_selectedEffect < 0))
        return false;
    auto p = duplicate ? m_collection.effects[m_selectedEffect] : EffectPreset();
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!duplicate)
        p.type = type;
    p.name = duplicate            ? tr("%1 copy").arg(p.name)
        : type == "notifications" ? tr("New Notifications preset")
        : type == "solid"         ? tr("New Solid Color preset")
        : type == "brightness"    ? tr("New Brightness preset")
                                  : tr("New Ambient preset");
    m_collection.effects << p;
    m_selectedEffect = m_collection.effects.size() - 1;
    touch();
    return true;
}
bool SettingsController::removeEffect()
{
    if (m_selectedEffect < 0)
        return false;
    const auto id = m_collection.effects[m_selectedEffect].id;
    for (const auto& p : m_collection.presets) {
        if (referencedEffects(p).contains(id)) {
            m_error = tr("This effect is used by keyboard preset \"%1\". Change its assignments first.")
                          .arg(p.name);
            Q_EMIT documentController()->changed();
            return false;
        }
    }
    for (auto& preset : m_collection.presets)
        for (auto* profile : deviceProfiles(preset))
            if (profile->defaultEffectId == id)
                profile->defaultEffectId.clear();
    m_collection.effects.removeAt(m_selectedEffect);
    m_selectedEffect = m_collection.effects.isEmpty() ? -1 : 0;
    touch();
    return true;
}
void SettingsController::importEffectFile(const QUrl& path)
{
    if (!path.isLocalFile() || m_collection.effects.size() >= 64)
        return;
    auto p = importEffectPreset(path.toLocalFile(), &m_error);
    if (p) {
        p->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_collection.effects << *p;
        m_selectedEffect = m_collection.effects.size() - 1;
        touch();
    } else
        Q_EMIT documentController()->changed();
}
void SettingsController::exportEffectFile(const QUrl& path)
{
    if (!path.isLocalFile() || m_selectedEffect < 0)
        return;
    exportEffectPreset(m_collection.effects[m_selectedEffect], path.toLocalFile(), &m_error);
    Q_EMIT documentController()->changed();
}
void SettingsController::setNotification(const QString& key, const QVariant& value, const QString& localId)
{
    if (key == "pauseOnBattery" || key == "pauseOnLowBattery") {
        setEffectPower(key, value.toBool(), localId);
        return;
    }
    auto* effect = editableEffect(localId);
    if (!effect || effect->type != "notifications")
        return;
    auto& n = effect->notification;
    if (key == "color") {
        const QColor color(value.toString());
        if (!color.isValid() || color.alpha() != 255)
            return;
        n.color = color;
    } else if (key == "trigger") {
        if (!QStringList { "notification", "power-connected" }.contains(value.toString()))
            return;
        n.trigger = value.toString();
    } else if (key == "pattern") {
        if (!QStringList { "pulse", "blink", "double" }.contains(value.toString()))
            return;
        n.pattern = value.toString();
    } else if (key == "period")
        n.period = std::clamp(value.toInt(), 500, 5000);
    else if (key == "cycles")
        n.cycles = std::clamp(value.toInt(), 1, 10);
    else if (key == "urgency")
        n.urgency = std::clamp(value.toInt(), -1, 2);
    else if (key == "application")
        n.application = value.toString().left(256);
    else if (key == "category")
        n.category = value.toString().left(256);
    else if (key == "eventId")
        n.eventId = value.toString().left(256);
    else
        return;
    touch();
}
int SettingsController::effectSource(const EffectAssignments& assignments, int id) const
{
    if (assignments.contains(id))
        return id;
    for (const auto& strip : editingDescription().strips)
        if (assignments.contains(strip.id)
            && std::any_of(
                strip.elements.begin(), strip.elements.end(), [&](const auto& e) { return e.id == id; }))
            return strip.id;
    return assignments.contains(DefaultAssignment) ? DefaultAssignment : id;
}
QList<int> SettingsController::effectIds(const QVariantList& ids) const
{
    if (ids.isEmpty() && !editingDescription().id.isEmpty())
        return { DefaultAssignment };
    auto result = validIds(ids);
    const auto& keys = editingDescription().keys;
    if (!keys.isEmpty()
        && std::all_of(keys.begin(), keys.end(), [&](const auto& key) { return result.contains(key.id); }))
        return { DefaultAssignment };
    for (const auto& strip : editingDescription().strips)
        if (result.contains(strip.id)
            && editingDescription().capabilities.testFlag(DeviceCapability::PerStripColor))
            for (const auto& element : strip.elements)
                if (!result.contains(element.id))
                    result.append(element.id);
    return result;
}
EffectList SettingsController::localEffects(int layer, int id) const
{
    const auto& assignments = layer < 0 ? current().effects : current().layers[layer].effects;
    if (id == DefaultAssignment) {
        if (assignments.contains(id))
            return assignments[id];
        if (layer >= 0)
            return localEffects(-1, id);
        if (current().defaultEffectId.isEmpty())
            return { };
        return { { current().defaultEffectId } };
    }
    int source = effectSource(assignments, id);
    const auto styles = layer < 0 ? expandStyles(current().base, current().gradients, editingDescription())
                                  : expandStyles(current().layers[layer].styles,
                                        current().layers[layer].gradients, editingDescription());
    auto style = keyStyle(styles, id, layer >= 0);
    if (source == DefaultAssignment && styles.contains(id) && style.mode != KeyMode::Inherit)
        source = id;
    if (source == DefaultAssignment)
        return assignments[source];
    if (style.mode == KeyMode::Inherit && !assignments.contains(source))
        return localEffects(-1, id);
    if (style.mode == KeyMode::Inherit)
        style = keyStyle(expandStyles(current().base, current().gradients, editingDescription()), id);
    auto list = assignments.value(source, implicitEffects(style));
    for (auto& effect : list)
        if (effect.id.isEmpty())
            effect.id = current().defaultEffectId;
    list.removeIf([](const auto& effect) { return effect.id.isEmpty(); });
    if (style.mode != KeyMode::Off && style.color.alpha() > 0
        && std::none_of(
            list.begin(), list.end(), [](const auto& e) { return e.id == QLatin1String(InlineColorId); })) {
        int position = 0;
        for (int i = 0; i < list.size(); ++i) {
            const auto ref = list[i].id.isEmpty() ? current().defaultEffectId : list[i].id;
            if (std::any_of(m_collection.effects.cbegin(), m_collection.effects.cend(),
                    [&](const auto& effect) { return effect.id == ref && effect.type == "ambient"; }))
                position = i + 1;
        }
        QColor color = style.color;
        color.setAlpha(255);
        list.insert(position, { InlineColorId, qRound(style.color.alphaF() * 100), color });
    }
    return list;
}
void SettingsController::storeLocalEffects(int layer, int id, const EffectList& list)
{
    auto& assignments = layer < 0 ? current().effects : current().layers[layer].effects;
    auto& styles = layer < 0 ? current().base : current().layers[layer].styles;
    assignments.insert(id, list);
    // The former color underlay is now an ordered Solid Color entry.
    if (id != DefaultAssignment)
        styles.insert(id, { KeyMode::Static, Qt::transparent });
}
void SettingsController::setEffectPower(const QString& key, bool value, const QString& localId)
{
    auto* effect = editableEffect(localId);
    if (!effect)
        return;
    if (key == "pauseOnBattery")
        effect->pauseOnBattery = value;
    else if (key == "pauseOnLowBattery")
        effect->pauseOnLowBattery = value;
    else
        return;
    touch();
}
void SettingsController::setSolidColor(const QString& color, const QString& localId)
{
    const QColor value(color);
    auto* effect = editableEffect(localId);
    if (!effect || effect->type != "solid" || !value.isValid() || value.alpha() != 255)
        return;
    effect->solidColor = value;
    touch();
}
void SettingsController::setAssignedColor(int layer, const QVariantList& ids, const QString& color)
{
    const QColor value(color);
    if (layer < -1 || layer >= current().layers.size() || !value.isValid() || value.alpha() != 255)
        return;
    QMap<int, EffectList> lists;
    for (int id : effectIds(ids))
        lists.insert(id, localEffects(layer, id));
    for (auto it = lists.cbegin(); it != lists.cend(); ++it) {
        auto list = it.value();
        bool found = false;
        for (auto& effect : list)
            if (effect.id == QLatin1String(InlineColorId)) {
                effect.color = value;
                found = true;
            }
        if (found)
            storeLocalEffects(layer, it.key(), list);
    }
    touch();
}
QVariantMap SettingsController::selectionEffects(int layer, const QVariantList& ids) const
{
    QVariantList rows;
    auto definitions = m_collection.effects;
    definitions.append(current().localEffects);
    if (layer < -1 || layer >= current().layers.size())
        return { { "items", rows } };
    const auto selected = effectIds(ids);
    bool mixed = false;
    EffectList firstNormal, firstAbove;
    for (int i = 0; i < selected.size(); ++i) {
        const int id = selected[i];
        const auto normal = localEffects(layer, id);
        const auto above = current().effectOverrides.value(effectSource(current().effectOverrides, id));
        if (i == 0) {
            firstNormal = normal;
            firstAbove = above;
        } else if (normal != firstNormal || above != firstAbove)
            mixed = true;
        const auto add = [&](const EffectList& list, bool isAbove) {
            for (const auto& effect : list) {
                int found = -1;
                for (int j = 0; j < rows.size(); ++j) {
                    const auto row = rows[j].toMap();
                    if (row["id"].toString() == effect.id && row["above"].toBool() == isAbove) {
                        found = j;
                        break;
                    }
                }
                if (found >= 0) {
                    auto row = rows[found].toMap();
                    row["count"] = row["count"].toInt() + 1;
                    if (row["opacity"].toInt() != effect.opacity)
                        row["opacity"] = -1;
                    if (row["color"].toString() != (effect.color.isValid() ? effect.color.name() : QString()))
                        row["color"] = QString();
                    rows[found] = row;
                } else {
                    QString name = effect.id == QLatin1String(InlineColorId) ? tr("Solid Color")
                                                                             : tr("Default Ambient");
                    QString type = effect.id == QLatin1String(InlineColorId) ? QStringLiteral("solid")
                                                                             : QStringLiteral("ambient");
                    for (const auto& preset : definitions)
                        if (preset.id == (effect.id.isEmpty() ? current().defaultEffectId : effect.id)) {
                            name = preset.name;
                            type = preset.type;
                            break;
                        }
                    rows.append(QVariantMap { { "id", effect.id }, { "name", name }, { "type", type },
                        { "above", isAbove }, { "opacity", effect.opacity }, { "count", 1 },
                        { "local", !localEffectOptions(effect.id).isEmpty() },
                        { "color", effect.color.isValid() ? effect.color.name() : QString() } });
                }
            }
        };
        add(normal, false);
        add(above, true);
    }
    // Above-layer effects always compose after the local stack.
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.toMap()["above"].toBool() < b.toMap()["above"].toBool();
    });
    return { { "items", rows }, { "mixed", mixed }, { "count", selected.size() },
        { "defaults", selected == QList<int> { DefaultAssignment } } };
}
void SettingsController::addAssignedEffect(
    int layer, const QVariantList& ids, const QString& effectId, bool aboveLayers)
{
    if (layer < -1 || layer >= current().layers.size())
        return;
    auto definitions = m_collection.effects;
    definitions.append(current().localEffects);
    const auto preset = std::find_if(
        definitions.cbegin(), definitions.cend(), [&](const auto& e) { return e.id == effectId; });
    if ((!effectId.isEmpty() && effectId != QLatin1String(InlineColorId) && preset == definitions.cend())
        || (aboveLayers && (preset == definitions.cend() || preset->type != "notifications")))
        return;
    auto& assignments = aboveLayers ? current().effectOverrides
        : layer < 0                 ? current().effects
                                    : current().layers[layer].effects;
    QMap<int, EffectList> updated;
    for (int id : effectIds(ids)) {
        auto list = aboveLayers ? assignments.value(effectSource(assignments, id)) : localEffects(layer, id);
        if (std::any_of(list.begin(), list.end(), [&](const auto& effect) { return effect.id == effectId; }))
            continue;
        if (list.size() >= 32) {
            m_error = tr("Use at most 32 effects per element and layer.");
            Q_EMIT documentController()->changed();
            return;
        }
        list.append(
            { effectId, 100, effectId == QLatin1String(InlineColorId) ? QColor(Qt::white) : QColor() });
        updated.insert(id, list);
    }
    for (auto it = updated.cbegin(); it != updated.cend(); ++it)
        if (aboveLayers)
            assignments.insert(it.key(), it.value());
        else
            storeLocalEffects(layer, it.key(), it.value());
    if (!updated.isEmpty())
        touch();
}
void SettingsController::updateAssignedEffect(
    int layer, const QVariantList& ids, const QString& effectId, bool wasAbove, bool aboveLayers, int opacity)
{
    if (layer < -1 || layer >= current().layers.size())
        return;
    auto definitions = m_collection.effects;
    definitions.append(current().localEffects);
    if (aboveLayers && std::none_of(definitions.cbegin(), definitions.cend(), [&](const auto& e) {
            return e.id == effectId && e.type == "notifications";
        }))
        return;
    const auto selected = effectIds(ids);
    // Check capacity before moving anything between the local and global stacks.
    if (wasAbove != aboveLayers)
        for (int id : selected) {
            const auto target = aboveLayers
                ? current().effectOverrides.value(effectSource(current().effectOverrides, id))
                : localEffects(layer, id);
            if (target.size() >= 32 && std::none_of(target.begin(), target.end(), [&](const auto& e) {
                    return e.id == effectId;
                }))
                return;
        }
    QMap<int, EffectList> normalLists, aboveLists;
    for (int id : selected) {
        normalLists.insert(id, localEffects(layer, id));
        aboveLists.insert(id, current().effectOverrides.value(effectSource(current().effectOverrides, id)));
    }
    for (int id : selected) {
        auto normal = normalLists.value(id);
        auto above = aboveLists.value(id);
        auto& source = wasAbove ? above : normal;
        const auto found
            = std::find_if(source.begin(), source.end(), [&](const auto& e) { return e.id == effectId; });
        if (found == source.end())
            continue;
        auto effect = *found;
        source.erase(found);
        effect.opacity = std::clamp(opacity < 0 ? effect.opacity : opacity, 0, 100);
        auto& target = aboveLayers ? above : normal;
        if (wasAbove == aboveLayers) {
            // Updating opacity keeps the original position.
            target = wasAbove ? aboveLists.value(id) : normalLists.value(id);
            for (auto& entry : target)
                if (entry.id == effectId)
                    entry.opacity = effect.opacity;
        } else {
            target.removeIf([&](const auto& entry) { return entry.id == effectId; });
            target.append(effect);
        }
        if (!wasAbove || !aboveLayers)
            storeLocalEffects(layer, id, normal);
        if (wasAbove || aboveLayers)
            current().effectOverrides.insert(id, above);
    }
    touch();
}
void SettingsController::removeAssignedEffect(
    int layer, const QVariantList& ids, const QString& effectId, bool aboveLayers)
{
    if (layer < -1 || layer >= current().layers.size())
        return;
    auto& assignments = aboveLayers ? current().effectOverrides
        : layer < 0                 ? current().effects
                                    : current().layers[layer].effects;
    QMap<int, EffectList> lists;
    for (int id : effectIds(ids))
        lists.insert(
            id, aboveLayers ? assignments.value(effectSource(assignments, id)) : localEffects(layer, id));
    for (auto it = lists.cbegin(); it != lists.cend(); ++it) {
        const int id = it.key();
        auto list = it.value();
        if (list.removeIf([&](const auto& effect) { return effect.id == effectId; })) {
            if (aboveLayers)
                assignments.insert(id, list);
            else
                storeLocalEffects(layer, id, list);
        }
    }
    touch();
}
void SettingsController::moveAssignedEffect(
    int layer, const QVariantList& ids, const QString& effectId, bool aboveLayers, int offset)
{
    if (layer < -1 || layer >= current().layers.size() || offset == 0 || offset <= -32 || offset >= 32)
        return;
    auto& assignments = aboveLayers ? current().effectOverrides
        : layer < 0                 ? current().effects
                                    : current().layers[layer].effects;
    QMap<int, EffectList> lists;
    for (int id : effectIds(ids))
        lists.insert(
            id, aboveLayers ? assignments.value(effectSource(assignments, id)) : localEffects(layer, id));
    for (auto it = lists.cbegin(); it != lists.cend(); ++it) {
        const int id = it.key();
        auto list = it.value();
        for (int i = 0; i < list.size(); ++i)
            if (list[i].id == effectId && i + offset >= 0 && i + offset < list.size()) {
                list.move(i, i + offset);
                if (aboveLayers)
                    assignments.insert(id, list);
                else
                    storeLocalEffects(layer, id, list);
                break;
            }
    }
    touch();
}
void SettingsController::setKeyColor(int layer, const QVariantList& ids, const QString& color, int opacity)
{
    if (layer < -1 || layer >= current().layers.size())
        return;
    QColor value(color);
    if (!value.isValid())
        return;
    value.setAlpha((std::clamp(opacity, 0, 100) * 255 + 50) / 100);
    auto& assignments = layer < 0 ? current().effects : current().layers[layer].effects;
    auto& styles = layer < 0 ? current().base : current().layers[layer].styles;
    // Materialize the implicit Ambient before changing only the color overlay.
    QMap<int, EffectList> lists;
    for (int id : effectIds(ids))
        lists.insert(id, localEffects(layer, id));
    for (auto it = lists.cbegin(); it != lists.cend(); ++it) {
        assignments.insert(it.key(), it.value());
        styles.insert(it.key(), { KeyMode::Static, value });
    }
    touch();
}
void SettingsController::clearKeyLighting(int layer, const QVariantList& ids, bool inherit)
{
    if (layer < -1 || layer >= current().layers.size() || (inherit && layer < 0))
        return;
    auto& assignments = layer < 0 ? current().effects : current().layers[layer].effects;
    auto& styles = layer < 0 ? current().base : current().layers[layer].styles;
    for (int id : effectIds(ids)) {
        if (inherit) {
            assignments.remove(id);
            styles.remove(id);
        } else {
            assignments.insert(id, { });
            if (id != DefaultAssignment)
                styles.insert(id, { KeyMode::Off });
        }
    }
    touch();
}
void SettingsController::addNotificationLayout(const QString& effectId)
{
    if (!editableKeys())
        return;
    QVariantList ids;
    for (const auto& key : editingDescription().keys)
        if (QStringList { "F9", "F10", "F11", "F12" }.contains(key.label, Qt::CaseInsensitive))
            ids.append(key.id);
    if (ids.isEmpty()) {
        m_error = tr("This keyboard does not expose F9-F12 lighting zones.");
        Q_EMIT documentController()->changed();
        return;
    }
    QString id = effectId;
    if (id.isEmpty()) {
        if (!addEffect(QStringLiteral("notifications")))
            return;
        m_collection.effects[m_selectedEffect].name = tr("Notifications");
        id = m_collection.effects[m_selectedEffect].id;
    }
    addAssignedEffect(-1, ids, id, true);
}
void SettingsController::setBrightness(int value, const QString& localId)
{
    auto* effect = editableEffect(localId);
    if (!effect || effect->type != "brightness")
        return;
    effect->brightness = std::clamp(value, 0, 100);
    touch();
}
void SettingsController::setAmbient(const QString& key, int value, const QString& localId)
{
    if (key == "pauseOnBattery" || key == "pauseOnLowBattery") {
        setEffectPower(key, value, localId);
        return;
    }
    auto* effect = editableEffect(localId);
    if (!effect || effect->type != "ambient")
        return;
    auto& a = effect->ambient;
    if (key == "fps")
        a.fps = std::clamp(value, 1, MaxFramesPerSecond);
    else if (key == "ledFps")
        a.ledFps = std::clamp(value, 1, MaxLedFramesPerSecond);
    else if (key == "smoothing")
        a.smoothing = std::clamp(value, 0, MaxSmoothingMs);
    else if (key == "minimum") {
        a.minimum = std::clamp(value, 0, 100);
        a.maximum = std::max(a.maximum, a.minimum);
    } else if (key == "maximum") {
        a.maximum = std::clamp(value, 0, 100);
        a.minimum = std::min(a.minimum, a.maximum);
    } else if (key == "temperature")
        a.temperature = std::clamp(value, -100, 100);
    else
        return;
    touch();
}
}

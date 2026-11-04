// SPDX-License-Identifier: GPL-3.0-or-later
#include "integration_startup_guard.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace Ambient
{
namespace
{
    struct State {
        int attempts = 0;
        QString owner;
        qint64 expires = 0;
    };
    bool validOwner(const QString& owner) { return owner.startsWith(':') && owner.size() <= 128; }
    bool readState(const QString& path, State& state)
    {
        QFile file(path);
        if (!file.exists())
            return true;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
            return false;
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            return false;
        const auto obj = document.object();
        const double attempts = obj.value("attempts").toDouble(-1);
        const double expires = obj.value("expires").toDouble(-1);
        const auto owner = obj.value("owner").toString();
        if (obj.value("version") != 1 || !obj.value("owner").isString()
            || (!owner.isEmpty() && !validOwner(owner)) || !std::isfinite(attempts)
            || attempts != std::floor(attempts) || attempts < 0
            || attempts > IntegrationStartupGuard::MaximumAttempts || !std::isfinite(expires)
            || expires != std::floor(expires) || expires < 0 || expires > 9007199254740991.0)
            return false;
        state = { int(attempts), owner, qint64(expires) };
        return true;
    }
    template <typename Change> QString update(const QString& path, bool reset, Change change)
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return QStringLiteral("Cannot create desktop startup state directory");
        QLockFile lock(path + ".lock");
        if (!lock.tryLock())
            return QStringLiteral("Cannot lock desktop startup state");
        State state;
        if (!reset && !readState(path, state))
            return QStringLiteral("Invalid desktop startup state; retry from Lighting settings");
        if (const auto error = change(state); !error.isEmpty())
            return error;
        const auto bytes = QJsonDocument(QJsonObject { { "version", 1 }, { "attempts", state.attempts },
                                             { "owner", state.owner }, { "expires", state.expires } })
                               .toJson(QJsonDocument::Compact);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)
            || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || file.write(bytes) != bytes.size() || !file.commit())
            return QStringLiteral("Cannot save desktop startup state; loading remains paused");
        return { };
    }
}
QString IntegrationStartupGuard::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
        + QStringLiteral("/klighting/desktop-startup.json");
}
IntegrationStartupGuard::IntegrationStartupGuard(QString path)
    : m_path(std::move(path))
{
}
QString IntegrationStartupGuard::prepare(const QString& owner, bool manual, qint64 now) const
{
    if (!validOwner(owner) || now <= 0 || now > 9007199254740991LL - PermitLifetimeMs)
        return QStringLiteral("Invalid desktop startup request");
    return update(m_path, manual, [&](State& state) -> QString {
        if (state.attempts >= MaximumAttempts)
            return QStringLiteral("Automatic desktop loading paused after repeated startup attempts; "
                                  "retry from Lighting settings");
        ++state.attempts;
        state.owner = owner;
        state.expires = now + PermitLifetimeMs;
        return { };
    });
}
QString IntegrationStartupGuard::consume(const QString& owner, qint64 now) const
{
    return update(m_path, false, [&](State& state) -> QString {
        if (!validOwner(owner) || state.owner != owner || state.attempts == 0 || now <= 0
            || state.expires <= now || state.expires - now > PermitLifetimeMs)
            return QStringLiteral("Desktop integration requires a fresh startup permit from klightingd");
        // Persist consumption before any compositor-facing object is constructed.
        state.expires = 0;
        return { };
    });
}
QString IntegrationStartupGuard::cancel(const QString& owner) const
{
    return update(m_path, false, [&](State& state) -> QString {
        if (state.owner != owner)
            return QStringLiteral("Desktop startup owner changed");
        state.expires = 0;
        return { };
    });
}
QString IntegrationStartupGuard::recovered(const QString& owner, qint64 stableMs) const
{
    if (stableMs < RecoveryIntervalMs)
        return QStringLiteral("Desktop integration has not remained available long enough");
    return update(m_path, false, [&](State& state) -> QString {
        if (state.attempts == 0 && state.owner.isEmpty() && state.expires == 0)
            return { }; // Already recovered before the daemon restarted.
        if (state.owner != owner || state.expires != 0)
            return QStringLiteral("Desktop startup owner changed or loading is incomplete");
        state = { };
        return { };
    });
}
}

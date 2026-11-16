// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDateTime>
#include <QString>

namespace Ambient
{
// Shared recovery state, not a security boundary against the same user.
class IntegrationStartupGuard
{
public:
    static constexpr int MaximumAttempts = 3;
    static constexpr qint64 PermitLifetimeMs = 10000;
    static constexpr qint64 RecoveryIntervalMs = 60000;
    static QString defaultPath();
    explicit IntegrationStartupGuard(QString path = defaultPath());
    QString prepare(
        const QString& owner, bool manual = false, qint64 now = QDateTime::currentMSecsSinceEpoch()) const;
    QString consume(const QString& owner, qint64 now = QDateTime::currentMSecsSinceEpoch()) const;
    QString cancel(const QString& owner) const;
    QString recovered(const QString& owner, qint64 stableMs) const;

private:
    QString m_path;
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "integration_startup_guard.h"
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace Ambient;

class IntegrationStartupTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void permitIsBoundToOwnerAndConsumedOnce()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("state.json");
        IntegrationStartupGuard guard(path);
        QVERIFY(!guard.consume(":1.1", 1000).isEmpty());
        QVERIFY(guard.prepare(":1.1", false, 1000).isEmpty());
        QVERIFY(!guard.consume(":1.2", 1001).isEmpty());
        QVERIFY(IntegrationStartupGuard(path).consume(":1.1", 1001).isEmpty());
        QVERIFY(!guard.consume(":1.1", 1002).isEmpty());
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadOther | QFileDevice::WriteOther),
            QFileDevice::Permissions());
    }
    void expiredCancelledAndFuturePermitsAreRejected()
    {
        QTemporaryDir dir;
        IntegrationStartupGuard guard(dir.filePath("state.json"));
        QVERIFY(guard.prepare(":1.1", false, 1000).isEmpty());
        QVERIFY(!guard.consume(":1.1", 999).isEmpty());
        QVERIFY(!guard.consume(":1.1", 1000 + IntegrationStartupGuard::PermitLifetimeMs).isEmpty());
        QVERIFY(guard.prepare(":1.1", false, 20000).isEmpty());
        QVERIFY(guard.cancel(":1.1").isEmpty());
        QVERIFY(!guard.consume(":1.1", 20001).isEmpty());
    }
    void budgetSurvivesNewOwnersAndGuardInstances()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("state.json");
        for (int i = 1; i <= IntegrationStartupGuard::MaximumAttempts; ++i)
            QVERIFY(IntegrationStartupGuard(path).prepare(QString(":1.%1").arg(i)).isEmpty());
        IntegrationStartupGuard guard(path);
        QVERIFY(!guard.prepare(":1.4").isEmpty());
        QVERIFY(guard.prepare(":1.4", true).isEmpty());
        QVERIFY(guard.consume(":1.4").isEmpty());
    }
    void recoveryRequiresStableConsumedPermit()
    {
        QTemporaryDir dir;
        IntegrationStartupGuard guard(dir.filePath("state.json"));
        const auto stable = IntegrationStartupGuard::RecoveryIntervalMs;
        QVERIFY(guard.prepare(":1.1").isEmpty());
        QVERIFY(!guard.recovered(":1.1", stable).isEmpty());
        QVERIFY(guard.consume(":1.1").isEmpty());
        QVERIFY(!guard.recovered(":1.1", stable - 1).isEmpty());
        QVERIFY(!guard.recovered(":1.2", stable).isEmpty());
        QVERIFY(guard.recovered(":1.1", stable).isEmpty());
        // A restarted daemon may observe a plugin whose budget was already reset.
        QVERIFY(guard.recovered(":1.1", stable).isEmpty());
        for (int i = 0; i < IntegrationStartupGuard::MaximumAttempts; ++i)
            QVERIFY(guard.prepare(":1.1").isEmpty());
        QVERIFY(!guard.prepare(":1.1").isEmpty());
    }
    void staleOwnerCannotCancelNewPermit()
    {
        QTemporaryDir dir;
        IntegrationStartupGuard guard(dir.filePath("state.json"));
        QVERIFY(guard.prepare(":1.1").isEmpty());
        QVERIFY(guard.prepare(":1.2").isEmpty());
        QVERIFY(!guard.cancel(":1.1").isEmpty());
        QVERIFY(!guard.recovered(":1.1", IntegrationStartupGuard::RecoveryIntervalMs).isEmpty());
        QVERIFY(guard.consume(":1.2").isEmpty());
    }
    void corruptStateRequiresExplicitRetry()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("state.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("{broken") > 0);
        file.close();
        IntegrationStartupGuard guard(path);
        QVERIFY(!guard.prepare(":1.1").isEmpty());
        QVERIFY(!guard.consume(":1.1").isEmpty());
        QVERIFY(guard.prepare(":1.1", true).isEmpty());
        QVERIFY(guard.consume(":1.1").isEmpty());
    }
    void inaccessibleStateNeverAuthorizesLoading()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("state.json");
        QVERIFY(QDir().mkdir(path));
        IntegrationStartupGuard guard(path);
        QVERIFY(!guard.prepare(":1.1").isEmpty());
        QVERIFY(!guard.prepare(":1.1", true).isEmpty());
        QVERIFY(!guard.consume(":1.1").isEmpty());
        QVERIFY(QDir(path).removeRecursively());
        QLockFile lock(path + ".lock");
        QVERIFY(lock.tryLock());
        QVERIFY(!guard.prepare(":1.1").isEmpty());
        QVERIFY(!guard.prepare(":1.1", true).isEmpty());
    }
};
QTEST_GUILESS_MAIN(IntegrationStartupTest)
#include "test_integration_startup.moc"

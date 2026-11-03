// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QFileInfo>
#include <QPluginLoader>
#include <QProcess>
#include <QTextStream>
#include <sys/resource.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream error(stderr), output(stdout);
    if (geteuid() == 0) {
        error << "Run plugin checks as an unprivileged user, not root.\n";
        return 1;
    }
    const rlimit noCore { 0, 0 };
    if (setrlimit(RLIMIT_CORE, &noCore) != 0) {
        error << "Cannot disable core dumps for plugin checks.\n";
        return 1;
    }
    const auto args = app.arguments().mid(1);
    if (args.size() == 2 && args.first() == "--probe") {
        QPluginLoader loader(args.last());
        loader.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
        // Loading checks the actual dependencies. Never instantiate the factory
        // or backend: those may require a compositor or access real hardware.
        if (!loader.load()) {
            error << loader.errorString() << '\n';
            return 1;
        }
        return 0;
    }
    if (args.isEmpty() || args.contains("--probe")) {
        error << "Usage: klighting-plugin-check plugin.so [plugin.so ...]\n";
        return 2;
    }
    for (const auto& path : args) {
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(app.applicationFilePath(), { "--probe", QFileInfo(path).absoluteFilePath() });
        if (!child.waitForStarted(1000)) {
            error << path << ": cannot start check: " << child.errorString() << '\n';
            return 1;
        }
        if (!child.waitForFinished(5000)) {
            child.kill();
            child.waitForFinished(1000);
            error << path << ": plugin check timed out\n";
            return 1;
        }
        if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0) {
            error << path << ": plugin check failed"
                  << (child.exitStatus() == QProcess::CrashExit ? " (crashed)" : "") << '\n'
                  << child.readAll();
            return 1;
        }
        output << path << ": OK\n";
    }
    return 0;
}

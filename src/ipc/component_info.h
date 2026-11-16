// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QVariantMap>

namespace Ambient
{
enum class Component { Service, Desktop, Capture };

QVariantMap componentInfo(Component component);
QVariantMap identityInfo(const QVariantMap& snapshot);
bool compatibleComponent(Component component, const QVariantMap& snapshot);

// Installed metadata is read without loading native code. Empty means unknown.
struct InstallationPaths {
    QString service, desktop, capture;
};
InstallationPaths installationPaths();
QVariantMap installedComponentInfo(const QString& path, bool plugin);
}

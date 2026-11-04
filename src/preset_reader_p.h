// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "presets.h"
#include <functional>

namespace Ambient::Internal
{
// Internal I/O seam for deterministic publication/read interleaving tests.
using PresetFileReader = std::function<std::optional<QByteArray>(const QString&, QString*)>;
std::optional<PresetCollection> loadPresets(
    const PresetCollection& fallback, QString* error, const PresetFileReader& read);
}

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "microphone_source.h"
#include <memory>

namespace Ambient
{
std::unique_ptr<MicrophoneSource> microphoneSource();
}

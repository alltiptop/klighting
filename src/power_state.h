// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Ambient
{
struct PowerState {
    bool known = false, onBattery = false, lowBattery = false;
    bool operator==(const PowerState&) const = default;
};
inline bool batteryPolicyActive(const PowerState& state, bool onBattery, bool onLowBattery)
{
    return state.known && state.onBattery && (onBattery || (onLowBattery && state.lowBattery));
}
}

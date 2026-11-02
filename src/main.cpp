// SPDX-License-Identifier: GPL-3.0-or-later
#include "ambient_effect.h"

KWIN_EFFECT_FACTORY_SUPPORTED(
    Ambient::AmbientEffect, "metadata.json", return Ambient::AmbientEffect::supported();)

#include "main.moc"

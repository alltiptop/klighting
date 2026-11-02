---
name: klighting-effect
description: Guides built-in klighting effect development, including settings and persistence. Use for new effect types or behavior changes, not hardware drivers or editing user presets.
---

# Built-in effects

This skill is repository-local; resolve links relative to this directory.
Identify the requested effect behavior, inputs and fallback before changing its data model.

Read the [architecture map](../../../docs/architecture.md) first. Effects currently
live in the host; device plugins describe hardware and consume composed frames.

1. Define options and defaults in [presets.h](../../../src/presets.h), then update the
   JSON serializer/parser and reference validation. Shared and preset-local effects
   must both round-trip through import/export.
2. Add computation to the appropriate runtime component: [EffectPalette](../../../src/effect_palette.h)
   handles continuous colors, [NotificationEffects](../../../src/notification_effect.h)
   handles transient overlays, and [AmbientEffect](../../../src/ambient_effect.h) owns
   KWin events and scheduling. Keep expensive inputs inactive when no assignment needs them.
3. Expose options through [SettingsController](../../../src/settings_controller.h)
   and add the effect form/creation choices in `ui/`. Reuse the assignment list;
   preserve opacity, order, local/shared references, Base and the single active layer.
4. Cover defaults, malformed input, composition and battery/lock behavior in the
   relevant existing tests. Add shared UI strings to `translations/`, never a driver catalog.

Use [CONTRIBUTING](../../../CONTRIBUTING.md) for build and checks and the vendored
Qt skills for language/framework review. Avoid a new plugin framework for an ordinary effect.

Deliver the runtime/settings integration, compatible JSON handling and relevant
verification results. State any compositor or hardware behavior not exercised.

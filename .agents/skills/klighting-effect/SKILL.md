---
name: klighting-effect
description: Guides adding or changing klighting built-in lighting effects, their settings and persistence. Not for hardware drivers or editing user preset files.
---

# Built-in effects

This skill is repository-local; resolve links relative to its installed directory.
Identify the requested behavior, inputs and fallback. Use the
[architecture map](../../../docs/architecture.md) for process boundaries; inspect
only the integration points affected by the change.

- **Computation and scheduling:** [LightingRuntime](../../../src/lighting_runtime.h)
  owns lighting in `klightingd`. [EffectPalette](../../../src/effect_palette.h) handles
  Ambient, solid colors and brightness filters; [NotificationEffects](../../../src/notification_effect.h)
  handles transient overlays. Use [microphone evaluation](../../../src/microphone_effect.h)
  for state-driven mute indicators. Extend these built-in components for ordinary
  effects; no effect plugin framework is needed.
- **Inputs:** [LightingService](../../../src/lighting_service.cpp) connects runtime
  requirements to capture, desktop, notification, microphone and power sources.
  [CaptureSource](../../../src/capture_source.h), [DesktopSource](../../../src/desktop_source.h)
  and [MicrophoneSource](../../../src/microphone_source.h) are independent boundaries.
  KWin's [AmbientEffect](../../../src/ambient_effect.h) only samples the screen;
  hardware, presets and effect animations stay in the daemon.
  Keep expensive inputs inactive when unused. Screen sampling and LED output have
  separate cadences; capture failure must not stop unrelated effects or device control.
  Stop settled animation without dropping pending input or final output. Account
  for elapsed idle time before starting a new transition or notification. Preserve
  the latest valid deferred input; invalidate it when capture, lock, power or
  configuration changes make it stale.
- **Capture changes:** Use [CaptureSchedule](../../../src/capture_schedule.h) for timing
  and [FrameSampler](../../../src/frame_sampler.h) for GPU readback. Wake on valid screen
  paints, retain one deferred change, and stop the timer when no work remains; a capture's
  own repaint must not sustain sampling. Preserve nonblocking polling, bounded repaint
  retries and capture-error shutdown until explicit retry. Native effects share KWin's
  process; exception handling cannot isolate a crash there.
- **Provider changes:** Preserve owner-bound IPC and protocol/capability checks;
  build IDs are diagnostics, not compatibility gates. The [capture adapter](../../../src/kwin_capture_source.cpp)
  and [desktop adapter](../../../src/kwin_desktop_source.cpp) retain acknowledged
  configuration per provider instance/owner, coalesce pending changes and reject stale
  replies. Preserve recovery after errors, owner/instance changes and legacy providers
  without instance IDs. Configuration retry must not bypass GPU or startup failure
  guards. Use the private-bus [service tests](../../../tests/test_lighting_service.cpp)
  when changing delivery or recovery.
- **Data and composition:** For option/schema changes, update [presets.h](../../../src/presets.h),
  [serialization/validation](../../../src/presets.cpp) and the relevant
  [preset evaluation](../../../src/preset_runtime.cpp). Preserve shared and profile-local
  references through import/export. Preserve the
  [composition order](../../../docs/architecture.md#composition-and-scheduling),
  including above-layer assignments. Missing assignments may inherit, but an
  explicitly empty list stays off. Let device adaptation limit final output
  without rewriting the saved effect for each device.
- **Device boundary:** Effects and their editors depend on capabilities and layout
  data from the public driver contract, never a concrete driver's private code, model
  IDs, key maps, calibration, UI refinements or defaults. Those artifacts and their
  tests/install rules/docs stay inside that driver's directory. Shared rendering and
  adaptation stay generic so adding a model requires no effect changes. Use synthetic
  descriptions for effect tests; a new capability needs a separately scoped generic
  contract extension. See the [driver ownership rules](../klighting-driver/SKILL.md).
- **Resolution changes:** [DeviceManager](../../../src/device_manager.cpp) reuses the
  selected profile/layer and its required-effect set. Always retain observed input;
  unchanged selection must not restart effects. Invalidate on document edits (even
  with the same IDs), power policy or backend/description changes; readiness alone
  can resend output without resolving it again. Cover affected invalidation paths in
  [profile resolution tests](../../../tests/test_profile_resolution.cpp).
- **Editing:** [settings_effects.cpp](../../../src/settings_effects.cpp) exposes effect
  options and assignments to `ui/`. Reuse the ordered assignment list and shared/local
  editing paths. Keep changes pending until Apply and maintain selection bindings when
  switching referenced effects. Shared strings belong in `translations/`, not driver catalogs.

Use [CONTRIBUTING](../../../CONTRIBUTING.md) for build/check commands. Choose relevant
existing tests for changed defaults, malformed input, JSON round-trips, composition,
source loss/recovery or battery/lock behavior. Extend isolated fixtures rather than
depending on the working desktop or connected devices.

Report runtime/settings changes and verification results, including any compositor
or hardware behavior not exercised.

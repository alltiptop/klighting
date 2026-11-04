# Contributing to klighting

Start with the [architecture map](docs/architecture.md). Public driver contracts are
in [device_driver.h](src/device_driver.h) and the [driver guide](docs/device-drivers.md).
Keep all model-specific code, tests, fixtures, documentation, translations, dependencies
and installation rules in its driver directory. Shared build/install/CI mechanisms
discover module declarations; adding a model must not require editing a central list.

## Build and checks

Requires Linux, CMake 3.25+, Ninja, C++20, Qt 6.10+, ECM 6.26+ and KWin 6.7 development
headers. Install Qt Core/Gui/DBus/Widgets/Quick/QuickControls2/Test/LinguistTools;
KF6 CoreAddons/Config/KCMUtils/GuiAddons/I18n/Service/Kirigami; the KDE Qt Quick
Controls desktop style; libusb-1.0; and KWin's development dependencies (Wayland,
libepoxy, libdrm, Vulkan headers/loader). Dependencies are installed separately;
configuration never downloads them. Native KWin plugins must be rebuilt after KWin updates.

Use the build commands in [README](README.md#build). A core-only build
builds `klightingd` and exercises drivers, presets, desktop input adapters, power
and notification logic without KWin headers:

```sh
cmake -S . -B build-core -G Ninja -DBUILD_KWIN_EFFECT=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

The full suite also checks native KCMs, QML interactions, translations, input layers
and geometry. Tests use private buses with service activation disabled, temporary
configuration, and fake USB transports. They must never depend on a contributor's
saved presets, desktop applications or attached hardware.

Formatting and type-aware QML checks use existing `clang-format`, Qt 6 `qmlformat`,
`qmllint`, Python 3 and Bash. No tools are downloaded by the checker:

```sh
python3 tools/check.py
python3 tools/check.py --format
# Equivalent check target, when Python was found during configuration:
cmake --build build --target lint
```

C++ follows [.clang-format](.clang-format); QML follows [.qmlformat.ini](.qmlformat.ini).
The formatting baseline uses clang-format 23 and Qt tools 6.11. If tools are installed
outside normal locations, set `CLANG_FORMAT`, `QMLFORMAT` and `QMLLINT` to their executables.
The checker excludes vendored skills and build outputs. QML errors and type/property warnings fail the check. Unqualified-access diagnostics
are informational because KCMs and their delegates use dynamic creation contexts;
they remain visible for review. The three host-injected `kcm` references have a
scoped suppression and are covered by KCM interaction tests.

Run tests appropriate to the changed contract. Driver changes need independent
packet/descriptor fixtures and failure/recovery coverage. Tests should establish
behavior, not reproduce implementation formulas. A hardware test is separate:
obtain the device owner's explicit approval, identify the exact device, and report
what was actually observed. Compilation and fake-transport tests do not establish
physical mapping or firmware behavior.

## Continuous integration

The build workflow uses a pinned Arch image and the 2026-10-01 package archive,
with clang-format 23.1.1, Qt 6.11.2 and KWin 6.7.5. Checks, the full KWin build and CTest run
as an unprivileged user. Package versions and failure logs are retained as artifacts.
Update the archive date and tool baseline together after validating the new stack.
Driver dependencies come from each module's `dependencies.arch` (one package name per
line); keep package discovery in its own CMake file. No central CI edits are needed.

Dependency vulnerability scanning is limited to dependencies managed by this
project. Currently all external application libraries come from the distribution;
there are no vendored libraries or downloaded dependency manifests to scan.
System packages and the CI base image are outside this check's scope.

Secret scanning uses checksum-pinned Gitleaks 8.30.1 and scans all fetched history,
including intermediate PR commits. For PRs, both the scanner wrapper and policy come
from the base revision; proposed ignore files and inline allow comments are ignored.
Bootstrap changes to this workflow through a reviewed push to the default branch.
Only rule/path/line/commit metadata is published. Add narrowly scoped false-positive
exceptions to `.github/gitleaks.toml`, combining the rule, exact public fixture and
path with `condition = "AND"`; exposed credentials must be revoked or rotated.
The wrapper's synthetic directory tests run without Git or network access:

```sh
python3 -m unittest discover -s .github/tests -p 'test_*.py'
```

## Runtime measurements

`./build/runtime_benchmark` emits JSON for six isolated lighting scenarios (three runs
each, about 54 seconds). It uses temporary settings and a fake driver, with no D-Bus,
GPU or hardware access. Compare the same build configuration and scenario parameters
before and after a change; do not run builds or other benchmarks concurrently. See
[measurement boundaries](docs/performance.md) before interpreting the counters.

`./build/capture_schedule_benchmark` exercises the shared capture timing policy with
synthetic paints and readback completion, also without KWin, GPU or hardware access.
`./build/profile_benchmark` measures profile/layer changes and unchanged selections
through synchronous runtime inputs and fake devices.
Driver-specific benchmarks and their instructions live inside their modules.

## Translations

Host strings use Qt translation contexts and catalogs in `translations/`. Driver
strings and resources stay under `src/drivers/<model>/translations/`. Do not copy
driver strings into the host catalog. English source text is the fallback.

```sh
cmake --build build --target update_translations
cmake --build build
ctest --test-dir build --output-on-failure -R translations
```

Review catalog changes before accepting them; rebuilding compiles catalogs but does
not translate new strings. Currently supplied languages are Russian, French,
Italian, Spanish, Ukrainian, Polish, German, Simplified Chinese, Arabic and Hebrew.

## Development skills

The concise project skills describe integration points:

- [klighting-driver](.agents/skills/klighting-driver/SKILL.md)
- [klighting-effect](.agents/skills/klighting-effect/SKILL.md)

The vendored [Qt C++ review](.agents/skills/qt-cpp-review/SKILL.md),
[QML review](.agents/skills/qt-qml-review/SKILL.md),
[QML guidance](.agents/skills/qt-qml/SKILL.md) and
[CMake guidance](.agents/skills/qt-cmake-project/SKILL.md) are optional review aids.
Their regex-based suggestions require judgment; they are not CI pass/fail rules.
Use explicit file lists rather than Git-dependent modes when repository rules
restrict Git. Native KDE KCM packaging takes precedence over generic standalone-QML
examples: these modules are resources loaded by `KQuickConfigModule`.
Original authorship, pinned revision and licenses are recorded in [THIRD_PARTY](THIRD_PARTY.md).

## Publication hygiene

Publish source, tests and documentation, not local `build/`, `dist/`, `stage/`,
IDE state or user configuration. Do not add real application bindings, device
serials, home paths, logs, screenshots or credentials as fixtures. Prefer synthetic
fixtures; model IDs and public protocol descriptors belong to the driver. Debug
binaries can contain build paths and must be considered separately from source.

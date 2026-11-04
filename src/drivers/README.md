# Driver modules

Each child directory with a `CMakeLists.txt` is discovered automatically. Open a
module's README for its supported devices, dependencies, access requirements,
layout, tests and diagnostics. `generic/` contains shared standard-protocol utilities.

A model or verified compatible family owns its complete implementation in one
directory: protocol, discovery, UI description/assets, metadata/defaults, translations,
documentation, tests/fixtures/benchmarks, dependencies and install/migration rules.
The host consumes the public API and declared capabilities; it has no model registry.
Adding a module supported by that API requires no changes elsewhere in the repository.

See the [driver guide](../../docs/device-drivers.md) for the contract, in-tree build
registration and independent external builds. Disable optional modules with
`-DKLIGHTING_DISABLED_DRIVERS="directory-name;another-directory"`.

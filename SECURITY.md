# Security and privacy

klighting runs inside the user's KWin process. A bad native driver or effect can
crash the compositor; device modules are trusted code, not a sandbox or a scripting
format. Install plugins only from sources you trust. `AMBIENT_LED_DRIVER_PATH`
replaces discovery paths for development and must not point at untrusted directories.

Preset imports contain data only. Parsers bound file sizes, collection lengths,
identifiers and effect references. Managed filenames cannot select paths outside
the configuration directory. Saves use atomic replacement; malformed collections
are rejected. These checks do not protect files from another process already able
to write the same user's configuration or plugin directories.

Ambient samples screen colors in memory. It does not save or transmit screenshots.
Application bindings use window classes. Notification filtering uses structured
application/category/event/urgency metadata, not notification text; incoming events
are checked against the current notification service's unique D-Bus owner. No
telemetry or network client is part of the runtime. Debug output can contain private
application names, preset names and device identities: review it before sharing.

Hardware access uses the logged-in user's device permissions. Do not run KWin as
root or grant world-writable access to all USB devices. Drivers must honor hardware
Off, bound I/O, avoid blocking KWin, and release interfaces on shutdown. A crash or
concurrent access by another lighting application can prevent restoration; this
cannot be guaranteed solely by the host API.

For a suspected vulnerability, use a private reporting channel provided by the
project's hosting platform. If none is configured, request a private contact before
posting sensitive reproduction data. Include the klighting/Qt/KWin versions and
minimal synthetic inputs; exclude credentials, user configuration and screen captures.

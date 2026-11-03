// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QThread>
#include <cstdlib>

#if defined(PROBE_unresolved)
extern "C" int klighting_missing_dependency();
extern "C" Q_DECL_EXPORT int unresolvedCall() { return klighting_missing_dependency(); }
#else
static const int onLoad = [] {
#if defined(PROBE_crash)
    std::abort();
#else
    QThread::sleep(60);
#endif
    return 0;
}();
#endif

class ProbeFixture : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.klighting.TestProbe/1.0")
};
#include "plugin_probe.moc"

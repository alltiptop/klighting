// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QRect>
#include <epoxy/gl.h>
#include <memory>
#include <optional>

namespace KWin
{
class GLTexture;
class GLFramebuffer;
class RenderTarget;
class RenderViewport;
}
namespace Ambient
{
class FrameSampler
{
public:
    FrameSampler();
    ~FrameSampler(); // Must be destroyed with KWin's OpenGL context current.
    bool capture(const KWin::RenderTarget& target, const KWin::RenderViewport& viewport, const QRect& area);
    std::optional<QByteArray> poll();
    bool pending() const { return m_fence != nullptr; }
    bool failed() const { return m_failed; }

private:
    bool initialize();
    std::unique_ptr<KWin::GLTexture> m_sourceTexture, m_srgbTexture;
    std::unique_ptr<KWin::GLFramebuffer> m_sourceFbo, m_srgbFbo;
    GLuint m_pbo = 0;
    GLsync m_fence = nullptr;
    QElapsedTimer m_pendingSince;
    bool m_failed = false;
};
}

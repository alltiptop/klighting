// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_sampler.h"
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/eglcontext.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

namespace Ambient
{
namespace
{
    constexpr int Width = 32, Height = 18, Bytes = Width * Height * 4;

    // Keep the compositor's GL state intact, including pixel-pack offsets. An
    // inherited pack row length would otherwise write past our small PBO.
    class StateGuard
    {
    public:
        StateGuard()
        {
            blend = glIsEnabled(GL_BLEND);
            scissor = glIsEnabled(GL_SCISSOR_TEST);
            desktop = !KWin::EglContext::currentContext()->isOpenGLES();
            if (desktop) {
                srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
                glDisable(GL_FRAMEBUFFER_SRGB);
            }
            glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pbo);
            glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
            glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
            glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
            glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        }
        ~StateGuard()
        {
            (blend ? glEnable : glDisable)(GL_BLEND);
            (scissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
            if (desktop)
                (srgb ? glEnable : glDisable)(GL_FRAMEBUFFER_SRGB);
            glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
            glPixelStorei(GL_PACK_ALIGNMENT, alignment);
            glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
            glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
            glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glActiveTexture(activeTexture);
        }

    private:
        GLboolean blend, scissor, srgb = false, colorMask[4];
        bool desktop;
        GLint activeTexture, texture, pbo, alignment, rowLength, skipRows, skipPixels;
    };
    class FramebufferGuard
    {
    public:
        explicit FramebufferGuard(KWin::GLFramebuffer* fbo) { KWin::GLFramebuffer::pushFramebuffer(fbo); }
        ~FramebufferGuard() { KWin::GLFramebuffer::popFramebuffer(); }
    };
}
FrameSampler::FrameSampler() = default;
FrameSampler::~FrameSampler()
{
    if (m_fence)
        glDeleteSync(m_fence);
    if (m_pbo)
        glDeleteBuffers(1, &m_pbo);
}
bool FrameSampler::initialize()
{
    if (m_pbo)
        return true;
    m_sourceTexture = KWin::GLTexture::allocate(GL_RGBA16F, { Width, Height });
    m_srgbTexture = KWin::GLTexture::allocate(GL_RGBA8, { Width, Height });
    if (!m_sourceTexture || !m_srgbTexture)
        return false;
    m_sourceTexture->setFilter(GL_LINEAR);
    m_sourceTexture->setWrapMode(GL_CLAMP_TO_EDGE);
    m_sourceFbo = std::make_unique<KWin::GLFramebuffer>(m_sourceTexture.get());
    m_srgbFbo = std::make_unique<KWin::GLFramebuffer>(m_srgbTexture.get());
    if (!m_sourceFbo->valid() || !m_srgbFbo->valid())
        return false;
    glGenBuffers(1, &m_pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, Bytes, nullptr, GL_STREAM_READ);
    GLint size = 0;
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_SIZE, &size);
    return size == Bytes;
}
bool FrameSampler::capture(
    const KWin::RenderTarget& target, const KWin::RenderViewport& viewport, const QRect& area)
{
    if (pending() || m_failed || !target.framebuffer() || area.isEmpty())
        return false;
    StateGuard state;
    if (!initialize()) {
        m_failed = true;
        return false;
    }
    const KWin::Rect destination(0, 0, Width, Height);
    {
        FramebufferGuard source(target.framebuffer());
        if (!m_sourceFbo->blitFromRenderTarget(target, viewport, KWin::Rect(area), destination)) {
            m_failed = true;
            return false;
        }
    }
    {
        FramebufferGuard converted(m_srgbFbo.get());
        auto* shader = KWin::ShaderManager::instance()->shader(
            KWin::ShaderTrait::MapTexture | KWin::ShaderTrait::TransformColorspace);
        if (!shader) {
            m_failed = true;
            return false;
        }
        KWin::ShaderBinder binder(shader);
        QMatrix4x4 projection;
        projection.ortho(QRectF(0, 0, Width, Height));
        binder.shader()->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, projection);
        binder.shader()->setUniform(KWin::GLShader::IntUniform::Sampler, 0);
        binder.shader()->setColorspaceUniforms(
            target.colorDescription(), KWin::ColorDescription::sRGB, KWin::RenderingIntent::Perceptual);
        m_sourceTexture->bind();
        m_sourceTexture->render(QSizeF(Width, Height));
        m_sourceTexture->unbind();
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
        glReadPixels(0, 0, Width, Height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
    }
    m_failed = !m_fence;
    m_pendingSince.start();
    return !m_failed;
}
std::optional<QByteArray> FrameSampler::poll()
{
    if (!m_fence || m_failed)
        return std::nullopt;
    const auto result = glClientWaitSync(m_fence, 0, 0); // Never wait for the GPU on KWin's thread.
    if (result == GL_WAIT_FAILED || (result == GL_TIMEOUT_EXPIRED && m_pendingSince.elapsed() > 1000)) {
        m_failed = true;
        return std::nullopt;
    }
    if (result == GL_TIMEOUT_EXPIRED)
        return std::nullopt;
    GLint previous = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
    const auto* data
        = static_cast<const char*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, Bytes, GL_MAP_READ_BIT));
    std::optional<QByteArray> pixels;
    if (data) {
        pixels = QByteArray(data, Bytes);
        if (!glUnmapBuffer(GL_PIXEL_PACK_BUFFER)) {
            pixels.reset();
            m_failed = true;
        }
    } else {
        m_failed = true;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, previous);
    glDeleteSync(m_fence);
    m_fence = nullptr;
    return pixels;
}
}

#pragma once

#include <juce_opengl/juce_opengl.h>
#include "shared/shell/Theme.h"

// Animation timers schedule frames. VSync must not block the message thread:
// JUCE's macOS resize handler renders and swaps a frame synchronously.
class AnimationOpenGLContext final : public juce::OpenGLContext,
                                     private juce::OpenGLRenderer
{
public:
    AnimationOpenGLContext()
    {
        setRenderer(this);
        setContinuousRepainting(false);
    }

    ~AnimationOpenGLContext() override
    {
        detach();
        setRenderer(nullptr);
    }

private:
    void newOpenGLContextCreated() override
    {
        setSwapInterval(0);
        // A newly exposed native surface must never present uninitialised front
        // or back buffers while the component/shader cache is being prepared.
        clearSurface();
        swapBuffers();
        clearSurface();
    }

    void renderOpenGL() override { clearSurface(); }
    void openGLContextClosing() override
    {
        clearSurface();
        swapBuffers();
    }
    void clearSurface()
    {
        juce::gl::glBindFramebuffer(juce::gl::GL_FRAMEBUFFER, getFrameBufferID());
        juce::gl::glDisable(juce::gl::GL_SCISSOR_TEST);
        juce::gl::glColorMask(juce::gl::GL_TRUE, juce::gl::GL_TRUE,
                            juce::gl::GL_TRUE, juce::gl::GL_TRUE);
        juce::OpenGLHelpers::clear(ana::ui::background);
    }
};

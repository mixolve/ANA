#pragma once
#include "FrameRendering.h"
#include <optional>

// A fixed, offscreen GL surface produces preview images without painting or
// locking the settings window's component tree on the GL thread.
class BurningFilmPreview final : public juce::Component, private juce::OpenGLRenderer
{
public:
    BurningFilmPreview();
    ~BurningFilmPreview() override;
    void start();
    juce::Image requestFrame(BurningFilmFrame frame);

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    juce::CriticalSection mutex;
    std::optional<BurningFilmFrame> pendingFrame;
    juce::Image image;
    juce::OpenGLFrameBuffer frameBuffer;
    std::vector<juce::PixelARGB> pixels;
    juce::OpenGLGraphicsContextCustomShader shader { burningFilmFragmentShader };
    juce::OpenGLContext context;
};

#include "BurningFilmPreview.h"
#include <cstring>

BurningFilmPreview::BurningFilmPreview()
{
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    context.setRenderer(this);
    context.setComponentPaintingEnabled(false);
    context.setContinuousRepainting(false);
}

BurningFilmPreview::~BurningFilmPreview()
{
    context.detach();
    context.setRenderer(nullptr);
}

void BurningFilmPreview::start() { context.attachTo(*this); }

juce::Image BurningFilmPreview::requestFrame(BurningFilmFrame frame)
{
    juce::Image result;
    {
        const juce::ScopedLock lock(mutex);
        pendingFrame = std::move(frame);
        result = image;
    }
    context.triggerRepaint();
    return result;
}

void BurningFilmPreview::newOpenGLContextCreated() { context.setSwapInterval(0); }

void BurningFilmPreview::renderOpenGL()
{
    std::optional<BurningFilmFrame> request;
    {
        const juce::ScopedLock lock(mutex);
        request = std::move(pendingFrame);
        pendingFrame.reset();
    }
    if (!request) return;

    const auto width = juce::jmax(1, juce::roundToInt(static_cast<float>(request->bounds.getWidth()) * request->renderingScale));
    const auto height = juce::jmax(1, juce::roundToInt(static_cast<float>(request->bounds.getHeight()) * request->renderingScale));
    if (frameBuffer.getWidth() != width || frameBuffer.getHeight() != height)
        if (!frameBuffer.initialise(context, width, height)) return;

    {
        auto graphicsContext = juce::createOpenGLGraphicsContext(context, frameBuffer);
        if (graphicsContext == nullptr) return;
        juce::Graphics graphics(*graphicsContext);
        graphics.addTransform(juce::AffineTransform::scale(request->renderingScale));
        paintBurningFilmFrame(graphics, shader, *request);
    }

    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    if (!frameBuffer.readPixels(pixels.data(), {width, height}, juce::OpenGLFrameBuffer::RowOrder::fromTopDown))
        return;

    juce::Image completed(juce::Image::ARGB, width, height, false);
    {
        const juce::Image::BitmapData bitmap(completed, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
            std::memcpy(bitmap.getLinePointer(y), pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width),
                        static_cast<size_t>(width) * sizeof(juce::PixelARGB));
    }
    {
        const juce::ScopedLock lock(mutex);
        image = std::move(completed);
    }
}

void BurningFilmPreview::openGLContextClosing() { frameBuffer.release(); }

#include "Artwork.h"
#include "shared/shell/Theme.h"

#include "FrameRendering.h"
#include "Preview.h"
#include "ColourFilter.h"
#include <juce_opengl/juce_opengl.h>

namespace { constexpr int initialEditorWidth = 340; }

struct AnimationArtwork::Animation
{
    std::shared_ptr<AnimationSettings> settings = std::make_shared<AnimationSettings>();
    bool gpuRendering = false;
    juce::OpenGLGraphicsContextCustomShader shader { burningFilmFragmentShader };
    std::unique_ptr<BurningFilmPreview> preview;
    double lastTick = juce::Time::getMillisecondCounterHiRes();
    std::atomic<double> time { juce::Random::getSystemRandom().nextDouble() * 80.0 };


    void update()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto elapsed = juce::jlimit(0.0, 0.05, (now - lastTick) / 1000.0);
        time.store(time.load() + elapsed);
        lastTick = now;
    }
};

AnimationArtwork::AnimationArtwork() : animation(std::make_unique<Animation>())
{
    setInterceptsMouseClicks(true, false);
    startTimerHz(30);
}

AnimationArtwork::~AnimationArtwork() { stopTimer(); }
void AnimationArtwork::resized() { repaint(); }

void AnimationArtwork::timerCallback()
{
    if (! isShowing())
    {
        animation->lastTick = juce::Time::getMillisecondCounterHiRes();
        return;
    }
    if (animationSource == nullptr || !animationSource->isShowing())
        animation->update();
    repaint();
}

void AnimationArtwork::paint(juce::Graphics& g)
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const auto* source = animationSource != nullptr ? animationSource.getComponent() : this;
    const auto& current = source != this && !source->isShowing() ? *animation : *source->animation;
    const auto& animationSettings = *current.settings;
    g.fillAll(ana::ui::background);
    const auto& modeSettings = animationSettings.burningFilm;
    const auto* geometrySource = source;
    // Both the strip and its preview use the main editor's width, not preview width.
    const auto editorWidth = geometrySource->getParentComponent() != nullptr
        ? geometrySource->getParentComponent()->getWidth() : initialEditorWidth;
    const auto widthFactor = static_cast<float>(juce::jmax(1, editorWidth))
        / static_cast<float>(initialEditorWidth);
    const auto effectiveScale = 2.5f * widthFactor * modeSettings.scaleFactor.load();
    // The strip is a crop of a complete canvas, rather than a vertically squeezed scene.
    const auto canvasSide = juce::jmax(320, geometrySource->getWidth());
    const auto canvasHeight = juce::jmax(canvasSide, juce::roundToInt(
        static_cast<float>(geometrySource->getHeight()) * static_cast<float>(canvasSide)
        / static_cast<float>(juce::jmax(1, geometrySource->getWidth()))));
    const auto coordinateScale = static_cast<float>(canvasSide) / static_cast<float>(getWidth());
    const auto cropHeight = juce::jmin(static_cast<float>(canvasHeight),
        static_cast<float>(geometrySource->getHeight()) * static_cast<float>(canvasSide)
            / static_cast<float>(juce::jmax(1, geometrySource->getWidth())));
    const auto cropOffset = animationSource != nullptr ? 0.0f
        : (static_cast<float>(canvasHeight) - cropHeight) * modeSettings.cropY.load();
    const auto* clock = &current;
    const auto timeSnapshot=clock->time.load();
    if (animation->preview != nullptr)
    {
        const auto renderingScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        auto image = animation->preview->requestFrame(
            { getLocalBounds(), renderingScale, {}, canvasSide, canvasHeight, coordinateScale, 0.f,
              AnimationColourFilter(modeSettings.colour.load()), static_cast<float>(timeSnapshot),
              juce::jmax(.001f, modeSettings.speed.load() * 2.f), modeSettings.glow.load(), effectiveScale });
        if (image.isValid())
            g.drawImage(image, getLocalBounds().toFloat());
    }
    else if (animation->gpuRendering && juce::OpenGLContext::getCurrentContext() != nullptr)
    {
        auto* context = juce::OpenGLContext::getCurrentContext();
        const auto renderingScale = static_cast<float>(context->getRenderingScale());
        const auto origin = context->getTargetComponent()->getLocalPoint(this, juce::Point<int>());
        const AnimationColourFilter filter(modeSettings.colour.load());
        const auto time = static_cast<float>(timeSnapshot);
        const auto burnSpeed = juce::jmax(.001f, modeSettings.speed.load() * 2.0f);
        const auto glow = modeSettings.glow.load(), patternScale = effectiveScale;
        paintBurningFilmFrame(g, animation->shader,
            { getLocalBounds(), renderingScale, origin, canvasSide, canvasHeight, coordinateScale, cropOffset,
              filter, time, burnSpeed, glow, patternScale });
    }
    if (animationSource != nullptr)
    {
        g.setColour(ana::ui::light);
        g.drawRect(getLocalBounds(), 1);
        g.setColour(ana::ui::white);
        g.drawRect(getCropBounds().reduced(.75f),1.5f);
    }
}

void AnimationArtwork::setGpuRenderingEnabled(bool enabled) { animation->gpuRendering = enabled; repaint(); }
void AnimationArtwork::setAnimationSettings(std::shared_ptr<AnimationSettings> settings)
{
    animation->settings = std::move(settings);
    repaint();
}
void AnimationArtwork::mouseDown(const juce::MouseEvent& event)
{
    cropDragActive = animationSource != nullptr && event.mods.isLeftButtonDown()
        && getCropBounds().expanded(0.0f, 3.0f).contains(event.position);
    if (cropDragActive)
    {
        const auto& current = *animationSource->animation;
        const auto& modeSettings = current.settings->burningFilm;
        cropDragStartY = event.position.y;
        cropDragStartPosition = modeSettings.cropY.load();
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (event.mods.isPopupMenu() && onSettingsRequested)
        onSettingsRequested();
}
juce::Rectangle<float> AnimationArtwork::getCropBounds() const
{
    if (animationSource == nullptr || animationSource->getWidth() <= 0)
        return {};
    const auto& current=*animationSource->animation;
    const auto& animationSettings=*current.settings;
    const auto& modeSettings=animationSettings.burningFilm;
    const auto height=juce::jmin(static_cast<float>(getHeight()),
        static_cast<float>(animationSource->getHeight())*static_cast<float>(getWidth())
            /static_cast<float>(animationSource->getWidth()));
    return { 0.f,(static_cast<float>(getHeight())-height)*modeSettings.cropY.load(),static_cast<float>(getWidth()),height };
}
void AnimationArtwork::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(animationSource!=nullptr && getCropBounds().expanded(0.f,3.f).contains(event.position)
        ?juce::MouseCursor::DraggingHandCursor:juce::MouseCursor::NormalCursor);
}
void AnimationArtwork::mouseDrag(const juce::MouseEvent& event)
{
    if (cropDragActive && animationSource!=nullptr)
    {
        const auto travel=static_cast<float>(getHeight())-getCropBounds().getHeight();
        if (travel<=0.f) return;
        auto& current=*animationSource->animation;
        auto& modeSettings=current.settings->burningFilm;
        const auto position=juce::jlimit(0.f,1.f,cropDragStartPosition+(event.position.y-cropDragStartY)/travel);
        modeSettings.cropY.store(position);
        if (onCropChanged) onCropChanged(position);
        animationSource->repaint(); repaint();
        return;
    }
}
void AnimationArtwork::mouseUp(const juce::MouseEvent&)
{
    cropDragActive=false;
}
void AnimationArtwork::setAnimationSource(AnimationArtwork& source)
{
    animationSource = &source;
    animation->settings = source.animation->settings;
    if (animation->preview == nullptr)
    {
        animation->preview = std::make_unique<BurningFilmPreview>();
        // This context only renders offscreen frames; it never covers UI content.
        animation->preview->setBounds(-16384, -16384, 1, 1);
        addAndMakeVisible(*animation->preview);
        animation->preview->start();
    }
    repaint();
}

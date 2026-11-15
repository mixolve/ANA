#include "shared/shell/InlineResetPrompt.h"
#include "View.h"
#include "shared/shell/Theme.h"
#include "shared/shell/NumericValueBox.h"
#include <cstdlib>
#include <cmath>

AnimationBackdrop::AnimationBackdrop()
{
    setOpaque(true);
    setInterceptsMouseClicks(false, false);
}
void AnimationBackdrop::paint(juce::Graphics& g) { g.fillAll(ana::ui::background); }

AnimationOverlay::AnimationOverlay(juce::ValueTree& stateIn, std::shared_ptr<AnimationSettings> settingsIn)
    : state(stateIn), settings(std::move(settingsIn))
{
    loadAnimationSettings(*settings, state);
    setOpaque(false);
    artwork.setAnimationSettings(settings);
    artwork.setGpuRenderingEnabled(true);
    artwork.onSettingsRequested = [this] { if (onSettingsRequested) onSettingsRequested(); };
    artwork.addMouseListener(this, false);
    addAndMakeVisible(artwork);
}
AnimationOverlay::~AnimationOverlay()
{
    artwork.removeMouseListener(this);
    stopTimer();
    context.detach();
}
void AnimationOverlay::setActive(const bool active)
{
    if (active)
    {
        setVisible(true);
        startTimerHz(30);
        context.setComponentPaintingEnabled(true);
        context.attachTo(*this);
    }
    else
    {
        stopTimer();
        context.detach();
        setVisible(false);
        if (auto* parent = getParentComponent()) parent->repaint(getBounds());
        if (auto* peer = getPeer()) peer->performAnyPendingRepaintsNow();
    }
}
void AnimationOverlay::timerCallback()
{
    loadAnimationSettings(*settings, state);
}
void AnimationOverlay::resized()
{
    artwork.setBounds(getLocalBounds());
}
void AnimationOverlay::paint(juce::Graphics& g)
{
    g.fillAll(ana::ui::background);
}
void AnimationOverlay::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isLeftButtonDown() && event.originalComponent == &artwork)
        if (onClose) onClose();
}

struct AnimationSettingsPanel::Impl final : juce::Component, private juce::Timer
{
    Impl(AnimationSettingsPanel& ownerIn,juce::AudioProcessorValueTreeState& parameters,
         std::shared_ptr<AnimationSettings> settingsIn)
        : owner(ownerIn),state(parameters.state),settings(std::move(settingsIn))
    {
        for(const auto& def:animationParameterDefinitions())
        {
            if(def.mode==2 || def.property=="animation-mode") continue;
            auto control=std::make_unique<ParameterControl>(parameters,def.property,def.title,[def](double value)
            {
                if(def.boolean) return juce::String(value>=.5?"ON":"OFF");
                if(!def.choices.isEmpty()) return def.choices[juce::jlimit(0,def.choices.size()-1,juce::roundToInt(value))];
                return juce::String(value,def.interval>=1?0:2);
            });
            control->setComponentID(def.property);
            control->setFixedBinding(true);
            control->setSquareStyle(true);
            control->onTextEditingChanged=[this](bool editing) { if(owner.onTextEditingChanged) owner.onTextEditingChanged(editing); };
            control->onChoiceRequested=[this](ParameterControl& c) { if(owner.onChoiceRequested) owner.onChoiceRequested(c); };
            control->onResetRequested=[this](ParameterControl& c) { if(owner.onResetRequested) owner.onResetRequested(c); else c.resetToDefault(); };
            addAndMakeVisible(*control);
            controls.push_back(std::move(control));
        }
        refresh(); startTimerHz(30);
    }
    void refresh()
    {
        loadAnimationSettings(*settings,state);
        const auto mode=settings->mode.load();
        if(mode==displayedMode) return;
        displayedMode=mode;
        for(auto& control:controls) control->setVisible(true);
        resized();
        if(owner.onPreferredHeightChanged) owner.onPreferredHeightChanged();
    }
    void paint(juce::Graphics& g) override { g.fillAll(ana::ui::square::panelBackground); }
    int gridHeight() const
    {
        int count=0; for(const auto& control:controls) if(control->isVisible()) ++count;
        const int rows=(count+ana::ui::square::columns-1)/ana::ui::square::columns;
        return rows*ana::ui::square::size+juce::jmax(0,rows-1)*ana::ui::square::spacing;
    }
    int preferredHeight() const { return gridHeight(); }
    void resized() override
    {
        using namespace ana::ui::square;
        int index=0;
        for(auto& control:controls)
            if(control->isVisible()) { control->setBounds(index%columns*(size+spacing),index/columns*(size+spacing),size,size); ++index; }
            else control->setBounds({});
    }
    void timerCallback() override { if(isShowing()) refresh(); }
    AnimationSettingsPanel& owner;
    juce::ValueTree& state;
    std::shared_ptr<AnimationSettings> settings;
    std::vector<std::unique_ptr<ParameterControl>> controls;
    int displayedMode=-1;
};
AnimationSettingsPanel::AnimationSettingsPanel(juce::AudioProcessorValueTreeState& parameters,
    std::shared_ptr<AnimationSettings> settings)
    : impl(std::make_unique<Impl>(*this,parameters,std::move(settings)))
{
    getProperties().set("emptyEditorBackground",true);
    impl->getProperties().set("emptyEditorBackground",true);
    addAndMakeVisible(*impl);
}
AnimationSettingsPanel::~AnimationSettingsPanel()=default;
void AnimationSettingsPanel::resized() { impl->setBounds(getLocalBounds()); }
bool AnimationSettingsPanel::keyPressed(const juce::KeyPress& key) { if(key!=juce::KeyPress::escapeKey || !onClose) return false; onClose(); return true; }
void AnimationSettingsPanel::setActive(bool active) { if(active) grabKeyboardFocus(); }
void AnimationSettingsPanel::cancelEditors() { for(auto& control:impl->controls) control->cancelPendingEditor(); }
int AnimationSettingsPanel::preferredEmbeddedHeight(int) const { return impl->preferredHeight(); }
std::vector<ParameterControl*> AnimationSettingsPanel::parameterControls() const
{ std::vector<ParameterControl*> result; for(auto& control:impl->controls) result.push_back(control.get()); return result; }
void AnimationSettingsPanel::paintOverChildren(juce::Graphics& g) { NumericValueBox::paintRangeIndicators(g,*this); }

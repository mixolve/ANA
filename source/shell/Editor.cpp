#include "shared/shell/ControlDock.h"
#include "shared/shell/NumericValueBox.h"
#include "shared/shell/SettingsViewport.h"
#include "shared/shell/HostShortcutDocumentWindow.h"
#include "shared/shell/InlineResetPrompt.h"
#include "shared/shell/EdgeResizeHandle.h"
#include "Editor.h"
#include "Processor.h"
#include "shared/shell/Theme.h"
#include "shared/shell/SvgExport.h"
#include "shared/shell/EditorLayout.h"
#include "shared/shell/AuxiliaryWindowFocus.h"
#include "shared/shell/GraphColours.h"
#include "shared/shell/EdgeResizeContent.h"
#include "shared/shell/AuxiliaryWindowFooter.h"
#include "shared/shell/EditorWindowResize.h"
#include "shared/shell/WindowGeometry.h"
#include "shared/scop/Layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>

namespace
{
// Other pages keep the original compact minimum. SCOP reserves control space
// only for its currently displayed bands (see minimumHeightForCurrentPage).
constexpr int minimumEditorHeight = ana::scop::minimumEditorHeightForBands(
    ana::scop::minimumRealtimeBandHeight);
constexpr int maximumEditorSize = 32768;
constexpr int defaultEditorHeight = minimumEditorHeight;
constexpr int editorResizeHandleThickness = ana::ui::resizeHandleThickness;
constexpr int settingsWindowWidth = ana::ui::square::windowWidth;
constexpr int maximumSettingsWindowHeight = 1200;
constexpr int snapshotFileMagic = 0x414e4153; // "ANAS"
constexpr int snapshotFileVersion = 3;
constexpr int maximumSnapshotNameLength = 256;
constexpr const char* snapshotFileExtension = ".anasnapshot";
constexpr const char* snapshotFileWildcard = "*.anasnapshot";

int minimumEditorWidth() noexcept
{
    return ana::ui::minimumMainEditorWidth();
}

const auto& snapshotColourOptions = ana::ui::graphColourOptions;

const juce::Colour snapshotFieldBorderColour { 0xffbbbbbb };

juce::String formatSnapshotGain(const float gainDb)
{
    const auto clamped = juce::jlimit(SpecView::snapshotGainMinimumDb, SpecView::snapshotGainMaximumDb, gainDb);
    const auto absolute = juce::String(std::abs(clamped), 2).paddedLeft('0', 5);
    return juce::String(clamped >= 0.0f ? "+" : "-") + absolute;
}

class InvisibleResizableEdgeComponent final : public EdgeResizeHandle
{
public:
    InvisibleResizableEdgeComponent(juce::AudioProcessorEditor* editor,
        juce::ResizableEdgeComponent::Edge edge)
        : EdgeResizeHandle(*editor,
            edge == juce::ResizableEdgeComponent::leftEdge || edge == juce::ResizableEdgeComponent::rightEdge
                ? Axis::horizontal : Axis::vertical, 1, 100000,
            edge == juce::ResizableEdgeComponent::leftEdge || edge == juce::ResizableEdgeComponent::topEdge),
          target(*editor), side(edge) {}
    void mouseDown(const juce::MouseEvent& event) override
    { nativeResize.begin(target, side); EdgeResizeHandle::mouseDown(event); }
    void mouseDrag(const juce::MouseEvent& event) override
    { EdgeResizeHandle::mouseDrag(event); nativeResize.update(); }
    void mouseUp(const juce::MouseEvent&) override { nativeResize.update(); nativeResize.end(); }
private:
    juce::AudioProcessorEditor& target;
    juce::ResizableEdgeComponent::Edge side;
    EditorWindowResize nativeResize;
};

class InvisibleResizableCornerComponent final : public EdgeResizeHandle
{
public:
    explicit InvisibleResizableCornerComponent(juce::AudioProcessorEditor* editor)
        : EdgeResizeHandle(*editor, Axis::both, 1, 100000),
          target(*editor) {}
    void mouseDown(const juce::MouseEvent& event) override
    { nativeResize.beginCorner(target); EdgeResizeHandle::mouseDown(event); }
    void mouseDrag(const juce::MouseEvent& event) override
    { EdgeResizeHandle::mouseDrag(event); nativeResize.update(); }
    void mouseUp(const juce::MouseEvent&) override { nativeResize.update(); nativeResize.end(); }
private:
    juce::AudioProcessorEditor& target;
    EditorWindowResize nativeResize;
};

class EditorSizeConstrainer final : public juce::ComponentBoundsConstrainer
{
public:
    void applyBoundsToComponent(juce::Component& component, juce::Rectangle<int> bounds) override
    {
        // The host owns the editor's origin. JUCE's left/top edge resizers
        // otherwise shift the plugin inside its host instead of resizing it.
        bounds.setPosition(component.getPosition());
        juce::ComponentBoundsConstrainer::applyBoundsToComponent(component, bounds);
    }
};

class SettingsWindowContent final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override { g.fillAll(ana::ui::square::panelBackground); }
    void paintOverChildren(juce::Graphics& g) override
    { NumericValueBox::paintRangeIndicators(g,*this); g.setColour(ana::ui::background); g.drawRect(getLocalBounds(),2); }
    explicit SettingsWindowContent(SettingsPanel& panelIn)
        : dock(panelIn.getProcessor(),panelIn), panel(panelIn)
    {
        getProperties().set("emptyEditorBackground",true);
        panel.getProperties().set("externalSettingsFrame",true);
        addAndMakeVisible(panel);
        addAndMakeVisible(dock);
    }

    void showPopup(const juce::Rectangle<int> anchorBounds,
                   const juce::StringArray& choices,
                   const std::vector<bool>& enabledChoices,
                   const int selectedIndex,
                   std::function<void(int)> onSelect,
                   std::unique_ptr<ChoicePopup>* slot = nullptr)
    {
        dismissChoicePrompt();
        dismissResetPrompt();

        juce::Component::SafePointer<SettingsWindowContent> safeContent(this);
        auto popup = std::make_unique<ChoicePopup>(
            anchorBounds, choices, enabledChoices, selectedIndex,
            [safeContent, select = std::move(onSelect)] (const int index) mutable
            {
                if (safeContent != nullptr && select != nullptr)
                    select(index);
            },
            [safeContent, slot]
            {
                if (safeContent == nullptr)
                    return;
                if (slot == nullptr || slot == &safeContent->choicePrompt)
                    safeContent->dismissChoicePrompt();
                else
                    safeContent->dismissResetPrompt();
            });
        addAndMakeVisible(*popup);
        popup->setBounds(getLocalBounds());
        popup->toFront(true);
        if (slot == nullptr)
            choicePrompt = std::move(popup);
        else
            *slot = std::move(popup);
    }

    void showAboutPopup(AboutPopup& popup)
    {
        dismissChoicePrompt(); dismissResetPrompt();
        about = &popup;
        addAndMakeVisible(popup);
        popup.setBounds(getLocalBounds());
        popup.toFront(true);
        popup.grabKeyboardFocus();
    }

    void showChoicePrompt(ParameterControl& control)
    {
        const auto choices = control.getChoiceNames();
        if (choices.isEmpty())
            return;

        juce::Component::SafePointer<ParameterControl> safeControl(&control);
        showPopup(getLocalArea(&control, control.getValueBounds()), choices, {}, control.getSelectedChoiceIndex(),
                  [safeControl] (const int selectedIndex)
                  {
                      if (safeControl != nullptr)
                          safeControl->setSelectedChoiceIndex(selectedIndex);
                  });
    }

    void dismissChoicePrompt()
    {
        choicePrompt.reset();
    }

    void showResetPrompt(ParameterControl& control)
    {
        const auto anchorBounds = getLocalArea(&control, control.getTitleBounds());
        if (anchorBounds.isEmpty())
            return;

        juce::Component::SafePointer<ParameterControl> safeControl(&control);
        dismissChoicePrompt();
        resetPrompt = std::make_unique<InlineResetPrompt>(anchorBounds, false,
            [safeControl] { if (safeControl != nullptr) safeControl->resetToDefault(); },
            [safeThis = juce::Component::SafePointer<SettingsWindowContent>(this)]
            { if (safeThis != nullptr) safeThis->dismissResetPrompt(); });
        addAndMakeVisible(*resetPrompt);
        resetPrompt->setBounds(getLocalBounds());
        resetPrompt->toFront(false);
    }

    void dismissResetPrompt()
    {
        resetPrompt.reset();
    }

    void resized() override
    {
        auto area=getLocalBounds();
        panel.setVisible(!dock.dockOnly());
        if(dock.dockOnly()) { panel.setBounds({}); dock.setBounds(area); }
        else { dock.setBounds(area.removeFromBottom(96)); area.removeFromBottom(8); panel.setBounds(area); }
        if (about != nullptr) { about->setBounds(getLocalBounds()); about->toFront(true); }
        if (choicePrompt != nullptr)
        {
            choicePrompt->setBounds(getLocalBounds());
            choicePrompt->toFront(true);
        }
        if (resetPrompt != nullptr)
        {
            resetPrompt->setBounds(getLocalBounds());
            resetPrompt->toFront(true);
        }
    }

public:
    ControlDock dock;
private:
    SettingsPanel& panel;
    juce::Component::SafePointer<AboutPopup> about;
    std::unique_ptr<ChoicePopup> choicePrompt;
    std::unique_ptr<InlineResetPrompt> resetPrompt;
};

class SnapshotRow final : public juce::Component
{
public:
    SnapshotRow(SpecView& specViewIn, const size_t snapshotIndexIn,
                const int initialColourIndex)
        : specView(specViewIn), snapshotIndex(snapshotIndexIn)
    {
        getProperties().set("emptyEditorBackground", true);

        gainSlider.setRange(SpecView::snapshotGainMinimumDb,
                            SpecView::snapshotGainMaximumDb, 0.01);
        gainSlider.setValue(0.0, juce::dontSendNotification);
        gainSlider.textFromValueFunction = [] (double value)
        { return formatSnapshotGain(static_cast<float>(value)); };
        gainSlider.valueFromTextFunction = [] (const juce::String& value)
        { return value.trim().getDoubleValue(); };
        gainSlider.onValueChange = [this]
        {
            if (!updatingGainValue && isAvailable())
                setGainDb(static_cast<float>(gainSlider.getValue()));
        };
        gainControl.setSquareStyle([] { return juce::String("GAIN"); });
        gainControl.setSlotIndicatorVisible(false);
        gainControl.setNumericControlEnabled(true);
        gainControl.onResetRequested = [this]
        {
            if (isAvailable() && onGainResetPromptRequested)
                onGainResetPromptRequested(*this);
        };
        addAndMakeVisible(gainControl);
        gainControl.onEditorVisibilityChanged = [this] (bool editing)
        { if (onTextEditingChanged) onTextEditingChanged(editing); };

        for (auto* control : std::array<NumericValueBox*, 5> {
                 &cameraControl, &colourControl, &exportControl, &importControl, &visibilityControl })
        {
            control->setNumericControlEnabled(false);
            control->setSlotIndicatorVisible(false);
            addAndMakeVisible(*control);
        }
        const auto configureCommand = [] (NumericValueBox& control, juce::Slider& slider,
                                          const juce::String& title, std::function<void()> action)
        {
            slider.setRange(0.0, 1.0, 1.0);
            control.setSquareStyle([title] { return title; });
            control.setDiscreteChoiceCount(2);
            control.displayTextProvider = [title] { return title; };
            control.onChoiceClicked = [&slider, action] (int)
            {
                action();
                slider.setValue(0.0, juce::dontSendNotification);
            };
            control.onDiscreteEdited = [&slider, action]
            {
                if (slider.getValue() >= 0.5) action();
                slider.setValue(0.0, juce::dontSendNotification);
            };
        };
        configureCommand(cameraControl, cameraSlider, "SNAP\nSHOT", [this]
        {
            if (isAvailable() && specView.captureSnapshot(snapshotIndex))
            {
                setHidden(false);
                exportControl.setInteractionEnabled(true);
            }
        });
        configureCommand(exportControl, exportSlider, "EXP", [this]
        { if (isAvailable() && onExportRequested) onExportRequested(*this); });
        configureCommand(importControl, importSlider, "IMP\nONE", [this]
        { if (isAvailable() && onImportRequested) onImportRequested(*this); });

        colourSlider.setRange(0.0, double(snapshotColourOptions.size() - 1), 1.0);
        colourControl.setSquareStyle([] { return juce::String("COLOUR"); });
        colourControl.setDiscreteChoiceCount(static_cast<int>(snapshotColourOptions.size()));
        colourControl.displayTextProvider = [this]
        { return juce::String(snapshotColourOptions[static_cast<size_t>(colourIndex)].name); };
        // Colour is a normal discrete control: click to advance, Shift+click
        // to go back; drag uses the standard square-control discrete gesture.
        colourControl.onChoiceClicked = [this](int direction)
        {
            if (!isAvailable()) return;
            const auto count = static_cast<int>(snapshotColourOptions.size());
            setColourIndex((colourIndex + (direction < 0 ? count - 1 : 1)) % count);
        };
        colourControl.onDiscreteEdited = [this]
        { if (isAvailable()) setColourIndex(juce::roundToInt(colourSlider.getValue())); };

        visibilityControl.setSquareStyle([] { return juce::String("SNAP\nHIDE"); });
        visibilitySlider.setRange(0.0, 1.0, 1.0);
        visibilityControl.setDiscreteChoiceCount(2);
        visibilityControl.onChoiceClicked = [this](int)
        { if (isAvailable()) setHidden(specView.isSnapshotVisible(snapshotIndex)); };
        visibilityControl.onDiscreteEdited = [this]
        { if (isAvailable()) setHidden(visibilitySlider.getValue() >= 0.5); };
        visibilityControl.displayTextProvider = [this]
        { return specView.isSnapshotVisible(snapshotIndex) ? juce::String("HIDE") : juce::String("SHOW"); };

        // Stable action IDs are used when the user pins controls in the dock.
        cameraControl.setName("snapshots.shot");
        visibilityControl.setName("snapshots.hide");

        colourIndex = initialColourIndex;
        bindGainAttachment();
        syncFromSnapshot();
    }

    void resized() override
    {
        using namespace ana::ui::square;
        std::array<juce::Component*, 6> controls {
            &cameraControl, &colourControl, &exportControl,
            &importControl, &visibilityControl, &gainControl
        };
        for (size_t index = 0; index < controls.size(); ++index)
            controls[index]->setBounds(static_cast<int>(index) * (size + spacing),
                                        0, size, size);
    }

    juce::Array<juce::Component*> dockableControls()
    { return { &cameraControl, &visibilityControl }; }

    size_t getSnapshotIndex() const noexcept { return snapshotIndex; }
    void setSnapshotIndex(const size_t newSnapshotIndex)
    {
        if (snapshotIndex != newSnapshotIndex)
        {
            snapshotIndex = newSnapshotIndex;
            bindGainAttachment();
        }
        syncFromSnapshot();
    }
    bool isAvailable() const noexcept { return snapshotIndex < specView.getSnapshotCount(); }
    void syncAvailability()
    {
        const auto available = isAvailable();
        cameraControl.setInteractionEnabled(available);
        colourControl.setInteractionEnabled(available);
        exportControl.setInteractionEnabled(available && specView.hasSnapshotData(snapshotIndex));
        importControl.setInteractionEnabled(available);
        visibilityControl.setInteractionEnabled(available);
        gainControl.setInteractionEnabled(available);
    }
    int getColourIndex() const noexcept { return colourIndex; }
    juce::String getSnapshotName() const { return specView.getSnapshotName(snapshotIndex); }
    juce::Rectangle<int> getGainControlBounds() const noexcept { return gainControl.getBounds(); }

    void setColourIndex(const int newIndex)
    {
        if (!isAvailable()) return;
        colourIndex = juce::jlimit(0, static_cast<int>(snapshotColourOptions.size()) - 1, newIndex);
        colourSlider.setValue(double(colourIndex), juce::dontSendNotification);
        specView.setSnapshotColour(snapshotIndex, ana::ui::graphColour(colourIndex));
        colourControl.repaint();
    }

    void setGainDb(const float gainDb)
    {
        if (!isAvailable()) return;
        const auto clamped = juce::jlimit(SpecView::snapshotGainMinimumDb,
                                         SpecView::snapshotGainMaximumDb, gainDb);
        const auto quantised = std::round(clamped * 100.0f) * 0.01f;
        specView.setSnapshotGain(snapshotIndex, quantised);
        const juce::ScopedValueSetter<bool> guard(updatingGainValue, true);
        gainSlider.setValue(quantised, juce::sendNotificationSync);
        gainControl.repaint();
    }

    void syncFromSnapshot()
    {
        const auto hidden = !specView.isSnapshotVisible(snapshotIndex);
        visibilitySlider.setValue(hidden ? 1.0 : 0.0, juce::dontSendNotification);
        visibilityControl.repaint();
        syncGainFromSnapshot();
        const auto snapshotColour = specView.getSnapshotColour(snapshotIndex);
        for (size_t index = 0; index < snapshotColourOptions.size(); ++index)
            if (ana::ui::graphColour(static_cast<int>(index)) == snapshotColour)
            {
                colourIndex = static_cast<int>(index);
                colourSlider.setValue(double(colourIndex), juce::dontSendNotification);
                break;
            }
        colourControl.repaint();
        syncAvailability();
    }

    std::function<void(SnapshotRow&)> onImportRequested;
    std::function<void(SnapshotRow&)> onExportRequested;
    std::function<void(SnapshotRow&)> onGainResetPromptRequested;
    std::function<void(bool)> onTextEditingChanged;

private:
    static juce::String snapshotGainId(size_t i)
    { return "snapshot_gain_" + juce::String(static_cast<int>(i) + 1); }
    void setHidden(bool hidden)
    {
        specView.setSnapshotVisible(snapshotIndex, !hidden);
        visibilitySlider.setValue(hidden ? 1.0 : 0.0, juce::dontSendNotification);
        visibilityControl.repaint();
    }
    void bindGainAttachment()
    {
        const auto savedGain = specView.getSnapshotGain(snapshotIndex);
        gainAttachment.reset();
        {
            const juce::ScopedValueSetter<bool> guard(updatingGainValue, true);
            if (snapshotIndex < 64)
                gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    specView.getProcessor().getParameters(), snapshotGainId(snapshotIndex), gainSlider);
            gainSlider.setValue(savedGain, juce::sendNotificationSync);
        }
        if (isAvailable())
            specView.setSnapshotGain(snapshotIndex, savedGain);
        gainControl.repaint();
    }
    void syncGainFromSnapshot()
    {
        const juce::ScopedValueSetter<bool> guard(updatingGainValue, true);
        gainSlider.setValue(specView.getSnapshotGain(snapshotIndex), juce::sendNotificationSync);
        gainControl.repaint();
    }

    SpecView& specView;
    size_t snapshotIndex = 0;
    int colourIndex = 0;
    bool updatingGainValue = false;
    juce::Slider gainSlider, cameraSlider, colourSlider, exportSlider, importSlider, visibilitySlider;
    NumericValueBox gainControl { gainSlider };
    NumericValueBox cameraControl { cameraSlider };
    NumericValueBox colourControl { colourSlider };
    NumericValueBox exportControl { exportSlider };
    NumericValueBox importControl { importSlider };
    NumericValueBox visibilityControl { visibilitySlider };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
};
} // namespace

// Name tiles are the only per-snapshot UI objects. All six actions above
// are shared and rebound to the selected tile.
class SnapshotNameTile final : public juce::Component
{
public:
    SnapshotNameTile(SpecView& view, size_t indexIn)
        : specView(view), index(indexIn)
    {
        getProperties().set("emptyEditorBackground", true);
        label.setFont(ana::ui::makeFont());
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
        label.setColour(juce::TextEditor::highlightColourId, ana::ui::light);
        label.setColour(juce::TextEditor::highlightedTextColourId, ana::ui::black);
        label.setBorderSize(juce::BorderSize<int>(0, ana::ui::gap.pixels(), 0, ana::ui::gap.pixels()));
        label.setDrawBackground(true);
        label.setHoverEnabled(false); // Selected and idle tiles have stable colours; no hover flash.
        label.setEditable(false, false, false); // Editing is started only by RENAME.
        label.setInterceptsMouseClicks(false, true); // Route clicks and wheel to the tile.
        label.setTooltip("CLICK TO SELECT");
        label.onTextChange = [this]
        {
            auto name = label.getText().trim().substring(0, maximumSnapshotNameLength);
            if (name.isEmpty())
                name = "SNAPSHOT " + juce::String(static_cast<int>(index + 1));
            specView.setSnapshotName(index, name);
            if (name != label.getText()) label.setText(name, juce::dontSendNotification);
        };
        label.onEditorVisibilityChanged = [this] (bool editing)
        { if (onTextEditingChanged) onTextEditingChanged(editing); };
        addAndMakeVisible(label);
        refresh();
    }
    void resized() override { label.setBounds(getLocalBounds()); }
    void setSnapshotIndex(size_t newIndex) { index = newIndex; refresh(); }
    size_t getSnapshotIndex() const noexcept { return index; }
    void refresh()
    {
        auto name = specView.getSnapshotName(index);
        if (name.isEmpty()) name = "SNAPSHOT " + juce::String(static_cast<int>(index + 1));
        label.setText(name, juce::dontSendNotification);
    }
    void beginRename() { label.showEditor(); }
    void setSelected(bool selected)
    {
        label.setColour(juce::Label::backgroundColourId, selected ? juce::Colour(0xffffffff) : ana::ui::dark);
        label.setColour(juce::Label::textColourId, selected ? juce::Colour(0xff222222) : ana::ui::white);
        label.setColour(juce::Label::outlineColourId,
                        selected ? ana::ui::white : snapshotFieldBorderColour);
        label.repaint();
    }
    std::function<void(size_t)> onSelected;
    std::function<void(bool)> onTextEditingChanged;
    std::function<void(const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;
private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;
        if (onSelected) onSelected(index);
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (onWheel) onWheel(event, wheel);
    }
    SpecView& specView;
    size_t index;
    EllipsisLabel label;
};

class SnapshotsWindowContent final : public juce::Component
{
public:
    explicit SnapshotsWindowContent(SpecView& specViewIn)
        : specView(specViewIn), actionsRow(specViewIn, 0, ana::ui::defaultGraphColourIndex)
    {
        setName("unit.snapshots");
        getProperties().set("emptyEditorBackground", true);
        setOpaque(false);
        const auto commandControl = [] (NumericValueBox& control, juce::Slider& slider,
                                        const juce::String& title, std::function<void()> action)
        {
            slider.setRange(0.0, 1.0, 1.0);
            control.setSquareStyle([title] { return title; });
            control.displayTextProvider = [title] { return title; };
            control.setNumericControlEnabled(false);
            control.setDiscreteChoiceCount(2);
            control.setSlotIndicatorVisible(false);
            control.onChoiceClicked = [action, &slider] (int)
            { action(); slider.setValue(0.0, juce::dontSendNotification); };
            control.onDiscreteEdited = [action, &slider]
            {
                if (slider.getValue() >= 0.5) action();
                slider.setValue(0.0, juce::dontSendNotification);
            };
        };
        commandControl(addControl, addSlider, "ADD", [this] { addSnapshotRow(); });
        addAndMakeVisible(addControl);
        commandControl(bulkImportControl, bulkImportSlider, "IMP\nALL", [this] { beginBulkSnapshotImport(); });
        addAndMakeVisible(bulkImportControl);
        commandControl(deleteControl, deleteSlider, "DELETE", [this]
        { if (hasSelection()) deleteSnapshotRow(selectedSnapshot); });
        addAndMakeVisible(deleteControl);
        commandControl(renameControl, renameSlider, "RENAME", [this]
        {
            if (hasSelection() && selectedSnapshot < snapshotRows.size())
                snapshotRows[selectedSnapshot]->beginRename();
        });
        addAndMakeVisible(renameControl);

        actionsRow.onImportRequested = [this](SnapshotRow&)
        { clearInteractionSelection(); beginSnapshotImport(); };
        actionsRow.onExportRequested = [this](SnapshotRow&)
        { clearInteractionSelection(); beginSnapshotExport(); };
        actionsRow.onGainResetPromptRequested = [this](SnapshotRow& row)
        { showGainResetPrompt(row); };
        actionsRow.onTextEditingChanged = [this](bool editing)
        { if (onTextEditingChanged) onTextEditingChanged(editing); };
        addAndMakeVisible(actionsRow);

        rowsContent.getProperties().set("emptyEditorBackground", true);
        rowsViewport.getProperties().set("emptyEditorBackground", true);
        rowsViewport.setViewedComponent(&rowsContent, false);
        rowsViewport.setScrollBarsShown(false, false, true, false);
        rowsViewport.setWantsKeyboardFocus(false);
        rowsViewport.setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(rowsViewport);

        specView.onSnapshotStateRestored = [this] { restoreSnapshotRows(); };
        restoreSnapshotRows();
    }

    ~SnapshotsWindowContent() override
    {
        specView.onSnapshotStateRestored = nullptr;
        rowsViewport.setViewedComponent(nullptr, false);
    }

    juce::Array<juce::Component*> dockableControls()
    { return actionsRow.dockableControls(); }

    void paint(juce::Graphics& graphics) override
    { graphics.fillAll(ana::ui::square::panelBackground); }

    void resized() override
    {
        using namespace ana::ui::square;
        auto area = getLocalBounds();
        auto row = area.removeFromTop(size);
        addControl.setBounds(row.removeFromLeft(size));
        row.removeFromLeft(spacing);
        bulkImportControl.setBounds(row.removeFromLeft(size));
        area.removeFromTop(spacing);
        actionsRow.setBounds(area.removeFromTop(size));
        area.removeFromTop(spacing);
        row = area.removeFromTop(size);
        deleteControl.setBounds(row.removeFromLeft(size));
        row.removeFromLeft(spacing);
        renameControl.setBounds(row.removeFromLeft(size));
        area.removeFromTop(spacing);
        rowsViewport.setBounds(area);

        const auto stride = ana::ui::controlHeight + spacing;
        const auto rowsHeight = snapshotRows.empty()
            ? 0 : static_cast<int>(snapshotRows.size()) * stride - spacing;
        rowsContent.setSize(std::max(1, area.getWidth()), std::max(area.getHeight(), rowsHeight));
        auto listRow = rowsContent.getLocalBounds();
        for (auto& snapshot : snapshotRows)
        {
            snapshot->setBounds(listRow.removeFromTop(ana::ui::controlHeight));
            listRow.removeFromTop(spacing);
        }

        if (gainResetPrompt != nullptr)
        { gainResetPrompt->setBounds(getLocalBounds()); gainResetPrompt->toFront(true); }
    }

    void dismissGainResetPrompt() { gainResetPrompt.reset(); }

    std::function<void(bool)> onTextEditingChanged;
    std::function<void(int)> onPreferredHeightChanged;

    int preferredEmbeddedHeight() const noexcept
    {
        const int count = juce::jmax(1, static_cast<int>(snapshotRows.size()));
        const int gap = ana::ui::square::spacing;
        return 3 * ana::ui::square::size + 3 * gap
            + count * ana::ui::controlHeight + (count - 1) * gap;
    }

private:
    static constexpr size_t invalidIndex = static_cast<size_t>(-1);
    bool hasSelection() const noexcept
    { return selectedSnapshot < specView.getSnapshotCount(); }
    void notifyPreferredHeightChanged()
    { if (onPreferredHeightChanged) onPreferredHeightChanged(preferredEmbeddedHeight()); }

    void selectSnapshot(size_t index)
    {
        if (index >= snapshotRows.size()) return;
        selectedSnapshot = index;
        for (size_t i = 0; i < snapshotRows.size(); ++i)
            snapshotRows[i]->setSelected(i == index);
        actionsRow.setSnapshotIndex(index);
        deleteControl.setInteractionEnabled(true);
        renameControl.setInteractionEnabled(true);
    }

    void restoreSnapshotRows()
    {
        dismissGainResetPrompt();
        const auto previousSelection = selectedSnapshot;
        snapshotRows.clear();
        for (size_t index = 0; index < specView.getSnapshotCount(); ++index)
            addNameTile(index);
        if (!snapshotRows.empty())
            selectSnapshot(previousSelection == invalidIndex ? 0
                : std::min(previousSelection, snapshotRows.size() - 1));
        else
        {
            selectedSnapshot = invalidIndex;
            actionsRow.syncAvailability();
            deleteControl.setInteractionEnabled(false);
            renameControl.setInteractionEnabled(false);
        }
        resized();
        revealSelected();
        notifyPreferredHeightChanged();
    }

    void revealSelected()
    {
        if (!hasSelection()) return;
        const int bottom = static_cast<int>(selectedSnapshot) *
            (ana::ui::controlHeight + ana::ui::square::spacing) + ana::ui::controlHeight;
        if (bottom > rowsViewport.getViewPosition().y + rowsViewport.getHeight())
            rowsViewport.setViewPosition(0, bottom - rowsViewport.getHeight());
    }

    void addNameTile(size_t index)
    {
        auto row = std::make_unique<SnapshotNameTile>(specView, index);
        row->onSelected = [this](size_t selected) { selectSnapshot(selected); };
        row->onWheel = [this](const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
        {
            // Scroll snapshot names when possible; at either end pass the wheel to
            // the enclosing Settings viewport instead of trapping it on a tile.
            auto* viewed = rowsViewport.getViewedComponent();
            const auto maximum = viewed != nullptr
                ? juce::jmax(0, viewed->getHeight() - rowsViewport.getHeight()) : 0;
            const auto delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX)
                ? wheel.deltaY : -wheel.deltaX;
            const auto position = rowsViewport.getViewPosition().y;
            const bool canScrollNames = maximum > 0 && delta != 0.0f
                && !((delta > 0.0f && position == 0)
                    || (delta < 0.0f && position >= maximum));
            if (canScrollNames)
            {
                rowsViewport.mouseWheelMove(event, wheel);
                return;
            }
            for (auto* parent = getParentComponent(); parent != nullptr;
                 parent = parent->getParentComponent())
                if (auto* settings = dynamic_cast<SettingsViewport*>(parent))
                {
                    settings->mouseWheelMove(event, wheel);
                    break;
                }
        };
        row->onTextEditingChanged = [this](bool editing)
        { if (onTextEditingChanged) onTextEditingChanged(editing); };
        rowsContent.addAndMakeVisible(*row);
        snapshotRows.push_back(std::move(row));
    }

    size_t addSnapshotRow()
    {
        const auto index = specView.addSnapshotSlot();
        specView.setSnapshotName(index, "SNAPSHOT " + juce::String(static_cast<int>(index + 1)));
        specView.setSnapshotColour(index, ana::ui::graphColour(ana::ui::defaultGraphColourIndex));
        addNameTile(index);
        selectSnapshot(index);
        resized();
        revealSelected();
        notifyPreferredHeightChanged();
        return index;
    }

    void showGainResetPrompt(SnapshotRow& row)
    {
        if (!hasSelection()) return;
        dismissGainResetPrompt();
        const auto anchorBounds = getLocalArea(&row, row.getGainControlBounds());
        const auto targetIndex = selectedSnapshot;
        gainResetPrompt = std::make_unique<InlineResetPrompt>(anchorBounds, false,
            [safeContent = juce::Component::SafePointer<SnapshotsWindowContent>(this), targetIndex]
            {
                if (safeContent != nullptr && targetIndex < safeContent->specView.getSnapshotCount())
                {
                    safeContent->specView.setSnapshotGain(targetIndex, 0.0f);
                    if (safeContent->selectedSnapshot == targetIndex)
                        safeContent->actionsRow.syncFromSnapshot();
                }
            },
            [safeContent = juce::Component::SafePointer<SnapshotsWindowContent>(this)]
            { if (safeContent != nullptr) safeContent->dismissGainResetPrompt(); });
        addAndMakeVisible(*gainResetPrompt);
        gainResetPrompt->setBounds(getLocalBounds()); gainResetPrompt->toFront(true);
    }

    void deleteSnapshotRow(size_t index)
    {
        if (index >= snapshotRows.size() || !specView.removeSnapshotSlot(index)) return;
        snapshotRows.erase(snapshotRows.begin() + static_cast<std::ptrdiff_t>(index));
        for (size_t i = index; i < snapshotRows.size(); ++i)
            snapshotRows[i]->setSnapshotIndex(i);
        if (snapshotRows.empty())
        {
            selectedSnapshot = invalidIndex;
            actionsRow.syncAvailability();
            deleteControl.setInteractionEnabled(false);
            renameControl.setInteractionEnabled(false);
        }
        else selectSnapshot(std::min(index, snapshotRows.size() - 1));
        resized(); notifyPreferredHeightChanged();
    }

    void clearInteractionSelection()
    {
        dismissGainResetPrompt();
        juce::Component::unfocusAllComponents();
    }

    void beginBulkSnapshotImport()
    {
        const auto startDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        snapshotFileChooser = std::make_unique<juce::FileChooser>(
            "IMPORT SNAPSHOTS", startDirectory, snapshotFileWildcard);
        juce::Component::SafePointer<SnapshotsWindowContent> safeContent(this);
        snapshotFileChooser->launchAsync(
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::canSelectMultipleItems,
            [safeContent] (const juce::FileChooser& chooser)
            {
                if (safeContent == nullptr) return;
                for (const auto& file : chooser.getResults())
                {
                    if (file.getFullPathName().isEmpty()) continue;
                    const auto index = safeContent->addSnapshotRow();
                    if (!safeContent->importSnapshotFromFile(index, file))
                        safeContent->deleteSnapshotRow(index);
                }
            });
    }

    void beginSnapshotExport()
    {
        if (!hasSelection() || !specView.hasSnapshotData(selectedSnapshot)) return;
        const auto index = selectedSnapshot;
        auto stem = juce::File::createLegalFileName(specView.getSnapshotName(index).trim());
        if (stem.isEmpty()) stem = "snapshot";
        const auto defaultFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(stem + snapshotFileExtension);
        snapshotFileChooser = std::make_unique<juce::FileChooser>(
            "EXPORT SNAPSHOT", defaultFile, snapshotFileWildcard);
        juce::Component::SafePointer<SnapshotsWindowContent> safeContent(this);
        snapshotFileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeContent, index] (const juce::FileChooser& chooser)
            {
                if (safeContent == nullptr) return;
                const auto file = chooser.getResult();
                if (file.getFullPathName().isNotEmpty())
                    safeContent->exportSnapshotToFile(index, file);
            });
    }

    void beginSnapshotImport()
    {
        if (!hasSelection()) return;
        const auto index = selectedSnapshot;
        const auto startDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        snapshotFileChooser = std::make_unique<juce::FileChooser>(
            "IMPORT SNAPSHOT", startDirectory, snapshotFileWildcard);
        juce::Component::SafePointer<SnapshotsWindowContent> safeContent(this);
        snapshotFileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeContent, index] (const juce::FileChooser& chooser)
            {
                if (safeContent == nullptr) return;
                const auto file = chooser.getResult();
                if (file.getFullPathName().isNotEmpty())
                    safeContent->importSnapshotFromFile(index, file);
            });
    }

    bool exportSnapshotToFile(size_t index, const juce::File& requestedFile)
    {
        if (index >= specView.getSnapshotCount() || !specView.hasSnapshotData(index)) return false;
        const auto file = requestedFile.hasFileExtension("anasnapshot")
            ? requestedFile : requestedFile.withFileExtension(snapshotFileExtension);
        juce::MemoryOutputStream output;
        if (!output.writeInt(snapshotFileMagic)
            || !output.writeInt(snapshotFileVersion)
            || !output.writeString(specView.getSnapshotName(index))
            || !specView.writeSnapshot(index, output)) return false;
        return file.replaceWithData(output.getData(), output.getDataSize());
    }

    bool importSnapshotFromFile(size_t index, const juce::File& file)
    {
        if (index >= specView.getSnapshotCount()) return false;
        juce::FileInputStream input(file);
        if (input.failedToOpen() || input.readInt() != snapshotFileMagic) return false;
        if (input.readInt() != snapshotFileVersion) return false;
        auto importedName = input.readString().trim();
        if (importedName.length() > maximumSnapshotNameLength) return false;
        if (!specView.readSnapshot(index, input)) return false;
        if (importedName.isEmpty())
            importedName = "SNAPSHOT " + juce::String(static_cast<int>(index + 1));
        specView.setSnapshotName(index, importedName);
        if (index < snapshotRows.size()) snapshotRows[index]->refresh();
        if (index == selectedSnapshot) actionsRow.syncFromSnapshot();
        return true;
    }

    SpecView& specView;
    juce::Slider addSlider, bulkImportSlider, deleteSlider, renameSlider;
    NumericValueBox addControl { addSlider };
    NumericValueBox bulkImportControl { bulkImportSlider };
    SnapshotRow actionsRow;
    NumericValueBox deleteControl { deleteSlider };
    NumericValueBox renameControl { renameSlider };
    juce::Component rowsContent;
    SettingsViewport rowsViewport;
    std::vector<std::unique_ptr<SnapshotNameTile>> snapshotRows;
    size_t selectedSnapshot = invalidIndex;
    std::unique_ptr<InlineResetPrompt> gainResetPrompt;
    std::unique_ptr<juce::FileChooser> snapshotFileChooser;
};

class SettingsWindow final : public HostShortcutDocumentWindow
{
public:
    SettingsWindow(SettingsPanel& panel, std::function<void()> closeCallbackIn)
        : HostShortcutDocumentWindow(ana::ui::square::panelBackground, Side::right),
          panelRef(panel),
          content(panel),
          closeCallback(std::move(closeCallbackIn))
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setResizable(false, false);
        // Minimum Settings window is square; only its height may grow.
        setResizeLimits(settingsWindowWidth, settingsWindowWidth,
                        settingsWindowWidth, maximumSettingsWindowHeight);

        resizeContent = std::make_unique<ana::ui::EdgeResizeContent>(
            *this, *getConstrainer(), content, false, false);
        resizeContent->setSize(settingsWindowWidth, settingsWindowWidth);
        setContentNonOwned(resizeContent.get(), false);
        resizeContent->getProperties().set("topGapDragOnly",true);
        enableBackgroundDragging(*resizeContent);
        setSize(settingsWindowWidth, settingsWindowWidth);
        panelRef.onTextEditingChanged = [this] (const bool isEditing)
        {
            setTextInputActive(isEditing);
        };
        content.dock.onModeChanged=[this] { applyDockMode(); };
        applyDockMode(true);
        initialiseDesktopPeer();
    }

    void applyDockMode(bool initial = false)
    {
        panelRef.dismissParameterEditors(); content.dismissChoicePrompt(); content.dismissResetPrompt();
        auto& state=panelRef.getProcessor().getParameters().state;
        setBackgroundDragSnappingEnabled(content.dock.dockOnly());
        if(content.dock.dockOnly())
        {
            if(!initial && getWidth()>104 && getHeight()>104) state.setProperty("ana.settings.dock.expandedHeight",getHeight(),nullptr);
            const auto size=content.dock.collapsedSize();
            const int width=size.x,height=size.y;
            setResizeLimits(width,height,width,height); setSize(width,height);
        }
        else
        {
            const int height=juce::jlimit(settingsWindowWidth,maximumSettingsWindowHeight,int(state.getProperty("ana.settings.dock.expandedHeight",settingsWindowWidth)));
            setResizeLimits(settingsWindowWidth,settingsWindowWidth,settingsWindowWidth,maximumSettingsWindowHeight); setSize(settingsWindowWidth,height);
        }
        content.resized();
    }

    ~SettingsWindow() override
    {
        disableBackgroundDragging();
        panelRef.onTextEditingChanged = nullptr;
        clearContentComponent();
    }

    void showFor(juce::Component& owner)
    {
        if (!initialPositionRestored)
        {
            initialPositionRestored = true;
            const auto& state = panelRef.getProcessor().getParameters().state;
            if (content.dock.dockOnly() && state.hasProperty("ana.settings.dock.x")
                && state.hasProperty("ana.settings.dock.y"))
            {
                auto bounds = getBounds();
                bounds.setPosition(static_cast<int>(state.getProperty("ana.settings.dock.x")),
                                   static_cast<int>(state.getProperty("ana.settings.dock.y")));
                showAt(owner, bounds);
            }
            else
                showBeside(owner);
        }
        else
            showAt(owner, getBounds());
        rememberDockGeometry();
        scheduleDockMouseRegionSync();
    }

    void showAtScreensetBounds(juce::Component& owner, juce::Rectangle<int> bounds)
    {
        initialPositionRestored = true;
        showAt(owner, bounds);
        rememberDockGeometry();
        scheduleDockMouseRegionSync();
    }

    void moved() override { rememberDockGeometry(); }
    void resized() override
    {
        HostShortcutDocumentWindow::resized();
        rememberDockGeometry();
    }

    void rememberDockGeometry()
    {
        if (!initialPositionRestored || !isVisible() || !content.dock.dockOnly()) return;
        const auto bounds = getBounds();
        if (bounds.isEmpty()) return;
        auto& state = panelRef.getProcessor().getParameters().state;
        const auto put = [&state](const char* key, int value)
        {
            if (static_cast<int>(state.getProperty(key, -999999)) != value)
                state.setProperty(key, value, nullptr);
        };
        put("ana.settings.dock.x", bounds.getX());
        put("ana.settings.dock.y", bounds.getY());
    }

    void showAboutPopup(AboutPopup& popup) { content.showAboutPopup(popup); }

    void showChoicePrompt(ParameterControl& control)
    {
        content.showChoicePrompt(control);
    }

    void refreshFor(juce::Component& owner)
    {
        content.dismissChoicePrompt();
        content.dismissResetPrompt();
        showFor(owner);
        // Finish any deferred inline-editor focus restoration before raising
        // the updated settings once. Do not pin it above the main window.
        juce::Component::SafePointer<SettingsWindow> safeWindow(this);
        juce::MessageManager::callAsync([safeWindow]
        {
            if (safeWindow != nullptr && safeWindow->isVisible())
                safeWindow->toFront(false);
        });
    }

    void showResetPrompt(ParameterControl& control)
    {
        content.showResetPrompt(control);
    }

    void hideWindow()
    {
        panelRef.dismissParameterEditors();
        content.dismissChoicePrompt();
        content.dismissResetPrompt();
        hideAuxiliaryWindow();
    }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback();
        else
            hideWindow();
    }

private:
    // REAPER restores screenset windows using native positioning, while JUCE
    // maintains its own peer / mouse hit-test geometry. After a project or a
    // screenset restore the dock can be drawn at the right coordinates but the
    // collapse/orientation buttons do not receive clicks until the user moves
    // the window. Perform the same native position notification that a drag
    // performs, then refresh the child hit-test rectangles. The delayed pass
    // covers REAPER positioning the HWND after our screenset callback returns.
    void syncDockMouseRegions()
    {
        if (!isVisible() || getPeer() == nullptr)
            return;
        // Works for the expanded dock too. Don't move the native window to
        // the same point: on macOS this is not a hit-test repair and may cause
        // extra window-server work during screenset restores.
       #if JUCE_MAC
        enableAuxiliaryMouseMoveEvents(*this);
       #endif
        if (resizeContent != nullptr)
            resizeContent->resized();
        content.resized();
        content.dock.resized();
        content.dock.repaint();
        juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
    }

    void scheduleDockMouseRegionSync()
    {
        const auto generation = ++dockMouseSyncGeneration;
        juce::Component::SafePointer<SettingsWindow> safe(this);
        const auto sync = [safe, generation]
        {
            if (safe != nullptr && safe->dockMouseSyncGeneration == generation)
                safe->syncDockMouseRegions();
        };
        juce::MessageManager::callAsync(sync);
        juce::Timer::callAfterDelay(120, sync);
    }

    SettingsPanel& panelRef;
    SettingsWindowContent content;
    std::unique_ptr<ana::ui::EdgeResizeContent> resizeContent;
    std::function<void()> closeCallback;
    bool initialPositionRestored = false;
    unsigned int dockMouseSyncGeneration = 0;
};

namespace
{
// Prevent duplicated FX instances from registering the same screenset identifier.
std::set<std::string> registeredAnaDockIds;
}

PluginEditor::PluginEditor(PluginProcessor& processorRef)
    : AudioProcessorEditor(&processorRef)
    , AudioProcessorEditorARAExtension(&processorRef)
    , audioProcessor(processorRef),
      scopDisplay(processorRef),
      settingsComponent(processorRef),
      specDisplay(processorRef),
      corrDisplay(processorRef),
      lvlsDisplay(processorRef)
{
    // Opening a new editor must never strand the user in CLEAN VIEW.
    if (auto* clean = audioProcessor.getParameters().getParameter(PluginProcessor::cleanViewParameterId))
        if (clean->getValue() >= 0.5f)
            clean->setValueNotifyingHost(0.0f);
    displayedStateRevision = audioProcessor.getStateRevision();
    squareResizeMode = static_cast<bool>(audioProcessor.getParameters().state.getProperty("ui.square-resize", false));
    getProperties().set("emptyEditorBackground",true);
    setName("editor");
#if JUCE_MAC
    svgExportKeyboardMonitor = ana::svg_export::installKeyboardShortcut(*this, [this] { toggleSquareResizeMode(); });
#endif
    for (auto* component : std::array<juce::Component*, 8> {
             &scopDisplay, &specDisplay, &corrDisplay, &lvlsDisplay,
             &fullSourceButton,
             &clearButton, &freezeButton, &aboutButton })
        addAndMakeVisible(*component);

    updateButton.onClick = [this] { if(!udDragMoved) showAnalyzerSettings(!showingAnalyzerSettings); };
    updateButton.addMouseListener(this,false);
    aboutButton.setSquareRailVisible(true);
    aboutButton.setTooltip("ABOUT");
    updateButton.setTooltip("SETTINGS / UPDATE STATUS");
    updateButton.setSlotIndicatorVisible(false);
    updateButton.setTextHorizontalInset(2);
    updateButton.setComponentID("update-settings");
    freezeButton.setTooltip("FREEZE");
    fullSourceButton.setTooltip("FULL");
    clearButton.setTooltip("CLEAR");
    aboutButton.addMouseListener(this, false);
    aboutButton.onClick = [this]
    {
        if (animationOverlay == nullptr || !animationOverlay->isVisible())
        {
            if (aboutPopup != nullptr) dismissAboutPopup();
            else showAboutPopup();
        }
    };
    for (auto* component : std::array<juce::Component*, 5> {
             &takeButton, &keepSecondTakeButton, &refreshButton,
             &locationButton, &updateButton })
        addChildComponent(*component);
    updateButton.setVisible(true);
    refreshButton.addMouseListener(this, false);
    refreshPress.onArmed = [this]
    {
        if (!audioProcessor.isOfflineMode()) return;
        audioProcessor.setOfflineAutomaticRefresh(! audioProcessor.isOfflineAutomaticRefresh());
        refreshButton.setToggleState(audioProcessor.isOfflineAutomaticRefresh(),
                                     juce::dontSendNotification);
        refreshOfflineSelectionButtons();
    };
    refreshButton.onClick = [this]
    {
        const auto release = refreshPress.release();
        if (release == LongPressGesture::ReleaseResult::longPress
            || release == LongPressGesture::ReleaseResult::cancelled)
            return;
        audioProcessor.requestOfflineRefresh();
        refreshOfflineSelectionButtons();
        updateButton.setButtonText("00");
    };

    locationButton.onClick = [this]
    {
        if (!audioProcessor.isOfflineMode()) return;
        dismissChoicePrompt();
        auto* parameter=audioProcessor.getParameters().getParameter(PluginProcessor::offlineMonitorParameterId);
        parameter->beginChangeGesture();
        audioProcessor.setOfflineMonitorLocation(! audioProcessor.isOfflineMonitorLocation());
        parameter->endChangeGesture();
        refreshOfflineSelectionButtons();
        resized(); // mode-specific TAKE/KST slot changes immediately
        scopDisplay.refreshWaveform();
    };
    locationButton.setClickingTogglesState(true);
    keepSecondTakeButton.setClickingTogglesState(true);
    locationButton.getProperties().set("oscParameterId",PluginProcessor::offlineMonitorParameterId);
    keepSecondTakeButton.getProperties().set("oscParameterId",PluginProcessor::offlineKeepTakeParameterId);
    refreshButton.setSquareRailVisible(true);
    refreshButton.getProperties().set("oscParameterId",PluginProcessor::offlineRefreshParameterId);
    locationButton.setInteractionEnabled(false);
    keepSecondTakeButton.setInteractionEnabled(false);
    refreshButton.setInteractionEnabled(false);
    settingsComponent.attachMainControl(locationButton, 0);
    settingsComponent.attachMainControl(keepSecondTakeButton, 1);
    settingsComponent.attachMainControl(refreshButton, 2);
    settingsComponent.attachMainControl(aboutButton, 3, false);
    snapshotsContent = std::make_unique<SnapshotsWindowContent>(specDisplay);
    // The Settings panel owns placement, the editor owns the component lifetime.
    settingsComponent.attachSnapshotsContent(*snapshotsContent,
        snapshotsContent->preferredEmbeddedHeight(), snapshotsContent->dockableControls());
    snapshotsContent->onPreferredHeightChanged = [this](int height)
    {
        settingsComponent.setSnapshotsPreferredHeight(height);
    };
    snapshotsContent->onTextEditingChanged = [this](bool editing)
    {
        if (settingsComponent.onTextEditingChanged)
            settingsComponent.onTextEditingChanged(editing);
    };
    animationSettingsPanel=std::make_unique<AnimationSettingsPanel>(audioProcessor.getParameters(),animationSettings);
    animationSettingsPanel->setName("unit.animation");
    animationSettingsPanel->onTextEditingChanged=[this](bool editing) { if (settingsComponent.onTextEditingChanged) settingsComponent.onTextEditingChanged(editing); };
    animationSettingsPanel->onPreferredHeightChanged=[this] { settingsComponent.resized(); };
    settingsComponent.attachAnimationContent(*animationSettingsPanel);
    takeButton.onClick = [this] { showOfflineTakePrompt(); };
    keepSecondTakeButton.setTooltip("KEEP SECOND TAKE");
    keepSecondTakeButton.onClick = [this]
    {
        if (!audioProcessor.isOfflineMode()) return;
        auto* parameter=audioProcessor.getParameters().getParameter(PluginProcessor::offlineKeepTakeParameterId);
        parameter->beginChangeGesture();
        audioProcessor.setOfflineKeepSecondTake(! audioProcessor.isOfflineKeepSecondTake());
        parameter->endChangeGesture();
        refreshOfflineSelectionButtons();
    };



    settingsComponent.onDisplaySettingsChanged = [this]
    {
        scopDisplay.refreshDisplaySettings();
        // Horizontal controls change how much height each SCOP band needs.
        // Recalculate the host resize constraint as soon as a setting changes.
        if (activePage == ana::AnalyzerPage::scop)
        {
            updateResizeLimits();
            const auto minimum = minimumHeightForCurrentPage();
            if (getHeight() < minimum)
                setSize(getWidth(), minimum);
        }
        specDisplay.resized();
        specDisplay.repaint();
        corrDisplay.resized();
        corrDisplay.repaint();
        lvlsDisplay.resized();
    };
    settingsComponent.onScopBandLayoutChanged = [this]
    {
        scopDisplay.refreshBandLayout();
        updateResizeLimits();
        const auto minHeight = minimumHeightForCurrentPage();
        if (getHeight() < minHeight)
            setSize(getWidth(), minHeight);
    };
    settingsComponent.onScopLinkedNormalizeChanged = [this](bool enabled)
    { scopDisplay.setLinkedNormalization(enabled); };
    settingsComponent.onScopSlopeChanged = [this]
    {
        if (audioProcessor.isOfflineMode())
            scopDisplay.refreshWaveform();
    };
    settingsComponent.onLvlsPartsLayoutChanged = [this] { lvlsDisplay.refreshPartLayout(); };
    settingsComponent.onAnalysisModeChanged = [this] { timerCallback(); };
    settingsComponent.onDisplaySelected = [safeEditor = juce::Component::SafePointer<PluginEditor>(this)] (int index)
    {
        if (safeEditor == nullptr) return;
        auto& editor = *safeEditor;
        if (index == 0 || index == 1)
        {
            editor.audioProcessor.setSpecMapView(index == 1);
            editor.showAnalyzerPage(ana::AnalyzerPage::spec, editor.showingAnalyzerSettings);
            editor.specDisplay.refreshMode();
        }
        else if (index >= 2 && index <= 4)
        {
            editor.audioProcessor.setCorrMode(index == 2 ? 2 : index == 3 ? 0 : 1);
            editor.showAnalyzerPage(ana::AnalyzerPage::corr, editor.showingAnalyzerSettings);
            editor.corrDisplay.refreshMode();
        }
        else if (index == 5)
            editor.showAnalyzerPage(ana::AnalyzerPage::lvls, editor.showingAnalyzerSettings);
        else if (index == 6)
            editor.showAnalyzerPage(ana::AnalyzerPage::scop, editor.showingAnalyzerSettings);
        editor.settingsComponent.refreshExternalState();
        editor.repaint();
    };
    settingsComponent.onChoiceRequested = [this] (ParameterControl& control)
    {
        if (settingsWindow != nullptr)
            settingsWindow->showChoicePrompt(control);
    };
    settingsComponent.onResetRequested = [this] (ParameterControl& control)
    {
        if (settingsWindow != nullptr)
            settingsWindow->showResetPrompt(control);
    };

    settingsComponent.onFreezeChanged = [this] (bool frozen)
    {
        freezeButton.setToggleState(frozen, juce::dontSendNotification);
        if (freezeButton.onClick) freezeButton.onClick();
    };
    settingsComponent.onOverallFull = [this] { if (fullSourceButton.onClick) fullSourceButton.onClick(); };
    settingsComponent.onScopClear = [this] { scopDisplay.clearHistory(); };

    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this]
    {
        if (audioProcessor.isOfflineMode()) return;
        if (activePage == ana::AnalyzerPage::spec)
            audioProcessor.getSpecProcessor().setFrozen(freezeButton.getToggleState());
        else if (activePage == ana::AnalyzerPage::corr)
            audioProcessor.getCorrProcessor().setFrozen(freezeButton.getToggleState());
        else if (activePage == ana::AnalyzerPage::lvls)
            audioProcessor.setLvlsFrozen(freezeButton.getToggleState());
        else
        {
            scopFrozen = freezeButton.getToggleState();
            scopDisplay.setFrozen(scopFrozen);
        }
    };
    fullSourceButton.onClick = [this]
    {
        if (audioProcessor.getScopSingleViewBand() >= 0)
            return;
        const auto enabled = ! audioProcessor.isScopFullSourceView();
        audioProcessor.setScopFullSourceView(enabled);
        scopDisplay.setFullSourceView(enabled);
    };
    clearButton.onClick = [this]
    {
        if (activePage != ana::AnalyzerPage::scop)
            return;
        scopDisplay.clearHistory();
    };

    showAnalyzerPage(audioProcessor.getAnalyzerPageState(),
                     false);


    timerCallback();
    startTimerHz(15);
    editorSizeConstrainer = std::make_unique<EditorSizeConstrainer>();
    setConstrainer(editorSizeConstrainer.get());
    setResizable(true, false);
    leftEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, juce::ResizableEdgeComponent::leftEdge);
    topEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, juce::ResizableEdgeComponent::topEdge);
    rightEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, juce::ResizableEdgeComponent::rightEdge);
    bottomEdgeResizer = std::make_unique<InvisibleResizableEdgeComponent>(
        this, juce::ResizableEdgeComponent::bottomEdge);
    leftEdgeResizer->setAlwaysOnTop(true);
    topEdgeResizer->setAlwaysOnTop(true);
    rightEdgeResizer->setAlwaysOnTop(true);
    bottomEdgeResizer->setAlwaysOnTop(true);
    cornerResizer = std::make_unique<InvisibleResizableCornerComponent>(this);
    cornerResizer->setAlwaysOnTop(true);
    addAndMakeVisible(*cornerResizer);
    addAndMakeVisible(*leftEdgeResizer);
    addAndMakeVisible(*topEdgeResizer);
    addAndMakeVisible(*rightEdgeResizer);
    addAndMakeVisible(*bottomEdgeResizer);
    const auto minimumWidth = minimumEditorWidth();
    updateResizeLimits();
    const auto savedSize = audioProcessor.getLastEditorSize();
    setSize(juce::jlimit(minimumWidth, getConstrainer()->getMaximumWidth(),
                         savedSize.x > 0 ? savedSize.x : minimumWidth),
            juce::jlimit(minimumHeightForCurrentPage(), getConstrainer()->getMaximumHeight(),
                         savedSize.y > 0 ? savedSize.y : defaultEditorHeight));
    dockScreensetInitializationComplete = true;
    registerDockScreenset();
    // Fallback for hosts without REAPER's screenset API: restore the dock when
    // this editor is opened again, retaining its last position.
    if (bool(audioProcessor.getParameters().state.getProperty("ana.dock.only", false))
        && bool(audioProcessor.getParameters().state.getProperty("ana.settings.dock.visible", false)))
        showAnalyzerSettings(true);
}

PluginEditor::~PluginEditor()
{
    unregisterDockScreenset();
#if JUCE_MAC
    ana::svg_export::removeKeyboardShortcut(svgExportKeyboardMonitor);
#endif
    dismissAboutPopup();
    snapshotsContent->onTextEditingChanged = nullptr;
    snapshotsContent->onPreferredHeightChanged = nullptr;
    settingsWindow.reset();
    settingsComponent.detachSnapshotsContent();
    snapshotsContent.reset();
    animationSettingsPanel->onPreferredHeightChanged=nullptr;
    animationSettingsPanel->onTextEditingChanged=nullptr;
    settingsComponent.detachAnimationContent();
    animationSettingsPanel.reset();
    animationOverlay.reset();
    animationBackdrop.reset();
    updateButton.removeMouseListener(this);
    aboutButton.removeMouseListener(this);
    setConstrainer(nullptr);
    refreshButton.removeMouseListener(this);
    if (displayedStateRevision == audioProcessor.getStateRevision() && getWidth() > 0 && getHeight() > 0)
        audioProcessor.setLastEditorSize(getWidth(), getHeight());
}

void PluginEditor::registerDockScreenset()
{
    auto& state = audioProcessor.getParameters().state;
    auto id = state.getProperty("ana.settings.dock.screensetId").toString();
    std::string key = "ANA_DOCK_" + id.toStdString();
    if (dockScreensetRegistered)
    {
        if (dockScreensetId == key)
            return;
        // The host has restored a different project/FX instance state.
        unregisterDockScreenset();
    }
    if (id.isEmpty() || registeredAnaDockIds.contains(key))
    {
        id = juce::Uuid().toString().removeCharacters("-{}");
        key = "ANA_DOCK_" + id.toStdString();
        state.setProperty("ana.settings.dock.screensetId", id, nullptr);
    }

    dockScreensetId = std::move(key);
    dockScreensetRegistered = audioProcessor.registerDockScreenset(
        dockScreensetId.c_str(), &PluginEditor::dockScreensetCallback, this);
    if (dockScreensetRegistered)
        registeredAnaDockIds.insert(dockScreensetId);
}

void PluginEditor::unregisterDockScreenset()
{
    if (!dockScreensetRegistered)
        return;
    audioProcessor.unregisterDockScreenset(this);
    registeredAnaDockIds.erase(dockScreensetId);
    dockScreensetRegistered = false;
}

std::intptr_t PluginEditor::dockScreensetCallback(int action, const char*,
                                                  void* context, void* data, int size)
{
    if (context == nullptr)
        return 0;
    return static_cast<PluginEditor*>(context)->handleDockScreenset(action, data, size);
}

std::intptr_t PluginEditor::handleDockScreenset(int action, void* data, int size)
{
    // Actions defined by REAPER's reaper_plugin.h (screensetNewCallbackFunc).
    constexpr int getWindow = 0;
    constexpr int isDocked = 1;
    constexpr int saveState = 0x101;
    constexpr int loadState = 0x100;
    constexpr int wantSize = 0x102;
    if (action == wantSize)
        return 256;
    if (action == isDocked)
        return 0; // Floating JUCE window, not a REAPER docker tab.
    if (action == getWindow)
        return settingsWindow != nullptr && settingsWindow->isVisible()
            && settingsWindow->getPeer() != nullptr
            ? reinterpret_cast<std::intptr_t>(settingsWindow->getPeer()->getNativeHandle()) : 0;

    if (action == saveState)
    {
        const auto& state = audioProcessor.getParameters().state;
        const bool visible = showingAnalyzerSettings && settingsWindow != nullptr
            && settingsWindow->isVisible();
        const bool dockOnly = bool(state.getProperty("ana.dock.only", false));
        const bool vertical = bool(state.getProperty("ana.dock.vertical", false));
        const auto bounds = settingsWindow != nullptr ? settingsWindow->getBounds()
            : juce::Rectangle<int>(int(state.getProperty("ana.settings.dock.x", 0)),
                                   int(state.getProperty("ana.settings.dock.y", 0)), 0, 0);
        char output[160] {};
        const auto count = std::snprintf(output, sizeof(output), "1|%d|%d|%d|%d|%d|%d|%d",
            int(visible), int(dockOnly), int(vertical),
            bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight());
        if (data == nullptr || size < 1 || count < 0 || count >= size)
            return 0;
        std::memcpy(data, output, static_cast<size_t>(count) + 1);
        return count + 1;
    }
    if (action == loadState)
    {
        // REAPER may request this during a screenset change. Do not mutate
        // the editor hierarchy from inside the host callback.
        juce::String serialized;
        if (data != nullptr && size > 0)
        {
            const auto* bytes = static_cast<const char*>(data);
            int len = 0;
            while (len < size && bytes[len] != '\0') ++len;
            serialized = juce::String::fromUTF8(bytes, len);
        }
        juce::Component::SafePointer<PluginEditor> safeEditor(this);
        juce::MessageManager::callAsync([safeEditor, serialized]
        {
            if (safeEditor != nullptr)
                safeEditor->loadDockScreensetState(serialized);
        });
    }
    return 0;
}

void PluginEditor::loadDockScreensetState(juce::String serialized)
{
    if (serialized.isEmpty())
    {
        showAnalyzerSettings(false);
        return;
    }
    const auto fields = juce::StringArray::fromTokens(serialized, "|", "");
    if (fields.size() != 8 || fields[0] != "1")
        return;
    const bool visible = fields[1].getIntValue() != 0;
    if (!visible)
    {
        showAnalyzerSettings(false);
        return;
    }

    auto& state = audioProcessor.getParameters().state;
    state.setProperty("ana.dock.only", fields[2].getIntValue() != 0, nullptr);
    state.setProperty("ana.dock.vertical", fields[3].getIntValue() != 0, nullptr);
    showAnalyzerSettings(true);
    if (settingsWindow == nullptr)
        return;
    settingsWindow->applyDockMode();
    const auto savedBounds = juce::Rectangle<int>(fields[4].getIntValue(), fields[5].getIntValue(),
                                                  fields[6].getIntValue(), fields[7].getIntValue());
    // The current dock layout determines its size. The screenset restores its
    // position, but does not stretch or distort the dock's standard controls.
    auto bounds = settingsWindow->getBounds();
    bounds.setPosition(savedBounds.getPosition());
    settingsWindow->showAtScreensetBounds(*this, bounds);
}

void PluginEditor::showAnalyzerSettings(const bool shouldShowSettings)
{
    if (!shouldShowSettings) dismissAboutPopup();
    auto& state = audioProcessor.getParameters().state;
    const bool dockOnly = bool(state.getProperty("ana.dock.only", false));
    const bool rememberDockVisible = shouldShowSettings && dockOnly;
    if (bool(state.getProperty("ana.settings.dock.visible", false)) != rememberDockVisible)
        state.setProperty("ana.settings.dock.visible", rememberDockVisible, nullptr);
    if (shouldShowSettings && settingsWindow == nullptr)
    {
        settingsWindow = std::make_unique<SettingsWindow>(
            settingsComponent, [this] { showAnalyzerSettings(false); });
    }

    showAnalyzerPage(activePage, shouldShowSettings);
}

void PluginEditor::showAnalyzerPage(const ana::AnalyzerPage page,
                                               const bool shouldShowSettings)
{
    if (! shouldShowSettings || page != activePage)
        dismissChoicePrompt();

    const auto previousPage = activePage;
    activePage = page;
    audioProcessor.setAnalyzerPageState(page);
    // Switching DISPLAY must never force the host window taller. Keep the
    // compact constraint until the user starts resizing vertically.
    if (previousPage != page)
    {
        deferOfflineScopMinimum = page == ana::AnalyzerPage::scop
            && audioProcessor.isOfflineMode() && getConstrainer() != nullptr
            && getHeight() > 0 && getHeight() < requiredHeightForCurrentPage();
        heightAtScopSwitch = getHeight();
    }
    showingAnalyzerSettings = shouldShowSettings;
    updateButton.setEnabled(true);
    updateButton.setToggleState(showingAnalyzerSettings, juce::dontSendNotification);
    scopDisplay.setVisible(page == ana::AnalyzerPage::scop);
    specDisplay.setVisible(page == ana::AnalyzerPage::spec);
    corrDisplay.setVisible(page == ana::AnalyzerPage::corr);
    lvlsDisplay.setVisible(page == ana::AnalyzerPage::lvls);
    fullSourceButton.setEnabled(page == ana::AnalyzerPage::scop
        && audioProcessor.getScopSingleViewBand() < 0);
    freezeButton.setToggleState(page == ana::AnalyzerPage::spec
                                    ? audioProcessor.getSpecProcessor().isFrozen()
                                    : page == ana::AnalyzerPage::corr
                                        ? audioProcessor.getCorrProcessor().isFrozen()
                                    : page == ana::AnalyzerPage::lvls
                                        ? audioProcessor.getLvlsProcessor().isFrozen()
                                    : scopFrozen,
                                juce::dontSendNotification);
    const auto settingsContextChanged = settingsComponent.setAnalyzerContext(
        page, getActiveSettingsViewMode());
    if (settingsWindow != nullptr)
    {
        if (showingAnalyzerSettings)
        {
            if (settingsContextChanged)
                settingsWindow->refreshFor(*this);
            else
                settingsWindow->showFor(*this);
        }
        else
            settingsWindow->hideWindow();
    }

    if (getConstrainer() != nullptr)
    {
        updateResizeLimits();
        const auto minHeight = minimumHeightForCurrentPage();
        if (getHeight() < minHeight)
            setSize(getWidth(), minHeight);
    }
    resized();
    repaint();
}

juce::String PluginEditor::getActiveSettingsViewMode() const
{
    if (activePage == ana::AnalyzerPage::spec)
        return specDisplay.getSettingsViewModeName();
    if (activePage == ana::AnalyzerPage::corr)
        return corrDisplay.getSettingsViewModeName();
    return {};
}

void PluginEditor::showChoicePrompt(
    const juce::Rectangle<int> anchorBounds,
    juce::StringArray choices,
    const int selectedIndex,
    std::function<void(int)> onSelect,
    std::vector<bool> enabledChoices)
{
    if (choices.isEmpty())
        return;

    dismissChoicePrompt();
    choicePrompt = std::make_unique<ChoicePopup>(
        anchorBounds,
        std::move(choices),
        std::move(enabledChoices),
        selectedIndex,
        std::move(onSelect),
        [this] { dismissChoicePrompt(); });
    addAndMakeVisible(*choicePrompt);
    choicePrompt->setBounds(getLocalBounds());
    choicePrompt->toFront(true);
}



void PluginEditor::dismissChoicePrompt()
{
    choicePrompt.reset();
}

void PluginEditor::showAboutPopup()
{
    dismissChoicePrompt();
    dismissAboutPopup();
    aboutPopup = std::make_unique<AboutPopup>(
        [safeEditor = juce::Component::SafePointer<PluginEditor>(this)]
        {
            if (safeEditor != nullptr)
                safeEditor->dismissAboutPopup();
        });
    if (settingsWindow != nullptr) settingsWindow->showAboutPopup(*aboutPopup);
    aboutButton.setToggleState(true, juce::dontSendNotification);
    aboutPopup->grabKeyboardFocus();
}

void PluginEditor::showAnimation(const bool active)
{
    if (! active && (automaticPauseAnimation || animationPausedSince >= 0.0))
    {
        pauseAnimationDismissed = true;
        automaticPauseAnimation = false;
    }
    if (active) ensureAnimationComponents();
    if (audioProcessor.isScreensaverEnabled() != active)
        if (auto* parameter = audioProcessor.getParameters().getParameter(PluginProcessor::screensaverParameterId))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(active ? 1.0f : 0.0f);
            parameter->endChangeGesture();
        }
    if (animationOverlay != nullptr)
    {
        if (active)
        {
            animationBackdrop->setBounds(getLocalBounds().reduced(2));
            animationBackdrop->setVisible(true);
            animationBackdrop->toFront(false);
            // Paint the software backing before GL replaces the overlay's
            // normal paint with an initially empty CachedComponentImage.
            if (auto* peer = getPeer()) peer->performAnyPendingRepaintsNow();
        }
        animationOverlay->setBounds(getLocalBounds().reduced(2));
        animationOverlay->setActive(active);
        if (active)
        {
            dismissAboutPopup();
            dismissChoicePrompt();
            animationOverlay->toFront(false);
            updateButton.toFront(false);
        }
        else
        {
            animationBackdrop->setVisible(false);
            repaint();
            if (auto* peer = getPeer()) peer->performAnyPendingRepaintsNow();
        }
    }
}

void PluginEditor::ensureAnimationComponents()
{
    if (animationOverlay == nullptr)
    {
        animationBackdrop = std::make_unique<AnimationBackdrop>();
        addChildComponent(*animationBackdrop);
        animationOverlay = std::make_unique<AnimationOverlay>(audioProcessor.getParameters().state, animationSettings);
        animationOverlay->onSettingsRequested = [this] { showAnimationSettings(); };
        animationOverlay->onClose = [this] { showAnimation(false); };
        addChildComponent(*animationOverlay);
    }
}

void PluginEditor::showAnimationSettings()
{
    showAnalyzerSettings(true);
    audioProcessor.getParameters().state.setProperty("ana.dock.only",false,nullptr);
    if (settingsWindow!=nullptr) settingsWindow->applyDockMode();
    settingsComponent.openAnimationBlock();
}

void PluginEditor::dismissAboutPopup()
{
    aboutPopup.reset();
    aboutButton.setToggleState(false, juce::dontSendNotification);
    repaint();
}

void PluginEditor::syncDisplayedEditorSize()
{
    // A host can display the editor's native view fitted into an FX window of a
    // different aspect (for example while the window is being resized), which
    // stretches the drawn UI. A uniform host UI scale is deliberate and is
    // preserved; only a stretched view is corrected by adopting the size the
    // host is really showing.
    auto* peer = getPeer();
    if (peer == nullptr || ! isShowing() || svgLayoutActive
        || juce::Desktop::getInstance().getMainMouseSource().isDragging())
        return;

    window_geometry::resetViewContentsScale(*this);

    const auto peerBounds = peer->getBounds();
    auto displayed = juce::Point<int>(peerBounds.getWidth(), peerBounds.getHeight());
    if (displayed.getX() <= 0 || displayed.getY() <= 0)
        return;

    // The host window's content is the size the user really sees: when it is
    // smaller than our view, the host is fitting the editor into it.
    if (const auto content = window_geometry::getHostContentSize(*this);
        content.getX() > 0 && content.getY() > 0
        && content.getX() <= displayed.getX()
        && content.getY() <= displayed.getY())
        displayed = content;

    const auto scaleX = static_cast<double>(displayed.getX()) / juce::jmax(1, getWidth());
    const auto scaleY = static_cast<double>(displayed.getY()) / juce::jmax(1, getHeight());
    if (scaleX >= 1.0 && scaleY >= 1.0)
        return;

    if (juce::jmax(scaleX, scaleY) / juce::jmin(scaleX, scaleY) < 1.1)
        return;

    // Wait until the host has stopped resizing before adopting its size.
    if (displayed != lastDisplayedEditorSize)
    {
        lastDisplayedEditorSize = displayed;
        return;
    }

    setSize(displayed.getX(), displayed.getY());
    repaint();
}


void PluginEditor::timerCallback()
{
    if (dockScreensetInitializationComplete)
        registerDockScreenset();
    const auto stateChanged = displayedStateRevision != audioProcessor.getStateRevision();
    if (stateChanged) restoreProcessorState();
    else syncDisplayedEditorSize();
    loadAnimationSettings(*animationSettings, audioProcessor.getParameters().state);
    const auto animationActive = audioProcessor.isScreensaverEnabled();
    if (automaticPauseAnimation && !animationActive)
    {
        automaticPauseAnimation = false;
        pauseAnimationDismissed = true;
    }
    const auto pauseMode = animationSettings->playOnPause.load();
    const auto transport = audioProcessor.getHostTransportState();
    const auto paused = pauseMode && transport == 0;
    if (paused)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (animationPausedSince < 0.0) animationPausedSince = now;
        if (!pauseAnimationDismissed && !animationActive
            && now - animationPausedSince >= animationSettings->pauseDelayMs.load())
        {
            showAnimation(true);
            automaticPauseAnimation = true;
        }
    }
    else
    {
        animationPausedSince = -1.0;
        if (automaticPauseAnimation || (pauseMode && transport == 1 && animationActive))
            showAnimation(false);
        automaticPauseAnimation = false;
        pauseAnimationDismissed = false;
    }
    const auto shouldShowAnimation = audioProcessor.isScreensaverEnabled();
    if (shouldShowAnimation != (animationOverlay != nullptr && animationOverlay->isVisible()))
        showAnimation(shouldShowAnimation);
    const auto offline = audioProcessor.isOfflineMode();
    const auto modeAvailable = audioProcessor.isOfflineAvailable();
    if (displayedOffline != offline || displayedModeAvailable != modeAvailable)
    {
        if (displayedOffline && ! offline)
            audioProcessor.cancelOfflineAnalysis();
        displayedOffline = offline;
        displayedModeAvailable = modeAvailable;
        dismissChoicePrompt();
        specDisplay.refreshMode();
        corrDisplay.refreshMode();
        scopDisplay.refreshMode();
        settingsComponent.refreshExternalState();
        settingsComponent.resized();
        lvlsDisplay.resized();
        resized();
        repaint();
    }
    if (offline || showingAnalyzerSettings)
    {
        refreshOfflineSelectionButtons();
        refreshButton.setToggleState(audioProcessor.isOfflineAutomaticRefresh(), juce::dontSendNotification);
    }
    if (offline) refreshOfflineUpdateStatus();
    else updateButton.setButtonText("UD");
    // In realtime the offline action controls remain disabled. Polling their
    // source/track state every 66 ms takes the offline-selection mutex on the
    // GUI thread while the analysis worker may be updating it. Only refresh
    // continuously when offline actually needs the live track information.
    if (offline || stateChanged || !offlineActionControlsInitialized)
    {
        refreshOfflineActionControls();
        offlineActionControlsInitialized = true;
    }

    const auto clean = audioProcessor.isCleanView();
    if (cleanViewActive != clean)
    {
        cleanViewActive = clean;
        if (clean)
        {
            dismissChoicePrompt();
            dismissAboutPopup();
        }
        resized();
        repaint();
    }

    const auto restoredPage = audioProcessor.getAnalyzerPageState();
    if (restoredPage != activePage)
        showAnalyzerPage(restoredPage, showingAnalyzerSettings);
    if (auto* bounds = getConstrainer())
    {
        const auto minimum = minimumHeightForCurrentPage();
        if (bounds->getMinimumHeight() != minimum)
        {
            updateResizeLimits();
            if (getHeight() < minimum)
                setSize(getWidth(), minimum);
        }
    }

    fullSourceButton.setToggleState(audioProcessor.isScopFullSourceView(),
                                    juce::dontSendNotification);
    clearButton.setEnabled(!audioProcessor.isOfflineMode() && activePage == ana::AnalyzerPage::scop);
    freezeButton.setEnabled(!audioProcessor.isOfflineMode());
    fullSourceButton.setEnabled(activePage == ana::AnalyzerPage::scop
        && audioProcessor.getScopSingleViewBand() < 0);
    updateButton.setEnabled(true);
    if (settingsComponent.setAnalyzerContext(activePage, getActiveSettingsViewMode())
        && showingAnalyzerSettings && settingsWindow != nullptr)
        settingsWindow->refreshFor(*this);
    freezeButton.setToggleState(activePage == ana::AnalyzerPage::spec
                                    ? audioProcessor.getSpecProcessor().isFrozen()
                                    : activePage == ana::AnalyzerPage::corr
                                        ? audioProcessor.getCorrProcessor().isFrozen()
                                    : activePage == ana::AnalyzerPage::lvls
                                        ? audioProcessor.getLvlsProcessor().isFrozen()
                                    : scopFrozen,
                                juce::dontSendNotification);
    settingsComponent.setActionStates(freezeButton.getToggleState(), audioProcessor.isScopFullSourceView(),
                                      !audioProcessor.isOfflineMode(), audioProcessor.getScopSingleViewBand() < 0);

    if (editorSizeSavePending
        && juce::Time::getMillisecondCounterHiRes() >= editorSizeSaveDeadlineMilliseconds)
    {
        audioProcessor.setLastEditorSize(pendingEditorSize.x, pendingEditorSize.y);
        editorSizeSavePending = false;
    }
}

void PluginEditor::paintOverChildren(juce::Graphics& g)
{
    NumericValueBox::paintRangeIndicators(g,*this);
    g.setColour(ana::ui::background);
    g.drawRect(getLocalBounds(),2);
}

void PluginEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::windowBackground);
}

void PluginEditor::resized()
{
    if (deferOfflineScopMinimum && !svgLayoutActive && !restoringEditorState
        && getHeight() != heightAtScopSwitch)
    {
        // This is the first actual change of HEIGHT after selecting Offline
        // SCOP (width-only changes leave the deferred minimum untouched).
        deferOfflineScopMinimum = false;
        updateResizeLimits();
        const auto required = minimumHeightForCurrentPage();
        if (getHeight() < required)
        {
            setSize(getWidth(), required);
            return;
        }
    }
    if (!svgLayoutActive && !restoringEditorState
        && displayedStateRevision == audioProcessor.getStateRevision()
        && getWidth() > 0 && getHeight() > 0)
    {
        audioProcessor.setLastEditorSize(getWidth(), getHeight());
        pendingEditorSize = { getWidth(), getHeight() };
        editorSizeSaveDeadlineMilliseconds = juce::Time::getMillisecondCounterHiRes() + 250.0;
        editorSizeSavePending = true;
    }

    const auto layoutBounds = svgLayoutActive ? svgLayoutBounds : getLocalBounds();
    auto area = layoutBounds.reduced(ana::ui::gap.pixels());
    const bool showTake=audioProcessor.isOfflineMode() && !audioProcessor.isOfflineMonitorLocation();
    const int updateWidth=ana::ui::controlHeight;
    const int overlayWidth=showTake?takeButton.getPreferredWidth()+ana::ui::gap.pixels():0;
    for(auto* view:std::array<juce::Component*,4>{ &scopDisplay,&specDisplay,&corrDisplay,&lvlsDisplay })
    {
        view->getProperties().set("topRightOverlayWidth",overlayWidth);
        view->getProperties().set("topLeftOverlayWidth",updateWidth+2*ana::ui::gap.pixels());
        view->getProperties().set("topLeftOverlayBottom",2*ana::ui::gap.pixels()+updateWidth-(view==&scopDisplay?0:ana::ui::gap.pixels()));
    }
    for(auto* control:std::array<juce::Component*,3>{ &clearButton,&freezeButton,&fullSourceButton })
    { control->setVisible(false); control->setBounds({}); }
    updateButton.setVisible(true);
    updateButton.setBounds(layoutBounds.getX()+2*ana::ui::gap.pixels(),layoutBounds.getY()+2*ana::ui::gap.pixels(),updateWidth,updateWidth);
    takeButton.setVisible(showTake);
    takeButton.setBounds(showTake?juce::Rectangle<int>(area.getRight()-ana::scop::bandZoomSliderWidth-ana::ui::gap.pixels()-takeButton.getPreferredWidth(),updateButton.getY(),takeButton.getPreferredWidth(),ana::ui::controlHeight):juce::Rectangle<int>());
    auto scopBounds = area;
    scopBounds.setTop(scopBounds.getY() - ana::ui::gap.pixels());
    scopBounds.setBottom(getHeight());
    scopDisplay.setBounds(scopBounds);
    specDisplay.setBounds(area);
    corrDisplay.setBounds(area);
    lvlsDisplay.setBounds(area);
    scopDisplay.resized(); specDisplay.resized(); corrDisplay.resized(); lvlsDisplay.resized();
    updateButton.toFront(false); takeButton.toFront(false);

    if (cornerResizer != nullptr)
    {
        cornerResizer->setBounds(getWidth() - editorResizeHandleThickness, getHeight() - editorResizeHandleThickness,
                                editorResizeHandleThickness, editorResizeHandleThickness);
        cornerResizer->toFront(false);
    }
    if (leftEdgeResizer != nullptr)
    {
        leftEdgeResizer->setBounds(0, 0, editorResizeHandleThickness, getHeight());
        leftEdgeResizer->toFront(false);
    }
    if (topEdgeResizer != nullptr)
    {
        topEdgeResizer->setBounds(0, 0, getWidth(), editorResizeHandleThickness);
        topEdgeResizer->toFront(false);
    }
    if (rightEdgeResizer != nullptr)
    {
        rightEdgeResizer->setBounds(getWidth() - editorResizeHandleThickness, 0,
                                    editorResizeHandleThickness, getHeight());
        rightEdgeResizer->toFront(false);
    }
    if (bottomEdgeResizer != nullptr)
    {
        bottomEdgeResizer->setBounds(0, getHeight() - editorResizeHandleThickness,
                                     getWidth(), editorResizeHandleThickness);
        bottomEdgeResizer->toFront(false);
        if (cornerResizer != nullptr) cornerResizer->toFront(false);
    }

    if (choicePrompt != nullptr)
    {
        choicePrompt->setBounds(layoutBounds);
        choicePrompt->toFront(true);
    }
    if (animationOverlay != nullptr && !animationOverlay->isVisible()) animationOverlay->setBounds(layoutBounds.reduced(2));
    if (animationOverlay != nullptr && animationOverlay->isVisible())
    {
        animationBackdrop->setBounds(layoutBounds.reduced(2));
        animationBackdrop->toFront(false);
        animationOverlay->setBounds(layoutBounds.reduced(2));
        animationOverlay->toFront(false);
        for (auto* edge : { leftEdgeResizer.get(), topEdgeResizer.get(),
                            rightEdgeResizer.get(), bottomEdgeResizer.get(),
                            cornerResizer.get() })
            if (edge != nullptr) edge->toFront(false);
    }
}

void PluginEditor::showOfflineTakePrompt()
{
    refreshOfflineSelectionButtons();
    if (audioProcessor.isOfflineMonitorLocation())
        return; // MONITOR has KST, never the manual TAKE menu
    juce::StringArray names;
    std::vector<int> choiceNumbers;
    const auto selectedSourceId = audioProcessor.getSelectedOfflineSourceId();
    const auto selectedTakeNumber = audioProcessor.getSelectedOfflineTakeNumber();
    auto selectedIndex = -1;

    for (const auto& choice : offlineSourceChoices)
    {
        if (selectedSourceId.isNotEmpty() && choice.sourceId != selectedSourceId)
            continue;
        if (choice.takeNumber < 1 || choice.takeNumber > 2)
            continue;
        if (std::find(choiceNumbers.begin(), choiceNumbers.end(), choice.takeNumber) != choiceNumbers.end())
            continue;

        choiceNumbers.push_back(choice.takeNumber);
        names.add(juce::String(choice.takeNumber));

        if (choice.takeNumber == selectedTakeNumber)
            selectedIndex = names.size() - 1;
    }

    if (names.isEmpty())
        return;

    showChoicePrompt(getLocalArea(&takeButton, takeButton.getLocalBounds()),
                     std::move(names), selectedIndex,
                     [this, takeNumbers = std::move(choiceNumbers)] (const int index)
                     {
                         if (! juce::isPositiveAndBelow(index, static_cast<int>(takeNumbers.size())))
                             return;

                         audioProcessor.setSelectedOfflineTakeNumber(
                             takeNumbers[static_cast<size_t>(index)]);
                         refreshOfflineSelectionButtons();
                         scopDisplay.refreshWaveform();
                     });
}

void PluginEditor::refreshOfflineSelectionButtons()
{
    auto latestChoices = audioProcessor.getSourceChoices();
    audioProcessor.refreshOfflineSelection(latestChoices);
    const auto choicesChanged = latestChoices != offlineSourceChoices;
    offlineSourceChoices = std::move(latestChoices);
    if (choicesChanged && ! offlineSourceChoices.empty())
    {
        updateButton.setButtonText("00");
    }
    const auto sourceId = audioProcessor.getSelectedOfflineSourceId();
    const auto selectedTakeNumber = audioProcessor.getSelectedOfflineTakeNumber();
    std::vector<int> availableTakeNumbers;
    if (! offlineSourceChoices.empty())
    {
        for (const auto& choice : offlineSourceChoices)
        {
            if (sourceId.isNotEmpty() && choice.sourceId != sourceId)
                continue;
            if (choice.takeNumber < 1 || choice.takeNumber > 2)
                continue;

            if (std::find(availableTakeNumbers.begin(), availableTakeNumbers.end(),
                          choice.takeNumber) == availableTakeNumbers.end())
                availableTakeNumbers.push_back(choice.takeNumber);
        }

        std::sort(availableTakeNumbers.begin(), availableTakeNumbers.end());
    }

    const auto monitorLocation = audioProcessor.isOfflineMonitorLocation();
    takeButton.setButtonText("TAKE");
    const auto offlineAvailable = audioProcessor.isOfflineSourceAvailable();
    takeButton.setEnabled(offlineAvailable && ! monitorLocation && ! availableTakeNumbers.empty());
    takeButton.setToggleState(false, juce::dontSendNotification);
    takeButton.setTooltip("TRACK TAKE: " + juce::String(selectedTakeNumber));
    refreshOfflineActionControls();
}



bool PluginEditor::isSvgExportGesture(const juce::ModifierKeys& modifiers)
{
    return modifiers.isCtrlDown() && modifiers.isAltDown() && modifiers.isShiftDown()
#if JUCE_MAC
        && !modifiers.isCommandDown()
#endif
        ;
}

int PluginEditor::requiredHeightForCurrentPage() const noexcept
{
    if (activePage != ana::AnalyzerPage::scop || audioProcessor.isCleanView())
        return minimumEditorHeight;
    const size_t bandCount = (audioProcessor.isScopFullSourceView()
        || audioProcessor.getScopSingleViewBand() >= 0)
        ? 1u : audioProcessor.getCrossoverCount() + 1u;
    const auto bandHeight = audioProcessor.isOfflineMode()
        ? ana::scop::minimumBandHeightForControls(true,true)
        : ana::scop::minimumRealtimeBandHeight;
    return juce::jmax(minimumEditorHeight,
        ana::scop::minimumEditorHeightForBands(bandHeight, bandCount));
}

int PluginEditor::minimumHeightForCurrentPage() const noexcept
{
    return deferOfflineScopMinimum ? minimumEditorHeight : requiredHeightForCurrentPage();
}

void PluginEditor::updateResizeLimits()
{
    auto* constrainer = getConstrainer();
    if (constrainer == nullptr) return;
    const auto minHeight = minimumHeightForCurrentPage();
    const auto maximumWidth = squareResizeMode ? juce::jmax(minimumEditorWidth(), 1000) : maximumEditorSize;
    const auto maximumHeight = squareResizeMode ? juce::jmax(minHeight, 1000) : maximumEditorSize;
    constrainer->setFixedAspectRatio(0.0);
    constrainer->setSizeLimits(minimumEditorWidth(), minHeight, maximumWidth, maximumHeight);
    constrainer->setFixedAspectRatio(squareResizeMode ? 1.0 : 0.0);
}

void PluginEditor::toggleSquareResizeMode()
{
    if (getConstrainer() == nullptr) return;
    squareResizeMode = !squareResizeMode;
    audioProcessor.getParameters().state.setProperty("ui.square-resize", squareResizeMode, nullptr);
    updateResizeLimits();
    setSize(juce::jlimit(minimumEditorWidth(), getConstrainer()->getMaximumWidth(), getWidth()),
            juce::jlimit(minimumHeightForCurrentPage(), getConstrainer()->getMaximumHeight(), getHeight()));
    resized();
}

void PluginEditor::restoreProcessorState()
{
    const juce::ScopedValueSetter<bool> restoreGuard(restoringEditorState, true);
    displayedStateRevision = audioProcessor.getStateRevision();
    editorSizeSavePending = false;
    squareResizeMode = static_cast<bool>(audioProcessor.getParameters().state.getProperty("ui.square-resize", false));
    updateResizeLimits();
    const auto size = audioProcessor.getLastEditorSize();
    if (size.x > 0 && size.y > 0)
        setSize(juce::jlimit(minimumEditorWidth(), getConstrainer()->getMaximumWidth(), size.x),
                juce::jlimit(minimumHeightForCurrentPage(), getConstrainer()->getMaximumHeight(), size.y));
    specDisplay.refreshSnapshots(true);
    specDisplay.refreshMode();
    corrDisplay.refreshMode();
    scopDisplay.refreshMode();
    settingsComponent.refreshExternalState();
    resized();
    repaint();
}

bool PluginEditor::keyPressed(const juce::KeyPress& key)
{
    if (isSvgExportGesture(key.getModifiers()) && (key.isKeyCode('R') || key.isKeyCode('r')))
    {
        toggleSquareResizeMode();
        return true;
    }
    if (isSvgExportGesture(key.getModifiers())
        && (key.isKeyCode('E') || key.isKeyCode('e')))
    {
        ana::svg_export::chooseFile(*this);
        return true;
    }
    return juce::AudioProcessorEditor::keyPressed(key);
}

void PluginEditor::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent == &aboutButton && event.mods.isPopupMenu())
        showAnimation(true);
    else if (event.originalComponent == &refreshButton && refreshButton.isInteractionEnabled() && event.mods.isLeftButtonDown())
        refreshPress.begin();
    else if(event.originalComponent==&updateButton && event.mods.isLeftButtonDown())
    {
        udDragPressed=true; udDragMoved=false;
        udDragStart=event.source.getScreenPosition().roundToInt();
    }
    else
        juce::AudioProcessorEditor::mouseDown(event);
}

void PluginEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (event.originalComponent == &refreshButton)
    {
        if (event.getDistanceFromDragStart() >= 5)
            refreshPress.markDragged();
    }
    else if(event.originalComponent==&updateButton && udDragPressed)
    {
        if(!udDragMoved && event.getDistanceFromDragStart()>=4)
        { udDragMoved=true; windowDragHandle.beginDragAt(udDragStart); }
        if(windowDragHandle.isDragging()) windowDragHandle.dragTo(event.source.getScreenPosition().roundToInt());
    }
    else if (windowDragHandle.isDragging())
    {
        if (event.mods.isLeftButtonDown())
            windowDragHandle.dragTo(event.source.getScreenPosition().roundToInt());
        else
            windowDragHandle.endDragAt(event.source.getScreenPosition().roundToInt());
    }
    else
        juce::AudioProcessorEditor::mouseDrag(event);
}

void PluginEditor::mouseUp(const juce::MouseEvent& event)
{
    if (windowDragHandle.isDragging())
        windowDragHandle.endDragAt(event.source.getScreenPosition().roundToInt());
    else
        juce::AudioProcessorEditor::mouseUp(event);
    udDragPressed=false; udDragMoved=false;
}

void PluginEditor::refreshOfflineUpdateStatus()
{
    const auto progress = audioProcessor.getOfflineAnalysisProgress();
    updateButton.setButtonText(progress < 0 ? juce::String("ERR")
                                           : progress >= 100 ? juce::String("UD")
                                          : juce::String(progress).paddedLeft('0', 2));
}



void PluginEditor::refreshOfflineActionControls()
{
    settingsComponent.setTrackInfo(audioProcessor.getOfflineTrackNumber(), audioProcessor.getOfflineTrackName());
    const auto monitorLocation = audioProcessor.isOfflineMonitorLocation();
    const auto locationText = juce::String("MONITOR");
    if (locationButton.getButtonText() != locationText)
        locationButton.setButtonText(locationText);
    locationButton.setToggleState(monitorLocation, juce::dontSendNotification);
    locationButton.setInteractionEnabled(audioProcessor.isOfflineMode() && audioProcessor.isOfflineLocationAvailable());
    locationButton.setTooltip(monitorLocation ? "LOCATION: MONITOR (SWITCH TO TRACK)" : "LOCATION: TRACK (SWITCH TO MONITOR)");
    keepSecondTakeButton.setToggleState(audioProcessor.isOfflineKeepSecondTake(), juce::dontSendNotification);
    keepSecondTakeButton.setInteractionEnabled(audioProcessor.isOfflineMode() && audioProcessor.isOfflineSourceAvailable() && monitorLocation);
    refreshButton.setInteractionEnabled(audioProcessor.isOfflineMode());
    keepSecondTakeButton.setTooltip("MONITOR: USE TAKE 2 WHERE PRESENT, OTHERWISE TAKE 1");
    refreshButton.setToggleState(audioProcessor.isOfflineAutomaticRefresh(), juce::dontSendNotification);
}

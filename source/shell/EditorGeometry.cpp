#include "Editor.h"
#include "EditorSizeLimits.h"
#include "Processor.h"
#include "shared/shell/Theme.h"
#include "shared/shell/NumericValueBox.h"
#include "shared/scop/Layout.h"

#include <algorithm>
#include <array>

using ana::editor_detail::minimumEditorHeight;
using ana::editor_detail::maximumEditorSize;
using ana::editor_detail::minimumEditorWidth;
using ana::editor_detail::editorResizeHandleThickness;

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

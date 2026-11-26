#include "Controls.h"
#include "Theme.h"
#include "ValueRange.h"
#include "SquareControl.h"
#include "PopupFrame.h"
#include "PointerInput.h"

#include <algorithm>
#include <cmath>

CopyPasteTextEditor::CopyPasteTextEditor()
{
    setInputFilter(new UppercaseEditorInputFilter(), true);
    setLookAndFeel(&editorLookAndFeel);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    setIndents(2, 0);
    setBorder(juce::BorderSize<int>(1));
    setColour(juce::CaretComponent::caretColourId, ana::ui::white);
    setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::highlightedTextColourId, ana::ui::black);
}

void CopyPasteTextEditor::paint(juce::Graphics& g)
{
    juce::TextEditor::paint(g);
    if(bool(getProperties().getWithDefault("squareValueEditor",false)))
    {
        const auto selection=getHighlightedRegion();
        if(selection.isEmpty()) return;
        const juce::Graphics::ScopedSaveState saved(g);
        auto clip=getLocalBounds();
        for(auto* child:getChildren())
            if(auto* viewport=dynamic_cast<juce::Viewport*>(child)) clip=getLocalArea(viewport,viewport->getLocalBounds());
        g.reduceClipRegion(clip);
        g.setColour(ana::ui::background);
        for(const auto& bounds:getTextBounds(selection)) g.fillRect(bounds);
        return;
    }
    if (!findColour(juce::TextEditor::highlightColourId).isTransparent())
        return; // JUCE already paints the configured selection.

    const auto selection=getHighlightedRegion();
    if(selection.isEmpty()) return;
    g.setColour(ana::ui::light.withMultipliedAlpha(hasKeyboardFocus(true)?1.0f:0.5f));
    g.reduceClipRegion(getLocalBounds().reduced(1));
    for(const auto& bounds:getTextBounds(selection))
        g.fillRect(bounds.reduced(0,juce::jmin(2,bounds.getHeight()/2)).withTrimmedTop(1));
}

CopyPasteTextEditor::~CopyPasteTextEditor()
{
    contextMenu.reset();
    setLookAndFeel(nullptr);
}

juce::CaretComponent* CopyPasteTextEditor::EditorLookAndFeel::createCaretComponent(juce::Component* owner)
{
    auto* caret = new juce::CaretComponent(owner);
    caret->setColour(juce::CaretComponent::caretColourId, owner->findColour(juce::CaretComponent::caretColourId));
    return caret;
}

void CopyPasteTextEditor::EditorLookAndFeel::drawTextEditorOutline(juce::Graphics& g,int width,int height,juce::TextEditor& editor)
{
    const auto focused=editor.hasKeyboardFocus(true) && !editor.isReadOnly();
    const auto colour=editor.findColour(focused?juce::TextEditor::focusedOutlineColourId:juce::TextEditor::outlineColourId);
    ControlAppearance style(false,0.0f,focused && !colour.isTransparent());
    style.normalFrameVisible=!colour.isTransparent();style.normalBorder=colour;
    style.paintFrame(g,juce::Rectangle<int>(0,0,width,height));
}

void EllipsisLabel::paintOverChildren(juce::Graphics& g)
{
    ControlAppearance style(false, 0.0f, isBeingEdited());
    style.normalFrameVisible=false;
    style.paintFrame(g,getLocalBounds());
}

void CopyPasteTextEditor::mouseDown(const juce::MouseEvent& event)
{
    if(event.mods.isCtrlDown() && !event.mods.isRightButtonDown()) return;
    if (!isPopupMenuEnabled() || !event.mods.isPopupMenu() || getParentComponent() == nullptr)
    {
        juce::TextEditor::mouseDown(event);
        return;
    }
    auto* scope = getTopLevelComponent();
    contextMenu.reset();
    const auto selection = getHighlightedRegion();
    auto anchor = juce::Rectangle<int>(std::max(96, getWidth()), ana::ui::controlHeight);
    anchor.setCentre(scope->getLocalPoint(this, event.getPosition()));
    auto popup = std::make_unique<ChoicePopup>(anchor, juce::StringArray { "COPY", "PASTE" },
        std::vector<bool> { !selection.isEmpty(), !isReadOnly() }, -1,
        [safeThis = juce::Component::SafePointer<CopyPasteTextEditor>(this), selection](int choice)
        {
            if (safeThis == nullptr) return;
            safeThis->setHighlightedRegion(selection);
            safeThis->performPopupMenuAction(choice == 0 ? juce::StandardApplicationCommandIDs::copy
                                                        : juce::StandardApplicationCommandIDs::paste);
            if (safeThis->isShowing()) safeThis->grabKeyboardFocus();
        },
        [safeThis = juce::Component::SafePointer<CopyPasteTextEditor>(this)]
        {
            if (safeThis == nullptr) return;
            safeThis->contextMenu.reset();
            if (safeThis->isShowing()) safeThis->grabKeyboardFocus();
        });
    popup->setName("text-edit-context-menu");
    scope->addAndMakeVisible(*popup);
    popup->setBounds(scope->getLocalBounds());
    popup->toFront(false);
    auto* menu = popup.get();
    contextMenu = std::move(popup);
    // ANA's numeric editors and editable labels are modal. The menu must be
    // above them in the modal stack so its actions remain clickable.
    menu->enterModalState(false, nullptr, false);
}

void CopyPasteTextEditor::addPopupMenuItems(juce::PopupMenu& menu, const juce::MouseEvent*)
{
    menu.addItem(juce::StandardApplicationCommandIDs::copy, "COPY", !getHighlightedRegion().isEmpty());
    menu.addItem(juce::StandardApplicationCommandIDs::paste, "PASTE", !isReadOnly());
}

void CopyPasteTextEditor::performPopupMenuAction(int id)
{
    if (id == juce::StandardApplicationCommandIDs::copy) copyToClipboard();
    else if (id == juce::StandardApplicationCommandIDs::paste) pasteFromClipboard();
}

ExplicitCommitTextEditor::ExplicitCommitTextEditor()
{
    setColour(juce::CaretComponent::caretColourId, ana::ui::white);
}

void ExplicitCommitTextEditor::inputAttemptWhenModal()
{
    auto* clicked=ana::ui::componentAtPointer(*this);
    if (!ana::ui::isEmptyEditorBackground(clicked)) { grabKeyboardFocus(); return; }
    juce::Component::SafePointer<ExplicitCommitTextEditor> safeThis(this);
    const auto commit=onReturnKey;
    if (!getText().trim().isEmpty() && commit) commit();
    if (safeThis!=nullptr && isCurrentlyModal(false))
    {
        const auto cancel=onEscapeKey;
        if (cancel) cancel();
    }
}

void EllipsisLabel::paint(juce::Graphics& graphics)
{
    if (isBeingEdited())
    {
        getLookAndFeel().drawLabel(graphics, *this);
        return;
    }

    if (hideWhenEmpty && getText().isEmpty())
        return;

    const auto hoverAmount = hoverEnabled
        ? hover.amount(isEnabled() && isEditable() && ana::ui::isMouseHovering(*this))
        : 0.0f;
    ControlAppearance style(false, hoverAmount);
    if (drawBackground)
    {
        graphics.fillAll(findColour(juce::Label::backgroundColourId));
        if (hoverAmount > 0.0f)
            style.paintBackground(graphics, getLocalBounds());
    }
    else if (hoverAmount > 0.0f)
        style.paintBackground(graphics, getLocalBounds());
    graphics.setColour(isEnabled() ? (hoverAmount > 0.0f ? style.contentColour()
                                                       : findColour(juce::Label::textColourId))
                                   : ana::ui::light);
    graphics.setFont(getFont());
    const auto bounds = getBorderSize().subtractedFrom(getLocalBounds()).translated(0, textVerticalOffset);
    if (ana::ui::drawLoopingText(graphics, uppercaseText(getText()), bounds, getFont(), getJustificationType()))
        ana::ui::scheduleMarqueeRepaint(*this, marqueePending);
    graphics.setColour(isEnabled() ? findColour(juce::Label::outlineColourId)
                                   : ana::ui::light);
    graphics.drawRect(getLocalBounds());
}

void EllipsisLabel::resized()
{
    juce::Label::resized();
    alignEditorToPaintedText();
}

void EllipsisLabel::alignEditorToPaintedText()
{
    auto* editor = getCurrentTextEditor();

    if (editor == nullptr)
        return;

    // Painted label text uses the border-inset bounds; JUCE centres the
    // editor's own line box, so move the editor until both texts coincide.
    const auto paintedText = getBorderSize().subtractedFrom(getLocalBounds());
    ana::ui::alignInlineEditorText(*editor, getLocalBounds(),
        paintedText.getCentreY() + textVerticalOffset);
}

juce::TextEditor* EllipsisLabel::createEditorComponent()
{
    auto* editor = new CopyPasteTextEditor();
    editor->setName(getName());
    editor->applyFontToAllText(getLookAndFeel().getLabelFont(*this));
    copyAllExplicitColoursTo(*editor);
    const auto copyEditingColour = [&](int from, int to)
    {
        if (isColourSpecified(from)) editor->setColour(to, findColour(from));
    };
    copyEditingColour(textWhenEditingColourId, juce::TextEditor::textColourId);
    copyEditingColour(backgroundWhenEditingColourId, juce::TextEditor::backgroundColourId);
    copyEditingColour(outlineWhenEditingColourId, juce::TextEditor::focusedOutlineColourId);
    return editor;
}

void EllipsisLabel::editorShown(juce::TextEditor* editor)
{
    if (editor == nullptr)
        return;

    editor->setText(uppercaseText(editor->getText()), false);
    const auto border = getBorderSize();
    editor->setBorder(juce::BorderSize<int>(border.getTop(), border.getLeft(),
                                            border.getBottom(), std::max(0, border.getRight() - 2)));
    editor->setIndents(border.getRight() >= 2 ? 0 : 2, 0);
    editor->setJustification(getJustificationType());
    editor->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);

    // Auxiliary windows pass shortcuts to the host except while inline text editing is active.
    if (onEditorVisibilityChanged)
        onEditorVisibilityChanged(true);

    alignEditorToPaintedText();
}

void EllipsisLabel::inputAttemptWhenModal()
{
    auto* clicked=ana::ui::componentAtPointer(*this);
    if (ana::ui::isEmptyEditorBackground(clicked)) { hideEditor(false); return; }
    if (auto* editor=getCurrentTextEditor()) editor->grabKeyboardFocus();
}

void EllipsisLabel::editorAboutToBeHidden(juce::TextEditor*)
{
    hover.inherit(false);
    // Re-enable host shortcut passthrough after Label finishes TextEditor teardown.
    juce::Component::SafePointer<EllipsisLabel> safeLabel(this);
    juce::MessageManager::callAsync([safeLabel]
    {
        if (safeLabel != nullptr && ! safeLabel->isBeingEdited()
            && safeLabel->onEditorVisibilityChanged)
            safeLabel->onEditorVisibilityChanged(false);
    });
}

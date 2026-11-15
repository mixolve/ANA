#include "SvgExport.h"
#include "ParameterControl.h"
#include "NumericValueBox.h"
#include <algorithm>
#include <unordered_set>
#include <vector>
#include <unordered_map>

namespace ana::svg_export
{
namespace
{
juce::String number(float value) { return juce::String(value, 5); }
juce::String parameterIdFor(juce::Component& component)
{
    const auto property=component.getProperties()["oscParameterId"].toString();
    if (property.isNotEmpty()) return property;
    if (auto* control=dynamic_cast<ParameterControl*>(&component)) return control->getParameterId();
    return {};
}
juce::String sanitizeSvgId(juce::String text)
{
    text=text.toLowerCase().replaceCharacters(" _./", "----");
    text=text.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-");
    while (text.contains("--")) text=text.replace("--","-");
    while (text.startsWithChar('-')) text=text.substring(1);
    while (text.endsWithChar('-')) text=text.dropLastCharacters(1);
    if (text.isEmpty()||juce::CharacterFunctions::isDigit(text[0])) text="p-"+text;
    return text;
}
bool isParameterRow(juce::Component& component)
{
    if (dynamic_cast<ParameterControl*>(&component) != nullptr) return true;
    int labels = 0, values = 0;
    for (auto* child : component.getChildren())
    {
        if (dynamic_cast<juce::Button*>(child) != nullptr) ++labels;
        if (dynamic_cast<NumericValueBox*>(child) != nullptr) ++values;
    }
    return labels == 1 && values == 1;
}
juce::String rowTitle(juce::Component& component)
{
    for (auto* child : component.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
            return button->getButtonText();
    return {};
}
juce::String matrix(const juce::AffineTransform& t)
{
    return "matrix(" + number(t.mat00) + " " + number(t.mat10) + " "
        + number(t.mat01) + " " + number(t.mat11) + " " + number(t.mat02) + " " + number(t.mat12) + ")";
}
juce::String pathData(const juce::Path& path)
{
    juce::String result;
    for (juce::Path::Iterator it(path); it.next();)
    {
        const auto point = [](float x, float y) { return number(x) + " " + number(y) + " "; };
        switch (it.elementType)
        {
            case juce::Path::Iterator::startNewSubPath: result += "M" + point(it.x1,it.y1); break;
            case juce::Path::Iterator::lineTo: result += "L" + point(it.x1,it.y1); break;
            case juce::Path::Iterator::quadraticTo: result += "Q" + point(it.x1,it.y1) + point(it.x2,it.y2); break;
            case juce::Path::Iterator::cubicTo: result += "C" + point(it.x1,it.y1) + point(it.x2,it.y2) + point(it.x3,it.y3); break;
            case juce::Path::Iterator::closePath: result += "Z "; break;
        }
    }
    return result;
}
bool isRectanglePath(const juce::Path& path)
{
    const auto bounds = path.getBounds();
    if (bounds.isEmpty()) return false;
    int starts = 0, corners = 0;
    juce::Point<float> first, previous;
    for (juce::Path::Iterator it(path); it.next();)
    {
        if (it.elementType == juce::Path::Iterator::closePath)
        {
            if (std::abs(previous.x-first.x) > 0.0001f && std::abs(previous.y-first.y) > 0.0001f) return false;
            continue;
        }
        if (it.elementType == juce::Path::Iterator::startNewSubPath) { ++starts; first = {it.x1,it.y1}; }
        else if (it.elementType != juce::Path::Iterator::lineTo) return false;
        else if (std::abs(previous.x-it.x1) > 0.0001f && std::abs(previous.y-it.y1) > 0.0001f) return false;
        previous = {it.x1,it.y1};
        const auto left = std::abs(it.x1 - bounds.getX()) < 0.0001f;
        const auto right = std::abs(it.x1 - bounds.getRight()) < 0.0001f;
        const auto top = std::abs(it.y1 - bounds.getY()) < 0.0001f;
        const auto bottom = std::abs(it.y1 - bounds.getBottom()) < 0.0001f;
        if ((!left && !right) || (!top && !bottom)) return false;
        corners |= 1 << ((right ? 1 : 0) + (bottom ? 2 : 0));
    }
    return starts == 1 && corners == 15;
}

// Clip filled polygons themselves instead of exporting a clipping group for every paint call.
bool clipPolygons(const juce::Path& source, juce::Rectangle<float> clip, juce::Path& result)
{
    std::vector<juce::Point<float>> polygon;
    result.setUsingNonZeroWinding(source.isUsingNonZeroWinding());
    const auto flush = [&]
    {
        for (int edge = 0; edge < 4 && !polygon.empty(); ++edge)
        {
            const auto coordinate = [edge](juce::Point<float> p) { return edge < 2 ? p.x : p.y; };
            const auto boundary = edge == 0 ? clip.getX() : edge == 1 ? clip.getRight()
                : edge == 2 ? clip.getY() : clip.getBottom();
            const auto inside = [&](juce::Point<float> p)
            { return edge % 2 == 0 ? coordinate(p) >= boundary : coordinate(p) <= boundary; };
            std::vector<juce::Point<float>> output;
            auto previous = polygon.back();
            for (const auto current : polygon)
            {
                if (inside(previous) != inside(current))
                {
                    const auto ratio = (boundary - coordinate(previous)) / (coordinate(current) - coordinate(previous));
                    output.push_back(previous + (current - previous) * ratio);
                }
                if (inside(current)) output.push_back(current);
                previous = current;
            }
            polygon = std::move(output);
        }
        float area = 0.0f;
        for (size_t i = 0; i < polygon.size(); ++i)
        {
            const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            area += a.x * b.y - b.x * a.y;
        }
        if (polygon.size() >= 3 && std::abs(area) > 0.0001f)
        {
            result.startNewSubPath(polygon.front());
            for (size_t i = 1; i < polygon.size(); ++i) result.lineTo(polygon[i]);
            result.closeSubPath();
        }
        polygon.clear();
    };
    for (juce::Path::Iterator it(source); it.next();)
    {
        switch (it.elementType)
        {
            case juce::Path::Iterator::startNewSubPath: flush(); polygon.push_back({it.x1, it.y1}); break;
            case juce::Path::Iterator::lineTo: polygon.push_back({it.x1, it.y1}); break;
            case juce::Path::Iterator::closePath: flush(); break;
            case juce::Path::Iterator::quadraticTo:
            case juce::Path::Iterator::cubicTo: return false;
        }
    }
    flush();
    return true;
}

class Context final : public juce::LowLevelGraphicsContext
{
public:
    Context(juce::Component& rootIn, juce::Rectangle<int> bounds)
        : root(rootIn), canvasBounds(bounds.toFloat()) { state.clipBounds = canvasBounds; }
    juce::String defs, content;
    // Skip layout wrappers so Illustrator exposes controls as flat selectable objects.
    static bool isStructuralIdentifier(const juce::String& identifier)
    {
        return identifier.isEmpty()
            || identifier == "editor"
            || identifier == "viewport"
            || identifier == "viewport-content"
            || identifier.startsWith("module-")
            || identifier.startsWith("page-")
            || identifier.startsWith("group-");
    }
    bool paintComponent(juce::Component& component, juce::Graphics& graphics,
                        const juce::String& parentParameterId = {})
    {
        const auto contentStart=content.length();
        const auto defsStart=defs.length();
        const auto clipsStart=rectClipCache.size();
        const auto parameterId = parameterIdFor(component);
        const auto* parameterControl = dynamic_cast<ParameterControl*>(&component);
        const auto* parentControl = dynamic_cast<ParameterControl*>(component.getParentComponent());
        const auto compactValue = parameterControl != nullptr && parameterControl->getTitleBounds().isEmpty();
        const auto inlineValue = parentControl != nullptr && parentControl->getTitleBounds().isEmpty();
        const auto row = isParameterRow(component) && !compactValue;
        const auto parentIsRow = component.getParentComponent() != nullptr && isParameterRow(*component.getParentComponent());
        const auto value = dynamic_cast<NumericValueBox*>(&component) != nullptr
            || dynamic_cast<juce::ComboBox*>(&component) != nullptr;
        const auto comboLabel = dynamic_cast<juce::Label*>(&component) != nullptr
            && dynamic_cast<juce::ComboBox*>(component.getParentComponent()) != nullptr;
        auto parameterContext = parameterId.isNotEmpty() ? parameterId : parentParameterId;
        if (row && parameterContext.isEmpty())
        {
            parameterContext = component.getComponentID();
            if (parameterContext.isEmpty()) parameterContext = rowTitle(component);
        }
        juce::String identifier = parameterId;
        juce::String role;
        const auto explicitRole = component.getProperties()["svgExportRole"].toString();
        if (explicitRole.isNotEmpty())
        {
            role = explicitRole;
            identifier = component.getComponentID();
        }
        else if (compactValue) { identifier = parameterContext + "-value"; role = "value"; }
        else if (row) { identifier = parameterContext; role = "control"; }
        else if (value && !inlineValue)
        {
            identifier = parameterContext.isNotEmpty() ? parameterContext + "-value" : component.getComponentID();
            if (identifier.isEmpty()) identifier = "value";
            role = "value";
        }
        else if (auto* button = dynamic_cast<juce::Button*>(&component))
        {
            role = parentIsRow && parameterId.isEmpty() ? "label" : "button";
            if (role == "label") identifier = parameterContext + "-label";
            if (identifier.isEmpty()) identifier = component.getName();
            if (identifier.isEmpty()) identifier = component.getComponentID();
            if (identifier.isEmpty()) identifier = button->getButtonText();
            if (identifier.isEmpty()) identifier = "button";
        }
        else if (auto* label = dynamic_cast<juce::Label*>(&component); label != nullptr && !comboLabel)
        {
            role = "label";
            identifier = component.getName().isNotEmpty() ? component.getName() : label->getText();
        }
        else if (!comboLabel && parentParameterId.isEmpty())
        {
            identifier = component.getName();
            if (identifier.isEmpty()) identifier = component.getComponentID();
        }
        if (auto* slider = dynamic_cast<RangeSlider*>(&component); slider != nullptr && role.isEmpty())
        {
            identifier = component.getComponentID();
            if (identifier.isEmpty()) identifier = component.getName();
            if (identifier.isEmpty()) identifier = "range";
            role = "control";
        }
        if (dynamic_cast<juce::Slider*>(&component) != nullptr && role.isEmpty())
        {
            if (identifier.isEmpty()) identifier = "slider";
            role = "control";
        }
        if (identifier.isEmpty() && role.isNotEmpty()) identifier = role;
        const auto generic = role.isEmpty();
        const auto structuralContainer = comboLabel || inlineValue || identifier.isEmpty()
            || (generic && isStructuralIdentifier(identifier));
        juce::XmlElement group("g");
        if (!structuralContainer && !generic)
        {
            group.setAttribute("id", uniqueId(sanitizeSvgId(identifier)));
            group.setAttribute("data-role", generic ? "artwork" : role);
            if (parameterId.isNotEmpty()) group.setAttribute("data-parameter", parameterId);
            if (row) parameterContext = group.getStringAttribute("id");
        }
        auto opening = structuralContainer || generic ? juce::String()
            : group.toString(juce::XmlElement::TextFormat().withoutHeader()).replace("/>", ">");
        content += opening;
        {
            const juce::Graphics::ScopedSaveState saved(graphics);
            // ParameterControl paints its title itself; its value is a child component.
            auto* parameter = dynamic_cast<ParameterControl*>(&component);
            const auto title = parameter != nullptr && !parameter->getTitleBounds().isEmpty();
            const auto titleStart = content.length();
            if (title)
            {
                juce::XmlElement label("g");
                label.setAttribute("id", uniqueId(parameterContext + "-label"));
                label.setAttribute("data-role", "label");
                content += label.toString(juce::XmlElement::TextFormat().withoutHeader()).replace("/>", ">");
            }
            const auto paintStart = content.length();
            component.paint(graphics);
            if (title)
            {
                if (content.length() > paintStart) content += "</g>\n";
                else content = content.substring(0, titleStart);
            }
        }
        const auto groupArtwork = generic && parentParameterId.isEmpty();
        const auto artworkId = &component == &root ? juce::String("base")
            : isStructuralIdentifier(identifier) ? juce::String("background") : identifier;
        if (groupArtwork) groupPaint(contentStart, artworkId);
        for (int i=0;i<component.getNumChildComponents();++i)
        {
            auto* child=component.getChildComponent(i);
            if (!child->isVisible() || child->getAlpha()<=0.f) continue;
            const auto childContentStart=content.length();
            const auto childDefsStart=defs.length();
            const auto childClipsStart=rectClipCache.size();
            const juce::Graphics::ScopedSaveState saved(graphics);
            graphics.addTransform(juce::AffineTransform::translation(
                static_cast<float>(child->getX()),static_cast<float>(child->getY()))
                .followedBy(child->getTransform()));
            if (!child->isPaintingUnclipped() && !graphics.reduceClipRegion(child->getLocalBounds()))
            {
                defs=defs.substring(0,childDefsStart);
                rollbackRectClips(childClipsStart);
                continue;
            }
            const auto transparent=child->getAlpha()<1.f;
            if (transparent) graphics.beginTransparencyLayer(child->getAlpha());
            const auto painted=paintComponent(*child,graphics,parameterContext);
            if (transparent) graphics.endTransparencyLayer();
            if (!painted)
            {
                content=content.substring(0,childContentStart);
                defs=defs.substring(0,childDefsStart);
                rollbackRectClips(childClipsStart);
            }
        }
        {
            const juce::Graphics::ScopedSaveState saved(graphics);
            const auto start = content.length();
            component.paintOverChildren(graphics);
            if (groupArtwork) groupPaint(start, artworkId + "-overlay");
        }
        if (auto* field = dynamic_cast<NumericValueBox*>(&component))
        {
            const juce::Graphics::ScopedSaveState saved(graphics);
            state.clipBounds = canvasBounds;
            state.clips.clear();
            state.vectorMask.clear();
            state.onlyRectClips = true;
            state.hasRectClip = true;
            for (auto* parent = field->getParentComponent(); parent != nullptr && parent != &root;
                 parent = parent->getParentComponent())
                if (dynamic_cast<juce::Viewport*>(parent) != nullptr)
                {
                    const auto bounds = root.getLocalArea(parent, parent->getLocalBounds());
                    state.clipBounds = state.clipBounds.getIntersection(bounds.withTop(bounds.getY() - 4).toFloat());
                }
            field->paintRangeIndicator(graphics);
        }
        if (content.length()==contentStart+opening.length())
        {
            content=content.substring(0,contentStart);
            defs=defs.substring(0,defsStart);
            rollbackRectClips(clipsStart);
            return false;
        }
        if (opening.isNotEmpty()) content += "</g>\n";
        return true;
    }
    bool isVectorDevice() const override { return true; }
    void setOrigin(juce::Point<int> p) override { addTransform(juce::AffineTransform::translation(static_cast<float>(p.x),static_cast<float>(p.y))); }
    void addTransform(const juce::AffineTransform& t) override { state.transform = t.followedBy(state.transform); }
    float getPhysicalPixelScaleFactor() const override { return 1.f; }
    void pushClipRectangle(const juce::Rectangle<float>& rect)
    {
        // JUCE reads clipBounds while painting, including fillAll(). Accumulate rectangle
        // intersections here so emit() avoids deep SVG clip groups; path/mask clips still nest.
        state.clipBounds=state.clipBounds.getIntersection(rect);
        state.hasRectClip=true;
    }
    bool clipToRectangle(const juce::Rectangle<int>& r) override
    { pushClipRectangle(r.toFloat().transformedBy(state.transform)); return !isClipEmpty(); }
    bool clipToRectangleList(const juce::RectangleList<int>& list) override
    {
        if(list.getNumRectangles()==1)
        {
            pushClipRectangle(list.getRectangle(0).toFloat().transformedBy(state.transform));
            return !isClipEmpty();
        }
        juce::Path p; for(auto r:list) p.addRectangle(r); clipToPath(p,{});
        return !isClipEmpty();
    }
    void excludeClipRectangle(const juce::Rectangle<int>& r) override
    {
        juce::Path p; p.setUsingNonZeroWinding(false);p.addRectangle(getClipBounds());p.addRectangle(r);
        clipToPath(p,{});
    }
    void clipToPath(const juce::Path& path,const juce::AffineTransform& t) override
    {
        auto p=path;p.applyTransform(t.followedBy(state.transform));
        if (isRectanglePath(p)) { pushClipRectangle(p.getBounds()); return; }
        state.onlyRectClips=false;
        state.clipBounds=state.clipBounds.getIntersection(p.getBounds());
        const auto id=nextId();
        defs += "<clipPath id=\""+id+"\" clipPathUnits=\"userSpaceOnUse\"><path d=\""+pathData(p)
            +"\" clip-rule=\""+(p.isUsingNonZeroWinding()?"nonzero":"evenodd")+"\"/></clipPath>\n";
        state.clips.push_back("clip-path=\"url(#"+id+")\"");
    }
    void clipToImageAlpha(const juce::Image& image,const juce::AffineTransform& t) override
    {
        state.onlyRectClips=false;
        const auto id=nextId();
        defs += "<mask id=\""+id+"\" maskUnits=\"userSpaceOnUse\" maskContentUnits=\"userSpaceOnUse\" style=\"mask-type:alpha\">"
            +imageElement(image,t.followedBy(state.transform),true)+"</mask>\n";
        state.clips.push_back("mask=\"url(#"+id+")\"");
        state.vectorMask = image.getProperties()->getWithDefault("ana-source-svg", {}).toString();
        state.vectorMaskTransform = t.followedBy(state.transform);
        state.vectorMaskSize = image.getBounds().toFloat();
        state.vectorMaskClip = state.clips.back();
        state.clipBounds=state.clipBounds.getIntersection(image.getBounds().toFloat().transformedBy(t.followedBy(state.transform)));
    }
    bool clipRegionIntersects(const juce::Rectangle<int>& r) override
    { return state.clipBounds.intersects(r.toFloat().transformedBy(state.transform)); }
    juce::Rectangle<int> getClipBounds() const override
    { return state.clipBounds.transformedBy(state.transform.inverted()).getSmallestIntegerContainer(); }
    bool isClipEmpty() const override {return state.clipBounds.isEmpty();}
    void saveState() override { stack.push_back(state); }
    void restoreState() override { if(!stack.empty()){state=stack.back();stack.pop_back();} }
    void beginTransparencyLayer(float opacity) override {content += "<g opacity=\""+number(opacity)+"\">\n";saveState();}
    void endTransparencyLayer() override {restoreState();content += "</g>\n";}
    void setFill(const juce::FillType& fill) override {state.fill=fill;}
    void setOpacity(float opacity) override {state.fill.setOpacity(opacity);}
    void setInterpolationQuality(juce::Graphics::ResamplingQuality) override {}
    void fillRect(const juce::Rectangle<int>& r,bool) override {fillRect(r.toFloat());}
    void fillRect(const juce::Rectangle<float>& r) override {juce::Path p;p.addRectangle(r);fillPath(p,{});}
    void fillRectList(const juce::RectangleList<float>& list) override {for(auto r:list)fillRect(r);}
    void fillPath(const juce::Path& path,const juce::AffineTransform& t) override
    {
        if (isClipEmpty() || path.isEmpty() || state.fill.getOpacity() <= 0.0f) return;
        auto world = path;
        const auto transform = t.followedBy(state.transform);
        world.applyTransform(transform);
        if (!state.clipBounds.intersects(world.getBounds())) return;
        if (state.vectorMask.isNotEmpty() && state.fill.isColour()
            && world.getBounds().contains(state.vectorMaskSize.transformedBy(state.vectorMaskTransform)))
        {
            const auto clips = state.clips;
            state.clips.erase(std::remove(state.clips.begin(), state.clips.end(), state.vectorMaskClip), state.clips.end());
            emit(vectorMaskElement(), state.vectorMaskSize.transformedBy(state.vectorMaskTransform));
            state.clips = clips;
            return;
        }
        juce::Path clipped;
        if (state.fill.isColour() && clipPolygons(world, state.clipBounds, clipped))
        {
            if (!clipped.isEmpty()) emit("<path d=\"" + pathData(clipped) + "\" fill-rule=\""
                + (path.isUsingNonZeroWinding() ? "nonzero" : "evenodd") + "\" " + fillAttributes() + "/>\n", clipped.getBounds());
        }
        else emit("<path d=\""+pathData(path)+"\" transform=\""+matrix(transform)
            +"\" fill-rule=\""+(path.isUsingNonZeroWinding()?"nonzero":"evenodd")+"\" "+fillAttributes()+"/>\n", world.getBounds());
    }
    void drawImage(const juce::Image& image,const juce::AffineTransform& t) override
    {
        if (!image.isValid() || state.fill.getOpacity() <= 0.0f) return;
        emit("<g opacity=\""+number(state.fill.getOpacity())+"\">"+imageElement(image,t.followedBy(state.transform),false)+"</g>\n",
            image.getBounds().toFloat().transformedBy(t.followedBy(state.transform)));
    }
    void drawLine(const juce::Line<float>& line) override {drawLineWithThickness(line,1.f);}
    void setFont(const juce::Font& font) override {state.font=font;}
    const juce::Font& getFont() override {return state.font;}
    void drawGlyphs(juce::Span<const uint16_t> glyphs,juce::Span<const juce::Point<float>> positions,const juce::AffineTransform& t) override
    {
        if (glyphs.size() == 0 || isClipEmpty() || state.fill.getOpacity() <= 0.0f) return;
        const auto typeface=state.font.getTypefacePtr();
        auto& characters = glyphCharacters[typeface.get()];
        if (characters.empty())
        {
            // JUCE passes shaped glyph IDs to its low-level renderer. Recover
            // their Unicode characters from the typeface's nominal cmap.
            for (juce::juce_wchar codepoint = 32; codepoint <= 0xffff; ++codepoint)
                if (codepoint < 0xd800 || codepoint > 0xdfff)
                    if (const auto glyph = typeface->getNominalGlyphForCodepoint(codepoint); glyph && *glyph != 0)
                        characters.try_emplace(*glyph, codepoint);
        }
        juce::String text, xPositions, yPositions;
        const auto horizontalScale=state.font.getHorizontalScale();
        juce::Rectangle<float> inkBounds;
        bool hasInk = false;
        for(size_t i=0;i<glyphs.size();++i)
        {
            juce::Path outline;
            typeface->getOutlineForGlyph(state.font.getMetricsKind(), glyphs[i], outline);
            if (!outline.isEmpty())
            {
                const auto bounds = outline.getBoundsTransformed(juce::AffineTransform::scale(
                    state.font.getHeight() * horizontalScale, state.font.getHeight())
                    .translated(positions[i]).followedBy(t).followedBy(state.transform));
                inkBounds = hasInk ? inkBounds.getUnion(bounds) : bounds;
                hasInk = true;
            }
            const auto found=characters.find(glyphs[i]);
            text += juce::String::charToString(found!=characters.end() ? found->second : 0xfffd);
            xPositions += number(positions[i].x / horizontalScale) + " ";
            yPositions += number(positions[i].y) + " ";
        }
        if (!hasInk || !inkBounds.intersects(state.clipBounds)) return;
        juce::XmlElement element("text");
        element.setAttribute("xml:space", "preserve");
        element.setAttribute("font-family", typeface->getName());
        element.setAttribute("font-size", number(state.font.getHeight() * state.font.getHeightToPointsFactor()));
        const auto style=typeface->getStyle().toLowerCase();
        element.setAttribute("font-weight", style.contains("bold") ? "700" : style.contains("medium") ? "500" : "400");
        element.setAttribute("font-style", state.font.isItalic() ? "italic" : "normal");
        element.setAttribute("x", xPositions.trimEnd());
        element.setAttribute("y", yPositions.trimEnd());
        element.setAttribute("transform", matrix(juce::AffineTransform::scale(horizontalScale,1.f).followedBy(t).followedBy(state.transform)));
        element.addTextElement(text);
        auto markup=element.toString(juce::XmlElement::TextFormat().withoutHeader());
        markup=markup.replaceFirstOccurrenceOf("<text ", "<text " + fillAttributes() + " ");
        emit(markup, inkBounds);

    }
    std::unique_ptr<juce::ImageType> getPreferredImageTypeForTemporaryImages() const override
    {return std::make_unique<juce::SoftwareImageType>();}
    juce::uint64 getFrameId() const override {return 1;}
private:
    struct State
    {
        juce::AffineTransform transform;
        juce::Rectangle<float> clipBounds;
        juce::FillType fill;
        juce::Font font {juce::FontOptions()};
        std::vector<juce::String> clips;
        juce::String vectorMask, vectorMaskClip;
        juce::AffineTransform vectorMaskTransform;
        juce::Rectangle<float> vectorMaskSize;
        bool onlyRectClips=true;
        bool hasRectClip=false;
    } state;
    juce::Component& root;
    const juce::Rectangle<float> canvasBounds;
    std::vector<State> stack;
    std::unordered_map<const juce::Typeface*, std::unordered_map<uint32_t, juce::juce_wchar>> glyphCharacters;
    int serial=0;
    std::unordered_map<std::string,int> usedIds;
    std::unordered_set<std::string> assignedIds;
    std::vector<std::pair<std::string,juce::String>> rectClipCache;
    std::unordered_map<std::string,size_t> rectClipCacheIndex;
    juce::String uniqueId(const juce::String& base)
    {
        const auto clean = sanitizeSvgId(base);
        auto& count = usedIds[clean.toStdString()];
        juce::String id;
        do { id = ++count == 1 ? clean : clean + "-" + juce::String(count); }
        while (!assignedIds.insert(id.toStdString()).second);
        return id;
    }
    void groupPaint(int start, const juce::String& name)
    {
        if (content.length() == start) return;
        juce::XmlElement group("g");
        group.setAttribute("id", uniqueId(name));
        group.setAttribute("data-role", "artwork");
        const auto opening = group.toString(juce::XmlElement::TextFormat().withoutHeader()).replace("/>", ">");
        content = content.substring(0, start) + opening + content.substring(start) + "</g>\n";
    }
    juce::String nextId(){return "ana-svg-"+juce::String(++serial);}
    juce::String rectClipAttribute(const juce::Rectangle<float>& rect)
    {
        const auto key=number(rect.getX())+","+number(rect.getY())+","
            +number(rect.getWidth())+","+number(rect.getHeight());
        const auto keyString=key.toStdString();
        if(const auto found=rectClipCacheIndex.find(keyString);found!=rectClipCacheIndex.end())
            return rectClipCache[found->second].second;
        const auto id=nextId();
        juce::Path p;p.addRectangle(rect);
        defs += "<clipPath id=\""+id+"\" clipPathUnits=\"userSpaceOnUse\"><path d=\""+pathData(p)+"\" clip-rule=\"nonzero\"/></clipPath>\n";
        juce::String attribute="clip-path=\"url(#"+id+")\"";
        rectClipCacheIndex.emplace(keyString,rectClipCache.size());
        rectClipCache.emplace_back(keyString,attribute);
        return attribute;
    }
    void rollbackRectClips(size_t count)
    {
        while(rectClipCache.size()>count)
        {
            rectClipCacheIndex.erase(rectClipCache.back().first);
            rectClipCache.pop_back();
        }
    }
    void emit(const juce::String& element, juce::Rectangle<float> bounds)
    {
        if(isClipEmpty() || element.isEmpty() || !state.clipBounds.intersects(bounds))return;
        const auto rectWrapper = state.hasRectClip && !state.clipBounds.contains(bounds)
            ? rectClipAttribute(state.clipBounds) : juce::String();
        if(state.onlyRectClips)
        {
            if(rectWrapper.isEmpty()) content += element;
            else content += "<g "+rectWrapper+">"+element+"</g>";
            content += "\n";
            return;
        }
        if(rectWrapper.isNotEmpty()) content += "<g "+rectWrapper+">";
        for(const auto& clip:state.clips)content += "<g "+clip+">";
        content += element;
        for(size_t i=0;i<state.clips.size();++i)content += "</g>";
        if(rectWrapper.isNotEmpty()) content += "</g>";
        content += "\n";
    }
    juce::String fillAttributes()
    {
        const auto opacity="fill-opacity=\""+number(state.fill.getOpacity())+"\"";
        if(state.fill.gradient!=nullptr)
        {
            const auto& gradient=*state.fill.gradient;const auto id=nextId();
            const auto radial=gradient.isRadial;
            defs += "<"+juce::String(radial?"radialGradient":"linearGradient")+" id=\""+id+"\" gradientUnits=\"userSpaceOnUse\" gradientTransform=\""+matrix(state.fill.transform)+"\" ";
            if(radial)defs += "cx=\""+number(gradient.point1.x)+"\" cy=\""+number(gradient.point1.y)+"\" r=\""+number(gradient.point1.getDistanceFrom(gradient.point2))+"\">";
            else defs += "x1=\""+number(gradient.point1.x)+"\" y1=\""+number(gradient.point1.y)+"\" x2=\""+number(gradient.point2.x)+"\" y2=\""+number(gradient.point2.y)+"\">";
            for(int i=0;i<gradient.getNumColours();++i)
            {
                const auto c=gradient.getColour(i);
                defs += "<stop offset=\""+juce::String(gradient.getColourPosition(i),5)+"\" stop-color=\"#"+c.toDisplayString(false)+"\" stop-opacity=\""+number(c.getFloatAlpha())+"\"/>";
            }
            defs += "</"+juce::String(radial?"radialGradient":"linearGradient")+">\n";
            return "fill=\"url(#"+id+")\" "+opacity;
        }
        return "fill=\"#"+state.fill.colour.toDisplayString(false)+"\" "+opacity;
    }
    juce::String vectorMaskElement()
    {
        auto xml = juce::parseXML(state.vectorMask);
        if (xml == nullptr) return {};
        const auto viewBox = juce::StringArray::fromTokens(xml->getStringAttribute("viewBox"), " ,", "");
        if (viewBox.size() != 4 || viewBox[2].getFloatValue() <= 0 || viewBox[3].getFloatValue() <= 0) return {};
        const auto transform = juce::AffineTransform::translation(-viewBox[0].getFloatValue(), -viewBox[1].getFloatValue())
            .scaled(state.vectorMaskSize.getWidth() / viewBox[2].getFloatValue(),
                    state.vectorMaskSize.getHeight() / viewBox[3].getFloatValue())
            .followedBy(state.vectorMaskTransform);
        const auto colour = "#" + state.fill.colour.toDisplayString(false);
        const auto recolour = [&colour](auto&& self, juce::XmlElement& node) -> void
        {
            for (const auto* attribute : {"stroke", "fill"})
                if (node.hasAttribute(attribute) && node.getStringAttribute(attribute) != "none")
                    node.setAttribute(attribute, colour);
            for (auto* child : node.getChildIterator()) self(self, *child);
        };
        recolour(recolour, *xml);
        xml->setTagName("g");
        for (const auto* attribute : {"xmlns", "xmlns:xlink", "width", "height", "viewBox", "version"}) xml->removeAttribute(attribute);
        xml->setAttribute("transform", matrix(transform));
        xml->setAttribute("opacity", number(state.fill.getOpacity()));
        return xml->toString(juce::XmlElement::TextFormat().withoutHeader());
    }
    juce::String imageElement(const juce::Image& image,const juce::AffineTransform& t,bool mask)
    {
        if(!image.isValid())return {};
        if(auto xml=juce::XmlDocument::parse(image.getProperties()->getWithDefault("ana-source-svg",{}).toString()))
        {
            xml->setAttribute("width",image.getWidth());xml->setAttribute("height",image.getHeight());
            if(mask){xml->setAttribute("stroke","#ffffff");}
            return "<g transform=\""+matrix(t)+"\">"+xml->toString(juce::XmlElement::TextFormat().withoutHeader())+"</g>";
        }
        juce::MemoryOutputStream stream;juce::PNGImageFormat().writeImageToStream(image,stream);
        return "<image width=\""+juce::String(image.getWidth())+"\" height=\""+juce::String(image.getHeight())+"\" transform=\""+matrix(t)+"\" xlink:href=\"data:image/png;base64,"+juce::Base64::toBase64(stream.getData(),stream.getDataSize())+"\"/>";
    }
};

void compactGroups(juce::XmlElement& parent)
{
    for (int index = 0; index < parent.getNumChildElements();)
    {
        auto* child = parent.getChildElement(index);
        compactGroups(*child);
        if (child->getStringAttribute("transform") == "matrix(1.00000 0.00000 0.00000 1.00000 0.00000 0.00000)")
            child->removeAttribute("transform");
        if (child->getDoubleAttribute("opacity", 1.0) == 1.0) child->removeAttribute("opacity");
        const auto empty = (child->hasTagName("g") && child->getNumChildElements() == 0)
            || (child->hasTagName("path") && child->getStringAttribute("d").trim().isEmpty())
            || (child->hasTagName("text") && child->getAllSubText().trim().isEmpty())
            || child->getDoubleAttribute("opacity", 1.0) <= 0.0;
        if (empty) { parent.removeChildElement(child, true); continue; }
        if (child->hasTagName("g") && !child->hasAttribute("id"))
        {
            if (child->getNumAttributes() == 0)
            {
                auto at = index;
                while (auto* item = child->getFirstChildElement())
                {
                    child->removeChildElement(item, false);
                    parent.insertChildElement(item, at++);
                }
                parent.removeChildElement(child, true);
                continue;
            }
            auto* item = child->getFirstChildElement();
            auto mergeable = child->getNumChildElements() == 1;
            for (int attribute = 0; attribute < child->getNumAttributes(); ++attribute)
            {
                const auto name = child->getAttributeName(attribute);
                if (name != "transform" && name != "opacity" && name != "clip-path" && name != "mask") mergeable = false;
                if ((name == "clip-path" || name == "mask")
                    && (item->hasAttribute("transform") || item->hasAttribute(name) || item->hasTagName("svg"))) mergeable = false;
            }
            if (mergeable)
            {
                for (int attribute = 0; attribute < child->getNumAttributes(); ++attribute)
                {
                    const auto name = child->getAttributeName(attribute);
                    auto value = child->getAttributeValue(attribute);
                    if (name == "transform" && item->hasAttribute(name)) value += " " + item->getStringAttribute(name);
                    if (name == "opacity") value = number(static_cast<float>(value.getDoubleValue() * item->getDoubleAttribute(name, 1.0)));
                    item->setAttribute(name, value);
                }
                child->removeChildElement(item, false);
                parent.replaceChildElement(child, item);
                continue;
            }
        }
        ++index;
    }
}

void replaceDefinitionReferences(juce::XmlElement& node, const juce::String& from, const juce::String& to)
{
    for (int i = 0; i < node.getNumAttributes(); ++i)
    {
        const auto name = node.getAttributeName(i);
        auto value = node.getAttributeValue(i).replace("url(#" + from + ")", "url(#" + to + ")");
        if ((name == "href" || name == "xlink:href") && value == "#" + from) value = "#" + to;
        node.setAttribute(name, value);
    }
    for (auto* child : node.getChildIterator()) replaceDefinitionReferences(*child, from, to);
}

void collectDefinitionReferences(const juce::XmlElement& node, std::unordered_set<std::string>& result)
{
    for (int i = 0; i < node.getNumAttributes(); ++i)
    {
        const auto name = node.getAttributeName(i);
        auto value = node.getAttributeValue(i);
        if ((name == "href" || name == "xlink:href") && value.startsWithChar('#')) result.insert(value.substring(1).toStdString());
        while (value.contains("url(#"))
        {
            value = value.fromFirstOccurrenceOf("url(#", false, false);
            result.insert(value.upToFirstOccurrenceOf(")", false, false).toStdString());
            value = value.fromFirstOccurrenceOf(")", false, false);
        }
    }
    for (const auto* child : node.getChildIterator()) collectDefinitionReferences(*child, result);
}

juce::String compactSvg(const juce::String& source)
{
    auto svg = juce::parseXML(source);
    if (svg == nullptr) return source;
    compactGroups(*svg);
    if (auto* definitions = svg->getChildByName("defs"))
    {
        std::unordered_map<std::string, juce::String> canonical;
        for (int index = 0; index < definitions->getNumChildElements();)
        {
            auto* definition = definitions->getChildElement(index);
            juce::XmlElement shape(*definition);
            shape.removeAttribute("id");
            const auto key = shape.toString(juce::XmlElement::TextFormat().singleLine().withoutHeader()).toStdString();
            const auto id = definition->getStringAttribute("id");
            if (const auto found = canonical.find(key); found != canonical.end())
            {
                replaceDefinitionReferences(*svg, id, found->second);
                definitions->removeChildElement(definition, true);
            }
            else { canonical.emplace(key, id); ++index; }
        }
        std::unordered_set<std::string> referenced;
        for (const auto* child : svg->getChildIterator())
            if (child != definitions) collectDefinitionReferences(*child, referenced);
        for (size_t before = 0; before != referenced.size();)
        {
            before = referenced.size();
            for (const auto* definition : definitions->getChildIterator())
                if (referenced.count(definition->getStringAttribute("id").toStdString()) != 0)
                    collectDefinitionReferences(*definition, referenced);
        }
        for (int index = definitions->getNumChildElements(); --index >= 0;)
            if (referenced.count(definitions->getChildElement(index)->getStringAttribute("id").toStdString()) == 0)
                definitions->removeChildElement(definitions->getChildElement(index), true);
        if (definitions->getNumChildElements() == 0) svg->removeChildElement(definitions, true);
    }
    return svg->toString();
}

class ViewportExportLayout
{
public:
    explicit ViewportExportLayout(juce::Component& rootIn) : root(rootIn), originalBounds(root.getBounds())
    {
        collect(root);
        int extra = 0;
        for (const auto& item : views)
            if (auto* content = item.viewport->getViewedComponent())
                extra = juce::jmax(extra, content->getHeight() - item.viewport->getHeight());
        // Editors/windows provide a virtual export layout: never resize their native peer.
        if (extra > 0 && dynamic_cast<Layout*>(&root) == nullptr && !root.isOnDesktop())
            root.setSize(root.getWidth(), root.getHeight() + extra);
        for (const auto& item : views)
            if (item.viewport != nullptr) item.viewport->setViewPosition(0, 0);
    }
    ~ViewportExportLayout()
    {
        if (root.getBounds() != originalBounds) root.setBounds(originalBounds);
        for (const auto& item : views)
            if (item.viewport != nullptr) item.viewport->setViewPosition(item.position);
    }
private:
    void collect(juce::Component& component)
    {
        if (auto* viewport = dynamic_cast<juce::Viewport*>(&component))
            views.push_back({viewport, viewport->getViewPosition()});
        for (auto* child : component.getChildren())
            if (child->isVisible()) collect(*child);
    }
    struct View { juce::Component::SafePointer<juce::Viewport> viewport; juce::Point<int> position; };
    juce::Component& root;
    juce::Rectangle<int> originalBounds;
    std::vector<View> views;
};

void collectExportLayouts(juce::Component& component,std::vector<Layout*>& result)
{
    // Match the paint walk: only visible components end up in the file.
    if(auto* layout=dynamic_cast<Layout*>(&component))
        result.push_back(layout);
    for(int index=0;index<component.getNumChildComponents();++index)
        if(auto* child=component.getChildComponent(index))
            if(child->isVisible())
                collectExportLayouts(*child,result);
}
}
juce::String render(juce::Component& component)
{
    ViewportExportLayout viewportLayout(component);
    std::vector<Layout*> exportLayouts;
    collectExportLayouts(component,exportLayouts);

    // The artwork can be taller than the window: the canvas grows to the height the
    // content needs so that everything that does not fit is exported as well.
    auto canvasHeight=component.getHeight();
    for(auto* layout:exportLayouts)
        canvasHeight=juce::jmax(canvasHeight,layout->getSvgExportCanvasHeight());
    for(auto* layout:exportLayouts)
        layout->prepareSvgExportLayout(canvasHeight);

    Context context(component,{0,0,component.getWidth(),canvasHeight});juce::Graphics graphics(context);
    context.paintComponent(component,graphics);

    for(auto* layout:exportLayouts)
        layout->finishSvgExportLayout();
    return compactSvg("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" version=\"1.1\" width=\""
        +juce::String(component.getWidth())+"\" height=\""+juce::String(canvasHeight)+"\" viewBox=\"0 0 "
        +juce::String(component.getWidth())+" "+juce::String(canvasHeight)+"\">\n<defs>"+context.defs+"</defs>\n"+context.content+"</svg>\n");
}
void chooseFile(juce::Component& component)
{
    const auto svg=render(component);
    auto chooser=std::make_shared<juce::FileChooser>("Export SVG",juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("ANA.svg"),"*.svg");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [chooser,svg](const juce::FileChooser& dialog)
        {
            const auto file=dialog.getResult();if(file==juce::File())return;
            const auto target=file.withFileExtension("svg");
            if(!target.replaceWithText(svg))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"SVG export","Unable to save SVG.");
        });
}
}

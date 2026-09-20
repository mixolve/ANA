#import <AppKit/AppKit.h>

#include "SfSymbols.h"

juce::Image loadSfSymbol(const juce::String& symbolName, const float pointSize)
{
    if (@available(macOS 11.0, *))
    {
        NSString* name = [NSString stringWithUTF8String:symbolName.toRawUTF8()];
        auto* image = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
        if (image == nil)
            return {};

        NSImageSymbolConfiguration* configuration = [NSImageSymbolConfiguration
            configurationWithPointSize:pointSize weight:NSFontWeightRegular];
        image = [image imageWithSymbolConfiguration:configuration];
        if (image == nil)
            return {};

        const auto pixelSize = std::max(1, juce::roundToInt(pointSize * 2.0f));
        auto* representation = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nil
                          pixelsWide:pixelSize
                          pixelsHigh:pixelSize
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bytesPerRow:0
                        bitsPerPixel:0];
        if (representation == nil)
            return {};

        [NSGraphicsContext saveGraphicsState];
        auto* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:representation];
        [NSGraphicsContext setCurrentContext:context];
        [image drawInRect:NSMakeRect(0.0, 0.0, pixelSize, pixelSize)
                 fromRect:NSZeroRect
                operation:NSCompositingOperationSourceOver
                 fraction:1.0];
        [context flushGraphics];
        [NSGraphicsContext restoreGraphicsState];

        auto* data = [representation representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (data == nil)
            return {};

        auto rendered = juce::ImageFileFormat::loadFrom(data.bytes, static_cast<size_t>(data.length));
        if (! rendered.isValid())
            return {};

        // Trim SF Symbol padding so iconFontSize describes the visible glyph.
        juce::Image::BitmapData pixels(rendered, juce::Image::BitmapData::readOnly);
        auto left = rendered.getWidth();
        auto top = rendered.getHeight();
        auto right = -1;
        auto bottom = -1;

        for (int y = 0; y < rendered.getHeight(); ++y)
        {
            for (int x = 0; x < rendered.getWidth(); ++x)
            {
                if (pixels.getPixelColour(x, y).getAlpha() == 0)
                    continue;

                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }

        if (right >= left && bottom >= top)
        {
            const auto visibleBounds = juce::Rectangle<int>::leftTopRightBottom(
                left, top, right + 1, bottom + 1)
                .expanded(1)
                .getIntersection(rendered.getBounds());
            return rendered.getClippedImage(visibleBounds);
        }

        return rendered;
    }

    return {};
}

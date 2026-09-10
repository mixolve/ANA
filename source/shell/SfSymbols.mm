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
        return data != nil
            ? juce::ImageFileFormat::loadFrom(data.bytes, static_cast<size_t>(data.length))
            : juce::Image {};
    }

    return {};
}

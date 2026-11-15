#pragma once
#include "Theme.h"

// JUCE snaps cached glyph baselines to integer device pixels. Centre the
// original raster mask instead of rounding an outline-derived baseline: an odd
// ink height inside an even field needs a half-device-pixel translation.
class NumericTextRaster final
{
public:
    static float inkCentre(const juce::Image& image)
    {
        std::vector<float> coverage(static_cast<size_t>(image.getHeight()),0.0f);
        for(int y=0;y<image.getHeight();++y)
            for(int x=0;x<image.getWidth();++x)
                coverage[static_cast<size_t>(y)]=juce::jmax(coverage[static_cast<size_t>(y)],
                    static_cast<float>(image.getPixelAt(x,y).getAlpha())/255.0f);
        int top=0,bottom=image.getHeight()-1;
        while(top<image.getHeight() && coverage[static_cast<size_t>(top)]<0.5f) ++top;
        while(bottom>=top && coverage[static_cast<size_t>(bottom)]<0.5f) --bottom;
        if(bottom<top) return static_cast<float>(image.getHeight())*0.5f;
        const auto first=coverage[static_cast<size_t>(top)], last=coverage[static_cast<size_t>(bottom)];
        const auto above=top>0 ? coverage[static_cast<size_t>(top-1)] : 0.0f;
        const auto below=bottom+1<image.getHeight() ? coverage[static_cast<size_t>(bottom+1)] : 0.0f;
        const auto upper=static_cast<float>(top)+0.5f-(first-0.5f)/(first-above);
        const auto lower=static_cast<float>(bottom)+0.5f+(last-0.5f)/(last-below);
        return (upper+lower)*0.5f;
    }

    static float verticalCorrection(int height, float scale)
    {
        juce::Image reference(juce::Image::ARGB, juce::roundToInt(200.0f*scale),
                              juce::roundToInt(static_cast<float>(height)*scale), true);
        juce::Graphics g(reference);
        g.addTransform(juce::AffineTransform::scale(scale));
        g.setFont(ana::ui::makeFont()); g.setColour(juce::Colours::white);
        g.drawText("0123456789",juce::Rectangle<int>(0,0,200,height),juce::Justification::centred,false);
        return (static_cast<float>(reference.getHeight())*0.5f-inkCentre(reference))/scale;
    }

    static juce::Image centreRaster(const juce::Image& source,float delta)
    {
        juce::Image result(juce::Image::ARGB,source.getWidth(),source.getHeight(),true);
        // Resample the device-pixel mask explicitly: JUCE also rounds a plain
        // image translation, so sending it a fractional Y repeats the bug.
        for(int y=0;y<source.getHeight();++y)
        {
            const auto sourceY=static_cast<float>(y)-delta;
            const auto row=static_cast<int>(std::floor(sourceY));
            const auto fraction=sourceY-static_cast<float>(row);
            for(int x=0;x<source.getWidth();++x)
            {
                const auto a=row>=0 && row<source.getHeight() ? source.getPixelAt(x,row) : juce::Colours::transparentBlack;
                const auto b=row+1>=0 && row+1<source.getHeight() ? source.getPixelAt(x,row+1) : juce::Colours::transparentBlack;
                result.setPixelAt(x,y,a.interpolatedWith(b,fraction));
            }
        }
        return result;
    }

    bool prepare(const juce::String& text, juce::Rectangle<int> field,
                 juce::Rectangle<int> textBounds, float deviceScale, double elapsed)
    {
        cachedElapsed=elapsed;
        const auto scale=juce::jmax(1.0f,deviceScale);
        const auto scrolling=ana::ui::getTextPixelWidth(ana::ui::makeFont(),text)>textBounds.getWidth();
        if(mask.isNull() || cachedText!=text || cachedField!=field || cachedTextBounds!=textBounds
            || !juce::approximatelyEqual(cachedScale,scale) || scrolling)
        {
            if(cachedField.getHeight()!=field.getHeight() || !juce::approximatelyEqual(cachedScale,scale) || mask.isNull())
                correction=verticalCorrection(field.getHeight(),scale);
            mask=juce::Image(juce::Image::ARGB,juce::roundToInt(static_cast<float>(field.getWidth())*scale),
                             juce::roundToInt(static_cast<float>(field.getHeight())*scale),true);
            juce::Graphics g(mask); g.addTransform(juce::AffineTransform::scale(scale));
            g.setFont(ana::ui::makeFont()); g.setColour(juce::Colours::white);
            ana::ui::drawLoopingText(g,text,textBounds,ana::ui::makeFont(),juce::Justification::centred,elapsed);
            mask=centreRaster(mask,correction*scale);
            cachedText=text; cachedField=field; cachedTextBounds=textBounds; cachedScale=scale;
        }
        return scrolling;
    }
    void draw(juce::Graphics& g) const
    {
        if(g.getInternalContext().isVectorDevice())
        {
            juce::Graphics::ScopedSaveState saved(g);
            g.addTransform(juce::AffineTransform::translation(0,correction));
            g.setFont(ana::ui::makeFont());
            ana::ui::drawLoopingText(g,cachedText,cachedTextBounds,ana::ui::makeFont(),juce::Justification::centred,cachedElapsed);
            return;
        }
        g.drawImageTransformed(mask,juce::AffineTransform::scale(1.0f/cachedScale),true);
    }
private:
    juce::Image mask;
    juce::String cachedText;
    juce::Rectangle<int> cachedField,cachedTextBounds;
    float cachedScale=0,correction=0;
    double cachedElapsed=0;
};


#pragma once
#include "BurningFilmShader.h"
#include "ColourFilter.h"
#include <juce_opengl/juce_opengl.h>

struct BurningFilmFrame
{
    juce::Rectangle<int> bounds;
    float renderingScale;
    juce::Point<int> origin;
    int canvasSide, canvasHeight;
    float coordinateScale, cropOffset;
    AnimationColourFilter filter;
    float time, burnSpeed, glow, patternScale;
};

inline void paintBurningFilmFrame(juce::Graphics& g,
                                 juce::OpenGLGraphicsContextCustomShader& shader,
                                 const BurningFilmFrame& frame)
{
    shader.onShaderActivated = [frame](juce::OpenGLShaderProgram& program)
    {
        juce::OpenGLShaderProgram::Uniform(program, "u_time").set(frame.time);
        juce::OpenGLShaderProgram::Uniform(program, "u_origin").set(static_cast<float>(frame.origin.x) * frame.renderingScale,
                                                                 static_cast<float>(frame.origin.y) * frame.renderingScale);
        juce::OpenGLShaderProgram::Uniform(program, "u_res").set(static_cast<float>(frame.canvasSide) * frame.renderingScale,
                                                              static_cast<float>(frame.canvasHeight) * frame.renderingScale);
        juce::OpenGLShaderProgram::Uniform(program, "u_coordinateScale").set(frame.coordinateScale);
        juce::OpenGLShaderProgram::Uniform(program, "u_cropOffset").set(frame.cropOffset * frame.renderingScale);
        juce::OpenGLShaderProgram::Uniform(program, "u_patternScale").set(frame.patternScale);
        juce::OpenGLShaderProgram::Uniform(program, "u_burnSpeed").set(frame.burnSpeed);
        juce::OpenGLShaderProgram::Uniform(program, "u_emberGlow").set(frame.glow);
        const auto& r = frame.filter.rows;
        juce::OpenGLShaderProgram::Uniform(program, "u_colourRow0").set(r[0][0],r[0][1],r[0][2]);
        juce::OpenGLShaderProgram::Uniform(program, "u_colourRow1").set(r[1][0],r[1][1],r[1][2]);
        juce::OpenGLShaderProgram::Uniform(program, "u_colourRow2").set(r[2][0],r[2][1],r[2][2]);
    };
    g.setColour(juce::Colours::white);
    shader.fillRect(g.getInternalContext(), frame.bounds);
}

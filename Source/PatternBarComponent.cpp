#include "PatternBarComponent.h"

namespace
{
    const juce::Colour offColour { 0xff2a2d35 };
    const juce::Colour accentColour { 0xffff8a3d };
    const juce::Colour textColour { 0xffe8e8ea };
    constexpr int labelWidth = 70;
    constexpr float lengthAreaWidth = 150.0f;
    constexpr float lengthLabelWidth = 44.0f;
}

PatternBarComponent::PatternBarComponent (NzDrumProcessor& processor)
    : nzProcessor (processor)
{
}

bool PatternBarComponent::patternHasSteps (int pattern) const
{
    const int patternLength = nzProcessor.getPatternLength (pattern);

    for (int pad = 0; pad < NzDrumProcessor::numPads; ++pad)
        for (int step = 0; step < patternLength; ++step)
            if (nzProcessor.getStep (pattern, pad, step) != StepLevel::off)
                return true;

    return false;
}

void PatternBarComponent::refresh()
{
    bool changed = false;

    const auto update = [&changed] (auto& field, auto value)
    {
        if (field != value)
        {
            field = value;
            changed = true;
        }
    };

    update (selected, nzProcessor.getSelectedPattern());
    update (playing, nzProcessor.getPlayingPattern());
    update (length, nzProcessor.getPatternLength (selected));

    for (int pattern = 0; pattern < NzDrumProcessor::numPatterns; ++pattern)
        update (hasSteps[(size_t) pattern], patternHasSteps (pattern));

    // Parpadeo de ~2 Hz para el patrón que sigue sonando mientras espera.
    if (++blinkCounter >= 8)
    {
        blinkCounter = 0;
        blinkOn = ! blinkOn;

        if (playing != selected)
            changed = true;
    }

    if (changed)
        repaint();
}

juce::Rectangle<float> PatternBarComponent::patternBounds (int pattern) const
{
    auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) labelWidth).withTrimmedRight (lengthAreaWidth + lengthLabelWidth + 8.0f);
    const float groupGap = 8.0f;
    const float width = (area.getWidth() - groupGap * 3.0f) / (float) NzDrumProcessor::numPatterns;
    const float x = area.getX() + (float) pattern * width + (float) (pattern / 4) * groupGap;

    return { x + 2.0f, area.getY() + 4.0f, width - 4.0f, area.getHeight() - 8.0f };
}

juce::Rectangle<float> PatternBarComponent::lengthBounds (int index) const
{
    auto area = getLocalBounds().toFloat().removeFromRight (lengthAreaWidth);
    const float width = area.getWidth() / (float) lengths.size();

    return { area.getX() + (float) index * width + 2.0f, area.getY() + 4.0f, width - 4.0f, area.getHeight() - 8.0f };
}

void PatternBarComponent::paint (juce::Graphics& g)
{
    g.setColour (textColour.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("Patrón"), getLocalBounds().removeFromLeft (labelWidth).reduced (14, 0),
                juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));

    for (int pattern = 0; pattern < NzDrumProcessor::numPatterns; ++pattern)
    {
        const auto bounds = patternBounds (pattern);
        const bool isSelected = pattern == selected;

        g.setColour (isSelected ? accentColour : offColour);
        g.fillRoundedRectangle (bounds, 4.0f);

        if (pattern == playing && playing != selected && blinkOn)
        {
            g.setColour (accentColour);
            g.drawRoundedRectangle (bounds, 4.0f, 2.0f);
        }

        g.setColour (isSelected ? juce::Colours::black : textColour.withAlpha (0.8f));
        g.drawText (juce::String (pattern + 1), bounds, juce::Justification::centred);

        if (hasSteps[(size_t) pattern])
        {
            g.setColour (isSelected ? juce::Colours::black.withAlpha (0.6f) : accentColour);
            g.fillEllipse (bounds.getRight() - 6.0f, bounds.getY() + 3.0f, 3.0f, 3.0f);
        }
    }

    // Rótulo para que el largo no se confunda con los patrones.
    g.setColour (textColour.withAlpha (0.6f));
    g.drawText ("Pasos", getLocalBounds().toFloat().withTrimmedRight (lengthAreaWidth).removeFromRight (lengthLabelWidth),
                juce::Justification::centred);

    for (int i = 0; i < (int) lengths.size(); ++i)
    {
        const auto bounds = lengthBounds (i);
        const bool isCurrent = lengths[(size_t) i] == length;

        g.setColour (isCurrent ? textColour : offColour);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (isCurrent ? juce::Colours::black : textColour.withAlpha (0.8f));
        g.drawText (juce::String (lengths[(size_t) i]), bounds, juce::Justification::centred);
    }
}

void PatternBarComponent::mouseDown (const juce::MouseEvent& event)
{
    for (int pattern = 0; pattern < NzDrumProcessor::numPatterns; ++pattern)
    {
        if (patternBounds (pattern).contains (event.position))
        {
            nzProcessor.selectPattern (pattern);
            refresh();
            return;
        }
    }

    for (int i = 0; i < (int) lengths.size(); ++i)
    {
        if (lengthBounds (i).contains (event.position))
        {
            nzProcessor.setPatternLength (selected, lengths[(size_t) i]);
            refresh();
            return;
        }
    }
}

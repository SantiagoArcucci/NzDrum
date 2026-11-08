#include "PatternBarComponent.h"

namespace
{
    const juce::Colour offColour { 0xff2a2d35 };
    const juce::Colour accentColour { 0xffff8a3d };
    const juce::Colour textColour { 0xffe8e8ea };
    constexpr int recWidth = 46;
    constexpr int labelWidth = recWidth + 64;
    constexpr float lengthAreaWidth = 150.0f;
    constexpr float lengthLabelWidth = 44.0f;
}

PatternBarComponent::PatternBarComponent (NzDrumProcessor& processor)
    : nzProcessor (processor)
{
    setTooltip (juce::String ("Right click on a pattern: copy, paste or clear"));

    recButton.setClickingTogglesState (true);
    recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    recButton.setTooltip (juce::String ("Record: while the DAW plays, pad hits (click or MIDI) land on the nearest "
                                              "step of the playing pattern, replacing what was there. "
                                              "In 16 Levels the source pad is recorded with its semitone."));
    recButton.onClick = [this] { nzProcessor.setRecording (recButton.getToggleState()); };
    addAndMakeVisible (recButton);
}

void PatternBarComponent::resized()
{
    recButton.setBounds (getLocalBounds().removeFromLeft (recWidth).reduced (2, 4));
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

    recButton.setToggleState (nzProcessor.isRecording(), juce::dontSendNotification);
    update (selected, nzProcessor.getSelectedPattern());
    update (playing, nzProcessor.getPlayingPattern());
    update (length, nzProcessor.getPatternLength (selected));

    for (int pattern = 0; pattern < NzDrumProcessor::numPatterns; ++pattern)
        update (hasSteps[(size_t) pattern], patternHasSteps (pattern));

    // ~2 Hz blink for the pattern that keeps playing while the switch waits.
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
    g.drawText (juce::String ("Pattern"), getLocalBounds().removeFromLeft (labelWidth).withTrimmedLeft (recWidth).reduced (8, 0),
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

    // A label so the length isn't mistaken for patterns.
    g.setColour (textColour.withAlpha (0.6f));
    g.drawText ("Steps", getLocalBounds().toFloat().withTrimmedRight (lengthAreaWidth).removeFromRight (lengthLabelWidth),
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
            if (event.mods.isPopupMenu())
            {
                showPatternMenu (pattern);
                return;
            }

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

void PatternBarComponent::showPatternMenu (int pattern)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String ("Pattern ") + juce::String (pattern + 1));
    menu.addItem ("Copy", [this, pattern] { nzProcessor.copyPattern (pattern); });
    menu.addItem ("Paste", nzProcessor.hasCopiedPattern(), false, [this, pattern] { nzProcessor.pastePattern (pattern); refresh(); });
    menu.addSeparator();
    menu.addItem ("Clear all steps", [this, pattern] { nzProcessor.clearPattern (pattern); refresh(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (patternBounds (pattern).toNearestInt())));
}

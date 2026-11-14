#include "PatternBarComponent.h"

namespace
{
    // Positions inside the panel, from the design.
    constexpr float keyTop = 22.0f, keyHeight = 30.0f;
    constexpr float patternLeft = 70.0f, patternPitch = 28.0f, patternWidth = 25.0f;
    constexpr float lengthLeft = 538.0f, lengthPitch = 36.0f, lengthWidth = 34.0f;
    constexpr float labelTop = 6.0f, labelHeight = 12.0f;
}

PatternBarComponent::PatternBarComponent (NzDrumProcessor& processor)
    : nzProcessor (processor)
{
    setTooltip (juce::String ("Right click on a pattern: copy, paste or clear"));

    recButton.setClickingTogglesState (true);
    recButton.setBorderAlpha (0.45f);
    recButton.setTooltip (juce::String ("Record: while the DAW plays, pad hits (click or MIDI) land on the nearest "
                                              "step of the playing pattern, replacing what was there. "
                                              "In 16 Levels the source pad is recorded with its semitone."));
    recButton.onClick = [this] { nzProcessor.setRecording (recButton.getToggleState()); };
    addAndMakeVisible (recButton);
}

void PatternBarComponent::resized()
{
    recButton.setBounds (14, (int) keyTop, 46, (int) keyHeight);
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
    return { patternLeft + (float) pattern * patternPitch, keyTop, patternWidth, keyHeight };
}

juce::Rectangle<float> PatternBarComponent::lengthBounds (int index) const
{
    return { lengthLeft + (float) index * lengthPitch, keyTop, lengthWidth, keyHeight };
}

void PatternBarComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    drawText (g, "LIVE", font::label(), colour::secondary, { 14.0f, labelTop, 46.0f, labelHeight }, juce::Justification::centredLeft);
    drawText (g, "PATTERN BANK", font::label(), colour::secondary, { patternLeft, labelTop, 300.0f, labelHeight }, juce::Justification::centredLeft);
    drawText (g, "STEPS", font::label(), colour::secondary, { lengthLeft, labelTop, 140.0f, labelHeight }, juce::Justification::centredLeft);

    for (int pattern = 0; pattern < NzDrumProcessor::numPatterns; ++pattern)
    {
        const auto bounds = patternBounds (pattern);
        const bool lit = pattern == playing;
        const auto textColour = drawUtilityKey (g, bounds, lit);

        // The pattern coming next, while the playing one finishes.
        if (pattern == selected && selected != playing && blinkOn)
        {
            juce::Path outline;
            outline.addRoundedRectangle (bounds.reduced (1.5f), 2.0f);
            const float dashes[] { 2.0f, 2.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (colour::active);
            g.fillPath (dashed);
        }

        drawText (g, juce::String (pattern + 1), font::bold (12.0f), textColour,
                  bounds.withTrimmedTop (2.0f), juce::Justification::centred);

        if (hasSteps[(size_t) pattern])
        {
            g.setColour (lit ? colour::lightInk : colour::active);
            g.fillEllipse (bounds.getX() + 10.5f, bounds.getY() + 3.0f, 4.0f, 4.0f);
        }
    }

    for (int i = 0; i < (int) lengths.size(); ++i)
    {
        const auto bounds = lengthBounds (i);
        const auto textColour = drawUtilityKey (g, bounds, lengths[(size_t) i] == length);
        drawText (g, juce::String (lengths[(size_t) i]), font::bold (12.0f), textColour, bounds, juce::Justification::centred);
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

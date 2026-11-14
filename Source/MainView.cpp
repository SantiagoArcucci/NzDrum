#include "MainView.h"
#include "Theme.h"

namespace
{
    // Panels, in design coordinates (Figma frame "NzDrum · B · Crema 80s").
    const juce::Rectangle<int> headerPanel { 10, 8, 700, 62 };
    const juce::Rectangle<int> padsPanel { 10, 76, 700, 480 };
    const juce::Rectangle<int> patternPanel { 10, 562, 700, 66 };
    const juce::Rectangle<int> sequencerPanel { 10, 634, 700, 104 };
    const juce::Rectangle<int> editorPanel { 10, 744, 700, 148 };

    constexpr int padWidth = 162, padHeight = 108;
    constexpr int padColumnX[] { 24, 194, 364, 534 };
    constexpr int padRowY[] { 90, 204, 318, 432 };   // top row first
    constexpr int gridColumns = 4;
}

// ============================================================================
// TransportDisplay
// ============================================================================

juce::String TransportDisplay::positionText (double ppq, int numerator, int denominator)
{
    if (numerator <= 0 || denominator <= 0)
    {
        numerator = 4;
        denominator = 4;
    }

    const double beatLength = 4.0 / (double) denominator;   // in quarter notes
    const double barLength = beatLength * (double) numerator;
    const double position = juce::jmax (0.0, ppq);
    const int bar = (int) std::floor (position / barLength);
    const double beat = (position - (double) bar * barLength) / beatLength + 1.0;

    return "BAR " + juce::String (bar + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (beat, 2);
}

void TransportDisplay::refresh()
{
    const auto t = nzProcessor.getTransport();
    const int selected = nzProcessor.getSelectedPattern();
    const int playing = nzProcessor.getPlayingPattern();

    const auto twoDigits = [] (int pattern) { return juce::String (pattern + 1).paddedLeft ('0', 2); };

    juce::String newLines[4];
    newLines[0] = "PATTERN " + twoDigits (playing);
    newLines[1] = selected != playing ? "NEXT " + twoDigits (selected) : juce::String();
    newLines[2] = juce::String (t.playing ? "PLAY  " : "STOP  ")
                + (t.bpm > 0.0 ? juce::String (t.bpm, 1) : juce::String ("---.-")) + " BPM";
    newLines[3] = positionText (t.ppq, t.timeSigNumerator, t.timeSigDenominator);

    bool changed = false;

    for (int i = 0; i < 4; ++i)
    {
        if (newLines[i] != lines[i])
        {
            lines[i] = newLines[i];
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void TransportDisplay::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto bounds = getLocalBounds().toFloat();
    drawDisplay (g, bounds);

    const auto font = font::mono (15.0f);
    const auto ink = colour::displayInk;
    const float right = bounds.getWidth() - 10.0f;

    drawText (g, lines[0], font, ink, { 10.0f, 2.0f, 200.0f, 20.0f }, juce::Justification::centredLeft);
    drawText (g, lines[1], font, ink, { right - 150.0f, 2.0f, 150.0f, 20.0f }, juce::Justification::centredRight);
    drawText (g, lines[2], font, ink, { 24.0f, 22.0f, 180.0f, 20.0f }, juce::Justification::centredLeft);
    drawText (g, lines[3], font, ink, { right - 150.0f, 22.0f, 150.0f, 20.0f }, juce::Justification::centredRight);

    // Play triangle or stop square.
    const juce::Rectangle<float> icon { 11.0f, 27.0f, 9.0f, 10.0f };
    g.setColour (ink);

    if (lines[2].startsWith ("PLAY"))
    {
        juce::Path triangle;
        triangle.addTriangle (icon.getTopLeft(), icon.getBottomLeft(), { icon.getRight(), icon.getCentreY() });
        g.fillPath (triangle);
    }
    else
    {
        g.fillRect (icon.withSizeKeepingCentre (8.0f, 8.0f));
    }
}

// ============================================================================
// MainView
// ============================================================================

MainView::MainView (NzDrumProcessor& p)
    : nzProcessor (p)
{
    for (int pad = 0; pad < NzDrumProcessor::numPads; ++pad)
    {
        pads.push_back (std::make_unique<PadComponent> (nzProcessor, pad));
        pads.back()->setName ("pad " + juce::String (pad + 1));
        pads.back()->onSelect = [this] (int selected)
        {
            // In 16 levels mode the selection stays on the source pad: the pads
            // play notes, they don't pick a pad.
            if (nzProcessor.getChromaticSource() < 0)
                selectPad (selected);
        };
        addAndMakeVisible (*pads.back());
    }

    patternBar.setName ("pattern bar");
    sequencer.setName ("sequencer");
    padEditor.setName ("pad editor");
    transport.setName ("transport");
    swingSlider.setName ("swing");

    addAndMakeVisible (patternBar);
    addAndMakeVisible (sequencer);
    addAndMakeVisible (padEditor);
    addAndMakeVisible (transport);

    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 24);

    // The LCD is small: "56%" rather than "56.0%", unless there is a fraction.
    swingSlider.textFromValueFunction = [] (double value)
    {
        const bool whole = std::abs (value - std::round (value)) < 0.05;
        return juce::String (value, whole ? 0 : 1) + "%";
    };
    swingSlider.updateText();
    swingSlider.setTooltip (juce::String ("Global swing: 50 % is straight, 75 % is the heaviest shuffle"));
    addAndMakeVisible (swingSlider);

    // If the window is closed and reopened with 16 levels on, the selection
    // goes back to the pad spread across the 16.
    const int chromaticSource = nzProcessor.getChromaticSource();
    selectPad (chromaticSource >= 0 ? chromaticSource : 0);

    // The background never changes: drawn once and reused.
    setBufferedToImage (true);

    setSize (designWidth, designHeight);
    startTimerHz (30);
}

void MainView::paint (juce::Graphics& g)
{
    using namespace theme;

    g.setGradientFill (juce::ColourGradient::vertical (colour::chassisTop, 0.0f, colour::chassisBottom, (float) getHeight()));
    g.fillAll();

    for (const auto& panel : { headerPanel, padsPanel, patternPanel, sequencerPanel, editorPanel })
        drawPanel (g, panel.toFloat());

    // Logo.
    drawText (g, "NzDrum", font::logo (32.0f), colour::primary, { 24.0f, 12.0f, 120.0f, 34.0f }, juce::Justification::centredLeft);
    g.setColour (colour::logoRed);
    g.fillRoundedRectangle ({ 26.0f, 46.0f, 24.0f, 3.0f }, 1.5f);
    drawText (g, "16-PAD SAMPLER", font::label(), colour::secondary, { 56.0f, 42.0f, 120.0f, 12.0f }, juce::Justification::centredLeft);

    drawText (g, "SWING", font::label(), colour::secondary, { 190.0f, 32.0f, 46.0f, 12.0f }, juce::Justification::centredRight);

    // The selected pad glows beyond its edges, so the glow is drawn here,
    // behind it.
    if (juce::isPositiveAndBelow (selectedPad, (int) pads.size()))
    {
        const auto rubber = pads[(size_t) selectedPad]->getBounds().toFloat().withHeight (80.0f);
        drawGlow (g, rubber, 4.0f, colour::active.withAlpha (0.75f), 14);
    }
}

void MainView::resized()
{
    swingSlider.setBounds (244, 17, 102, 44);
    transport.setBounds (366, 16, 330, 46);

    patternBar.setBounds (patternPanel);
    sequencer.setBounds (sequencerPanel);
    padEditor.setBounds (editorPanel);

    const int rows = NzDrumProcessor::numPads / gridColumns;

    // MPC-style layout: pad 1 bottom left, pad 16 top right.
    for (int pad = 0; pad < NzDrumProcessor::numPads; ++pad)
    {
        const int column = pad % gridColumns;
        const int row = rows - 1 - pad / gridColumns;
        pads[(size_t) pad]->setBounds (padColumnX[column], padRowY[row], padWidth, padHeight);
    }
}

void MainView::refreshAll()
{
    for (auto& pad : pads)
        pad->refresh();

    patternBar.refresh();
    sequencer.refresh();
    padEditor.refresh();
    transport.refresh();
}

void MainView::setScale (float scale)
{
    // JUCE's default: 250 screen pixels for a full turn.
    const int pixels = juce::jmax (1, juce::roundToInt (250.0f / scale));
    swingSlider.setMouseDragSensitivity (pixels);
    padEditor.setDragSensitivity (pixels);
}

void MainView::selectPad (int pad)
{
    selectedPad = pad;
    repaint();

    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setSelected (i == pad);

    sequencer.setPad (pad);
    padEditor.setPad (pad);
}

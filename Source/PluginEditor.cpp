#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 64;
    constexpr int gridColumns = 4;
    constexpr int patternBarHeight = 36;
    constexpr int sequencerHeight = 84;
    constexpr int padEditorHeight = 150;
}

NzDrumEditor::NzDrumEditor (NzDrumProcessor& p)
    : AudioProcessorEditor (p), nzProcessor (p)
{
    for (int pad = 0; pad < NzDrumProcessor::numPads; ++pad)
    {
        pads.push_back (std::make_unique<PadComponent> (nzProcessor, pad));
        pads.back()->onSelect = [this] (int selected)
        {
            // In 16 levels mode the selection stays on the source pad: the pads
            // play notes, they don't pick a pad.
            if (nzProcessor.getChromaticSource() < 0)
                selectPad (selected);
        };
        addAndMakeVisible (*pads.back());
    }

    addAndMakeVisible (patternBar);
    addAndMakeVisible (sequencer);

    swingLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    swingLabel.setJustificationType (juce::Justification::centredRight);
    swingSlider.setTextValueSuffix (" %");
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    swingSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffff8a3d));
    swingSlider.setTooltip (juce::String ("Global swing: 50 % is straight, 75 % is the heaviest shuffle"));
    addAndMakeVisible (swingLabel);
    addAndMakeVisible (swingSlider);
    addAndMakeVisible (padEditor);

    // If the window is closed and reopened with 16 levels on, the selection
    // goes back to the pad spread across the 16.
    const int chromaticSource = nzProcessor.getChromaticSource();
    selectPad (chromaticSource >= 0 ? chromaticSource : 0);

    setSize (720, 900);
    startTimerHz (30);
}

void NzDrumEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22));

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (20, 12);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("NzDrum", header.removeFromLeft (160), juce::Justification::centredLeft);

    const auto transport = nzProcessor.getTransport();

    juce::String info;
    info << (transport.playing ? "Play" : "Stop")
         << "   " << juce::String (transport.bpm, 1) << " BPM"
         << "   beat " << juce::String (transport.ppq, 2);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (info, header, juce::Justification::centredRight);
}

void NzDrumEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop (headerHeight);
    padEditor.setBounds (area.removeFromBottom (padEditorHeight).reduced (14, 8));
    sequencer.setBounds (area.removeFromBottom (sequencerHeight).reduced (14, 4));
    patternBar.setBounds (area.removeFromBottom (patternBarHeight).reduced (14, 2));

    // Swing sits in the middle of the header, between the name and the transport.
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (20, 18);
    header.removeFromLeft (170);
    auto swingArea = header.removeFromLeft (230);
    swingLabel.setBounds (swingArea.removeFromLeft (50));
    swingSlider.setBounds (swingArea);
    auto grid = area.reduced (14, 0);

    const int rows = NzDrumProcessor::numPads / gridColumns;
    const int cellWidth = grid.getWidth() / gridColumns;
    const int cellHeight = grid.getHeight() / rows;

    // MPC-style layout: pad 1 bottom left, pad 16 top right.
    for (int pad = 0; pad < NzDrumProcessor::numPads; ++pad)
    {
        const int column = pad % gridColumns;
        const int rowFromBottom = pad / gridColumns;
        const int row = rows - 1 - rowFromBottom;

        pads[(size_t) pad]->setBounds (grid.getX() + column * cellWidth,
                                       grid.getY() + row * cellHeight,
                                       cellWidth, cellHeight);
    }
}

void NzDrumEditor::timerCallback()
{
    for (auto& pad : pads)
        pad->refresh();

    patternBar.refresh();
    sequencer.refresh();
    padEditor.refresh();

    // The header shows the transport, which changes all the time.
    repaint (getLocalBounds().removeFromTop (headerHeight));
}

void NzDrumEditor::selectPad (int pad)
{
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setSelected (i == pad);

    sequencer.setPad (pad);
    padEditor.setPad (pad);
}

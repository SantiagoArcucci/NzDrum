#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 64;
    constexpr int gridColumns = 4;
    constexpr int sequencerHeight = 72;
}

NzDrumEditor::NzDrumEditor (NzDrumProcessor& p)
    : AudioProcessorEditor (p), nzProcessor (p)
{
    for (int pad = 0; pad < NzDrumProcessor::numPads; ++pad)
    {
        pads.push_back (std::make_unique<PadComponent> (nzProcessor, pad));
        pads.back()->onSelect = [this] (int selected) { selectPad (selected); };
        addAndMakeVisible (*pads.back());
    }

    addAndMakeVisible (sequencer);
    selectPad (0);

    setSize (640, 680);
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
         << "   negra " << juce::String (transport.ppq, 2);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (info, header, juce::Justification::centredRight);
}

void NzDrumEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop (headerHeight);
    sequencer.setBounds (area.removeFromBottom (sequencerHeight).reduced (14, 4));
    auto grid = area.reduced (14, 0);

    const int rows = NzDrumProcessor::numPads / gridColumns;
    const int cellWidth = grid.getWidth() / gridColumns;
    const int cellHeight = grid.getHeight() / rows;

    // Disposición tipo MPC: el pad 1 abajo a la izquierda, el 16 arriba a la derecha.
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

    sequencer.refresh();

    // El encabezado muestra el transporte, que cambia todo el tiempo.
    repaint (getLocalBounds().removeFromTop (headerHeight));
}

void NzDrumEditor::selectPad (int pad)
{
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setSelected (i == pad);

    sequencer.setPad (pad);
}

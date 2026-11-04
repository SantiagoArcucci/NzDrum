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
        pads.back()->onSelect = [this] (int selected) { selectPad (selected); };
        addAndMakeVisible (*pads.back());
    }

    addAndMakeVisible (patternBar);
    addAndMakeVisible (sequencer);

    swingLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    swingLabel.setJustificationType (juce::Justification::centredRight);
    swingSlider.setTextValueSuffix (" %");
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    swingSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffff8a3d));
    swingSlider.setTooltip (juce::String::fromUTF8 ("Swing global: 50 % es recto, 75 % es el shuffle más marcado"));
    addAndMakeVisible (swingLabel);
    addAndMakeVisible (swingSlider);
    addAndMakeVisible (padEditor);
    selectPad (0);

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
         << "   negra " << juce::String (transport.ppq, 2);

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

    // El swing va en el medio del encabezado, entre el nombre y el transporte.
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (20, 18);
    header.removeFromLeft (170);
    auto swingArea = header.removeFromLeft (230);
    swingLabel.setBounds (swingArea.removeFromLeft (50));
    swingSlider.setBounds (swingArea);
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

    patternBar.refresh();
    sequencer.refresh();
    padEditor.refresh();

    // El encabezado muestra el transporte, que cambia todo el tiempo.
    repaint (getLocalBounds().removeFromTop (headerHeight));
}

void NzDrumEditor::selectPad (int pad)
{
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setSelected (i == pad);

    sequencer.setPad (pad);
    padEditor.setPad (pad);
}

#include "PadComponent.h"

namespace
{
    const juce::Colour padColour { 0xff2a2d35 };
    const juce::Colour accentColour { 0xffff8a3d };
    const juce::Colour textColour { 0xffe8e8ea };
    constexpr float uiVelocity = 0.8f;
}

PadComponent::PadComponent (NzDrumProcessor& processor, int padIndex)
    : nzProcessor (processor), pad (padIndex)
{
    for (auto* button : { &previousButton, &loadButton, &nextButton })
        addAndMakeVisible (button);

    previousButton.setTooltip ("Archivo anterior de la carpeta");
    nextButton.setTooltip ("Archivo siguiente de la carpeta");
    loadButton.setTooltip ("Elegir un archivo");

    previousButton.onClick = [this] { nzProcessor.stepSample (pad, -1); };
    nextButton.onClick = [this] { nzProcessor.stepSample (pad, +1); };
    loadButton.onClick = [this] { openFileChooser(); };

    lastTriggerCount = nzProcessor.getTriggerCount (pad);
}

void PadComponent::refresh()
{
    bool needsRepaint = false;

    const auto file = nzProcessor.getPadFile (pad);
    const auto newName = file.getFileNameWithoutExtension();

    if (newName != fileName)
    {
        fileName = newName;
        needsRepaint = true;
    }

    const bool nowLoading = nzProcessor.isLoading (pad);

    if (nowLoading != loading)
    {
        loading = nowLoading;
        needsRepaint = true;
    }

    const bool hasFile = file != juce::File();
    previousButton.setEnabled (hasFile);
    nextButton.setEnabled (hasFile);

    const auto count = nzProcessor.getTriggerCount (pad);

    if (count != lastTriggerCount)
    {
        lastTriggerCount = count;
        flash = 1.0f;
    }

    if (flash > 0.0f)
    {
        flash = juce::jmax (0.0f, flash - 0.15f);
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void PadComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (3.0f);

    g.setColour (padColour.interpolatedWith (accentColour, flash * 0.6f));
    g.fillRoundedRectangle (bounds, 8.0f);

    const bool highlighted = dragHover || selected;
    g.setColour (highlighted ? accentColour : padColour.brighter (0.3f));
    g.drawRoundedRectangle (bounds, 8.0f, highlighted ? 2.0f : 1.0f);

    auto textArea = getLocalBounds().reduced (12, 10);

    g.setColour (textColour.withAlpha (0.5f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (juce::String (pad + 1), textArea.removeFromTop (18), juce::Justification::topLeft);

    g.setColour (textColour);
    g.setFont (juce::FontOptions (14.0f));

    // Los literales con acentos se pasan como UTF-8 explícito: juce::String
    // interpreta un const char* como ASCII.
    juce::String label = fileName.isEmpty() ? juce::String::fromUTF8 ("Vacío") : fileName;

    if (loading)
        label = "Cargando...";

    g.drawFittedText (label, textArea.removeFromTop (40), juce::Justification::centredLeft, 2);
}

void PadComponent::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto row = area.removeFromBottom (24);
    const int width = row.getWidth() / 3;

    previousButton.setBounds (row.removeFromLeft (width).reduced (2, 0));
    loadButton.setBounds (row.removeFromLeft (width).reduced (2, 0));
    nextButton.setBounds (row.reduced (2, 0));
}

void PadComponent::mouseDown (const juce::MouseEvent&)
{
    nzProcessor.triggerFromUi (pad, uiVelocity);

    if (onSelect)
        onSelect (pad);
}

void PadComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void PadComponent::openFileChooser()
{
    const auto startFolder = nzProcessor.getPadFile (pad).getParentDirectory();

    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Elegí un sample para el pad ") + juce::String (pad + 1),
                                                   startFolder,
                                                   nzProcessor.getSupportedWildcard());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto result = fc.getResult();

                              if (nzProcessor.isSupportedFile (result))
                                  nzProcessor.loadSample (pad, result);
                          });
}

bool PadComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && nzProcessor.isSupportedFile (juce::File (files[0]));
}

void PadComponent::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void PadComponent::fileDragExit (const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void PadComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    nzProcessor.loadSample (pad, juce::File (files[0]));
    repaint();
}

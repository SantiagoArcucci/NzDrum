#include "PadComponent.h"

namespace
{
    const juce::Colour padColour { 0xff2a2d35 };
    const juce::Colour accentColour { 0xffff8a3d };
    const juce::Colour textColour { 0xffe8e8ea };
    constexpr float uiVelocity = 0.8f;
}

PadComponent::PadComponent (NzDrumProcessor& processor, int padIndex)
    : nzProcessor (processor), pad (padIndex),
      muteAttachment (processor.getParameters(), NzDrumProcessor::paramId (padIndex, "mute"), muteButton),
      soloAttachment (processor.getParameters(), NzDrumProcessor::paramId (padIndex, "solo"), soloButton)
{
    for (auto* button : { &previousButton, &loadButton, &nextButton, &muteButton, &soloButton })
        addAndMakeVisible (button);

    muteButton.setClickingTogglesState (true);
    soloButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe6c229));
    soloButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    muteButton.setTooltip ("Mute: the pad is silent");
    soloButton.setTooltip ("Solo: only soloed pads play");

    previousButton.setTooltip ("Previous file in the folder");
    nextButton.setTooltip ("Next file in the folder");
    loadButton.setTooltip ("Choose a file");

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

    const bool nowMissing = nzProcessor.isPadMissing (pad);

    if (nowMissing != missing)
    {
        missing = nowMissing;
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

    // In 16 levels mode every pad plays the source, so they follow its mute
    // and solo.
    const int source = nzProcessor.getChromaticSource();
    const bool nowChromatic = source >= 0;

    if (nowChromatic != chromatic)
    {
        chromatic = nowChromatic;
        needsRepaint = true;
    }

    const bool nowAudible = nzProcessor.isPadAudible (nowChromatic ? source : pad);

    if (nowAudible != audible)
    {
        audible = nowAudible;
        needsRepaint = true;
    }

    const auto count = nzProcessor.getTriggerCount (pad);

    if (count != lastTriggerCount)
    {
        lastTriggerCount = count;

        // A pad that doesn't sound doesn't flash either.
        if (audible)
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

    juce::String label = fileName.isEmpty() ? juce::String ("Empty") : fileName;

    // The session asked for a file that is gone: say so in red, with the
    // name, so it can be loaded again.
    if (missing)
    {
        label = "Missing: " + fileName;
        g.setColour (juce::Colour (0xffff6b5e));
    }

    if (loading)
        label = "Loading...";

    // In 16 levels the pad shows how many semitones it shifts the source.
    if (chromatic)
    {
        const int offset = NzDrumProcessor::chromaticOffsetFor (pad);
        label = (offset > 0 ? "+" : "") + juce::String (offset) + " st";
        g.setColour (offset == 0 ? accentColour : textColour);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    }

    g.drawFittedText (label, textArea.removeFromTop (40), juce::Justification::centredLeft, 2);

    // Silenced by mute or solo: darkened, but still readable.
    if (! audible)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bounds, 8.0f);
    }
}

void PadComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (20);
    soloButton.setBounds (top.removeFromRight (22));
    top.removeFromRight (4);
    muteButton.setBounds (top.removeFromRight (22));

    auto row = area.removeFromBottom (24);
    const int width = row.getWidth() / 3;

    previousButton.setBounds (row.removeFromLeft (width).reduced (2, 0));
    loadButton.setBounds (row.removeFromLeft (width).reduced (2, 0));
    nextButton.setBounds (row.reduced (2, 0));
}

void PadComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        showPadMenu();
        return;
    }

    // Same as the step row: Shift plays an accent and Option a soft hit (it
    // matters when recording, since the step gets that level).
    float velocity = uiVelocity;

    if (event.mods.isShiftDown())
        velocity = NzDrumProcessor::gainFor (StepLevel::accent);
    else if (event.mods.isAltDown())
        velocity = NzDrumProcessor::gainFor (StepLevel::low);

    nzProcessor.triggerFromUi (pad, velocity);

    if (onSelect)
        onSelect (pad);
}

void PadComponent::showPadMenu()
{
    const bool hasFile = nzProcessor.getPadFile (pad) != juce::File();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Pad " + juce::String (pad + 1));
    menu.addItem ("Copy pad (audio and settings)", hasFile, false, [this] { nzProcessor.copyPad (pad); });
    menu.addItem ("Paste pad", nzProcessor.hasCopiedPad(), false, [this] { nzProcessor.pastePad (pad); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
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

    chooser = std::make_unique<juce::FileChooser> (juce::String ("Choose a sample for pad ") + juce::String (pad + 1),
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

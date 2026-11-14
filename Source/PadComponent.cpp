#include "PadComponent.h"

namespace
{
    constexpr float uiVelocity = 0.8f;
    constexpr float rubberHeight = 80.0f;
    constexpr float stripTop = 84.0f;
    constexpr float cornerRadius = 4.0f;
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
    {
        // The strip is narrow: the full name (or what is missing) goes in the tooltip.
        if (missing)
            setTooltip ("Missing: " + nzProcessor.getPadFile (pad).getFullPathName());
        else
            setTooltip (fileName);

        repaint();
    }
}

juce::Rectangle<float> PadComponent::rubberBounds() const
{
    return getLocalBounds().toFloat().withHeight (rubberHeight);
}

void PadComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto rubber = rubberBounds();
    const bool highlighted = dragHover || selected;

    // The rubber pad. Silenced pads (mute, or another pad in solo) look flat.
    // The selected pad's glow is drawn by the parent: it goes past our edges.
    if (audible && ! highlighted)
        drawDropShadow (g, rubber, cornerRadius, juce::Colours::black.withAlpha (0.5f), 3, { 0, 2 });

    const auto top = audible ? juce::Colour (0xffd8d4cc) : juce::Colour (0xffe6e0d4);
    const auto bottom = audible ? juce::Colour (0xffb8b4ab) : juce::Colour (0xffd9d2c5);
    g.setGradientFill (juce::ColourGradient::vertical (top, rubber.getY(), bottom, rubber.getBottom()));
    g.fillRoundedRectangle (rubber, cornerRadius);

    if (flash > 0.0f)
    {
        g.setColour (colour::active.withAlpha (0.35f * flash));
        g.fillRoundedRectangle (rubber, cornerRadius);
    }

    if (audible)
    {
        drawInnerShadow (g, rubber, cornerRadius, juce::Colours::white.withAlpha (0.2f), 1, { 0, 1 });
        drawInnerShadow (g, rubber, cornerRadius, juce::Colours::black.withAlpha (0.3f), 6, { 0, -3 });
    }

    if (highlighted)
    {
        drawInnerShadow (g, rubber, cornerRadius, colour::active.withAlpha (0.18f), 18, {});
        g.setColour (colour::active);
        g.drawRoundedRectangle (rubber.reduced (1.0f), cornerRadius, 2.0f);
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (audible ? 0.85f : 0.25f));
        g.drawRoundedRectangle (rubber.reduced (0.5f), cornerRadius, 1.0f);
    }

    // In 16 levels the pad shows how many semitones it shifts the source.
    if (chromatic)
    {
        const int offset = NzDrumProcessor::chromaticOffsetFor (pad);
        drawText (g, (offset > 0 ? "+" : "") + juce::String (offset), font::bold (26.0f),
                  offset == 0 ? colour::active : colour::primary.withAlpha (0.75f),
                  rubber, juce::Justification::centred);
    }

    // The strip below: a small LCD with the number and the sample name.
    const auto strip = getLocalBounds().toFloat().withTrimmedTop (stripTop);
    g.setColour (colour::display);
    g.fillRoundedRectangle (strip, cornerRadius);
    drawInnerShadow (g, strip, cornerRadius, juce::Colours::black.withAlpha (0.5f), 2, { 0, 1 });

    drawText (g, juce::String (pad + 1).paddedLeft ('0', 2), font::mono (11.0f), colour::displayInk,
              { 6.0f, strip.getY(), 18.0f, strip.getHeight() }, juce::Justification::centredLeft);

    juce::String name = fileName.isEmpty() ? juce::String::fromUTF8 ("\xe2\x80\x94") : fileName.toUpperCase();   // —
    auto nameColour = colour::displayInk;

    // The session asked for a file that is gone: say so in red (the tooltip
    // has the path), so it can be loaded again.
    if (missing)
    {
        name = "MISSING";
        nameColour = colour::missing;
    }

    if (loading)
        name = juce::String::fromUTF8 ("LOADING\xe2\x80\xa6");

    g.setFont (font::semiBold (12.0f));
    g.setColour (nameColour);
    g.drawFittedText (name, juce::Rectangle<float> (26.0f, strip.getY(), 43.0f, strip.getHeight()).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.8f);
}

void PadComponent::resized()
{
    constexpr int keySize = 16, keyTop = 88;
    int x = 70;

    for (auto* key : { &previousButton, &nextButton, &loadButton, &muteButton, &soloButton })
    {
        key->setBounds (x, keyTop, keySize, keySize);
        x += keySize + 2;
    }
}

void PadComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        showPadMenu();
        return;
    }

    // Only the rubber plays: the strip below is for reading and its keys.
    if (! rubberBounds().contains (event.position))
        return;

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

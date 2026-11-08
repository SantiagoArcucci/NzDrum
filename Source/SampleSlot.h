#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

// An audio file already decoded into memory.
struct LoadedSample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::File file;
};

// Hands samples from the loader thread to the audio thread without locks and
// without the audio thread ever freeing memory. There is one SampleSlot per pad.
//
// Protocol, with a clear owner for every pointer at every moment:
//   1. The loader thread calls publish(): it leaves the new sample in `pending`.
//   2. The audio thread calls acquire() at the start of each block: if there is
//      a new one, it becomes active and the previous one goes to the retired
//      queue.
//   3. The UI thread calls collectGarbage() now and then and deletes whatever
//      was retired. That way `delete` never runs on the audio thread.
class SampleSlot
{
public:
    SampleSlot() = default;

    ~SampleSlot()
    {
        // Destroyed with the plugin, once audio is no longer running.
        delete pending.exchange (nullptr);
        delete active;
        collectGarbage();
    }

    // Loader thread.
    void publish (std::unique_ptr<LoadedSample> sample)
    {
        // exchange is atomic: if it returns something, the audio thread had not
        // taken it yet, so it is ours and we can safely delete it here.
        delete pending.exchange (sample.release(), std::memory_order_acq_rel);
    }

    // Audio thread. Returns the active sample (or nullptr if the pad is
    // empty) and reports in `changed` whether it was just replaced.
    const LoadedSample* acquire (bool& changed) noexcept
    {
        changed = false;

        const bool hasNew = pending.load (std::memory_order_acquire) != nullptr;
        const bool canRetire = active == nullptr || retiredFifo.getFreeSpace() > 0;

        // If the retired queue were full, we wait for the next block rather
        // than free memory here.
        if (hasNew && canRetire)
        {
            if (auto* fresh = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (active != nullptr)
                {
                    const auto scope = retiredFifo.write (1);
                    scope.forEach ([this] (int index) { retired[(size_t) index] = active; });
                }

                active = fresh;
                changed = true;
            }
        }

        return active;
    }

    // UI thread.
    void collectGarbage()
    {
        const auto scope = retiredFifo.read (retiredFifo.getNumReady());
        scope.forEach ([this] (int index)
        {
            delete retired[(size_t) index];
            retired[(size_t) index] = nullptr;
        });
    }

private:
    std::atomic<LoadedSample*> pending { nullptr };
    LoadedSample* active = nullptr;     // only touched by the audio thread

    // Lock-free queue with one producer (audio) and one consumer (UI).
    static constexpr int retiredCapacity = 8;
    juce::AbstractFifo retiredFifo { retiredCapacity };
    std::array<LoadedSample*, retiredCapacity> retired {};

    JUCE_DECLARE_NON_COPYABLE (SampleSlot)
};

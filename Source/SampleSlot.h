#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

// Un archivo de audio ya decodificado en memoria.
struct LoadedSample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::File file;
};

// Pasa samples del hilo de carga al hilo de audio sin locks y sin que el audio
// tenga que liberar memoria. Hay un SampleSlot por pad.
//
// Protocolo, con un dueño claro para cada puntero en cada momento:
//   1. El hilo de carga llama a publish(): deja el sample nuevo en `pending`.
//   2. El hilo de audio llama a acquire() al inicio de cada bloque: si hay uno
//      nuevo, lo vuelve activo y manda el anterior a la cola de descarte.
//   3. El hilo de UI llama a collectGarbage() de vez en cuando y borra lo
//      descartado. Así el `delete` nunca corre en el hilo de audio.
class SampleSlot
{
public:
    SampleSlot() = default;

    ~SampleSlot()
    {
        // Se destruye con el plugin, cuando el audio ya no corre.
        delete pending.exchange (nullptr);
        delete active;
        collectGarbage();
    }

    // Hilo de carga.
    void publish (std::unique_ptr<LoadedSample> sample)
    {
        // exchange es atómico: si devuelve algo, el audio todavía no lo había
        // tomado, así que es nuestro y lo podemos borrar acá sin riesgo.
        delete pending.exchange (sample.release(), std::memory_order_acq_rel);
    }

    // Hilo de audio. Devuelve el sample activo (o nullptr si el pad está
    // vacío) y avisa en `changed` si se acaba de reemplazar.
    const LoadedSample* acquire (bool& changed) noexcept
    {
        changed = false;

        const bool hasNew = pending.load (std::memory_order_acquire) != nullptr;
        const bool canRetire = active == nullptr || retiredFifo.getFreeSpace() > 0;

        // Si la cola de descarte estuviera llena, esperamos al próximo bloque
        // antes que liberar memoria acá.
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

    // Hilo de UI.
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
    LoadedSample* active = nullptr;     // solo lo toca el hilo de audio

    // Cola de un productor (audio) y un consumidor (UI), sin locks.
    static constexpr int retiredCapacity = 8;
    juce::AbstractFifo retiredFifo { retiredCapacity };
    std::array<LoadedSample*, retiredCapacity> retired {};

    JUCE_DECLARE_NON_COPYABLE (SampleSlot)
};

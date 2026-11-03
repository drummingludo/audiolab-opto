// Paramètres du plugin « Audio Lab Opto » (compresseur optique d'Audio Lab, inspiré du LA-2A)
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace audiolab::params
{
inline constexpr const char* gain    = "gain";
inline constexpr const char* peakRed = "peakRed";
inline constexpr const char* mode    = "mode";
inline constexpr const char* mix     = "mix";
inline constexpr const char* bypass  = "bypass";
// 2.0.0 (réglages avancés)
inline constexpr const char* oversampling = "oversampling";
inline constexpr const char* emphasis     = "emphasis";
inline constexpr int oversamplingDefault = 1;   // 2× : repliement < −80 dB (−97 dB au réglage de référence)

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto fl = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit,
                   std::function<String (float, int)> txt)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (std::move (txt))));
    };
    // Mêmes plages que les potentiomètres d'Audio Lab (opKnobs)
    fl (gain, "Gain", NormalisableRange<float> (-20, 30, 0.1f), 0, "dB",
        [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; });
    fl (peakRed, "Peak Reduction", NormalisableRange<float> (0, 100, 0.1f), 40, "",
        [] (float v, int) { return String (roundToInt (v)); });
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { mode, 1 }, "Mode", StringArray { "Compress", "Limit" }, 0));
    fl (mix, "Dry/Wet", NormalisableRange<float> (0, 100, 0.1f), 100, "%",
        [] (float v, int) { return String (roundToInt (v)) + " %"; });
    l.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));
    // 2.0.0 : suréchantillonnage du moteur (Off = aucune latence) ; emphase aiguë du side-chain (R37 de l'original)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { oversampling, 2 }, "Oversampling",
        StringArray { "Off", String::fromUTF8 ("2×"), String::fromUTF8 ("4×"), String::fromUTF8 ("8×") }, oversamplingDefault));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { emphasis, 2 }, "SC Emphasis", NormalisableRange<float> (0, 100, 0.1f), 0,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })));
    return l;
}
} // namespace audiolab::params

// Tests du plugin hors DAW : sauvegarde / rappel, anciennes sessions, bypass et Dry/Wet recalés sur la latence,
// mono, latence par mode de suréchantillonnage, bascule sans clic, plugin = moteur testé.
#include "PluginProcessor.h"
#include <cstdio>

static bool OK = true;
static void check (bool c, const char* msg) { std::printf ("  %s %s\n", c ? "OK   " : "ÉCHEC", msg); OK &= c; }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    using namespace audiolab;
    auto set = [] (AudioLabOptoProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    auto sine = [] (juce::AudioBuffer<float>& b, int64_t& pos, double amp, double f = 1000)
    {
        for (int i = 0; i < b.getNumSamples(); ++i, ++pos)
            for (int c = 0; c < b.getNumChannels(); ++c)
                b.setSample (c, i, (float) (amp * std::sin (2 * juce::MathConstants<double>::pi * f * pos / 48000.0)));
    };
    char msg[200];

    std::puts ("\nPlugin Lab Opto 2.0");
    {
        AudioLabOptoProcessor a; set (a, params::gain, 7.3f); set (a, params::peakRed, 63); set (a, params::mode, 1);
        set (a, params::mix, 42); set (a, params::oversampling, 3); set (a, params::emphasis, 55);
        a.ui.setProperty ("meter", 2, nullptr); a.ui.setProperty ("adv", true, nullptr);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        AudioLabOptoProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b.getParam (params::gain) - 7.3f) < 1e-4 && std::abs (b.getParam (params::peakRed) - 63) < 1e-4
               && b.getParam (params::mode) > 0.5f && std::abs (b.getParam (params::mix) - 42) < 1e-4
               && juce::roundToInt (b.getParam (params::oversampling)) == 3 && std::abs (b.getParam (params::emphasis) - 55) < 1e-4
               && (int) b.ui.getProperty ("meter") == 2 && (bool) b.ui.getProperty ("adv"),
               "sauvegarde / rappel de la session (réglages, suréchantillonnage, emphase, affichage du VU, panneau avancé)");
    }
    {
        // session de la version 1.2 : pas de « oversampling » ni d'« emphasis » → valeurs par défaut (2×, 0 %), le reste relu
        AudioLabOptoProcessor a; set (a, params::gain, 9.0f); set (a, params::peakRed, 70);
        auto st = a.apvts.copyState();
        for (int i = st.getNumChildren(); --i >= 0;)
        {
            const auto id = st.getChild (i).getProperty ("id").toString();
            if (id == params::oversampling || id == params::emphasis) st.removeChild (i, nullptr);
        }
        juce::MemoryBlock mb;
        if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        AudioLabOptoProcessor b; set (b, params::oversampling, 3); set (b, params::emphasis, 80);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (juce::roundToInt (b.getParam (params::oversampling)) == params::oversamplingDefault && b.getParam (params::emphasis) < 1e-4f
               && std::abs (b.getParam (params::gain) - 9.0f) < 1e-4 && std::abs (b.getParam (params::peakRed) - 70) < 1e-4,
               "session 1.2 (sans les nouveaux réglages) : rouverte, suréchantillonnage 2x et emphase 0 % par défaut");
    }
    {
        // latence annoncée par mode : 0 / 69 / 76 / 78 échantillons
        int lat[4];
        for (int os = 0; os < 4; ++os)
        {
            AudioLabOptoProcessor p; set (p, params::oversampling, (float) os);
            p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512); lat[os] = p.getLatencySamples();
        }
        std::snprintf (msg, sizeof msg, "latence annoncée : Off %d, 2x %d, 4x %d, 8x %d échantillons", lat[0], lat[1], lat[2], lat[3]);
        check (lat[0] == 0 && lat[1] == 69 && lat[2] == 76 && lat[3] == 78, msg);
    }
    for (int os : { 0, 1 })
    {
        // bypass : la sortie est exactement l'entrée retardée de la latence annoncée
        AudioLabOptoProcessor p; set (p, params::oversampling, (float) os);
        p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
        set (p, params::bypass, 1); set (p, params::peakRed, 90);
        const int L = p.getLatencySamples();
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; int64_t pos = 0;
        std::vector<float> in, out;
        for (int k = 0; k < 50; ++k) { sine (b, pos, 0.5); for (int i = 0; i < 512; ++i) in.push_back (b.getSample (0, i)); p.processBlock (b, m);
            for (int i = 0; i < 512; ++i) out.push_back (b.getSample (1, i)); }
        bool same = true;
        for (size_t i = 6 * 512; i < out.size(); ++i) same &= juce::exactlyEqual (out[i], in[i - (size_t) L]);
        std::snprintf (msg, sizeof msg, "bypass (%s) : sortie = entrée retardée de %d échantillons, au bit près", os ? "2x" : "Off", L);
        check (same, msg);
    }
    {
        // Dry/Wet 0 % : son direct recalé (aucun filtre en peigne quand on mélange)
        AudioLabOptoProcessor p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
        set (p, params::mix, 0); set (p, params::peakRed, 100); set (p, params::gain, 20);
        const int L = p.getLatencySamples();
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; int64_t pos = 0; float err = 0;
        std::vector<float> in, out;
        for (int k = 0; k < 40; ++k) { sine (b, pos, 0.5); for (int i = 0; i < 512; ++i) in.push_back (b.getSample (0, i)); p.processBlock (b, m);
            for (int i = 0; i < 512; ++i) out.push_back (b.getSample (0, i)); }
        for (size_t i = 6 * 512; i < out.size(); ++i) err = std::max (err, std::abs (out[i] - in[i - (size_t) L]));
        std::snprintf (msg, sizeof msg, "Dry/Wet à 0 %% (2x) : son direct retardé de %d échantillons (écart %.1e)", L, err);
        check (err < 1e-7f, msg);
    }
    {
        // Dry/Wet 50 % sans compression (PR 0) : sec et traité en phase, aucun peigne. Trois sinus séparément :
        // en phase, 0,707 + 0,707 → +3,01 dB (moins l'effet du coupe-haut 18 kHz, < 0,02 dB jusqu'à 3 kHz) ;
        // décalés de 69 échantillons, on aurait par exemple −8 dB à 1 kHz.
        double worst = 0; char row[120] = ""; int pos = 0;
        for (double f : { 300.0, 1000.0, 3000.0 })
        {
            AudioLabOptoProcessor p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
            set (p, params::mix, 50); set (p, params::peakRed, 0);
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; int64_t pos2 = 0;
            double ein = 0, eout = 0;
            for (int k = 0; k < 100; ++k)
            {
                sine (b, pos2, 0.05, f);
                if (k > 20) for (int i = 0; i < 512; ++i) ein += b.getSample (0, i) * b.getSample (0, i);
                p.processBlock (b, m);
                if (k > 20) for (int i = 0; i < 512; ++i) eout += b.getSample (0, i) * b.getSample (0, i);
            }
            const double g = 10 * std::log10 (eout / ein);
            worst = std::max (worst, std::abs (g - 3.01));
            pos += std::snprintf (row + pos, sizeof row - (size_t) pos, " %g Hz %+.2f dB", f, g);
        }
        std::snprintf (msg, sizeof msg, "Dry/Wet 50 %%, sans compression, sec et traité en phase :%s (+3.01 attendus)", row);
        check (worst < 0.1, msg);
    }
    {
        AudioLabOptoProcessor p; p.setPlayConfigDetails (1, 1, 48000, 300); p.prepareToPlay (48000, 300);
        set (p, params::peakRed, 90);
        juce::AudioBuffer<float> b (1, 300); juce::MidiBuffer m; int64_t pos = 0; float mx = 0, gr = 0;
        for (int k = 0; k < 400; ++k) { sine (b, pos, 0.5); p.processBlock (b, m); mx = std::max (mx, b.getMagnitude (0, 0, 300));
            gr = std::min (gr, p.readMeters().grDb); }
        check (std::isfinite (mx) && mx > 0.01f && gr < -6, "mono (1 voie, blocs de 300) : compression active, sortie saine");
    }
    {
        // VU de sortie : sinus de crête −18 dBFS → −18 dBFS (convention AES17), donc 0 VU en mode +4
        AudioLabOptoProcessor p; p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
        set (p, params::mix, 0);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; int64_t pos = 0;
        for (int k = 0; k < 100; ++k) { sine (b, pos, std::pow (10.0, -18 / 20.0)); p.processBlock (b, m); }
        const float o = p.readMeters().outDb;
        std::snprintf (msg, sizeof msg, "niveau de sortie pour le VU : sinus de crête −18 dBFS lu à %.2f dBFS (0 VU en mode +4)", o);
        check (std::abs (o + 18) < 0.1f, msg);
    }
    {
        // Le plugin sonne exactement comme le moteur testé par le banc qualité (Limit, PR 80, Gain +8, emphase 40 %, Mix 100 %, 2x)
        AudioLabOptoProcessor p; p.setPlayConfigDetails (2, 2, 48000, 256); p.prepareToPlay (48000, 256);
        set (p, params::mode, 1); set (p, params::peakRed, 80); set (p, params::gain, 8); set (p, params::emphasis, 40);
        OptoChain chain; chain.prepare (48000, params::oversamplingDefault);
        OptoParams fp; fp.gainDb = 8; fp.peakRed = 80; fp.limit = true; fp.emphasis = 0.4; chain.setParams (fp);
        juce::AudioBuffer<float> b (2, 256), ref (2, 256), out (2, 256); juce::MidiBuffer m; int64_t pos = 0, pr = 0; float err = 0;
        for (int k = 0; k < 100; ++k)
        {
            sine (b, pos, 0.3); sine (ref, pr, 0.3); p.processBlock (b, m);
            chain.process (ref.getReadPointer (0), ref.getReadPointer (1), out.getWritePointer (0), out.getWritePointer (1), 256);
            if (k > 10) for (int i = 0; i < 256; ++i) err = std::max (err, std::abs (b.getSample (0, i) - out.getSample (0, i)));
        }
        std::snprintf (msg, sizeof msg, "Limit + emphase : le plugin = le moteur du banc qualité (écart max %.1e)", err);
        check (err < 1e-6f, msg);
    }
    {
        // bascule du suréchantillonnage pendant la lecture : fondu (pas de clic), nouvelle latence annoncée
        AudioLabOptoProcessor p; p.setPlayConfigDetails (2, 2, 48000, 480); p.prepareToPlay (48000, 480);
        set (p, params::peakRed, 70);
        juce::AudioBuffer<float> b (2, 480); juce::MidiBuffer m; int64_t pos = 0;
        float prev = 0, jump = 0;
        for (int k = 0; k < 200; ++k)
        {
            if (k == 100) set (p, params::oversampling, 0);
            sine (b, pos, 0.3, 200); p.processBlock (b, m);
            for (int i = 0; i < 480; ++i) { const float v = b.getSample (0, i); if (k > 50) jump = std::max (jump, std::abs (v - prev)); prev = v; }
        }
        // pente maximale d'un sinus 200 Hz d'amplitude ≈ 0,3 : 2π·200/48000·0,3 ≈ 0,008 par échantillon
        std::snprintf (msg, sizeof msg, "bascule 2x → Off en lecture : saut max %.4f par échantillon (sinus seul : 0.008), latence %d", jump, p.getLatencySamples());
        check (jump < 0.012f && p.getLatencySamples() == 0, msg);
    }
    std::printf ("\nRÉSULTAT : %s\n", OK ? "tout est conforme" : "ÉCHECS ci-dessus");
    return OK ? 0 : 1;
}

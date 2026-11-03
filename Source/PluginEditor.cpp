#include "PluginEditor.h"

using namespace audiolab;
using juce::String;

static String U (const char* s) { return String::fromUTF8 (s); }

static const char* helpGain = "Output make-up (tube stage). Compression lowers the level: bring it back up here. "
                              "Always compare at equal loudness (bypass): louder sounds \"better\".";
static const char* helpPR   = "Peak Reduction: a single knob that sets both the threshold and the amount of compression. "
                              "The further you turn it, the harder the optical cell works. The response is slow and musical.";
static const char* helpMode = "Compress: gentle ratio (3.5:1). Limit: high ratio (12:1), to hold the peaks.";
static const char* helpMtr  = "What the needle shows: GR = gain reduction; +4 / +10 = output level "
                              "(0 VU = −18 / −12 dBFS).";
static const char* helpMix  = "Dry/Wet: blends the processed sound with the direct sound (parallel compression).";
static const char* helpOs   = "Oversampling: the tube stage runs at 2, 4 or 8 times the session rate, so its harmonics "
                              "don't fold back as aliasing. 2× is the default; Off = no latency, lowest CPU.";
static const char* helpEmph = "SC Emphasis (the R37 trim of the original): the optical cell reacts more to the highs, "
                              "up to +9 dB above 3 kHz. 0 % = flat. Useful to tame sibilance or bright cymbals.";

AudioLabOptoEditor::AudioLabOptoEditor (AudioLabOptoProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      vu (theme, [&p] { const auto m = p.readMeters(); return VuFace::Reading { m.grDb, m.outDb, m.active }; })
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& ap = proc.apvts;
    auto prm = [&] (const char* id) -> juce::RangedAudioParameter& { return *ap.getParameter (id); };

    // couleurs de la charte : gain = teal secondaire, Peak Reduction (seuil + ratio) = teal, Mix = teal secondaire
    kGain = std::make_unique<ArcKnob> (theme, prm (params::gain), "Gain", "dB", Family::teal2, ArcKnob::Size::md,
        [] (float v) { return (v > 0 ? "+" : "") + String (v, 1); }, 0.0f, U (helpGain));
    kGain->hint = U ("output make-up");
    kPR = std::make_unique<ArcKnob> (theme, prm (params::peakRed), "Peak Reduction", "%", Family::teal, ArcKnob::Size::md,
        [] (float v) { return String (juce::roundToInt (v)); }, 0.0f, U (helpPR));
    kPR->hint = U ("linked threshold + ratio");
    kMix = std::make_unique<ArcKnob> (theme, prm (params::mix), "Mix", "%", Family::teal2, ArcKnob::Size::xs,
        [] (float v) { return String (juce::roundToInt (v)); }, 0.0f, U (helpMix));

    vu.setModes ({ -18.0f, -12.0f }, { "OUTPUT  +4", "OUTPUT  +10" }, false);
    const int mtr = juce::jlimit (0, 2, (int) proc.ui.getProperty ("meter", 0));
    meterSeg.setSelected (mtr); vu.setMode (mtr);
    meterSeg.onSelect = [this] (int i) { vu.setMode (i); proc.ui.setProperty ("meter", i, nullptr); };
    modeSeg.attach (prm (params::mode));

    bypassAtt = std::make_unique<juce::ParameterAttachment> (prm (params::bypass), [this] (float v) {
        bypassed = v > 0.5f; refreshState(); }, nullptr);
    bypassAtt->sendInitialUpdate();

    // Réglages avancés (2.0.0) : suréchantillonnage (latence affichée à côté), emphase aiguë du side-chain
    osSeg.onSurface = true;
    osSeg.attach (prm (params::oversampling));
    osSeg.setTooltip (U (helpOs));
    // libellé dessiné dans le panneau (comme « Oversampling ») : les deux commandes commencent au même x
    rowEmph = std::make_unique<SliderRow> (theme, prm (params::emphasis), "", Family::teal2,
        [] (float v) { return String (juce::roundToInt (v)) + " %"; });
    rowEmph->setTitle ("SC Emphasis");
    rowEmph->setTooltip (U (helpEmph));
    advBtn.setTooltip (U ("Show / hide Oversampling and SC Emphasis"));
    advBtn.onClick = [this] { setAdvanced (! advOpen, true); };
    advItems = { &osSeg, rowEmph.get() };

    for (auto* c : std::initializer_list<juce::Component*> { kGain.get(), kPR.get(), kMix.get(), &vu, &meterSeg, &modeSeg, &advBtn })
        content.addAndMakeVisible (c);
    for (auto* c : advItems) content.addAndMakeVisible (c);
    proc.apvts.addParameterListener (params::oversampling, this);
    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));   // lu avant les limites
    setAdvanced ((bool) proc.ui.getProperty ("adv", false), false);   // panneau avancé caché par défaut
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / currentH());
    setResizeLimits (baseW * 6 / 10, currentH() * 6 / 10, baseW * 2, currentH() * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (currentH() * sc));
    refreshState();
}

AudioLabOptoEditor::~AudioLabOptoEditor()
{
    proc.apvts.removeParameterListener (params::oversampling, this);
    cancelPendingUpdate();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AudioLabOptoEditor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }
void AudioLabOptoEditor::handleAsyncUpdate() { content.repaint(); }

// Ouvre / ferme le panneau « Advanced settings » : la fenêtre grandit ou rétrécit de 108 px (à l'échelle)
void AudioLabOptoEditor::setAdvanced (bool open, bool resizeWindow)
{
    advOpen = open;
    proc.ui.setProperty ("adv", open, nullptr);
    advBtn.open = open; advBtn.repaint();
    for (auto* c : advItems) c->setVisible (open);
    if (resizeWindow)
    {
        const double s = (double) getWidth() / baseW;   // échelle actuelle, lue avant de changer les limites
        getConstrainer()->setFixedAspectRatio ((double) baseW / currentH());
        setResizeLimits (baseW * 6 / 10, currentH() * 6 / 10, baseW * 2, currentH() * 2);
        setSize (juce::roundToInt (baseW * s), juce::roundToInt (currentH() * s));
    }
    resized();
    content.repaint();
}

// « Latency 69 smp (1.44 ms) » pour le choix affiché ; « Off » : aucune latence
juce::String AudioLabOptoEditor::latencyText() const
{
    const int n = proc.latencyForChoice (osSeg.getSelected());
    if (n == 0) return U ("No latency");
    const double ms = 1000.0 * n / proc.sampleRateForDisplay();
    return "Latency " + String (n) + " smp (" + String (ms, ms < 10 ? 2 : 1) + " ms)";
}

void AudioLabOptoEditor::refreshState()
{
    // Charte V2 : en bypass, la lampe du VU s'éteint ; les réglages restent accessibles
    vu.setLampOff (bypassed);
}

void AudioLabOptoEditor::resized()
{
    const float s = (float) getWidth() / baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, baseW, currentH());
    proc.ui.setProperty ("scale", (double) s, nullptr);
}

void AudioLabOptoEditor::Content::paint (juce::Graphics& g)
{
    const auto& p = ed.theme.pal();
    g.fillAll (p.bg);
    if (! ed.advOpen) return;
    // panneau des réglages avancés (« Surface », coins 12, marge intérieure 16) : deux lignes de 26 px
    g.setColour (p.surface); g.fillRoundedRectangle (24, 284, 526, 92, 12);
    g.setColour (p.text2); g.setFont (ed.theme.ui (12));
    g.drawText ("Oversampling", juce::Rectangle<float> (40, 300, 90, 26), juce::Justification::centredLeft, false);
    g.drawText ("SC Emphasis", juce::Rectangle<float> (40, 334, 90, 26), juce::Justification::centredLeft, false);
    g.setColour (p.soft);
    g.drawText (ed.latencyText(), juce::Rectangle<float> ((float) ed.osSeg.getRight() + 16, 300, 200, 26), juce::Justification::centredLeft, false);
}

void AudioLabOptoEditor::Content::resized()
{
    auto& e = ed;
    // Rangée principale (y 24 → 224) : Gain (centre x 75,5) | VU 270 × 200 | Peak Reduction (centre x 498,5)
    e.kGain->setBounds (4, 68, 143, 150);
    e.vu.setBounds (152, 24, 270, 200);
    e.kPR->setBounds (427, 68, 143, 150);
    // Rangée du bas (y 240) : affichage du VU à gauche, Compress / Limit sous le VU, petit Mix à droite
    e.meterSeg.setBounds (24, 240, juce::roundToInt (e.meterSeg.idealWidth()), 28);
    e.meterSeg.setTooltip (U (helpMtr));
    const int mw = juce::roundToInt (e.modeSeg.idealWidth());
    e.modeSeg.setBounds (152 + 135 - mw / 2, 240, mw, 28);
    e.modeSeg.setTooltip (U (helpMode));
    const int kw = juce::roundToInt (e.kMix->idealWidth());
    e.kMix->setBounds (baseW - 24 - kw, 240, kw, 28);
    // flèche « Advanced settings » en haut à gauche, au-dessus de Gain (la rangée du bas est pleine) ; 14 px avant le VU
    e.advBtn.setBounds (0, 20, juce::roundToInt (e.advBtn.idealWidth()), 28);
    // panneau avancé (y 284 → 376) : Oversampling + latence, puis SC Emphasis
    e.osSeg.setBounds (40 + 98, 300, juce::roundToInt (e.osSeg.idealWidth()), 26);
    e.rowEmph->setBounds (40 + 98 - 78, 334, 78 + 210 + 58, 26);   // piste à partir de x 138, comme le sélecteur
}

// Moteur du compresseur optique d'Audio Lab (classe OptoDSP du worklet « al-opto »),
// recopié ligne à ligne en C++ : mêmes formules, même ordre des calculs, en double précision.
// Cellule EL/CdS (attaque ~10 ms, relâchement en deux temps 60 ms puis 0,5–5 s selon la densité),
// étage à tubes (tanh), transfo (coupe-bas 25 Hz, coupe-haut 18 kHz).
// Indépendant de JUCE : testé hors plugin contre le vrai code web (tests/verify_dsp.py).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace audiolab
{
inline constexpr double kPi = 3.14159265358979323846;

struct OptoParams
{
    double gainDb = 0, peakRed = 40;   // Gain (dB), Peak Reduction (0–100)
    bool limit = false;                // Compress / Limit
    double emphasis = 0, tube = 0.3;   // fixés comme dans Audio Lab (optoParams())
    bool bypass = false;               // fondu interne (le plugin garde false : bypass géré par l'hôte)
};

class OptoCore
{
public:
    // Équivaut à « new OptoDSP(sr) » puis setParams({bypass:0}) (constructeur d'OptoProc)
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        p = OptoParams(); p.bypass = true;   // valeurs initiales d'OptoDSP (bypass:1)
        fast = dens = slow = 0;
        sx1 = sx2 = sy1 = sy2 = 0;
        hpLx1 = hpLy1 = hpRx1 = hpRy1 = 0; lpL = lpR = 0;
        gLin = 1; gTarget = 1; wet = 0; wetTarget = 0;
        aAtt = coef (0.010); aRelF = coef (0.060);
        dAtt = coef (0.5); dRel = coef (2.0);
        hpA = (1 / (2 * kPi * 25)) / ((1 / (2 * kPi * 25)) + 1 / sr);
        lpB = 1 - std::exp (-2 * kPi * 18000 / sr);
        smooth = std::exp (-1 / (0.02 * sr));
        setParams (p);
        OptoParams q = p; q.bypass = false;
        setParams (q);
        blockPos = 0;
        grMinShared.store (0); pkShared.store (0);
    }

    void setParams (const OptoParams& np)
    {
        p = np;
        thr = 10 - 0.55 * p.peakRed;
        ratio = p.limit ? 12 : 3.5;
        knee = 12;
        gTarget = std::pow (10.0, p.gainDb / 20);
        d = 1 + 4 * p.tube;
        wetTarget = p.bypass ? 0 : 1;
        // high shelf RBJ, S=1
        const double G = 9 * p.emphasis, A = std::pow (10.0, G / 40), w0 = 2 * kPi * 3000 / sr;
        const double c = std::cos (w0), s = std::sin (w0), al = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) - (A - 1) * c + sa;
        b0 = A * ((A + 1) + (A - 1) * c + sa) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        b2 = A * ((A + 1) + (A - 1) * c - sa) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
        a2 = ((A + 1) - (A - 1) * c - sa) / a0;
    }

    // Traitement : le web calcule le relâchement lent une fois par bloc de 128 échantillons ;
    // on refait exactement pareil quelle que soit la taille des blocs envoyés par le logiciel.
    void process (const float* L, const float* R, float* oL, float* oR, int n)
    {
        const double half = knee / 2, slope = (1 / ratio - 1);
        double grMin = 0, pk = 0;
        for (int i = 0; i < n; ++i)
        {
            if (blockPos == 0)
            {
                // relâchement lent dépendant de la densité (taux de bloc)
                const double relS = 0.5 + 4.5 * std::min (1.0, dens * 4);
                aRelS = coef (relS);
            }
            if (++blockPos == 128) blockPos = 0;

            const double l = L[i], r = R[i];
            // ----- sidechain -----
            const double x = (l + r) * 0.5;
            const double y = b0 * x + b1 * sx1 + b2 * sx2 - a1 * sy1 - a2 * sy2;
            sx2 = sx1; sx1 = x; sy2 = sy1; sy1 = y;
            const double a = std::abs (y);
            fast = a + (fast - a) * (a > fast ? aAtt : aRelF);
            dens = a + (dens - a) * (a > dens ? dAtt : dRel);
            slow = a + (slow - a) * (a > slow ? aAtt : aRelS);
            const double cell = std::max (fast, 0.8 * slow);
            const double ldb = 20 * std::log10 (std::max (cell, 1e-6));
            const double xg = ldb - thr;
            double over;
            if (xg < -half) over = 0;
            else if (xg > half) over = xg;
            else { const double t = xg + half; over = t * t / (2 * knee); }
            const double gr = slope * over;               // dB, <= 0
            if (gr < grMin) grMin = gr;
            const double g = std::pow (10.0, gr / 20);
            // ----- étage de sortie -----
            gLin += (gTarget - gLin) * (1 - smooth);
            wet += (wetTarget - wet) * (1 - smooth);
            double vl = l * g * gLin, vr = r * g * gLin;
            vl = std::tanh (vl * d) / d; vr = std::tanh (vr * d) / d;
            // HP 25 Hz
            const double hl = hpA * (hpLy1 + vl - hpLx1); hpLx1 = vl; hpLy1 = hl;
            const double hr = hpA * (hpRy1 + vr - hpRx1); hpRx1 = vr; hpRy1 = hr;
            // LP 18 kHz
            lpL += lpB * (hl - lpL); lpR += lpB * (hr - lpR);
            const double w = wet;
            const double outL = lpL * w + l * (1 - w), outR = lpR * w + r * (1 - w);
            oL[i] = (float) outL; oR[i] = (float) outR;
            const double m = std::max (std::abs (outL), std::abs (outR)); if (m > pk) pk = m;
        }
        // mesures pour l'affichage (lues puis remises à zéro par l'interface)
        float cur = grMinShared.load();
        while ((float) grMin < cur && ! grMinShared.compare_exchange_weak (cur, (float) grMin)) {}
        float cp = pkShared.load();
        while ((float) pk > cp && ! pkShared.compare_exchange_weak (cp, (float) pk)) {}
    }

    // Réduction de gain la plus forte (dB, ≤ 0) depuis la dernière lecture
    float takeGRMin() { return grMinShared.exchange (0.0f); }
    float takePeak()  { return pkShared.exchange (0.0f); }

    // Courbe statique (pour les tests) : réduction en dB pour un niveau détecteur en dB
    static double staticGR (double ldb, double peakRed, bool limit)
    {
        const double thr = 10 - 0.55 * peakRed, ratio = limit ? 12 : 3.5, knee = 12, half = knee / 2;
        const double xg = ldb - thr;
        double over;
        if (xg < -half) over = 0; else if (xg > half) over = xg; else { const double t = xg + half; over = t * t / (2 * knee); }
        return (1 / ratio - 1) * over;
    }

private:
    double coef (double t) const { return std::exp (-1 / (t * sr)); }

    double sr = 48000;
    OptoParams p;
    double fast = 0, dens = 0, slow = 0;
    double sx1 = 0, sx2 = 0, sy1 = 0, sy2 = 0;
    double hpLx1 = 0, hpLy1 = 0, hpRx1 = 0, hpRy1 = 0, lpL = 0, lpR = 0;
    double gLin = 1, gTarget = 1, wet = 0, wetTarget = 0;
    double aAtt = 0, aRelF = 0, dAtt = 0, dRel = 0, aRelS = 0;
    double hpA = 0, lpB = 0, smooth = 0;
    double thr = 0, ratio = 3.5, knee = 12, d = 1;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    int blockPos = 0;
    std::atomic<float> grMinShared { 0 }, pkShared { 0 };
};
} // namespace audiolab

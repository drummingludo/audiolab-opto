// Moteur du compresseur optique d'Audio Lab (type LA-2A), version 2.0 « pro ».
// Point de départ : la classe OptoDSP du worklet « al-opto » d'Audio_Lab.html (même cellule, même courbe),
// améliorée pour le plugin (la page web ne change pas pour l'instant) :
//  - détection APRÈS l'étage de gain (la cellule T4 est alimentée par la sortie, avant le Gain de rattrapage) ;
//    loi de la boucle déduite de la courbe d'origine : même compression en régime établi ;
//  - détection stéréo sur la somme des puissances √((L² + R²) / 2) (avant : (L + R) / 2, qui ignorait une source
//    en opposition de phase et sous-estimait de 6 dB une source d'un seul côté) ;
//  - emphase aiguë du side-chain (réglage R37 de l'original) : étagère +0 à +9 dB à 3 kHz, lissée ;
//  - étage à lampes asymétrique : H2 dominante, ≈ 0,25 % de THD à −18 dBFS en sortie, qui monte avec le niveau ;
//  - le moteur non linéaire tourne au rythme suréchantillonné (Oversampler.h, ×1 à ×8) ;
//  - transfo de sortie : coupe-bas 25 Hz (bilinéaire pré-déformée) et coupe-haut 18 kHz (biquad ajusté par
//    moindres carrés sur le 1er ordre analogique) au rythme de base : ±0,02 dB du modèle jusqu'à 20 kHz à toutes
//    les fréquences d'échantillonnage (avant : −1,3 dB à 44,1 kHz contre −2,9 dB à 96 kHz à 20 kHz) ;
//  - relâchement lent recalculé à chaque échantillon ; seuil, ratio et emphase lissés (aucun clic).
// Indépendant de JUCE : testé hors plugin (tests/quality_test.cpp).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include "Oversampler.h"

namespace audiolab
{
inline constexpr double kPi = 3.14159265358979323846;

struct OptoParams
{
    double gainDb = 0, peakRed = 40;   // Gain (dB), Peak Reduction (0–100)
    bool limit = false;                // Compress / Limit
    double emphasis = 0;               // emphase aiguë du side-chain (R37), 0 à 1
};

// Coupe-haut du 1er ordre analogique (fc) reproduit par un biquad : ajustement du module par moindres carrés
// pondérés (méthode de l'erreur d'équation, 3 itérations) sur 0 → min (20 kHz, 0,45 fs), puis factorisation
// spectrale du numérateur et du dénominateur (zéros et pôles dans le cercle unité). Appelé hors temps réel.
struct LowpassFit
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static bool factor (double p2, double p1, double p0, double& k0, double& k1, double& k2)
    {
        // P(c) = p2 c² + p1 c + p0 (c = cos ω) = K (1 + q1² − 2 q1 c)(1 + q2² − 2 q2 c) = |N (e^jω)|²
        using C = std::complex<double>;
        if (std::abs (p2) < 1e-14) return false;
        const C disc = std::sqrt (C (p1 * p1 - 4 * p2 * p0, 0));
        C roots[2] = { (-p1 + disc) / (2 * p2), (-p1 - disc) / (2 * p2) }, q[2];
        for (int i = 0; i < 2; ++i)
        {
            const C ci = roots[i], s = std::sqrt (ci * ci - 1.0);
            q[i] = std::abs (ci - s) <= 1 ? ci - s : ci + s;    // zéro dans le cercle unité (phase minimale)
        }
        if (std::abs (roots[0].imag()) > 1e-12) q[1] = std::conj (q[0]);
        const double K = p2 / (4 * (q[0] * q[1]).real());
        if (! (K > 0) || std::abs (q[0]) > 1 || std::abs (q[1]) > 1) return false;
        k0 = std::sqrt (K); k1 = -k0 * (q[0] + q[1]).real(); k2 = k0 * (q[0] * q[1]).real();
        return true;
    }

    void design (double fc, double fs)
    {
        const double fmax = std::min (20000.0, 0.45 * fs);
        constexpr int nIn = 160, nOut = 24, N = nIn + nOut;
        double sol[5] = { 0, 0, 1, 0, 0 };   // p2, p1, p0, q2, q1 ; Q (c) = q2 c² + q1 c + 1
        for (int it = 0; it < 3; ++it)
        {
            double M[5][6] = {};
            for (int k = 0; k < N; ++k)
            {
                const double f = k < nIn ? fmax * k / (nIn - 1) : fmax + (fs / 2 - fmax) * (k - nIn + 1) / nOut;
                const double w = k < nIn ? 1.0 : 0.01;
                const double c = std::cos (2 * kPi * f / fs), T = 1 / (1 + (f / fc) * (f / fc));
                const double Q = it == 0 ? 1.0 : std::abs (sol[3] * c * c + sol[4] * c + 1);
                const double sc = w / (T * Q);
                const double row[5] = { c * c * sc, c * sc, sc, -T * c * c * sc, -T * c * sc }, y = T * sc;
                for (int i = 0; i < 5; ++i) { for (int j = 0; j < 5; ++j) M[i][j] += row[i] * row[j]; M[i][5] += row[i] * y; }
            }
            // élimination de Gauss avec pivot partiel
            for (int i = 0; i < 5; ++i)
            {
                int pv = i; for (int r = i + 1; r < 5; ++r) if (std::abs (M[r][i]) > std::abs (M[pv][i])) pv = r;
                for (int j = 0; j < 6; ++j) std::swap (M[i][j], M[pv][j]);
                for (int r = 0; r < 5; ++r)
                {
                    if (r == i || std::abs (M[i][i]) < 1e-300) continue;
                    const double f = M[r][i] / M[i][i];
                    for (int j = i; j < 6; ++j) M[r][j] -= f * M[i][j];
                }
            }
            for (int i = 0; i < 5; ++i) sol[i] = std::abs (M[i][i]) > 1e-300 ? M[i][5] / M[i][i] : 0;
        }
        double n0, n1, n2, d0, d1, d2;
        if (factor (sol[0], sol[1], sol[2], n0, n1, n2) && factor (sol[3], sol[4], 1.0, d0, d1, d2) && d0 > 0)
        {
            const double g = (n0 + n1 + n2) / (d0 + d1 + d2);   // gain 1 au continu
            b0 = n0 / (d0 * g); b1 = n1 / (d0 * g); b2 = n2 / (d0 * g); a1 = d1 / d0; a2 = d2 / d0;
            if (std::abs (a2) < 1 && std::abs (a1) < 1 + a2) return;   // stable
        }
        // repli (ne devrait pas servir) : 1er ordre bilinéaire pré-déformé
        const double K = std::tan (kPi * std::min (fc, 0.49 * fs) / fs);
        b0 = b1 = K / (1 + K); b2 = 0; a1 = (K - 1) / (K + 1); a2 = 0;
    }
};

class OptoCore
{
public:
    // Étage à lampes (calibration −18 dBFS en sortie = niveau nominal) : t = tanh (k·v) / k ; v = t + c2·t² (H2 dominante)
    static constexpr double tubeK = 0.62, tubeC2 = 0.036;

    // baseRate : fréquence du logiciel ; factor : suréchantillonnage (1, 2, 4, 8) ; le moteur non linéaire tourne à baseRate × factor
    void prepare (double baseRate, int factor)
    {
        srBase = baseRate; fac = std::max (1, factor); srHi = srBase * fac;
        aAtt = coef (0.010); aRelF = coef (0.060);
        dAtt = coef (0.5); dRel = coef (2.0);
        smHi = std::exp (-1 / (0.02 * srHi));
        smBase = std::exp (-1 / (0.02 * srBase));
        // transfo : coupe-bas 25 Hz (bilinéaire pré-déformée), coupe-haut 18 kHz (biquad ajusté)
        const double K = std::tan (kPi * 25.0 / srBase);
        hpB0 = 1 / (1 + K); hpA1 = (1 - K) / (1 + K);
        lp.design (18000.0, srBase);
        reset();
        setParams (p);
        thr = thrT; slope = slopeT; emph = emphT; gLin = gTarget; thrInv = std::pow (10.0, -thr / 20);
        designShelf (emph);
    }

    void reset()
    {
        fast = dens = slow = 0; gPrev = 1; memSlope = 1;
        for (auto& s : sc) s = {};
        hpLx1 = hpLy1 = hpRx1 = hpRy1 = 0;
        for (auto& s : lpS) s = {};
        grMinShared.store (0); pkShared.store (0);
    }

    void setParams (const OptoParams& np)
    {
        p = np;
        thrT = 10 - 0.55 * p.peakRed;
        slopeT = 1 / (p.limit ? 12.0 : 3.5) - 1;
        gTarget = std::pow (10.0, p.gainDb / 20);
        emphT = std::clamp (p.emphasis, 0.0, 1.0);
    }

    int factor() const { return fac; }

    // Courbe statique d'origine (réduction en dB pour un niveau de cellule en dB) ; la boucle la reproduit en régime établi
    static double staticGR (double ldb, double peakRed, bool limit)
    {
        const double thr = 10 - 0.55 * peakRed, s = 1 / (limit ? 12.0 : 3.5) - 1, half = knee / 2;
        const double xg = ldb - thr;
        double over;
        if (xg < -half) over = 0; else if (xg > half) over = xg; else { const double t = xg + half; over = t * t / (2 * knee); }
        return s * over;
    }

    // Partie non linéaire, au rythme haut, sur place
    void processHigh (float* L, float* R, int n)
    {
        double grMin = 0;
        const double k20 = std::log (10.0) / 20;
        for (int i = 0; i < n; ++i)
        {
            // réglages lissés (20 ms) : seuil, ratio, rattrapage, emphase du side-chain
            if (std::abs (thr - thrT) > 0)
            {
                thr += (thrT - thr) * (1 - smHi);
                if (std::abs (thrT - thr) < 1e-7) thr = thrT;   // atteint : plus de calcul
                thrInv = std::pow (10.0, -thr / 20);
            }
            slope += (slopeT - slope) * (1 - smHi);
            gLin += (gTarget - gLin) * (1 - smHi);
            if (std::abs (emphT - emph) > 1e-6)
            {
                emph += (emphT - emph) * (1 - smHi);
                if ((++shelfCount & 15) == 0) designShelf (emph);
            }
            const double l = L[i], r = R[i];
            // ----- side-chain : la cellule voit la sortie de l'étage de gain (avant le Gain de rattrapage) -----
            const double yl = l * gPrev, yr = r * gPrev;
            const double el = sc[0].run (yl, sb), er = sc[1].run (yr, sb);
            const double a = std::sqrt (0.5 * (el * el + er * er));        // somme des puissances
            // Au-dessus du seuil, la boucle accélère la cellule d'un facteur R (ratio) : temps compensés pour garder
            // ceux de l'original (attaque 10 ms, relâchement 60 ms puis 0,5 à 5 s), mesurés sur l'appareil en contre-réaction.
            const double kR = 1 + slope;                              // 1 / R
            const double aA = 1 - (1 - aAtt) * kR, aRF = 1 - (1 - aRelF) * kR;
            fast = a + (fast - a) * (a > fast ? aA : aRF);
            // mémoire de la cellule : éclairement du panneau (sortie / seuil, saturé à 2), moyenné (0,5 s / 2 s)
            const double lit = std::min (a * thrInv, 2.0);
            dens = lit + (dens - lit) * (lit > dens ? dAtt : dRel);
            // relâchement lent selon la mémoire, recalculé à chaque échantillon : exp (−u), u < 1e−4
            const double u = kR / ((0.5 + 4.5 * std::min (1.0, dens)) * srHi);
            const double aRelS = 1 - u * (1 - 0.5 * u);
            slow = a + (slow - a) * (a > slow ? aA : aRelS);
            // la mémoire lente retient 0,8 du niveau « vu à l'entrée » : 0,8^(1/R) sur le niveau de sortie lu par la cellule
            if (std::abs (slope - memSlope) > 1e-9) { memSlope = slope; memF = std::pow (0.8, 1 + slope); }
            const double cell = std::max (fast, memF * slow);
            const double gr = loopGR (20 * std::log10 (std::max (cell, 1e-6)));   // ≤ 0
            if (gr < grMin) grMin = gr;
            const double g = std::exp (gr * k20);
            gPrev = g;
            // ----- étage de sortie (lampes) -----
            L[i] = (float) tube (l * g * gLin);
            R[i] = (float) tube (r * g * gLin);
        }
        float cur = grMinShared.load();
        while ((float) grMin < cur && ! grMinShared.compare_exchange_weak (cur, (float) grMin)) {}
    }

    // Partie linéaire, au rythme de base, sur place : transfo (coupe-bas 25 Hz, coupe-haut 18 kHz)
    void processOut (float* L, float* R, int n)
    {
        double pk = 0;
        for (int i = 0; i < n; ++i)
        {
            const double vl = L[i], vr = R[i];
            const double hl = hpB0 * (vl - hpLx1) + hpA1 * hpLy1; hpLx1 = vl; hpLy1 = hl;
            const double hr = hpB0 * (vr - hpRx1) + hpA1 * hpRy1; hpRx1 = vr; hpRy1 = hr;
            const double ol = lpS[0].run (hl, lp), orr = lpS[1].run (hr, lp);
            L[i] = (float) ol; R[i] = (float) orr;
            const double m = std::max (std::abs (ol), std::abs (orr)); if (m > pk) pk = m;
        }
        float cp = pkShared.load();
        while ((float) pk > cp && ! pkShared.compare_exchange_weak (cp, (float) pk)) {}
    }

    // Réduction de gain la plus forte (dB, ≤ 0) depuis la dernière lecture
    float takeGRMin() { return grMinShared.exchange (0.0f); }
    float takePeak()  { return pkShared.exchange (0.0f); }

    // Réponse du coupe-haut de sortie (tests)
    double lowpassMag (double f) const
    {
        const std::complex<double> z = std::polar (1.0, -2 * kPi * f / srBase);
        return std::abs ((lp.b0 + lp.b1 * z + lp.b2 * z * z) / (1.0 + lp.a1 * z + lp.a2 * z * z));
    }

private:
    static constexpr double knee = 12;
    struct Biquad
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        template <typename C> double run (double x, const C& c)
        {
            const double y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
        }
    };
    struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    double coef (double t) const { return std::exp (-1 / (t * srHi)); }

    // high shelf RBJ (S = 1) à 3 kHz, +0 à +9 dB (emphase aiguë du side-chain, comme dans Audio Lab)
    void designShelf (double e)
    {
        const double G = 9 * e, A = std::pow (10.0, G / 40), w0 = 2 * kPi * 3000 / srHi;
        const double c = std::cos (w0), s = std::sin (w0), al = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) - (A - 1) * c + sa;
        sb.b0 = A * ((A + 1) + (A - 1) * c + sa) / a0;
        sb.b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        sb.b2 = A * ((A + 1) + (A - 1) * c - sa) / a0;
        sb.a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
        sb.a2 = ((A + 1) - (A - 1) * c - sa) / a0;
    }

    // Loi de la boucle (niveau de cellule lu sur la sortie → réduction) : inverse de y = x + gr (x) sur la courbe d'origine,
    // donc T (y) = gr (x) ; la contre-réaction retrouve exactement la courbe d'origine en régime établi.
    double loopGR (double y) const
    {
        const double h = knee / 2, s = slope;
        if (y <= thr - h) return 0;
        if (y >= thr + h + s * h) { const double x = (y + s * thr) / (1 + s); return y - x; }
        const double cp = y - thr + h;
        const double t = 2 * cp / (1 + std::sqrt (std::max (0.0, 1 + 2 * s * cp / knee)));
        return std::min (0.0, cp - t);   // y − x' avec x' = t + thr − h
    }

    static double tube (double v)
    {
        const double t = std::tanh (tubeK * v) / tubeK;
        return t + tubeC2 * t * t;
    }

    double srBase = 48000, srHi = 48000;
    int fac = 1;
    OptoParams p;
    double fast = 0, dens = 0, slow = 0, gPrev = 1;
    Biquad sc[2], lpS[2];
    Coefs sb;
    LowpassFit lp;
    double hpLx1 = 0, hpLy1 = 0, hpRx1 = 0, hpRy1 = 0, hpB0 = 1, hpA1 = 0;
    double gLin = 1, gTarget = 1;
    double aAtt = 0, aRelF = 0, dAtt = 0, dRel = 0;
    double smHi = 0, smBase = 0;
    double thr = -12, thrT = -12, thrInv = 4, slope = -0.71, slopeT = -0.71, emph = 0, emphT = 0;
    double memSlope = 1, memF = 0.8;
    unsigned shelfCount = 0;
    std::atomic<float> grMinShared { 0 }, pkShared { 0 };
};

// Chaîne complète au rythme de base : montée → moteur non linéaire → descente → transfo.
class OptoChain
{
public:
    static constexpr int maxChunk = 512;
    static int factorFor (int choice) { return 1 << std::clamp (choice, 0, 3); }

    void prepare (double baseRate, int choice)
    {
        sr = baseRate;
        for (int s = 1; s <= 3; ++s) os[s - 1].prepare (s, 2, maxChunk);
        setOversampling (choice);
    }
    void setOversampling (int choice)
    {
        mode = std::clamp (choice, 0, 3);
        if (mode > 0) os[mode - 1].reset();
        core.prepare (sr, factorFor (mode));
    }
    int oversampling() const { return mode; }
    // latence de chaque choix : 0 / 69 / 76 / 78 échantillons (celle du suréchantillonneur)
    int latencyFor (int choice) const { return choice > 0 ? os[std::clamp (choice, 1, 3) - 1].latency() : 0; }
    int latency() const { return latencyFor (mode); }
    int maxLatency() const { return latencyFor (3); }

    void setParams (const OptoParams& p) { core.setParams (p); }
    OptoCore& engine() { return core; }

    void process (const float* L, const float* R, float* oL, float* oR, int n)
    {
        for (int off = 0; off < n; off += maxChunk)
        {
            const int m = std::min (maxChunk, n - off);
            if (mode == 0)
            {
                std::copy (L + off, L + off + m, oL + off);
                std::copy (R + off, R + off + m, oR + off);
                core.processHigh (oL + off, oR + off, m);
            }
            else
            {
                const float* in[2] = { L + off, R + off };
                auto& o = os[mode - 1];
                float* const* hi = o.upsample (in, m);
                core.processHigh (hi[0], hi[1], m * o.factor());
                float* out[2] = { oL + off, oR + off };
                o.downsample (out, m);
            }
            core.processOut (oL + off, oR + off, m);
        }
    }

private:
    double sr = 48000;
    int mode = 0;
    os::Oversampler os[3];
    OptoCore core;
};
} // namespace audiolab

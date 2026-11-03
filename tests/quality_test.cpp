// Banc qualité du moteur Lab Opto 2.0, SANS JUCE (quelques secondes) :
//   clang++ -O2 -std=c++17 -I Source tests/quality_test.cpp -o qt && ./qt
// Seuils chiffrés : repliement, distorsion selon le niveau (H2 dominante), filtres de sortie identiques
// à 44,1 / 48 / 96 / 192 kHz, contre-réaction (même courbe statique), détection stéréo, emphase du side-chain,
// temps de la cellule, stabilité, réglages sans clic, latence. Renvoie 1 en cas d'échec.
#include "OptoCore.h"
#include <complex>
#include <cstdio>
#include <memory>
#include <vector>

using namespace audiolab;
static bool OK = true;
static void check (bool c, const char* m) { std::printf ("  %s %s\n", c ? "OK   " : "ÉCHEC", m); OK &= c; }
static double db (double a) { return 20 * std::log10 (std::max (a, 1e-30)); }

static OptoParams P (double gainDb = 0, double pr = 40, bool limit = false, double emph = 0)
{
    OptoParams p; p.gainDb = gainDb; p.peakRed = pr; p.limit = limit; p.emphasis = emph; return p;
}

static OptoChain* makeChain (double fs, int os, const OptoParams& p)
{
    auto* c = new OptoChain(); c->prepare (fs, os); c->setParams (p); c->engine().prepare (fs, OptoChain::factorFor (os));
    return c;
}

static std::vector<float> run (double fs, int os, const OptoParams& p, const std::vector<float>& xl, const std::vector<float>* xr = nullptr,
                               std::vector<float>* outR = nullptr)
{
    std::unique_ptr<OptoChain> c (makeChain (fs, os, p));
    const int n = (int) xl.size();
    std::vector<float> yl ((size_t) n), yr ((size_t) n);
    c->process (xl.data(), xr ? xr->data() : xl.data(), yl.data(), yr.data(), n);
    if (outR) *outR = yr;
    return yl;
}

static std::vector<float> sine (double fs, double f, double peakDb, int n)
{
    std::vector<float> x ((size_t) n);
    const double a = std::pow (10.0, peakDb / 20);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (a * std::sin (2 * kPi * f * i / fs));
    return x;
}

static std::vector<double> lines (const std::vector<float>& y, int start, int Pd, int k)
{
    const int N = Pd * k;
    std::vector<double> a ((size_t) Pd / 2 + 1, 0.0);
    for (int m = 0; m <= Pd / 2; ++m)
    {
        std::complex<double> s = 0;
        for (int i = 0; i < N; ++i) s += (double) y[(size_t) (start + i)] * std::polar (1.0, -2 * kPi * (double) (m * k) * i / N);
        a[(size_t) m] = std::abs (s) * (m == 0 ? 1.0 : 2.0) / N;
    }
    return a;
}

static double aliasing (double fs, int os, const OptoParams& p, double f, double lvl, int Pd, int k, double* fAt = nullptr)
{
    const int warm = (int) (3.0 * fs);   // la cellule optique et sa mémoire lente se stabilisent
    const auto y = run (fs, os, p, sine (fs, f, lvl, warm + Pd * k));
    const auto a = lines (y, warm, Pd, k);
    double fund = 0, worst = 0; int wm = 0;
    for (int m = 1; m <= Pd / 2; ++m)
    {
        const double fr = m * fs / Pd;
        bool harm = false;
        for (int q = 1; q * f < fs / 2 + 1; ++q) if (std::abs (fr - q * f) < 1) harm = true;
        if (std::abs (fr - f) < 1) fund = a[(size_t) m];
        else if (! harm && a[(size_t) m] > worst) { worst = a[(size_t) m]; wm = m; }
    }
    if (fAt) *fAt = wm * fs / Pd;
    return db (worst / fund);
}

struct Thd { double thd, h2, h3, out; };
static Thd thd (int os, const OptoParams& p, double lvl)
{
    const double fs = 48000; const int warm = (int) (3 * fs);
    const auto y = run (fs, os, p, sine (fs, 1000, lvl, warm + 48 * 200));
    const auto a = lines (y, warm, 48, 200);
    double s = 0; for (int m = 2; m <= 10; ++m) s += a[(size_t) m] * a[(size_t) m];
    return { 100 * std::sqrt (s) / a[1], 100 * a[2] / a[1], 100 * a[3] / a[1], db (a[1]) };
}

// réduction de gain (dB) bloc par bloc
static std::vector<double> grTrace (double fs, int os, const OptoParams& p, const std::vector<float>& xl, int blk,
                                    const std::vector<float>* xr = nullptr)
{
    std::unique_ptr<OptoChain> c (makeChain (fs, os, p));
    std::vector<float> yl ((size_t) blk), yr ((size_t) blk);
    std::vector<double> g;
    for (size_t off = 0; off + (size_t) blk <= xl.size(); off += (size_t) blk)
    {
        c->process (xl.data() + off, (xr ? xr->data() : xl.data()) + off, yl.data(), yr.data(), blk);
        g.push_back (c->engine().takeGRMin());
    }
    return g;
}

int main()
{
    char m[256];
    const char* osName[4] = { "Off", "2x", "4x", "8x" };
    std::puts ("\nLab Opto 2.0 : banc qualité du moteur (sans JUCE)");

    std::puts ("\n1) Repliement : sinus 9.5 kHz, −12 dBFS, Peak Reduction 50, Gain +6 (raie repliée la plus forte, sous la note)");
    for (int os = 0; os < 4; ++os)
    {
        double f48, f44;
        const double a48 = aliasing (48000, os, P (6, 50), 9500, -12, 96, 60, &f48);
        const double a44 = aliasing (44100, os, P (6, 50), 9500, -12, 441, 12, &f44);
        std::snprintf (m, sizeof m, "%-3s : %6.1f dB à 48 kHz (%5.0f Hz), %6.1f dB à 44.1 kHz (%5.0f Hz)%s", osName[os], a48, f48, a44, f44,
                       os == 0 ? "  [1.2 : −28 dB]" : "");
        check (os == 0 || (a48 < -80 && a44 < -80), m);
    }
    {
        double worst = -1e9;
        for (double pr : { 50.0, 100.0 }) for (bool lim : { false, true }) for (double g : { 6.0, 12.0 })
            worst = std::max (worst, aliasing (48000, 1, P (g, pr, lim), 9500, -6, 96, 40));
        std::snprintf (m, sizeof m, "2x (défaut), −6 dBFS, Peak Reduction 50 et 100, Compress et Limit, Gain +6 et +12 : pire raie %.1f dB", worst);
        check (worst < -78, m);
        const double sat = aliasing (48000, 1, P (20, 50), 9500, -6, 96, 40), sat4 = aliasing (48000, 2, P (20, 50), 9500, -6, 96, 40);
        std::printf ("  info  Gain +20 (lampes saturées, sortie au-dessus de 0 dBFS) : %.1f dB en 2x, %.1f dB en 4x\n", sat, sat4);
    }

    std::puts ("\n2) Étage à lampes : distorsion selon le niveau de sortie (1 kHz, sans compression : Peak Reduction 0, 2x)");
    {
        std::puts ("       sortie     THD      H2      H3");
        double tPrev = 0, tNom = 0; bool rising = true, h2dom = true;
        for (double lvl : { -36.0, -30.0, -24.0, -18.0, -12.0, -6.0, -3.0 })
        {
            const auto t = thd (1, P (0, 0), lvl);
            std::printf ("     %5.1f dBFS  %5.2f %%  %5.2f %%  %5.2f %%\n", t.out, t.thd, t.h2, t.h3);
            if (lvl == -18) tNom = t.thd;
            if (lvl > -30 && t.thd < tPrev * 0.98) rising = false;
            if (lvl <= -6 && t.h2 < t.h3) h2dom = false;
            tPrev = t.thd;
        }
        std::snprintf (m, sizeof m, "niveau nominal (−18 dBFS en sortie) : THD %.2f %% (cible ≈ 0.25 %%), H2 dominante jusqu'à −6 dBFS, qui monte avec le niveau", tNom);
        check (tNom > 0.18 && tNom < 0.32 && rising && h2dom, m);
        const auto r = thd (1, P (6, 50), -12);
        std::snprintf (m, sizeof m, "réglage de référence (−12 dBFS, PR 50, Gain +6, sortie %.1f dBFS) : H2 %.1f dB, H3 %.1f dB  [1.2 : H2 −130 dB, H3 −27 dB]",
                       r.out, db (r.h2 / 100), db (r.h3 / 100));
        check (r.h2 > r.h3 && db (r.h3 / 100) < -45, m);
    }

    std::puts ("\n3) Filtres de sortie (25 Hz, 18 kHz) : même réponse à 44.1 / 48 / 96 / 192 kHz, conforme au modèle analogique");
    {
        const double freqs[] = { 20, 25, 50, 1000, 10000, 16000, 18000, 20000 };
        auto analog = [] (double f) { return db (1 / std::sqrt (1 + std::pow (25.0 / f, 2))) + db (1 / std::sqrt (1 + std::pow (f / 18000, 2))); };
        for (int os : { 0, 1 })
        {
            double worst = 0, worstModel = 0; char row[300] = ""; int pos = 0;
            for (double f : freqs)
            {
                double ref = 0;
                for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
                {
                    const int n = (int) (fs * 0.5), st = (int) (fs * 0.3);
                    const auto y = run (fs, os, P (0, 0), sine (fs, f, -40, n));
                    std::complex<double> s = 0, w = 0;
                    for (int i = st; i < n; ++i) { const double hw = 0.5 - 0.5 * std::cos (2 * kPi * (i - st) / (n - st)); s += hw * (double) y[(size_t) i] * std::polar (1.0, -2 * kPi * f * i / fs); w += hw; }
                    const double gdb = db (2 * std::abs (s) / std::abs (w) / std::pow (10.0, -40 / 20.0));
                    if (fs == 44100.0) ref = gdb; else worst = std::max (worst, std::abs (gdb - ref));
                    worstModel = std::max (worstModel, std::abs (gdb - analog (f)));
                    if (fs == 44100.0) pos += std::snprintf (row + pos, sizeof row - (size_t) pos, " %g Hz %+.2f", f, gdb);
                }
            }
            std::snprintf (m, sizeof m, "%-3s : écart max entre fréquences %.3f dB, au modèle %.3f dB (44.1 kHz :%s dB)", osName[os], worst, worstModel, row);
            check (worst < 0.1 && worstModel < 0.1, m);
        }
        std::printf ("  info  1.2 à 20 kHz : −1.3 dB (44.1 kHz) contre −2.9 dB (96 kHz) ; modèle analogique −3.49 dB\n");
    }

    std::puts ("\n4) Détection après l'étage de gain : même courbe statique qu'avant (niveau constant)");
    {
        double worst = 0;
        for (double pr : { 40.0, 80.0, 100.0 }) for (bool lim : { false, true })
            for (double lvl = -40; lvl <= 0; lvl += 4)
            {
                std::vector<float> x ((size_t) (48000 * 4), (float) std::pow (10.0, lvl / 20));
                const auto g = grTrace (48000, 0, P (0, pr, lim), x, 512);
                worst = std::max (worst, std::abs (g.back() - OptoCore::staticGR (lvl, pr, lim)));
            }
        std::snprintf (m, sizeof m, "Peak Reduction 40 / 80 / 100, Compress et Limit, −40 à 0 dBFS : écart max à la courbe d'origine %.3f dB", worst);
        check (worst < 0.05, m);
    }

    std::puts ("\n5) Détection stéréo sur la somme des puissances (avant : (L + R) / 2)");
    {
        const double fs = 48000; const int n = (int) (3 * fs);
        auto s = sine (fs, 1000, -6, n); std::vector<float> neg (s.size()), zero (s.size(), 0.0f);
        for (size_t i = 0; i < s.size(); ++i) neg[i] = -s[i];
        const double gMono = grTrace (fs, 0, P (0, 80), s, 512).back();
        const double gAnti = grTrace (fs, 0, P (0, 80), s, 512, &neg).back();
        const double gLeft = grTrace (fs, 0, P (0, 80), s, 512, &zero).back();
        std::snprintf (m, sizeof m, "réduction : L = R %.2f dB ; L = −R %.2f dB (1.2 : 0 dB) ; L seul %.2f dB (niveau détecteur −3 dB, 1.2 : −6 dB)", gMono, gAnti, gLeft);
        check (std::abs (gAnti - gMono) < 0.01 && gLeft > gMono && gLeft < 0, m);
    }

    std::puts ("\n6) Emphase aiguë du side-chain (R37) : 0 % = neutre, 100 % = la cellule réagit plus aux aigus");
    {
        const double fs = 48000; const int n = (int) (3 * fs);
        auto lo = sine (fs, 300, -10, n), hi = sine (fs, 10000, -10, n);
        const double l0 = grTrace (fs, 1, P (0, 70, false, 0), lo, 512).back(), h0 = grTrace (fs, 1, P (0, 70, false, 0), hi, 512).back();
        const double l1 = grTrace (fs, 1, P (0, 70, false, 1), lo, 512).back(), h1 = grTrace (fs, 1, P (0, 70, false, 1), hi, 512).back();
        std::snprintf (m, sizeof m, "300 Hz / 10 kHz au même niveau : emphase 0 %% %.2f / %.2f dB ; emphase 100 %% %.2f / %.2f dB", l0, h0, l1, h1);
        check (std::abs (l0 - h0) < 0.3 && h1 < h0 - 2 && std::abs (l1 - l0) < 0.3, m);
    }

    std::puts ("\n7) Temps de la cellule (Peak Reduction 100, Compress), mesurés comme en 1.2 : niveau de cellule reconstruit");
    {
        // au-dessus du genou, réduction = pente × (niveau − seuil) : on retrouve le niveau « vu » par la cellule (équivalent entrée)
        const double fs = 48000, thr = 10 - 0.55 * 100, slope = 1 / 3.5 - 1; const int blk = 16;
        auto lvlOf = [&] (double g) { return thr + g / slope; };
        const int n0 = (int) (0.2 * fs), n1 = (int) (2.0 * fs);
        std::vector<float> x ((size_t) n0, 0.001f); x.resize ((size_t) (n0 + n1), 0.5f); x.resize ((size_t) (n0 + 2 * n1), 0.001f);
        const auto g = grTrace (fs, 0, P (0, 100), x, blk);
        const int b0 = n0 / blk;
        int k = b0; while (std::pow (10.0, lvlOf (g[(size_t) k]) / 20) < 0.632 * 0.5) ++k;
        const double t63 = (k - b0 + 1) * blk / fs;
        std::snprintf (m, sizeof m, "attaque : 63 %% du niveau en %.1f ms (cellule 10 ms ; 1.2 : 10.0 ms)", t63 * 1000);
        check (std::abs (t63 - 0.010) < 0.0015, m);
        const int r0 = (n0 + n1) / blk;
        const double l0 = lvlOf (g[(size_t) r0 - 1]), l30 = lvlOf (g[(size_t) (r0 + (int) (0.030 * fs / blk))]);
        std::snprintf (m, sizeof m, "relâchement 1er temps (60 ms) : le niveau tombe de %.2f dB en 30 ms puis rejoint la mémoire lente (×0.8 = −1.94 dB)", l0 - l30);
        check (std::abs ((l30 - l0) - db (0.8)) < 0.3, m);
        const int i1 = r0 + (int) (0.1 * fs / blk), i2 = r0 + (int) (0.6 * fs / blk);
        const double tau = 0.5 * 20 * std::log10 (std::exp (1.0)) / (lvlOf (g[(size_t) i1]) - lvlOf (g[(size_t) i2]));
        std::snprintf (m, sizeof m, "relâchement 2e temps après 2 s de signal dense : constante de temps %.2f s (attendu 5 s)", tau);
        check (tau > 4.5 && tau < 5.5, m);
        // coup bref de 30 ms : mémoire lente plus courte (0,5 s au minimum)
        std::vector<float> y ((size_t) n0, 0.001f); y.resize ((size_t) (n0 + 0.03 * fs), 0.5f); y.resize ((size_t) (n0 + 0.03 * fs + n1), 0.001f);
        const auto g2 = grTrace (fs, 0, P (0, 100), y, blk);
        const int r1 = (int) ((n0 + 0.03 * fs) / blk);
        const int j1 = r1 + (int) (0.05 * fs / blk), j2 = r1 + (int) (0.15 * fs / blk);
        const double tau2 = 0.1 * 20 * std::log10 (std::exp (1.0)) / (lvlOf (g2[(size_t) j1]) - lvlOf (g2[(size_t) j2]));
        std::snprintf (m, sizeof m, "relâchement 2e temps après un coup bref de 30 ms : constante de temps %.2f s (0.5 à 5 s selon la densité)", tau2);
        check (tau2 >= 0.45 && tau2 < 2.0, m);
    }

    std::puts ("\n8) Stabilité (Limit, Peak Reduction 100, Gain +30) et réglages sans clic");
    {
        const double fs = 44100; const int Pd = 42 * 2, k = 60, warm = (int) (3 * fs);
        auto x = sine (fs, 1050, -3, warm + Pd * k);
        const auto y = run (fs, 0, P (30, 100, true), x);
        const auto a = lines (y, warm, Pd, k);
        double sub = 0; for (int q = 1; q <= Pd / 2; q += 2) sub = std::max (sub, a[(size_t) q]);
        std::snprintf (m, sizeof m, "sinus 1050 Hz, 44.1 kHz sans suréchantillonnage : aucune sous-harmonique (pire raie %.0f dB sous la note)", db (sub / a[2]));
        check (db (sub / a[2]) < -100, m);
        // changements brusques de Peak Reduction et de mode pendant un sinus 200 Hz : pas de saut dans la sortie
        std::unique_ptr<OptoChain> c (makeChain (48000, 1, P (0, 40)));
        auto s = sine (48000, 200, -6, 48000 * 3); std::vector<float> yl (s.size()), yr (s.size());
        float jump = 0, prev = 0;
        for (size_t off = 0; off < s.size(); off += 480)
        {
            if (off == 48000) c->setParams (P (0, 100, true));
            if (off == 96000) c->setParams (P (0, 20, false, 1));
            c->process (s.data() + off, s.data() + off, yl.data() + off, yr.data() + off, 480);
            for (size_t i = off; i < off + 480; ++i) { if (off > 24000) jump = std::max (jump, std::abs (yl[i] - prev)); prev = yl[i]; }
        }
        std::snprintf (m, sizeof m, "Peak Reduction 40 → 100 + Limit, puis 20 + emphase 100 %% : saut max %.4f par échantillon (sinus seul ≤ 0.013)", jump);
        check (jump < 0.016f, m);
    }

    std::puts ("\n9) Latence (impulsion) : exacte et entière");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int os = 0; os < 4; ++os)
        {
            std::unique_ptr<OptoChain> c (makeChain (fs, os, P (0, 0)));
            std::vector<float> x (4096, 0.0f), y (4096), z (4096);
            x[1000] = 0.01f;
            c->process (x.data(), x.data(), y.data(), z.data(), 4096);
            int pk = 0; for (int i = 0; i < 4096; ++i) if (std::abs (y[(size_t) i]) > std::abs (y[(size_t) pk])) pk = i;
            std::snprintf (m, sizeof m, "%3.0f kHz, %-3s : latence annoncée %d, pic mesuré à %d", fs / 1000, osName[os], c->latency(), pk - 1000);
            check (pk - 1000 == c->latency(), m);
        }

    std::printf ("\nRÉSULTAT : %s\n", OK ? "tout est conforme" : "ÉCHECS ci-dessus");
    return OK ? 0 : 1;
}

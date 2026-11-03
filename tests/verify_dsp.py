"""Banc de test du moteur opto : courbe statique (Peak Reduction, Compress / Limit), temps d'attaque / relâchement.

Version 2.0.0 (03/11/2026) : le plugin devient volontairement différent du code web d'Audio_Lab.html
(décision : les plugins sont la version « pro », la page web ne change pas pour l'instant) : détection après
l'étage de gain, stéréo en somme des puissances, emphase du side-chain, lampes asymétriques, suréchantillonnage,
filtres de sortie identiques à toutes les fréquences d'échantillonnage. Retiré : la comparaison échantillon par
échantillon plugin ↔ web (1) et « même résultat que le web » selon la taille des blocs. Gardé : l'empreinte de la
copie de référence du code web, l'indépendance à la taille des blocs (comparée au plugin lui-même), la courbe
statique (la contre-réaction la reproduit exactement) et les temps de la cellule (compensés dans la boucle).
Le banc chiffré de la version 2.0 est tests/quality_test.cpp (sans JUCE, sans Node.js)."""
import hashlib, json, os, subprocess, sys, numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.environ.get("ALO_TMP", "/tmp/alo")
os.makedirs(TMP, exist_ok=True)
rng = np.random.default_rng(1234)
OK = True
REF_SHA = "daf98f403fd1b62c448e7b8ba2cbcda8d0236e4e24b4f3c17d036f0a4024a298"   # Audio_Lab.html du 17/10/2026, lignes 1445–1541

def check(cond, msg):
    global OK
    print(("  OK   " if cond else "  ÉCHEC ") + msg)
    OK &= bool(cond)

def run_core(fs, x, prm, block=128):
    inp = os.path.join(TMP, "in.f32"); x.astype(np.float32).tofile(inp)
    pt = os.path.join(TMP, "p.txt"); open(pt, "w").write("%r %r %d" % (prm["gain"], prm["pr"], int(prm["limit"])))
    oc = os.path.join(TMP, "oc.f32")
    subprocess.run([os.path.join(HERE, "core_run"), str(fs), pt, inp, oc, str(block)], check=True)
    return np.fromfile(oc, np.float32).reshape(-1, 2)

def P(**k):
    base = dict(gain=0.0, pr=40.0, limit=False); base.update(k); return base

def music(fs, sec=4.0):
    n = int(fs * sec); t = np.arange(n) / fs; L = np.zeros(n); R = np.zeros(n)
    for k in range(int(sec / 0.5)):
        s = int(k * 0.5 * fs); m = min(n - s, int(0.25 * fs)); tt = np.arange(m) / fs
        kick = np.sin(2 * np.pi * (50 + 120 * np.exp(-tt * 30)) * tt) * np.exp(-tt * 9) * 0.9
        L[s:s + m] += kick; R[s:s + m] += kick
        if k % 2:
            sn = rng.standard_normal(m) * np.exp(-tt * 18) * 0.5
            L[s:s + m] += sn; R[s:s + m] += 0.8 * sn
    bass = 0.25 * np.sin(2 * np.pi * 55 * t) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.5 * t))
    voice = 0.2 * np.sin(2 * np.pi * 220 * t + 3 * np.sin(2 * np.pi * 5 * t)) * (np.sin(2 * np.pi * 0.7 * t) > -0.3)
    L += bass + voice; R += bass + 0.6 * voice + 0.02 * rng.standard_normal(n)
    L[int(3.2 * fs):int(3.5 * fs)] = 0; R[int(3.2 * fs):int(3.5 * fs)] = 0
    return np.stack([L, R], 1).astype(np.float32)

print("\n0) Copie du code web utilisée comme référence")
check(hashlib.sha256(open(os.path.join(HERE, "opto_ref.js"), "rb").read()).hexdigest() == REF_SHA,
      "opto_ref.js = classe OptoDSP d'Audio_Lab.html (empreinte SHA-256)")

print("\n1) Indépendance à la taille des blocs envoyés par le logiciel hôte (référence : blocs de 128)")
x = music(48000)
ref = run_core(48000, x, P(pr=60, gain=8))
for b in (1, 64, 100, 512, 1024, 4096):
    oc = run_core(48000, x, P(pr=60, gain=8), block=b)
    check(np.max(np.abs(oc - ref)) <= 1e-6, f"blocs de {b} éch. : même résultat qu'en blocs de 128")

def trace(fs, x, prm, block=16):
    """Réduction de gain du moteur, bloc par bloc (dB, ≤ 0)."""
    inp = os.path.join(TMP, "in.f32"); x.astype(np.float32).tofile(inp)
    pt = os.path.join(TMP, "p.txt"); open(pt, "w").write("%r %r %d" % (prm["gain"], prm["pr"], int(prm["limit"])))
    r = subprocess.run([os.path.join(HERE, "core_run"), str(fs), pt, inp, os.path.join(TMP, "oc.f32"), str(block), "t"],
                       check=True, capture_output=True, text=True)
    return np.array([float(v) for v in r.stdout.split()])

def theory(lvl, pr, limit):
    thr, ratio, knee = 10 - 0.55 * pr, (12 if limit else 3.5), 12
    xg = lvl - thr; h = knee / 2
    over = 0 if xg < -h else (xg if xg > h else (xg + h) ** 2 / (2 * knee))
    return (1 / ratio - 1) * over

fs = 48000
print("\n2) Courbe statique (Peak Reduction, Compress / Limit) : niveau constant, détection après l'étage de gain")
for pr, limit in [(40, False), (80, False), (70, True), (100, True)]:
    errs = []
    for lvl in range(-60, 7, 3):
        a = 10 ** (lvl / 20); x = np.full((int(fs * 1.5), 2), a, np.float32)
        g = trace(fs, x, P(pr=pr, limit=limit), 512)[-1]
        errs.append(abs(g - theory(lvl, pr, limit)))
    mode = "Limit" if limit else "Compress"
    check(max(errs) < 0.01, f"PR {pr:3d}, {mode:8s} : seuil {10 - 0.55 * pr:6.1f} dB, ratio {12 if limit else 3.5}:1, knee 12 dB — écart max {max(errs):.4f} dB")

print("\n3) Temps de la cellule optique (niveau détecteur reconstruit à partir de la réduction de gain)")
pr, thr, slope = 100, 10 - 0.55 * 100, 1 / 3.5 - 1
blk = 16
hi, lo = 0.5, 0.001
n_on, n_hold, n_rel = int(0.2 * fs), int(2.0 * fs), int(2.0 * fs)
x = np.concatenate([np.full(n_on, lo), np.full(n_hold, hi), np.full(n_rel, lo)]).astype(np.float32)
g = trace(fs, np.stack([x, x], 1), P(pr=pr), blk)
t = (np.arange(len(g)) + 1) * blk / fs
lvl = thr + g / slope                      # valable au-dessus du knee
lin = 10 ** (lvl / 20)
on = n_on / fs
seg = (t > on) & (t < on + 0.1)
t63 = t[seg][np.argmax(lin[seg] >= 0.632 * hi)] - on
check(abs(t63 - 0.010) < 0.0015, f"attaque : 63 % du niveau en {t63 * 1000:.1f} ms (cellule 10 ms)")
off = (n_on + n_hold) / fs
l0 = lvl[np.argmin(np.abs(t - off)) - 1]
l30 = lvl[np.argmin(np.abs(t - (off + 0.030)))]
check(abs((l30 - l0) - 20 * np.log10(0.8)) < 0.3, f"relâchement 1er temps (60 ms) : le niveau tombe de {l0 - l30:.2f} dB en 30 ms puis rejoint la mémoire lente (×0,8 = −1,94 dB)")
i1, i2 = np.argmin(np.abs(t - (off + 0.1))), np.argmin(np.abs(t - (off + 0.6)))
tau = (t[i2] - t[i1]) * 20 * np.log10(np.e) / (lvl[i1] - lvl[i2])
check(4.5 < tau < 5.5, f"relâchement 2e temps après 2 s de signal dense : constante de temps {tau:.2f} s (attendu 5 s)")
# programme clairsemé : coup bref → mémoire lente plus rapide (0,5 s minimum)
x = np.concatenate([np.full(n_on, lo), np.full(int(0.03 * fs), hi), np.full(n_rel, lo)]).astype(np.float32)
g = trace(fs, np.stack([x, x], 1), P(pr=pr), blk)
t = (np.arange(len(g)) + 1) * blk / fs; lvl = thr + g / slope
off = (n_on + int(0.03 * fs)) / fs
i1, i2 = np.argmin(np.abs(t - (off + 0.05))), np.argmin(np.abs(t - (off + 0.15)))
tau2 = (t[i2] - t[i1]) * 20 * np.log10(np.e) / (lvl[i1] - lvl[i2])
check(0.5 <= tau2 < 2.0, f"relâchement 2e temps après un coup bref de 30 ms : constante de temps {tau2:.2f} s (plus rapide : 0,5–5 s selon la densité)")

print("\nRÉSULTAT :", "tout est conforme" if OK else "ÉCHECS ci-dessus")
sys.exit(0 if OK else 1)

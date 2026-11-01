# Audio Lab Opto

Le compresseur optique d'**Audio Lab** (« Chimiste du son », Transversal Studio), inspiré du LA-2A, en plugin **VST3 / AU** pour Logic Pro, Ableton Live, FL Studio, Cubase, Reaper, Studio One, Bitwig…, et **AAX** pour Pro Tools (version de test non signée, voir plus bas).

- **Même son que l'outil web** : le moteur est un portage ligne à ligne du processeur `al-opto` d'`Audio_Lab.html`. Le banc de test compare les deux versions : sortie identique (écart < 10⁻⁶) à 44,1 / 48 / 96 kHz, quelle que soit la taille des blocs du logiciel.
- **Deux réglages, comme l'original** : Gain (rattrapage de sortie) et Peak Reduction (seuil et ratio couplés), Compress (3,5:1) / Limit (12:1). Cellule optique : attaque 10 ms, relâchement en deux temps (60 ms puis 0,5 à 5 s selon la densité du programme), étage à tubes et transfo.
- **Interface Audio Lab** : potentiomètres à arc, VU-mètre à aiguille (réduction de gain ou niveau de sortie +4 / +10), petit Dry/Wet, thème sombre de la charte Transversal, fenêtre redimensionnable, info-bulles en français.

Plan détaillé de l'interface et du moteur : `docs/09_GUI_Plugins_Opto_FET.md`.

## Télécharger

Chaque fabrication produit deux archives (onglet **Actions** > dernière fabrication > *Artifacts*) :
- `AudioLabOpto-Mac.zip` : VST3 + AU, Apple Silicon et Intel, avec un installateur en un double-clic.
- `AudioLabOpto-Windows.zip` : VST3 64 bits.

Pour une version durable sur la page **Releases**, créer un tag `v…` : la fabrication y dépose alors les deux archives.
Mode d'emploi : fichier `LISEZ-MOI.txt` dans chaque archive.

### Pro Tools (AAX)

Chaque archive contient aussi `Audio Lab Opto.aaxplugin` (Mac universel, Windows 64 bits), fabriqué avec le SDK AAX fourni par JUCE (GPL v3). Il n'est **pas encore signé** par PACE : il ne se charge que dans **Pro Tools Developer**. Aucune latence : rien de particulier à régler dans Pro Tools.

## Fabrication

Chaque envoi sur `main` déclenche la fabrication sur des machines Mac et Windows de GitHub, puis la validation automatique (`auval` pour l'AU, `pluginval` niveau 10 pour VST3 et AU ; pour l'AAX, contrôle de structure, d'architectures et du point d'entrée — pluginval ne sait pas charger l'AAX). Un tag `v…` publie la version.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Tests (Linux ou Mac) :

```
cmake -B build -DALO_TESTS=ON -DALO_STANDALONE=ON
cmake --build build --target ALOProcessorTest ALORenderUI
g++ -O2 -std=c++17 -ISource tests/core_run.cpp -o tests/core_run
python3 tests/verify_dsp.py     # moteur C++ ↔ code web d'Audio Lab (Node.js requis)
```

## Licence

Code sous **GNU AGPLv3** (voir `LICENSE`), comme la version libre du framework [JUCE](https://juce.com) qu'il utilise. Police Poppins sous licence SIL OFL 1.1 (`Resources/Poppins-OFL-LICENSE.txt`).

© Transversal Studio — Ludovic Diaz

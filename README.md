# Onirique Shaper (prototype)

Processeur de transitoires spectral : il ne coupe ou ne pousse que les fréquences qui composent
les transitoires, et laisse le reste intact. Chaque bin est comparé à sa propre moyenne récente,
en dB : la détection ne dépend pas du niveau d'entrée.

Les paramètres sont ceux de spiff, ni plus ni moins, sous les mêmes identifiants (`gain mode`,
`cut depth`, `band2 freq`, `decay lf/hf`...). L'interface et le code sont ONIRIQUE.

Même architecture que ResoBox : le moteur (`Source/dsp`) ne dépend pas de JUCE et se teste hors
ligne ; la FFT est celle de ResoBox.

## État

| Partie | État |
|---|---|
| Moteur (fenêtres, oversample, résolution, phase linéaire / minimale, stéréo, écoute de bande) | fait, testé hors ligne |
| Plugin VST3 / AU, paramètres automatisables | compile sur Mac (universel arm64 + x86_64) |
| Licence ONIRIQUE (`brumebox`) | branchée ; fiche CENTRAL « fondateurs seulement », démo et image en ligne |
| Presets | 32 d'usine, personnels, ouverture de fichiers |
| Interface | faite (`snapshot.png`), vérifiée sur capture (`shaper_snapshot`) |
| Version Windows | compile (Visual Studio 2022, x64) ; 1.0.2 publiée sur CENTRAL |
| Écoute dans un DAW | **pas encore faite** |

Latence : (FFT + fenêtre) / 2, soit 2048 échantillons en fenêtre medium sans oversample à 48 kHz,
1024 en short, 4096 en long ; l'oversample l'allonge (medium 4x : 5120). Annoncée à l'hôte à chaque
changement.

## Construire

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSHAPER_JUCE_DIR=../ResoBox/JUCE
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Le test écrit `build/shaper_sec.wav`, `shaper_cut.wav` et `shaper_boost.wav` pour écouter
l'effet sans DAW.

## Interface

![Shaper](snapshot.png)

- **En-tête** : les presets (< > menu SAVE), la pastille de licence, DELTA, BYPASS, ⋯ (couleur).
- **La colonne** : CUT / BOOST, DEPTH (celui du mode choisi), puis SENSITIVITY, DECAY, SHARPNESS,
  DECAY LF/HF.
- **Le pied**, sous l'écran, rangé comme celui de ResoBox : MIX, OUT (trim) | STEREO, LINK, BAL |
  WINDOW, PHASE. Resolution et oversample n'ont pas de bouton : ils gardent leur valeur, suivent
  les presets et l'automation.
- **L'écran**, dessiné comme l'analyseur d'ONIRIQUE EQ : l'entrée en trait pâle, la sortie en surface
  rouge (l'écart entre les deux = ce qui est coupé ou poussé), la courbe de sensibilité en rouge lumineux,
  ses cinq points numérotés (LC, 1, 2, 3, HC), l'échelle en dB à droite et la légende en haut. Un point
  se glisse ; Cmd (Ctrl) en glissant ou la molette règle sa largeur ; Alt en glissant le fait écouter ;
  double clic l'éteint. Un clic l'ouvre dans son panneau de verre (celui de l'EQ, bosse au-dessus de SENS) :
  marche, forme (ou pente), LISTEN, FREQ, SENS, Q, bande précédente / suivante, fermer.

Laisser le curseur deux secondes sur un contrôle affiche sa description, en français.

Capture sans hôte : `cmake --build build --target shaper_snapshot`, puis
`build/shaper_snapshot snapshot.png [largeur hauteur] [--boost] [--closed] [--scale 2] [--tip x y]`.
`build/shaper_snapshot --selftest` vérifie les presets (identifiants, enregistrer puis rouvrir, fichier spiff).

## Ce qu'il fait (les usages de spiff)

- **Retirer les bruits de bouche** et autres petits bruits : preset « Voix : bruits de bouche ».
- **Donner de la clarté et de l'expression à un instrument** : « Instrument : plus d'expression ».
- **Mettre un son en avant ou en arrière sans changer son volume** : « Mettre en avant », « Mettre en arrière ».
- **Rendre dynamique et détail à un mix trop compressé** : « Mix trop compressé : rendre la dynamique ».

`shaper_snapshot --selftest` mesure les quatre sur le plugin et ses presets, avec des sons fabriqués
(voix et claquements, corde pincée, mix passé dans un compresseur rapide). Dernière mesure :
claquements -14,5 dB et voix intacte à -25 dB près ; attaques +3,7 dB, notes tenues 0 dB ;
en avant +2,7 dB / en arrière -4,6 dB d'attaque pour moins de 0,2 dB de volume ; mix écrasé
+3,4 dB de crête sur moyenne. Pas encore vérifié à l'oreille sur de vrais enregistrements.

## Presets

32 presets d'usine ONIRIQUE (`Source/Presets.h`), par catégorie : divers, batterie, voix, basse, guitare,
synthés et claviers, image, mix, master. Les presets personnels sont des fichiers `.preset` dans
`Documents/Onirique/Shaper/Presets` (rangés par sous-dossier). Un fichier XML aux
`<PARAM id="..." value="..."/>` portant nos identifiants (donc aussi un `.preset` de spiff) se lit avec
`loadPresetFile` ; il n'y a plus de bouton OPEN.

## Visuels pour CENTRAL

- `visuels/fiche-shaper.png` (et `-boost`) : l'image de la fiche, l'interface au travail, en 2x.
  `build/shaper_snapshot visuels/fiche-shaper.png 1300 740 --closed --scale 2`
- La démo interactive : `cmake --build build --target shaper_demo`, puis `build/shaper_demo visuels/demo`
  (base.png + demo.json, format onirique-demo-v1 : le travail d'onirique-probe, fait sur Mac). À rattacher
  par `deploy/attach-demo.mjs` du dépôt ONIRIQUE CENTRAL.

## Paramètres

| Identifiant | Plage | Rôle |
|---|---|---|
| `gain mode` | Cut / Boost | couper ou pousser les transitoires |
| `cut depth`, `boost depth` | 0 – 10 | quantité de traitement de chaque mode (jusqu'à −30 dB / +15 dB) |
| `sensitivity` | 0 – 10 | plus haut, plus de transitoires détectées |
| `sharpness` | 0 – 10 | largeur de chaque coupe ou boost, de 2 octaves à 1/30 d'octave |
| `decay` | 0 – 10 | temps de retour, de 4 ms à environ 1,4 s |
| `decay lf/hf` | 0 – 10 | < 5 graves plus longs, > 5 aigus plus longs |
| `band0 on/freq/mode` | 20 Hz – 20 kHz | coupe-bas de la détection, pente 6 / 12 / 24 / 48 dB/oct |
| `band1–3 on/freq/sens/q/mode` | sens ±18 dB, q 0,1 – 10 | low shelf, parametric, high shelf, tilt shelf |
| `band4 on/freq/mode` | 20 Hz – 20 kHz | coupe-haut de la détection |
| `stereo mode` | Left/Right, Mid/Side | |
| `stereo link` | 0 – 100 % | détection commune (100 %) ou deux mono (0 %) |
| `stereo balance` | 100/0 – 100/100 – 0/100 | part de traitement de chaque canal |
| `resolution` | Normal, High | pas d'analyse : quart ou huitième de fenêtre |
| `oversample` | 1x, 2x, 4x | FFT plus fine, avec la place pour les résonances du filtre |
| `window length` | Short, Medium, Long | 1024 / 2048 / 4096 échantillons à 48 kHz |
| `phase mode` | Linear, Minimum | |
| `mix` | 0 – 100 % | |
| `trim` | ±12 dB | gain du signal traité seulement |
| `delta`, `bypass` | | |

L'écoute d'une bande (LISTEN) est un état de l'interface, pas un paramètre.

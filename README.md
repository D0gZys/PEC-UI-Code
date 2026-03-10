# Plateforme de Mesure (Microscope + Caméra + Platine + Potentiostat)

Projet de contrôle instrumenté sous Windows pour:
- piloter une caméra Thorlabs (live + paramètres),
- piloter une platine XY Newport CONEX-CC,
- aligner automatiquement la platine sur un point image via `GoTo`,
- exécuter des séquences de balayage,
- tester un potentiostat BioLogic SP-300 (scripts dédiés).

Le point rouge affiché dans l'image représente la position fixe du laser (référentiel caméra). La platine bouge, pas le laser.

---

## Table des matieres

1. [Vue d'ensemble](#vue-densemble)
2. [Arborescence du projet](#arborescence-du-projet)
3. [Prerequis](#prerequis)
4. [Installation](#installation)
5. [Lancement rapide](#lancement-rapide)
6. [Scripts de lancement (reference rapide)](#scripts-de-lancement-reference-rapide)
7. [Securite d'utilisation](#securite-dutilisation)
8. [Validation rapide du code](#validation-rapide-du-code)
9. [Guide complet `MainUI/main_ui.py`](#guide-complet-mainuimain_uipy)
10. [Configuration persistante (`laser_presets.json`)](#configuration-persistante-laser_presetsjson)
11. [Autres scripts Python](#autres-scripts-python)
12. [Potentiostat BioLogic](#potentiostat-biologic)
13. [Depannage](#depannage)
14. [Workflow Git recommande](#workflow-git-recommande)
15. [Limites connues](#limites-connues)
16. [References et documentation constructeur](#references-et-documentation-constructeur)
17. [Notes pratiques](#notes-pratiques)

---

## Vue d'ensemble

### Matériel cible

- Microscope avec objectifs `4x`, `10x`, `50x`
- Caméra Thorlabs (ex. LP126CU/M)
- 2 axes Newport CONEX-CC (X/Y)
- (Optionnel) Potentiostat BioLogic SP-300

### Fonctionnalités principales

- Interface unifiée caméra + moteurs (`MainUI/main_ui.py`)
- Live caméra (mono/couleur) + affichage FPS
- Overlay laser: cercle rouge + mire centrale
- `GoTo` sur clic image avec curseur en croix
- Zoom numérique image (molette/trackpad selon driver)
- Déplacement moteurs:
  - relatif (jog)
  - continu au clavier
  - absolu (aller à une position)
  - lecture position live X/Y
- Séquences auto:
  - linéaire
  - rectangle en serpentin
- Sauvegarde automatique des presets laser/objectif dans JSON

---

## Arborescence du projet

```text
Programme/
├─ README.md
├─ Camera/
│  ├─ SDK/
│  │  ├─ Native Toolkit/
│  │  └─ Python Toolkit/
│  ├─ ... docs Thorlabs (PDF/CHM/README)
├─ MainUI/
│  ├─ main_ui.py
│  ├─ laser_presets.json
│  ├─ newport_conex_test_ui.py
│  ├─ thorlabs_camera_test_ui.py
│  ├─ motor_camera_combo_test_ui.py
│  ├─ biologic_potentiostat_test_ui.py
│  ├─ biologic_ca_cli.py
│  └─ README_Potentiostat.md
├─ Potentiostat/
│  └─ Examples/
│     ├─ C-C++/
│     ├─ LabVIEW/
│     └─ Python/
└─ Servo controler/
   └─ CONEX-CC-Command_Interface_Manual.pdf
```

---

## Prérequis

### Système

- Windows 10/11
- Python 3.11 recommandé (3.10+ généralement OK)

### Logiciels constructeurs

- **Thorlabs ThorCam / SDK** (DLL natives caméra)
- **Newport Motion Control** (DLL .NET CONEX-CC)
- **EC-Lab Development Package** (si potentiostat BioLogic)

### Dépendances Python

Pour l'UI principale caméra+moteurs:

- `numpy`
- `pillow`
- `pythonnet`
- `thorlabs_tsi_sdk` (installé via zip fourni dans `Camera/SDK/Python Toolkit/`)

Pour les scripts BioLogic:

- pas de package pip externe obligatoire (utilise `kbio` fourni dans `Potentiostat/Examples/Python/kbio/`)

---

## Installation

### 1) Créer un environnement virtuel (recommandé)

Depuis la racine du projet:

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
```

### 2) Installer les dépendances UI principale

```powershell
python -m pip install --upgrade pip
python -m pip install numpy pillow pythonnet
```

### 3) Installer le SDK Python Thorlabs depuis le zip du projet

```powershell
python -m pip install "Camera/SDK/Python Toolkit/thorlabs_tsi_camera_python_sdk_package.zip"
```

Remarque: `main_ui.py` sait aussi ajouter dynamiquement un sous-chemin du zip au runtime, mais l'installation pip reste la méthode la plus robuste.

### 4) Vérifier les DLL natives

- Caméra Thorlabs: `thorlabs_tsi_camera_sdk.dll`
- Moteurs Newport: `Newport.CONEXCC.CommandInterface.dll`

L'application propose des dialogues pour sélectionner les chemins si besoin.

---

## Lancement rapide

Depuis `Programme/`:

```powershell
python MainUI/main_ui.py
```

---

## Scripts de lancement (reference rapide)

Depuis la racine `Programme/`:

```powershell
python MainUI/main_ui.py
python MainUI/newport_conex_test_ui.py
python MainUI/thorlabs_camera_test_ui.py
python MainUI/motor_camera_combo_test_ui.py
python MainUI/biologic_potentiostat_test_ui.py
python MainUI/biologic_ca_cli.py --help
```

---

## Securite d'utilisation

- Verifier que la zone de deplacement mecanique est libre avant tout `GoTo` ou balayage.
- Toujours initialiser (`Home`) les axes avant une sequence automatisee.
- Ne pas lancer des pas/trajectoires trop grands sans surveillance.
- Utiliser `STOP moteurs` en cas de comportement inattendu.

---

## Validation rapide du code

```powershell
python -m py_compile MainUI/main_ui.py
```

---

## Guide complet `MainUI/main_ui.py`

### 1) Connexion moteurs

Menu `Moteurs`:

1. `Configuration moteurs...`
2. `Charger DLL`
3. `Scanner ports`
4. Choisir COM X et COM Y
5. `Connecter moteurs`
6. `Initialiser (Home X+Y)`

Important: l'implémentation applique un mapping COM inversé interne (`X <- Y_port`, `Y <- X_port`) pour rester cohérente avec le montage historique.

### 2) Connexion caméra

Menu `Caméra`:

1. `Configuration caméra...`
2. `Charger SDK`
3. `Découvrir`
4. `Connecter caméra`
5. `Démarrer flux live`

Paramètres rapides disponibles en bas: exposition (s), gain.

### 3) Panneau Objectif / Laser

- Sélecteur objectif: `4x / 10x / 50x`
- Position du point laser (preset par objectif)
- Taille du cercle (rayon en px)

Le centre du cercle est matérialisé par une mire centrale.

### Échelle GoTo (auto)

La conversion mm/px est calculée automatiquement à partir de:

- pitch capteur: `3.45 µm/pixel`
- grossissement objectif

Formule de base:

```text
mm_per_px = 3.45 / (grossissement * 1000)
```

Valeurs théoriques:

- `4x`  -> `0.0008625 mm/px`
- `10x` -> `0.0003450 mm/px`
- `50x` -> `0.0000690 mm/px`

Les cases `Inv X` / `Inv Y` permettent d'inverser le sens par axe si votre montage l'exige.

### 4) GoTo sur clic image

Procédure:

1. cliquer bouton `GoTo`
2. curseur image passe en croix (mode précision)
3. cliquer la cible dans l'image
4. la platine se déplace pour amener le centre laser sur la cible

Notes:

- le point rouge reste fixe à l'écran (référence laser)
- c'est l'échantillon qui se déplace via la platine
- la précision dépend de l'étalonnage optique réel et du comportement mécanique de la platine
- le `GoTo` utilise une vitesse dédiée `Vit. GoTo (mm/s)` pour améliorer la précision des petits déplacements
- quatre corrections directionnelles sont disponibles en millimètres:
- `Corr X+ (mm)` pour les mouvements finaux en `X positif`
- `Corr X- (mm)` pour les mouvements finaux en `X negatif`
- `Corr Y+ (mm)` pour les mouvements finaux en `Y positif`
- `Corr Y- (mm)` pour les mouvements finaux en `Y negatif`
- ces corrections sont utiles quand l'erreur n'est pas symétrique entre gauche/droite/haut/bas

### 5) Zoom numérique image

Fonctionne sur la zone preview:

- molette souris
- gestes trackpad transformés en évènements wheel (selon OS/driver)

Le zoom est numérique (crop + resize), sans changer le capteur caméra.

### 6) Contrôle moteurs

- Jog relatif: `X-/X+`, `Y-/Y+`
- Jog continu clavier: flèches
- Aller absolu: `Abs X`, `Abs Y`
- Positions live: affichage `Pos X`, `Pos Y`
- Stop: menu `STOP moteurs`

Plage logicielle de sécurité (code): `0.0 mm` à `25.0 mm`.

### 7) Séquences de balayage

Deux modes:

- `Linéaire`: interpolation du point départ à arrivée
- `Rectangle`: balayage serpentin (raster)

Paramètres:

- `Pas (mm)`
- `Durée/pt (s)`
- positions départ/arrivée lues directement sur moteurs

Exécution sur thread dédié avec arrêt propre via bouton `Stop`.

### 8) Raccourcis clavier

- flèches: jog continu
- les raccourcis sont ignorés si un champ texte est actif

---

## Configuration persistante (`laser_presets.json`)

Fichier: `MainUI/laser_presets.json`

Le fichier contient deux blocs principaux:

- `objective_presets`
- `goto_settings`

### `objective_presets`

Pour chaque objectif (`4x`, `10x`, `50x`):

- `x`, `y`: position du centre laser en pixels
- `size`: rayon du cercle rouge
- `mm_per_px_x`, `mm_per_px_y`: conversion image -> deplacement mecanique

Ces valeurs sont mises a jour automatiquement quand on deplace ou redimensionne le marqueur laser.

### `goto_settings`

Reglages globaux du `GoTo`:

- `goto_velocity_mm_s`: vitesse dediee aux deplacements `GoTo`
- `corr_xp_mm`: correction appliquee si le deplacement final est en `X+`
- `corr_xm_mm`: correction appliquee si le deplacement final est en `X-`
- `corr_yp_mm`: correction appliquee si le deplacement final est en `Y+`
- `corr_ym_mm`: correction appliquee si le deplacement final est en `Y-`

Ces corrections sont exprimees en millimetres, car elles compensent un comportement mecanique de la platine et non un simple decalage d'affichage.

Exemple:

```json
{
  "objective_presets": {
    "4x": {
      "x": 2623,
      "y": 1293,
      "size": 19,
      "mm_per_px_x": 0.0008625,
      "mm_per_px_y": 0.0008625
    },
    "10x": {
      "x": 2624,
      "y": 1294,
      "size": 20,
      "mm_per_px_x": 0.000345,
      "mm_per_px_y": 0.000345
    }
  },
  "goto_settings": {
    "goto_velocity_mm_s": 0.1,
    "corr_xp_mm": -0.01,
    "corr_xm_mm": 0.01,
    "corr_yp_mm": -0.02,
    "corr_ym_mm": 0.0
  }
}
```

---

## Autres scripts Python

Dans `MainUI/`:

- `newport_conex_test_ui.py`
  - UI dédiée test moteurs Newport
  - utile pour valider COM/DLL hors caméra

- `thorlabs_camera_test_ui.py`
  - UI dédiée test caméra Thorlabs
  - utile pour valider SDK/DLL/flux avant intégration

- `motor_camera_combo_test_ui.py`
  - ancienne UI combinée (prototype)

- `biologic_potentiostat_test_ui.py`
  - UI de test BioLogic SP-300 (chronoampérométrie)

- `biologic_ca_cli.py`
  - version CLI BioLogic (sans interface graphique)

---

## Potentiostat BioLogic

Le projet contient une partie dédiée au SP-300.

Entrées principales:

- `MainUI/biologic_potentiostat_test_ui.py`
- `MainUI/biologic_ca_cli.py`
- `MainUI/README_Potentiostat.md` (documentation détaillée)

Pré-requis principaux:

- EC-Lab Development Package installé (DLL + fichiers techniques `.ecc`)
- accès réseau/IP au SP-300

---

## Dépannage

### `pythonnet is not installed`

```powershell
python -m pip install pythonnet
```

### DLL Newport introuvable

- vérifier installation Newport Motion Control
- charger manuellement la DLL dans `Configuration moteurs`

### SDK caméra Thorlabs non importable

```powershell
python -m pip install numpy pillow
python -m pip install "Camera/SDK/Python Toolkit/thorlabs_tsi_camera_python_sdk_package.zip"
```

### Caméra non détectée

- vérifier câble USB / alimentation
- vérifier chemin DLL natif
- lancer `thorlabs_camera_test_ui.py` pour isoler le problème

### Moteurs non détectés / ne bougent pas

- vérifier ports COM dans le gestionnaire de périphériques
- fermer logiciels concurrents utilisant le port
- vérifier adresse (default `1`)
- tester d'abord `newport_conex_test_ui.py`

### GoTo legerement decale

- verifier l'objectif actif (`4x/10x/50x`)
- verifier les sens `Inv X` / `Inv Y`
- reduire `Vit. GoTo (mm/s)` si la cible est depassee sur les petits deplacements
- ajuster `Corr X+`, `Corr X-`, `Corr Y+`, `Corr Y-` si l'erreur depend du sens d'approche
- garder en tete que ces corrections sont mecaniques, donc a regler en millimetres et non en pixels
- utiliser la mire centrale et le curseur en croix pour juger le centre reel au clic

### Erreur Tkinter liée au focus (`popdown`)

Le code contient désormais une récupération de focus sécurisée pour éviter les crashes lors des interactions Combobox/clic global.

---

## Workflow Git recommandé

Pour éviter de casser `main`:

1. créer une branche de travail
2. développer et tester dessus
3. fusionner vers `main` une fois validé

Exemple:

```powershell
git checkout -b codex/travail-mainui
# ... modifications + tests ...
git add -A
git commit -m "MainUI: ..."
git checkout main
git merge --no-ff codex/travail-mainui
```

---

## Limites connues

- La précision GoTo dépend d'un modèle d'échelle (pas de correction visuelle par vision assistée).
- Le geste pincement trackpad dépend du mapping driver -> événements wheel Tkinter.
- Le projet est fortement orienté Windows (DLL natives + chemins constructeurs).

---

## Références et documentation constructeur

- Thorlabs SDK/API: dossier `Camera/` (PDF/CHM inclus)
- Newport CONEX-CC manual: `Servo controler/CONEX-CC-Command_Interface_Manual.pdf`
- BioLogic examples: `Potentiostat/Examples/`

---

## Notes pratiques

- Script principal à utiliser au quotidien: `MainUI/main_ui.py`
- Fichier de configuration laser/objectifs: `MainUI/laser_presets.json`
- Si vous voulez documenter uniquement la partie potentiostat, voir `MainUI/README_Potentiostat.md`


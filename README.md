# Plateforme de mesure — Microscope + Caméra + Platine motorisée

Interface de contrôle Python pour un système de microscopie équipé d'une platine XY motorisée Newport CONEX-CC, d'une caméra USB Thorlabs LP126CU/M et d'un microscope à trois objectifs.

---

## Table des matières

1. [Description du système physique](#1-description-du-système-physique)
2. [Architecture du projet](#2-architecture-du-projet)
3. [Matériel requis](#3-matériel-requis)
4. [Installation](#4-installation)
5. [Lancement](#5-lancement)
6. [Interface principale — Guide d'utilisation](#6-interface-principale--guide-dutilisation)
7. [Séquences de balayage](#7-séquences-de-balayage)
8. [Raccourcis clavier](#8-raccourcis-clavier)
9. [Dépannage](#9-dépannage)

---

## 1. Description du système physique

Le système est composé de :

- Un **microscope** équipé de trois objectifs interchangeables : **4×**, **10×**, **50×**
- Une **platine XY motorisée** (Newport CONEX-CC) positionnée sous le microscope, qui déplace l'échantillon
- Une **caméra USB** (Thorlabs LP126CU/M) montée sur le microscope qui retransmet l'image en temps réel
- Un **laser** projeté depuis le microscope sur la platine, dont la position dans l'image dépend de l'objectif utilisé

Le programme permet de :

- Visualiser en temps réel le flux vidéo de la caméra
- Afficher un **marqueur rouge** représentant la position du faisceau laser dans l'image
- Controler la platine XY (déplacements manuels et séquences automatiques)
- Effectuer des **balayages automatisés** (linéaire ou rectangulaire) d'une zone de l'échantillon

---

## 2. Architecture du projet

```
Programme/
├── MainUI/
│   ├── main_ui.py                   ← Interface principale (programme à lancer)
│   ├── newport_conex_test_ui.py     ← Classes et UI de test pour les moteurs
│   └── motor_camera_combo_test_ui.py← Ancienne interface combinée (conservée)
├── Camera/
│   └── SDK/
│       ├── Native Toolkit/          ← DLLs natives Thorlabs (thorlabs_tsi_camera_sdk.dll)
│       └── Python Toolkit/          ← Package Python SDK caméra (.zip)
├── Servo controler/
│   └── CONEX-CC-Command_Interface_Manual.pdf
└── README.md
```

---

## 3. Matériel requis

| Équipement | Modèle | Interface |
|---|---|---|
| Caméra | Thorlabs LP126CU/M | USB |
| Contrôleur moteur X | Newport CONEX-CC | COM (USB-série) |
| Contrôleur moteur Y | Newport CONEX-CC | COM (USB-série) |

**Logiciels / pilotes à installer :**

- [ThorCam](https://www.thorlabs.com/software_pages/ViewSoftwarePage.cfm?Code=ThorCam) (installe les DLLs natives Thorlabs)
- [Newport Motion Control](https://www.newport.com/) — fournit `Newport.CONEXCC.CommandInterface.dll`
- Python 3.11 (recommandé) avec les paquets listés ci-dessous

---

## 4. Installation

### 4.1 Dépendances Python

```bash
pip install numpy pillow pythonnet
```

### 4.2 SDK caméra Thorlabs

```bash
pip install "Camera/SDK/Python Toolkit/thorlabs_tsi_camera_python_sdk_package.zip"
```

### 4.3 Emplacement des DLLs

Les DLLs natives sont recherchées automatiquement dans :

- `C:\Program Files\Thorlabs\ThorImageCAM\`
- `Camera/SDK/Native Toolkit/dlls/Native_64_lib/`

Le chemin peut aussi être renseigné manuellement dans le menu **Caméra → Configuration caméra**.

---

## 5. Lancement

```bash
cd "c:\Users\thoma\Documents\Stage\Programme"
python MainUI/main_ui.py
```

---

## 6. Interface principale — Guide d'utilisation

### 6.1 Structure de la fenêtre

```
┌─────────────────────────────────────────────────┐
│  [Moteurs ▼]   [Caméra ▼]   [Affichage ▼]      │  ← Barre de menus
├─────────────────────────────────────────────────┤
│                                                 │
│           IMAGE CAMÉRA (taille maximale)        │
│         (point rouge = position du laser)        │
│                                                 │
├──────────────┬──────────────┬───────────────────┤
│ Objectif /   │   Moteurs    │ Séquence balayage │  ← Panneaux compacts
│    Laser     │              │                   │
├──────────────┴──────────────┴───────────────────┤
│ Journal (5 lignes, masquable)                    │
├─────────────────────────────────────────────────┤
│ Status | FPS | Coords point | [▶ Live] [■ Stop] │
└─────────────────────────────────────────────────┘
```

### 6.2 Menus de connexion

Toutes les connexions se font depuis les menus en haut de la fenêtre.

**Menu Moteurs**

| Action | Description |
|---|---|
| Configuration moteurs… | Ouvre le dialogue : chemin DLL, ports COM X/Y, adresse, timeout |
| Connecter moteurs | Ouvre les ports COM et connecte les deux axes |
| Initialiser (Home X+Y) | Lance la procédure de mise à zéro (homing) des deux axes |
| Déconnecter moteurs | Ferme les connexions |
| STOP moteurs | Arrêt d'urgence des deux axes |

**Menu Caméra**

| Action | Description |
|---|---|
| Configuration caméra… | Ouvre le dialogue : chemin DLL, numéro de série, exposition, gain |
| Connecter caméra | Se connecte à la caméra sélectionnée |
| Déconnecter caméra | Ferme la connexion |
| Démarrer flux live | Lance la capture vidéo en continu |
| Arrêter flux live | Arrête la capture |
| Appliquer paramètres | Applique exposition et gain sans redémarrer |

**Menu Affichage**

Permet de masquer/afficher le journal et les panneaux de contrôle pour maximiser la zone image.

### 6.3 Panneau « Objectif / Laser »

Le marqueur rouge représente la position du faisceau laser dans le champ de la caméra. Sa position et sa taille varient selon l'objectif utilisé.

| Objectif | X (px) | Y (px) | Taille (px) |
|---|---|---|---|
| 4× | 2588 | 1350 | 32 |
| 10× | à calibrer | à calibrer | à calibrer |
| 50× | à calibrer | à calibrer | à calibrer |

- **Sélecteur d'objectif** → applique automatiquement le bon preset
- **Pas (px) + boutons X−/X+/Y−/Y+** → déplace le marqueur manuellement
- **Taille (px) + −/+** → ajuste le rayon du cercle rouge

### 6.4 Panneau « Moteurs »

Contrôle manuel de la platine XY en déplacement relatif.

- **Pas X / Pas Y (mm)** → distance par clic ou par appui touche
- **X − / X + / Y − / Y +** → jog relatif
- Raccourcis clavier : **← → ↑ ↓**

---

## 7. Séquences de balayage

Les séquences permettent de déplacer la platine automatiquement entre deux points, avec un temps de pause configurable à chaque étape.

### 7.1 Définir les points

1. Déplacer la platine vers la **position de départ** → cliquer **Set Départ**
2. Déplacer la platine vers la **position d'arrivée** → cliquer **Set Arrivée**

Les coordonnées moteur (en mm) sont lues directement depuis les contrôleurs et affichées en vert/rouge dans le panneau.

### 7.2 Paramètres

| Paramètre | Description |
|---|---|
| Mode | Linéaire ou Rectangle |
| Pas (mm) | Distance entre deux positions consécutives |
| Durée/pt (s) | Temps d'attente à chaque position (temps d'acquisition) |

### 7.3 Mode Linéaire

Déplace la platine en ligne droite du point de départ au point d'arrivée, avec des arrêts régulièrement espacés de `pas (mm)`.

```
Départ ●──────────────────────────────● Arrivée
         step  step  step  step  step
```

### 7.4 Mode Rectangle (serpentin)

Balaye toute la zone rectangulaire délimitée par les deux points, en parcourant des lignes horizontales en sens alternés (serpentin) :

```
Départ ●→→→→→→→→→→→→→→→→→→→→→●
        ←←←←←←←←←←←←←←←←←←←←
        →→→→→→→→→→→→→→→→→→→→→→
        ←←←←←←←←←←←←←←←←←←←←
                                ● Arrivée
```

- Le nombre de colonnes = `round(|ΔX| / pas)`
- Le nombre de lignes = `round(|ΔY| / pas)`
- Les segments de connexion entre lignes ne comptent **pas** comme des points de mesure

### 7.5 Déroulement

1. **Mise en position** — La platine se déplace rapidement vers le point de départ (sans délai de mesure). Le statut affiche `Mise en position…`.
2. **Balayage** — Chaque point est atteint, la platine attend la convergence, puis marque une pause de `durée/pt` secondes. Le statut affiche `N / Total – (x, y)`.
3. **Fin** — Le statut affiche `Terminé (N points).`

À tout moment, le bouton **■ Stop** interrompt la séquence proprement.

---

## 8. Raccourcis clavier

| Touche | Action |
|---|---|
| ← | Jog X − |
| → | Jog X + |
| ↑ | Jog Y + |
| ↓ | Jog Y − |

> Les raccourcis sont désactivés quand le curseur est dans un champ texte.

---

## 9. Dépannage

| Problème | Cause probable | Solution |
|---|---|---|
| `pythonnet is not installed` | Paquet manquant | `pip install pythonnet` |
| `Unable to load Newport.CONEXCC…` | DLL Newport introuvable | Installer Newport Motion Control ou renseigner le chemin manuellement |
| `Cannot import Thorlabs camera SDK` | SDK non installé | `pip install "Camera/SDK/Python Toolkit/…zip"` |
| Caméra non détectée | DLL native absente | Vérifier le chemin DLL dans Configuration caméra |
| Moteur ne répond pas | Port COM incorrect ou déjà utilisé | Fermer Newport Motion Control, vérifier le port dans le gestionnaire de périphériques |
| Grand saut au démarrage d'une séquence | Comportement normal | La phase "Mise en position" amène la platine au point de départ sans pause de mesure |
| Image caméra qui pousse les panneaux | Ancien layout | Utiliser `main_ui.py` (le layout utilise `grid` avec poids fixes) |


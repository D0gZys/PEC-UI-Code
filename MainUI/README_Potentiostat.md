 # BioLogic SP-300 — Interface de Test Chronoampérométrie (CA)

## Description

`biologic_potentiostat_test_ui.py` est une interface graphique Python/tkinter permettant de piloter un potentiostat **BioLogic SP-300** connecté via **Ethernet**. L'application est dédiée aux expériences de **Chronoampérométrie (CA)** et reproduit fidèlement les paramètres de l'interface EC-Lab de BioLogic.

### Fonctionnalités principales

- **Connexion Ethernet** au SP-300 avec chargement du firmware sur le canal sélectionné
- **Configuration CA style EC-Lab** : pas de tension (Ewe), durées en h/mn/s, mode vs. Ref/Pref, limites de courant, gammes E/I, bande passante
- **Acquisition en temps réel** avec affichage graphique interactif
- **4 types de graphiques** : I = f(t), Ewe = f(t), I = f(Ewe), Ewe = f(I)
- **Navigation graphique** : zoom molette (centré sur le curseur), panoramique cliquer-glisser, réinitialisation de vue, mode plein écran
- **Export CSV** des données acquises (t, Ewe, I, cycle)
- **Journal horodaté** des opérations et erreurs

---

## Prérequis

### Matériel

| Élément | Détail |
|---------|--------|
| Potentiostat | BioLogic SP-300 |
| Connexion | Ethernet (câble RJ45 direct ou via réseau) |
| Adresse IP par défaut | `169.254.3.150` (configurable dans l'interface) |

### Logiciel

| Composant | Version / Détail |
|-----------|------------------|
| Python | 3.10+ |
| tkinter | Inclus avec Python (stdlib) |
| EC-Lab Development Package | Installé à `C:\EC-Lab Development Package\` |
| Bibliothèque kbio | Fournie dans `Potentiostat/Examples/Python/kbio/` |

> **Note** : Aucune dépendance pip externe n'est requise. L'interface utilise uniquement tkinter (stdlib) et la bibliothèque kbio fournie par BioLogic (wrapper ctypes autour de EClib64.dll).

### Fichiers requis du EC-Lab Development Package

Le dossier `C:\EC-Lab Development Package\lib\` doit contenir :

| Fichier | Rôle |
|---------|------|
| `EClib64.dll` (ou `EClib.dll` en 32-bit) | DLL principale de communication |
| `ca.ecc` | Fichier technique CA pour cartes **ESSENTIAL** |
| `ca4.ecc` | Fichier technique CA pour cartes **PREMIUM** |
| `ca5.ecc` | Fichier technique CA pour cartes **DIGICORE** |
| `kernel.bin` | Firmware pour cartes ESSENTIAL / DIGICORE |
| `kernel4.bin` | Firmware pour cartes PREMIUM |
| `Vmp_ii_0437_a6.xlx` | FPGA pour cartes ESSENTIAL |
| `vmp_iv_0395_aa.xlx` | FPGA pour cartes PREMIUM |

---

## Installation

Aucune installation spécifique n'est nécessaire. Il suffit que :

1. **Python 3.10+** soit installé avec tkinter (inclus par défaut sur Windows)
2. **EC-Lab Development Package** soit installé à l'emplacement par défaut (`C:\EC-Lab Development Package\`)
3. Le dossier `Potentiostat/Examples/Python/kbio/` soit présent dans l'arborescence du projet

Le programme ajoute automatiquement le chemin vers kbio au `sys.path` au démarrage.

---

## Lancement

```bash
cd MainUI
python biologic_potentiostat_test_ui.py
```

Ou depuis n'importe quel répertoire :

```bash
python "C:\Users\thoma\Documents\Stage\Programme\MainUI\biologic_potentiostat_test_ui.py"
```

---

## Guide d'utilisation

### 1. Connexion à l'instrument

1. **Chemin DLL** : Vérifié automatiquement si le EC-Lab Development Package est installé à l'emplacement par défaut. Sinon, cliquer sur **« Parcourir… »** pour sélectionner le dossier contenant `EClib64.dll`.
2. **Adresse IP** : Saisir l'adresse IP du SP-300 (par défaut `169.254.3.150`).
3. **Canal** : Sélectionner le canal de mesure (1 à 16).
4. Cliquer sur **« Connecter »**.
5. Cliquer sur **« Charger Firmware »** pour initialiser le canal (obligatoire avant la première expérience).

Le statut de connexion s'affiche en bleu sous les contrôles de connexion. Le journal en bas de la fenêtre détaille les opérations effectuées.

### 2. Configuration des paramètres CA

Les paramètres sont organisés en sections reproduisant l'interface EC-Lab :

#### Apply Ewe (Voltage Steps)

Chaque pas de tension se configure avec :

| Champ | Description | Exemple |
|-------|-------------|---------|
| **Ewe (V)** | Tension appliquée en Volts | `0.500` |
| **vs.** | Référence de tension : `Ref` (référence) ou `Pref` (potentiel initial) | `Ref` |
| **h / mn / s** | Durée du pas décomposée en heures, minutes, secondes | `0 h 2 mn 10.0000 s` |

- **« + Ajouter pas »** : Ajouter un pas de tension supplémentaire
- **« ✕ »** : Supprimer un pas (au moins un pas doit rester)

#### Limits

| Paramètre | Description | Valeur par défaut |
|-----------|-------------|-------------------|
| **Imax** | Courant maximum (ou `pass` pour ignorer) | `pass` |
| **Imin** | Courant minimum (ou `pass` pour ignorer) | `pass` |
| **\|ΔQ\| > ΔQM** | Seuil de charge pour arrêt | `0.000 mA.h` |

Les unités de courant sont sélectionnables : A, mA, µA, nA, pA.

#### Record

| Paramètre | Description | Valeur par défaut |
|-----------|-------------|-------------------|
| **Record** | Mode d'enregistrement : `<I>`, `<Ewe>`, ou `<I> and <Ewe>` | `<I>` |
| **every dta** | Intervalle d'enregistrement en secondes | `0.1000 s` |

#### Ranges

| Paramètre | Options | Valeur par défaut |
|-----------|---------|-------------------|
| **E Range** | `-2,5 V; 2,5 V` / `-5 V; 5 V` / `-10 V; 10 V` / `Auto` | `-2,5 V; 2,5 V` |
| **I Range** | `Keep` / `100 pA` à `1 A` / `Booster` / `Auto` | `Auto` |
| **Bandwidth** | `1` à `9` | `8` |

#### Cycles

| Paramètre | Description | Valeur par défaut |
|-----------|-------------|-------------------|
| **N Cycles** | Nombre de répétitions (0 = illimité) | `0` |

### 3. Lancer une expérience

1. Configurer les paramètres dans le panneau de gauche.
2. Cliquer sur **« ▶ Démarrer CA »**.
3. L'acquisition commence : les données s'affichent en temps réel sur le graphique.
4. Pour arrêter manuellement : **« ■ Arrêter »**.
5. L'expérience s'arrête automatiquement lorsque le potentiostat renvoie le statut `STOP`.

### 4. Graphiques

#### Types de graphiques

Quatre modes d'affichage sont disponibles via les boutons radio :

| Mode | Axe X | Axe Y | Couleur |
|------|-------|-------|---------|
| **I = f(t)** | Temps (s) | Courant (A) | Bleu `#1f77b4` |
| **Ewe = f(t)** | Temps (s) | Tension (V) | Rouge `#d62728` |
| **I = f(Ewe)** | Tension (V) | Courant (A) | Vert `#2ca02c` |
| **Ewe = f(I)** | Courant (A) | Tension (V) | Violet `#9467bd` |

#### Affichage

- Les points de données sont affichés sous forme de **croix** (✕) individuelles, sans lignes de connexion
- Grille en arrière-plan avec 5 divisions sur chaque axe
- Axes avec labels scientifiques (notation `3e` pour Y, `3g` pour X)
- Compteur de points affiché sous le graphique

#### Navigation interactive

| Action | Contrôle |
|--------|----------|
| **Zoom** | Molette de la souris (centré sur le curseur) |
| **Panoramique** | Clic gauche + glisser |
| **Réinitialiser** | Bouton **« Réinitialiser vue »** |
| **Plein écran** | Bouton **« Plein écran »** (ouvre une fenêtre maximisée) |

La fenêtre plein écran dispose de ses propres contrôles de navigation (zoom, pan, réinitialisation) et d'un sélecteur de type de graphique indépendant.

### 5. Export des données

Après l'acquisition, le bouton **« Exporter CSV »** devient disponible. Le fichier CSV contient :

| Colonne | Description |
|---------|-------------|
| `t` | Temps en secondes |
| `Ewe` | Tension mesurée (V) |
| `I` | Courant mesuré (A) |
| `cycle` | Numéro de cycle |

---

## Architecture du code

### Structure du fichier (1158 lignes)

```
biologic_potentiostat_test_ui.py
│
├── Imports et configuration des chemins
│   ├── Ajout de Potentiostat/Examples/Python/ au sys.path
│   └── Imports lazy de kbio (KBIO, KBIO_api, ECC_parm, etc.)
│
├── CA_PARMS                       # Paramètres technique CA (ECC_parm)
├── _rebuild_ca_parms()            # Reconstruction après import lazy
├── VoltageStep                    # Dataclass (voltage, duration, vs_init)
│
├── Labels et constantes           # I_RANGE_LABELS, E_RANGE_LABELS, BW_LABELS, etc.
│
└── BiologicPotentiostatTestApp(tk.Tk)
    ├── __init__()                 # État, variables tk, appel _build_ui()
    │
    ├── UI Construction
    │   ├── _build_ui()            # Layout principal (connexion, paramètres, graphe, journal)
    │   ├── _build_params_panel()  # Panneau gauche : steps, limites, record, ranges, cycles
    │   ├── _build_graph_panel()   # Canvas + sélecteur type + boutons navigation
    │   ├── _add_step_row()        # Ajouter un pas de tension dynamiquement
    │   └── _remove_step_row()     # Supprimer un pas
    │
    ├── Connexion Instrument
    │   ├── _ensure_api()          # Initialisation KBIO_api (lazy)
    │   ├── _on_connect()          # Connexion Ethernet
    │   ├── _on_load_firmware()    # Chargement firmware selon type de carte
    │   └── _on_disconnect()       # Déconnexion propre
    │
    ├── Expérience
    │   ├── _on_start_experiment() # Validation paramètres + lancement thread
    │   ├── _run_experiment()      # Thread : LoadTechnique → StartChannel → boucle GetData
    │   ├── _parse_ca_data()       # Extraction t, Ewe, I, cycle des données brutes
    │   ├── _on_stop_experiment()  # Arrêt canal + signal thread
    │   └── _experiment_finished() # Mise à jour UI post-expérience
    │
    ├── Graphiques
    │   ├── _get_current_data()    # Sélection données selon type de graphe
    │   ├── _update_plot()         # Mise à jour canvas principal
    │   ├── _draw_curve_viewport() # Rendu : grille, axes, labels, croix
    │   ├── _data_bounds()         # Calcul bornes des données
    │   └── _get_view()            # Viewport actuel (zoom/pan)
    │
    ├── Navigation
    │   ├── _on_graph_scroll()     # Zoom molette centré sur curseur
    │   ├── _on_graph_pan_*()      # Pan cliquer-glisser (start/move/end)
    │   ├── _reset_view()          # Réinitialiser le viewport
    │   ├── _pixel_to_data()       # Conversion coordonnées pixel → données
    │   └── _on_fullscreen_graph() # Fenêtre plein écran avec sa propre navigation
    │
    ├── Export
    │   └── _on_export_csv()       # Écriture CSV (DictWriter)
    │
    └── Cleanup
        └── _on_close()            # Arrêt propre (stop expérience, join thread, déconnexion)
```

### Bibliothèque kbio

Le programme utilise la bibliothèque kbio (wrapper ctypes) fournie par BioLogic :

| Module | Rôle |
|--------|------|
| `kbio_api.py` | Classe `KBIO_api` — wrapping des fonctions EClib DLL (Connect, LoadFirmware, LoadTechnique, StartChannel, GetData, StopChannel, etc.) |
| `kbio_types.py` | Structures ctypes et enums (DEVICE, BOARD_TYPE, I_RANGE, E_RANGE, BANDWIDTH, ERROR, PROG_STATE, DeviceInfo, ChannelInfo, etc.) |
| `kbio_tech.py` | Gestion des paramètres ECC : `ECC_parm`, `make_ecc_parm()`, `make_ecc_parms()`, `get_info_data()`, `get_experiment_data()` |
| `tech_types.py` | Enum `TECH_ID` (CA=101, OCV=100, CP=102, etc.) |
| `c_utils.py` | Utilitaires ctypes (`c_is_64b` pour détecter l'architecture) |

### Flux d'exécution d'une expérience

```
1. _on_start_experiment()
   ├── Validation des paramètres (steps, record_dt, ranges)
   ├── Réinitialisation des données et du graphique
   └── Lancement du thread _run_experiment()

2. _run_experiment() [thread séparé]
   ├── Sélection du fichier .ecc selon le type de carte
   ├── Construction des ECC_parms (voltage, durée, vs_init pour chaque pas)
   ├── api.LoadTechnique()
   ├── api.StartChannel()
   └── Boucle d'acquisition :
       ├── api.GetData()
       ├── get_info_data() → status
       ├── _parse_ca_data() → extraction des enregistrements
       ├── Ajout aux listes _plot_t, _plot_I, _plot_E
       ├── Mise à jour du graphique (via self.after())
       └── Arrêt si status == "STOP" ou _stop_event.is_set()

3. _experiment_finished() [thread principal]
   └── Réactivation des boutons, compteur final
```

---

## Correspondance avec les paramètres EC-Lab

| Paramètre interface | Paramètre ECC (EClib) | Type |
|---------------------|----------------------|------|
| Ewe (V) | `Voltage_step` | float |
| Durée (h/mn/s → secondes) | `Duration_step` | float |
| vs. Ref/Pref | `vs_initial` | bool (Pref = True) |
| Nombre de pas | `Step_number` | int |
| Record every dta | `Record_every_dT` | float |
| N Cycles | `N_Cycles` | int |
| I Range | `I_Range` | int (index enum) |
| E Range | `E_Range` | int (index enum) |
| Bandwidth | `Bandwidth` | int |

---

## Type de carte et fichiers associés

| Type de carte | Fichier technique | Firmware | FPGA |
|---------------|-------------------|----------|------|
| ESSENTIAL | `ca.ecc` | `kernel.bin` | `Vmp_ii_0437_a6.xlx` |
| PREMIUM | `ca4.ecc` | `kernel4.bin` | `vmp_iv_0395_aa.xlx` |
| DIGICORE | `ca5.ecc` | `kernel.bin` | *(aucun)* |

Le type de carte est détecté automatiquement après connexion via `GetChannelBoardType()`.

---

## Dépannage

| Problème | Cause probable | Solution |
|----------|---------------|----------|
| « DLL introuvable » | EC-Lab Development Package non installé ou chemin incorrect | Vérifier que `EClib64.dll` existe dans le chemin spécifié. Installer le package depuis le site BioLogic. |
| « Erreur de connexion » | IP incorrecte, instrument éteint, ou câble débranché | Vérifier l'adresse IP, l'alimentation de l'instrument, et la connectivité réseau (`ping 169.254.3.150`). |
| « Erreur firmware » | Type de carte non reconnu ou fichier firmware manquant | Vérifier que les fichiers `kernel.bin`/`kernel4.bin` et `.xlx` sont présents dans le dossier DLL. |
| « Paramètres invalides » | Valeur non numérique dans un champ | Vérifier les valeurs saisies (tension, durée, etc.). |
| Import kbio échoue | Dossier `Potentiostat/Examples/Python/` absent | Vérifier que l'arborescence du projet est complète. |
| Warnings Pylance sur les imports kbio | Normal — imports lazy via `sys.path` | Ces warnings n'affectent pas le fonctionnement à l'exécution. |
| Pas de données sur le graphique | Moins de 2 points acquis | Attendre quelques instants ou réduire l'intervalle `dta`. |

---

## Limitations connues

- L'interface ne gère que la technique **Chronoampérométrie (CA)**. D'autres techniques (OCV, CP, CV, etc.) nécessiteraient des adaptations.
- Les paramètres **Imax**, **Imin** et **|ΔQ| > ΔQM** sont affichés dans l'interface mais ne sont pas encore transmis comme paramètres ECC à l'instrument.
- Le graphique utilise un canvas tkinter natif (pas de matplotlib) : les performances sont optimisées par décimation des points en cas de grand nombre de mesures, mais restent limitées pour des acquisitions très longues (> 100 000 points).
- La fenêtre plein écran partage les données en temps réel avec la fenêtre principale mais possède son propre état de zoom/pan.

---

## Arborescence du projet liée

```
Programme/
├── MainUI/
│   ├── biologic_potentiostat_test_ui.py   ← Ce programme
│   ├── README_Potentiostat.md             ← Cette documentation
│   ├── main_ui.py                         ← Interface principale (microscope)
│   ├── thorlabs_camera_test_ui.py         ← Interface test caméra
│   └── newport_conex_test_ui.py           ← Interface test moteurs
│
├── Potentiostat/
│   └── Examples/
│       └── Python/
│           └── kbio/                      ← Bibliothèque BioLogic (kbio_api, kbio_types, etc.)
│
└── Camera/
    └── SDK/                               ← SDK caméra Thorlabs
```

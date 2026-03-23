# LaserBenchC++

Réécriture de l'interface Python en C++ / Qt6 pour le banc laser microscope.

## Récupérer et lancer le projet sur un autre PC

Aucune installation de compilateur ou de SDK n'est requise pour utiliser l'application.

1. Aller sur [github.com/D0gZys/PEC-UI-Code/releases](https://github.com/D0gZys/PEC-UI-Code/releases)
2. Télécharger `LaserBench_Windows_x64.zip`
3. Clic droit sur le zip → **Propriétés** → cocher **Débloquer** → OK
4. Extraire le zip dans un dossier
5. Double-cliquer sur `LaserBench.exe`

> Le zip contient l'exécutable et toutes les DLLs nécessaires. Aucune installation de Qt, Python, Visual Studio ou EC-Lab n'est requise.

---

## Ce qui a changé depuis la version initiale

### Intégration matérielle réelle (fini le mock)

| Composant | Avant | Maintenant |
|---|---|---|
| Moteurs Newport CONEX-CC | Interface simulée | Pilotage réel via `NewportConexHelper.exe` (C#/.NET) |
| Caméra Thorlabs | Interface simulée | Pilotage réel via Thorlabs TSI SDK natif |
| Potentiostat BioLogic | Interface simulée puis subprocess Python | Appel direct à `EClib64.dll` (SDK C natif) |

### Potentiostat BioLogic — réécriture complète

**Avant** : `BioLogicController` lançait un subprocess Python (`PotentiostatHelper.py`) et communiquait avec lui via stdin/stdout en JSON. Cela nécessitait Python installé avec le module `kbio`.

**Maintenant** : `BioLogicController` charge directement `EClib64.dll` via `LoadLibrary`/`GetProcAddress` au moment de la connexion. Toute la logique est en C++ natif. Python n'est plus utilisé nulle part dans le projet.

Fonctions implémentées :
- `BL_Connect` / `BL_Disconnect`
- `BL_LoadFirmware`
- `BL_LoadTechnique` + `BL_StartChannel` / `BL_StopChannel`
- `BL_GetData` (parsing des trames CA et OCV : temps, Ewe, I)
- `BL_GetCurrentValues`

Le champ **Chemin DLL** dans l'interface peut être laissé vide — l'application trouve `EClib64.dll` automatiquement dans son propre dossier.

### Nouveau widget Potentiostat 3D (`Potentiostat3DWidget`)

Visualisation 3D de la cartographie spatiale des mesures potentiostat (position X/Y + valeur mesurée).

---

## Ce qui ne change pas

- Le **workflow général** reste identique à la version Python : scan spatial, déclenchement potentiostat point par point, reconstruction de cartographie.
- L'**interface utilisateur** conserve la même structure (onglets Paramétrage / Mesure).
- Les **fichiers de configuration** et présets sont compatibles.
- La **caméra** et les **moteurs** fonctionnent de la même façon que dans la version Python.

---

## Contenu du zip de release

```
LaserBench.exe                  Application principale
EClib64.dll                     SDK BioLogic (connexion potentiostat)
blfind64.dll                    Dépendance de EClib64.dll
kernel.bin / kernel4.bin / kernel5.bin    Firmware potentiostat
Vmp_ii_0437_a6.xlx / Vmp_iv_0395_aa.xlx  Fichiers FPGA potentiostat
ca.ecc / ocv.ecc / ...          Techniques électrochimiques
Newport.CONEXCC.CommandInterface.dll      SDK moteurs Newport
NewportConexHelper.exe          Helper .NET pour les moteurs
thorlabs_tsi_camera_sdk.dll     SDK caméra Thorlabs
Qt6Core.dll / Qt6Gui.dll / ...  Runtime Qt6
```

---

## Build (développeurs uniquement)

Prérequis : Visual Studio 2022, Qt6, CMake 3.21+, les SDK dans les chemins attendus.

```powershell
cmake -S LaserBenchC++ -B LaserBenchC++/build -G "Visual Studio 17 2022" -A x64
cmake --build LaserBenchC++/build --config Release
```

L'exécutable et toutes les DLLs sont copiés automatiquement dans `build/Release/`.

---

## Architecture

```
LaserBenchC++/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── AppState.hpp / .cpp      État partagé de l'application
│   ├── hardware/
│   │   ├── DeviceContracts.hpp      Interfaces abstraites
│   │   ├── BioLogicController       Potentiostat (EClib64.dll natif)
│   │   ├── NewportConexController   Moteurs X/Y
│   │   ├── NewportConexHelper.cs    Helper .NET pour Newport
│   │   ├── ThorlabsCameraController Caméra
│   │   └── MockHardware             Simulation (tests sans matériel)
│   └── ui/
│       ├── MainWindow               Fenêtre principale
│       ├── CameraPreviewWidget      Aperçu caméra live
│       ├── PotentiostatGraphWidget  Graphe I/V en temps réel
│       ├── PotentiostatHeatmapWidget Cartographie 2D
│       └── Potentiostat3DWidget     Cartographie 3D
└── helpers/
    └── PotentiostatHelper.py        (obsolète, conservé pour référence)
```

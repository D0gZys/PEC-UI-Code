# LaserBenchC++

Interface Windows/Qt6 pour piloter un banc de mesure combine :

- camera Thorlabs
- platine XY Newport CONEX-CC
- potentiostat BioLogic

Cette partie `LaserBenchC++` est aujourd'hui la base active du projet. Elle remplace la version Python historique pour l'interface principale et le pilotage reel du materiel.

## Perimetre

L'application permet de :

- connecter la camera, regler exposition/gain et afficher le live
- connecter les moteurs X/Y, faire des jogs, du GoTo et du positionnement absolu
- calibrer la position et la taille du spot laser par objectif
- definir une zone de mesure directement sur l'image
- lancer un balayage point par point ou continu
- lancer une mesure simple sans zone, comme dans EC-Lab
- executer des mesures potentiostat `CA`, `OCV` et `CVA`
- visualiser les resultats en graphe, heatmap 2D et vue 3D
- exporter la cartographie au format `.gsf`

## Interface actuelle

L'UI est organisee en 3 pages :

### 1. Camera

Cette page regroupe :

- les parametres camera
- les boutons de connexion et de live
- le controle moteur
- le GoTo
- la definition de zone de mesure sur l'image
- les outils de mesure image

### 2. Potentiostat

Cette page regroupe :

- la connexion BioLogic
- le choix de la technique `CA / OCV / CVA`
- les parametres de la technique selectionnee
- les plages `E Range / I Range`
- les reglages de bande passante et d'acquisition

### 3. Resultat

Cette page regroupe :

- le lancement et l'arret de la mesure
- le graphe temps reel
- la heatmap 2D
- la vue 3D
- l'export `.gsf`

Comportement important :

- si une zone est definie, l'application lance une cartographie
- si aucune zone n'est definie, l'application propose une mesure simple sans deplacement moteur

## Techniques potentiostat

### CA

Chronoamperometrie.

Usage typique :

- mesure simple `I = f(t)`
- acquisition sur chaque point d'une cartographie

### OCV

Open Circuit Voltage.

Usage typique :

- suivi de potentiel en fonction du temps

### CVA

Cyclic Voltammetry Advanced.

La version actuelle a ete ajustee pour se comporter comme attendu sur le SP-50, y compris en mesure simple sans zone.

## Balayage et cartographie

Deux modes de balayage sont disponibles :

### Point par point

- le moteur se place sur un point
- attend le temps de stabilisation
- la mesure est prise
- le moteur passe au point suivant

### Continu

- le moteur se deplace a vitesse constante sur chaque ligne
- l'acquisition se fait soit tous les `X mm`, soit toutes les `X s`
- le saut de ligne est parametre
- la trajectoire effective peut etre allongee pour respecter le pas demande jusqu'au bord
- la vitesse est verifiee pour rester compatible avec la cadence d'acquisition

## Resultats et visualisation

Les widgets principaux sont :

- `CameraPreviewWidget` : image live et overlays
- `PotentiostatGraphWidget` : graphe temps reel
- `PotentiostatHeatmapWidget` : cartographie 2D
- `Potentiostat3DWidget` : vue 3D

Le graphe affiche selon le mode :

- `I = f(t)`
- `Ewe = f(t)`
- `I = f(Ewe)`
- `Ewe = f(I)`

Le dernier point acquis peut etre mis en evidence visuellement.

## Architecture du code

Arborescence utile :

```text
LaserBenchC++/
|-- CMakeLists.txt
|-- calibration.json
|-- cmake/
|   |-- CopyIfNotExists.cmake
|   `-- DeployQtRuntime.cmake
`-- src/
    |-- main.cpp
    |-- core/
    |   `-- AppState.*
    |-- hardware/
    |   |-- BioLogicController.*
    |   |-- DeviceContracts.hpp
    |   |-- MockHardware.*
    |   |-- NewportConexBridge.cpp
    |   |-- NewportConexBridgeApi.hpp
    |   |-- NewportConexController.*
    |   |-- NewportConexHelper.cs
    |   `-- ThorlabsCameraController.*
    `-- ui/
        |-- CameraPreviewWidget.*
        |-- MainWindow.*
        |-- Potentiostat3DWidget.*
        |-- PotentiostatGraphWidget.*
        `-- PotentiostatHeatmapWidget.*
```

Repartition des responsabilites :

- `MainWindow` orchestre l'application et les workflows materiel/UI
- `BioLogicController` encapsule `EClib64.dll`
- `NewportConexController` pilote les axes via `NewportConexHelper.exe`
- `ThorlabsCameraController` charge dynamiquement le SDK camera

## Build et execution

### Prerequis

Le projet est Windows-only.

Prerequis minimaux :

- Windows 10 ou 11 x64
- Visual Studio 2022 avec outils C++
- CMake 3.21+
- Qt 6 avec `Widgets`
- SDK natif Thorlabs
- SDK BioLogic EC-Lab Development Package
- DLL .NET Newport CONEX-CC
- compilateur C# .NET Framework `csc.exe`

Le `CMakeLists.txt` suppose ces emplacements relatifs :

- `../MotorController/lib/Newport.CONEXCC.CommandInterface.dll`
- `../Potentiostat/Examples/C-C++/include`
- `../Potentiostat/lib`
- `../Camera/SDK/Native Toolkit`

### Configuration

Depuis la racine du depot :

```powershell
cmake -S LaserBenchC++ -B LaserBenchC++/build -G "Visual Studio 17 2022" -A x64
```

### Compilation

```powershell
cmake --build LaserBenchC++/build --config Release
```

Le binaire principal est genere dans :

```text
LaserBenchC++/build/Release/LaserBench.exe
```

Le post-build fait aussi :

- la copie de `calibration.json` si absent
- la copie des DLL BioLogic
- la copie des DLL Thorlabs
- la copie de la DLL Newport
- la compilation de `NewportConexHelper.exe`
- le deploiement des runtimes MSVC
- le deploiement Qt via `DeployQtRuntime.cmake`

### Execution

Si le dossier `Release` est complet, lancer :

```powershell
LaserBenchC++\build\Release\LaserBench.exe
```

En usage normal, il n'est pas necessaire d'avoir Qt Creator ou Visual Studio ouverts.

### Release Windows

Une release exploitable doit contenir au minimum :

- `LaserBench.exe`
- `NewportConexHelper.exe`
- `Newport.CONEXCC.CommandInterface.dll`
- les DLL Thorlabs
- `EClib64.dll`
- `blfind64.dll`
- les fichiers `.ecc`
- les fichiers firmware BioLogic
- `calibration.json`
- les DLL Qt et leurs plugins

## Runtime, logs et diagnostic

L'application cree automatiquement un log de session dans :

```text
LaserBenchC++/build/Release/logs/
```

Format typique :

```text
LaserBench_YYYYMMDD_HHMMSS.log
```

Le menu `Aide` contient :

- `Diagnostic runtime`
- `Ouvrir dossier logs`

Le diagnostic runtime verifie la presence des fichiers critiques du package avant usage.

## Calibration

Le fichier `calibration.json` stocke :

- la position du spot laser
- sa taille
- les reglages par objectif

Il est charge au demarrage depuis le dossier de l'application, pas depuis le dossier source.

## Export

Les cartographies peuvent etre exportees au format `Gwyddion Simple Field` :

- extension `.gsf`
- usage prevu dans Gwyddion

## Limitations connues

- validation complete uniquement possible sur le vrai banc
- pas de suite de tests automatisee complete
- architecture encore tres centralisee autour de `MainWindow`
- build et execution limites a Windows

## Conseils pour developper ici

- tester d'abord dans `LaserBenchC++/build/Release`
- verifier `Aide > Diagnostic runtime` apres une recompilation
- relire les logs de session avant de conclure a un bug SDK ou materiel
- pour les modifications potentiostat, verifier a la fois la mesure simple et le mode avec zone

## Fichiers a connaitre

- `src/ui/MainWindow.cpp` : logique principale UI + workflows
- `src/hardware/BioLogicController.cpp` : interface BioLogic
- `src/hardware/NewportConexController.cpp` : interface moteurs
- `src/hardware/ThorlabsCameraController.cpp` : interface camera
- `src/ui/PotentiostatGraphWidget.cpp` : rendu du graphe
- `src/ui/PotentiostatHeatmapWidget.cpp` : rendu 2D
- `src/ui/Potentiostat3DWidget.cpp` : vue 3D

## Historique

Le depot racine contient encore la documentation et des outils lies a l'ancienne base Python, mais pour l'interface actuelle il faut considerer `LaserBenchC++` comme la reference principale.

# LaserBenchC++

Reecriture progressive de l'interface Python en `C++ / Qt6`.

L'objectif n'est pas de traduire le code Python ligne par ligne, mais de reconstruire une application plus propre, plus performante et plus maintenable, en gardant la version Python comme reference fonctionnelle.

## Cible fonctionnelle

L'application devra, a terme:

- piloter la camera du microscope;
- piloter les axes `X/Y` de la platine;
- gerer le positionnement `GoTo` a partir de l'image;
- permettre la selection d'une zone sur l'echantillon;
- executer un balayage spatial sur cette zone;
- declencher la mesure au potentiostat;
- reconstruire une cartographie du courant mesure en fonction de la position eclairee.

## Ce que contient ce premier squelette

- une base `CMake + Qt6 Widgets`;
- une fenetre principale structuree en onglets `Parametrage` et `Mesure`;
- des modeles metier de base pour le scan et les presets;
- des interfaces materielles abstraites pour camera, platine et potentiostat;
- des implementations simulees pour preparer l'integration reelle.

## Arborescence

```text
LaserBenchC++/
|- CMakeLists.txt
|- README.md
`- src/
   |- main.cpp
   |- core/
   |  |- AppState.hpp
   |  `- AppState.cpp
   |- hardware/
   |  |- DeviceContracts.hpp
   |  |- MockHardware.hpp
   |  `- MockHardware.cpp
   `- ui/
      |- MainWindow.hpp
      `- MainWindow.cpp
```

## Build

Exemple de configuration:

```powershell
cmake -S LaserBenchC++ -B LaserBenchC++/build -G "Ninja"
cmake --build LaserBenchC++/build
```

Avec Visual Studio:

```powershell
cmake -S LaserBenchC++ -B LaserBenchC++/build -G "Visual Studio 17 2022" -A x64
cmake --build LaserBenchC++/build --config Debug
```

## Feuille de route proposee

1. stabiliser ce squelette et la structure des couches;
2. integrer les moteurs;
3. integrer la camera;
4. reconstruire l'overlay laser et la selection de zone;
5. integrer le potentiostat;
6. executer le workflow complet de cartographie.

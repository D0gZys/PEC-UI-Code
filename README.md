# LaserBench C++ - source minimal

Ce paquet source contient uniquement le necessaire pour developper et
recompiler LaserBench C++.

Pour utiliser directement le logiciel sans compiler, telecharger plutot
l'archive Windows de la release :

```text
LaserBenchC++_2026-06-05_release.zip
```

## Contenu

- `LaserBenchC++/` : application Qt/C++ principale.
- `Camera/SDK/Native Toolkit/` : headers, helpers et DLL Thorlabs necessaires.
- `MotorController/lib/Newport.CONEXCC.CommandInterface.dll` : dependance Newport.
- `Potentiostat/Examples/C-C++/include/` : headers BioLogic.
- `Potentiostat/lib/` : DLL et fichiers BioLogic necessaires.

Le source minimal exclut volontairement les scripts Python, interfaces de test,
outils, anciens zips, dossiers de build et logs locaux.

## Environnement requis

Installer sur Windows 64 bits :

- CMake 3.21 ou plus recent.
- Visual Studio ou Visual Studio Build Tools avec MSVC C++ x64.
- Windows SDK.
- Qt 6 pour MSVC 64 bits, avec le module Widgets.
- .NET Framework 4.x avec `csc.exe`, utilise pour compiler
  `NewportConexHelper.exe`.

Exemple de chemin Qt courant :

```text
C:\Qt\6.x.x\msvc2022_64
```

## Compilation

Depuis la racine du paquet source :

```powershell
cmake -S LaserBenchC++ -B LaserBenchC++\build -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build LaserBenchC++\build --config Release
```

Remplacer `C:\Qt\6.x.x\msvc2022_64` par le chemin Qt installe sur la machine.

L'executable genere se trouve ensuite ici :

```text
LaserBenchC++\build\Release\LaserBench.exe
```

Le build copie automatiquement dans `build\Release` les dependances Newport,
BioLogic, Thorlabs, Qt et runtime MSVC necessaires. L'icone de l'executable est
integree via :

```text
LaserBenchC++\Icon\LaserBench.ico
LaserBenchC++\LaserBench.rc
```

## Erreurs frequentes

### Qt6 introuvable

Indiquer le chemin Qt avec `-DCMAKE_PREFIX_PATH=...` ou lancer CMake depuis un
environnement Qt configure.

### `csc.exe not found`

Installer le .NET Framework Developer Pack ou les composants .NET Framework de
Visual Studio. Le projet cherche `csc.exe` dans les dossiers .NET Framework
Windows standards.

### Dependances Newport, Thorlabs ou BioLogic introuvables

Verifier que vous avez telecharge le **Source code** de cette release minimale,
pas seulement l'archive executable. Les dossiers `Camera`, `MotorController` et
`Potentiostat` doivent etre presents a cote de `LaserBenchC++`.

### Warning `pwsh.exe` pendant le build

Ce warning peut apparaitre pendant le deploiement de runtime. Il n'empeche pas
forcement la generation de `LaserBench.exe` si le build se termine avec succes.

## Developpement

Les fichiers principaux sont dans :

```text
LaserBenchC++\src
LaserBenchC++\src\ui
LaserBenchC++\src\hardware
LaserBenchC++\src\core
```

Apres modification, recompiler avec :

```powershell
cmake --build LaserBenchC++\build --config Release
```

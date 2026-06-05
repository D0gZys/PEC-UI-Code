# Presentation fonctionnelle de LaserBench

Ce document presente l'application LaserBench comme on la presenterait a une personne qui decouvre le projet. Il explique ce que fait l'application, a quoi servent les differentes parties de l'interface et comment les fonctionnalites s'enchainent pendant une mesure.

Le projet actif est le dossier `LaserBenchC++`. C'est l'interface Qt/C++ utilisee pour piloter le banc reel.

## Idee generale

LaserBench est une application de pilotage et de mesure pour un banc experimental qui combine :

- une camera Thorlabs pour observer l'echantillon ;
- une platine motorisee XY Newport pour deplacer l'echantillon ;
- un potentiostat BioLogic pour faire les mesures electrochimiques ;
- un spot laser fixe, visible dans l'image camera comme point de reference.

L'objectif de l'application est de permettre a l'utilisateur de voir l'echantillon, de choisir precisement une zone ou un point d'interet, de deplacer la platine, de lancer une mesure potentiostat, puis de visualiser et exporter les resultats.

La chose importante a comprendre est que le laser est considere fixe dans l'image. Quand l'utilisateur clique sur une cible ou definit une zone, LaserBench deplace la platine pour amener l'echantillon sous le laser.

## Ce que permet l'application

LaserBench permet de :

- connecter les appareils du banc depuis une interface unique ;
- afficher le flux live de la camera ;
- regler l'exposition et le gain camera ;
- choisir l'objectif utilise ;
- afficher et calibrer la position du laser dans l'image ;
- mesurer des distances, diametres et rectangles directement sur l'image ;
- deplacer la platine en X/Y ;
- faire un GoTo en cliquant dans l'image ;
- definir une zone de mesure sur l'image camera ;
- configurer un balayage point par point ou continu ;
- lancer une mesure simple sans deplacement ;
- lancer une cartographie avec deplacement moteur ;
- utiliser les techniques potentiostat CA, OCV et CVA ;
- suivre l'acquisition en temps reel ;
- afficher les resultats sous forme de courbes, carte 2D et surface 3D ;
- importer un ancien CSV LaserBench ;
- exporter les resultats en plusieurs formats.

## Parcours typique d'une mesure

Un usage standard ressemble a ceci :

1. L'utilisateur lance LaserBench.
2. Il connecte les moteurs, la camera et le potentiostat.
3. Il demarre le live camera.
4. Il choisit l'objectif utilise, par exemple `4x`, `10x` ou `50x`.
5. Il verifie que le cercle rouge correspond bien au spot laser.
6. Il deplace la platine avec les boutons, le clavier ou le GoTo.
7. Il definit une zone de mesure directement sur l'image.
8. Il choisit les parametres de balayage.
9. Il choisit la technique electrochimique et ses parametres.
10. Il lance l'acquisition.
11. Il suit les resultats en direct.
12. Il exporte les donnees et les visualisations.

Ce parcours montre le role principal de l'application : faire le lien entre l'image camera, les coordonnees moteur et la mesure potentiostat.

## Organisation de l'interface

L'application est organisee autour de trois onglets principaux :

- `Camera`
- `Resultat`
- `Import`

Une fenetre de connexion des appareils s'ouvre au demarrage. Elle peut aussi etre rouverte depuis les menus.

## Fichiers, bibliotheques et devkits utilises

LaserBench s'appuie sur plusieurs SDK constructeurs et bibliotheques externes. Cette section resume ce qui est utilise pour chaque partie du banc.

### Socle logiciel de l'application

Pour construire et lancer l'interface C++, le projet utilise :

- Windows 64 bits ;
- C++20 ;
- CMake ;
- Visual Studio / MSVC ;
- Qt 6, principalement le module `Widgets` ;
- `windeployqt` pour copier les fichiers Qt necessaires dans le dossier de l'executable ;
- les runtimes MSVC copies pendant le build.

Fichiers importants cote projet :

- `LaserBenchC++/CMakeLists.txt` : configuration de build principale ;
- `LaserBenchC++/cmake/DeployQtRuntime.cmake` : deploiement des DLL et plugins Qt ;
- `LaserBenchC++/cmake/CopyIfNotExists.cmake` : copie de `calibration.json` si absent ;
- `LaserBenchC++/calibration.json` : presets de position du laser par objectif ;
- `LaserBenchC++/src/main.cpp` : point d'entree de l'application ;
- `LaserBenchC++/src/ui/MainWindow.cpp` : interface principale et logique de workflow.

Fichiers Qt attendus au runtime :

- `Qt6Core.dll`
- `Qt6Gui.dll`
- `Qt6Widgets.dll`
- `platforms/qwindows.dll`
- plugins Qt utiles dans `styles/`, `imageformats/` et `iconengines/`

### Camera Thorlabs

La camera est pilotee avec le Thorlabs Native C SDK.

Emplacement attendu dans le depot :

```text
Camera/SDK/Native Toolkit/
```

Fichiers et dossiers utilises :

- `Camera/SDK/Native Toolkit/include/tl_camera_sdk.h`
- `Camera/SDK/Native Toolkit/include/tl_color_enum.h`
- `Camera/SDK/Native Toolkit/load_dll_helpers/tl_camera_sdk_load.c`
- `Camera/SDK/Native Toolkit/load_dll_helpers/tl_camera_sdk_load.h`
- `Camera/SDK/Native Toolkit/load_dll_helpers/tl_mono_to_color_processing_load.c`
- `Camera/SDK/Native Toolkit/load_dll_helpers/tl_mono_to_color_processing_load.h`
- `Camera/SDK/Native Toolkit/dlls/Native_64_lib/`

DLL principales copiees dans le dossier de l'application :

- `thorlabs_tsi_camera_sdk.dll`
- `thorlabs_tsi_mono_to_color_processing.dll`
- `thorlabs_ccd_tsi_usb.dll`

Fichiers du projet qui utilisent ces dependances :

- `LaserBenchC++/src/hardware/ThorlabsCameraController.hpp`
- `LaserBenchC++/src/hardware/ThorlabsCameraController.cpp`

Fonctions couvertes par ce controleur :

- recherche des cameras ;
- ouverture/fermeture camera ;
- reglage exposition ;
- reglage gain ;
- demarrage/stop du live ;
- recuperation des frames ;
- conversion mono/couleur quand necessaire.

### Moteurs Newport CONEX-CC

Les moteurs Newport sont pilotes avec la bibliotheque .NET constructeur.

Fichier constructeur utilise :

```text
MotorController/lib/Newport.CONEXCC.CommandInterface.dll
```

Cette DLL est une dependance .NET. L'application C++ ne l'appelle pas directement. Elle passe par un petit executable C# compile pendant le build.

Fichiers du projet pour les moteurs :

- `LaserBenchC++/src/hardware/NewportConexController.hpp`
- `LaserBenchC++/src/hardware/NewportConexController.cpp`
- `LaserBenchC++/src/hardware/NewportConexHelper.cs`

Fichiers generes ou copies au runtime :

- `NewportConexHelper.exe`
- `Newport.CONEXCC.CommandInterface.dll`

Outils necessaires au build :

- compilateur C# .NET Framework `csc.exe` ;
- `System.Web.Extensions.dll`, utilise pour la serialisation JSON cote helper ;
- DLL Newport `Newport.CONEXCC.CommandInterface.dll`.

Principe de fonctionnement :

- `NewportConexController.cpp` lance `NewportConexHelper.exe` ;
- le C++ envoie des commandes JSON via stdin ;
- le helper C# pilote la DLL Newport ;
- le helper renvoie des reponses JSON via stdout.

Fonctions couvertes :

- scan des ports COM ;
- connexion X/Y ;
- deconnexion ;
- homing ;
- mouvement relatif ;
- mouvement absolu ;
- mouvement absolu sans attente ;
- changement de vitesse ;
- stop axe ;
- lecture position/etat/erreur.

### Potentiostat BioLogic

Le potentiostat est pilote avec le package de developpement EC-Lab / BioLogic.

Emplacements utilises par le projet :

```text
Potentiostat/Examples/C-C++/include
Potentiostat/lib
```

Header principal utilise :

- `BLStructs.h`

DLL principales attendues :

- `EClib64.dll`
- `blfind64.dll`

Fichiers du projet pour le potentiostat :

- `LaserBenchC++/src/hardware/BioLogicController.hpp`
- `LaserBenchC++/src/hardware/BioLogicController.cpp`

Fonctions BioLogic chargees dynamiquement depuis `EClib64.dll` :

- `BL_Connect`
- `BL_Disconnect`
- `BL_LoadFirmware`
- `BL_LoadTechnique`
- `BL_StartChannel`
- `BL_StopChannel`
- `BL_GetData`
- `BL_GetCurrentValues`
- `BL_GetChannelBoardType`
- `BL_DefineSglParameter`
- `BL_DefineBoolParameter`
- `BL_DefineIntParameter`
- `BL_ConvertNumericIntoSingle`
- `BL_ConvertTimeIntoSeconds`
- `BL_GetErrorMsg`

Fichiers techniques `.ecc` utilises selon la technique et le type de carte :

- CA : `ca.ecc`, `ca4.ecc`, `ca5.ecc`
- OCV : `ocv.ecc`, `ocv4.ecc`, `ocv5.ecc`
- CVA native : `biovscan.ecc`
- CVA emulee / scans intermediaires : `vscan.ecc`, `vscan4.ecc`, `vscan5.ecc`, `cv.ecc`, `cv4.ecc`, `cv5.ecc`

Fichiers firmware BioLogic attendus selon le materiel :

- `kernel.bin`
- `kernel4.bin`
- `kernel5.bin`
- `Vmp_ii_0437_a6.xlx`
- `Vmp_iv_0395_aa.xlx`

Techniques gerees dans l'application :

- CA ;
- OCV ;
- CVA.

### Fichiers de visualisation et d'export

Les resultats ne dependent pas d'une bibliotheque externe specialisee pour etre affiches. Les visualisations sont dessinees avec Qt.

Widgets internes :

- `LaserBenchC++/src/ui/PotentiostatGraphWidget.cpp`
- `LaserBenchC++/src/ui/PotentiostatHeatmapWidget.cpp`
- `LaserBenchC++/src/ui/Potentiostat3DWidget.cpp`
- `LaserBenchC++/src/ui/CameraPreviewWidget.cpp`

Formats geres :

- CSV : export texte pour Excel/Python ;
- MPT : format texte type EC-Lab ;
- GSF : format Gwyddion Simple Field ;
- TIFF : image 2D/3D ecrite par un encodeur minimal interne ;
- PDF : rapport genere avec les outils Qt.

## Fenetre de connexion

La fenetre `Connexion des appareils` regroupe les trois familles de materiel.

### Connexion moteurs

La partie moteurs sert a connecter la platine Newport CONEX-CC.

L'utilisateur peut :

- rechercher les ports COM disponibles ;
- choisir le port de l'axe X ;
- choisir le port de l'axe Y ;
- inverser rapidement les ports si le cablage est inverse ;
- connecter les deux axes ;
- lancer le homing ;
- deconnecter les moteurs.

Une fois connectes, les moteurs sont utilises pour les deplacements manuels, les GoTo et les balayages automatiques.

### Connexion camera

La partie camera sert a connecter la camera Thorlabs.

L'utilisateur peut :

- rechercher les cameras detectees ;
- selectionner la camera ;
- la connecter ;
- demarrer ou arreter le live ;
- la deconnecter.

Le live camera est indispensable pour utiliser le GoTo image et la selection de zone.

### Connexion potentiostat

La partie potentiostat sert a connecter l'appareil BioLogic.

L'utilisateur renseigne :

- le chemin de la DLL BioLogic ;
- l'adresse IP de l'appareil ;
- le canal utilise.

Il peut ensuite :

- connecter le potentiostat ;
- charger le firmware ;
- deconnecter l'appareil.

Une fois connecte, le potentiostat devient disponible dans l'onglet `Resultat`.

## Onglet Camera

L'onglet `Camera` est l'ecran de preparation et de positionnement.

Il sert a voir l'echantillon, controler la platine et definir les zones de mesure.

### Live camera

La zone centrale affiche l'image camera.

L'utilisateur peut :

- demarrer ou arreter le live ;
- regler l'exposition ;
- regler le gain ;
- zoomer dans l'image avec la molette ;
- lire les coordonnees du curseur en pixels et en micrometres.

Le zoom est numerique : il aide a inspecter l'image, mais ne change pas l'acquisition camera.

### Choix de l'objectif

L'objectif selectionne indique a l'application l'echelle de l'image.

Les objectifs disponibles sont :

- `4x`
- `10x`
- `50x`

Cette information est essentielle parce qu'elle permet de convertir les pixels de l'image en distances reelles. Elle est utilisee pour les mesures sur l'image, le GoTo et la definition des zones de scan.

### Spot laser

Le laser est represente par un cercle rouge dans l'image.

Ce cercle montre :

- la position estimee du laser ;
- le diametre apparent du spot ;
- le centre utilise comme reference de positionnement.

La position du laser est calibree par objectif. Cela permet d'avoir une position differente pour le `4x`, le `10x` et le `50x`.

Si le cercle rouge n'est pas aligne avec le vrai spot laser, l'utilisateur peut corriger sa position dans le menu `Aide > Calibrage`.

### Controle manuel de la platine

La partie moteur de l'onglet camera permet de deplacer la platine.

L'utilisateur peut :

- lire la position actuelle X/Y ;
- saisir une position absolue ;
- envoyer la platine a cette position ;
- choisir une vitesse de deplacement ;
- choisir un pas de jog ;
- deplacer la platine avec les boutons fleches ;
- utiliser les fleches du clavier pour un jog continu.

Ces fonctions servent a amener l'echantillon dans la bonne zone avant de lancer une mesure.

### GoTo dans l'image

Le GoTo est une des fonctions centrales de LaserBench.

Principe :

1. L'utilisateur clique sur `GoTo`.
2. Il clique dans l'image camera sur la zone qu'il veut amener sous le laser.
3. LaserBench calcule l'ecart entre ce point et le centre du laser.
4. L'application convertit cet ecart en mouvement moteur.
5. La platine se deplace pour aligner la zone cliquee avec le laser.

Le GoTo evite de devoir calculer manuellement les deplacements X/Y. L'utilisateur raisonne directement dans l'image.

### Calibrage du GoTo

Le GoTo depend :

- de l'objectif selectionne ;
- de la position du spot laser ;
- du sens reel des axes ;
- de petites erreurs mecaniques possibles.

Le dialogue de calibrage permet donc de regler :

- inversion X ;
- inversion Y ;
- correction des mouvements X+ ;
- correction des mouvements X- ;
- correction des mouvements Y+ ;
- correction des mouvements Y-.

Ces reglages permettent d'adapter l'application au montage reel.

### Selection d'une zone de mesure

L'utilisateur peut definir une zone de mesure directement sur l'image.

Il clique sur `Definir zone`, puis choisit deux coins dans l'image. LaserBench transforme ces deux coins en coordonnees moteur.

Cette zone sert ensuite de base pour une cartographie.

Apres la selection, l'application ouvre les parametres de balayage pour definir comment la zone sera parcourue.

### Outils de mesure image

A droite de l'image, des outils permettent d'annoter et mesurer :

- une distance avec la regle ;
- un diametre avec l'outil cercle ;
- une largeur/hauteur avec l'outil rectangle ;
- supprimer une annotation avec la gomme.

Ces outils sont utiles pour estimer rapidement une taille, verifier un diametre de spot ou mesurer une zone d'interet.

## Parametres de balayage

Quand une zone est definie, LaserBench peut la parcourir automatiquement.

Deux grands modes existent.

### Balayage point par point

Dans ce mode, la platine s'arrete sur chaque point.

Pour chaque point :

1. la platine se place ;
2. elle attend une duree choisie ;
3. le potentiostat mesure ;
4. l'application enregistre la valeur ;
5. la platine va au point suivant.

Ce mode est simple a comprendre et robuste. Il est adapte quand on veut que chaque point soit mesure apres stabilisation.

L'utilisateur regle :

- le pas entre points ;
- la duree d'attente ;
- le nombre de mesures par point.

Si plusieurs mesures sont prises au meme point, LaserBench calcule une moyenne et conserve aussi les valeurs individuelles.

### Balayage continu

Dans ce mode, la platine ne s'arrete pas a chaque point. Elle avance a vitesse constante pendant que l'application prend des mesures a intervalles reguliers.

L'utilisateur regle :

- la vitesse moteur ;
- l'intervalle d'acquisition ;
- le saut entre lignes ou colonnes.

L'acquisition peut etre planifiee :

- tous les X millimetres ;
- ou toutes les X secondes.

Ce mode est plus rapide, mais demande une bonne synchronisation entre le mouvement moteur et la lecture potentiostat.

### Parcours rectangle

Pour une zone rectangle, LaserBench peut choisir comment parcourir la grille.

L'utilisateur peut selectionner :

- le coin de depart ;
- l'axe principal du balayage ;
- le type de parcours.

Deux types de parcours sont disponibles :

- `Zig-zag` : le sens alterne a chaque ligne ;
- `One-way` : chaque ligne est mesuree dans le meme sens, avec retour sans acquisition.

L'application affiche un apercu visuel du chemin avant validation.

### Estimation de duree

Le dialogue de balayage donne une estimation avant lancement :

- taille de la zone ;
- diametre laser ;
- nombre de points ;
- taille de grille ;
- duree par ligne ;
- duree totale.

Cela aide l'utilisateur a verifier que les parametres sont realistes avant de lancer une mesure longue.

## Onglet Resultat

L'onglet `Resultat` sert a configurer et lancer les mesures, puis a visualiser les donnees.

Il contient :

- les boutons de lancement et d'arret ;
- l'etat de la mesure ;
- le courant courant ;
- le nombre de points acquis ;
- la progression ;
- la duree ;
- les parametres potentiostat ;
- les courbes ;
- la cartographie 2D ;
- la vue 3D ;
- l'export.

### Lancement et arret

Le bouton lecture lance l'acquisition.

Le bouton stop arrete :

- la boucle d'acquisition ;
- les moteurs si un mouvement est en cours ;
- le canal potentiostat.

L'application essaie de terminer proprement et garde une trace de la raison d'arret.

### Choix de l'electrode

L'utilisateur peut choisir :

- `Anode`
- `Cathode`

Ce choix modifie la facon dont le courant est affiche dans les visualisations. En mode cathode, le signe est inverse pour rendre la lecture plus intuitive.

### Techniques potentiostat

LaserBench propose trois techniques principales.

#### CA

CA signifie chronoamperometrie.

L'utilisateur choisit :

- la tension `Ewe` ;
- la reference ;
- la plage de potentiel ;
- la plage de courant ;
- la bande passante ;
- le nombre de cycles.

La CA peut etre utilisee :

- comme mesure simple sans deplacement ;
- comme technique de mesure pour une cartographie.

Dans le cas d'une cartographie, LaserBench deplace la platine et associe les mesures de courant aux positions X/Y.

#### OCV

OCV signifie Open Circuit Voltage.

L'utilisateur choisit :

- la duree de repos ;
- l'intervalle d'enregistrement en temps ;
- le seuil d'enregistrement en potentiel ;
- la plage de potentiel.

Dans l'etat actuel de l'interface, OCV est une mesure simple : elle ne lance pas de balayage moteur.

#### CVA

CVA signifie Cyclic Voltammetry Advanced.

L'utilisateur peut regler :

- le potentiel initial ;
- les potentiels de balayage ;
- la vitesse de scan ;
- les temps de maintien ;
- les intervalles d'enregistrement ;
- les plages ;
- la bande passante ;
- le nombre de cycles ;
- les parametres de fin de scan.

Dans l'etat actuel de l'interface, CVA est aussi une mesure simple : elle ne lance pas de balayage moteur.

### Mesure simple

Une mesure simple est une acquisition sans deplacement moteur.

Elle est utilisee lorsque :

- aucune zone n'a ete definie ;
- l'utilisateur choisit volontairement de ne pas utiliser la zone ;
- la technique choisie est OCV ou CVA.

Pendant une mesure simple, LaserBench affiche l'evolution de la mesure sous forme de courbe.

### Cartographie

Une cartographie est une mesure avec deplacement de la platine.

Elle utilise :

- une zone definie dans l'image ;
- un plan de balayage ;
- une technique potentiostat ;
- les positions moteur ;
- les mesures de courant et de potentiel.

Le resultat est une matrice de valeurs, une valeur par point ou cellule de la zone.

Dans l'etat actuel, le workflow de cartographie est principalement associe a la CA.

## Visualisation des resultats

LaserBench affiche les donnees pendant et apres la mesure.

### Courbes

L'utilisateur peut choisir plusieurs types de graphes :

- courant en fonction du temps ;
- potentiel en fonction du temps ;
- courant en fonction du potentiel ;
- potentiel en fonction du courant.

Le dernier point acquis est mis en evidence.

Pour les mesures point par point, le graphe peut aussi montrer les phases de mouvement et les phases de mesure.

### Carte 2D

La carte 2D affiche le courant mesure sur la zone.

Chaque cellule correspond a une position de mesure. La couleur represente l'intensite du courant.

L'utilisateur peut :

- voir la distribution spatiale du courant ;
- survoler les cellules pour avoir des informations ;
- cliquer une cellule pour afficher le detail.

Le detail cellule contient les informations utiles :

- ligne et colonne ;
- position X/Y ;
- temps ;
- courant ;
- potentiel ;
- mesures brutes si plusieurs points ont ete pris au meme endroit.

### Surface 3D

La vue 3D represente la meme information que la carte 2D, mais sous forme de surface.

L'utilisateur peut :

- tourner la surface avec la souris ;
- zoomer avec la molette ;
- visualiser les variations de courant comme des variations de hauteur.

Cette vue est utile pour reperer rapidement des reliefs, pics ou zones contrastees.

## Import CSV

L'onglet `Import` sert a rouvrir des resultats deja exportes.

L'utilisateur choisit un fichier CSV LaserBench. L'application reconstruit ensuite les donnees et les affiche comme si elles venaient d'etre mesurees.

Deux types de CSV sont reconnus :

- mesure simple ;
- cartographie.

Pour une cartographie, l'import reconstruit :

- la grille ;
- les positions ;
- les courants ;
- les potentiels ;
- les mesures detaillees par cellule si elles existent ;
- la carte 2D ;
- la vue 3D.

Cet onglet permet de consulter et analyser une mesure sans reconnecter le banc.

## Export des resultats

Apres une mesure, LaserBench peut exporter plusieurs formats.

### CSV

Le CSV est le format principal pour retraiter les donnees dans Excel, Python ou un autre outil.

Il contient :

- les temps ;
- les courants ;
- les potentiels ;
- les positions X/Y pour les cartographies ;
- les moyennes ;
- les mesures detaillees si disponibles.

### MPT

Le format `.mpt` reprend un style compatible EC-Lab ASCII.

Il est utile pour relire ou visualiser les donnees avec des outils proches de l'environnement BioLogic.

### GSF

Le format `.gsf` est destine a Gwyddion.

Il permet d'ouvrir la cartographie comme un champ 2D avec des dimensions physiques.

### TIFF

LaserBench peut exporter :

- l'image de la heatmap 2D ;
- l'image de la surface 3D.

Ces exports sont utiles pour les rapports, presentations ou archives visuelles.

### PDF

Le rapport PDF rassemble les informations importantes d'une cartographie :

- parametres de mesure ;
- dimensions de zone ;
- objectif utilise ;
- diametre laser ;
- duree ;
- statistiques de courant ;
- commentaire utilisateur optionnel ;
- image de la zone ;
- graphe ;
- carte 2D ;
- surface 3D ;
- schema du chemin de balayage.

Le PDF est pense comme un document de synthese partageable.

## Logs et diagnostic

LaserBench enregistre automatiquement des logs.

### Logs de session

Un log de session est cree au demarrage.

Il garde l'historique general :

- connexions ;
- erreurs ;
- changements de parametres ;
- demarrages et arrets ;
- exports ;
- imports.

### Logs de mesure

Chaque acquisition cree aussi un log de mesure detaille.

Il contient :

- les parametres de la mesure ;
- la zone ;
- le plan de scan ;
- les mouvements ;
- les acquisitions ;
- les erreurs eventuelles ;
- l'etat final.

Ces logs sont importants pour comprendre ce qui s'est passe pendant une experience, surtout en cas de probleme de synchronisation ou de comportement inattendu.

### Diagnostic runtime

Le menu `Diagnostic runtime` verifie que les fichiers necessaires sont bien presents dans le dossier de l'executable :

- DLL Qt ;
- plugin Qt Windows ;
- DLL Thorlabs ;
- helper Newport ;
- DLL Newport ;
- DLL BioLogic ;
- fichiers `.ecc` ;
- firmwares ;
- fichier de calibration.

Cette fonction aide a verifier qu'une release est complete avant de travailler sur le banc.

## Securites et limites

LaserBench integre plusieurs protections :

- il refuse certains mouvements si les appareils ne sont pas connectes ;
- il controle les limites moteur principales ;
- il valide les champs numeriques ;
- il stoppe les moteurs lors d'un arret d'acquisition ;
- il bloque la fermeture si une operation moteur est en cours ;
- il desarme les modes de clic avec `Echap` ;
- il journalise les erreurs.

Il reste toutefois une application liee a du materiel reel. Les mouvements doivent etre surveilles et les parametres doivent etre choisis avec prudence.

## Ce qu'il faut retenir

LaserBench sert a faire une mesure spatialisee en reliant trois mondes :

- l'image camera ;
- les coordonnees moteur ;
- les mesures potentiostat.

L'utilisateur travaille principalement dans l'image, puis l'application transforme ses actions en mouvements et en acquisitions.

Les fonctionnalites les plus importantes sont :

- le live camera ;
- le calibrage du spot laser ;
- le GoTo par clic image ;
- la selection de zone ;
- le choix du mode de balayage ;
- la mesure simple ou cartographie ;
- la visualisation temps reel ;
- l'export des resultats.

Pour developper le projet, il faut considerer `LaserBenchC++` comme la reference principale.

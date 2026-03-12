# interface_final

Nouvelle interface integree camera + moteurs + potentiostat.

Entree principale:
- `interface_final/final_ui.py`

Objectif de cette premiere base:
- onglet `Parametrage`: potentiostat a gauche, camera + moteurs a droite
- onglet `Mesure`: graphe direct + carte spatiale
- reutilisation des fonctions existantes de `MainUI/main_ui.py`
- reutilisation du SDK BioLogic via `Potentiostat/Examples/Python/kbio`

Lancement:

```powershell
python interface_final/final_ui.py
```

Etat actuel:
- base d integration fonctionnelle posee
- mesure spatiale basee sur la zone moteur deja definie dans l interface
- simplification volontaire des parametres CA par rapport a l UI de test potentiostat

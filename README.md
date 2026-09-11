# Royale 35+E II

Fan-game original inspiré du gameplay de Clash Royale, conçu pour **Casio Graph 35+E II** en add-in `.g1a` avec **fxSDK + gint**.

> Ce projet ne contient aucun asset, logo, nom de carte ou code officiel de Supercell. Les graphismes sont dessinés en primitives monochromes pour rester légers et adaptés à la calculatrice.

## Fonctionnalités

- combat temps réel 1v1 sur deux voies ;
- 3 tours par camp (2 tours latérales + tour du roi) ;
- élixir de 0 à 10 avec régénération ;
- 4 cartes : chevalier, archère, géant, boule de feu ;
- unités de mêlée, distance et tank ;
- IA adverse ;
- attaques automatiques des tours ;
- timer de 3 minutes ;
- victoire par destruction de la tour du roi ou aux points de vie restants.

## Contrôles

- `F1` : Chevalier (3 élixir)
- `F2` : Archère (3 élixir)
- `F3` : Géant (5 élixir)
- `F4` : Boule de feu (4 élixir)
- `↑` / `↓` : choisir voie haute / basse
- `EXE` : jouer la carte
- `EXIT` : quitter

## Compilation

Il faut une installation fonctionnelle de fxSDK/gint pour cible fx-9860G / Graph 35+E II.

```sh
cd royale35e
fxsdk build-fx
```

Le fichier produit sera normalement :

```text
build-fx/ROYALE35.g1a
```

Copie ensuite le `.g1a` à la racine de la mémoire de stockage USB de la Graph 35+E II.

## Dépendances

Le projet n'installe aucune dépendance tout seul et ne modifie rien hors du projet. La toolchain fxSDK/gint doit être déjà disponible sur la machine de compilation.

## Compilation sans WSL : GitHub Actions

Le projet contient `.github/workflows/build-g1a.yml`.
Après avoir envoye le contenu du dossier sur un depot GitHub, ouvre l'onglet **Actions**,
choisis **Compiler le G1A**, puis telecharge l'artefact **ROYALE35-g1a**.
Il contient `ROYALE35.g1a`.

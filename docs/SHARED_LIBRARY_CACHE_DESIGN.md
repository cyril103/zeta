# Cache partagé de bibliothèques

## Objectif

Une paire `module.zti` + `module.o` construite avec `--build-library` doit pouvoir
être installée une fois puis importée par plusieurs projets, sans recopier ses
artefacts à côté de chaque source consommateur.

## Commandes et sélection du cache

L'installation accepte le chemin de l'interface ; l'objet doit porter le même nom
dans le même dossier :

```text
zeta --install-library dist/module.zti [--library-cache dossier] [--force]
```

`--library-cache dossier` sélectionne également le cache consulté pendant une
compilation ordinaire ou `--build-library`. Sans cette option, sa racine est
choisie dans cet ordre :

1. `ZETA_LIBRARY_CACHE` ;
2. `$XDG_CACHE_HOME/zeta/libraries` ;
3. `$HOME/.cache/zeta/libraries`.

Les artefacts sont isolés par ABI dans `dossier/abi-<version>/`. Cette racine ne
contient pas de sources et n'est pas le cache incrémental d'un exécutable.

## Installation

Avant publication, le compilateur vérifie :

- le format et le contenu structurel de l'interface ;
- l'égalité entre le module déclaré et le nom du fichier ;
- la présence d'un objet ELF64 little-endian relogeable x86-64 ;
- la présence, dans le même cache ABI, de chaque dépendance déclarée ;
- la validité de chaque paire de dépendance.

Une réinstallation avec la même empreinte d'interface est idempotente. Une
empreinte différente est refusée afin d'éviter un remplacement implicite d'ABI ;
`--force` rend ce remplacement explicite. Les deux fichiers sont copiés vers des
noms temporaires, validés, puis publiés ensemble. Les temporaires sont supprimés
en cas d'erreur.

## Résolution

Pour un `import module`, le chargeur cherche dans cet ordre :

1. `module.zeta` à côté du fichier racine ;
2. `module.zti` et `module.o` à côté du fichier racine ;
3. la paire du cache partagé pour l'ABI courante ;
4. la bibliothèque standard précompilée, puis ses sources.

La résolution transitive suit les imports enregistrés dans les `.zti`. Une paire
locale garde donc toujours priorité sur une installation partagée.

## Diagnostics et tests

Les erreurs d'interface et d'objet conservent les familles `ZTI` et `ABI`. Les
erreurs d'installation utilisent `LIB001` pour un contrat de paire invalide,
`LIB002` pour une dépendance absente ou invalide et `LIB003` pour un conflit
d'empreinte nécessitant `--force`.

La matrice couvre l'installation, la consommation sans source local, la priorité
locale, les dépendances transitives, l'idempotence, le remplacement forcé, les
artefacts corrompus et le nettoyage des fichiers temporaires.

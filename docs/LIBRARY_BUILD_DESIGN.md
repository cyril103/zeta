# Construction explicite d'une bibliothèque

## Objectif

Le compilateur doit pouvoir transformer un module source en une paire d'artefacts
précompilés consommable par un autre programme, sans exiger de fonction `main` et
sans construire d'exécutable intermédiaire.

## Commande

```text
zeta --build-library module.zeta -o dossier
```

`-o` désigne obligatoirement un dossier. La commande y produit exactement les
deux artefacts publics portant le nom du module racine :

```text
dossier/module.zti
dossier/module.o
```

Le fichier source doit être un module `.zeta`. La commande refuse une sortie
absente, un fichier de sortie à la place d'un dossier, et la combinaison avec
`--build-stdlib`.

## Sémantique

- Le module racine n'a pas besoin de déclarer `main`.
- Ses déclarations publiques, types publics et corps génériques nécessaires sont
  écrits dans l'interface `.zti` selon le format courant.
- Son code non générique et son initialiseur de module sont compilés dans un objet
  ELF64 repositionnable `.o`.
- Les éventuels symboles natifs du module sont fusionnés dans le même `.o` avec
  une liaison repositionnable (`ld -r`).

Les imports servent à analyser et compiler le module, mais leurs artefacts ne
sont pas recopiés dans le dossier de sortie. L'interface garde leurs empreintes
et dépendances. Le consommateur doit donc rendre les paires `.zti`/`.o`
correspondantes accessibles, comme pour toute dépendance précompilée.

## Isolation des artefacts

La commande ne produit ni point d'entrée, ni exécutable, ni répertoire de cache,
ni répertoire `.modules`. Les fichiers assembleur et objets intermédiaires sont
temporaires et supprimés après succès comme après erreur. Une paire déjà présente
n'est remplacée qu'après la production réussie des deux nouveaux artefacts.

## Validation attendue

Les tests couvrent :

1. une bibliothèque sans `main` consommée sans son source ;
2. la conservation et l'instanciation d'une fonction générique publique ;
3. une bibliothèque qui dépend d'une autre paire précompilée ;
4. la fusion d'une implémentation native dans l'objet publié ;
5. les erreurs de contrat CLI et l'absence d'artefacts de construction annexes.

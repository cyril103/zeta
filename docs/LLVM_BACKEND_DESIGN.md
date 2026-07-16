# Backend LLVM/Clang expérimental

Ce document décrit l'introduction progressive d'un backend LLVM pour Zeta. Le
backend FASM reste la référence de correction ; le backend LLVM/Clang est ajouté
comme cible expérimentale, derrière des options explicites, afin de gagner en
optimisations, diagnostics d'assemblage/linkage et portabilité sans réécrire le
front-end.

## Objectifs

- Émettre du LLVM IR textuel (`.ll`) depuis l'IR Zeta déjà vérifiée.
- Utiliser `clang` comme driver pour optimiser, assembler et lier le programme.
- Garder `fasm` comme backend par défaut et oracle de comparaison.
- Faire progresser le support par sous-ensembles testables.
- Ne pas exposer de nouvelle ABI publique tant que le mapping runtime n'est pas
  stabilisé.

## Non-objectifs de la première tranche

- Pas de remplacement du parser, de l'analyse sémantique ou de l'IR Zeta.
- Pas de support immédiat pour `String`, `Box`, `Vec`, slices, modules séparés ou
  stdlib précompilée via LLVM.
- Pas de dépendance à l'API C++ LLVM dans le compilateur : la première tranche
  produit du texte `.ll` déterministe.
- Pas de sélection automatique du backend selon la présence de `clang`.

## Interface CLI cible

Le backend reste explicite :

```text
zeta source.zeta -o programme                  # backend FASM par défaut
zeta source.zeta --backend=fasm -o programme   # équivalent explicite
zeta source.zeta --emit-llvm -o programme      # écrit programme.ll seulement
zeta source.zeta --backend=clang -o programme  # écrit .ll puis appelle clang
```

Contraintes initiales :

- `--emit-llvm` implique le backend LLVM mais ne lance pas `clang`.
- `--backend=clang` échoue avec un diagnostic clair si `clang` est absent.
- `--backend=fasm --emit-llvm` est rejeté comme combinaison incohérente.
- `--build-library`, `--install-library` et `--build-stdlib` restent FASM-only au
  début.

## Pipeline

```text
source Zeta
  -> lexer/parser
  -> analyse sémantique
  -> IR Zeta
  -> vérificateur IR
  -> LlvmIrCodeGenerator
  -> fichier .ll
  -> clang -x ir <fichier.ll> -o <exécutable>   # seulement --backend=clang
```

La génération LLVM intervient donc après la même frontière que le backend FASM.
Toute divergence doit être visible dans les tests de comparaison FASM/Clang.

## Sous-ensemble initial `--emit-llvm`

La première tranche supporte uniquement un noyau sans runtime :

- `Int` (`i32`) et `Bool` (`i1` pour les calculs, extension vers `i32` au besoin)
- constantes entières et booléennes ;
- arithmétique et comparaisons entières ;
- fonctions Zeta non génériques avec paramètres scalaires ;
- appels directs ;
- blocs, labels, branches conditionnelles et sauts ;
- `return` et sortie de `main` par code de retour.

Les tests doivent commencer par un programme minimal qui retourne un entier
constant, puis élargir à arithmétique, `if`, `while` et appel de fonction.

## Mapping de types initial

- `Int` -> `i32`
- `Byte` -> `i8` (tranche suivante)
- `Bool` -> `i1` en SSA, normalisé depuis/vers `i32` lorsque l'IR Zeta attend une
  valeur stockable homogène
- `Char` -> `i32` (tranche suivante)
- `Double` -> `double` (tranche suivante)
- `Unit` -> valeur fantôme côté IR Zeta ; pas d'allocation LLVM quand elle n'est
  pas observable
- `Never` -> terminaison de bloc, pas de valeur

Les agrégats (`struct`, enum, array), références, slices et propriétaires
runtime (`String`, `Box`, `Vec`) sont explicitement hors du sous-ensemble initial.

## Convention de noms et linkage

- Les fonctions utilisateur continuent d'être abaissées avec les noms de liaison
  déjà produits par l'IR Zeta (`zeta_fn_*` lorsque présent).
- Un wrapper LLVM `@main() -> i32` appelle le point d'entrée Zeta lorsque l'IR
  exécutable contient déjà l'appel/exit central.
- Tant que les modules séparés ne sont pas supportés, `--emit-llvm` et
  `--backend=clang` sont limités à un executable monolithique sans dépendances
  précompilées autres que les modules source que l'IR global sait déjà intégrer.

## Stratégie runtime

La première tranche évite le runtime natif. Ensuite :

1. définir les signatures LLVM des primitives runtime déjà écrites en assembleur ;
2. fournir une version C ou LLVM IR des primitives nécessaires ;
3. faire lier ces objets par `clang` ;
4. valider `String`, `StringView`, `Box`, `retain`, `drop` et destruction
   déterministe par comparaison avec FASM.

Aucune sémantique de propriété ne doit être déplacée dans LLVM : l'IR Zeta reste
responsable des `retain`/`drop` et le backend ne fait qu'émettre les appels.

## Organisation du code

Tranche recommandée :

1. extraire une petite interface de backend ou, au minimum, ajouter
   `LlvmIrCodeGenerator` en parallèle de `FasmCodeGenerator` ;
2. ajouter les options CLI sans changer le comportement FASM par défaut ;
3. implémenter `--emit-llvm` pour un programme `main(): Int = 42` ;
4. ajouter `--backend=clang` uniquement après validation que le `.ll` minimal est
   accepté par `clang` ;
5. élargir le générateur instruction par instruction.

## Matrice de tests

Chaque tranche LLVM doit inclure :

- un test `emit_llvm_*` qui vérifie la création du `.ll` et quelques marqueurs
  textuels stables (`target triple`, `define i32 @main`, `ret i32 ...`) ;
- un test `compile_clang_backend_*` lorsque `--backend=clang` est livré ;
- un test d'exécution FASM existant ou nouveau avec le même code source ;
- un test d'exécution Clang qui doit retourner le même code ou produire la même
  sortie ;
- un test négatif CLI pour les combinaisons d'options incohérentes.

La suite complète doit continuer à passer avec FASM par défaut.

## Diagnostics

Les erreurs doivent être explicites :

- backend inconnu : `backend inconnu '...'` ;
- `clang` introuvable : `clang introuvable pour --backend=clang` ;
- instruction IR non couverte : `backend LLVM: instruction non supportée ...` ;
- type non couvert : `backend LLVM: type non supporté ...`.

Ces diagnostics sont préférables à une génération partielle de `.ll` invalide.

## Critères de sortie de la première étape

- `docs/LLVM_BACKEND_DESIGN.md` existe et définit le périmètre.
- `--emit-llvm` produit un `.ll` minimal vérifiable pour `main(): Int = 42`.
- Le backend FASM par défaut reste inchangé.
- La roadmap pointe vers l'élargissement arithmétique/appels/branches après le
  smoke test LLVM minimal.

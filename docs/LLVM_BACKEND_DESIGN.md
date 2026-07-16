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

1. fait : ajouter `LlvmIrCodeGenerator` en parallèle de `FasmCodeGenerator` ;
2. fait : ajouter les options CLI sans changer le comportement FASM par défaut ;
3. fait : implémenter `--emit-llvm` pour un programme `main(): Int = 42` ;
4. fait : élargir le générateur aux opérations scalaires `Int`/`Bool`
   (`IrBinary`, comparaisons et copies scalaires) ;
5. fait : ajouter les labels/branches et les slots scalaires nécessaires aux
   programmes de contrôle simples (`if`/`while`) ;
6. fait : ajouter les appels avec paramètres scalaires ;
7. fait : ajouter `--backend=clang` pour le sous-ensemble validé.

État actuel : `emit_llvm_minimal` vérifie la génération de `target triple`,
`define i32 @main()`, `ret i32 42`, puis compile le `.ll` avec `clang` et exécute
le binaire obtenu. `emit_llvm_scalars` ajoute un programme sans contrôle de flux
qui couvre `IrBinary` arithmétique (`add nsw`, `sub nsw`, `mul nsw`, `sdiv`), les
comparaisons (`icmp`), les opérations booléennes simples et `IrCopy` scalaire.

La génération LLVM accepte donc le squelette exécutable, `IrCall` sans
paramètre, `IrConst(Int/Bool)`, `IrExit`, `IrFunctionStart`, `IrReturn(Int/Bool)`,
`IrBinary(Int/Bool)` et `IrCopy(Int/Bool/Unit)`. Toute autre instruction échoue
explicitement avec `backend LLVM: instruction non supportée ...` ou un diagnostic
de type non supporté.

`emit_llvm_control` couvre maintenant un programme combinant `if` et `while`.
Cette tranche ajoute `IrLabel`, `IrJump`, `IrBranch`, `IrLoad` et `IrStore` pour
les slots scalaires `Int`/`Bool`. Les labels Zeta sont rendus comme blocs LLVM,
les branches conditionnelles introduisent un bloc de continuation synthétique
pour représenter le fallthrough, et les slots scalaires sont matérialisés par des
`alloca` en entrée de fonction. Les types de slot non scalaires restent hors
périmètre et doivent continuer à produire un diagnostic explicite.

`emit_llvm_parameters` couvre les signatures et appels paramétrés pour `Int` et
`Bool`. Le générateur pré-scanne les `IrParameter` d'une fonction pour émettre la
signature LLVM complète (`define i32 @fn(i32 %arg0, ...)`) avant le corps, puis
lie chaque instruction `IrParameter` à `%argN` sans allocation supplémentaire.
Les appels utilisent les types d'arguments déjà présents dans `IrCall` et restent
limités aux types scalaires couverts par `llvmType`.

`compile_clang_backend_parameters` active le premier chemin `--backend=clang` : le
compilateur écrit le `.ir` vérifié et le `.ll`, invoque `clang -x ir -o <sortie>`,
puis exécute le binaire attendu. `run_fasm_backend_still_default` compile le même
programme sans option backend pour vérifier que FASM reste le défaut et que le
chemin Clang ne crée pas d'artefact `.asm`.

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

- fait : `docs/LLVM_BACKEND_DESIGN.md` existe et définit le périmètre.
- fait : `--emit-llvm` produit un `.ll` minimal vérifiable pour
  `main(): Int = 42`.
- fait : `--emit-llvm` couvre les opérations scalaires `Int`/`Bool` sans contrôle
  de flux et les valide par compilation/exécution `clang`.
- fait : `--emit-llvm` couvre `if`/`while` via labels/branches et slots scalaires,
  validés par compilation/exécution `clang`.
- fait : `--emit-llvm` couvre les appels avec paramètres scalaires, validés par
  compilation/exécution `clang`.
- fait : `--backend=clang` produit un exécutable via `clang -x ir` pour le
  sous-ensemble validé.
- fait : le backend FASM par défaut reste inchangé et testé explicitement.

# Roadmap de Zeta — reprise de session

Ce document est le point d'entrée de la prochaine session. Il remplace l'ancien
journal cumulatif par un état vérifié, les invariants à préserver et un ordre de
travail directement exécutable. Le détail historique des fonctionnalités livrées
reste dans le `README.md`, les documents de `docs/` et l'historique Git.

## Reprise rapide

État vérifié le 14 juillet 2026 sur la branche `master` :

- construction CMake réussie ;
- stdlib locale régénérée ;
- 382 tests CTest réussis sur 382 ;
- aucun changement suivi en attente après les commits de la session ;
- `build/`, `stdlib/precompiled/` et certains artefacts de tests sont ignorés.

Commandes de reprise :

```sh
git status --short
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/zeta --build-stdlib
```

Après une modification du compilateur, reconstruire `zeta` avant de régénérer la
stdlib. Après une modification de la génération de code ou du format de cache,
incrémenter la version concernée dans `src/version.hpp` afin de ne pas réutiliser
un ancien objet valide en apparence.

## Versions et formats actuels

| Élément | Version |
| --- | ---: |
| Compilateur | `0.1.0` |
| ABI | `4` |
| Interface `.zti` | `7` |
| Tokens génériques | `1` |
| Cache de modules | `12` |
| Cache de démarrage | `2` |
| Manifeste de stdlib | `1` |

Une modification de représentation mémoire ou de convention d'appel exige une
révision ABI. Une modification de sérialisation exige une révision `.zti`. Une
modification susceptible de changer un objet généré exige au minimum une révision
du cache de modules.

## Vision et pipeline

Zeta est un langage statiquement typé produisant des exécutables Linux x86-64
autonomes. Il privilégie la monomorphisation, une propriété déterministe, des
emprunts explicites et une compilation séparée distribuable sans sources.

```text
source -> lexer -> parser -> AST -> analyse sémantique -> AST typé
       -> IR par module -> assembleur FASM -> objets ELF64 -> ld -> exécutable
```

Le compilateur est organisé ainsi :

- `lexer.*`, `parser.*`, `ast.*` : syntaxe et représentation du programme ;
- `semantic.*`, `type_rules.hpp`, `symbol_table.hpp` : typage, propriété et
  emprunts ;
- `module.*`, `interface.*`, `generic_tokens.*` : graphe de modules, `.zti` et
  génériques distribués ;
- `ir.*` : abaissement, instances génériques et IR textuelle ;
- `codegen.*` : émission FASM x86-64 ;
- `main.cpp` : CLI, construction, cache, assemblage et édition de liens ;
- `version.hpp` : contrats de compatibilité à mettre à jour explicitement.

## Socle terminé

### Langage

- `val`, `var`, `def`, fonctions globales, récursion et appels terminaux ;
- blocs expressions, `if/else`, `while ... do`, `break`, `continue`, `return` ;
- `Int`, `Byte`, `Double`, `Bool`, `Char`, `String` et conversions explicites ;
- tableaux fixes et imbriqués avec vérification de bornes ;
- références `&T` et `&mut T`, `Slice[T]` et `SliceMut[T]` ;
- `Box[T]` et `Vec[T]` possédés avec déplacement et destruction déterministe ;
- structures et énumérations ordinaires ou génériques ;
- `match` exhaustif et type builtin partagé `Option[T]` ;
- fonctions génériques monomorphisées, inférence et contraintes `Copy`,
  `Numeric`, `Ordered`, `Equatable` ;
- variables locales utilisant un paramètre de type et substitution correcte de
  leur type pendant la génération IR.

### Chaînes

- UTF-8 validé, échappements Unicode et égalité exacte ;
- représentation possédée `{données, longueur}` avec comptage de références ;
- concaténation et conversions depuis les types primitifs ;
- propriétés `lengthBytes` et `isEmpty` ;
- décodage contrôlé, parcours par offset d'octet et `StringView` empruntée ;
- recherche de sous-vues dans le module `strings`.

### Modules et distribution

- imports récursifs, visibilité `pub`, noms qualifiés et cycles diagnostiqués ;
- IR et objet ELF64 séparés par module ;
- cache incrémental dépendant des sources et interfaces ;
- interfaces `.zti` consommables avec un `.o` sans source ;
- types publics ordinaires et génériques avec disposition ABI sérialisée ;
- génériques publics stockés sous forme de tokens canoniques réduits à leur
  fermeture sémantique ;
- identité canonique et déduplication `link-once` des instances génériques ;
- diagnostics `ZTI`, `MOD`, `ABI`, `GEN` et `LIB` contextualisés ;
- `--build-stdlib`, `--build-library` et `--install-library` ;
- cache partagé de bibliothèques isolé par ABI et installation atomique.

Les contrats détaillés sont dans :

- `docs/PUBLIC_TYPES_DESIGN.md` et `docs/PUBLIC_ENUMS_DESIGN.md` ;
- `docs/GENERIC_INTERFACE_DESIGN.md` ;
- `docs/GENERIC_INSTANCE_DEDUP_DESIGN.md` ;
- `docs/INTERFACE_DIAGNOSTICS.md` ;
- `docs/LIBRARY_BUILD_DESIGN.md` et `docs/SHARED_LIBRARY_CACHE_DESIGN.md`.

## Bibliothèque standard actuelle

La stdlib publique contient exactement quatre modules source :

- `io` : affichage des types primitifs, `String` et `Slice[Byte]` ;
- `collections` : accès sûrs à une slice et helpers `Option[T]` ;
- `strings` : décodage UTF-8, vues et recherche ;
- `sequences` : algorithmes génériques sur `Slice[T]` et `SliceMut[T]`.

`Vec[T]` est builtin et s'utilise sans import. L'ancien module artificiel
`vectors` a été retiré ; sa frontière générique est désormais une fixture privée
dans `tests/fixtures/stdlib/`.

API actuelle de `sequences` :

- recherche : `contains`, `indexOf`, `lastIndexOf`, `binarySearch`, `lowerBound`,
  `upperBound` ;
- comparaison : `equals`, `startsWith`, `endsWith`, `allEqual` ;
- agrégation : `count`, `minimum`, `maximum`, `sum`, `product` ;
- mutation : `reverse`, `fill`, `swap`, `sort` ;
- validation : `isSorted`.

Les fonctions prennent des slices pour ne pas déplacer le `Vec` et pour rester
utilisables avec les tableaux. `sum` et `product` retournent `Option[T]` sur une
séquence potentiellement vide. Le tri actuel est un tri par insertion stable en
`O(n²)`, adapté comme première implémentation et non comme objectif final de
performance.

La distribution sans sources de toutes ces familles est couverte par
`tests/precompiled_stdlib.sh`.

## Invariants à préserver

- une valeur possédée non `Copy` ne doit jamais être dupliquée silencieusement ;
- une `SliceMut[T]` reste exclusive et non copiable ;
- aucun accès indexé ne contourne les contrôles de bornes ;
- une interface publique ne peut exposer un type privé ;
- un module précompilé n'est accepté que si manifeste, empreinte, formats et objet
  ELF sont cohérents ;
- deux instances génériques identiques se dédupliquent, deux identités différentes
  restent distinctes ;
- `Option[T]` conserve une identité builtin unique entre sources et interfaces ;
- `--build-stdlib` doit lire les sources même si des `.zti` sont déjà présents ;
- tout changement de code généré invalide les objets mis en cache.

## Limites et dettes connues

### Langage et ergonomie

- une seule contrainte est exprimable par paramètre générique ;
- pas encore de traits définis par l'utilisateur ;
- pas de fonctions de première classe, lambdas, `map`, `filter` ou `fold` ;
- pas de type `Unit`, plusieurs fonctions mutantes retournent donc un compteur ou
  un booléen utile ;
- pas de boucle `for` ni de protocole d'itération ;
- les méthodes utilisateur et receveurs mutables ne sont pas disponibles ;
- la mutation d'un `Vec[T]` stocké dans un champ reste peu composable ;
- références, slices et `StringView` ne peuvent pas être retournées ni stockées
  globalement ;
- les durées de vie sont lexicales, réduites à la dernière utilisation connue,
  sans analyse interprocédurale générale ;
- les fonctions locales capturantes ne sont pas de véritables fermetures.

### Backend et performances

- aucune vérification structurelle autonome de l'IR avant le backend ;
- pas de propagation de constantes ni élimination de code mort ;
- nombreuses valeurs temporaires matérialisées sur la pile ;
- copies d'agrégats encore conservatrices ;
- cible unique Linux x86-64, FASM et `ld` ;
- pas d'informations de débogage source.

### Outillage et stdlib

- `--build-stdlib` réécrit le manifeste courant mais ne supprime pas les anciens
  `.o`/`.zti` qui ne correspondent plus à un module source ; le manifeste empêche
  leur chargement, mais un nettoyage manuel peut être nécessaire ;
- pas d'entrée standard, fichiers, réseau, dictionnaire ni ensemble ;
- pas de gestionnaire de paquets ;
- les artefacts de `stdlib/precompiled/` sont locaux et ignorés par Git.

## Prochaine priorité — vérificateur structurel d'IR

La prochaine session doit commencer ici. Les corrections récentes sur les locaux
génériques ont montré qu'une IR textuellement plausible pouvait conserver un type
symbolique et produire une corruption de pile. Un vérificateur entre abaissement
et codegen doit transformer ce genre d'erreur en diagnostic interne déterministe.

### Plan committable

1. Écrire `docs/IR_VERIFIER_DESIGN.md` : modèle des fonctions, valeurs, slots,
   labels, terminaisons et catégories d'erreurs.
2. Introduire un composant `IrVerifier` sans modifier le codegen ; vérifier les
   identifiants de valeurs, slots et labels ainsi que leurs bornes.
3. Vérifier qu'une valeur est définie avant usage dans sa fonction et une seule
   fois, avec traitement explicite des paramètres.
4. Vérifier les types de chaque instruction : charges, stores, indexation,
   conversions, appels, retours, slices, structures, enums, `Box` et `Vec`.
5. Vérifier les frontières de fonctions, branches, labels uniques et chemins
   terminés par retour ou appel terminal.
6. Appeler le vérificateur avant toute écriture d'IR/codegen et produire des
   diagnostics internes stables préfixés `IRV`.
7. Ajouter des tests unitaires construisant volontairement des `IrProgram`
   invalides, plus un test d'intégration confirmant une IR valide par module.
8. Exécuter la suite complète, mesurer le coût et documenter le contrat dans le
   README. Incrémenter le cache de modules si l'intégration change les objets.

### Critère de sortie

Chaque instruction envoyée au backend appartient à une fonction valide, ne
référence que des valeurs et slots définis, respecte les types attendus et mène à
une terminaison cohérente. Les erreurs du compilateur sont interceptées avant
l'émission assembleur avec un diagnostic `IRV` reproductible.

## Priorités suivantes

### 2. Optimisations IR mesurées

À entreprendre seulement après le vérificateur :

1. propagation et pliage des constantes ;
2. simplification algébrique ;
3. suppression des valeurs, branches et fonctions mortes ;
4. réduction ou réutilisation des temporaires de pile ;
5. élimination des copies d'agrégats inutiles ;
6. inlining contrôlé et maintien des appels terminaux ;
7. mesures reproductibles de taille, temps de compilation et temps d'exécution.

Chaque passe doit préserver une IR vérifiable avant et après transformation.

### 3. Composition des types possédés

Objectif : permettre d'écrire une vraie collection en Zeta au-dessus de `Vec[T]`
sans ajout builtin.

1. mutation sûre d'un champ `Vec[T]` ;
2. passage et utilisation de `&mut Vec[T]` ;
3. méthodes utilisateur avec receveur partagé ou mutable ;
4. combinaisons de contraintes génériques ;
5. implémentation d'une `Queue[T]` de stdlib comme test de sortie.

### 4. Itération et fonctions de première classe

1. protocole d'itération et boucle `for` ;
2. fonctions comme valeurs sans capture ;
3. fermetures avec environnement explicite ;
4. `map`, `filter`, `fold`, prédicats et comparateurs dans `sequences` ;
5. règles de propriété pour arguments et résultats des callbacks.

### 5. Runtime et écosystème

- erreurs structurées et propagation ;
- entrée standard et fichiers ;
- dictionnaires et ensembles ;
- informations de débogage ;
- autres architectures et formats objets ;
- gestionnaire de paquets.

## Discipline de livraison

Chaque étape doit :

- former un commit cohérent et réversible ;
- ajouter des tests positifs, d'exécution et de rejet quand ils sont pertinents ;
- couvrir les frontières `.zti`/`.o` lorsqu'une API générique publique change ;
- préserver le fallback source et l'invalidation des caches ;
- exécuter `git diff --check`, la construction CMake et les tests ciblés ;
- terminer par `ctest --test-dir build --output-on-failure` ;
- régénérer la stdlib avec `build/zeta --build-stdlib` si ses sources changent ;
- mettre à jour le README, la roadmap et le document de conception concerné.

Ne jamais committer `build/` ni `stdlib/precompiled/`. Ne pas supprimer les
changements locaux d'un utilisateur pour nettoyer le dépôt.

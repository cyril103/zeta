# Roadmap de Zeta — prochaine session

Ce document est le point de reprise opérationnel du projet. Il intègre les
enseignements tirés de l'écriture de `examples/stdlib_showcase.zeta`, qui utilise
ensemble toute la bibliothèque standard publique.

L'objectif n'est plus seulement d'ajouter des fonctionnalités : Zeta possède déjà
un socle conséquent. Les prochains chantiers doivent surtout renforcer le
compilateur, réduire la cérémonie du code utilisateur et permettre de composer
les primitives existantes sans ajouter un builtin pour chaque nouveau type.

## Snapshot vérifié

État validé le 14 juillet 2026 sur la branche `master` :

- construction CMake réussie ;
- stdlib locale régénérée ;
- 385 tests CTest réussis sur 385 ;
- exemple complet compilé, exécuté et couvert par CTest ;
- aucun changement suivi en attente à la fin de la session ;
- `build/`, `stdlib/precompiled/` et certains artefacts de tests sont ignorés.

Commandes de reprise :

```sh
git status --short
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
build/zeta --build-stdlib
build/zeta examples/stdlib_showcase.zeta -o build/stdlib-showcase
build/stdlib-showcase
```

Le programme de référence utilisateur est
`examples/stdlib_showcase.zeta`. Toute évolution ergonomique importante devrait
chercher à le simplifier sans réduire sa sûreté.

## Versions actuelles

| Contrat | Version |
| --- | ---: |
| Compilateur | `0.1.0` |
| ABI | `4` |
| Interface `.zti` | `7` |
| Tokens génériques | `1` |
| Cache de modules | `14` |
| Cache de démarrage | `2` |
| Manifeste de stdlib | `1` |

Règles de versionnement :

- représentation mémoire ou convention d'appel : réviser l'ABI ;
- sérialisation publique : réviser le format `.zti` ;
- représentation des génériques : réviser les tokens génériques et `.zti` ;
- modification pouvant changer un objet généré : réviser le cache de modules ;
- modification du démarrage : réviser le cache de démarrage.

Un changement de codegen sans révision du cache peut réutiliser un objet ancien et
masquer une correction. Ce cas a déjà été rencontré avec les locaux génériques.

## Vision

Zeta est un langage statiquement typé produisant des exécutables Linux x86-64
autonomes. Il privilégie :

- la propriété déterministe et l'absence de copies implicites dangereuses ;
- les emprunts explicites ;
- la monomorphisation des génériques ;
- des interfaces binaires distribuables sans sources ;
- une stdlib majoritairement écrite en Zeta ;
- des diagnostics précoces plutôt que des comportements indéfinis.

Pipeline actuel :

```text
source -> lexer -> parser -> AST -> analyse sémantique -> AST typé
       -> IR par module -> assembleur FASM -> objets ELF64 -> ld -> exécutable
```

## Évaluation issue de l'exemple complet

### Points forts à préserver

La chaîne `Vec[T] -> Slice[T]/SliceMut[T] -> sequences` est la meilleure base
architecturale actuelle. Elle sépare correctement propriété, lecture, mutation et
algorithmes, tout en réutilisant les mêmes fonctions pour les tableaux.

Les autres acquis particulièrement utiles sont :

- `Option[T]` pour les accès sûrs ;
- les contraintes `Copy`, `Numeric`, `Ordered` et `Equatable` ;
- les imports et noms qualifiés explicites ;
- les conversions `String(value)` et la concaténation ;
- les règles de déplacement et d'emprunt déjà actives sur `Box`, `Vec` et slices ;
- les modules `.zti` + `.o` consommables sans source ;
- une stdlib générique réellement écrite dans le langage ;
- des diagnostics avec positions et familles de codes stables.

### Frictions observées

L'exemple est compréhensible, mais trop cérémonieux :

- chaque variable locale exige une annotation de type ;
- l'absence de `Unit` force les mutations à retourner un compteur ou un booléen ;
- `sequences.sort(values.asSliceMut())` expose trop de mécanique pour un usage
  courant ;
- `collections.unwrapOr` et `strings.unwrapOr` dupliquent une API qui appartient
  conceptuellement à `Option[T]` ;
- `if` est toujours une expression, ce qui rend certaines gardes suivies de
  déclarations locales maladroites ;
- tous les parcours utilisent une boucle `while` et un indice manuel ;
- les offsets UTF-8 en octets sont corrects mais trop bas niveau pour l'usage
  quotidien ;
- un `Vec[T]` placé dans une structure ne permet pas encore de construire
  naturellement une nouvelle collection ;
- une seule contrainte générique peut être exprimée par paramètre ;
- l'absence de fonctions de première classe bloque `map`, `filter`, `fold` et les
  comparateurs personnalisés.

### Positionnement actuel

Zeta est déjà un langage expérimental solide pour la propriété, les emprunts, les
génériques et la compilation séparée. Il n'est pas encore assez ergonomique ni
composable pour une application générale. L'architecture interne est aujourd'hui
plus avancée que la surface utilisateur.

## Socle livré

### Langage

- `val`, `var`, `def`, fonctions globales et récursives, retours et appels
  terminaux ;
- blocs expressions, `if/else`, `while ... do`, `break`, `continue` ;
- types primitifs, conversions explicites et opérations typées ;
- tableaux fixes et imbriqués avec bornes contrôlées ;
- références, emprunts mutables, `Slice[T]` et `SliceMut[T]` ;
- `Box[T]` et `Vec[T]` possédés ;
- structures et enums ordinaires ou génériques ;
- `match` exhaustif et `Option[T]` builtin ;
- fonctions génériques monomorphisées, inférence des appels et contraintes ;
- locaux typés par un paramètre générique correctement substitué dans l'IR.

### Chaînes

- UTF-8 validé et échappements Unicode ;
- `String` possédée avec comptage de références ;
- concaténation, égalité et conversions depuis les types primitifs ;
- propriétés `lengthBytes` et `isEmpty` ;
- décodage contrôlé et `StringView` empruntée ;
- recherche de sous-vues via `strings`.

### Modules et distribution

- imports récursifs, visibilité publique, noms qualifiés et cycles diagnostiqués ;
- objets ELF64 séparés par module et initialisation topologique ;
- cache incrémental ;
- interfaces `.zti` versionnées ;
- types publics avec disposition ABI sérialisée ;
- tokens génériques canoniques réduits à leur fermeture sémantique ;
- déduplication `link-once` des instances génériques ;
- diagnostics `ZTI`, `MOD`, `ABI`, `GEN` et `LIB` ;
- construction et installation de bibliothèques sans sources ;
- stdlib précompilée avec manifeste et fallback source.

### Bibliothèque standard

La stdlib publique contient exactement :

- `io` ;
- `collections` ;
- `strings` ;
- `sequences`.

`Vec[T]` est builtin et ne nécessite aucun import. L'ancien module `vectors` a été
retiré ; sa frontière générique subsiste uniquement comme fixture de test privée.

`sequences` fournit actuellement :

- recherche : `contains`, `indexOf`, `lastIndexOf`, `binarySearch`, `lowerBound`,
  `upperBound` ;
- comparaison : `equals`, `startsWith`, `endsWith`, `allEqual` ;
- agrégation : `count`, `minimum`, `maximum`, `sum`, `product` ;
- mutation : `reverse`, `fill`, `swap`, `sort` ;
- validation : `isSorted`.

Le tri actuel est un tri par insertion stable en `O(n²)`. Il constitue une
implémentation correcte de référence, pas la cible finale de performance.

## Invariants non négociables

- aucune valeur possédée non `Copy` ne doit être dupliquée silencieusement ;
- une `SliceMut[T]` reste exclusive et non copiable ;
- tous les accès indexés restent contrôlés ;
- une interface publique ne peut pas exposer un type privé ;
- une interface précompilée n'est chargée que si manifeste, empreintes, formats et
  objet ELF sont cohérents ;
- deux instances génériques identiques se dédupliquent, deux identités différentes
  restent distinctes ;
- `Option[T]` conserve une identité builtin unique entre parseurs et interfaces ;
- `--build-stdlib` lit toujours les sources pendant la reconstruction ;
- tout changement de code généré invalide les objets mis en cache ;
- une amélioration ergonomique ne doit pas introduire de copie ou d'emprunt
  implicite ambigu.

## Priorité 0 — vérificateur structurel d'IR

La prochaine session doit commencer ici. Une variable locale générique non
substituée a déjà atteint le backend et corrompu la pile. Un vérificateur doit
intercepter ce type d'erreur avant FASM.

### Étapes committables

1. **Livré le 14 juillet 2026** — écrire `docs/IR_VERIFIER_DESIGN.md`, inventorier
   les 47 variantes actuelles et définir les diagnostics `IRV`.
2. **Livré le 14 juillet 2026** — ajouter un composant `IrVerifier` indépendant
   du codegen, avec erreurs `IRV` testables directement.
3. **Livré le 14 juillet 2026** — vérifier les frontières de fonctions, bornes et
   régions des identifiants de valeurs et slots, ainsi que les labels.
4. **Livré le 14 juillet 2026** — vérifier les producteurs uniques, l'usage après
   définition sur tous les chemins et le préambule ABI des paramètres. Cette
   vérification a détecté et corrigé la double production interne de `Vec.push`.
5. **Livré le 14 juillet 2026** — vérifier les types des 47 instructions, slots,
   agrégats, conversions et opérateurs, ainsi que les signatures des appels,
   tail calls et retours internes.
6. **Livré le 14 juillet 2026** — vérifier les conditions et cibles de branches,
   le code après terminal, les terminaux autorisés par région et la possibilité
   pour chaque chemin atteignable de terminer. Cette étape a supprimé le code IR
   mort après les retours anticipés dans les blocs, boucles, `if` et `match`.
7. **Livré le 14 juillet 2026** — intégrer le vérificateur au pipeline principal
   et aux frontières publiques `IrGenerator::print`,
   `FasmCodeGenerator::generate` et `generateObject`. Le mode exécutable ou objet
   est explicite et une IR injectée directement ne peut plus atteindre le printer
   ou le codegen sans validation.
8. **En cours** — les tests unitaires construisent déjà des `IrProgram`
   volontairement invalides pour `IRV001`, `IRV002`, `IRV003`, `IRV010`,
   `IRV011`, `IRV012`, `IRV013`, `IRV020` à `IRV023`, `IRV030`, `IRV031`,
   `IRV040` à `IRV045` et `IRV050` à `IRV054`; le pipeline CTest exerce désormais
   aussi la vérification intégrée par module et l'injection d'une IR invalide aux
   trois frontières publiques de sortie.
9. Exécuter toute la suite et mesurer le coût de la vérification.

### Critère de sortie

Toute IR envoyée au backend possède des fonctions correctement délimitées, des
valeurs et slots définis, des types cohérents et des chemins terminés. Une erreur
interne produit un diagnostic `IRV` reproductible avant l'assembleur.

## Priorité 1 — ergonomie fondamentale

Objectif : réduire fortement le bruit de `stdlib_showcase.zeta` sans modifier son
comportement ni affaiblir le typage.

### 1A. Inférence des variables locales

Syntaxe cible :

```zeta
val total = sequences.sum(values.asSlice())
var index = 0
```

Étapes :

1. rendre l'annotation locale optionnelle tout en la conservant aux signatures et
   déclarations publiques ;
2. inférer depuis l'initialiseur après résolution générique ;
3. refuser une inférence ambiguë ou sans type concret ;
4. couvrir `Option`, structures, enums, slices et types possédés ;
5. vérifier que l'inférence ne change pas les règles de déplacement.

### 1B. Type `Unit`

Objectif : permettre des fonctions et expressions sans résultat métier :

```zeta
def log(value: String): Unit = io.println(value)
```

Définir représentation ABI, compatibilité des branches, appels utilisés comme
instructions et convention de retour. Les API mutantes pourront ensuite choisir
`Unit` lorsque leur compteur actuel n'est pas contractuel.

### 1C. Contrôle de flux

Clarifier les gardes et branches qui ne reviennent pas :

- soit autoriser un `if` instruction sans `else` ;
- soit introduire un type interne `Never` pour `return`, `break` et `continue` ;
- conserver `if/else` comme expression typée lorsque sa valeur est utilisée.

### Critère de sortie

Réécrire l'exemple complet avec moins d'annotations et sans valeurs factices,
tout en conservant les mêmes sorties et toute la suite de tests existante.

## Priorité 2 — composabilité de `Vec` et méthodes

Objectif : permettre d'écrire une collection possédée entièrement en Zeta.

1. autoriser la mutation sûre d'un `Vec[T]` stocké dans un champ ;
2. permettre le passage et l'utilisation de `&mut Vec[T]` ;
3. définir des méthodes utilisateur avec receveur partagé ou mutable ;
4. définir éventuellement des méthodes d'extension pour les modules ;
5. rendre possibles des appels ergonomiques comme `values.sort()` tout en
   conservant `SliceMut` comme mécanisme interne ;
6. écrire `Stack[T]` comme validation minimale ;
7. écrire `Queue[T]` comme critère de sortie réel.

Le compilateur ne doit pas recevoir un nouveau builtin `Stack` ou `Queue`. Le but
est précisément de prouver que `Vec` est une brique de fondation suffisante.

## Priorité 3 — généricité composable

### Contraintes multiples

Syntaxe cible indicative :

```zeta
def replaceAll[T: Equatable + Copy](values: SliceMut[T], old: T, next: T): Int
```

Étapes : parser une liste canonique, diagnostiquer doublons et contraintes
incompatibles, sérialiser dans `.zti`, intégrer à l'identité générique et tester la
compilation sans sources.

### API commune d'Option

Éliminer la duplication de `collections.unwrapOr`, `strings.unwrapOr` et
`isNone`. Deux directions sont acceptables :

- module standard `option` ;
- méthodes génériques sur `Option[T]` après livraison des méthodes utilisateur.

Ne pas dupliquer une troisième fois les helpers entre modules.

### Traits utilisateur

À entreprendre après les contraintes multiples. Commencer par des contrats sans
état et monomorphisés ; reporter les objets de traits dynamiques.

## Priorité 4 — itération

Objectif : remplacer les boucles indexées répétitives de la stdlib par une
abstraction sûre.

1. concevoir un protocole d'itération sans allocation ;
2. ajouter `for` sur tableaux, slices et `Vec` ;
3. distinguer itération partagée, mutable et consommatrice ;
4. fournir un parcours de `String` par `Char` masquant les offsets UTF-8 ;
5. migrer progressivement `sequences` vers ce protocole ;
6. vérifier les emprunts jusqu'à la dernière utilisation.

Exemple cible :

```zeta
for (value in values.asSlice()) do {
    io.printlnInt(value)
}

for (character in text.chars()) do {
    io.printChar(character)
}
```

## Priorité 5 — fonctions de première classe

1. fonctions sans capture comme valeurs ;
2. convention d'appel et types de fonctions ;
3. callbacks génériques monomorphisés ;
4. fermetures avec environnement explicite ;
5. règles de propriété des captures ;
6. `map`, `filter`, `fold`, prédicats et comparateurs dans `sequences`.

Ce chantier vient après l'itération : les deux modèles doivent être conçus
ensemble, mais livrés séparément.

## Priorité 6 — texte, erreurs et entrées-sorties

- permettre des vues retournées avec un modèle de durée de vie explicite ;
- ajouter une API Unicode de haut niveau au-dessus des offsets d'octets ;
- introduire une gestion d'erreurs structurée, probablement fondée sur un enum
  générique ;
- ajouter entrée standard et fichiers ;
- remplacer progressivement les codes négatifs et arrêts de processus par des
  résultats typés lorsque la récupération est raisonnable.

## Priorité 7 — performances et backend

À commencer après le vérificateur d'IR, mais sans bloquer les améliorations
d'ergonomie indépendantes :

1. mesures reproductibles de taille et de temps ;
2. propagation et pliage des constantes ;
3. simplification algébrique ;
4. suppression de code mort ;
5. réduction des temporaires de pile ;
6. élimination des copies d'agrégats inutiles ;
7. meilleur tri générique pour les grandes séquences ;
8. informations de débogage source ;
9. autres architectures et formats objets.

Chaque passe doit produire une IR valide avant et après transformation.

## Limites opérationnelles à garder visibles

- `--build-stdlib` ne purge pas les anciens `.o`/`.zti` sans source correspondante ;
  le manifeste empêche leur chargement, mais un nettoyage manuel peut être requis ;
- `stdlib/precompiled/` est local et ignoré par Git ;
- cible unique Linux x86-64 avec FASM et `ld` ;
- pas encore de gestionnaire de paquets ;
- pas de dictionnaire, ensemble, fichiers ou réseau ;
- références, slices et `StringView` ne peuvent pas encore être retournées ou
  stockées globalement ;
- les durées de vie restent lexicales sans analyse interprocédurale générale.

## Discipline de livraison

Chaque étape doit :

- former un commit cohérent et réversible ;
- ajouter des tests de compilation, d'exécution et de rejet pertinents ;
- couvrir `.zti` + `.o` sans sources lorsqu'une API générique publique change ;
- préserver fallback source, propriété, emprunts et invalidation des caches ;
- exécuter `git diff --check`, les tests ciblés puis toute la suite CTest ;
- régénérer la stdlib après toute modification de ses sources ;
- mettre à jour README, roadmap et document de conception associé ;
- ne jamais committer `build/` ou `stdlib/precompiled/` ;
- ne jamais effacer les changements locaux d'un utilisateur pour nettoyer le
  dépôt.

## Première action de la prochaine session

Créer `docs/IR_VERIFIER_DESIGN.md`, inventorier chaque variante de
`IrInstruction`, puis définir pour chacune : valeurs lues, valeur produite, slots,
types attendus, effet de contrôle et diagnostic `IRV` associé. Aucun changement de
codegen ne doit être réalisé avant que ce contrat soit relu et testé sur l'IR
actuelle de `stdlib_showcase.zeta`.

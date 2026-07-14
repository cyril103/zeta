# Roadmap de Zeta — prochaine session

Ce document est le point de reprise opérationnel du projet. Il intègre les
enseignements tirés de l'écriture de `examples/stdlib_showcase.zeta`, qui utilise
ensemble toute la bibliothèque standard publique.

L'objectif n'est plus seulement d'ajouter des fonctionnalités : Zeta possède déjà
un socle conséquent. Les prochains chantiers doivent surtout renforcer le
compilateur, réduire la cérémonie du code utilisateur et permettre de composer
les primitives existantes sans ajouter un builtin pour chaque nouveau type.

## Snapshot vérifié

État validé le 15 juillet 2026 sur la branche `master` :

- construction CMake réussie ;
- stdlib locale régénérée ;
- 419 tests CTest réussis sur 419 ;
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
| ABI | `5` |
| Interface `.zti` | `10` |
| Tokens génériques | `2` |
| Cache de modules | `21` |
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
- les algorithmes mutateurs autres que `sort` exposent encore `SliceMut` pour un
  usage courant ;
- `collections.unwrapOr` et `strings.unwrapOr` dupliquent une API qui appartient
  conceptuellement à `Option[T]` ;
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
- blocs expressions, gardes `if`, `if/else`, `while ... do`, `break`, `continue` ;
- `Unit` public pour les effets et `Never` interne pour les chemins terminés ;
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

**Statut : terminée le 15 juillet 2026.**

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
8. **Livré le 14 juillet 2026** — couvrir directement chaque famille de diagnostic
   avec des `IrProgram` volontairement invalides : `IRV001` à `IRV004`, `IRV010`
   à `IRV013`, `IRV020` à `IRV024`, `IRV030` à `IRV032`, `IRV040` à `IRV045` et
   `IRV050` à `IRV054`. Le pipeline CTest exerce aussi la vérification intégrée
   par module et l'injection d'une IR invalide aux trois frontières publiques de
   sortie.
9. **Livré le 14 juillet 2026** — exécuter toute la suite et mesurer le coût sur
   cinq échantillons alternés de vingt compilations propres du showcase en
   Release, épinglées à un CPU. Sur Intel Core i5-10300H sous WSL2 avec GCC 15.2,
   la médiane est de 35,30 ms avec vérification contre 33,09 ms sans, soit
   +6,67 %. Un résultat validé opaque réutilisé par les sorties, des bitsets de
   définitions et des tables précalculées ont réduit le coût initial.
10. **Livré le 15 juillet 2026** — ramener le surcoût médian sous la cible de 5 %.
    La construction paresseuse des diagnostics, la réutilisation des buffers de
    définitions et la table plate des lectures donnent 34,38 ms avec vérification
    contre 32,83 ms sans sur le même protocole, soit +4,70 %.

### Critère de sortie

Toute IR envoyée au backend possède des fonctions correctement délimitées, des
valeurs et slots définis, des types cohérents et des chemins terminés. Une erreur
interne produit un diagnostic `IRV` reproductible avant l'assembleur.

## Priorité 1 — ergonomie fondamentale

Objectif : réduire fortement le bruit de `stdlib_showcase.zeta` sans modifier son
comportement ni affaiblir le typage.

### 1A. Inférence des variables locales

**Statut : livrée le 15 juillet 2026.** Le showcase ne conserve que l'annotation
nécessaire pour imposer le type `[Byte; 4]` de son tableau de littéraux entiers.

Syntaxe cible :

```zeta
val total = sequences.sum(values.asSlice())
var index = 0
```

Étapes :

1. **Livré le 15 juillet 2026** — rendre l'annotation locale optionnelle tout en
   la conservant aux signatures et déclarations globales ou publiques.
2. **Livré le 15 juillet 2026** — inférer depuis l'initialiseur après résolution
   générique, y compris les conversions `Slice` et `Box` déduites de leur opérande.
3. **Livré le 15 juillet 2026** — refuser les tableaux vides et les types
   génériques qui ne deviennent pas concrets.
4. **Livré le 15 juillet 2026** — couvrir `Option`, structures, enums, slices et
   types possédés dans le test d'intégration dédié.
5. **Livré le 15 juillet 2026** — vérifier qu'une annotation omise ne permet pas
   de contourner les règles de déplacement des valeurs possédées.

### 1B. Type `Unit`

**Livré le 15 juillet 2026.** Les fonctions et expressions sans résultat métier
utilisent désormais `Unit` :

```zeta
def log(value: String): Unit = io.println(value)
```

La représentation ABI est de taille nulle, les appels et retours n'utilisent pas
de registre de résultat, les blocs sans expression finale produisent `Unit` et les
branches `if/else` convergent sur ce type. `IrUnit`, le vérificateur structurel,
les interfaces `.zti` et l'invalidation ABI/cache couvrent ce contrat. L'API `io`
retourne maintenant `Unit`; ses tests d'exécution valident toujours les sorties.

Les compteurs des opérations mutantes de `Vec` restent provisoirement inchangés :
leur migration sera faite avec la composabilité de `Vec` en priorité 2 afin de ne
pas mélanger deux contrats publics.

### 1C. Contrôle de flux

**Livré le 15 juillet 2026.** Le contrôle de flux distingue désormais :

- `if` sans `else`, instruction de type `Unit` ;
- `return`, `break` et `continue`, dont les blocs terminés ont le type interne
  `Never` ;
- `if/else`, qui reste une expression typée et fait converger `Never` vers la
  branche atteignable.

Les gardes refusent les déplacements de valeurs possédées qui ne seraient pas
effectués sur tous les chemins. Les tests couvrent retours précoces, `break`,
`continue`, convergence avec une valeur et rejet d'un `if` sans `else` lorsqu'un
résultat métier est attendu.

### Critère de sortie

**Atteint le 15 juillet 2026.** L'exemple complet utilise l'inférence locale et
les appels `Unit` sans valeurs factices, conserve ses sorties et passe avec toute
la suite de tests.

## Priorité 2 — composabilité de `Vec` et méthodes

Objectif : permettre d'écrire une collection possédée entièrement en Zeta.

1. **Livré le 15 juillet 2026** — autoriser `push`, `set`, `reserve` et `clear`
   directement sur un champ `Vec[T]` d'une structure `var`, sans copie du
   propriétaire et avec rejet des liaisons immuables, empruntées ou déplacées ;
2. **Livré le 15 juillet 2026** — permettre `push`, `set`, `reserve` et `clear`
   à travers `&mut Vec[T]`, en propageant l'adresse du propriétaire dans l'IR,
   avec validation de la cible et rejet de `&Vec[T]` ;
3. **Livré le 15 juillet 2026** — définir les méthodes de structures non
   génériques dans leur module avec `self: &Type` ou `self: &mut Type`, emprunter
   automatiquement le receveur à l'appel, préserver les conflits d'emprunt et
   exporter le contrat dans les interfaces `.zti` ;
4. **Livré le 15 juillet 2026** — ajouter `extend def` pour les extensions
   génériques de `Vec[T]`, activer leur résolution uniquement par import,
   diagnostiquer les collisions entre modules et préserver leur corps dans les
   interfaces précompilées ;
5. **Livré le 15 juillet 2026** — rendre `values.sort()` disponible par une
   extension de `sequences`, produire les vues depuis `&Vec[T]` et `&mut Vec[T]`,
   puis déléguer au tri sur `SliceMut` sans dupliquer son algorithme ;
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

Point de départ attendu : branche `master`, commit `94519b2`, arbre propre,
stdlib régénérée et 419 tests réussis.

Poursuivre la priorité 2 avec `Stack[T]`. L'API cible minimale, écrite dans la
stdlib et non dans le compilateur, est :

```zeta
pub struct Stack[T] {
    values: Vec[T]
}

pub def Stack.push[T](self: &mut Stack[T], value: T): Unit
pub def Stack.pop[T](self: &mut Stack[T]): Option[T]
pub def Stack.isEmpty[T](self: &Stack[T]): Bool
```

Ordre de travail recommandé :

1. autoriser les méthodes inhérentes génériques dont les paramètres de type
   correspondent à ceux de leur structure propriétaire, puis préserver leur
   résolution, leur monomorphisation et leur corps dans les interfaces `.zti` ;
2. représenter en IR la projection d'un champ `Vec[T]` depuis `&Stack[T]` ou
   `&mut Stack[T]`, sans charger ni copier les trois mots propriétaires ;
3. permettre à `push` et `pop` d'utiliser la projection mutable, et à `isEmpty`
   d'utiliser la projection partagée, avec validation dédiée dans
   `IrVerifier` et prise en charge backend ;
4. implémenter `Stack[T]` dans la stdlib, probablement dans `collections`, sans
   constructeur retournant la structure tant que les retours d'agrégats de plus
   de 16 octets restent interdits ; la construction littérale suffit pour ce
   jalon ;
5. tester au minimum la pile vide, l'ordre LIFO, plusieurs types d'éléments, le
   déplacement d'une valeur possédée, le rejet sur `val`, les conflits d'emprunt,
   les contraintes de receveur et la consommation de `.zti` + `.o` sans sources ;
6. réviser le cache de modules si l'IR ou le code généré change, régénérer la
   stdlib, exécuter `git diff --check` puis toute la suite CTest, et documenter
   les limites restantes avant de passer à `Queue[T]`.

Critère de sortie : un programme utilisateur importe la stdlib, construit une
`Stack[Int]`, appelle `push`, `pop` et `isEmpty` avec la syntaxe méthode, passe
également avec la stdlib précompilée seule, et aucun builtin `Stack` n'existe dans
le lexer, l'analyse sémantique, l'IR ou le backend.

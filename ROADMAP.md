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
- 452 tests CTest réussis sur 452 ;
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
build/zeta examples/resource_showcase/main.zeta -o build/resource-showcase
build/resource-showcase
```

Le programme de référence utilisateur est
`examples/stdlib_showcase.zeta`. Toute évolution ergonomique importante devrait
chercher à le simplifier sans réduire sa sûreté.

`examples/resource_showcase/main.zeta` est le programme de référence des traits.
Il sépare contrat, implémentations et consommateur, puis exerce le dispatch
générique partagé et mutable, le retour `Self` et les contraintes multiples.

## Versions actuelles

| Contrat | Version |
| --- | ---: |
| Compilateur | `0.1.0` |
| ABI | `5` |
| Interface `.zti` | `13` |
| Tokens génériques | `4` |
| Cache de modules | `28` |
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

Zeta est un langage statiquement typé produisant des exécutables natifs. Le
backend historique FASM garde la cible Linux x86-64 autonome comme référence et
secours ; un backend LLVM/Clang expérimental doit maintenant être ajouté sans
régression pour préparer optimisations, informations de débogage et portabilité.
Il privilégie :

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

Pipeline cible avec Clang :

```text
source -> lexer -> parser -> AST -> analyse sémantique -> AST typé
       -> IR Zeta validée -> LLVM IR textuel -> clang/LLVM -> exécutable natif
```

Les deux backends doivent rester sélectionnables :

```text
zeta programme.zeta --backend=fasm  -o programme
zeta programme.zeta --backend=clang -o programme
zeta programme.zeta --emit-llvm     -o programme.ll
```

La migration vers Clang doit être incrémentale : `--emit-llvm` précède
`--backend=clang`, FASM reste l'oracle de comportement, et chaque test backend
doit comparer les résultats observables sur les mêmes programmes.

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

- les tableaux vides et appels génériques insuffisamment contraints exigent encore
  une annotation locale ;
- les algorithmes mutateurs autres que `sort` exposent encore `SliceMut` pour un
  usage courant ;
- `Option[T]` n'accepte pas encore de méthodes inhérentes utilisateur ; son API
  commune reste donc qualifiée par le module `option` ;
- tous les parcours utilisent une boucle `while` et un indice manuel ;
- les offsets UTF-8 en octets sont corrects mais trop bas niveau pour l'usage
  quotidien ;
- un `Vec[T]` placé dans une structure ne permet pas encore de construire
  naturellement une nouvelle collection ;
- les traits restent volontairement statiques : méthodes par défaut, types
  associés, objets de traits et vtables ne sont pas encore disponibles ;
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
- stdlib précompilée avec manifeste et fallback source ;
- toolchain LLVM/Clang/LLD 19 disponible localement sous `/opt/data/local/bin`
  pour le futur backend expérimental.

### Bibliothèque standard

La stdlib publique contient exactement :

- `io` ;
- `collections` ;
- `option` ;
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

Les opérations mutantes de `Vec` ont été migrées vers `Unit` avec `Stack[T]` en
priorité 2.

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
6. **Livré le 15 juillet 2026** — écrire `Stack[T]` comme validation minimale,
   avec méthodes inhérentes génériques, projection directe de son champ `Vec[T]`,
   opérations mutantes `Unit` et consommation précompilée sans sources ;
7. **Livré le 15 juillet 2026** — écrire `Queue[T]` comme critère de sortie réel,
   avec deux `Vec[T]`, transferts amortis sans copie, ordre FIFO, éléments
   possédés et consommation précompilée sans sources.

Le compilateur ne doit pas recevoir un nouveau builtin `Stack` ou `Queue`. Le but
est précisément de prouver que `Vec` est une brique de fondation suffisante.

**Critère de sortie atteint le 15 juillet 2026.** `Stack[T]` et `Queue[T]` sont
entièrement écrites dans `collections`, fonctionnent avec des éléments `Box[T]`
et ne possèdent aucun traitement nominal dans le compilateur.

## Priorité 3 — généricité composable

### Contraintes multiples

**Livré le 15 juillet 2026.** Les listes avec `+` sont triées et sérialisées sous
une forme canonique. Les doublons et noms inconnus sont rejetés ; chaque capacité
est vérifiée sur les types concrets et la propagation entre fonctions génériques
exige un sur-ensemble. Les contraintes intégrées étant toutes positives, aucune
paire n'est actuellement incompatible. Le contrat participe aux empreintes et
identités génériques, passe dans `.zti` et fonctionne sans sources avec les
versions interface `11`, tokens `3` et cache `24`.

Syntaxe cible indicative :

```zeta
def replaceAll[T: Equatable + Copy](values: SliceMut[T], old: T, next: T): Int
```

### API commune d'Option

**Livré le 15 juillet 2026.** Le module standard `option` fournit l'unique
implémentation générique de `isNone[T: Copy]` et `unwrapOr[T: Copy]`. Les anciennes
exportations de `collections` et `strings` ont été retirées et tous les
consommateurs migrés. La paire `option.zti` + `option.o` est validée sans sources,
avec des instanciations sur plusieurs types `Copy` et un rejet sur `Box[Int]`.
Le cache de modules passe à `25`.

Une future prise en charge des méthodes utilisateur sur les enums pourra rendre
les appels plus concis sans réintroduire de duplication.

### Traits utilisateur

**Fondation livrée le 15 juillet 2026.** `trait Nom {}` déclare une capacité
nominale sans état et `impl Nom for Type {}` l'accorde à un type concret. Les
traits se composent avec `+`, participent à la propagation des contraintes et
restent entièrement monomorphisés. La cohérence autorise une implémentation au
propriétaire du trait ou du type ; les paires dupliquées, implémentations
orphelines et fuites d'un trait privé sont rejetées.

Les traits publics et leurs implémentations sont sérialisés dans `.zti`. Une
implémentation définie par le propriétaire d'un type dans un second module est
validée avec les seules paires `.zti` + `.o`. Ce contrat porte les versions à
l'interface `12`, aux tokens génériques `4` et au cache de modules `26`.

**Méthodes requises livrées le 15 juillet 2026.** Le corps d'un trait accepte des
signatures utilisant `Self`, avec receveur partagé ou mutable. Chaque bloc `impl`
doit fournir exactement les méthodes attendues ; noms de paramètres, types,
mutabilité et retour sont vérifiés après substitution de `Self`. Les méthodes sur
structures concrètes réutilisent le dispatch statique existant.

Le format `.zti` `13` sérialise les signatures, les noms fournis et les exports de
méthodes. Le chargeur refuse avec `ZTI400` une interface incomplète ou
incompatible. Le cache de modules passe à `27`; les tokens restent en version
`4`, aucun nouveau genre lexical n'ayant été ajouté.

**Dispatch générique livré le 15 juillet 2026.** Un appel `value.method()` sur
`T` sélectionne sa signature dans les contraintes, substitue `Self` et conserve
l'identité canonique du trait dans l'AST typé. La monomorphisation retrouve
ensuite la paire `(trait, type concret)` et appelle directement l'export de
l'implémentation. Les receveurs partagés et mutables, le retour `Self`, la
compilation intermodules sans sources et les ambiguïtés entre traits sont
couverts. Ce changement de codegen porte le cache de modules à `28`, sans changer
l'interface `13` ni les tokens `4`.

Les méthodes par défaut, types associés, méthodes de traits sur enums, objets de
traits et vtables restent reportés. Le cœur prévu de la priorité 3 est terminé ;
la prochaine étape active est le protocole d'itération de la priorité 4.

## Priorité 4 — itération

Objectif : remplacer les boucles indexées répétitives de la stdlib par une
abstraction sûre.

**Socle livré le 16 juillet 2026.** `docs/ITERATION_DESIGN.md` définit le
protocole initial sans allocation : état copiable séparé de la vue, dispatch
statique, pas de closure, pas de vtable et pas d'itérateur possédant les éléments.
La première API publique reste volontairement minimale et ABI-stable :
`iterate`, `iterateMut`, `hasNext`, `hasNextMut`, `position` et `advance` exposent
un état `Int` plutôt qu'un type `IndexIterator` public prématuré. La première
syntaxe `for (value in view) do { ... }` est livrée pour `Slice[T]` et
`SliceMut[T]` avec `T: Copy`, et s'abaisse sans allocation vers une boucle
indexée compatible avec ce protocole.

`tests/sequences_iteration.zeta` couvre les parcours partagés et mutables de
`Slice[Int]`, `SliceMut[Int]`, tableaux et `Vec[Int]`. `tests/for_iteration.zeta`
couvre la syntaxe `for` sur `Slice[Int]`, `SliceMut[Int]`, `Vec[Int].asSlice()`
et `[Int; N]` directement. `sequences` utilise déjà ces helpers pour ses scans linéaires partagés
(`contains`, `indexOf`, `count`,
`allEqual`, `sum`, `product`, `minimum`, `maximum`, `equals`, `startsWith`,
`endsWith`, `isSorted`) et pour `fill` en mutation. Les algorithmes dont la forme
n'est pas un simple parcours avant (`lastIndexOf`, recherche binaire, bornes,
`reverse`, `sort`) restent explicitement indexés.

Étapes restantes :

1. **Livré le 16 juillet 2026** — préciser l'itération consommatrice pour
   `Vec[T]` propriétaire : `for (value in values)` déplace les éléments sans
   copie implicite, conserve l'ordre d'insertion, droppe l'élément de boucle non
   déplacé et interdit la réutilisation du vecteur consommé ;
2. **Livré le 16 juillet 2026** — fournir un parcours de `String` et
   `StringView` par `Char`, masquant les offsets UTF-8 derrière l'état interne ;
3. vérifier les emprunts jusqu'à la dernière utilisation pour les corps de boucle.

L'itération directe des tableaux est livrée par abaissement spécialisé sur la
longueur statique : `for (value in values)` fonctionne pour `[T; N]` lorsque
`T: Copy`, sans conversion publique vers slice. La séparation partagé/mutable est
également amorcée : `for (mut value in values)` sur `SliceMut[T]` produit une
référence mutable d'élément `&mut T`, ce qui permet de modifier `Int` en place et
de remplacer des `Box[T]` sans copie implicite.

La couverture négative dédiée de `for` est maintenant livrée : source non
iterable, élément non `Copy` sur slice et tableau, nom de variable dupliqué,
demande `mut` sur vue partagée et emprunt actif d'un `Vec` pendant
`for (value in values.asSlice())`.

Première action de la prochaine session : préciser l'itération consommatrice des
valeurs possédées. Commencer par écrire les tests RED qui distinguent clairement
un futur `into`/parcours par déplacement d'un parcours partagé ou mutable par
référence, sans déplacer implicitement les éléments de `Vec` ou de tableau.

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

## Priorité 7 — backend LLVM/Clang et performances

À commencer après le vérificateur d'IR, mais sans bloquer les améliorations
d'ergonomie indépendantes. Le backend FASM reste la référence ; Clang est ajouté
comme cible expérimentale validée par comparaison.

### 7A. Backend LLVM/Clang expérimental

Objectif : produire du LLVM IR textuel depuis l'IR Zeta validée, puis utiliser
`clang` comme driver d'assemblage, d'optimisation et de linkage.

1. introduire `--backend=fasm|clang` avec `fasm` par défaut ;
2. ajouter `--emit-llvm` pour écrire le `.ll` sans lancer le linkage ;
3. extraire une interface backend autour de `FasmCodeGenerator` ;
4. créer `LlvmIrCodeGenerator` avec `target triple = "x86_64-pc-linux-gnu"` ;
5. couvrir d'abord `Int`, `Bool`, fonctions, appels, labels, branches, boucles et
   retours ;
6. compiler le `.ll` avec `clang` et comparer codes de sortie/stdout avec FASM ;
7. étendre ensuite à `Byte`, `Double`, `Char`, agrégats, tableaux, références,
   slices, contrôles de bornes et chemins d'erreur ;
8. définir une ABI commune pour runtime, `String`, `Box`, `retain`, `drop` et
   destruction déterministe ;
9. produire et relier les objets par module, la runtime et la stdlib via `clang` ;
10. afficher des diagnostics clairs si `clang` ou `llvm-config` est absent, et
    permettre de configurer leur chemin.

Critère de sortie : un sous-ensemble documenté passe sur FASM et Clang, puis la
stdlib et les modules séparés rejoignent progressivement cette matrice.

### 7B. Optimisations communes

Certaines optimisations restent dans l'IR Zeta pour bénéficier aux deux backends ;
d'autres pourront être déléguées à LLVM quand `--backend=clang` sera stable.

1. mesures reproductibles de taille et de temps pour FASM et Clang ;
2. propagation et pliage des constantes ;
3. simplification algébrique ;
4. suppression de code mort ;
5. réduction des temporaires de pile pour FASM ;
6. élimination des copies d'agrégats inutiles ;
7. meilleur tri générique pour les grandes séquences ;
8. informations de débogage source, idéalement via LLVM/DWARF ;
9. autres architectures et formats objets via LLVM.

Chaque passe doit produire une IR valide avant et après transformation.

## Limites opérationnelles à garder visibles

- `--build-stdlib` ne purge pas les anciens `.o`/`.zti` sans source correspondante ;
  le manifeste empêche leur chargement, mais un nettoyage manuel peut être requis ;
- `stdlib/precompiled/` est local et ignoré par Git ;
- cible de production actuelle Linux x86-64 avec FASM et `ld` ;
- backend LLVM/Clang expérimental limité à `--emit-llvm` minimal pour le moment ;
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
- comparer FASM et Clang sur les mêmes programmes pour tout chantier backend ;
- mettre à jour README, roadmap et document de conception associé ;
- ne jamais committer `build/` ou `stdlib/precompiled/` ;
- ne jamais effacer les changements locaux d'un utilisateur pour nettoyer le
  dépôt.

## Première action de la prochaine session

La syntaxe `for` sans allocation est livrée pour `Slice[T]`, `SliceMut[T]` et
`[T; N]` lorsque `T: Copy`. `for (mut value in SliceMut[T])` lie `value` comme
`&mut T` pour muter en place sans copier les éléments possédés non `Copy`.
`for (value in Vec[T])` consomme un vecteur propriétaire par retrait destructif
en ordre d'insertion : l'élément est déplacé dans `value`, le vecteur source est
considéré déplacé après la boucle, et les éléments non explicitement déplacés par
le corps sont droppés à la fin de leur itération. `for (value in text)` sur
`String` ou `StringView` parcourt maintenant les points de code Unicode en `Char`
avec un état d'offset d'octet interne.

Les diagnostics négatifs couvrent source non iterable, élément non `Copy` sur
parcours emprunté, nom d'élément dupliqué, demande `mut` sur vue partagée ou
chaîne, mutation d'un `Vec` dont la vue `asSlice()` est active, réutilisation
d'un `Vec` consommé par `for`, et ordre d'insertion de l'itération
consommatrice.

L'analyse d'emprunts dans les corps de boucle a été renforcée : les tests
`loop_local_slice_last_use_allows_vec_push` et
`loop_local_slice_mut_last_use_allows_vec_push` valident qu'une vue locale
`asSlice()`/`asSliceMut()` est libérée à sa dernière utilisation avant une
mutation ultérieure dans la même itération, tandis que
`reject_for_iterable_slice_blocks_vec_push` verrouille l'emprunt caché de
l'itérable d'un `for` jusqu'à la fin de la boucle. La contrainte reste inchangée :
pas de trait public `Iterator`, pas d'allocation de state machine, et pas de
copie implicite pour `T` non `Copy`.

Le backend LLVM/Clang expérimental avance par tranches :
`docs/LLVM_BACKEND_DESIGN.md` fixe le périmètre, `--emit-llvm` écrit un `.ll`
depuis l'IR Zeta vérifiée, `emit_llvm_minimal` compile le smoke test
`main(): Int = 42` avec `clang`, et `emit_llvm_scalars` couvre maintenant les
opérations scalaires `Int`/`Bool` (`IrBinary`, comparaisons, booléens simples,
`IrCopy` sans allocation). `emit_llvm_control` ajoute les labels/branches,
`IrLoad`/`IrStore` scalaires et valide un programme combinant `if` et `while`
compilé puis exécuté via `clang`. `emit_llvm_parameters` couvre maintenant les
signatures de fonctions avec paramètres `Int`/`Bool`, `IrParameter` et les appels
argumentés. `--backend=clang` est maintenant activé pour ce sous-ensemble : il
écrit le `.ir` et le `.ll`, appelle `clang -x ir`, puis produit l'exécutable natif
validé par `compile_clang_backend_parameters`. FASM reste le backend par défaut,
protégé par `run_fasm_backend_still_default`. Les diagnostics CLI du backend
expérimental sont couverts par `reject_clang_backend_cli_diagnostics` : backend
inconnu, `--emit-llvm` forcé vers FASM, et usage Clang/LLVM sur les modes
bibliothèque/stdlib non pris en charge. `compile_clang_backend_global_values`
élargit maintenant le périmètre runtime contrôlé aux imports de modules et aux
`pub val` globales scalaires `Int`/`Bool` initialisées dans le wrapper principal,
en comparant l'exécution Clang au backend FASM.

Prochaine étape : élargir le backend Clang par tests RED/GREEN au prochain
périmètre runtime contrôlé : diagnostics LLVM pour les globales/types non
scalaires, puis seulement ensuite agrégats, chaînes et stdlib.

La limite ABI reste visible : `Stack[T]` et `Queue[T]` se construisent encore par
littéral, car leurs agrégats dépassent 16 octets.

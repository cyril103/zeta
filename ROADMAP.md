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

## Priorité 7 — migration LLVM/Clang comme backend principal

Objectif stratégique actualisé : **remplacer FASM comme backend de développement
principal**. FASM reste temporairement l'oracle de compatibilité et le filet de
sécurité, mais les nouvelles tranches de Zeta doivent désormais viser d'abord le
backend LLVM/Clang (`--backend=clang`), puis préserver FASM tant qu'il sert à la
comparaison.

### 7A. Socle LLVM/Clang déjà livré

Le backend LLVM/Clang n'est plus une simple preuve de concept : il produit des
exécutables natifs via `clang -x ir`, écrit les artefacts `.ir`/`.ll`, et couvre un
sous-ensemble applicatif riche validé par CTest contre FASM. Le socle livré inclut
notamment :

1. `--backend=fasm|clang`, `--emit-llvm`, diagnostics CLI et FASM encore par défaut ;
2. génération textuelle LLVM après IR Zeta vérifiée (`LlvmIrCodeGenerator`) ;
3. `Int`/`Bool`/`Byte`/`Char`/`Double`, arithmétique, comparaisons, conversions
   couvertes, `Bool` -> `String` via helper runtime interne et `Double` en `double` ;
4. fonctions, paramètres, appels, retours, labels, branches, boucles, slots locaux
   et copies SSA matérialisées si plusieurs chemins les définissent ;
5. imports de modules source et globales scalaires `Int`/`Bool` ;
6. `String`/`StringView` : littéraux, slots locaux, concat heap, `lengthBytes`,
   `isEmpty`, vues, recherche, UTF-8, itération `for` ;
7. `io.print`/`io.println(String)`, `io.printInt`/`io.printlnInt`,
   `io.printBool`/`io.printlnBool`, `io.printByte`/`io.printlnByte`,
   `io.printChar`/`io.printlnChar` et `io.printDouble`/`io.printlnDouble` via
   helpers runtime internes `zeta_rt_*` ;
8. structs locaux simples, mixtes et imbriqués, ABI de fonctions portant des
   structs simples, mutations de champs et sous-champs ;
9. ownership des chaînes heap dans des structs LLVM : propagation par chemins de
   champs, `drop`, `retain`/copie, remplacement répété de champs `String` heap ;
10. diagnostics explicites pour agrégats/globals/types encore hors périmètre.

### 7B. Plan court terme pour basculer le développement sur LLVM

Les prochaines sessions doivent transformer LLVM de backend couvert à backend de
travail par défaut pour le développement courant :

1. **Livré le 17 juillet 2026 — ownership interprocédural des strings dans
   structs** : `compile_clang_backend_struct_heap_string_interprocedural` couvre
   une fonction qui retourne une struct contenant un `String` heap et une fonction
   qui reçoit/copie cette struct. Le backend LLVM propage maintenant les chemins
   de propriété par paramètres, appels et retours, conserve les strings statiques
   avec un refcount sentinelle, incrémente les copies heap et décrémente/free aux
   drops sans double libération.
2. **En cours le 17 juillet 2026 — ABI runtime/stdlib par Clang** : premières
   frontières stabilisées pour `io.print`/`io.println(String)`, `io.printInt`/
   `io.printlnInt`, `io.printBool`/`io.printlnBool`, `io.printByte`/
   `io.printlnByte`, `io.printChar`/`io.printlnChar` et `io.printDouble`/
   `io.printlnDouble`. Les appels `String` passent par le helper LLVM interne
   `@zeta_rt_io_write_string(ptr, i64, i1)`, les appels `Int` par
   `@zeta_rt_io_write_int(i32, i1)`, les appels `Bool` par
   `@zeta_rt_io_write_bool(i1, i1)`, les appels `Byte` par
   `@zeta_rt_io_write_byte(i8, i1)`, les appels `Char` par
   `@zeta_rt_io_write_char(i32, i1)` et les appels `Double` par
   `@zeta_rt_io_write_double(double, i1)`, au lieu de dupliquer les appels `write` ou
   `printf` dans les corps applicatifs. `compile_clang_backend_io_println_string`,
   `compile_clang_backend_io_println_int`, `compile_clang_backend_io_println_bool`,
   `compile_clang_backend_io_println_byte`, `compile_clang_backend_io_println_char`
   et `compile_clang_backend_io_println_double`
   verrouillent chacun la définition unique du helper, les chemins `print`/
   `println` et la comparaison stdout Clang/FASM.
3. **En cours le 17 juillet 2026 — conversions `String(value)` par Clang** :
   `String(Bool)` passe maintenant par le helper LLVM interne
   `@zeta_rt_string_from_bool(i1)`, qui réutilise les constantes privées
   `true`/`false` et retourne la paire `{ ptr, i64 }` sans allocation. Le test
   `compile_clang_backend_string_bool_conversion` verrouille la définition unique,
   les appels applicatifs, la sortie Clang et la comparaison FASM.
4. **En cours le 17 juillet 2026 — helpers `strings.*` par Clang** :
   `strings.view` passe maintenant par le helper LLVM interne
   `@zeta_rt_strings_view(ptr, i64, i32, i32)`, `strings.viewIsValid` par
   `@zeta_rt_strings_view_is_valid(ptr)`, `strings.decodeAtByte` par
   `@zeta_rt_strings_decode_at_byte(ptr, i64, i32)`, `strings.nextByteOffset`
   par `@zeta_rt_strings_next_byte_offset(ptr, i64, i32)`, et `strings.indexOf` /
   `strings.contains` passent par `@zeta_rt_strings_index_of(ptr, i64, ptr, i64)`,
   au lieu de dupliquer les bornes, le calcul de pointeur, le sentinelle
   `{ null, 0 }`, le test de validité, le décodage UTF-8 direct, le calcul d'offset
   UTF-8 suivant et la boucle `memcmp` dans chaque corps applicatif.
   `compile_clang_backend_string_view`, `compile_clang_backend_string_utf8_decode`,
   `compile_clang_backend_for_string_char_iteration` et `compile_clang_backend_string_search`
   verrouillent chacun une définition unique des helpers, les appels applicatifs et
   la comparaison Clang/FASM.
5. **Suite runtime/stdlib par Clang** : étendre cette ABI stable aux autres
   primitives `strings.*` (UTF-8) et aux conversions
   générales vers `String` restantes, après consolidation des `io.print*`/`io.println*`
   primitifs directs derrière `zeta_rt_*`, sans réintroduire d'assembleur FASM
   dans le chemin Clang.
6. **Modules séparés et stdlib précompilée** : produire/relier objets runtime,
   modules et stdlib via `clang`, puis lever les diagnostics FASM-only de
   `--build-library`, `--install-library` et `--build-stdlib` pour le backend LLVM.
7. **Agrégats restants** : décider et implémenter le support LLVM ou les rejets
   définitifs pour globals agrégats, tableaux dans structs, `Box`, `Vec`, enums et
   grands agrégats dépassant l'ABI actuelle.
8. **Mode par défaut contrôlé** : ajouter un job/test de matrice qui compile les
   exemples et la stdlib avec `--backend=clang`; quand il est vert, inverser le
   défaut local de développement vers Clang tout en gardant `--backend=fasm` comme
   fallback explicite.
9. **Retrait progressif de FASM** : une fois la matrice LLVM verte sur exemples,
   stdlib et modules, figer FASM en backend legacy puis supprimer les dépendances
   de développement qui bloquent la portabilité.

Critère de sortie de la migration : les exemples non triviaux, la stdlib et les
modules séparés se construisent et s'exécutent via LLVM/Clang sans recourir à FASM;
FASM n'est plus le backend par défaut ni nécessaire au développement de nouvelles
fonctionnalités Zeta.

### 7C. Optimisations communes

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
- cible de transition actuelle Linux x86-64 : FASM reste disponible, mais LLVM/Clang
  est le backend visé pour le développement principal ;
- backend LLVM/Clang déjà exécutable sur un large sous-ensemble, mais modules
  séparés, stdlib précompilée et quelques familles d'agrégats restent à basculer ;
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

Reprendre la migration LLVM avec l'objectif explicite de remplacer FASM comme
backend de développement. Le dépôt est vert après
`compile_clang_backend_string_search` : **527 tests CTest passent** et le backend
LLVM couvre déjà scalaires, strings, IO ciblée, structs imbriqués, copies à
travers branches, ownership heap-string intra-struct et propagation de cette
propriété par paramètres/retours de fonctions portant des structs.

Démarrer par la tranche RED/GREEN suivante de l'ABI runtime/stdlib LLVM :
**poursuivre l'extraction des primitives `strings.*`** ou une conversion générale
vers `String`, maintenant que `strings.view`, la recherche `StringView` et les
`io.*` directs primitifs passent par `zeta_rt_*`, en réduisant les lowerings
spécialisés dans les corps applicatifs. Les briques livrées sont
`@zeta_rt_io_write_string(ptr, i64, i1)`
pour `io.print`/
`io.println(String)`, `@zeta_rt_io_write_int(i32, i1)` pour `io.printInt`/
`io.printlnInt`, et `@zeta_rt_io_write_bool(i1, i1)` pour `io.printBool`/
`io.printlnBool`, `@zeta_rt_io_write_byte(i8, i1)` pour `io.printByte`/
`io.printlnByte`, et `@zeta_rt_io_write_char(i32, i1)` pour `io.printChar`/
`io.printlnChar`, `@zeta_rt_io_write_double(double, i1)` pour `io.printDouble`/
`io.printlnDouble`, `@zeta_rt_string_length_bytes(ptr, i64)` pour
`String.lengthBytes`, `@zeta_rt_string_is_empty(ptr, i64)` pour
`String.isEmpty`, `@zeta_rt_strings_view(ptr, i64, i32, i32)` pour
`strings.view`, et `@zeta_rt_strings_view_is_valid(ptr)` pour
`strings.viewIsValid`, `@zeta_rt_strings_decode_at_byte(ptr, i64, i32)` pour
`strings.decodeAtByte`, `@zeta_rt_strings_next_byte_offset(ptr, i64, i32)` pour
`strings.nextByteOffset`, `@zeta_rt_strings_index_of(ptr, i64, ptr, i64)` pour
`strings.indexOf`/`strings.contains`, `@zeta_rt_string_from_bool(i1)` pour
`String(Bool)`, `@zeta_rt_string_from_int(i32)` pour `String(Int)`,
`@zeta_rt_string_from_byte(i8)` pour `String(Byte)`,
`@zeta_rt_string_from_char(i32)` pour `String(Char)` et
`@zeta_rt_string_from_double(double)` pour `String(Double)`, verrouillées respectivement par
`compile_clang_backend_io_println_string`, `compile_clang_backend_io_println_int`,
`compile_clang_backend_io_println_bool`, `compile_clang_backend_io_println_byte`,
`compile_clang_backend_io_println_char`, `compile_clang_backend_io_println_double`,
`compile_clang_backend_string_literal`, `compile_clang_backend_string_is_empty`,
`compile_clang_backend_string_view`, `compile_clang_backend_string_utf8_decode`,
`compile_clang_backend_for_string_char_iteration`, `compile_clang_backend_string_search`,
`compile_clang_backend_string_bool_conversion`,
`compile_clang_backend_string_int_conversion`,
`compile_clang_backend_string_byte_conversion`,
`compile_clang_backend_string_char_conversion` et
`compile_clang_backend_string_double_conversion`, ainsi que
`compile_clang_backend_build_library` pour le premier objet bibliothèque LLVM et
`compile_clang_backend_shared_library_cache` pour la liaison d'un exécutable Clang
contre des bibliothèques précompilées LLVM installées, et
`compile_clang_backend_build_stdlib` pour la première précompilation stdlib simple
via LLVM, `compile_clang_backend_global_string` pour les `pub val String`
globales LLVM, `compile_clang_backend_global_double` pour les `pub val Double`
globales LLVM, et `compile_clang_backend_global_byte` pour les `pub val Byte`
globales LLVM.
Les tests doivent continuer à comparer Clang et FASM tant que FASM sert d'oracle,
mais la nouvelle frontière doit être conçue pour le backend LLVM principal.

Après cette tranche :

1. poursuivre la généralisation des helpers runtime/stdlib encore
   spécialisés `io.*`/`strings.*` ;
2. fait partiel : `--build-library --backend=clang` produit maintenant `.zti`,
   `.ll` et `.o` via `clang -c` pour les modules bibliothèque simples,
   `--backend=clang` relie les exécutables aux objets précompilés LLVM installés
   (`.zti`/`.o`) en copiant les dépendances dans `<app>.modules`, et
   `--build-stdlib --backend=clang` précompile désormais les modules stdlib simples
   en `.zti`/`.ll`/`.o`, les `pub val String` globales sont émises en
   `{ ptr, i64 } zeroinitializer` puis initialisées dans le wrapper `@main`,
   les `pub val Double` globales sont émises en `global double 0.000000e+00`, et
   les `pub val Byte` globales sont émises en `global i8 0` ;
   poursuivre avec la vraie stdlib complète, ses modules
   génériques/agrégats et les modules locaux non précompilés ;
3. lever les diagnostics FASM-only restants des modes stdlib quand les objets
   LLVM sont reliés ;
4. ajouter une matrice exemples + stdlib en `--backend=clang` ;
5. inverser le backend par défaut vers Clang lorsque cette matrice est verte, en
   gardant `--backend=fasm` comme fallback legacy explicite.

Discipline : commits petits et séparés, tests RED/GREEN, comparaison FASM tant
que FASM sert d'oracle, puis mise à jour de `ROADMAP.md` et
`docs/LLVM_BACKEND_DESIGN.md` à chaque étape de bascule.

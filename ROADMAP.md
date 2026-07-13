# Roadmap de Zeta

Ce document décrit l'état réel du compilateur et l'ordre recommandé des prochains
chantiers. Une fonctionnalité n'est considérée comme terminée que si elle possède
des diagnostics, une génération de code exécutable et des tests positifs et négatifs.

## Vision

Zeta est un langage statiquement typé, compilé vers des exécutables Linux x86-64
autonomes. Il privilégie une sémantique explicite, la monomorphisation des
génériques, une propriété déterministe et une compilation séparée simple.

Pipeline actuel :

```text
source -> lexer -> parser -> AST -> analyse sémantique -> AST typé
       -> IR par module -> assembleur FASM -> objets ELF64 -> ld -> exécutable
```

## Fondations disponibles

### Langage

- déclarations `val`, `var` et `def` ;
- fonctions globales typées, récursion, `return` et appels terminaux ;
- blocs d'expressions et portées lexicales ;
- `if`, `else`, `while ... do`, `break` et `continue` ;
- opérateurs arithmétiques, logiques et comparaisons typées ;
- conversions explicites entre les types compatibles ;
- types primitifs `Int`, `Byte`, `Double`, `Bool`, `Char` et `String` ;
- tableaux fixes `[T; N]`, tableaux imbriqués et contrôles de limites ;
- références `&T` et `&mut T` avec contrôle des alias ;
- `Slice[T]` et `SliceMut[T]` sous la forme `{adresse, longueur}` ;
- `Box[T]`, déplacement sans copie et destruction déterministe ;
- structures ordinaires et génériques, construction nommée et mutation des champs ;
- énumérations ordinaires et génériques, variantes à charge utile et `match`
  exhaustif ;
- fonctions génériques monomorphisées avec inférence et contraintes intégrées
  `Copy`, `Numeric`, `Ordered` et `Equatable`.

### Chaînes

- UTF-8 validé et échappements Unicode ;
- représentation ABI `{données, longueur}` et objets gérés par comptage de références ;
- concaténation avec `+` ;
- égalité exacte sur les octets ;
- conversions `String(Int)`, `String(Byte)`, `String(Double)`, `String(Bool)` et
  `String(Char)` ;
- passage et retour par valeur, affichage avec le module `io`.

### Modules et outillage

- imports récursifs, visibilité `pub`, noms qualifiés et détection des cycles ;
- IR, assembleur et véritable objet ELF64 distincts pour chaque module ;
- symboles de fonctions et globales liés entre les objets ;
- objet `start.o` minimal et initialisation topologique des modules ;
- cache incrémental invalidé par les sources et les interfaces importées ;
- interfaces persistantes versionnées `.zti` consommables avec un `.o` sans source ;
- corps génériques publics conservés pour la monomorphisation côté consommateur ;
- stdlib précompilable avec `zeta --build-stdlib` ;
- manifeste partagé vérifiant compilateur, ABI, format `.zti` et empreintes des sources ;
- sélection alternative de la stdlib avec `--stdlib <dossier>`.

### Bibliothèque standard

- `io` : affichage de `String`, `Char`, `Int`, `Byte`, `Bool`, `Double` et
  `Slice[Byte]` ;
- `collections` : accès génériques sûrs `first`, `second` et `at` sur `Slice[T]`,
  avec `Option[T]`, `isNone` et `unwrapOr`.

## Limites connues

- pas de collection dynamique possédée (`Vec[T]`, dictionnaire, ensemble) ;
- pas encore d'API publique pour la longueur, l'indexation Unicode ou les
  sous-chaînes de `String` ;
- les références ne peuvent pas être retournées ni stockées globalement ;
- les durées de vie restent lexicales avec réduction à la dernière utilisation,
  sans inférence générale interprocédurale ;
- les fonctions locales capturantes ne sont pas encore de véritables fermetures ;
- les structures publiques ne sont pas encore exportées dans les `.zti` ;
- les corps génériques sont conservés textuellement dans les interfaces, pas sous
  forme d'AST sérialisée compacte ;
- l'IR ne possède pas encore de pipeline d'optimisation ;
- la cible unique reste Linux x86-64 avec FASM et `ld`.

## Priorité 1 — Énumérations et Option — terminée

Objectif : permettre aux APIs de signaler explicitement l'absence ou l'échec.

Syntaxe à concevoir :

```zeta
enum Option[T] {
    Some(value: T)
    None
}
```

Travail livré :

1. syntaxe des variantes et construction qualifiée ;
2. calculer une disposition ABI avec discriminant et charge utile ;
3. ajouter une expression de correspondance exhaustive ;
4. diagnostiquer les variantes inconnues, doublons et correspondances incomplètes ;
5. monomorphiser les énumérations génériques ;
6. fournir `Option[T]` dans la stdlib ;
7. remplacer les accès partiels de `collections` par des variantes sûres.

Critère de sortie : une fonction `first` sûre doit pouvoir retourner `None` pour
une slice vide sans arrêt du processus — couvert par les tests d'intégration.

## Priorité 2 — API String et vues UTF-8

Objectif : rendre `String` utilisable autrement que par concaténation et affichage.

Ordre recommandé :

1. `lengthBytes` et `isEmpty` ;
2. validation et décodage d'un point de code UTF-8 ;
3. itération contrôlée sur les `Char` ;
4. type de vue non possédée pour les sous-chaînes ;
5. recherche et comparaison de vues ;
6. factorisation complète du formatage entre `String(value)` et `io.print...`.

Les APIs d'indexation devront retourner `Option[Char]` plutôt qu'utiliser un index
d'octet ambigu.

## Priorité 3 — Collections dynamiques

Objectif : ajouter un premier conteneur possédé, probablement `Vec[T]`.

Étapes :

1. définir la représentation `{adresse, longueur, capacité}` ;
2. introduire allocation, croissance et libération déterministes ;
3. appliquer les règles de déplacement aux éléments non `Copy` ;
4. exposer des vues `Slice[T]` et `SliceMut[T]` sans copie ;
5. fournir `push`, `pop`, `get`, `set`, `reserve` et `clear` ;
6. tester les types primitifs, structures, `String` et `Box[T]` ;
7. intégrer le module à la stdlib précompilée.

Le comportement en cas d'échec d'allocation doit être défini avant l'ajout de
plusieurs conteneurs.

## Priorité 4 — Interfaces publiques complètes

Objectif : stabiliser `.zti` comme véritable format de distribution de bibliothèques.

Travail prévu :

1. visibilité et export des structures et futures énumérations ;
2. sérialisation de leur disposition, champs, variantes et paramètres génériques ;
3. remplacement des sources génériques incorporées par une AST versionnée ;
4. diagnostics précis pour ABI ou interface incompatible ;
5. commande explicite de construction d'une bibliothèque ;
6. installation dans un cache partagé indépendant d'un projet ;
7. déduplication robuste des instances génériques entre consommateurs.

## Priorité 5 — Optimisations IR

À commencer après stabilisation des types de données :

1. vérification structurelle systématique de l'IR ;
2. évaluation et propagation des constantes ;
3. simplification algébrique ;
4. suppression des instructions et fonctions mortes ;
5. réduction et réutilisation des temporaires de pile ;
6. élimination des copies d'agrégats inutiles ;
7. optimisation des appels terminaux et inlining contrôlé ;
8. mesures reproductibles de taille et de temps de compilation.

## Chantiers ultérieurs

- boucles `for` et protocole d'itération ;
- traits définissables par l'utilisateur ;
- fermetures et fonctions de première classe ;
- gestion d'erreurs structurée ;
- entrée standard et fichiers ;
- débogage avec informations de lignes ;
- autres architectures ou formats objets ;
- mode bibliothèque et gestionnaire de paquets.

## Qualité continue

Chaque étape doit :

- être isolée dans un commit cohérent ;
- ajouter des tests de compilation, d'exécution et de rejet ;
- vérifier les frontières ABI et la compilation séparée lorsque concernées ;
- préserver le fallback source et l'invalidation des artefacts précompilés ;
- exécuter `git diff --check`, la construction CMake et toute la suite CTest ;
- mettre à jour le README et les documents de conception associés.

## Prochaine session recommandée

Commencer la priorité 2 avec `String.lengthBytes` et `String.isEmpty`, puis définir
le contrat des vues UTF-8 avant toute indexation par point de code.

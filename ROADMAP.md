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
- `Vec[T]`, croissance dynamique, déplacement, destruction et vues empruntées ;
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
- propriétés `lengthBytes` et `isEmpty`, décodage et itération UTF-8 contrôlés ;
- vues `StringView` non possédées, comparaison exacte et recherche par sous-vue.

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
- `strings` : décodage par offset d'octet, `Option[Char]`, vues bornées et recherche.
- `Vec[T]` : collection possédée dynamique, croissance contrôlée, vues empruntées
  et accès sûrs avec `Option[T]`.
- `vectors` : helpers consommateurs génériques distribués avec la stdlib
  précompilée.

## Limites connues

- pas encore de dictionnaire ni d'ensemble dynamique ;
- les `StringView` restent locales et ne peuvent pas encore être retournées ;
- les références ne peuvent pas être retournées ni stockées globalement ;
- les durées de vie restent lexicales avec réduction à la dernière utilisation,
  sans inférence générale interprocédurale ;
- les fonctions locales capturantes ne sont pas encore de véritables fermetures ;
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

## Priorité 2 — API String et vues UTF-8 — terminée

Objectif : rendre `String` utilisable autrement que par concaténation et affichage.

Travail livré :

1. `lengthBytes` et `isEmpty` ;
2. validation et décodage d'un point de code UTF-8 ;
3. itération contrôlée sur les `Char` ;
4. type de vue non possédée pour les sous-chaînes ;
5. recherche et comparaison de vues ;
6. factorisation complète du formatage entre `String(value)` et `io.print...`.

`strings.charAtByte` rend l'unité explicite et retourne `Option[Char]`. L'itération
avance avec `nextByteOffset`, sans confondre octets et points de code.

## Priorité 3 — Collections dynamiques — terminée

Objectif : ajouter un premier conteneur possédé, `Vec[T]`.

Conception arrêtée dans `docs/VEC_DESIGN.md` : représentation ABI sur trois mots,
capacité exprimée en éléments, croissance géométrique et arrêt avec le code `105`
en cas d'échec d'allocation ou de dépassement de taille.

Étapes :

1. définir la représentation `{adresse, longueur, capacité}` — conception terminée ;
2. introduire allocation, croissance et libération déterministes — terminé ;
3. appliquer les règles de déplacement aux éléments non `Copy` — terminé ;
4. exposer des vues `Slice[T]` et `SliceMut[T]` sans copie — terminé ;
5. fournir `push`, `pop`, `get`, `set`, `reserve` et `clear` — terminé ;
6. tester les types primitifs, structures, `String`, `Box[T]` et vecteurs
   imbriqués — terminé ;
7. intégrer le module à la stdlib précompilée — terminé.

Le comportement en cas d'échec d'allocation est désormais défini avant l'ajout de
plusieurs conteneurs.

## Priorité 4 — Interfaces publiques complètes — en cours

Objectif : stabiliser `.zti` comme véritable format de distribution de bibliothèques.

Le contrat d'identité qualifiée, de disposition sérialisée et de chargement en
deux phases est défini dans `docs/PUBLIC_TYPES_DESIGN.md`.

Travail livré :

1. syntaxe `pub struct` et rejet des types privés qui fuient dans une interface ;
2. sérialisation versionnée des tailles, alignements, champs et paramètres
   génériques des structures ;
3. résolution qualifiée `module.Type` après chargement en deux phases ;
4. construction, annotation et passage par fonction de structures ordinaires et
   génériques avec uniquement le couple `.zti` + `.o` du producteur ;
5. syntaxe `pub enum`, validation des charges publiques et empreinte ABI des
   variantes ;
6. sérialisation des discriminants, charges, champs et paramètres génériques des
   enums dans le format `.zti` 6 ;
7. annotations, constructions et motifs `match` qualifiés pour les enums
   ordinaires et génériques distribuées sans source.

Travail restant :

1. remplacement des sources génériques incorporées par une AST versionnée ;
2. diagnostics plus détaillés pour toutes les incompatibilités ABI ;
3. commande explicite de construction d'une bibliothèque ;
4. installation dans un cache partagé indépendant d'un projet ;
5. déduplication robuste des instances génériques entre consommateurs.

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

Remplacer le texte source des fonctions génériques publiques incorporé aux
interfaces par une représentation versionnée, structurée et indépendante du
parseur de sources complet.

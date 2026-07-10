# Roadmap de Zeta

Ce document sert de point de reprise pour les prochaines sessions de développement
du langage Zeta et de son compilateur.

## État actuel

La chaîne de compilation est fonctionnelle :

```text
source.zeta
    -> lexer
    -> parser
    -> AST
    -> analyse sémantique et typage
    -> IR
    -> assembleur x86-64
    -> FASM
    -> exécutable ELF64
```

Le langage prend actuellement en charge :

- `val` pour les valeurs immuables ;
- `var` pour les variables réaffectables ;
- `def` pour les définitions paresseuses et les fonctions typées ;
- les blocs d'expressions avec portée locale ;
- les types `Int`, `Byte`, `Double` et `Bool` ;
- l'arithmétique `+`, `-`, `*`, `/` ;
- les comparaisons `==`, `!=`, `<`, `>`, `<=`, `>=` ;
- les opérateurs logiques `&&`, `||`, `!` avec court-circuit ;
- les expressions `if`, `else if`, `else` ;
- les instructions `while ... do` et les boucles imbriquées ;
- un point d'entrée obligatoire `def main () : Int` ;
- la transmission du résultat de `main` au code de sortie du processus.

Le backend produit des exécutables Linux x86-64 autonomes. Il gère notamment les
calculs flottants avec SSE2 et le stockage adapté à la taille de chaque type.

## Limites actuelles

- Les fonctions sont développées dans l'IR à chaque appel.
- Il n'existe pas encore de véritables appels de fonctions en assembleur.
- La récursion n'est pas possible.
- L'analyse sémantique et la génération IR sont encore fortement liées.
- Il n'existe ni entrée utilisateur, ni affichage, ni runtime Zeta.
- Les chaînes de caractères ne sont pas disponibles.
- Il n'existe aucune conversion explicite entre les types.
- `return`, `break` et `continue` ne sont pas disponibles.
- La compilation ne gère qu'un fichier source à la fois.
- L'IR ne possède pas encore de véritable pipeline d'optimisation.

## Phase 1 — Consolider l'architecture

Priorité recommandée avant d'ajouter beaucoup de syntaxe.

- Séparer l'analyse sémantique de la génération IR.
- Introduire une véritable table de symboles avec une pile de portées.
- Produire un AST typé après l'analyse sémantique.
- Attacher une position source complète à chaque nœud.
- Centraliser les règles de compatibilité et de conversion des types.
- Découper l'actuel générateur IR en composants plus petits.

Critères de validation :

- tous les tests existants continuent de passer ;
- aucune règle de typage n'est réalisée directement par le backend ;
- l'IR ne reçoit que des nœuds déjà validés et typés.

## Phase 2 — Conversions explicites

Syntaxe envisagée :

```zeta
val entier : Int = Int(octet)
val octet : Byte = Byte(entier)
val flottant : Double = Double(entier)
```

À définir :

- conversions autorisées entre `Int`, `Byte` et `Double` ;
- contrôles ou comportement en cas de dépassement ;
- conversion de `Double` vers `Int` ;
- éventuelles conversions impliquant `Bool`.

## Phase 3 — Runtime minimale et affichage

Objectif : observer les résultats autrement que par le code de sortie.

Syntaxe envisagée :

```zeta
print(42)
print(3.1415)
print(true)
```

Travail nécessaire :

- créer une petite runtime Zeta ;
- convertir `Int`, `Byte`, `Double` et `Bool` en texte ;
- utiliser les syscalls Linux d'écriture ;
- intégrer la runtime au processus d'assemblage et de liaison ;
- ajouter des tests qui vérifient la sortie standard.

## Phase 4 — Véritables fonctions

Remplacer le développement systématique des fonctions par :

- des fonctions IR distinctes ;
- une instruction IR `call` ;
- une instruction IR `return` ;
- des labels assembleur dédiés ;
- une convention d'appel ;
- le passage des paramètres ;
- une stack frame par appel ;
- la préservation correcte des registres.

Cette phase devra permettre :

- la récursion ;
- des binaires plus petits ;
- des appels multiples sans duplication du corps ;
- une séparation claire entre les fonctions.

## Phase 5 — Contrôle de flux supplémentaire

Ajouter `return` après la mise en place des véritables fonctions :

```zeta
def cherche (x : Int) : Int = {
    if (x < 0) {
        return 0
    }
    x
}
```

Compléter ensuite les boucles avec :

```zeta
break
continue
```

Une boucle `for` pourra être étudiée après stabilisation de ces instructions.

## Phase 6 — Type String

Syntaxe envisagée :

```zeta
val message : String = "Bonjour Zeta"
```

Travail nécessaire :

- lexer les chaînes et leurs séquences d'échappement ;
- choisir une représentation mémoire avec adresse et longueur ;
- générer les constantes dans une section de données ;
- ajouter l'affichage ;
- définir l'égalité et la concaténation.

## Phase 7 — Structures de données

Ordre recommandé :

1. tableaux de taille fixe ;
2. références ou pointeurs contrôlés ;
3. structures ;
4. énumérations ;
5. tableaux dynamiques.

Exemple envisagé :

```zeta
val nombres : [Int; 3] = [1, 2, 3]
```

## Phase 8 — Modules et compilation séparée

Syntaxe envisagée :

```zeta
import maths

def main () : Int = maths.abs(-5)
```

À prévoir :

- résolution de plusieurs fichiers source ;
- symboles publics et privés ;
- graphe de dépendances ;
- unités IR ou objets séparés ;
- liaison finale avec FASM.

## Phase 9 — Optimisations IR

- évaluation des constantes à la compilation ;
- propagation des constantes ;
- suppression du code mort ;
- suppression des définitions inutilisées ;
- simplification algébrique ;
- réutilisation des temporaires ;
- réduction de l'espace utilisé sur la stack ;
- inlining contrôlé des petites fonctions.

## Qualité continue

À maintenir pendant toutes les phases :

- ajouter des tests positifs et négatifs pour chaque fonctionnalité ;
- vérifier l'exécutable produit, pas seulement la compilation ;
- conserver des diagnostics avec ligne et colonne ;
- documenter les décisions de syntaxe et de sémantique ;
- préserver la compatibilité avec les programmes Zeta existants ;
- exécuter `git diff --check`, la construction CMake et tous les tests avant commit.

## Prochaine session recommandée

Commencer par la phase 1 : séparer clairement le pipeline en
`AST -> analyse sémantique -> AST typé -> IR`. Une fois cette base stabilisée,
enchaîner avec les véritables fonctions et une runtime minimale fournissant
`print`. Ces travaux débloqueront la récursion, `return`, les chaînes et des tests
de programmes beaucoup plus observables.

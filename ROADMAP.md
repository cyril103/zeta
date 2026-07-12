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
- les types `Int`, `Byte`, `Double`, `Bool`, `Char` et `String` ;
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

- Il n'existe ni entrée utilisateur, ni affichage, ni runtime Zeta.
- Les chaînes de caractères ne sont pas disponibles.
- Il n'existe aucune conversion explicite entre les types.
- `return`, `break` et `continue` ne sont pas disponibles.
- La compilation ne gère qu'un fichier source à la fois.
- L'IR ne possède pas encore de véritable pipeline d'optimisation.

## Phase 1 — Consolider l'architecture

Priorité recommandée avant d'ajouter beaucoup de syntaxe.

État : terminée. Le pipeline passe désormais par un analyseur sémantique et un
AST typé avant la génération IR ; la résolution utilise une pile de portées et
les règles de types sont centralisées.

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

État : terminée. Les conversions numériques explicites sont représentées dans
l'AST typé et l'IR, puis émises nativement par le backend x86-64.

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

État : terminée pour les fonctions de niveau global. Chacune devient une fonction
IR distincte avec `call`, paramètres, `return`, label assembleur et stack frame.
La récursion est prise en charge. Les fonctions locales capturant leur portée
restent développées sur place jusqu'à l'introduction de véritables fermetures.

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

État : terminée. `return` est disponible et optionnel ; `break` et `continue`
contrôlent la boucle englobante la plus proche.

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

## Phase 6 — Type String (en cours)

Le type `String` immuable est disponible. Il utilise UTF-8 et une représentation
de 16 octets `{adresse, longueur en octets}`.

Syntaxe envisagée :

```zeta
val message : String = "Bonjour Zeta"
```

Disponible :

- littéraux et séquences d'échappement avec validation Unicode stricte ;
- constantes dans la section de données ;
- variables globales et locales ;
- paramètres et retours de fonctions ;
- égalité et différence exactes sur les octets UTF-8.

Prochaines étapes :

- exposer les longueurs en octets et en points de code ;
- ajouter l'accès contrôlé à un `Char` ;
- introduire un runtime et une politique de propriété pour la concaténation ;
- ajouter l'affichage et les sous-chaînes.

## Phase 7 — Structures de données

Les tableaux de taille fixe sont disponibles : types récursifs `[T; N]`,
littéraux, copie par valeur, lecture et mutation indexées, tableaux imbriqués et
contrôles statiques/dynamiques des limites.

Les références empruntées sont également disponibles : `&T`, `&mut T`, prise
d'adresse, lecture et écriture par déréférencement, paramètres sur 64 bits et
indexation sans copie d'un tableau reçu par référence. L'analyse impose plusieurs
emprunts partagés ou un unique emprunt mutable, refuse les alias mutables dans un
appel et réduit les emprunts locaux après la dernière utilisation connue. Les
références capturées restent lexicales.

Limites actuelles des références :

- aucun retour ni stockage global de référence ;
- aucune variable référence réaffectable ;
- pas de slices, pointeurs bruts, `Box`, tas ou allocation dynamique ;
- pas d'inférence de durées de vie non lexicales.

Ordre recommandé :

1. tableaux de taille fixe — terminé ;
2. références empruntées `&T` et `&mut T` — disponibles localement et en paramètres ;
3. structures ;
4. énumérations ;
5. tableaux dynamiques.

Exemple envisagé :

```zeta
val nombres : [Int; 3] = [1, 2, 3]
```

## Phase 8 — Modules et compilation séparée (disponible)

Syntaxe envisagée :

```zeta
import maths

def main () : Int = maths.abs(-5)
```

Disponible :

- résolution récursive de plusieurs fichiers source ;
- symboles publics et privés et noms qualifiés ;
- interfaces de modules et graphe de dépendances sans cycles ;
- ordre topologique et IR fusionnée avec noms manglés ;
- objets ELF64 relogeables assemblés par FASM ;
- liaison finale avec `ld` ;
- cache invalidé par le source, l'interface et les dépendances.

Le module standard `io` valide désormais cette chaîne avec des fonctions natives
liées depuis un objet runtime : affichage de `String`, `Char` et `Int`, gestion des
écritures partielles et reprise après `EINTR`.

Une évolution future pourra déplacer davantage de code de l'unité IR fusionnée
vers les objets propres à chaque module, puis sérialiser les interfaces pour ne
plus reparcourir les sources inchangés.

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

Consolider d'abord les références et préparer les vues dynamiques :

1. autoriser la mutation `values[index] = valeur` lorsque
   `values : &mut [T; N]` — terminé ;
2. ajouter des tests de références vers `String`, `Char`, tableaux imbriqués et
   appels récursifs — terminé ;
3. réduire les emprunts lexicaux lorsque leur dernière utilisation est connue —
   terminé ;
4. concevoir `Slice[T]` et `SliceMut[T]` comme `{adresse, longueur}` sans
   allocation — terminé, voir `docs/SLICE_DESIGN.md` ;
5. permettre de créer une slice depuis un tableau fixe emprunté — terminé ;
6. passer et indexer les slices dans l'ABI avec les mêmes contrôles de limites ;
7. utiliser ensuite les slices pour les buffers d'E/S et les futures chaînes
   construites dynamiquement.

Après stabilisation des slices, choisir explicitement le premier modèle de
propriété dynamique (`Box[T]`, déplacement sans copie et destruction
déterministe) avant d'introduire un allocateur fondé sur `mmap`. En parallèle, la
compilation séparée pourra évoluer de l'IR fusionnée vers du code réellement
réparti dans les objets propres à chaque module.

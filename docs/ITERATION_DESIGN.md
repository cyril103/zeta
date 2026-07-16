# Protocole d'itération sans allocation

Ce document fixe le premier contrat d'itération de Zeta. L'objectif immédiat est
réduit : rendre les parcours de tableaux, `Slice[T]`, `SliceMut[T]` et `Vec[T]`
composables sans allocation, sans vtable et sans affaiblir les règles d'emprunt.
La syntaxe `for` viendra ensuite ; le premier livrable doit pouvoir être validé
avec des fonctions et méthodes explicites.

## Objectifs

- Remplacer progressivement les boucles `while` indexées répétitives de la stdlib.
- Parcourir les séquences contiguës sans allocation et sans objet de trait.
- Distinguer clairement lecture partagée et mutation exclusive.
- Réutiliser le dispatch statique déjà disponible pour les traits utilisateur.
- Garder `Vec[T]`, tableaux et slices comme fondations ; ne pas ajouter de builtin
  `Iterator` magique dans le compilateur.
- Reporter l'itération consommatrice et le parcours Unicode de `String` tant que
  le socle partagé/mutable n'est pas démontré.

## Non-objectifs de la première tranche

- Pas de closures ni fonctions de première classe.
- Pas de `map`, `filter`, `fold` avec callback utilisateur.
- Pas d'objets de traits, de vtables ni de dispatch dynamique.
- Pas d'allocation de state machine sur le tas.
- Pas de retour de références hors de la durée lexicale du parcours.
- Pas de protocole d'itération consommatrice pour les types possédés non `Copy`.
- Pas encore de syntaxe `for`; elle sera un abaissement vers ce protocole.

## Modèle retenu

Un parcours est représenté par un état copiable, généralement un indice `Int`, et
par une vue empruntée de la séquence. L'état ne possède pas les éléments et ne
prolonge pas leur durée de vie ; il n'est valide que tant que la vue source reste
valide.

Pour les séquences contiguës, l'état minimal est :

```zeta
struct IndexIterator {
    index: Int
}
```

La première tranche n'expose volontairement pas cette structure à l'utilisateur :
les helpers publics de `sequences` utilisent aujourd'hui un `Int` comme état
copiable (`iterate`, `iterateMut`, `hasNext`, `hasNextMut`, `position`,
`advance`). Cela évite d'élargir trop tôt l'ABI publique avec un type d'itérateur
spécifique tout en validant le modèle d'état séparé. Le compilateur peut
continuer à compiler ces helpers explicites vers des boucles indexées, puis la
syntaxe `for` pourra être abaissée vers la même forme :

```text
state = 0
while (state < view.length) do {
    element = view[state]
    state = state + 1
    body(element)
}
```

Le choix important est sémantique : l'état est séparé de la vue, donc il ne copie
ni `Vec[T]` ni éléments possédés. Les vues existantes (`Slice[T]` et
`SliceMut[T]`) restent la frontière d'emprunt.

## Contrats de lecture partagée

Un parcours partagé lit chaque élément sans mutation. Il est disponible pour :

- tableaux fixes `[T; N]` via une vue `Slice[T]` ;
- `Slice[T]` directement ;
- `SliceMut[T]` réempruntée en lecture lorsque le corps ne demande pas de mutation ;
- `Vec[T]` via `asSlice()` ;
- `&Vec[T]` via `asSlice()`.

Pour `T: Copy`, le parcours peut produire une valeur `T` copiée :

```zeta
pub def eachCopy[T: Copy](values: Slice[T]): Slice[T] = values
```

La fonction ci-dessus n'est qu'un marqueur conceptuel : la première implémentation
peut commencer par des helpers spécialisés dans `sequences`, tant que les tests
prouvent que le parcours partagé ne déplace pas les éléments.

Pour les types non `Copy`, le protocole devra produire une lecture par référence
`&T`. Cette capacité dépend encore de la possibilité de retourner ou conserver une
référence d'élément dans une durée strictement bornée par le corps du parcours.
Elle est donc reportée pour la syntaxe `for` initiale si nécessaire. La tranche 1
peut limiter les éléments lus à `T: Copy`, comme les algorithmes `sequences`
existants qui copient déjà les éléments.

## Contrats de mutation exclusive

Un parcours mutable exige une vue `SliceMut[T]` ou une source capable de produire
`asSliceMut()` :

- `SliceMut[T]` directement ;
- `Vec[T]` lié par `var` via `asSliceMut()` ;
- `&mut Vec[T]` via `asSliceMut()` ;
- tableau mutable lorsque l'abaissement vers `SliceMut[T]` est disponible.

Pendant tout le parcours mutable :

1. la source ne peut pas être déplacée ;
2. aucune autre lecture ou mutation concurrente de la même région n'est permise ;
3. la longueur de la séquence ne peut pas changer ;
4. les écritures `values[index] = ...` restent contrôlées par les bornes ;
5. les éléments non `Copy` ne peuvent pas être dupliqués par le parcours.

La mutation élément par élément est donc d'abord exprimée avec l'indice courant et
la vue `SliceMut[T]`, pas avec une référence mutable retournée hors du helper :

```zeta
pub def fill[T: Copy](values: SliceMut[T], replacement: T): Int = {
    var index = 0
    while (index < values.length) do {
        values[index] = replacement
        index = index + 1
    }
    values.length
}
```

La syntaxe `for (value in values.asSliceMut())` ne devra être acceptée que lorsque
le compilateur sait borner la durée de `value` à l'itération courante.

## Traits candidats

Les traits ci-dessous décrivent la direction, mais ils ne doivent pas tous être
livrés avant les tests du socle. Ils servent à nommer les contrats publics futurs.

```zeta
trait IterableCopy {
    def length(self: &Self): Int
}

trait MutableContiguous {
    def length(self: &Self): Int
}
```

Les traits utilisateur actuels ne disposent pas encore de types associés. Pour
éviter un mauvais design générique, la première tranche ne doit donc pas exposer
un trait `Iterator[Item]` artificiel avec trop de paramètres. Les helpers de
stdlib peuvent rester spécialisés sur `Slice[T]` et `SliceMut[T]` jusqu'à
l'arrivée des types associés ou d'une autre représentation explicite du type
d'élément.

## Abaissement futur de `for`

La forme utilisateur cible :

```zeta
for (value in values.asSlice()) do {
    io.printlnInt(value)
}
```

s'abaisse conceptuellement en :

```zeta
{
    val __view = values.asSlice()
    var __index = 0
    while (__index < __view.length) do {
        val value = __view[__index]
        __index = __index + 1
        io.printlnInt(value)
    }
}
```

Pour `SliceMut[T]`, l'abaissement doit conserver l'exclusivité de `__view` jusqu'à
la fin de la boucle. La variable d'élément ne doit jamais devenir une copie
implicite d'une valeur non `Copy`.

La première version de `for` peut donc être limitée à :

- `Slice[T]` avec `T: Copy` ;
- `SliceMut[T]` avec mutation par indice ou élément `T: Copy` ;
- `Vec[T]` seulement après conversion explicite ou implicite vers slice ;
- tableaux après conversion vers slice.

## Interaction avec `Vec[T]`

`Vec[T]` ne devient pas lui-même un itérateur. Il fournit des vues :

- `values.asSlice()` pour l'itération partagée ;
- `values.asSliceMut()` pour l'itération mutable.

Cela préserve les invariants existants : un parcours ne peut pas appeler `push`,
`pop`, `reserve`, `clear` ou déplacer le vecteur tant que la vue est vivante. La
longueur parcourue reste donc stable.

## Tests attendus avant la syntaxe `for`

La validation du protocole doit précéder le sucre syntaxique. Les tests doivent
couvrir :

1. parcours partagé de `[Int; N]`, `Slice[Int]` et `Vec[Int]` avec même résultat ;
2. parcours mutable de `SliceMut[Int]` et `Vec[Int]` avec modification en place ;
3. rejet d'une mutation de `Vec` pendant qu'une slice issue de ce `Vec` est encore
   utilisée ;
4. rejet du déplacement d'un `Vec` pendant un parcours actif ;
5. absence de copie implicite pour `Vec[Box[Int]]` ou autre élément possédé ;
6. consommation d'une stdlib précompilée sans sources lorsque les helpers publics
   sont exposés ;
7. diagnostics stables pour les erreurs d'emprunt ou de capacité non supportée.

## Découpage committable

1. Documenter ce contrat et aligner la roadmap.
2. Ajouter des tests de helpers `sequences` qui expriment les parcours partagés et
   mutables sans nouvelle syntaxe. Cette première tranche est couverte par
   `tests/sequences_iteration.zeta` pour `[Int; N]`, `Slice[Int]`,
   `SliceMut[Int]` et `Vec[Int]`.
3. Factoriser les boucles répétitives de `sequences` quand cela n'élargit pas la
   surface publique de manière prématurée.
4. Ajouter les diagnostics nécessaires pour les emprunts actifs de vues issues de
   `Vec` ou de tableaux.
5. Concevoir puis implémenter la syntaxe `for` comme abaissement testé vers les
   mêmes primitives.

## Décisions reportées

- Le nom final des traits publics d'itération.
- Les types associés ou une alternative pour exprimer `Item`.
- L'itération consommatrice des valeurs possédées.
- Les références d'éléments retournées par un itérateur général.
- Le parcours Unicode haut niveau de `String`/`StringView`.
- Les adaptateurs `map`, `filter`, `fold`, `enumerate` et comparateurs
  personnalisés.

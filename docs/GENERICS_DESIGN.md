# Généricité de Zeta

Zeta utilise une généricité statique destinée à être monomorphisée :

```zeta
def identity[T] (value : T) : T = value
```

Plan d'implémentation :

1. paramètres de type dans les fonctions et substitution dans les signatures ;
2. instanciation explicite `identity[Int](42)` — syntaxe et validation disponibles ;
3. monomorphisation avec noms manglés et cache des instances — terminé ;
4. inférence récursive des types depuis les arguments — terminé ;
5. contraintes intégrées `Copy`, `Numeric`, `Ordered` et `Equatable`, y compris
   les listes multiples canoniques avec `+` — terminé ;
6. structures génériques et fonctions génériques exportées — disponibles ;
7. traits définissables par l'utilisateur.

Les fonctions et structures génériques sont monomorphisées à la demande. Les
fonctions génériques peuvent être exportées par un module, comme celles de
`stdlib/collections.zeta`.

## Contraintes multiples

Un paramètre accepte plusieurs capacités :

```zeta
def replaceAll[T: Equatable + Copy](values: SliceMut[T], old: T, next: T): Int
```

Le parseur refuse un nom répété puis trie les noms lexicalement. L'AST et le
format `.zti` stockent ainsi une chaîne canonique, par exemple
`Copy+Equatable`, indépendamment de l'ordre source. L'analyse sémantique vérifie
toutes les capacités sur un type concret. Pour un appel entre deux fonctions
génériques, l'ensemble disponible chez l'appelant doit contenir l'ensemble exigé
par l'appelé.

Les contraintes intégrées actuelles décrivent seulement des capacités positives
et sont toutes cumulables ; aucune combinaison n'est donc incompatible. Cette
règle devra être revisitée si une future contrainte exprime une exclusion.

Le contrat canonique participe à l'empreinte d'interface et à l'identité des
instances monomorphisées. Les traits marqueurs utilisateur portent les versions
courantes à l'interface `12`, aux tokens génériques `4` et au cache de modules
`26`.

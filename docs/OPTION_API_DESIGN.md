# API commune d'`Option[T]`

## Décision

Les opérations communes à `Option[T]` vivent dans le module standard `option` :

```zeta
import option

val value = option.unwrapOr(possible, fallback)
val absent = option.isNone(possible)
```

Une méthode inhérente sur l'enum serait plus concise, mais les méthodes
utilisateur sur les enums ne sont pas encore prises en charge. Le module garde
l'API unique et évite de coupler une opération d'`Option[T]` à la provenance de
la valeur (`collections`, `strings` ou `sequences`).

## Contrat

`isNone[T: Copy]` et `unwrapOr[T: Copy]` consomment leur `Option[T]`. La contrainte
`Copy` rend cette consommation explicite et correspond au contrat des anciennes
fonctions. Un appel avec `Option[Box[T]]` est rejeté ; l'assouplissement de ce
contrat demandera une API par emprunt ou des méthodes d'enum.

Les anciennes exportations de `collections` et `strings` sont supprimées. Il n'y
a pas d'alias de compatibilité afin de conserver une seule définition publique.

## Compilation séparée

Le module produit sa propre paire `option.zti` + `option.o`. Le test de stdlib
précompilée retire tous les fichiers `.zeta`, puis compile et exécute un programme
qui instancie les deux fonctions sur plusieurs types `Copy`. Le cache de modules
est révisé à `25` pour empêcher la réutilisation d'objets construits avec les
anciennes exportations.

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
5. contraintes intégrées `Copy`, `Numeric`, `Ordered` et `Equatable` — terminé ;
6. structures génériques et fonctions génériques exportées — disponibles ;
7. traits définissables par l'utilisateur.

Les fonctions et structures génériques sont monomorphisées à la demande. Les
fonctions génériques peuvent être exportées par un module, comme celles de
`stdlib/collections.zeta`.

# Généricité de Zeta

Zeta utilise une généricité statique destinée à être monomorphisée :

```zeta
def identity[T] (value : T) : T = value
```

Plan d'implémentation :

1. paramètres de type dans les fonctions et substitution dans les signatures ;
2. instanciation explicite `identity[Int](42)` — syntaxe et validation disponibles ;
3. monomorphisation avec noms manglés et cache des instances ;
4. inférence des types depuis les arguments ;
5. contraintes intégrées `Copy`, `Numeric`, `Ordered` et `Equatable` ;
6. structures génériques puis traits définissables par l'utilisateur.

La première étape est disponible. Les fonctions génériques sont analysées mais ne
produisent pas encore d'unité IR avant leur instanciation explicite.

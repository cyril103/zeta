# Zeta

Zeta est un langage compilé à valeurs immuables. Sa syntaxe actuelle est :

```text
val identifiant : Int = expression
```

`Int` est un entier signé sur 32 bits. Un identifiant commence par une lettre ou
`_`, puis contient des lettres, chiffres ou `_`. Les mots `val` et `Int` sont
réservés. Une valeur déjà déclarée peut être lue dans une expression, mais ne peut
être ni redéclarée ni affectée avec `=`.

Les expressions acceptent les littéraux, les valeurs précédentes, les parenthèses,
les signes unaires `+`/`-` et les opérateurs `+`, `-`, `*`, `/` avec leur priorité
habituelle. Les déclarations sont séparées par un retour à la ligne ou `;`.

## Construire et utiliser

```sh
cmake -S . -B build
cmake --build build
./build/zeta examples/basic.zeta -o build/basic
./build/basic
```

La compilation produit `build/basic.ir`, `build/basic.asm` et l'exécutable
`build/basic`. La cible actuelle est Linux x86-64 et nécessite `fasm` dans le
`PATH`.

# Zeta

Zeta est un langage compilé avec des valeurs immuables, des variables mutables et
des définitions paresseuses. Ses syntaxes de déclaration actuelles sont :

```text
val identifiant : Int = expression
var identifiant : Int = expression
def identifiant : Int = expression
```

`Int` est un entier signé sur 32 bits. Un identifiant commence par une lettre ou
`_`, puis contient des lettres, chiffres ou `_`. Les mots `val` et `Int` sont
réservés. Un identifiant ne peut jamais être redéclaré. Une déclaration `val` est
immuable, tandis qu'une déclaration `var` peut être réaffectée sans créer un
nouveau slot sur la stack :

```text
var compteur : Int = 1
compteur = compteur + 1
```

Une déclaration `def` est immuable et paresseuse : elle ne réserve pas de slot et
son expression ne génère du calcul qu'à chaque utilisation de son identifiant. Le
résultat n'est pas mis en cache. Les `var` référencés sont donc lus avec leur valeur
au moment de l'utilisation :

```text
var base : Int = 2
def double : Int = base * 2
base = 5
val resultat : Int = double // vaut 10
```

Comme pour les autres déclarations, les identifiants utilisés dans une `def`
doivent avoir été déclarés auparavant.

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

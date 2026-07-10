# Zeta

Zeta est un langage compilé avec des valeurs immuables, des variables mutables et
des définitions paresseuses. Ses syntaxes de déclaration actuelles sont :

```text
val identifiant : Int = expression
var identifiant : Int = expression
def identifiant : Int = expression
def fonction(parametre : Int, autre : Int) : Int = expression
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

Une `def` peut également déclarer une fonction avec des paramètres typés. Elle
s'utilise alors avec des parenthèses :

```text
def add (a : Int, b : Int) : Int = a + b
val resultat : Int = add(2, 3)
```

Le corps d'une fonction n'est développé dans l'IR qu'à chaque appel. Chaque
argument est évalué une fois, puis lié au paramètre correspondant. Les fonctions
peuvent appeler des fonctions déclarées auparavant et peuvent ne prendre aucun
paramètre avec `def zero () : Int = 0`.

## Expressions sur plusieurs lignes

Une expression sur plusieurs lignes doit être placée entre accolades. Le bloc
peut contenir des déclarations et des affectations, puis doit se terminer par une
expression. Cette dernière fournit la valeur du bloc :

```text
def add (a : Int, b : Int) : Int = {
    val res : Int = a + b
    res
}
```

Les identifiants déclarés dans un bloc sont locaux à ce bloc. Les `val` et `def`
locales restent immuables, tandis que les `var` locales peuvent être réaffectées.
Les instructions sont séparées par un retour à la ligne ou `;`.

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

## Point d'entrée

Tout programme Zeta doit déclarer exactement ce point d'entrée :

```text
def main () : Int = {
    0
}
```

`main` ne prend aucun paramètre et retourne un `Int`. Sa valeur devient le code de
sortie du processus : `0` indique une terminaison normale, tandis qu'une autre
valeur signale une terminaison non normale. Un programme sans `main`, ou avec une
signature différente, est refusé par le compilateur.

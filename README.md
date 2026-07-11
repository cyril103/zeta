# Zeta

Zeta est un langage compilé avec des valeurs immuables, des variables mutables et
des définitions paresseuses. Ses syntaxes de déclaration actuelles sont :

```text
val identifiant : Int = expression
var identifiant : Int = expression
def identifiant : Int = expression
def fonction(parametre : Int, autre : Int) : Int = expression
```

`Int` est un entier signé sur 32 bits. `Byte` est un entier non signé sur 8 bits,
compris entre `0` et `255` :

```text
val x : Byte = 244
```

Les opérations entre `Byte` restent des opérations 8 bits et débordent modulo 256.
`Double` est un nombre à virgule flottante signé au format IEEE-754 double précision
sur 64 bits. Il accepte les écritures décimales et scientifiques avec exposant
positif ou négatif :

```text
val pi : Double = 31415e-4
val aussiPi : Double = 3.1415
val grand : Double = 1e+12
val petit : Double = 2.5e-3
```

Il n'existe pas de conversion implicite entre `Int`, `Byte` et `Double`, mais les
conversions explicites utilisent le nom du type comme une fonction :

```text
val entier : Int = Int(3.9)       // 3, troncature vers zéro
val octet : Byte = Byte(300)      // 44, réduction modulo 256
val flottant : Double = Double(entier)
```

Les six conversions entre types numériques sont disponibles, ainsi que les
conversions identité. `Int` vers `Double` conserve la valeur lorsque celle-ci est
représentable exactement. `Double` vers `Int` tronque vers zéro ; une valeur non
finie ou hors de l'intervalle Int produit `-2147483648`. Une conversion vers
`Byte` conserve ensuite les 8 bits de poids faible. Toute conversion depuis ou
vers `Bool` est interdite. Un littéral entier reste accepté directement dans une
expression `Double`, ou comme `Byte` lorsqu'il tient dans l'intervalle attendu.

`Bool` représente une valeur logique et n'accepte que les deux littéraux `true` et
`false` :

```text
val actif : Bool = true
var termine : Bool = false
termine = actif
```

Un `Bool` occupe un octet sur la stack. Il n'existe aucune conversion implicite
avec les types numériques. Les opérateurs disponibles sont :

- `==` et `!=` pour l'égalité et la différence sur tous les types ;
- `<`, `>`, `<=` et `>=` pour l'ordre sur `Int`, `Byte` et `Double` ;
- `&&` et `||` pour le et/ou logique avec court-circuit ;
- `!` pour la négation logique.

Les deux opérandes d'une comparaison doivent avoir le même type, à l'exception
des littéraux compatibles avec le contexte. Les opérateurs arithmétiques restent
interdits sur `Bool`. Les priorités, de la plus faible à la plus forte, sont `||`,
`&&`, égalité, comparaison, addition, multiplication et opérateurs unaires.

## Expressions conditionnelles

`if`, `else if` et `else` forment une expression et produisent donc une valeur :

```text
def abs (x : Int) : Int = if (x < 0) -x else x

def signe (x : Int) : Int = if (x < 0) {
    -1
} else if (x > 0) {
    1
} else {
    0
}
```

Le prédicat doit être un `Bool`, `else` est obligatoire et toutes les branches
doivent produire le type attendu par le contexte. Une seule branche est exécutée.
Chaque branche peut être une expression simple, un autre `if` ou une expression-bloc.

## Boucles

`while` et `do` définissent une instruction de boucle :

```text
var i : Int = 0
while (i < 10) do {
    i = i + 1
}
```

Le prédicat doit être un `Bool` et il est réévalué avant chaque itération. Le corps
peut contenir déclarations, affectations, appels utilisés comme instructions et
boucles imbriquées. Il possède sa propre portée : ses déclarations locales ne sont
plus visibles après `}`. Contrairement à `if`, `while` n'est pas une expression et
ne retourne aucune valeur.

Un identifiant commence par une lettre ou `_`, puis contient des lettres, chiffres
ou `_`. Les mots `val`, `var`, `def`, `if`, `else`, `while`, `do`, `Int`, `Byte`, `Double`,
`Bool`, `true` et `false` sont réservés. Un identifiant
ne peut jamais être redéclaré. Une déclaration `val` est
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

Chaque fonction possède une unité IR et un label assembleur propres. Un appel
évalue chaque argument une fois, les transmet sur la pile et crée une nouvelle
stack frame ; sa valeur est renvoyée dans un registre. Les fonctions peuvent
s'appeler récursivement, appeler des fonctions déclarées auparavant et ne prendre
aucun paramètre avec `def zero () : Int = 0`.

Cette convention concerne les fonctions déclarées au niveau global. Une fonction
locale peut capturer les variables de son bloc ; elle reste pour l'instant
développée au point d'appel, en attendant une représentation dédiée des
fermetures.

Un auto-appel placé directement en position de retour est optimisé en boucle,
y compris lorsqu'il se trouve dans une branche terminale de `if`. Le compilateur
réécrit alors les paramètres de la frame courante et saute au début du corps : la
pile d'appels ne grandit pas. Les appels non terminaux, par exemple
`n * factorielle(n - 1)`, conservent une récursion classique.

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

## Architecture du compilateur

Le pipeline interne sépare explicitement les responsabilités :

```text
tokens -> AST brut -> analyse sémantique -> AST typé -> IR -> x86-64
```

L'analyse sémantique résout les identifiants avec une pile de portées, contrôle
les déclarations, les affectations et les appels, puis annote chaque expression
avec son type. Les règles communes de compatibilité sont centralisées dans
`type_rules.hpp`. Le générateur IR n'accepte qu'un `TypedProgram` délivré après
la réussite de cette analyse ; il ne réalise donc aucun contrôle de typage.

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

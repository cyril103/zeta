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
- `<`, `>`, `<=` et `>=` pour l'ordre sur `Int`, `Byte`, `Double` et `Char` ;
- `&&` et `||` pour le et/ou logique avec court-circuit ;
- `!` pour la négation logique.

Les deux opérandes d'une comparaison doivent avoir le même type, à l'exception
des littéraux compatibles avec le contexte. Les opérateurs arithmétiques restent
interdits sur `Bool`. Les priorités, de la plus faible à la plus forte, sont `||`,
`&&`, égalité, comparaison, addition, multiplication et opérateurs unaires.

`Char` représente un point de code Unicode valide sur 32 bits. Il accepte un
caractère UTF-8 ou un échappement Unicode :

```text
val lettre : Char = 'é'
val emoji : Char = '🚀'
val aussiEmoji : Char = '\u{1F680}'
```

Les substituts `U+D800..U+DFFF` ne sont pas des valeurs `Char`. L'égalité et les
comparaisons d'ordre sont disponibles, mais pas l'arithmétique. `Int(char)` donne
le point de code et `Char(entier)` effectue la conversion inverse. Une valeur
entière invalide est normalisée vers le caractère de remplacement `U+FFFD`.

## Chaînes UTF-8

`String` représente une chaîne immuable encodée en UTF-8. Une valeur occupe
16 octets et contient deux champs machine : l'adresse du premier octet et la
longueur en octets. La longueur ne compte donc ni les points de code Unicode ni
les caractères visibles. Par exemple, `"hé🚀"` contient trois points de code mais
sept octets.

```text
val vide : String = ""
val message : String = "hé🚀"
val equivalent : String = "h\u{E9}\u{1F680}"
val ligne : String = "première\nseconde"
```

Les échappements disponibles sont `\n`, `\r`, `\t`, `\0`, `\\`, `\"` et
`\u{...}`. Le compilateur valide strictement l'UTF-8 et les points de code ; les
substituts `U+D800..U+DFFF` sont refusés. Les littéraux sont stockés dans la
section de données de l'exécutable et restent valides pendant toute son
exécution.

Les chaînes peuvent être globales ou locales, affectées à une `var`, passées à
une fonction et retournées par celle-ci. `==` et `!=` comparent d'abord la
longueur, puis chaque octet. Cette égalité est exacte : aucune normalisation
Unicode n'est effectuée, donc deux suites de points de code visuellement
identiques peuvent rester différentes.

`String` est pour l'instant non possédée : elle référence des données immuables
et ne réalise aucune allocation dynamique. La concaténation, les sous-chaînes,
la longueur publique et l'accès à un `Char` seront ajoutés avec le runtime de
chaînes. En conséquence, l'arithmétique et les comparaisons d'ordre sont encore
interdites sur `String`.

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
`Bool`, `Char`, `String`, `Slice`, `SliceMut`, `true` et `false` sont réservés. Un identifiant
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

Le retour d'une fonction peut être écrit explicitement avec `return`, notamment
pour quitter une branche avant la fin :

```text
def positif (x : Int) : Int = {
    if (x < 0) { return 0 } else x
}
```

`return` reste optionnel : sans lui, la dernière expression du corps fournit la
valeur comme auparavant. La valeur retournée doit respecter le type déclaré.

Les boucles acceptent `break` pour quitter immédiatement la boucle courante et
`continue` pour reprendre à l'évaluation de son prédicat. Dans des boucles
imbriquées, ces instructions ciblent toujours la boucle la plus proche.

## Modules et compilation séparée

Chaque fichier `.zeta` constitue un module portant le nom du fichier. Les modules
importés sont recherchés dans le même dossier que le fichier racine :

```text
// maths.zeta
pub def abs (x : Int) : Int = if (x < 0) -x else x

// main.zeta
import maths

def main () : Int = maths.abs(-5)
```

`pub` est autorisé uniquement sur une déclaration globale. Un symbole sans `pub`
reste privé, et un import ne réexporte pas les symboles qu'il importe. Le
compilateur charge récursivement les fichiers, construit leurs interfaces, rejette
les cycles avec leur chemin et analyse les modules dans l'ordre topologique. Seul
le fichier donné sur la ligne de commande doit fournir `def main () : Int`.

Les noms de liaison sont préfixés par leur module afin que deux modules puissent
déclarer le même identifiant. La compilation produit une IR fusionnée, des objets
ELF64 relogeables assemblés par FASM, puis utilise `ld` pour créer l'exécutable
statique. Les artefacts de module se trouvent dans `<sortie>.modules/`.

Le dossier `<sortie>.cache/` conserve les objets et leurs empreintes. Une empreinte
dépend du source, de l'interface publique, des interfaces importées et de la
version du format de cache. Un objet inchangé est réutilisé automatiquement.

## Entrées-sorties standard

Le module standard `io` fournit les premières fonctions natives du runtime :

```text
import io

def main () : Int = {
    val greeting : Int = io.println("Bonjour Zeta 🚀")
    val number : Int = io.printlnInt(-42)
    val character : Int = io.printChar('é')
    0
}
```

L'API disponible est :

- `io.print(String)` et `io.println(String)` ;
- `io.printChar(Char)` ;
- `io.printInt(Int)` et `io.printlnInt(Int)` ;
- `io.printByte(Byte)` et `io.printlnByte(Byte)` ;
- `io.printBool(Bool)` et `io.printlnBool(Bool)` ;
- `io.printDouble(Double)` et `io.printlnDouble(Double)` ;
- `io.printBytes(Slice[Byte])` et `io.printlnBytes(Slice[Byte])`.

Chaque fonction retourne le nombre d'octets UTF-8 écrits. Une erreur de l'appel
système Linux `write` est retournée sous forme de valeur négative. Les écritures
partielles sont poursuivies et une interruption `EINTR` est automatiquement
relancée. Aucune terminaison NUL, allocation ou copie de `String` n'est requise.

Un `Double` est affiché avec environ sept chiffres significatifs. La notation
fixe est utilisée pour les exposants décimaux de `-4` à `6`, et la notation
scientifique au-delà. Les valeurs spéciales sont écrites `inf`, `-inf` et `nan`.

`printBytes` écrit directement les octets vus par la slice, sans allocation ni
copie intermédiaire et sans exiger qu'ils forment du texte UTF-8 valide.

Une déclaration `native def` décrit une fonction fournie par un objet runtime et
ne possède pas de corps Zeta. Elle est réservée aux déclarations globales ; le
compilateur exporte sa signature, génère une référence ELF externe, assemble
l'objet correspondant depuis `runtime/`, le met en cache et le transmet à `ld`.

## Tableaux de taille fixe

Un tableau fixe porte son type d'élément et sa longueur dans son type :

```text
val nombres : [Int; 3] = [10, 20, 30]
var caractères : [Char; 2] = ['A', 'é']
val matrice : [[Int; 2]; 2] = [[1, 2], [3, 4]]
```

La longueur doit être strictement positive et le littéral doit fournir exactement
le nombre d'éléments annoncé. Chaque élément est vérifié récursivement. Les
tableaux peuvent contenir tous les types actuels, notamment `Char`, `String` et
d'autres tableaux.

Un tableau possède ses données directement. Sa taille est
`taille(element) × longueur` et une affectation ou initialisation depuis un autre
tableau copie tout le bloc :

```text
var copie : [Int; 3] = nombres
copie[0] = 99 // nombres[0] reste égal à 10
```

La mutation d'un élément exige une variable déclarée avec `var`. Une `val` et ses
éléments restent immuables. Les lectures et écritures imbriquées sont disponibles :

```text
var matrice : [[Int; 2]; 2] = [[1, 2], [3, 4]]
matrice[1][0] = 30
val résultat : Int = matrice[1][0]
```

Un index constant hors limites est rejeté à la compilation. Un index calculé est
contrôlé à l'exécution ; une valeur négative ou supérieure ou égale à la longueur
termine le programme avec le code `101` avant tout accès mémoire.

L'égalité, l'ordre et l'arithmétique sur des tableaux entiers ne sont pas encore
définis. Les paramètres et retours de type tableau sont également différés jusqu'à
l'introduction d'une convention d'appel pour les blocs par valeur (`sret`).

## Références empruntées

Une référence sûre désigne une valeur existante sans la posséder ni l'allouer :

```text
var valeur : Int = 41
val lecture : &Int = &valeur
val écriture : &mut Int = &mut valeur
```

`&T` autorise la lecture avec `*référence`. `&mut T` autorise également
`*référence = valeur`. Une référence est toujours non nulle, occupe 8 octets sur
x86-64 et ne réalise aucune allocation dynamique.

Les emprunts locaux se terminent après la dernière utilisation connue de leur
référence dans le bloc. Une référence inutilisée libère donc immédiatement son
emprunt. Les références capturées par une fonction locale restent conservées
jusqu'à la fin de la portée, faute de fermetures explicites. Les règles sont :

- plusieurs emprunts partagés `&T` peuvent coexister ;
- un emprunt `&mut T` doit être unique et exclut toute lecture ou mutation directe ;
- une valeur empruntée ne peut pas être réaffectée avant la dernière utilisation
  de la référence ;
- seuls les identifiants de données peuvent être empruntés ;
- `&mut` exige une variable déclarée avec `var` ;
- une référence partagée peut être copiée et chaque copie prolonge l'emprunt ;
- une référence mutable ne peut pas être copiée ;
- deux arguments incompatibles ne peuvent pas emprunter la même valeur dans un appel.

Les références peuvent être passées aux fonctions et évitent notamment la copie
des tableaux fixes :

```text
def somme (values : &[Int; 3]) : Int = values[0] + values[1] + values[2]

def remplace (values : &mut [Int; 3], index : Int, value : Int) : Int = {
    values[index] = value
    value
}
```

Une référence `&mut [T; N]` permet la mutation indexée, y compris sur les tableaux
imbriqués. Les mêmes contrôles de limites statiques et dynamiques que pour un
tableau direct sont appliqués. Une référence partagée `&[T; N]` reste en lecture
seule.

Le stockage global, les variables référence mutables et les retours de références
sont encore interdits. Les slices, `Box[T]`, les pointeurs bruts, l'arithmétique
d'adresse et l'allocation sur le tas ne font pas partie de cette étape.

## Types slice

`Slice[T]` décrit une vue partagée et `SliceMut[T]` une vue mutable sur des
éléments contigus de type `T`. Les deux types sont reconnus dans les signatures et
occupent 16 octets conceptuels `{adresse, longueur en éléments}` sans allocation.

```text
native def inspecte (values : Slice[Int]) : Int
native def modifie (values : SliceMut[Byte]) : Int
```

Une slice locale se crée explicitement depuis un tableau fixe emprunté :

```text
val lecture : Slice[Int] = Slice(&tableau)
val écriture : SliceMut[Int] = SliceMut(&mut tableauMutable)
```

Cette opération ne copie aucun élément. La slice conserve l'emprunt du tableau
jusqu'à sa dernière utilisation connue. Une `Slice[T]` peut être copiée et chaque
copie prolonge cet emprunt ; une `SliceMut[T]` reste exclusive et non copiable. Le
stockage global et le retour de slices restent interdits.

Les slices peuvent être passées aux fonctions et indexées. Une `Slice[T]` permet
la lecture, tandis qu'une `SliceMut[T]` permet également l'écriture :

```text
def premier (values : Slice[Int]) : Int = values[0]

def remplace (values : SliceMut[Int], index : Int, value : Int) : Int = {
    values[index] = value
    values[index]
}
```

Chaque accès vérifie dynamiquement `0 <= index < longueur` et termine avec le code
`101` en cas d'échec. Les slices d'éléments tableaux acceptent aussi l'indexation
imbriquée. Les invariants sont détaillés dans `docs/SLICE_DESIGN.md`.

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

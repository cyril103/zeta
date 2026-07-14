# Vérificateur structurel d'IR

## But et frontière

`IrVerifier` valide un `IrProgram` après sa génération et avant toute lecture par
`IrGenerator::print`, `FasmCodeGenerator::generate` ou
`FasmCodeGenerator::generateObject`. Son rôle est de transformer une incohérence
interne du compilateur en diagnostic déterministe `IRV`, au lieu de laisser le
printer indexer hors limites ou le backend calculer une mauvaise adresse de pile.

Le vérificateur ne refait pas l'analyse sémantique du programme source. Il vérifie
le contrat concret que l'IR promet au backend : tables cohérentes, régions de
fonction bien formées, valeurs disponibles, slots accessibles, types concrets,
opérandes compatibles et flot de contrôle fermé.

Une erreur `IRV` est une erreur interne du compilateur. Elle n'a pas de position
source fiable et prend la forme :

```text
[IRV023] instruction 41, fonction 'example__sum' : valeur $17 utilisée avant définition
```

Le message contient toujours le code, l'index d'instruction, la région (`<init>`
ou le nom de fonction), puis le contexte propre à l'erreur. Le vérificateur
s'arrête à la première erreur afin que le résultat reste stable.

## Modèle vérifié

### Tables du programme

Les invariants globaux sont :

- `valueCount == valueTypes.size()` ;
- chaque `ValueId` lu ou produit est strictement inférieur à `valueCount` ;
- chaque `SlotId` est strictement inférieur à `slots.size()` ;
- tous les types de `valueTypes`, `slots` et instructions sont concrets et bien
  formés ; aucun `TypeParameter` ne subsiste, y compris sous un tableau, une
  référence, une slice, une box, un vec, une structure ou un enum ;
- un type composé possède son pointeur de définition ou d'élément et une
  disposition cohérente avec ses champs et variantes ;
- le type déclaré d'une valeur produite est égal à `valueTypes[output]` ;
- `exitValue`, lorsqu'un `IrExit` est présent, désigne sa valeur `Int` et concorde
  avec l'opérande de cet `IrExit`.

Les types sont comparés par `ValueType::operator==`. Une validation récursive de
leur forme précède toute comparaison afin de ne jamais déréférencer un pointeur
nul dans un diagnostic.

### Régions

Les instructions précédant le premier `IrFunctionStart` appartiennent à la région
`<init>`. Chaque `IrFunctionStart` ouvre une nouvelle région qui se termine au
prochain `IrFunctionStart` ou à la fin du programme. Deux fonctions ne peuvent
pas porter le même nom. Un nom vide, une identité générique vide lorsque
`linkOnce` vaut vrai, ou la présence de `IrParameter` hors du préambule immédiat
d'une fonction sont invalides.

Les `IrParameter` d'une fonction forment une suite contiguë, ordonnée par `index`
à partir de zéro. Leurs intervalles de pile commencent à 16 et ne se recouvrent
pas. Le pas attendu est celui de la convention d'appel actuelle : agrégat arrondi
à huit octets, paire sur seize octets, autre valeur sur huit octets.

Les valeurs sont propres à leur région même si leurs identifiants sont alloués
globalement dans `IrProgram`. Une définition ou une lecture croisant une frontière
de fonction est interdite. Les slots globaux sont accessibles partout. Faute de
métadonnée dédiée dans `IrSlot`, un slot non global est rattaché à la première
région qui le référence ; toute référence ultérieure depuis une autre région est
rejetée. Une évolution de l'IR pourra rendre ce propriétaire explicite.

### Définitions de valeurs

L'IR actuelle n'est pas strictement SSA. La génération des `if`, des `match` et
des opérateurs à court-circuit réserve une sortie puis utilise plusieurs
`IrCopy` comme valeur de fusion. Pour `if` et `match`, ces écritures appartiennent
à des branches mutuellement exclusives. Un court-circuit initialise d'abord la
sortie avec l'opérande gauche, puis peut la remplacer par l'opérande droit sur le
chemin de chute.

Le contrat « définition unique » signifie donc :

- hors sorties de fusion écrites par `IrCopy`, une valeur a exactement un
  producteur dans sa région ;
- une sortie de fusion peut avoir plusieurs `IrCopy`; une réécriture séquentielle
  est admise uniquement pour ce support de fusion explicite ;
- toute lecture possède exactement une définition atteignable sur tous ses
  chemins entrants ;
- une valeur seulement définie sur certains prédécesseurs est une utilisation
  avant définition ;
- une seconde définition hors `IrCopy`, ou un mélange de producteurs `IrCopy` et
  non-`IrCopy`, est une redéfinition.

Une analyse de données avant, par bloc de base, propage donc des ensembles
`définitivement définies`. La fusion prend leur intersection. L'inventaire des
producteurs réserve le cas multiple à `IrCopy`. À terme, une instruction `IrPhi`
explicite permettra de retrouver une vraie propriété SSA et de supprimer cette
exception.

L'exclusivité des producteurs `IrCopy` est contrôlée sur les chemins avant du
graphe structuré. Une arête de retour de boucle ouvre une nouvelle itération et
peut donc réexécuter légitimement une même fusion.

### Flot de contrôle

Un bloc de base commence à l'entrée d'une région, à un `IrLabel`, ou après une
instruction terminale. `IrJump`, `IrReturn`, `IrTailCall` et `IrExit` terminent un
bloc. `IrBranch` a deux successeurs : sa cible et l'instruction suivante.

Tous les labels sont uniques dans le programme et chaque branche ou saut cible un
label de la même région. Une instruction ordinaire après un terminal est
inatteignable et invalide jusqu'au prochain label ou début de fonction.

Chaque chemin atteignable d'une fonction se termine par `IrReturn` ou
`IrTailCall`. `IrExit` est réservé à `<init>`. La région `<init>` d'un exécutable
se termine par `IrExit`; celle d'un objet de module peut tomber jusqu'à sa fin,
car `generateObject` y ajoute actuellement le `ret`. Cette différence devra être
passée explicitement au vérificateur (`Executable` ou `ModuleObject`) et non
déduite du contenu.

## Diagnostics stables

| Code | Contrat enfreint |
| --- | --- |
| `IRV001` | `valueCount` et `valueTypes.size()` divergent |
| `IRV002` | type mal formé ou pointeur de type composé nul |
| `IRV003` | paramètre générique non substitué dans l'IR |
| `IRV004` | métadonnée globale incohérente, notamment `exitValue` |
| `IRV010` | frontière de fonction invalide, nom vide ou doublon |
| `IRV011` | préambule, index ou offset de paramètre invalide |
| `IRV012` | valeur utilisée à travers une frontière de région |
| `IRV013` | slot local utilisé par plusieurs régions |
| `IRV020` | `ValueId` hors limites |
| `IRV021` | type de sortie différent de `valueTypes[output]` |
| `IRV022` | redéfinition atteignable d'une valeur |
| `IRV023` | valeur utilisée avant d'être définitivement définie |
| `IRV024` | pseudo-phi `IrCopy` incomplet ou non exclusif |
| `IRV030` | `SlotId` hors limites |
| `IRV031` | type d'instruction différent du type du slot |
| `IRV032` | catégorie de slot incohérente, notamment `external` sans `global` |
| `IRV040` | type d'opérande incompatible |
| `IRV041` | opérateur, propriété ou conversion inconnue |
| `IRV042` | arité ou liste de types d'un appel incohérente |
| `IRV043` | signature incohérente entre définition et appel interne |
| `IRV044` | type ou indice d'agrégat invalide |
| `IRV045` | combinaison invalide des drapeaux d'indexation |
| `IRV050` | label absent, dupliqué ou situé dans une autre région |
| `IRV051` | condition de branche différente de `Bool` |
| `IRV052` | instruction ordinaire après un terminal sans nouveau label |
| `IRV053` | chemin atteignable de fonction non terminé |
| `IRV054` | terminal interdit dans cette région |

Les codes décrivent une famille de faute, pas une variante C++ particulière. Une
nouvelle instruction réutilise un code si elle enfreint le même invariant.

## Contrat de chaque instruction

Dans les tableaux suivants, `type($n)` désigne `valueTypes[n]`, `type(%n)` le
type du slot, `T` un type concret, `E` un type élément et `Option[E]` l'instance
builtin canonique. Toutes les lectures sont soumises à `IRV020/023`, toutes les
sorties à `IRV020/021/022`, et tous les slots à `IRV030` ; ces contrôles ne sont
pas répétés sur chaque ligne.

### Constantes, chaînes et collections possédées

| Instruction | Lit | Produit | Contrat supplémentaire | Contrôle |
| --- | --- | --- | --- | --- |
| `IrConst` | — | `output : type` | `type` est `Int`, `Byte`, `Bool` ou `Char`; un `Bool` vaut 0 ou 1 et un `Byte` tient sur 8 bits | suite |
| `IrDoubleConst` | — | `output : Double` | — | suite |
| `IrStringConst` | — | `output : String` | `utf8` est un UTF-8 valide | suite |
| `IrStringConcat` | `left`, `right : String` | `output : String` | — | suite |
| `IrStringLength` | `string : String` ou `StringView` | `output : Int` | — | suite |
| `IrStringEmpty` | `string : String` ou `StringView` | `output : Bool` | — | suite |
| `IrArrayConstruct` | `elements[*] : E` | `output : T` | `T == Array[E, N]` et `elements.size() == N` | suite |
| `IrVecConstruct` | — | `output : T` | `T == Vec[E]` | suite |
| `IrVecProperty` | `vector : Vec[E]` | `output : Int` pour `length`/`capacity`, `Bool` pour `isEmpty` | propriété dans cet ensemble fermé | suite |
| `IrVecReserve` | `additional : Int`, `%slot : T` | `output : Int` | `T == Vec[E]` | suite |
| `IrVecPush` | `value : E`, `%slot : T` | `output : Int` | `T == Vec[E]` | suite |
| `IrVecClear` | `%slot : T` | `output : Int` | `T == Vec[E]` | suite |
| `IrVecView` | `%slot : Vec[E]` | `output : T` | `T == Slice[E]` ou `SliceMut[E]` | suite |
| `IrVecGet` | `index : Int`, `%slot : Vec[E]` | `output : Option[E]` | `elementType == E`, `optionType` est l'instance builtin exacte | suite |
| `IrVecPop` | `%slot : Vec[E]` | `output : Option[E]` | mêmes contraintes de type que `IrVecGet` | suite |
| `IrVecSet` | `index : Int`, `value : E`, `%slot : Vec[E]` | `output : Int` | — | suite |

Les retours `Int` des opérations mutantes sont le contrat provisoire actuel ; le
passage futur à `Unit` modifiera ces lignes et les tests associés.

### Agrégats, vues et accès mémoire

| Instruction | Lit | Produit | Contrat supplémentaire | Contrôle |
| --- | --- | --- | --- | --- |
| `IrStructConstruct` | `fields[i] : T.fields[i]` | `output : T` | `T` est une structure et l'arité est exacte | suite |
| `IrEnumConstruct` | champs de la variante | `output : T` | `T` est un enum, `variant` et l'arité sont valides | suite |
| `IrEnumTag` | `input : Enum` | `output : Int` | — | suite |
| `IrEnumFieldLoad` | `input : T` | `output : champ` | `T == type`, indices de variante et champ valides | suite |
| `IrFieldLoad` | `object : T` | `output : champ` | `T == objectType`, structure et champ valides | suite |
| `IrFieldStore` | `value : champ`, `%slot : T` | — | `T == objectType`, structure et indice valides | suite |
| `IrSliceConstruct` | `reference : Reference[Array[E,N]]` | `output : T` | `T == Slice[E]` avec même mutabilité, `length == N` | suite |
| `IrSliceLength` | `slice : Slice[E]` | `output : Int` | partagé ou mutable accepté | suite |
| `IrBoxConstruct` | `value : E` | `output : Box[E]` | `elementType == E` | suite |
| `IrIndexLoad` | `array`, `index : Int` | `output : E` | type et drapeaux suivent la matrice ci-dessous | suite |
| `IrIndexStore` | `array` si vue/référence, `indexes[*] : Int`, `value : E`; sinon `%slot` | — | au moins un index; descente exacte dans les tableaux imbriqués; cible mutable | suite |
| `IrAddressOf` | `%slot : T` | `output : Reference[T]` | type référencé égal à `T` | suite |
| `IrDereference` | `reference : Reference[T]` ou `Box[T]` | `output : T` | `type == T` | suite |
| `IrDereferenceStore` | `reference : ReferenceMut[T]` ou `Box[T]`, `value : T` | — | référence mutable ou propriétaire box obligatoire | suite |
| `IrLoad` | `%slot : T` | `output : T` | `type == T` | suite |
| `IrStore` | `value : T`, `%slot : T` | — | `type == T` | suite |
| `IrCopy` | `input : T` | `output : T` | producteur multiple de fusion; accepte aussi `Box[T] -> Reference[T]` pour l'emprunt du contenu, les deux ayant la même représentation pointeur | suite |

Matrice d'`IrIndexLoad` : sans drapeau, `arrayType == Array[E,N]` et `array` porte
ce type ; avec `arrayIsReference`, `array` porte une référence vers
`arrayType == Array[E,N]` ; avec `arrayIsSlice`, `arrayType == Slice[E]` et
`array` porte cette slice. Les deux drapeaux ne peuvent pas être vrais ensemble.
`IrIndexStore` applique la même matrice, exige une référence/slice mutable dans
les formes indirectes, et utilise exclusivement le slot dans la forme directe.

`IrSlot` ne sérialise actuellement ni la mutabilité de la déclaration ni celle
du module propriétaire. Le vérificateur peut donc contrôler la mutabilité portée
par une `Reference` ou une `Slice`, mais pas reconstituer si un slot direct vient
d'un `val` ou d'un `var`. Cette garantie reste dans l'analyse sémantique jusqu'à
l'ajout éventuel de cette métadonnée à l'IR.

### Calculs et appels

| Instruction | Lit | Produit | Contrat supplémentaire | Contrôle |
| --- | --- | --- | --- | --- |
| `IrConvert` | `input : source` | `output : target` | paire dans la matrice de conversions supportée par le backend | suite |
| `IrUnary` | `operand : type` | `output : type` | `-` sur `Int`, `Byte`, `Double`; `!` sur `Bool`; copie unaire seulement si explicitement conservée | suite |
| `IrBinary` | `left`, `right : operandType` | `output : type` | arithmétique : résultat `operandType`; comparaison : `Bool`; `&&`/`||` : trois types `Bool`; opérateur autorisé pour `operandType` | suite |
| `IrCall` | `arguments[i] : argumentTypes[i]` | `output : returnType` | tailles des listes égales, nom non vide; signature exacte si la cible est interne | suite |
| `IrTailCall` | `arguments[i] : argumentTypes[i]` | — | mêmes règles d'arguments; cible interne égale à la fonction courante dans le modèle actuel | terminal |

La matrice de `IrConvert` couvre les identités primitives, les six conversions
entre `Int`, `Byte` et `Double`, `Int <-> Char`, toute primitive vers `String`,
ainsi que les constructions de slice et de box qui possèdent leurs instructions
dédiées. `Bool` ne se convertit pas vers un nombre. Toute extension du backend
doit mettre cette matrice à jour dans le même changement.

### Fonctions, ressources et contrôle

| Instruction | Lit | Produit | Contrat supplémentaire | Contrôle |
| --- | --- | --- | --- | --- |
| `IrFunctionStart` | — | — | ouvre une région unique; voir préambule | frontière |
| `IrParameter` | pile d'appel | `output : type` | seulement dans le préambule; index et offset canoniques | suite |
| `IrReturn` | `value : type` | — | seulement dans une fonction; type identique sur tous les retours | terminal |
| `IrDrop` | `value : type` | — | type exact; `valueTypeNeedsDrop(type)` vrai | suite |
| `IrRetain` | `value : type` | — | type exact; type retenable par le backend (`String` ou agrégat qui en contient) | suite |
| `IrExit` | `value : Int` | — | seulement dans `<init>` en mode exécutable | terminal |
| `IrBranch` | `condition : Bool` | — | label de la même région | deux successeurs |
| `IrJump` | — | — | label de la même région | terminal |
| `IrLabel` | — | — | label unique | début de bloc |

`IrDrop` et `IrRetain` vérifient ici la compatibilité structurelle, pas la
linéarité complète des propriétaires. Les doubles destructions et usages après
déplacement restent d'abord garantis par l'analyse sémantique ; une analyse de
ressources dédiée pourra renforcer ultérieurement l'IR.

## Architecture d'implémentation

Le composant prévu est indépendant du codegen :

```cpp
enum class IrVerificationMode { Executable, ModuleObject };

class IrVerifier {
public:
    static void verify(const IrProgram&, IrVerificationMode);
};
```

L'implémentation suit quatre passes sans mutation :

1. valider tables et formes récursives des types ;
2. découper les régions, indexer fonctions, paramètres, labels et propriétaires
   de slots ;
3. construire les blocs et vérifier références, signatures et contrats locaux de
   chaque variante ;
4. propager définitions et accessibilité dans le graphe, puis vérifier les
   terminaisons.

`IrVerifier` lève une exception interne portant le code et le contexte. Le point
d'entrée du compilateur la laisse rejoindre le chemin d'affichage actuel des
erreurs, sans fabriquer de `SourceLocation`.

L'intégration se fait aux frontières publiques de sortie, pas seulement dans
`main` : `IrGenerator::print`, `FasmCodeGenerator::generate` et
`generateObject` vérifient un `IrProgram` même lorsqu'un test le leur fournit
directement. Le printer reçoit explicitement le mode ; `generate` applique le mode
exécutable et `generateObject` choisit le mode à partir de son paramètre
`entryPoint`. `IrVerifier::verify` retourne un `VerifiedIrProgram` opaque que le
flux normal réutilise ensuite pour le printer et le codegen. Les surcharges qui
reçoivent une IR brute restent sûres et construisent elles-mêmes ce résultat.

## Stratégie de tests

Un exécutable unitaire construit de petits `IrProgram` sans parser de source. Il
contient au minimum un cas valide par famille et un cas par code `IRV`. Les cas de
flot couvrent : label étranger, branche sans définition sur un prédécesseur,
pseudo-phi valide, pseudo-phi redéfini sur un même chemin, instruction après
terminal et fonction avec chute implicite.

Les tests d'intégration compilent modules, génériques et showcase avec la
vérification active. Des tests de frontière injectent aussi une corruption
déterministe directement dans `IrGenerator::print`,
`FasmCodeGenerator::generate` et `generateObject`, afin d'observer `IRV` avant
que ces API puissent produire leur sortie textuelle.

La validation globale exécute le vérificateur sur :

- l'IR de `examples/stdlib_showcase.zeta` en mode exécutable ;
- chaque IR de module produite pendant ce build en mode objet ;
- les fixtures génériques et interfaces précompilées existantes.

Le coût est mesuré avec cinq échantillons alternés par configuration, chaque
échantillon agrégeant vingt compilations propres du showcase et étant ramené au
temps par compilation. Cette agrégation limite le bruit d'ordonnancement sur une
commande Release très courte. La cible reste un surcoût médian inférieur à 5 %.

Mesure du 14 juillet 2026 : GCC 15.2.0 en Release, Linux WSL2
6.18.33.2, Intel Core i5-10300H (4 cœurs, 8 threads), processus épinglés au CPU 0.
La médiane est de 35,30 ms avec vérification contre 33,09 ms sans vérification,
soit +6,67 %. Le résultat validé opaque, les bitsets de définitions et les tables
de lectures/sorties précalculées ont supprimé les validations et allocations
redondantes, mais il reste 0,55 ms à gagner pour atteindre la cible de 5 %.

## Conditions d'évolution

Toute nouvelle variante de `IrInstruction` doit, dans le même commit :

- déclarer ses lectures, sorties, slots, types et effet de contrôle dans ce
  document ;
- ajouter au moins un test valide et un test de rejet ;
- être traitée explicitement par le visiteur de `IrVerifier` ;
- conserver un `static_assert` sur le nombre de variantes afin qu'un ajout ne
  puisse pas contourner silencieusement l'inventaire.

Une modification du seul vérificateur ne change ni ABI ni cache. Une correction
du générateur ou du codegen déclenchée par lui doit réviser le cache de modules si
elle peut changer l'objet produit, conformément à `ROADMAP.md`.

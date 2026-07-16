# Backend LLVM/Clang expérimental

Ce document décrit l'introduction progressive d'un backend LLVM pour Zeta. Le
backend FASM reste la référence de correction ; le backend LLVM/Clang est ajouté
comme cible expérimentale, derrière des options explicites, afin de gagner en
optimisations, diagnostics d'assemblage/linkage et portabilité sans réécrire le
front-end.

## Objectifs

- Émettre du LLVM IR textuel (`.ll`) depuis l'IR Zeta déjà vérifiée.
- Utiliser `clang` comme driver pour optimiser, assembler et lier le programme.
- Garder `fasm` comme backend par défaut et oracle de comparaison.
- Faire progresser le support par sous-ensembles testables.
- Ne pas exposer de nouvelle ABI publique tant que le mapping runtime n'est pas
  stabilisé.

## Non-objectifs de la première tranche

- Pas de remplacement du parser, de l'analyse sémantique ou de l'IR Zeta.
- Pas de support immédiat pour `String`, `Box`, `Vec`, slices, modules séparés ou
  stdlib précompilée via LLVM.
- Pas de dépendance à l'API C++ LLVM dans le compilateur : la première tranche
  produit du texte `.ll` déterministe.
- Pas de sélection automatique du backend selon la présence de `clang`.

## Interface CLI cible

Le backend reste explicite :

```text
zeta source.zeta -o programme                  # backend FASM par défaut
zeta source.zeta --backend=fasm -o programme   # équivalent explicite
zeta source.zeta --emit-llvm -o programme      # écrit programme.ll seulement
zeta source.zeta --backend=clang -o programme  # écrit .ll puis appelle clang
```

Contraintes initiales :

- `--emit-llvm` implique le backend LLVM mais ne lance pas `clang`.
- `--backend=clang` échoue avec un diagnostic clair si `clang` est absent.
- `--backend=fasm --emit-llvm` est rejeté comme combinaison incohérente.
- `--build-library`, `--install-library` et `--build-stdlib` restent FASM-only au
  début.

## Pipeline

```text
source Zeta
  -> lexer/parser
  -> analyse sémantique
  -> IR Zeta
  -> vérificateur IR
  -> LlvmIrCodeGenerator
  -> fichier .ll
  -> clang -x ir <fichier.ll> -o <exécutable>   # seulement --backend=clang
```

La génération LLVM intervient donc après la même frontière que le backend FASM.
Toute divergence doit être visible dans les tests de comparaison FASM/Clang.

## Sous-ensemble initial `--emit-llvm`

La première tranche supporte uniquement un noyau sans runtime :

- `Int` (`i32`) et `Bool` (`i1` pour les calculs, extension vers `i32` au besoin)
- constantes entières et booléennes ;
- arithmétique et comparaisons entières ;
- fonctions Zeta non génériques avec paramètres scalaires ;
- appels directs ;
- blocs, labels, branches conditionnelles et sauts ;
- `return` et sortie de `main` par code de retour.

Les tests doivent commencer par un programme minimal qui retourne un entier
constant, puis élargir à arithmétique, `if`, `while` et appel de fonction.

## Mapping de types initial

- `Int` -> `i32`
- `Byte` -> `i8` (tranche suivante)
- `Bool` -> `i1` en SSA, normalisé depuis/vers `i32` lorsque l'IR Zeta attend une
  valeur stockable homogène
- `Char` -> `i32` (codepoint Unicode, validé pour l'itération chaîne)
- `Double` -> `double` (constantes, slots locaux et IO spécialisée ; opérations
  arithmétiques complètes à couvrir par tranches suivantes)
- `Unit` -> valeur fantôme côté IR Zeta ; pas d'allocation LLVM quand elle n'est
  pas observable
- `Never` -> terminaison de bloc, pas de valeur

Les agrégats (`struct`, enum, array), références, slices et propriétaires
runtime (`String`, `Box`, `Vec`) sont explicitement hors du sous-ensemble initial.

## Convention de noms et linkage

- Les fonctions utilisateur continuent d'être abaissées avec les noms de liaison
  déjà produits par l'IR Zeta (`zeta_fn_*` lorsque présent).
- Un wrapper LLVM `@main() -> i32` appelle le point d'entrée Zeta lorsque l'IR
  exécutable contient déjà l'appel/exit central.
- Tant que les modules séparés ne sont pas supportés, `--emit-llvm` et
  `--backend=clang` sont limités à un executable monolithique sans dépendances
  précompilées autres que les modules source que l'IR global sait déjà intégrer.

## Stratégie runtime

La première tranche évite le runtime natif. Ensuite :

1. définir les signatures LLVM des primitives runtime déjà écrites en assembleur ;
2. fournir une version C ou LLVM IR des primitives nécessaires ;
3. faire lier ces objets par `clang` ;
4. valider `String`, `StringView`, `Box`, `retain`, `drop` et destruction
   déterministe par comparaison avec FASM.

Aucune sémantique de propriété ne doit être déplacée dans LLVM : l'IR Zeta reste
responsable des `retain`/`drop` et le backend ne fait qu'émettre les appels.

## Organisation du code

Tranche recommandée :

1. fait : ajouter `LlvmIrCodeGenerator` en parallèle de `FasmCodeGenerator` ;
2. fait : ajouter les options CLI sans changer le comportement FASM par défaut ;
3. fait : implémenter `--emit-llvm` pour un programme `main(): Int = 42` ;
4. fait : élargir le générateur aux opérations scalaires `Int`/`Bool`
   (`IrBinary`, comparaisons et copies scalaires) ;
5. fait : ajouter les labels/branches et les slots scalaires nécessaires aux
   programmes de contrôle simples (`if`/`while`) ;
6. fait : ajouter les appels avec paramètres scalaires ;
7. fait : ajouter `--backend=clang` pour le sous-ensemble validé.

État actuel : `emit_llvm_minimal` vérifie la génération de `target triple`,
`define i32 @main()`, `ret i32 42`, puis compile le `.ll` avec `clang` et exécute
le binaire obtenu. `emit_llvm_scalars` ajoute un programme sans contrôle de flux
qui couvre `IrBinary` arithmétique (`add nsw`, `sub nsw`, `mul nsw`, `sdiv`), les
comparaisons (`icmp`), les opérations booléennes simples et `IrCopy` scalaire.

La génération LLVM accepte donc le squelette exécutable, `IrCall` sans
paramètre, `IrConst(Int/Bool)`, `IrExit`, `IrFunctionStart`, `IrReturn(Int/Bool)`,
`IrBinary(Int/Bool)` et `IrCopy(Int/Bool/Unit)`. Toute autre instruction échoue
explicitement avec `backend LLVM: instruction non supportée ...` ou un diagnostic
de type non supporté.

`emit_llvm_control` couvre maintenant un programme combinant `if` et `while`.
Cette tranche ajoute `IrLabel`, `IrJump`, `IrBranch`, `IrLoad` et `IrStore` pour
les slots scalaires `Int`/`Bool`. Les labels Zeta sont rendus comme blocs LLVM,
les branches conditionnelles introduisent un bloc de continuation synthétique
pour représenter le fallthrough, et les slots scalaires sont matérialisés par des
`alloca` en entrée de fonction. Les types de slot non scalaires restent hors
périmètre et doivent continuer à produire un diagnostic explicite.

`emit_llvm_parameters` couvre les signatures et appels paramétrés pour `Int` et
`Bool`. Le générateur pré-scanne les `IrParameter` d'une fonction pour émettre la
signature LLVM complète (`define i32 @fn(i32 %arg0, ...)`) avant le corps, puis
lie chaque instruction `IrParameter` à `%argN` sans allocation supplémentaire.
Les appels utilisent les types d'arguments déjà présents dans `IrCall` et restent
limités aux types scalaires couverts par `llvmType`.

`compile_clang_backend_parameters` active le premier chemin `--backend=clang` : le
compilateur écrit le `.ir` vérifié et le `.ll`, invoque `clang -x ir -o <sortie>`,
puis exécute le binaire attendu. `run_fasm_backend_still_default` compile le même
programme sans option backend pour vérifier que FASM reste le défaut et que le
chemin Clang ne crée pas d'artefact `.asm`.

`reject_clang_backend_cli_diagnostics` fixe les diagnostics CLI minimums du
backend expérimental : backend inconnu, `--emit-llvm` combiné à `--backend=fasm`,
et tentative d'utiliser Clang/LLVM sur les modes bibliothèque/stdlib. Ces modes
restent exclus tant que les frontières objets/modules/runtime ne sont pas
couvertes par des tests dédiés.

`compile_clang_backend_global_values` couvre le premier périmètre runtime avec
imports de modules : les fonctions scalaires importées étaient déjà représentées
dans l'IR exécutable complet, et les `pub val` globales `Int`/`Bool` sont
maintenant émises comme `@slotN = global ... 0`, initialisées dans le wrapper
`@main`, puis relues par les fonctions importées via `load ..., ptr @slotN`. Le
test compare le code de retour Clang au code de retour FASM pour éviter une
divergence silencieuse entre backends.

`reject_clang_backend_unsupported_types` verrouille les diagnostics pour les
types non scalaires encore exclus du backend LLVM. Un `pub val greeting: String`
échoue avec `backend LLVM: globale non scalaire non supportée greeting: String` ;
un local `val greeting: String` échoue avec `backend LLVM: slot local non scalaire
non supporté greeting: String`. Ces erreurs utilisent le nom source quand un slot
IR a été qualifié par son module.

`compile_clang_backend_string_literal` ajoute la première représentation LLVM
positive des chaînes : `def main(): Int = "zeta".lengthBytes`. Les bytes du
littéral sont émis en constante privée `@str.N = private unnamed_addr constant
[N x i8] c"..."`, la valeur `String` reste une paire `{ ptr, i64 }`, et
`IrStringLength` devient un `extractvalue` de la longueur suivi d'un `trunc i64`
vers `i32` (`Int`). Le test compile avec Clang, exécute le binaire, et compare le
code retour avec FASM.

`compile_clang_backend_string_is_empty` couvre `IrStringEmpty` sur des littéraux
directs, dont une chaîne vide et une chaîne non vide dans des conditions de
`while`. Le lowering réutilise la paire `{ ptr, i64 }`, extrait l'élément longueur
avec `extractvalue`, puis émet `icmp eq i64 <len>, 0`. Comme pour `lengthBytes`,
le test compare l'exécution Clang à FASM.

`compile_clang_backend_local_string` couvre ensuite les slots locaux `String` :
`alloca { ptr, i64 }`, `store` de la paire complète, `load` de la paire, puis
réutilisation des lowerings `lengthBytes` et `isEmpty`. Les `drop`/`retain` de ce
sous-ensemble sont no-op côté LLVM pour l'instant, car les valeurs prises en
charge sont des littéraux statiques ; l'allocation/runtime des chaînes produites
par concaténation reste hors périmètre de cette tranche.

`compile_clang_backend_string_concat` ajoute une première concaténation locale :
les deux opérandes `{ ptr, i64 }` sont déstructurés, le backend appelle `malloc`
pour réserver `left.length + right.length + 16` octets, écrit le header compatible
avec le runtime actuel (`refcount`, `length`), copie les bytes avec `memcpy`, puis
reconstruit une paire `{ ptr, i64 }` pointant sur le début des bytes. Cette tranche
valide `lengthBytes`/`isEmpty` sur le résultat et compare l'exécution Clang à
FASM.

`compile_clang_backend_string_concat_drop` ajoute le premier nettoyage runtime
pour ces chaînes heap : le backend marque les résultats de `IrStringConcat`,
propage conservativement cette information à travers les `store`/`load` de slots
locaux `String`, puis abaisse le `drop` du propriétaire en extraction du pointeur
data, `getelementptr i8 ... -16` vers le header runtime, et `free(raw)`. Les
littéraux statiques et `StringView` restent no-op au drop pour éviter de libérer
des constantes ou des vues empruntées ; `retain`/aliasing heap complet reste hors
périmètre.

`compile_clang_backend_string_view` ajoute une première surface `stdlib/strings` :
`strings.view(text, start, end)` est abaissé en LLVM sans appel runtime externe.
Le backend extrait `{ ptr, i64 }`, vérifie `start >= 0`, `start <= end` et
`end <= length`, calcule le pointeur par `getelementptr i8`, puis sélectionne soit
la vue valide, soit `{ null, 0 }`. `strings.viewIsValid(view)` devient un test
`icmp ne ptr ... null`. Pour éviter que l'import de `strings` force la génération
LLVM de fonctions stdlib non utilisées et encore hors périmètre (`charAtByte`,
`nextByteOffset`), le backend saute uniquement les fonctions `strings__*` non
atteignables depuis l'exécutable courant ; les fonctions utilisateur non appelées
restent émises afin de préserver les tests `--emit-llvm` historiques.

`compile_clang_backend_string_search` couvre ensuite `strings.indexOf` et
`strings.contains` sur `StringView`. Le lowering reste spécialisé : il extrait les
paires `{ ptr, i64 }`, rejette les vues invalides par résultat `-1`, traite
l'aiguille vide comme l'offset `0`, borne la recherche par `haystack.length -
needle.length`, puis compare chaque position avec `memcmp`. `contains` est abaissé
comme `indexOf(...) >= 0` et la fonction stdlib `strings__contains` importée est
sautée côté LLVM, car les appels sont remplacés directement par cette primitive
spécialisée. Cette tranche ne définit pas encore d'ABI native générale pour les
fonctions `StringView`.

`compile_clang_backend_string_utf8_decode` ajoute les primitives UTF-8 de bas
niveau sur `String`. `strings.decodeAtByte(text, offset)` extrait la paire chaîne,
vérifie `offset >= 0 && offset < length`, lit le premier octet, puis produit un
codepoint `Int` pour les séquences 1, 2, 3 ou 4 octets avec validation des octets
de continuation ; les offsets invalides ou placés sur une continuation retournent
`-1`. `strings.nextByteOffset(text, offset)` réutilise ce décodage spécialisé et
avance de 1/2/3/4 octets, ou retourne `-1` si le décodage échoue.

`compile_clang_backend_for_string_char_iteration` réutilise ce décodage dans les
instructions IR de boucle `IrStringDecodeAt` et `IrStringNextOffset`. Le backend
Clang accepte désormais `Char` comme `i32`, les constantes/copies/conversions
`Char` <-> `Int`, et l'itération `for` sur `String` comme sur `StringView` ; l'état
reste un `Int` mais représente un offset d'octet UTF-8, pas un index logique de
caractère. La tranche ne définit toujours pas `charAtByte`/`Option[Char]` ni une
ABI native générale pour des fonctions arbitraires prenant `StringView`.

`compile_clang_backend_io_println_string` ajoute une première sortie standard
ciblée : les appels natifs directs `io.print(value: String)` et
`io.println(value: String)` sont remplacés par `write(1, ptr, len)` sur la paire
chaîne LLVM `{ ptr, i64 }`; `println` ajoute un second `write` vers une constante
newline privée. Les fonctions helper `io__*` non atteignables importées par le
module sont sautées comme les helpers `strings__*` hors périmètre, afin d'éviter de
forcer dans cette tranche les conversions `Int`/`Bool`/`Double` vers `String` ou les
retours `Unit` génériques. La tranche compare la sortie UTF-8 Clang à FASM.

`compile_clang_backend_io_println_int` ajoute une sortie entière ciblée : les
appels stdlib directs `io.printInt(value: Int)` et `io.printlnInt(value: Int)` sont
abaissés vers `printf` avec deux formats privés (`%d` et `%d\n`). Les fonctions
helpers `io__printInt`/`io__printlnInt` sont sautées pendant l'émission LLVM même
si elles sont atteignables, car leurs corps passent aujourd'hui par la conversion
générale `String(Int)` qui reste hors périmètre. Cette tranche n'est donc pas une
ABI native générale ni un support complet de `String(value)`.

`compile_clang_backend_io_println_bool` ajoute une sortie booléenne ciblée : les
appels stdlib directs `io.printBool(value: Bool)` et
`io.printlnBool(value: Bool)` sélectionnent entre deux constantes privées `true`
et `false`, puis appellent `write(1, ptr, len)` ; `printlnBool` écrit ensuite le
newline privé partagé. Les helpers `io__printBool`/`io__printlnBool` sont sautés
pendant l'émission LLVM pour éviter de dépendre de la conversion générale
`String(Bool)`, encore hors périmètre.

`compile_clang_backend_io_println_byte` ajoute une sortie `Byte` ciblée : `Byte`
est représenté comme `i8` côté LLVM, les conversions minimales `Int -> Byte` et
`Byte -> Int` sont abaissées en `trunc i32 ... to i8` et `zext i8 ... to i32`, et
les appels stdlib directs `io.printByte(value: Byte)` /
`io.printlnByte(value: Byte)` passent par `printf` avec formats `%u` / `%u\n`
après extension non signée. Les helpers `io__printByte`/`io__printlnByte` sont
sautés pendant l'émission LLVM pour éviter la conversion générale `String(Byte)`.

`compile_clang_backend_io_println_char` ajoute une sortie `Char` ciblée : la
stdlib expose désormais `printlnChar`, et les appels directs `io.printChar` /
`io.printlnChar` encodent le codepoint `i32` en UTF-8 dans un buffer stack de
quatre octets. Le backend sélectionne une longueur 1/2/3/4, écrit les bytes via
`write(1, ptr, len)`, puis `printlnChar` écrit le newline partagé. Les helpers
`io__printChar`/`io__printlnChar` sont sautés pendant l'émission LLVM pour éviter
la conversion générale `String(Char)`.

`compile_clang_backend_io_println_double` ajoute une sortie `Double` ciblée :
`Double` est représenté comme `double`, les constantes littérales et les slots
locaux doubles sont stockables, le moins unaire est abaissé en `fneg double`, et
les appels directs `io.printDouble` / `io.printlnDouble` passent par `printf`
avec formats `%g` / `%g\n`. La tranche reste volontairement limitée aux formats
stables comparés à FASM.

`compile_clang_backend_double_operations` couvre ensuite le noyau calculatoire
`Double` : les opérations `+`, `-`, `*`, `/` sont abaissées respectivement en
`fadd`, `fsub`, `fmul`, `fdiv`, et les comparaisons utilisent des prédicats
ordonnés `fcmp` (`oeq`, `one`, `olt`, `ole`, `ogt`, `oge`) pour rester explicites
sur le comportement en présence de NaN.

`compile_clang_backend_local_struct` introduit le premier agrégat positif : un
`struct` local dont tous les champs ont déjà un type LLVM supporté est abaissé en
type agrégat littéral (`{ i32, i32 }` pour le test initial). La construction utilise
une chaîne `insertvalue`, les slots locaux deviennent `alloca { ... }`, `store` /
`load` copient l'agrégat entier, et les lectures de champ utilisent `extractvalue`.
`compile_clang_backend_local_struct_field_store` ajoute ensuite la mutation de
champ locale : le backend recharge la valeur du slot, remplace le champ visé par
`insertvalue`, puis restocke l'agrégat complet. Cette tranche reste volontairement
locale : les globales struct, les paramètres/retours de structs et les champs hors
sous-ensemble (`Box`, `Vec`, tableaux, enums) restent hors périmètre.

## Matrice de tests

Chaque tranche LLVM doit inclure :

- un test `emit_llvm_*` qui vérifie la création du `.ll` et quelques marqueurs
  textuels stables (`target triple`, `define i32 @main`, `ret i32 ...`) ;
- un test `compile_clang_backend_*` lorsque `--backend=clang` est livré ;
- un test d'exécution FASM existant ou nouveau avec le même code source ;
- un test d'exécution Clang qui doit retourner le même code ou produire la même
  sortie ;
- un test négatif CLI pour les combinaisons d'options incohérentes.

La suite complète doit continuer à passer avec FASM par défaut.

## Diagnostics

Les erreurs doivent être explicites :

- backend inconnu : `backend inconnu '...'` ;
- combinaison exécutable requise : `--backend=clang et --emit-llvm sont réservés
  aux exécutables` ;
- `--emit-llvm` avec FASM : `--emit-llvm requiert le backend clang` ;
- `clang` introuvable : `clang introuvable pour --backend=clang` ;
- agrégat global non couvert : `backend LLVM: agrégat global non supporté ...` ;
- globale non scalaire : `backend LLVM: globale non scalaire non supportée ...` ;
- agrégat local non couvert : `backend LLVM: agrégat local non supporté ...` ;
- slot local non couvert : `backend LLVM: slot local non supporté ...` ;
- instruction IR non couverte : `backend LLVM: instruction non supportée ...` ;
- type non couvert : `backend LLVM: type non supporté ...`.

Ces diagnostics sont préférables à une génération partielle de `.ll` invalide.

## Critères de sortie de la première étape

- fait : `docs/LLVM_BACKEND_DESIGN.md` existe et définit le périmètre.
- fait : `--emit-llvm` produit un `.ll` minimal vérifiable pour
  `main(): Int = 42`.
- fait : `--emit-llvm` couvre les opérations scalaires `Int`/`Bool` sans contrôle
  de flux et les valide par compilation/exécution `clang`.
- fait : `--emit-llvm` couvre `if`/`while` via labels/branches et slots scalaires,
  validés par compilation/exécution `clang`.
- fait : `--emit-llvm` couvre les appels avec paramètres scalaires, validés par
  compilation/exécution `clang`.
- fait : `--backend=clang` produit un exécutable via `clang -x ir` pour le
  sous-ensemble validé.
- fait : le backend FASM par défaut reste inchangé et testé explicitement.
- fait : les diagnostics CLI minimums du backend Clang/LLVM sont testés.
- fait : `--backend=clang` couvre les imports de modules scalaires avec globales
  `Int`/`Bool` et comparaison d'exécution FASM.
- fait : les diagnostics LLVM pour globales/slots non scalaires sont couverts.
- fait : `--backend=clang` couvre un littéral `String` direct et `lengthBytes`.
- fait : `--backend=clang` couvre `String.isEmpty` sur littéraux directs.
- fait : `--backend=clang` couvre les slots locaux `String` initialisés par
  littéraux directs.
- fait : `--backend=clang` couvre une concaténation locale minimale de chaînes.
- fait : `--backend=clang` libère les buffers heap issus de concaténation au
  `drop` local propriétaire, sans libérer les littéraux statiques.
- fait : les diagnostics LLVM distinguent les agrégats locaux non supportés
  (`struct`, `Vec[T]`) avec des noms source lisibles.
- fait : les diagnostics LLVM distinguent les agrégats globaux non supportés
  (`struct`, `Vec[T]`) avec des noms source lisibles, séparément des globales
  `String`.
- fait : `--backend=clang` couvre `strings.view` et `strings.viewIsValid` comme
  première surface stdlib chaîne ciblée.
- fait : `--backend=clang` couvre `strings.indexOf` et `strings.contains` sur
  `StringView` via un lowering spécialisé et comparaison d'exécution FASM.
- fait : `--backend=clang` couvre `strings.decodeAtByte` et
  `strings.nextByteOffset` sur `String` pour les séquences UTF-8 1/2/3/4 octets.
- fait : `--backend=clang` couvre l'itération `for` UTF-8 sur `String` et
  `StringView`, avec `Char` abaissé en `i32`.
- fait : `--backend=clang` couvre `io.print`/`io.println` directs sur `String`
  via `write(1, ptr, len)` et comparaison stdout avec FASM.
- fait : `--backend=clang` couvre `io.printInt`/`io.printlnInt` directs via
  `printf` spécialisé et comparaison stdout avec FASM.
- fait : `--backend=clang` couvre `io.printBool`/`io.printlnBool` directs via
  sélection `true`/`false` et `write`, avec comparaison stdout FASM.
- fait : `--backend=clang` couvre `io.printByte`/`io.printlnByte` directs via
  `Byte` en `i8`, extension non signée et `printf`, avec comparaison stdout FASM.
- fait : `--backend=clang` couvre `io.printChar`/`io.printlnChar` directs via
  encodage UTF-8 1-4 octets et `write`, avec comparaison stdout FASM.
- fait : `--backend=clang` couvre `io.printDouble`/`io.printlnDouble` directs via
  `Double` en `double`, constantes/slots locaux, `fneg` unaire et `printf("%g")`
  sur formats stables comparés à FASM.
- fait : `--backend=clang` couvre les opérations arithmétiques `Double`
  `+`/`-`/`*`/`/` via `fadd`/`fsub`/`fmul`/`fdiv`, et les comparaisons ordonnées
  via `fcmp o*`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre les `struct` locaux simples en lecture :
  types agrégats LLVM littéraux, construction `insertvalue`, slots `alloca`/`store`/
  `load`, et lecture de champ `extractvalue`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre les mutations de champs de `struct` locaux
  simples par `load` de l'agrégat, `insertvalue` du champ remplacé, puis `store`
  de l'agrégat complet, avec exécution Clang et FASM.

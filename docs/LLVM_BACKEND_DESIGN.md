# Backend LLVM/Clang — migration vers le backend principal

Ce document décrivait l'introduction expérimentale d'un backend LLVM pour Zeta.
L'objectif est maintenant plus ambitieux : **faire de LLVM/Clang le backend de
développement principal et remplacer progressivement FASM**. FASM reste utile à
court terme comme oracle de comparaison et fallback legacy, mais les nouvelles
fonctionnalités doivent être conçues et testées d'abord pour le chemin LLVM.

## Objectifs actualisés

- Émettre du LLVM IR textuel (`.ll`) depuis l'IR Zeta déjà vérifiée.
- Utiliser `clang` comme driver pour optimiser, assembler et lier le programme.
- Faire passer les exemples, la stdlib et les modules séparés par LLVM/Clang.
- Garder FASM comme oracle temporaire de comparaison, puis comme backend legacy
  explicite (`--backend=fasm`) après bascule du défaut vers Clang.
- Stabiliser l'ABI runtime/stdlib côté LLVM pour éviter une prolifération de
  lowerings spécialisés.
- Continuer par sous-ensembles testables RED/GREEN, avec diagnostics explicites
  pour tout périmètre non encore porté.

## Non-objectifs immédiats de la bascule

- Pas de remplacement du parser, de l'analyse sémantique ou de l'IR Zeta.
- Pas de dépendance obligatoire à l'API C++ LLVM dans le compilateur tant que le
  générateur textuel `.ll` reste suffisant.
- Pas de suppression brutale de FASM avant que les exemples, la stdlib et les
  modules séparés soient verts en LLVM.

## Interface CLI cible

Interface actuelle, avant inversion du défaut :

```text
zeta source.zeta -o programme                  # FASM par défaut tant que la matrice LLVM n'est pas complète
zeta source.zeta --backend=fasm -o programme   # fallback/oracle explicite
zeta source.zeta --emit-llvm -o programme      # écrit programme.ll seulement
zeta source.zeta --backend=clang -o programme  # écrit .ir/.ll puis appelle clang
```

Interface cible après bascule :

```text
zeta source.zeta -o programme                  # Clang/LLVM par défaut
zeta source.zeta --backend=clang -o programme  # équivalent explicite
zeta source.zeta --backend=fasm -o programme   # backend legacy explicite
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

L'IR Zeta reste responsable des points `retain`/`drop` et de l'ordre de destruction.
Le backend LLVM matérialise ensuite ces opérations avec l'ABI runtime disponible :
appels runtime quand ils existent, ou séquences LLVM explicites de refcount/free
pour les sous-ensembles déjà portés (notamment `String` heap et chemins de champs
dans structs).

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
et usages encore interdits comme `--emit-llvm` sur les modes bibliothèque/stdlib
ou `--backend=clang` sur l'installation directe de modules. Les modes
`--build-library --backend=clang` et `--build-stdlib --backend=clang` sont désormais
réouverts par tranches testées.

`compile_clang_backend_global_values` couvre le premier périmètre runtime avec
imports de modules : les fonctions scalaires importées étaient déjà représentées
dans l'IR exécutable complet, et les `pub val` globales `Int`/`Bool` sont
maintenant émises comme `@slotN = global ... 0`, initialisées dans le wrapper
`@main`, puis relues par les fonctions importées via `load ..., ptr @slotN`. Le
test compare le code de retour Clang au code de retour FASM pour éviter une
divergence silencieuse entre backends.

`compile_clang_backend_global_string` étend ce périmètre aux `pub val String`
globales : les slots `String` globaux LLVM sont émis comme
`@slotN = global { ptr, i64 } zeroinitializer`, initialisés dans le wrapper
`@main`, puis relus par `load { ptr, i64 }, ptr @slotN`. Le test verrouille la
forme IR, exécute le binaire Clang, vérifie stdout (`zeta`) et le code retour issu
de `lengthBytes`.

`compile_clang_backend_global_double` couvre ensuite les `pub val Double`
globales : le slot est émis comme `@slotN = global double 0.000000e+00`, initialisé
par `store double` dans `@main`, relu via `load double, ptr @slotN`, puis utilisé
par `io.printlnDouble` et une comparaison `fcmp`, avec stdout et code retour
vérifiés côté Clang.

`compile_clang_backend_global_byte` couvre les `pub val Byte` globales :
le slot est émis comme `@slotN = global i8 0`, initialisé par `store i8`, relu via
`load i8, ptr @slotN`, puis utilisé par `io.printlnByte` et `Int(Byte)`, avec stdout
et code retour vérifiés côté Clang.

`compile_clang_backend_global_char` couvre les `pub val Char` globales : le slot
est émis comme `@slotN = global i32 0`, initialisé par `store i32`, relu via
`load i32, ptr @slotN`, puis utilisé par `io.printlnChar` et `Int(Char)`, avec stdout
et code retour vérifiés côté Clang.

`compile_clang_backend_global_struct` couvre les `pub val` structs dont tous les
champs sont déjà des types LLVM supportés : le slot est émis comme
`@slotN = global { ... } zeroinitializer`, initialisé par `store { ... }` dans
`@main`, relu par `load { ... }, ptr @slotN`, puis les champs restent extraits
via les chemins d'agrégats locaux existants.

Les diagnostics d'agrégats globaux restent couverts séparément par les tests
`reject_clang_backend_unsupported_aggregates` et
`reject_clang_backend_unsupported_global_aggregates` pour les tableaux, slices,
Box, Vec et enums encore hors périmètre global LLVM.

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
`strings.view(text, start, end)` passe par la frontière runtime interne
`@zeta_rt_strings_view(ptr, i64, i32, i32)`, après extraction du couple `String`
au point d'appel. Le helper vérifie `start >= 0`, `start <= end` et
`end <= length`, calcule le pointeur par `getelementptr i8`, puis sélectionne soit
la vue valide, soit `{ null, 0 }`. `strings.viewIsValid(view)` devient un test
`icmp ne ptr ... null`. Pour éviter que l'import de `strings` force la génération
LLVM de fonctions stdlib non utilisées et encore hors périmètre (`charAtByte`,
`nextByteOffset`), le backend saute uniquement les fonctions `strings__*` non
atteignables depuis l'exécutable courant ; les fonctions utilisateur non appelées
restent émises afin de préserver les tests `--emit-llvm` historiques.

`compile_clang_backend_string_search` couvre ensuite `strings.indexOf` et
`strings.contains` sur `StringView`. Les points d'appel extraient les paires
`{ ptr, i64 }` puis appellent la frontière runtime interne
`@zeta_rt_strings_index_of(ptr, i64, ptr, i64)`. Le helper rejette les vues
invalides par résultat `-1`, traite l'aiguille vide comme l'offset `0`, borne la
recherche par `haystack.length - needle.length`, puis compare chaque position avec
`memcmp`. `contains` est abaissé comme `zeta_rt_strings_index_of(...) >= 0` et la
fonction stdlib `strings__contains` importée est sautée côté LLVM, car les appels
sont remplacés directement par cette primitive spécialisée. Cette tranche ne
définit pas encore d'ABI native générale pour les fonctions `StringView`.

`compile_clang_backend_string_utf8_decode` ajoute les primitives UTF-8 de bas
niveau sur `String`. `strings.decodeAtByte(text, offset)` extrait la paire chaîne
et appelle la frontière runtime interne
`@zeta_rt_strings_decode_at_byte(ptr, i64, i32)`, qui vérifie
`offset >= 0 && offset < length`, lit le premier octet, puis produit un codepoint
`Int` pour les séquences 1, 2, 3 ou 4 octets avec validation des octets de
continuation ; les offsets invalides ou placés sur une continuation retournent
`-1`. `strings.nextByteOffset(text, offset)` passe par
`@zeta_rt_strings_next_byte_offset(ptr, i64, i32)`, qui réutilise ce décodage et
avance de 1/2/3/4 octets, ou retourne `-1` si le décodage échoue.

`compile_clang_backend_for_string_char_iteration` réutilise ces deux frontières
runtime dans les instructions IR de boucle `IrStringDecodeAt` et
`IrStringNextOffset`. Le backend Clang accepte désormais `Char` comme `i32`, les constantes/copies/conversions
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
helpers `io__printInt`/`io__printlnInt` sont sautées pendant l'émission LLVM pour
conserver les appels directs derrière `@zeta_rt_io_write_int(i32, i1)`, tandis que
la conversion générale `String(Int)` est maintenant couverte séparément par
`@zeta_rt_string_from_int(i32)`.

`compile_clang_backend_io_println_bool` ajoute une sortie booléenne ciblée : les
appels stdlib directs `io.printBool(value: Bool)` et
`io.printlnBool(value: Bool)` passent par la frontière runtime interne
`@zeta_rt_io_write_bool(i1, i1)`. Le helper sélectionne entre deux constantes
privées `true` et `false`, appelle `write(1, ptr, len)` et écrit le newline privé
partagé uniquement pour `printlnBool`. La conversion générale `String(Bool)` est
maintenant couverte séparément par `@zeta_rt_string_from_bool(i1)`, tandis que les
helpers `io__printBool`/`io__printlnBool` restent sautés pendant l'émission LLVM
pour conserver les chemins directs `io.*` derrière leurs frontières runtime dédiées.

`compile_clang_backend_string_literal` verrouille aussi `String.lengthBytes` côté
Clang : le champ longueur extrait de la paire `{ ptr, i64 }` passe désormais par la
frontière runtime interne `@zeta_rt_string_length_bytes(ptr, i64)`, qui retourne la
longueur en `i32`. Le test conserve l'oracle FASM sur le code de sortie tout en
vérifiant une définition unique du helper et un appel applicatif.

`compile_clang_backend_string_is_empty` verrouille `String.isEmpty` côté Clang :
l'accès au champ longueur de la paire `{ ptr, i64 }` passe désormais par la
frontière runtime interne `@zeta_rt_string_is_empty(ptr, i64)`, qui centralise le
prédicat `len == 0` et retourne `i1`. Le test vérifie les deux appels applicatifs
sur `""` et `"zeta"`, l'exécution Clang et l'oracle FASM sur le code de sortie.

`compile_clang_backend_string_bool_conversion` couvre la première conversion
générale vers `String` côté Clang : `String(true)` / `String(false)` appellent la
frontière runtime interne `@zeta_rt_string_from_bool(i1)`. Le helper réutilise les
constantes privées `@zeta.bool.true` / `@zeta.bool.false`, sélectionne la paire
`{ ptr, i64 }` sans allocation ni ownership heap, et le test compare la sortie
Clang/FASM après concaténation avec un littéral.

`compile_clang_backend_string_int_conversion` couvre `String(Int)` côté Clang : le
helper runtime interne `@zeta_rt_string_from_int(i32)` convertit les valeurs
positives, négatives et zéro en décimal, alloue un buffer heap avec en-tête
ownership compatible `drop`, retourne `{ ptr, i64 }`, puis le test compare la
sortie Clang/FASM après concaténation.

`compile_clang_backend_string_byte_conversion` couvre `String(Byte)` côté Clang :
le helper runtime interne `@zeta_rt_string_from_byte(i8)` effectue une extension
non signée `zext` vers `i32`, délègue au helper stabilisé
`@zeta_rt_string_from_int(i32)`, puis retourne la même paire `{ ptr, i64 }`. Le
test verrouille la frontière `Byte` dédiée, la dépendance au helper `Int` et la
sortie Clang/FASM `7 / 250` après concaténation.

`compile_clang_backend_string_char_conversion` couvre `String(Char)` côté Clang :
le helper runtime interne `@zeta_rt_string_from_char(i32)` encode le codepoint en
UTF-8 1/2/3/4 octets dans un buffer heap propriétaire avec en-tête refcount/len,
retourne `{ ptr, i64 }`, puis le test compare la sortie Clang/FASM `A / é / 🚀`
après concaténation.

`compile_clang_backend_string_double_conversion` couvre `String(Double)` côté Clang :
le helper runtime interne `@zeta_rt_string_from_double(double)` centralise le
formatage via `snprintf("%g")`, copie le résultat dans un buffer heap propriétaire
avec en-tête refcount/len, retourne `{ ptr, i64 }`, puis le test compare la sortie
Clang/FASM `18.5 / -0.25 / 0` après concaténation.

`compile_clang_backend_io_println_byte` ajoute une sortie `Byte` ciblée : `Byte`
est représenté comme `i8` côté LLVM, les conversions minimales `Int -> Byte` et
`Byte -> Int` sont abaissées en `trunc i32 ... to i8` et `zext i8 ... to i32`, et
les appels stdlib directs `io.printByte(value: Byte)` /
`io.printlnByte(value: Byte)` passent par la frontière runtime interne
`@zeta_rt_io_write_byte(i8, i1)`. Le helper garde l'extension non signée vers
`i32`, sélectionne les formats privés `%u` / `%u\n` et appelle `printf`. Les
helpers `io__printByte`/`io__printlnByte` sont sautés pendant l'émission LLVM pour
éviter la conversion générale `String(Byte)`.

`compile_clang_backend_io_println_char` ajoute une sortie `Char` ciblée : la
stdlib expose désormais `printlnChar`, et les appels directs `io.printChar` /
`io.printlnChar` passent par la frontière runtime interne
`@zeta_rt_io_write_char(i32, i1)`. Le helper encode le codepoint `i32` en UTF-8
dans un buffer stack de quatre octets, sélectionne une longueur 1/2/3/4, écrit les
bytes via `write(1, ptr, len)` et ajoute le newline partagé selon le booléen
`println`. Les helpers `io__printChar`/`io__printlnChar` sont sautés pendant
l'émission LLVM pour éviter la conversion générale `String(Char)`.

`compile_clang_backend_io_println_double` ajoute une sortie `Double` ciblée :
`Double` est représenté comme `double`, les constantes littérales et les slots
locaux doubles sont stockables, le moins unaire est abaissé en `fneg double`, et
les appels directs `io.printDouble` / `io.printlnDouble` passent par la frontière
runtime interne `@zeta_rt_io_write_double(double, i1)`. Le helper sélectionne les
formats privés `%g` / `%g\n` puis appelle `printf`, ce qui retire la logique de
formatage des corps applicatifs. La tranche reste volontairement limitée aux
formats stables comparés à FASM.

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
`insertvalue`, puis restocke l'agrégat complet. `compile_clang_backend_struct_function_abi`
étend ce même sous-ensemble aux signatures de fonctions : paramètres, retours et
appels portent directement le type agrégat LLVM littéral (`{ i32, i32 }`) lorsque
chaque champ est déjà supporté. `compile_clang_backend_mixed_struct` élargit enfin
les champs locaux supportés à un agrégat mixte `Bool`/`Byte`/`Char`/`Double`/
`String` (`{ i1, i8, i32, double, { ptr, i64 } }`) et verrouille les comparaisons
`Byte`/`Char` nécessaires par `icmp`. Cette tranche reste volontairement limitée :
les globales struct et les champs hors sous-ensemble (`Box`, `Vec`, tableaux,
enums) restent hors périmètre.

`compile_clang_backend_struct_heap_string_interprocedural` verrouille ensuite la
propagation de propriété au-delà d'un seul corps de fonction : une fonction
construit et retourne une struct contenant un `String` heap, une autre reçoit cette
struct, la copie, utilise les deux valeurs, puis les drops doivent décrémenter le
refcount et libérer exactement une fois. Le backend mémorise les chemins de
champs `String` heap sur les paramètres et résultats d'appels, retient les copies
non statiques, et conserve les littéraux avec un refcount sentinelle `-1` pour que
les drops/retains conditionnels ne libèrent jamais les constantes.

## État de migration et prochaines tranches

Le chemin LLVM/Clang couvre désormais un sous-ensemble exécutable large : scalaires
`Int`/`Bool`/`Byte`/`Char`/`Double`, contrôle de flot, appels, modules source avec
globales scalaires, strings et `StringView`, IO spécialisée avec six frontières
runtime internes (`@zeta_rt_io_write_string` pour `io.print`/
`io.println(String)`, `@zeta_rt_io_write_int` pour `io.printInt`/
`io.printlnInt` et `@zeta_rt_io_write_bool` pour `io.printBool`/
`io.printlnBool`, et `@zeta_rt_io_write_byte` pour `io.printByte`/
`io.printlnByte`, et `@zeta_rt_io_write_char` pour `io.printChar`/
`io.printlnChar`, et `@zeta_rt_io_write_double` pour `io.printDouble`/
`io.printlnDouble`), `String.lengthBytes` via
`@zeta_rt_string_length_bytes(ptr, i64)`, `String.isEmpty` via
`@zeta_rt_string_is_empty(ptr, i64)`, conversions `String(Bool)` / `String(Int)` / `String(Byte)` /
`String(Char)` / `String(Double)` via `@zeta_rt_string_from_bool(i1)`,
`@zeta_rt_string_from_int(i32)`, `@zeta_rt_string_from_byte(i8)`,
`@zeta_rt_string_from_char(i32)` et `@zeta_rt_string_from_double(double)`, strings (`String`/`StringView`, concaténation,
`strings.view`, `@zeta_rt_strings_view_is_valid` pour `strings.viewIsValid`,
`@zeta_rt_strings_decode_at_byte` pour `strings.decodeAtByte`,
`@zeta_rt_strings_next_byte_offset` pour `strings.nextByteOffset`,
`@zeta_rt_strings_index_of` pour `strings.indexOf`/`contains`),
structs simples, mixtes et imbriqués, ABI de fonctions sur
structs simples, copies à travers branches et ownership de chaînes heap encapsulées
dans des structs, y compris à travers paramètres, appels et retours de fonctions
portant ces structs. Le mode `--build-library --backend=clang` produit désormais
un objet bibliothèque LLVM (`.ll` + `.o` via `clang -c`) pour les modules simples,
sans définir `main` dans l'IR de bibliothèque ; les exécutables `--backend=clang`
relient aussi les objets précompilés LLVM installés depuis le cache de bibliothèques
en les copiant dans `<app>.modules` avant l'appel final à `clang`. Le mode
`--build-stdlib --backend=clang` sait aussi produire une stdlib précompilée LLVM
pour les modules simples (`.zti`, `.ll`, `.o`, `manifest`), consommable ensuite par
un exécutable Clang via le cache `precompiled`.

Tests structurants déjà verrouillés côté structs/ownership :

- `compile_clang_backend_nested_struct`
- `compile_clang_backend_nested_struct_field_store`
- `compile_clang_backend_nested_struct_subfield_store`
- `compile_clang_backend_struct_heap_string_drop`
- `compile_clang_backend_struct_heap_string_retain`
- `compile_clang_backend_struct_heap_string_field_replace`
- `compile_clang_backend_struct_heap_string_interprocedural`
- `compile_clang_backend_branch_copy`
- `compile_clang_backend_struct_branch_copy`

Prochaines tranches nécessaires pour remplacer FASM :

1. étendre l'ABI runtime/stdlib LLVM `zeta_rt_*` après les six briques
   `io.*` déjà livrées et la première brique `strings.*`
   (`@zeta_rt_io_write_string(ptr, i64, i1)`,
   `@zeta_rt_io_write_int(i32, i1)`, `@zeta_rt_io_write_bool(i1, i1)`,
   `@zeta_rt_io_write_byte(i8, i1)`, `@zeta_rt_io_write_char(i32, i1)` et
   `@zeta_rt_io_write_double(double, i1)`,
   `@zeta_rt_string_length_bytes(ptr, i64)`,
   `@zeta_rt_string_is_empty(ptr, i64)`,
   `@zeta_rt_strings_view(ptr, i64, i32, i32)`,
   `@zeta_rt_strings_view_is_valid(ptr)`,
   `@zeta_rt_strings_decode_at_byte(ptr, i64, i32)`,
   `@zeta_rt_strings_next_byte_offset(ptr, i64, i32)`,
   `@zeta_rt_strings_index_of(ptr, i64, ptr, i64)`,
   `@zeta_rt_string_from_bool(i1)`, `@zeta_rt_string_from_int(i32)`,
   `@zeta_rt_string_from_byte(i8)`, `@zeta_rt_string_from_char(i32)`,
   `@zeta_rt_string_from_double(double)`) utilisées par `io.print`/
   `io.println(String)`, `io.printInt`/`io.printlnInt`, `io.printBool`/
   `io.printlnBool`, `io.printByte`/`io.printlnByte`, `io.printChar`/
   `io.printlnChar`, `io.printDouble`/`io.printlnDouble`, `String.lengthBytes`,
   `String.isEmpty`, `strings.view`,
   `strings.viewIsValid`, `strings.decodeAtByte`, `strings.nextByteOffset`,
   `strings.indexOf`/`strings.contains`, `String(Bool)`, `String(Int)`,
   `String(Byte)`, `String(Char)` et `String(Double)` ; cibler ensuite
   les autres primitives `strings.*` ou les conversions générales vers `String` encore
   abaissées de façon spécialisée ;
2. fait partiel : `--build-library --backend=clang` produit désormais des objets
   bibliothèque LLVM pour les modules simples (`LlvmIrCodeGenerator::generateObject`,
   `clang -c`, `.zti` + `.ll` + `.o`, test `compile_clang_backend_build_library`),
   et `--backend=clang` relie les exécutables aux objets précompilés LLVM installés
   depuis le cache de bibliothèques (`compile_clang_backend_shared_library_cache`),
   et `--build-stdlib --backend=clang` précompile les modules stdlib simples en
   `.zti`/`.ll`/`.o` avec manifeste (`compile_clang_backend_build_stdlib`) ;
   poursuivre avec la vraie stdlib complète, ses modules génériques/agrégats et
   les modules locaux non précompilés ;
3. choisir le support ou le rejet final pour globals agrégats, tableaux dans
   structs, `Box`, `Vec`, enums et gros agrégats ;
4. ajouter une matrice exemples + stdlib en `--backend=clang`, puis inverser le
   backend par défaut lorsque cette matrice est verte.

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
  via la frontière runtime interne `@zeta_rt_io_write_string(ptr, i64, i1)`, qui
  centralise `write(1, ptr, len)` et l'écriture conditionnelle du newline, avec
  comparaison stdout FASM.
- fait : `--backend=clang` couvre `io.printInt`/`io.printlnInt` directs via la
  frontière runtime interne `@zeta_rt_io_write_int(i32, i1)`, qui centralise le
  choix du format `printf` avec ou sans newline, avec comparaison stdout FASM.
- fait : `--backend=clang` couvre `io.printBool`/`io.printlnBool` directs via
  la frontière runtime interne `@zeta_rt_io_write_bool(i1, i1)`, qui centralise la
  sélection `true`/`false`, l'appel `write` et l'écriture conditionnelle du newline,
  avec comparaison stdout FASM.
- fait : `--backend=clang` couvre `String(Bool)` via la frontière runtime interne
  `@zeta_rt_string_from_bool(i1)`, qui sélectionne la paire statique `true`/`false`
  sans allocation, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `String(Int)` via la frontière runtime interne
  `@zeta_rt_string_from_int(i32)`, qui convertit en décimal signé dans un buffer
  heap propriétaire compatible `drop`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `String(Byte)` via la frontière runtime interne
  `@zeta_rt_string_from_byte(i8)`, qui effectue une extension non signée puis
  délègue à `@zeta_rt_string_from_int(i32)`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `String(Char)` via la frontière runtime interne
  `@zeta_rt_string_from_char(i32)`, qui encode le codepoint en UTF-8 1-4 octets
  dans un buffer heap propriétaire compatible `drop`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `String(Double)` via la frontière runtime interne
  `@zeta_rt_string_from_double(double)`, qui centralise le formatage `%g` via
  `snprintf`, copie le résultat dans un buffer heap propriétaire compatible `drop`,
  avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `io.printByte`/`io.printlnByte` directs via
  la frontière runtime interne `@zeta_rt_io_write_byte(i8, i1)`, qui centralise
  l'extension non signée, le choix du format `printf` avec ou sans newline et
  l'appel varargs, avec comparaison stdout FASM.
- fait : `--backend=clang` couvre `io.printChar`/`io.printlnChar` directs via
  la frontière runtime interne `@zeta_rt_io_write_char(i32, i1)`, qui centralise
  l'encodage UTF-8 1-4 octets, l'appel `write` et le newline conditionnel, avec
  comparaison stdout FASM.
- fait : `--backend=clang` couvre `io.printDouble`/`io.printlnDouble` directs via
  la frontière runtime interne `@zeta_rt_io_write_double(double, i1)`, qui
  centralise `printf("%g")`, le format avec/sans newline, `Double` en `double`,
  constantes/slots locaux et `fneg` unaire sur formats stables comparés à FASM.
- fait : `--backend=clang` couvre `String.lengthBytes` via la frontière runtime
  interne `@zeta_rt_string_length_bytes(ptr, i64)`, qui centralise le passage
  longueur `{ ptr, i64 } -> i32`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `String.isEmpty` via la frontière runtime
  interne `@zeta_rt_string_is_empty(ptr, i64)`, qui centralise le prédicat
  `len == 0`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `strings.view` via la frontière runtime interne
  `@zeta_rt_strings_view(ptr, i64, i32, i32)`, qui centralise les bornes, le calcul
  du pointeur et le sentinelle `{ null, 0 }`; `strings.viewIsValid` passe par la
  frontière interne `@zeta_rt_strings_view_is_valid(ptr)`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `strings.decodeAtByte` via la frontière runtime
  interne `@zeta_rt_strings_decode_at_byte(ptr, i64, i32)`, qui centralise le
  décodage UTF-8 direct et les rejets d'offset invalides, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre `strings.nextByteOffset` et `IrStringNextOffset`
  via la frontière runtime interne `@zeta_rt_strings_next_byte_offset(ptr, i64, i32)`,
  qui réutilise le décodage UTF-8 et centralise l'avance 1/2/3/4 octets.
- fait : `--backend=clang` couvre `strings.indexOf` et `strings.contains` via la
  frontière runtime interne `@zeta_rt_strings_index_of(ptr, i64, ptr, i64)`, qui
  centralise les vues invalides, l'aiguille vide, la borne de recherche et la
  boucle `memcmp`; `contains` reste un test `index >= 0`, avec exécution Clang et FASM.
- fait : `--build-library --backend=clang` produit un objet bibliothèque LLVM pour
  un module simple : l'IR objet ne définit pas `main`, `clang -c` produit le `.o`,
  et les artefacts publiés incluent `.zti`, `.ll` et `.o`.
- fait : `--backend=clang` relie un exécutable consommateur contre des bibliothèques
  précompilées LLVM installées : les appels externes sont déclarés dans l'IR,
  les `.o` de dépendances sont copiés dans `<app>.modules`, puis passés au lien
  final `clang`, avec exécution du binaire résultant.
- fait : `--build-stdlib --backend=clang` précompile une stdlib simple côté LLVM :
  chaque module produit `.zti`, `.ll` et `.o`, le manifeste est écrit, les sources
  peuvent ensuite être absentes, et un exécutable Clang consomme le cache `precompiled`.
- fait : `--backend=clang` couvre les `pub val String` globales en les émettant
  comme `{ ptr, i64 } zeroinitializer`, puis en les initialisant dans `@main`; les
  lectures globales réutilisent `load { ptr, i64 }`, `String.lengthBytes` passe par
  `@zeta_rt_string_length_bytes`, avec exécution Clang et stdout vérifié.
- fait : `--backend=clang` couvre les `pub val Double` globales en les émettant
  comme `global double 0.000000e+00`, puis en les initialisant dans `@main`; les
  lectures globales réutilisent `load double`, avec `io.printlnDouble`, comparaison
  `fcmp`, stdout et code retour vérifiés.
- fait : `--backend=clang` couvre les `pub val Byte` globales en les émettant
  comme `global i8 0`, puis en les initialisant dans `@main`; les lectures globales
  réutilisent `load i8`, avec `io.printlnByte`, conversion `Int(Byte)`, stdout et
  code retour vérifiés.
- fait : `--backend=clang` couvre les `pub val Char` globales en les émettant
  comme `global i32 0`, puis en les initialisant dans `@main`; les lectures globales
  réutilisent `load i32`, avec `io.printlnChar`, conversion `Int(Char)`, stdout et
  code retour vérifiés.
- fait : `--backend=clang` couvre les `pub val` structs composées de types LLVM
  déjà supportés en les émettant comme `global { ... } zeroinitializer`, puis en
  les initialisant dans `@main`; les lectures globales réutilisent `load { ... }`
  et les champs restent extraits par `extractvalue`.
- fait : `--backend=clang` couvre les opérations arithmétiques `Double`
  `+`/`-`/`*`/`/` via `fadd`/`fsub`/`fmul`/`fdiv`, et les comparaisons ordonnées
  via `fcmp o*`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre les `struct` locaux simples en lecture :
  types agrégats LLVM littéraux, construction `insertvalue`, slots `alloca`/`store`/
  `load`, et lecture de champ `extractvalue`, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre les mutations de champs de `struct` locaux
  simples par `load` de l'agrégat, `insertvalue` du champ remplacé, puis `store`
  de l'agrégat complet, avec exécution Clang et FASM.
- fait : `--backend=clang` couvre les paramètres, retours et appels de fonctions
  portant des `struct` simples, en émettant directement les signatures LLVM en
  agrégats littéraux (`{ i32, i32 }`), avec exécution Clang et FASM.
- fait : `--backend=clang` couvre les `struct` locaux mixtes contenant
  `Bool`/`Byte`/`Char`/`Double`/`String`, avec agrégat LLVM littéral
  `{ i1, i8, i32, double, { ptr, i64 } }`, comparaisons `Byte`/`Char` par `icmp`,
  et exécution Clang/FASM.
- fait : `--backend=clang` propage l'ownership des chaînes heap contenues dans
  des structs à travers paramètres, appels et retours de fonctions, avec retain
  sur les copies, drops conditionnels, `free` final, et comparaison stdout/code
  retour Clang/FASM.


## Critères de sortie de la migration FASM -> LLVM

- `zeta source.zeta -o programme` utilise LLVM/Clang par défaut.
- `--backend=fasm` reste disponible uniquement comme fallback legacy explicite.
- Les exemples non triviaux et la stdlib se compilent/exécutent via Clang dans CTest.
- Les modules séparés, `.zti`, objets runtime et stdlib précompilée sont produits
  et reliés sans assembler FASM dans le chemin LLVM.
- Les tests de comparaison FASM/Clang restent présents pour les zones où FASM sert
  encore d'oracle, mais les nouveaux développements peuvent être validés avec LLVM
  comme cible de référence.
- Les diagnostics LLVM restants ne couvrent que des limitations documentées et
  assumées, pas des morceaux nécessaires au développement quotidien de Zeta.

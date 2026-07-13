# Conception des énumérations et de `Option[T]`

Les énumérations de Zeta sont des unions discriminées nominales. Chaque valeur
contient exactement une variante active, identifiée par un discriminant, et la
charge utile éventuelle de cette variante. Elles portent `Option[T]` et les APIs
d'accès sûres de la bibliothèque standard.

L'implémentation couvre les énumérations non génériques et génériques, leur
construction, la correspondance exhaustive et `Option[T]` dans `collections`.

## Déclaration et construction

Une variante possède zéro ou plusieurs champs nommés :

```zeta
enum Resultat {
    Valeur(value: Int)
    Erreur(code: Int, message: String)
    Absent
}

val valeur: Resultat = Resultat.Valeur(value: 42)
val erreur: Resultat = Resultat.Erreur(code: 5, message: "invalide")
val absent: Resultat = Resultat.Absent
```

La qualification par le nom du type est toujours obligatoire. Elle évite de
placer toutes les variantes dans l'espace de noms courant et reste non ambiguë
avec les imports. Les parenthèses appartiennent à la déclaration et à la
construction des variantes avec charge utile ; une variante vide n'en utilise
pas.

Comme pour les structures, l'ordre des arguments n'est pas significatif à la
construction, mais chaque champ doit apparaître exactement une fois. Il n'existe
pas de forme positionnelle implicite.

Une déclaration générique reprend la syntaxe des structures :

```zeta
enum Option[T] {
    Some(value: T)
    None
}

val present: Option[Int] = Option[Int].Some(value: 42)
val absent: Option[Int] = Option[Int].None
```

La première version exige les arguments de type sur le constructeur générique.
Leur inférence depuis le type attendu pourra être ajoutée séparément sans changer
le modèle de données.

Les mots `enum` et `match` deviennent réservés. Une variante doit commencer par
un identifiant ordinaire ; sa casse n'a pas de signification sémantique.

## Correspondance exhaustive

`match` est une expression et évalue son opérande une seule fois :

```zeta
def valeurOuZero(resultat: Resultat): Int = match (resultat) {
    Resultat.Valeur(value) => value
    Resultat.Erreur(code, message) => 0
    Resultat.Absent => 0
}
```

Dans un motif, les noms entre parenthèses lient les champs dans l'ordre de leur
déclaration. `_` ignore un champ sans créer de liaison :

```zeta
match (resultat) {
    Resultat.Valeur(value) => value
    Resultat.Erreur(code, _) => code
    Resultat.Absent => -1
}
```

Chaque variante doit apparaître exactement une fois. La première version ne
possède pas de motif générique couvrant plusieurs variantes : cette restriction
rend les ajouts de variantes visibles à tous les sites de correspondance. Les
gardes, motifs imbriqués et alternatives de motifs sont reportés.

Toutes les branches doivent produire le même type, avec les mêmes règles de type
attendu que `if`. Les liaisons d'une branche ont une portée limitée à son
expression. Une variante ou un champ inconnu, un nombre incorrect de liaisons,
une variante répétée et une correspondance incomplète sont des erreurs de
compilation. Le diagnostic d'exhaustivité énumère les variantes manquantes.

## Représentation mémoire et ABI

Le discriminant est un entier non signé de 32 bits placé à l'offset zéro. Les
variantes reçoivent les valeurs `0`, `1`, ... dans leur ordre de déclaration.
Cet ordre fait donc partie de l'ABI d'une énumération exportée.

La charge utile commence au premier offset compatible à la fois avec les quatre
octets du discriminant et avec l'alignement maximal des variantes :

```text
payload_offset = align_up(4, payload_alignment)
alignment      = max(4, payload_alignment)
size           = align_up(payload_offset + max_payload_size, alignment)
```

La charge de chaque variante utilise la même disposition que les champs d'une
structure. `max_payload_size` et `payload_alignment` sont les maxima de toutes
les variantes. Une enum ne contenant que des variantes vides occupe quatre
octets et est alignée sur quatre octets.

Par exemple :

```zeta
enum Exemple {
    Vide
    Octet(value: Byte)
    Texte(value: String)
}
```

`Exemple` est alignée sur 8 octets, sa charge commence à l'offset 8 et sa taille
totale est 24 octets. Les octets de remplissage et la partie inactive de la
charge n'ont aucune valeur observable.

Le discriminant et la disposition sont conservés après monomorphisation. Deux
instances d'une enum générique sont des types distincts et peuvent avoir des
tailles différentes. Une valeur dont le discriminant n'identifie aucune variante
ne peut pas être créée par du code Zeta sûr ; les interfaces natives devront
respecter cette invariance.

La limite ABI actuelle de 16 octets pour les retours d'agrégats devra être
appliquée aux enums comme aux structures tant que la convention d'appel ne sait
pas retourner de plus grands agrégats.

## Généricité et récursion

Les paramètres de type peuvent être utilisés dans n'importe quel champ de
variante. Une instance est créée à la demande, mise en cache par liste
d'arguments et reçoit une disposition calculée après substitution complète :

```zeta
enum Option[T] {
    Some(value: T)
    None
}
```

Une enum ne peut pas se contenir directement, car sa taille serait infinie. Une
récursion indirecte via un propriétaire de taille connue reste valide :

```zeta
enum Liste {
    Cons(value: Int, tail: Box[Liste])
    Nil
}
```

Les cycles de disposition entre structures et enums doivent être diagnostiqués
en citant les types impliqués. La monomorphisation réutilise les contraintes et
le nommage existants ; elle ne doit pas produire deux définitions pour la même
instance dans un module.

`Option[T]` est désormais un type générique de base, mais son nom peut être
reconnu sans traitement intrinsèque. Sa sémantique provient uniquement de la
déclaration générique ci-dessus.

## Propriété, copie et destruction

Une enum est `Copy` si et seulement si toutes les charges utiles de toutes ses
variantes sont `Copy`. Elle est `Equatable` si les charges utiles le sont toutes ;
l'égalité compare d'abord le discriminant, puis uniquement les champs de la
variante active. Les autres contraintes ne sont pas déduites.

Un `match` inspecte son opérande par valeur. Il copie une enum `Copy` et déplace
une enum non `Copy`, exactement comme un argument de fonction. Dans la branche
active, une liaison copie ou déplace son champ selon le type de celui-ci. Un champ
lié par `_` est détruit s'il possède une ressource. La fin de branche détruit les
liaisons encore actives, y compris lors d'un `return`, `break` ou `continue`.

Cette règle permet notamment d'extraire un `Box[T]` ou une `String` d'une enum
sans double destruction. Une correspondance sur `&Enum` et des motifs par
référence seront ajoutés plus tard ; ils ne doivent pas être simulés par une
copie implicite.

La destruction d'une enum lit son discriminant puis détruit récursivement les
champs de la seule variante active. L'IR doit donc disposer d'une opération de
branchement sur discriminant, utilisable à la fois par `match` et par `drop`.

## AST, typage et IR

Le modèle syntaxique doit introduire :

- `EnumType`, contenant le nom, les paramètres de type, les variantes et la
  disposition calculée ;
- un nouveau genre `Enum` dans `ValueType` ;
- `EnumExpr`, contenant l'instance, l'index de variante et les expressions de
  champs remises dans l'ordre de déclaration ;
- `MatchExpr`, contenant l'opérande et une branche résolue par variante.

Le parser enregistre le nom du type avant d'analyser ses charges afin d'autoriser
la récursion à travers `Box`. Le typage résout toutes les variantes et vérifie
l'exhaustivité avant la production de l'IR.

Dans l'IR, une construction réserve la taille totale, écrit le discriminant et
matérialise les champs de la variante active à leurs offsets. Un `match` charge le
discriminant une fois, branche vers un bloc par variante et fusionne la valeur de
résultat dans une destination commune. Le backend n'a pas à connaître les noms
des variantes.

## Interfaces de modules

Les signatures `.zti` sérialisent désormais le nom et les arguments d'une enum.
Les corps génériques incorporés permettent notamment de reconstruire `Option[T]`
et de monomorphiser les fonctions de `collections` côté consommateur.

L'export autonome de définitions publiques complètes — variantes, champs et
disposition sans source générique incorporée — reste prévu avec les interfaces
publiques complètes.

## Découpage d'implémentation — terminé

1. lexer, déclaration non générique, calcul de disposition et diagnostics de
   définition ;
2. construction qualifiée, typage, copie et passage par valeur ;
3. `match` exhaustif et génération du branchement IR ;
4. destruction conditionnelle des charges possédées ;
5. égalité et contraintes intégrées ;
6. monomorphisation des enums génériques ;
7. ajout de `Option[T]`, puis conversion de `collections.first`, `second` et `at`.

Chaque étape ajoute des tests de compilation, d'exécution et de rejet. Les cas
ABI couvrent au minimum les variantes vides, les alignements `Byte`/`Int`/
`String`, les charges de 8 et 16 octets, les arguments et retours de fonctions.
Les tests de propriété couvrent `String`, `Box[T]`, les déplacements, les champs
ignorés et toutes les sorties anticipées.

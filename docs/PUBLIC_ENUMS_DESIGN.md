# Énumérations publiques et interfaces `.zti`

Cette extension de la priorité 4 rend une énumération autonome dans une
interface persistante. Une déclaration `pub enum` exporte son identité nominale,
ses variantes et sa disposition ; une enum sans `pub` reste privée au module.

## Syntaxe et identité

Un consommateur qualifie le type par son module. La variante reste qualifiée par
le type :

```zeta
import protocol

val state: protocol.State = protocol.State.Ready
val result: protocol.Result[Int] =
    protocol.Result[Int].Ok(value: 42)
```

Les motifs suivent la même identité :

```zeta
match (result) {
    protocol.Result[Int].Ok(value) => value
    protocol.Result[Int].Error(_) => 0
}
```

Deux enums homonymes déclarées par deux modules restent distinctes. Les noms et
l'ordre des variantes font partie de l'interface publique.

## Enregistrement `.zti`

Chaque définition publique conserve :

- son nom, ses paramètres de type, sa taille et son alignement ;
- l'offset commun de la charge utile ;
- les variantes dans l'ordre de leur discriminant ;
- pour chaque variante, sa taille et son alignement de charge ;
- pour chaque champ, son nom, son type encodé et son offset dans la charge.

Le décodeur crée la définition nominale avant de lire ses charges. Cela autorise
les récursions de taille finie via `Box` et permet aux signatures suivantes de
réutiliser exactement le même objet de type. Les instances génériques sont
reconstruites par le cache commun de `instantiateEnumType`.

## Chargement et résolution

La lecture légère des imports charge les interfaces des dépendances avant le
parseur complet. Le parseur reçoit donc deux tables qualifiées, structures et
énumérations. Elles servent aux annotations, aux constructeurs et aux motifs.

Une interface publique ne peut exposer directement ou dans une charge utile un
type privé. Cette validation descend dans les tableaux, références, slices,
`Box`, `Vec`, structures, enums et arguments génériques.

`Option[T]` reste le type générique de base disponible sans import. Une enum
publique déclarée par un module suit, elle, les règles normales de qualification
et de distribution.

## Compatibilité

Les enums ont introduit le format `.zti` 6 et le cache de modules 9. La
représentation structurée des génériques porte les versions courantes à 7 et 10.
Une interface tronquée, une variante dupliquée, un champ hors charge ou une
disposition incohérente est rejeté avant l'analyse du consommateur.

La validation d'intégration compile une enum ordinaire et une enum générique,
exécute leurs constructions et leurs `match`, puis répète la compilation après
suppression du source du module producteur.

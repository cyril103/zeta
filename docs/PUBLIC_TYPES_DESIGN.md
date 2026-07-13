# Types publics et interfaces `.zti`

La priorité 4 étend les interfaces persistantes au-delà des fonctions et valeurs.
Une structure déclarée `pub struct` devient un type nominal exporté par son module.
Une structure non publique reste utilisable uniquement dans son fichier source.

## Identité et syntaxe

Un consommateur désigne un type importé avec son nom qualifié, par exemple
`geometry.Point`. La construction suit la même règle :

```zeta
import geometry

val point: geometry.Point = geometry.Point { x: 19, y: 23 }
```

L'identité nominale inclut le module. Deux structures publiques portant le même
nom dans deux modules restent donc distinctes. Les champs d'une structure
publique sont visibles pour cette première version ; une visibilité indépendante
des champs pourra être ajoutée ultérieurement sans modifier leur disposition.

## Contenu de l'interface

`ModuleInterface` possède une table de structures publiques en plus de sa table
de symboles. Chaque entrée `.zti` conserve :

- le nom et les paramètres de type ;
- la taille et l'alignement de la définition concrète ;
- pour chaque champ, son nom, son type encodé et son offset ABI.

Les types de champs peuvent référencer des primitives, paramètres génériques,
types propriétaires et autres structures publiques qualifiées. Une référence à
un type privé dans la disposition d'un type public est rejetée, car le
consommateur ne pourrait pas reconstruire cette disposition.

## Ordre de chargement

Les lignes `import` restent obligatoirement en tête du fichier. Le chargeur peut
donc effectuer une première lecture légère, charger les `.zti` des dépendances,
puis fournir leurs tables de types au parseur complet du module consommateur.
Cette phase conserve la détection de cycles existante en enregistrant le module
en cours avant de descendre dans ses dépendances.

Le parseur résout ensuite `module.Type` vers l'objet nominal provenant de
l'interface. L'analyse sémantique et l'IR manipulent ainsi la même disposition
que le producteur, sans source et sans recréer un type homonyme local.

## Compatibilité

Les structures ont introduit le format `.zti` 5 et le cache de module 8, puis les
enums publiques les versions 6 et 9. La représentation structurée des génériques
porte les versions courantes à 7 et 10. La désérialisation vérifie les tailles,
alignements, offsets, doublons et références de types avant d'exposer
l'interface. Une version antérieure reste diagnostiquée comme incompatible
plutôt que devinée.

La livraison couvre la syntaxe et la visibilité, la table de types en mémoire,
la sérialisation versionnée et la résolution qualifiée côté consommateur. Le test
d'intégration compile des structures ordinaires et génériques une première fois
depuis les sources, puis une seconde fois avec uniquement l'interface et l'objet
du module producteur.

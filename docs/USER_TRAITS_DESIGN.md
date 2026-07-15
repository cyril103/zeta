# Traits utilisateur statiques

## Incrément livré

Le premier contrat définissable par l'utilisateur est un trait marqueur sans
état :

```zeta
pub trait Answer {}

impl Answer for Int {}

def keep[T: Answer + Copy](value: T): T = value
```

Le trait étend le système de contraintes existant. Il ne crée aucune valeur à
l'exécution et n'ajoute aucune indirection : les fonctions restent
monomorphisées pour chaque type concret.

## Résolution et cohérence

Un trait local est canonisé avec son module (`capabilities.Answer`). Un trait
importé est nommé explicitement dans une implémentation ou une contrainte, par
exemple `capabilities.Answer`.

Une implémentation `impl Trait for Type {}` est acceptée lorsque le module possède
le trait ou le type nominal. Cette règle autorise le propriétaire d'un trait à
l'implémenter pour un builtin, et le propriétaire d'un type à adopter un trait
importé. Elle interdit deux modules tiers de créer des implémentations
concurrentes. Une deuxième implémentation de la même paire trait/type est rejetée.

Un trait privé ne peut pas contraindre une fonction publique. Une implémentation
portant sur un type privé reste locale et n'est pas exportée.

## Interfaces et compilation séparée

Le format `.zti` 12 ajoute deux entrées :

```text
trait "capabilities.Answer"
implementation "capabilities.Answer" "<type encodé>"
```

Les déclarations de traits nécessaires à un corps générique entrent également
dans la fermeture des tokens génériques. Leur table passe à la version 4. Les
traits et implémentations participent à l'empreinte publique ; le cache de modules
26 invalide les objets antérieurs.

Le test intermodules couvre une implémentation définie par le propriétaire d'un
type dans un module distinct du trait, puis retire les sources et recompile le
consommateur depuis les seules paires `.zti` + `.o`.

## Limites et prochaine étape

Le corps d'un trait et d'une implémentation doit rester vide. Le prochain
incrément ajoutera les signatures de méthodes requises et leur résolution
statique dans un corps générique. Les méthodes par défaut, types associés,
objets de traits et vtables restent reportés.

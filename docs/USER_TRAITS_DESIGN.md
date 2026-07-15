# Traits utilisateur statiques

## Fondation

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

## Méthodes requises

Un trait peut déclarer des signatures sans corps avec le type spécial `Self` :

```zeta
trait CounterOps {
    def read(self: &Self): Int
    def add(self: &mut Self, amount: Int): Unit
}

impl CounterOps for Counter {
    def read(self: &Counter): Int = (*self).value
    def add(self: &mut Counter, amount: Int): Unit = {
        *self = Counter { value: (*self).value + amount }
    }
}
```

Chaque signature commence par `self: &Self` ou `self: &mut Self`. Le bloc `impl`
doit fournir exactement les méthodes du contrat avec les mêmes noms de
paramètres, types, mutabilité et retour après substitution de `Self`. Les
méthodes sont abaissées vers les méthodes statiques de structure existantes ; un
appel sur un type concret ne crée donc ni vtable ni allocation.

Les implémentations avec méthodes sont actuellement limitées aux structures
nominales. Les traits marqueurs restent utilisables avec les builtins et les
autres types concrets.

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

Le format `.zti` 13 sérialise le contrat et les méthodes fournies :

```text
trait "capabilities.Answer" 1
trait_method "answer" "I" 1 "self" "R(T(Self))"
implementation "capabilities.Answer" "<type encodé>" 1 "answer"
```

Les déclarations de traits nécessaires à un corps générique entrent également
dans la fermeture des tokens génériques. Leur table passe à la version 4. Les
traits, signatures et implémentations participent à l'empreinte publique ; le
cache de modules 27 invalide les objets antérieurs.

Le test intermodules couvre une implémentation définie par le propriétaire d'un
type dans un module distinct du trait, puis retire les sources et recompile le
consommateur depuis les seules paires `.zti` + `.o`.

## Limites et prochaine étape

Un appel concret comme `counter.read()` est résolu. Le prochain incrément devra
résoudre `value.read()` lorsque `value` possède le type paramétrique
`T: CounterOps`, puis sélectionner la méthode de l'implémentation pendant la
monomorphisation. Les méthodes par défaut, types associés, implémentations de
méthodes sur enums, objets de traits et vtables restent reportés.

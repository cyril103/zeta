# Conception des méthodes utilisateur

Les méthodes utilisateur sont abaissées vers des fonctions ordinaires. Une
déclaration associe le nom du type et celui de la méthode, puis rend son receveur
explicite dans la signature :

```zeta
struct Counter { value: Int }

def Counter.read(self: &Counter): Int = (*self).value
def Counter.reset(self: &mut Counter): Unit = {
    *self = Counter { value: 0 }
}
```

Le préfixe doit désigner une structure non générique déclarée dans le même
module. Le premier paramètre doit se nommer `self` et être exactement `&Type` ou
`&mut Type`. Cette contrainte distingue les méthodes inhérentes des futures
méthodes d'extension et rend la mutabilité visible dans la signature publique.

## Résolution et emprunt

Pour `value.method(arguments)`, l'analyse sémantique résout la méthode à partir de
l'identité nominale de la structure. Un receveur possédé doit être un identifiant
adressable. Le compilateur construit alors un emprunt temporaire partagé ou
mutable selon le type de `self`, puis vérifie les arguments restants comme pour
un appel de fonction.

Un receveur mutable exige une liaison `var`. Un emprunt mutable existant bloque
les deux catégories de méthodes et un emprunt partagé existant bloque une méthode
mutable. L'emprunt automatique ne survit pas à l'appel. Un receveur déjà référencé
doit actuellement avoir exactement le type de référence déclaré par la méthode ;
les réemprunts implicites restent reportés.

## IR, ABI et liaison

L'AST typé conserve le nom qualifié de la fonction résolue. La génération d'IR
émet un `IrCall` ordinaire dont le premier argument est l'adresse du receveur.
L'ABI des références ne change donc pas. Le point dans `Type.method` est remplacé
par un caractère sûr dans le symbole assembleur afin que les méthodes locales et
importées utilisent le même nom de liaison déterministe.

Une méthode publique reste un export de fonction avec le nom `Type.method` et son
premier type de paramètre dans le fichier `.zti`. Le consommateur reconstruit la
table des méthodes depuis cette signature, sans avoir besoin des sources. Le
format d'interface passe à `9` et le cache de modules à `19`.

## Limites du premier jalon

Ce jalon couvre les structures non génériques et les receveurs identifiants. Les
méthodes d'extension, les structures génériques, les enums, les receveurs
temporaires et la syntaxe de réemprunt seront traités séparément. Une méthode peut
déjà utiliser toutes les opérations autorisées sur ses paramètres ; l'accès plus
ergonomique aux champs possédés à travers `self` sera développé avec `Stack[T]`.

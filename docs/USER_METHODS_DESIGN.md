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

Le préfixe doit désigner une structure déclarée dans le même module. Le premier
paramètre doit se nommer `self` et être exactement `&Type` ou `&mut Type`. Pour
une structure générique, la méthode reprend exactement les paramètres du type,
dans le même ordre, et son receveur les conserve. Cette contrainte distingue les
méthodes inhérentes des méthodes d'extension et rend la mutabilité visible dans
la signature publique.

## Extensions de module

Le mot-clé `extend` déclare explicitement une méthode apportée par un module :

```zeta
pub extend def Vec.appendTwice[T](self: &mut Vec[T], first: T, second: T): Unit = {
    self.push(first)
    self.push(second)
}
```

Le premier jalon d'extension cible `Vec[T]`, avec receveur `&Vec[T]` ou
`&mut Vec[T]`. Les paramètres génériques sont inférés depuis le receveur et les
arguments comme pour une fonction générique. Une extension est visible seulement
si son module est importé. Deux imports fournissant le même nom pour `Vec`
produisent un diagnostic d'ambiguïté au lieu d'un choix dépendant de l'ordre.

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
table des méthodes depuis cette signature, sans avoir besoin des sources.
L'interface marque explicitement les exports d'extension et les corps génériques
réduits conservent le token `extend`. Le format d'interface passe à `10`, les
tokens génériques à `2` et le cache de modules à `21`.

## Limites du premier jalon

Les méthodes inhérentes couvrent les structures ordinaires et génériques, et les
extensions couvrent actuellement `Vec[T]`. Les extensions de types nominaux
importés, les enums, les receveurs temporaires et la syntaxe générale de
réemprunt seront traités séparément. Une extension `&Vec[T]` peut créer une `Slice[T]`,
et une extension `&mut Vec[T]` une `SliceMut[T]` : `sequences` utilise ce chemin
pour exposer `values.sort()` sans dupliquer son algorithme.

Une méthode de structure générique peut aussi projeter un champ `Vec[T]` depuis
`(*self)`. L'IR conserve alors la référence du propriétaire, son type concret et
l'indice du champ. Le backend ajoute l'offset du champ à l'adresse reçue ; aucune
valeur `Vec` intermédiaire n'est chargée. `collections.Stack[T]` valide ce chemin
pour les propriétés, `push` et `pop`, y compris depuis une interface précompilée.

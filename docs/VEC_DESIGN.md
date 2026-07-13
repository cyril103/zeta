# Conception de `Vec[T]`

`Vec[T]` est le premier conteneur dynamique possédé de Zeta. Sa représentation
ABI occupe trois mots machine contigus :

```text
offset 0   adresse du premier élément, ou 0 pour une capacité nulle
offset 8   longueur, en nombre d'éléments initialisés
offset 16  capacité, en nombre d'éléments alloués
```

Les invariants sont `longueur <= capacité`, une adresse nulle si et seulement si
la capacité est nulle, et des éléments initialisés exactement dans l'intervalle
`[0, longueur)`. La représentation vide canonique est donc `{0, 0, 0}`. La taille
d'une allocation est `capacité * sizeof(T)` ; les multiplications et additions
de capacité sont contrôlées avant tout appel au système.

La première version refuse les types d'éléments de taille nulle ; leur prise en
charge nécessitera une convention de capacité sans allocation distincte.

## Syntaxe et API

La construction vide s'écrit `Vec[T]()`. Le type expose des propriétés en lecture
seule `length`, `capacity` et `isEmpty`, puis les opérations intrinsèques suivantes :

```zeta
values.push(value)
values.pop()
values.get(index)
values.set(index, value)
values.reserve(additional)
values.clear()
values.asSlice()
values.asSliceMut()
```

`push`, `set`, `reserve` et `clear` exigent un receveur `var`. `asSliceMut` exige
également un receveur mutable et produit un emprunt exclusif. `pop` renvoie un
`Option[T]` en déplaçant le dernier élément, tandis que `get` renvoie un
`Option[T]` et n'est disponible que pour `T: Copy`. `set` déplace sa nouvelle
valeur, détruit l'ancienne et termine le processus avec le code `101` si l'indice
est hors limites. Les méthodes sont abaissées directement en IR et ne sont donc
pas soumises à la limite ABI de 16 octets des retours de fonctions ordinaires.

## Allocation et croissance

Un vecteur vide n'alloue rien. `reserve(additional)` garantit une capacité d'au
moins `longueur + additional`. Si une croissance est nécessaire, la nouvelle
capacité est le maximum entre cette borne, `4` et le double de la capacité
actuelle. Le backend alloue le nouveau bloc avec `mmap`, copie ou déplace les
éléments actifs, puis libère l'ancien bloc avec `munmap`.

Tout dépassement de taille, capacité impossible ou échec de `mmap` termine le
processus avec le code `105`. Cette règle est commune à toutes les opérations de
`Vec` et évite de laisser un propriétaire partiellement modifié. Une allocation
réussie est publiée dans la valeur seulement après le transfert complet.

## Propriété et destruction

`Vec[T]` est toujours un type non `Copy`, même si `T` est copiable. Un passage,
un retour, une affectation initiale ou un `push` par valeur transfère la propriété
et rend l'ancienne source déplacée. La destruction parcourt les éléments actifs
en ordre inverse, applique récursivement la destruction de `T`, puis libère le
bloc. `clear` effectue la même destruction sans libérer la capacité.

Une croissance transfère les octets de chaque élément vers le nouveau bloc sans
dupliquer leur propriété. L'ancien bloc est ensuite libéré sans détruire ses
anciens emplacements. `pop` décrémente la longueur avant de rendre la valeur afin
que le vecteur ne la détruise plus.

## Vues empruntées

`asSlice()` produit `{adresse, longueur}` sous la forme `Slice[T]` et
`asSliceMut()` sous la forme `SliceMut[T]`, sans allocation ni copie. Les règles
lexicales existantes des emprunts s'appliquent : aucun déplacement, changement de
taille ou accès mutable concurrent du vecteur n'est permis pendant la vie de la
vue. Une slice ne participe jamais à la libération du bloc.

## Découpage d'implémentation

La mise en œuvre conserve des commits vérifiables séparés : type et ABI, cycle
d'allocation, règles de déplacement et destruction des éléments, vues empruntées,
API complète, matrice de tests, puis intégration à la stdlib précompilée. Les
formats d'interface et de cache seront versionnés dès que `Vec[T]` pourra
apparaître dans une signature publique.

Le cycle propriétaire des éléments est désormais disponible : `push` transfère
ou retient la valeur selon son type, la croissance déplace les emplacements sans
les détruire, et `clear` comme la destruction finale parcourent les éléments en
ordre inverse. `clear` conserve le bloc alloué pour sa réutilisation.

Les méthodes `asSlice` et `asSliceMut` produisent maintenant les deux mots
`{adresse, longueur}` directement depuis le propriétaire. Elles participent au
suivi lexical des emprunts : toute opération susceptible de modifier ou déplacer
le vecteur est refusée jusqu'à la dernière utilisation de la vue.

L'API prévue est complète. `get` copie uniquement un élément `Copy`, `pop`
transfère le dernier élément après avoir réduit la longueur, et les deux rendent
le type de base `Option[T]`. `set` détruit l'ancien élément avant de transférer le
nouveau et conserve le code d'erreur de bornes `101`.

# Conception des slices Zeta

## Modèle

`Slice[T]` est une vue partagée et `SliceMut[T]` une vue mutable sur une suite
contiguë de valeurs `T`. Une slice n'alloue et ne possède jamais ses éléments.

Les deux types occupent 16 octets sur x86-64 :

```text
offset 0 : adresse du premier élément (u64)
offset 8 : nombre d'éléments (u64)
```

La longueur compte des éléments, jamais des octets. L'adresse peut être nulle
uniquement lorsque la longueur vaut zéro. Pour une slice non vide, elle est
alignée selon `T` et désigne au moins `longueur × taille(T)` octets accessibles.

## Mutabilité et emprunts

- `Slice[T]` autorise la lecture et peut être copiée ;
- `SliceMut[T]` autorise lecture et écriture et reste exclusive ;
- créer une slice prolonge l'emprunt de son tableau source jusqu'à la dernière
  utilisation de la slice ;
- aucune slice ne peut survivre à son stockage source ;
- la conversion de `SliceMut[T]` vers `Slice[T]` sera explicite tant que Zeta ne
  possède pas de coercions d'emprunt générales.

Les premières slices porteront sur tout un tableau fixe emprunté. Les intervalles
et sous-slices seront ajoutés ensuite, avec des bornes de début inclusives et de
fin exclusives.

## ABI

Une slice est transmise comme deux mots machine consécutifs, d'abord l'adresse,
puis la longueur. Un retour de slice utilise `rax` pour l'adresse et `rdx` pour la
longueur, comme `String`. Les objets natifs suivent la même convention.

L'indexation vérifie `0 <= index < longueur` avant de calculer
`adresse + index × taille(T)`. Un échec termine le programme avec le code `101`,
comme l'indexation des tableaux fixes.

## Étapes d'implémentation

1. types et syntaxe `Slice[T]` / `SliceMut[T]` — terminé ;
2. création depuis `&[T; N]` et `&mut [T; N]` sans allocation — terminé,
   avec `Slice(&tableau)` et `SliceMut(&mut tableau)` ;
3. passage, retour et indexation dans l'IR et l'ABI ;
4. sous-slices, puis utilisation par les buffers d'E/S.

# Conception de `Box[T]`

`Box[T]` sera le premier type propriétaire dynamique de Zeta. Il occupe un mot
machine contenant l'adresse d'une unique valeur `T` allouée sur le tas.

La mise en œuvre est volontairement découpée en étapes indépendantes :

1. type récursif et syntaxe `Box[T]` ;
2. état sémantique `disponible` ou `déplacé` ;
3. déplacement dans les déclarations et les appels ;
4. construction `Box(valeur)`, déréférencement et emprunts ;
5. instruction IR `drop` et destruction sur toutes les sorties de portée ;
6. allocation par `mmap` et libération par `munmap`.

Une `Box[T]` ne sera jamais implicitement copiable. Une opération par valeur
transférera son propriétaire et rendra l'ancien nom inutilisable. Les emprunts ne
transféreront pas la propriété.

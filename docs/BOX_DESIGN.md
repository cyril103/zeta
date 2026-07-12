# Conception de `Box[T]`

`Box[T]` sera le premier type propriétaire dynamique de Zeta. Il occupe un mot
machine contenant l'adresse d'une unique valeur `T` allouée sur le tas.

La mise en œuvre est volontairement découpée en étapes indépendantes :

1. type récursif et syntaxe `Box[T]` ;
2. état sémantique `disponible` ou `déplacé` — disponible pour les déclarations,
   avec fusion conservatrice des branches ;
3. déplacement dans les déclarations et les appels — terminé ;
4. construction `Box(valeur)`, déréférencement et emprunts `&*box` /
   `&mut *box` — terminé ;
5. instruction IR `drop` et destruction — disponible sur les sorties lexicales,
   `return`, `break`, `continue` et les fins d'itération ;
6. allocation par `mmap` et libération récursive par `munmap` — terminé.

Cette première version du modèle propriétaire est complète. Chaque construction
réalise une allocation de la taille exacte de `T`. Un échec d'allocation termine
le processus avec le code `102`. Tous les chemins de sortie détruisent les
propriétaires encore actifs, tandis qu'un déplacement transfère cette obligation.

Une `Box[T]` ne sera jamais implicitement copiable. Une opération par valeur
transférera son propriétaire et rendra l'ancien nom inutilisable. Les emprunts ne
transféreront pas la propriété.

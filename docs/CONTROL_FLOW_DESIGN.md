# Contrôle de flux `Unit` et `Never`

## Contrat de surface

Un `if` sans `else` est une instruction de type `Unit`. Il est accepté dans un
bloc lorsque sa valeur est ignorée, mais rejeté lorsqu'un type métier est attendu.
Un `if/else` reste une expression : ses deux branches doivent converger vers le
type attendu.

`Never` est strictement interne. Aucun token source ni encodage `.zti` ne permet
de le nommer ou de l'exporter.

## Terminaison et convergence

Un bloc sans expression finale vaut `Unit`, sauf si sa dernière instruction est
`return`, `break` ou `continue`; il est alors inféré `Never`. `Never` est compatible
avec tout type attendu parce que le chemin correspondant ne produit aucune valeur.
Dans un `if/else`, une branche `Never` laisse donc le type de l'autre branche
déterminer le résultat. Deux branches atteignables restent soumises aux règles de
types ordinaires.

Une garde sans `else` possède un chemin implicite qui ne fait rien. Un déplacement
de valeur possédée dans sa branche est donc refusé, sauf si l'état de déplacement
reste identique à celui du chemin implicite.

## Abaissement IR

Une garde produit un `IrUnit` logique, branche vers son label de fin lorsque son
prédicat est faux, puis émet sa branche d'effet. `Never` ne possède ni instruction
IR ni représentation ABI : les terminaux existants (`IrReturn`, `IrJump` et
`IrTailCall`) ferment directement le chemin. Le vérificateur d'IR continue ainsi
de contrôler la fermeture du graphe sans valeur fictive.

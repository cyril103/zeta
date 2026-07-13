# Représentation structurée des génériques dans `.zti`

Les interfaces actuelles copient le fichier source complet dès qu'un symbole
générique public existe. Le chargeur relance ensuite le lexer et le parseur sur
ce texte. Cette dépendance conserve les commentaires, la mise en forme et toutes
les ambiguïtés futures du lexer alors que l'interface a déjà son propre numéro de
version.

## Fermeture sémantique

Une fonction générique publique peut appeler un helper privé, lire une globale
privée ou construire un type privé dans son corps. Sérialiser uniquement sa
déclaration ne suffit donc pas sans calculer la fermeture transitive de toutes
ses dépendances.

La première représentation structurée conserve la syntaxe canonique du module
sous forme d'une suite de tokens. Elle élimine le texte brut et le lexer tout en
préservant exactement les déclarations nécessaires à l'analyse et à la
monomorphisation. Une réduction ultérieure pourra supprimer les déclarations qui
ne sont pas dans la fermeture des exports génériques.

## Format

Une interface qui exporte au moins un générique contient un bloc :

```text
generic_tokens 1 <nombre>
token <genre> <ligne> <colonne> "<texte>"
...
```

`1` est la version propre de cette représentation. Le genre correspond à un
`TokenKind` validé par le lecteur. Le texte est conservé avec l'échappement
standard des chaînes du format `.zti`; les positions servent uniquement aux
diagnostics. Le dernier élément doit être le token `End`, et aucun élément ne
peut le précéder après un premier `End`.

Le changement de format `.zti` protège la correspondance entre les valeurs de
`TokenKind` et le compilateur. Un bloc tronqué, une version inconnue, un genre
hors intervalle ou une terminaison invalide est rejeté avant le parseur.

## Production et chargement

Le chargeur source conserve les tokens produits lors de sa lecture initiale.
L'écriture d'une interface les ajoute uniquement si un export possède des
paramètres de type. Les commentaires et espaces ont déjà disparu à ce stade.

À la lecture, `InterfaceCodec` reconstruit directement les objets `Token`. Le
`ModuleLoader` fournit cette suite au parseur avec les tables de types importés,
sans reconstruire un fichier `.zeta` et sans appeler le lexer.

L'empreinte publique d'un module générique dépend de la représentation canonique
et non plus des octets du fichier source. Une modification de commentaire ne doit
donc plus invalider ses consommateurs, contrairement à une modification de token.

## Migration

Le champ textuel `generic_source` disparaît lors du passage au nouveau format ;
aucune double lecture ambiguë n'est autorisée. Le numéro du format `.zti`, celui
du cache de module et les tests de stdlib précompilée sont incrémentés ensemble.

Les tests vérifient notamment :

- la disparition de `generic_source` et du texte hexadécimal ;
- la compilation de `collections` et `vectors` sans leurs sources ;
- le rejet d'une version de tokens inconnue ou d'une suite sans `End` ;
- la stabilité de l'interface lors d'un changement limité aux commentaires.

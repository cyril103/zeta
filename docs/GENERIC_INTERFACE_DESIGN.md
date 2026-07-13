# Représentation structurée des génériques dans `.zti`

Avant le format `.zti` 7, les interfaces copiaient le fichier source complet dès
qu'un symbole générique public existait. Le chargeur relançait ensuite le lexer
et le parseur sur ce texte, en conservant commentaires, mise en forme et
ambiguïtés du lexer dans une interface pourtant versionnée.

## Fermeture sémantique

Une fonction générique publique peut appeler un helper privé, lire une globale
privée ou construire un type privé dans son corps. Sérialiser uniquement sa
déclaration ne suffit donc pas sans calculer la fermeture transitive de toutes
ses dépendances.

La première représentation structurée conserve la syntaxe canonique du module
sous forme d'une suite de tokens. Elle élimine le texte brut et le lexer tout en
préservant exactement les déclarations nécessaires à l'analyse et à la
monomorphisation. La fermeture ci-dessous supprime les déclarations qui ne sont
pas nécessaires aux exports génériques.

## Fermeture de déclarations

La réduction travaille sur les frontières syntaxiques du niveau global, sans
réinterpréter les expressions :

1. les lignes `import` de tête sont conservées afin que les noms qualifiés restent
   reconnus par le parseur ;
2. les déclarations globales sont découpées en suivant les profondeurs de
   parenthèses, crochets et accolades ;
3. les déclarations des exports génériques publics constituent les racines ;
4. tout identifiant rencontré dans une déclaration retenue qui correspond au nom
   d'une autre déclaration globale ajoute celle-ci à la fermeture ;
5. l'étape précédente est répétée jusqu'au point fixe ;
6. les imports et déclarations retenues sont réémis dans leur ordre original,
   séparés canoniquement, puis suivis d'un unique token `End`.

La recherche par identifiant est volontairement conservative. Un nom de champ ou
une variable locale homonyme peut retenir une déclaration globale supplémentaire,
mais aucune dépendance réelle ne doit être supprimée. Les commentaires, lignes
vides et déclarations globales sans lien avec les racines disparaissent.

Une racine générique absente du découpage est une erreur interne de production de
l'interface ; elle ne doit jamais produire silencieusement un bloc incomplet.

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

Le chargeur source conserve d'abord les tokens produits lors de sa lecture
initiale, calcule les exports génériques racines, puis remplace cette séquence par
sa fermeture réduite. L'écriture d'une interface ajoute cette fermeture uniquement
si elle n'est pas vide. Les commentaires, espaces et déclarations sans lien ont
déjà disparu à ce stade.

À la lecture, `InterfaceCodec` reconstruit directement les objets `Token`. Le
`ModuleLoader` fournit cette suite au parseur avec les tables de types importés,
sans reconstruire un fichier `.zeta` et sans appeler le lexer.

L'empreinte publique d'un module générique dépend de la fermeture canonique et
non plus des octets du fichier source. Une modification de commentaire ou d'une
déclaration exclue ne doit donc plus invalider ses consommateurs, contrairement à
une modification d'une déclaration retenue.

## Migration

Le champ textuel `generic_source` a disparu dans le format `.zti` 7 ; aucune
double lecture ambiguë n'est autorisée. Le cache de module est passé à la version
10 et les tests de stdlib précompilée valident le nouveau chemin.

Les tests vérifient notamment :

- la disparition de `generic_source` et du texte hexadécimal ;
- la compilation de `collections` et de la fixture interne
  `vec_generic_fixture` sans leurs sources ;
- le rejet d'une version de tokens inconnue ou d'une suite sans `End` ;
- la stabilité de l'interface lors d'un changement limité aux commentaires.

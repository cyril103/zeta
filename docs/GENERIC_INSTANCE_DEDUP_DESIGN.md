# Déduplication des instances génériques

## Problème

Une fonction générique importée est monomorphisée dans chaque module qui
l'appelle. Deux consommateurs de `api.identity[Int]` définissaient donc tous deux
`zeta_fn_identity__Int`, ce qui provoquait une collision à l'édition de liens. Le
nom ne distinguait pas non plus deux fonctions génériques homonymes exportées par
des modules différents.

## Identité canonique

Une demande d'instance est identifiée par le tuple suivant :

```text
(ABI, empreinte de l'interface productrice, module, fonction, types concrets)
```

Les types utilisent un encodage structurel non ambigu. Les structures et enums
incluent leur module de définition ; les tableaux incluent leur longueur et les
références ou vues incluent leur mutabilité. Les types génériques imbriqués sont
encodés récursivement.

Le symbole ELF conserve un préfixe lisible
`module__fonction__Types`, complété par le condensat FNV-1a 64 bits de l'identité.
Deux demandes compatibles produisent donc exactement le même symbole, tandis
qu'une différence de producteur, de type, d'interface ou d'ABI ne peut pas être
confondue silencieusement.

## Collecte et propriété

Avant d'émettre les objets d'un graphe source, le compilateur collecte toutes les
demandes de monomorphisation et leur fermeture transitive. Le premier module dans
l'ordre topologique qui demande une clé devient son propriétaire déterministe.
Lui seul émet le corps ; les autres modules gardent un symbole externe vers la
même clé.

Une bibliothèque construite isolément reste autonome : elle possède les
instances demandées par son propre code. Quand plusieurs objets précompilés
apportent une même instance, la définition doit avoir une sémantique `link-once`
afin que `ld` n'en conserve qu'une. Cette propriété fait partie de l'ABI des
objets génériques.

## Validation

Les tests couvrent les appels répétés dans un module, plusieurs consommateurs
source, les fonctions homonymes de producteurs distincts, les types qualifiés,
les bibliothèques construites séparément et le cache partagé. Une identité
incompatible doit produire deux symboles distincts, jamais une déduplication par
le seul nom affiché du type.

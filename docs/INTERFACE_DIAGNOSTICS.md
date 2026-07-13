# Diagnostics des interfaces et objets précompilés

Les bibliothèques Zeta distribuées associent une interface `.zti` et un objet
ELF64 `.o`. Les erreurs de lecture doivent être diagnostiquées avant l'analyse du
consommateur ou l'édition de liens, avec un code stable, le chemin concerné et le
contexte nominal disponible.

## Familles de codes

### `ZTI` — interface persistante

- `ZTI001` : version globale inconnue ou incompatible ;
- `ZTI010` : en-tête ou entrée syntaxiquement invalide ;
- `ZTI100` : type encodé invalide ou référence nominale inconnue ;
- `ZTI200` : disposition ABI incohérente d'une structure ou enum ;
- `ZTI300` : représentation générique absente, tronquée ou de version inconnue.

Les messages de disposition citent le type, puis la variante ou le champ lorsque
ces noms sont déjà disponibles. Une erreur de type dans une signature cite
l'export ou le paramètre en cours de lecture.

### `MOD` — résolution de module

- `MOD001` : module ou fichier d'interface introuvable ;
- `MOD002` : le nom interne de l'interface ne correspond pas au nom importé ;
- `MOD003` : objet précompilé associé absent.

### `ABI` — objet associé

- `ABI001` : fichier trop court ou magie ELF invalide ;
- `ABI002` : classe, endianness, type ELF ou architecture incompatibles.

Zeta cible actuellement un objet ELF64 relogeable, little-endian, pour x86-64.
Cette vérification ne prouve pas que le code respecte toute la convention Zeta,
mais elle élimine les artefacts manifestement incompatibles avant `ld`.

## Forme des messages

Un diagnostic suit la forme :

```text
[ZTI200] /chemin/types.zti : structure 'Point', champ 'x' : champ hors disposition
```

Le codec reçoit donc le chemin logique de l'interface en plus de son contenu. Il
conserve une exception unique avec le code et le détail ; le chargeur n'ajoute
pas une seconde couche de texte susceptible de masquer le code initial.

## Compatibilité et tests

Les codes font partie de l'expérience de diagnostic, pas du format binaire. Leur
texte explicatif peut être amélioré, mais un cas ne doit pas changer de famille
sans modification documentée.

La matrice négative couvre au minimum : version `.zti`, entrée inconnue,
référence nominale absente, champ de structure hors limites, champ d'enum hors
charge, version des tokens génériques, token `End` absent, nom de module interne,
objet absent, magie ELF invalide et architecture incompatible.

Cette matrice est implémentée par `tests/interface_diagnostics.sh`. Le codec
propage `InterfaceError` jusqu'au chargeur, qui ajoute le chemin sans perdre le
code. Les objets sont contrôlés dès que le couple `.zti` + `.o` est résolu ; les
artefacts incompatibles n'atteignent donc plus `ld`.

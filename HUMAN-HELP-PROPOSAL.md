# Aide humaine plus lisible et export `man` — étape 0

Mission du 22 octobre 2026, v2. Base : `main` = `v0.8.0`
(`d868389994c35d0448bb0eb961f3619084158561`). Spec épinglée : agent-cli
2.16.0 (`346e545`) ; kit de référence 2.16.1 (`9401324`), dont seul le
générateur de lignes diffère. Ce document est une proposition de travail
sur la branche `feat/human-help-man` ; il n'a pas vocation à rester dans
le dépôt une fois les décisions prises.

Points soumis à David (spec ou catalogue) : **D1 à D4**, en fin de
document. Tout le reste est du rendu et peut avancer dès l'accord sur la
matrice.

## 0.1 Matrice

Quatre destinations, dans l'ordre où un humain les rencontre :

- **courte** : `PROGRAMME --help` = `PROGRAMME help` (aide générale) ;
- **détails** : `PROGRAMME COMMANDE --help` = `PROGRAMME help COMMANDE`
  (la page d'une commande ; `help FAMILLE` et `help conventions` restent) ;
- **man** : `man PROGRAMME`, généré du catalogue ;
- **describe** : `PROGRAMME describe --format json`, inchangé.

« Spec » dit ce que agent-cli 2.16.0 impose au **texte** de l'aide. Rien
dans la spec n'oblige à afficher l'effet, la sortie, le mode de flux ou la
disponibilité dans l'aide : leur place est `describe`. Le kit ne lit dans le
texte que l'absence des options cachées ; il vérifie `data.commands`.

### Aide générale (`--help`, `help`)

| Information affichée aujourd'hui | Spec | Courte | Détails | man | describe |
|---|---|---|---|---|---|
| `PROGRAMME VERSION - résumé` | — | oui | — | NOM | `program`, `version` (le résumé n'y est pas, voir D3) |
| USAGE générique `PROGRAMME COMMAND [OPERANDS] [OPTIONS]` | — | oui, une ligne | — | SYNOPSIS | — |
| Les trois formes `help COMMAND_ID`, `help FAMILY`, `help conventions` | — | une ligne de renvoi | — | VOIR AUSSI | — |
| COMMANDES du produit (motif, objet) | `commands` = ce que le texte montre ; jamais une commande cachée (s.6) | oui | — | COMMANDES | oui |
| Commandes intégrées (`help`, `version`, `describe`, `completion`) | idem | regroupées sur une ligne : `help, version, describe, completion` | — | COMMANDES | oui |
| Commande indisponible, marquée en ligne | aide due si demandée nommément (2.14.0) | regroupées en fin de liste, **nommées** : « Indisponibles dans cette construction : image » | raison, en tête de sa page | COMMANDES, marquées | `available`, `unavailableReason` |
| COMMANDE PAR DÉFAUT : paragraphe de 3 lignes + deux formes | SHOULD : la nommer, dire que les options en tête sont à elle, montrer les deux formes (s.8) | une ligne qui couvre les trois points (voir 1 ci-dessous) | paragraphe + `--` pour les opérandes | DESCRIPTION | `defaultCommand` |
| OPTIONS GLOBALES : liste des noms + renvoi | — | une ligne de renvoi vers `help conventions` | `help conventions` (complet) | OPTIONS GLOBALES (complet) | `globalOptions` |
| CONTRAT AGENT : paragraphe | — | **sort** ; une ligne « Pour un agent : `PROGRAMME describe --summary --format json` » | `help conventions` | une section en fin | `invariants`, `output` |
| `agent_guidance` du produit | — | **sort** de la courte | `help conventions` | section CONTRAT AGENT | — (pas dans `describe`) |

### Page d'une commande (`COMMANDE --help`, `help COMMANDE`)

| Information affichée aujourd'hui | Spec | Courte | Détails | man | describe |
|---|---|---|---|---|---|
| USAGE : synopsis complet, toutes les options | `usage` = `input.synopsis` est une donnée de `describe`, pas un texte imposé | — | synopsis **compact** (voir 1) | SYNOPSIS compact | `usage`, `input.synopsis` complets, inchangés |
| Objet (`purpose`) | — | dans la liste | oui, en tête | COMMANDES | `purpose` |
| EXEMPLES | SHOULD les montrer (s.2) | — | oui, **juste après l'objet** | EXEMPLES | `examples` |
| OPÉRANDES (résumé, « Optional. ») | — | — | oui | OPTIONS de la commande | `input.operands` |
| OPTIONS (étiquette, résumé, défaut, Required, Requires, Conflicts, repeatable, choix) | jamais une option cachée (s.2), vérifié par le kit | — | oui | oui | `input.options` |
| COMMANDE PAR DÉFAUT (si c'est elle) | SHOULD (s.8) | — | oui, paragraphe court + deux formes | DESCRIPTION | `defaultCommand` |
| INDISPONIBLE DANS CETTE CONSTRUCTION + raison | — | — | oui, en tête | marquée | `available`, `unavailableReason` |
| EFFECT `read` / `preview by default; apply with --apply` / `execute` / `stream` | — | — | **en mots** seulement quand il change ce que l'humain fait : transaction → « Montre un plan ; --apply l'applique. » ; `execute` → « Agit sans plan. » ; `read` → rien | oui, en mots | `effect`, `applyEffect` |
| OUTPUT `json-envelope`, `json-records`, `protocol-stream owned by protocol …`, délégué | — | — | **sort**, sauf une phrase pour un flux ou un délégué : « Sa sortie est celle du programme lancé ; --format ne s'y applique pas. » | oui | `output`, `protocol`, `delegate` |
| Renvoi OPTIONS GLOBALES | — | — | une ligne | OPTIONS GLOBALES | `globalOptions` |

### Ce que l'aide ne montre pas aujourd'hui

| Information | Courte | Détails | man | describe |
|---|---|---|---|---|
| Codes de sortie (0, 1, 2 et ceux d'une commande) | — | — | CODES DE SORTIE | `exitCodes` |
| Variables d'environnement (`MAELYS_CLI_FORMAT`, `NO_COLOR`, `TERM`, `PAGER`, `COLUMNS`) | — | `help conventions` | ENVIRONNEMENT | en partie (`globalOptions`) |
| Bornes et grammaire des valeurs (min, max, taille, durée, empreinte) | — | — | oui, une ligne par option typée | `input.options` |
| Contraintes de lignes (`group`, `depends_on_all`, contraintes d'opérandes) | — | oui, en mots | oui | `input.constraints` |
| Schéma de sortie | — | — | — | `outputSchema` |

## 1. Règles de rendu proposées (sans décision de spec)

- **Synopsis compact** : `PROGRAMME MOTIF OPÉRANDES [OPTIONS-REQUISES] [options]`. Les
  sous-commandes du motif et chaque opérande, obligatoire ou entre
  crochets, restent ; les options **requises** restent sur la ligne (sans
  elles la ligne ne s'exécute pas) ; les autres options deviennent
  `[options]`. Une option cachée n'y figure jamais. `describe` garde le
  synopsis complet.
- **Commande par défaut, une ligne** : `PROGRAMME [options]` lance
  `MOTIF`, comme `PROGRAMME MOTIF [options]` ; les options en tête sont les
  siennes. Cette ligne nomme le défaut, dit à qui vont les options en tête
  et montre les deux formes : les trois points du SHOULD tiennent dans
  l'aide courte. Le placement des opérandes après `--` va dans les
  détails.
- **Hiérarchie** : titres de section en capitales ; en gras et en couleur
  seulement quand `--color` le permet (`auto` sur un terminal, jamais si
  `NO_COLOR` ou `TERM=dumb`). Sans couleur, la hiérarchie tient par les
  titres, une ligne vide entre sections et l'indentation (2 et 6).
- **Largeur** : celle du terminal entre 60 et 100 colonnes ; hors terminal
  et en JSON, **80**, fixe (c'est déjà la règle, `HELP_WIDTH`). Elle sera
  documentée dans `docs/command-conventions.md`.
- **Identité** : C et Python produisent le même texte, octet pour octet,
  à largeur et capacités égales ; un test le compare sur trois catalogues
  (`maelys-hello`/`hello.py`, `default_surface`, `catalog_surface`). Les
  écarts actuels (synopsis d'un choix, objets des commandes intégrées)
  sont corrigés dans le même mouvement.
- **`data.commands`** suit le texte : les indisponibles étant nommées,
  elles y restent ; les intégrées aussi.

## 0.2 Syntaxe des nouvelles entrées

Catalogue intégré inspecté : `help [COMMAND_ID]` (opérande seul, aucune
option propre), `version`, `describe [COMMAND_ID] [--summary] [--prefix
PREFIX]`, `completion bash|zsh|fish`, `__complete` (caché),
`completion install` (réservé). La spec (s.6) fige le motif de `help` et
ses données ; une option ajoutée y serait admissible mais visible dans
`describe` de tous les produits.

**Détails.** Proposé : **pas de nouvelle entrée**. La page d'une commande
est le niveau « détails » ; l'aide générale est le niveau « court ». Ni la
spec ni le catalogue ne bougent. L'autre voie, `help COMMANDE --details`,
est en D1.

**Page `man`.** Proposé : un **outil externe** `maelys-cli-man`, installé
par la formule `libmaelys-cli` comme `maelys-cli-embed`, qui lit
`PROGRAMME describe --format json` et écrit le roff sur stdout :

```sh
maelys-cli-man ./build/maelys-hello > share/man/man1/maelys-hello.1
```

Il sert les produits C et Python, puisqu'il ne lit que `describe` ; le
cœur ne gagne aucune dépendance (l'outil lit le JSON avec maelys-json,
déjà dépendance de la bibliothèque d'extension). Il ne touche ni à la spec
ni au catalogue. Le produit l'appelle à la construction et installe la
page. L'autre voie, une commande intégrée `man`, est en D2.

## 0.3 Extensions du catalogue (séparées, ne conditionnent rien)

- **E1. Sections libres du manuel** (CONFIGURATION, FICHIERS, TOUCHES…) :
  un tableau `{titre, texte}` au niveau du programme. Sans décision de
  spec : membre `x-maelys-manual` de `describe` ; normatif : candidat de
  spec.
- **E2. Lieu d'exécution d'une commande** (locale, sur une autre machine,
  dans une VM) : un membre de descripteur. Même alternative : `x-` d'abord,
  normatif ensuite si un second produit en a besoin.

L'export `man` sort sans elles ; il les rendra quand l'une sera adoptée.

## Points soumis à David

- **D1 — accès aux détails.** (a) recommandé : la page de commande est le
  niveau détails, rien n'est ajouté ; (b) `help COMMANDE --details` : une
  option sur `help`, visible dans `describe` de chaque produit (les
  références générées que commitent Maelys Git et Egress changent), et
  `COMMANDE --help --details` est refusé (VALIDATION_FAILED, section 8,
  étape 2) ; l'admettre serait une question de spec.
- **D2 — `man`.** (a) recommandé : outil externe `maelys-cli-man` lisant
  `describe` ; (b) commande intégrée `man` : présente au catalogue de
  chaque produit, offerte par la complétion, en collision possible avec une
  commande `man` d'un produit, et hors de la section 6 de la spec.
- **D3 — le résumé du programme.** `describe` ne porte ni le résumé
  (`summary`, la ligne sous le nom) ni `agent_guidance`. La page `man` en a
  besoin pour NOM et DESCRIPTION. (a) recommandé : un membre
  `x-maelys-summary` (et `x-maelys-guidance`) dans `describe`, permis sans
  changer la spec ; (b) un membre normatif `summary`, candidat de spec.
  Dans les deux cas `describe` gagne un membre : la mission le veut
  « inchangé » ; un ajout `x-` laisse le contrat intact mais change les
  références générées des produits.
- **D4 — synopsis de l'aide différent de `usage`.** La spec appelle
  `usage` « the human synopsis » et l'égale à `input.synopsis`. L'aide
  montrerait un synopsis compact (`[options]`) et `describe` garderait le
  complet. Aucune règle ne l'interdit ; la session spec donnera sa
  lecture. Sinon, le synopsis compact reste dans le seul texte.

Non soumis : `CLICOLOR_FORCE` n'a pas d'effet (la spec ne connaît que
`--color`, `NO_COLOR`, `TERM=dumb`) ; il est seulement relevé dans les
preuves.

## Pour les preuves

`cx` 0.12.0 (`1a6086f`) n'est sur cette machine dans aucun dépôt cloné :
il faut son adresse pour le reconstruire avec le framework modifié. Second
produit proposé : maelys-egress (C) ou maelys-git-core (cinq programmes).

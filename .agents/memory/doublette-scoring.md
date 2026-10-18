---
name: Doublette scoring
description: User-confirmed scoring rule for all doublettes and the purpose of Harakiri Custom.
---

Every doublette consists of two clays. Each clay is either hit (2 points) or
missed (0 points). One doublette therefore has a maximum of 4 points. This
applies to H, all Custom doublettes, and Harakiri Custom; there is no one-point
second-shot option for a doublette clay.

**Why:** The user explicitly clarified: “Doublette, two clays, always 0 or 2
for each clay. total doublet is maximum 4 points all time.”

**How to apply:** Preserve this rule when changing scoring controls, validation,
new game modes, and maximum-score calculations. Ordinary singles retain their
separate first/second-shot scoring. Do not reinterpret previously recorded
historical scores without a separate request.

Harakiri Custom uses a configured doublette in place of H, mixed into the
random launch order as one intact pair.

**Why:** The user requested that the custom doublette integrate the same way
as H in Harakiri, rather than turning the whole mode into a fixed Custom sequence.

**How to apply:** Keep the pair intact when randomizing, while keeping its two
individual result entries and binary clay scoring.

Harakiri and Harakiri Custom require an independent random launch-unit order
for each player, not one shuffled order shared by everybody. Normal and Custom
1–4 must retain their shared sequence.

**Why:** The user reported that every player was shooting the same machine in
each Harakiri round and explicitly requested independent permutations.

**How to apply:** Shuffle whole units per player with hardware entropy; do not
force distinct orders (coincidence is valid randomness). Keep each player's
order fixed for the game, including both runs and restart recovery. Shared
Harakiri scorecards must align by machine and pair role, not another player's
shot index; a single A is distinct from A inside a custom pair.
/**
 * Unit tests for the leaderboard (Rangliste) computation logic.
 *
 * Run with:
 *   node --experimental-strip-types --test src/routes/rangliste-compute.test.ts
 */
import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { computeRangliste } from "./rangliste-compute.js";
import type { SpielRow, TeilnahmeRow, SpielerRow } from "./rangliste-compute.js";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Build a Normal game (18 targets × 2 pts × 2 läufe = 72 maxPunkte) */
function normalSpiel(id: number): SpielRow {
  return { id, modus: "Normal", taubenProLauf: 18, lauf: 2 };
}

/** Build a Custom game with explicit params */
function customSpiel(
  id: number,
  taubenProLauf: number,
  lauf: number,
): SpielRow {
  return { id, modus: "Custom", taubenProLauf, lauf };
}

function spieler(id: number, name: string): SpielerRow {
  return { id, name };
}

/** Construct a single-lauf Teilnahme row */
function t(spielerId: number, spielId: number, punkte: number): TeilnahmeRow {
  return { spielerId, spielId, punkte };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe("computeRangliste", () => {
  // ── empty inputs ──────────────────────────────────────────────────────────

  it("returns an empty array when there are no games", () => {
    const result = computeRangliste([], [], []);
    assert.deepEqual(result, []);
  });

  it("returns an empty array when there are games but no Teilnahmen", () => {
    const result = computeRangliste([normalSpiel(1)], [], [spieler(1, "Alice")]);
    assert.deepEqual(result, []);
  });

  // ── single player, single format ─────────────────────────────────────────

  it("ranks a single player with only Normal games", () => {
    // game: taubenProLauf=18, lauf=2 → maxPunkte=72
    // player scores 36 pts across one game
    const spiele = [normalSpiel(1)];
    const teilnahmen = [t(1, 1, 36)];
    const [entry] = computeRangliste(spiele, teilnahmen, [spieler(1, "Alice")]);

    assert.equal(entry.rang, 1);
    assert.equal(entry.spielerId, 1);
    assert.equal(entry.gesamtPunkte, 36);
    assert.equal(entry.anzahlSpiele, 1);
    // durchschnittProzent = round(36/72 * 1000) / 10 = 50.0
    assert.equal(entry.durchschnittProzent, 50);
    assert.equal(entry.bestPunkte, 36);
  });

  it("ranks a single player with only Custom games", () => {
    // game: taubenProLauf=10, lauf=3 → maxPunkte=60
    // player scores 48 pts → 80%
    const spiele = [customSpiel(1, 10, 3)];
    const teilnahmen = [t(1, 1, 48)];
    const [entry] = computeRangliste(spiele, teilnahmen, [spieler(1, "Bob")]);

    assert.equal(entry.rang, 1);
    assert.equal(entry.gesamtPunkte, 48);
    assert.equal(entry.durchschnittProzent, 80);
  });

  // ── durchschnittProzent formula ───────────────────────────────────────────

  it("calculates durchschnittProzent as (gesamtPunkte / gesamtMaxPunkte) × 100", () => {
    // Two Normal games (maxPunkte=72 each → gesamtMaxPunkte=144)
    // Player scores 54 + 36 = 90 → 90/144 = 62.5%
    const spiele = [normalSpiel(1), normalSpiel(2)];
    const teilnahmen = [t(1, 1, 54), t(1, 2, 36)];
    const [entry] = computeRangliste(spiele, teilnahmen, [spieler(1, "Alice")]);

    assert.equal(entry.gesamtPunkte, 90);
    assert.equal(entry.durchschnittProzent, 62.5);
  });

  it("rounds durchschnittProzent to one decimal place", () => {
    // maxPunkte=72, score=25 → 25/72*100 = 34.722… → rounds to 34.7
    const spiele = [normalSpiel(1)];
    const teilnahmen = [t(1, 1, 25)];
    const [entry] = computeRangliste(spiele, teilnahmen, [spieler(1, "Alice")]);

    assert.equal(entry.durchschnittProzent, 34.7);
  });

  // ── mixed formats – fairness check ───────────────────────────────────────

  it("ranks a Custom-only player above a Normal-only player when their % is higher", () => {
    // Alice: 1 Normal game (max=72), scores 60 → 83.3%
    // Bob:   1 Custom game (tauben=5, lauf=2 → max=20), scores 18 → 90%
    const spiele = [
      normalSpiel(1),
      customSpiel(2, 5, 2),
    ];
    const teilnahmen = [
      t(1, 1, 60), // Alice, Normal game
      t(2, 2, 18), // Bob, Custom game
    ];
    const result = computeRangliste(spiele, teilnahmen, [
      spieler(1, "Alice"),
      spieler(2, "Bob"),
    ]);

    assert.equal(result[0].name, "Bob");
    assert.equal(result[0].rang, 1);
    assert.equal(result[1].name, "Alice");
    assert.equal(result[1].rang, 2);
    assert.ok(
      result[0].durchschnittProzent > result[1].durchschnittProzent,
      "Bob's % should exceed Alice's %",
    );
  });

  it("does NOT penalise a Custom-only player: same % as Normal-only = same rank order by gesamtPunkte", () => {
    // Alice: Normal (max=72), scores 36 → 50%
    // Bob: Custom (max=40), scores 20 → 50%
    // Same %, tiebreak by gesamtPunkte: Alice (36) > Bob (20)
    const spiele = [
      normalSpiel(1),
      customSpiel(2, 10, 2), // max = 40
    ];
    const teilnahmen = [
      t(1, 1, 36),
      t(2, 2, 20),
    ];
    const result = computeRangliste(spiele, teilnahmen, [
      spieler(1, "Alice"),
      spieler(2, "Bob"),
    ]);

    assert.equal(result[0].durchschnittProzent, result[1].durchschnittProzent);
    assert.equal(result[0].name, "Alice"); // higher gesamtPunkte wins tiebreak
    assert.equal(result[1].name, "Bob");
  });

  // ── mixed formats for a single player ────────────────────────────────────

  it("aggregates Normal and Custom games correctly for a single player", () => {
    // Normal (max=72): scores 54
    // Custom (tauben=10, lauf=1 → max=20): scores 16
    // gesamtPunkte = 70, gesamtMaxPunkte = 92
    // durchschnittProzent = round(70/92 * 1000) / 10 = round(760.87) / 10 = 76.1
    const spiele = [
      normalSpiel(1),
      customSpiel(2, 10, 1),
    ];
    const teilnahmen = [
      t(1, 1, 54),
      t(1, 2, 16),
    ];
    const [entry] = computeRangliste(spiele, teilnahmen, [spieler(1, "Alice")]);

    assert.equal(entry.gesamtPunkte, 70);
    assert.equal(entry.anzahlSpiele, 2);
    assert.equal(entry.durchschnittProzent, 76.1);
    assert.equal(entry.bestPunkte, 54);
  });

  it("handles multiple Teilnahme rows (Läufe) per game for mixed-format player", () => {
    // Each game has lauf=2, so there are 2 Teilnahme rows per game.
    // Normal (tauben=18, lauf=2 → max=72): lauf1=30, lauf2=26 → total=56
    // Custom (tauben=5, lauf=2 → max=20): lauf1=8, lauf2=9 → total=17
    // gesamtPunkte = 73, gesamtMaxPunkte = 92
    const spiele = [
      normalSpiel(1), // max=72
      customSpiel(2, 5, 2), // max=20
    ];
    const teilnahmen = [
      t(1, 1, 30),
      t(1, 1, 26),
      t(1, 2, 8),
      t(1, 2, 9),
    ];
    const [entry] = computeRangliste(spiele, teilnahmen, [spieler(1, "Alice")]);

    assert.equal(entry.gesamtPunkte, 73);
    assert.equal(entry.anzahlSpiele, 2);
    assert.equal(entry.bestPunkte, 56);
    const expected = Math.round((73 / 92) * 1000) / 10;
    assert.equal(entry.durchschnittProzent, expected);
  });

  // ── modus filter (caller pre-filters spiele) ─────────────────────────────

  it("only counts Normal games when caller passes Normal-only spiele", () => {
    // Caller has already filtered out Custom games (simulating ?modus=Normal).
    const normalOnly = [normalSpiel(1)];
    const allTeilnahmen = [
      t(1, 1, 60), // Normal game
      t(1, 2, 18), // Custom game (spielId=2, not in normalOnly)
    ];
    const [entry] = computeRangliste(normalOnly, allTeilnahmen, [spieler(1, "Alice")]);

    // Custom game's Teilnahme is ignored because spielId=2 is not in spiele list.
    assert.equal(entry.gesamtPunkte, 60);
    assert.equal(entry.anzahlSpiele, 1);
  });

  it("only counts Custom games when caller passes Custom-only spiele", () => {
    const customOnly = [customSpiel(2, 5, 2)]; // max=20
    const allTeilnahmen = [
      t(1, 1, 60), // Normal game – should be ignored
      t(1, 2, 18), // Custom game
    ];
    const [entry] = computeRangliste(customOnly, allTeilnahmen, [spieler(1, "Alice")]);

    assert.equal(entry.gesamtPunkte, 18);
    assert.equal(entry.anzahlSpiele, 1);
    assert.equal(entry.durchschnittProzent, 90);
  });

  // ── ranking multiple players ──────────────────────────────────────────────

  it("assigns correct ranks to three players by durchschnittProzent", () => {
    // Game: Normal, max=72
    // Alice: 72 pts → 100%
    // Bob:   54 pts → 75%
    // Carol: 36 pts → 50%
    const spiele = [normalSpiel(1)];
    const teilnahmen = [t(1, 1, 72), t(2, 1, 54), t(3, 1, 36)];
    const result = computeRangliste(spiele, teilnahmen, [
      spieler(1, "Alice"),
      spieler(2, "Bob"),
      spieler(3, "Carol"),
    ]);

    assert.equal(result[0].name, "Alice");
    assert.equal(result[0].rang, 1);
    assert.equal(result[1].name, "Bob");
    assert.equal(result[1].rang, 2);
    assert.equal(result[2].name, "Carol");
    assert.equal(result[2].rang, 3);
  });

  it("uses 'Unbekannt' as name when spieler row is missing", () => {
    const spiele = [normalSpiel(1)];
    const teilnahmen = [t(99, 1, 36)]; // spielerId=99 not in spieler list
    const [entry] = computeRangliste(spiele, teilnahmen, []);

    assert.equal(entry.name, "Unbekannt");
    assert.equal(entry.spielerId, 99);
  });
});

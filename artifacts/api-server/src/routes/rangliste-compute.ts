/**
 * Pure computation helpers for the leaderboard (Rangliste).
 * Extracted so they can be unit-tested without a database connection.
 */

export interface SpielRow {
  id: number;
  modus: string;
  taubenProLauf: number;
  lauf: number;
}

export interface TeilnahmeRow {
  spielerId: number;
  spielId: number;
  punkte: number;
}

export interface SpielerRow {
  id: number;
  name: string;
}

export interface RanglisteEntry {
  rang: number;
  spielerId: number;
  name: string;
  gesamtPunkte: number;
  anzahlSpiele: number;
  durchschnitt: number;
  durchschnittProzent: number;
  bestPunkte: number;
}

/**
 * Compute the ranked leaderboard from raw DB rows.
 *
 * Normalization formula:
 *   durchschnittProzent = (gesamtPunkte / gesamtMaxPunkte) × 100
 * where gesamtMaxPunkte = sum of (taubenProLauf × 2 × lauf) for each game the
 * player participated in.  This makes Normal and Custom games comparable.
 */
export function computeRangliste(
  spiele: SpielRow[],
  teilnahmen: TeilnahmeRow[],
  spieler: SpielerRow[],
): RanglisteEntry[] {
  // Map spielId → maxPunkte for each game
  const maxPunkteMap = new Map<number, number>(
    spiele.map((s) => [s.id, s.taubenProLauf * 2 * s.lauf]),
  );

  const spieleIds = new Set(spiele.map((s) => s.id));

  // Step 1: Sum Läufe per (spieler, spiel) pair → per-game totals
  const perGame = new Map<
    string,
    { spielerId: number; total: number; maxPunkte: number }
  >();
  for (const t of teilnahmen) {
    if (!spieleIds.has(t.spielId)) continue; // guard: only included games
    const k = `${t.spielerId}:${t.spielId}`;
    if (!perGame.has(k)) {
      perGame.set(k, {
        spielerId: t.spielerId,
        total: 0,
        maxPunkte: maxPunkteMap.get(t.spielId) ?? 36,
      });
    }
    perGame.get(k)!.total += t.punkte;
  }

  // Step 2: Aggregate per spieler
  const aggregated = new Map<
    number,
    {
      gesamtPunkte: number;
      anzahlSpiele: number;
      spielPunkteListe: number[];
      gesamtMaxPunkte: number;
    }
  >();
  for (const { spielerId, total, maxPunkte } of perGame.values()) {
    if (!aggregated.has(spielerId)) {
      aggregated.set(spielerId, {
        gesamtPunkte: 0,
        anzahlSpiele: 0,
        spielPunkteListe: [],
        gesamtMaxPunkte: 0,
      });
    }
    const entry = aggregated.get(spielerId)!;
    entry.gesamtPunkte += total;
    entry.anzahlSpiele++;
    entry.spielPunkteListe.push(total);
    entry.gesamtMaxPunkte += maxPunkte;
  }

  if (!aggregated.size) return [];

  const nameMap = new Map(spieler.map((s) => [s.id, s.name]));

  return Array.from(aggregated.entries())
    .map(([spielerId, data]) => {
      const durchschnitt =
        Math.round((data.gesamtPunkte / data.anzahlSpiele) * 10) / 10;
      const durchschnittProzent =
        data.gesamtMaxPunkte > 0
          ? Math.round((data.gesamtPunkte / data.gesamtMaxPunkte) * 1000) / 10
          : 0;
      return {
        spielerId,
        name: nameMap.get(spielerId) ?? "Unbekannt",
        gesamtPunkte: data.gesamtPunkte,
        anzahlSpiele: data.anzahlSpiele,
        durchschnitt,
        durchschnittProzent,
        bestPunkte: Math.max(...data.spielPunkteListe),
      };
    })
    .sort(
      (a, b) =>
        b.durchschnittProzent - a.durchschnittProzent ||
        b.gesamtPunkte - a.gesamtPunkte,
    )
    .map((e, i) => ({ rang: i + 1, ...e }));
}

import test from "node:test";
import assert from "node:assert/strict";
import { db, spieleTable, spielTeilnahmenTable, spielerTable, ergebnisseTable } from "@workspace/db";
import { recentGamesPayload } from "./sync";

function databaseWith(rows: Map<unknown, unknown[]>) {
  let calls = 0;
  return {
    get calls() { return calls; },
    select() {
      calls++;
      let values: unknown[] = [];
      const query = {
        from(table: unknown) { values = rows.get(table) ?? []; return query; },
        where() { return query; },
        orderBy() { return query; },
        limit(limit: number) { values = values.slice(0, limit); return query; },
        then(resolve: (value: unknown[]) => unknown, reject?: (error: unknown) => unknown) {
          return Promise.resolve(values).then(resolve, reject);
        },
      };
      return query;
    },
  };
}

test("history publishes recorded clay results, player names and totals for each game", async () => {
  const shot = {
    spielerId: 11, lauf: 2, taube: 9, maschine: "H", posten: 4,
    schuss1: true, schuss2: false, punkte: 1, wiederholt: false,
  };
  const database = databaseWith(new Map<unknown, unknown[]>([
    [spieleTable, [
      { id: 101, externalId: "game-a", datum: new Date("2026-10-04T10:00:00Z"), modus: "NORMAL",
        lauf: 2, taubenProLauf: 9, abgeschlossen: true, confirmedLaunches: 18 },
      { id: 102, externalId: "game-b", datum: new Date("2026-10-04T09:00:00Z"), modus: "CUSTOM_4",
        lauf: 1, taubenProLauf: 3, abgeschlossen: true, confirmedLaunches: 3 },
    ]],
    [spielTeilnahmenTable, [
      { spielId: 101, spielerId: 11, startPosten: 1, punkte: 23, lauf: 2 },
      { spielId: 102, spielerId: 22, startPosten: 2, punkte: 3, lauf: 1 },
    ]],
    [spielerTable, [{ id: 11, name: "Andy" }, { id: 22, name: "Ben" }]],
    [ergebnisseTable, [
      { spielId: 101, ...shot },
      { spielId: 101, ...shot, wiederholt: true, punkte: 0 },
      { spielId: 102, ...shot, spielerId: 22, lauf: 1, taube: 1, maschine: "B" },
    ]],
  ]));
  const payload = await recentGamesPayload(20, database as unknown as typeof db);
  assert.equal(database.calls, 4); // bulk queries, not one query per game
  assert.equal(payload.spiele[0].externalId, "game-a");
  assert.equal(payload.spiele[0].teilnahmen[0].punkte, 23);
  assert.deepEqual(payload.spiele[0].spielerNamen, { 11: "Andy" });
  assert.deepEqual(payload.spiele[0].ergebnisse, [shot, { ...shot, wiederholt: true, punkte: 0 }]);
  assert.equal(payload.spiele[1].ergebnisse[0].maschine, "B");
  assert.equal(payload.spiele[1].ergebnisse[0].spielerId, 22);
  assert.ok(!("spielId" in payload.spiele[0].ergebnisse[0]));
});

test("empty history and totals-only legacy games have explicit empty clay arrays", async () => {
  const empty = databaseWith(new Map());
  assert.deepEqual(await recentGamesPayload(20, empty as unknown as typeof db), { spiele: [] });
  assert.equal(empty.calls, 1);
  const legacy = databaseWith(new Map<unknown, unknown[]>([[spieleTable, [
    { id: 1, externalId: "old", datum: new Date("2026-10-01T00:00:00Z"),
      modus: "NORMAL", lauf: 2, taubenProLauf: 9, abgeschlossen: true, confirmedLaunches: 0 },
  ]]]));
  const payload = await recentGamesPayload(20, legacy as unknown as typeof db);
  assert.deepEqual(payload.spiele[0].ergebnisse, []);
});
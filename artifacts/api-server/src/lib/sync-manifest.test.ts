import assert from "node:assert/strict";
import test from "node:test";
import { revisionToken } from "./sync-manifest";

test("revision tokens are stable for equivalent payloads regardless of object key order", () => {
  const first = { spieler: [{ id: 4, name: "Anne", portalAktiv: true }] };
  const sameDataDifferentKeyOrder = { spieler: [{ portalAktiv: true, name: "Anne", id: 4 }] };

  assert.equal(revisionToken(first), revisionToken(sameDataDifferentKeyOrder));
});

test("a material dataset change alters only that dataset revision", () => {
  const base = {
    roster: { spieler: [{ id: 4, name: "Anne" }] },
    products: { products: [{ id: 1, currentPrice: { unitPriceCents: 500 } }] },
    gameHistory: { spiele: [] },
    dailyCredits: { datum: "2025-02-03", kredite: [] },
    dailySales: { datum: "2025-02-03", sales: [], totalCents: 0 },
    dailyBillSummary: { datum: "2025-02-03", players: [] },
  };
  const before = Object.fromEntries(Object.entries(base).map(([name, payload]) => [name, revisionToken(payload)]));
  const afterPayloads = {
    ...base,
    dailySales: { datum: "2025-02-03", sales: [], totalCents: 500 },
  };
  const after = Object.fromEntries(Object.entries(afterPayloads).map(([name, payload]) => [name, revisionToken(payload)]));

  for (const name of Object.keys(base)) {
    assert.equal(after[name] === before[name], name !== "dailySales", `${name} revision changed unexpectedly`);
  }
});
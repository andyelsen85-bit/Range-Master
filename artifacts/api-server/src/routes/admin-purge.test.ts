import test from "node:test";
import assert from "node:assert/strict";
import { getTableName } from "drizzle-orm";
import { PgDialect, getTableConfig } from "drizzle-orm/pg-core";
import { db, ergebnisseTable, spielTeilnahmenTable, spieleTable } from "@workspace/db";
import adminRouter from "./admin";
import { authenticate, requireAdmin } from "./auth";

// Call the real purge handler with an in-memory transaction. Never query or
// delete the development database, including when the confirmation is valid.
const handler = (adminRouter as any).stack
  .find((layer: any) => layer.route?.path === "/purge").route.stack[0].handle;
const dialect = new PgDialect();

function response() {
  return {
    statusCode: 200,
    body: undefined as any,
    status(code: number) { this.statusCode = code; return this; },
    json(body: any) { this.body = body; return this; },
  };
}

test("reset endpoint requires authentication and an admin account", async () => {
  const unauthenticated = response();
  await authenticate({ headers: {} }, unauthenticated, () => assert.fail("Auth bypass"));
  assert.equal(unauthenticated.statusCode, 401);
  const nonAdmin = response();
  requireAdmin({ user: { isAdmin: false } }, nonAdmin, () => assert.fail("Role bypass"));
  assert.equal(nonAdmin.statusCode, 403);
});

test("global reset clears admin statistics but preserves every admin account", async () => {
  const originalTransaction = db.transaction;
  const rows: Record<string, any[]> = {
    spieler: [
      { id: 1, isAdmin: true, aktiv: true, passwortHash: "synthetic-admin-hash" },
      { id: 2, isAdmin: false, aktiv: true },
      { id: 3, isAdmin: true, aktiv: false, passwortHash: "synthetic-other-admin-hash" },
      { id: 4, isAdmin: false, aktiv: false },
    ],
    spiele: [{ id: 10 }],
    spiel_teilnahmen: [{ spielerId: 1 }, { spielerId: 2 }],
    ergebnisse: [{ spielerId: 1 }, { spielerId: 2 }],
    kredit_events: [{ spielerId: 1 }, { spielerId: 2 }],
    sale_events: [{ spielerId: 1 }, { spielerId: 2 }],
    bill_payments: [{ spielerId: 1 }, { spielerId: 2 }],
    spieler_updates: [{ spielerId: 1 }, { spielerId: 2 }],
  };
  const admins = rows.spieler.filter((p) => p.isAdmin).map((p) => ({ ...p }));
  const deleted: string[] = [];
  let locked = false;
  const tx = {
    async execute(query: any) {
      const compiled = dialect.sqlToQuery(query);
      if (compiled.sql.includes("LOCK TABLE")) {
        assert.match(compiled.sql, /ACCESS EXCLUSIVE/);
        for (const table of Object.keys(rows)) assert.ok(compiled.sql.includes(`"${table}"`));
        locked = true;
        return { rows: [] };
      }
      const table = compiled.sql.match(/FROM "([^"]+)"/)?.[1];
      assert.ok(table && rows[table], "Unexpected query");
      const count = table === "spieler"
        ? rows.spieler.filter((p) => !p.isAdmin).length : rows[table].length;
      return { rows: [{ count }] };
    },
    delete(table: any) {
      const name = getTableName(table);
      assert.ok(locked, "Must lock before deletion");
      assert.ok(rows[name], "Must not delete configuration, products, or API keys");
      return {
        async where(condition: any) {
          assert.equal(name, "spieler");
          const predicate = dialect.sqlToQuery(condition);
          assert.match(predicate.sql, /"spieler"\."is_admin" = \$1/);
          assert.deepEqual(predicate.params, [false]);
          rows.spieler = rows.spieler.filter((p) => p.isAdmin);
          deleted.push(name);
          return [];
        },
        then(resolve: (value: any[]) => void) {
          assert.notEqual(name, "spieler", "Unconditional account deletion forbidden");
          rows[name] = [];
          deleted.push(name);
          if (name === "spiele") {
            rows.ergebnisse = [];
            rows.spiel_teilnahmen = [];
          }
          resolve([]);
        },
      };
    },
  };
  (db as any).transaction = async (run: (transaction: any) => unknown) => run(tx);
  try {
    const res = response();
    await handler({ body: { mode: "all", confirmation: "PURGE_ALL" } }, res);
    assert.equal(res.statusCode, 200);
    assert.deepEqual(rows.spieler, admins);
    for (const [table, data] of Object.entries(rows)) {
      if (table !== "spieler") assert.equal(data.length, 0, `${table} not cleared`);
    }
    assert.equal(res.body.counts.players, 2);
    assert.equal(res.body.counts.results, 2);
    assert.equal(deleted.at(-1), "spieler");
    // Verify that actual schema relationships support the cascade modeled above.
    for (const table of [ergebnisseTable, spielTeilnahmenTable]) {
      const gameFk = getTableConfig(table).foreignKeys.find(
        (fk) => fk.reference().foreignTable === spieleTable);
      assert.equal(gameFk?.onDelete, "cascade");
    }
  } finally {
    (db as any).transaction = originalTransaction;
  }
});

test("missing, incorrect, or mismatched reset confirmation never enters a transaction", async () => {
  const originalTransaction = db.transaction;
  (db as any).transaction = () => assert.fail("Invalid request reached deletion");
  try {
    for (const body of [{ mode: "all" }, { mode: "all", confirmation: "PURGE_DAY" },
      { mode: "all", confirmation: "anything" }, { mode: "unknown", confirmation: "PURGE_ALL" }]) {
      const res = response();
      await handler({ body }, res);
      assert.equal(res.statusCode, 400);
    }
  } finally {
    (db as any).transaction = originalTransaction;
  }
});
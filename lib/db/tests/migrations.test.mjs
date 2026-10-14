import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtemp, readFile, readdir, copyFile, mkdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import pg from "pg";

const root = fileURLToPath(new URL("../../../", import.meta.url));
const folder = path.join(root, "lib/db/drizzle");
const journal = JSON.parse(await readFile(path.join(folder, "meta/_journal.json"), "utf8"));
const gamedaySql = await readFile(path.join(folder, "0009_gameday_daily_close.sql"), "utf8");
const dayTables = [
  "ammo_inventory_day", "gameday_maitre", "gameday_event", "daily_report_settings",
  "daily_report_recipient", "daily_report_log", "public_page_settings",
];
const quote = value => `"${value.replaceAll('"', '""')}"`;

// Every write targets a newly created disposable database, never DATABASE_URL itself.
async function withDatabase(run) {
  assert.ok(process.env.DATABASE_URL, "DATABASE_URL is required for local CREATEDB access");
  const name = `rangemaster_migration_test_${process.pid}_${Date.now()}`;
  assert.match(name, /^rangemaster_migration_test_\d+_\d+$/);
  const admin = new pg.Pool({ connectionString: process.env.DATABASE_URL });
  const url = new URL(process.env.DATABASE_URL);
  url.pathname = `/${name}`;
  let pool;
  let created = false;
  try {
    await admin.query(`CREATE DATABASE ${quote(name)}`);
    created = true;
    pool = new pg.Pool({ connectionString: url.toString() });
    await run(pool, url.toString());
  } finally {
    if (pool) await pool.end();
    if (created) {
      await admin.query(`DROP DATABASE ${quote(name)} WITH (FORCE)`);
    }
    await admin.end();
  }
}

function command(url, program, args, expectedStatus = 0) {
  const result = spawnSync(program, args, {
    cwd: root, encoding: "utf8", timeout: 90_000, maxBuffer: 2_000_000,
    env: { ...process.env, DATABASE_URL: url, NO_COLOR: "1" },
  });
  const output = `${result.stdout ?? ""}\n${result.stderr ?? ""}`
    .replaceAll(url, "[test database]").replaceAll(process.env.DATABASE_URL, "[database]");
  assert.equal(result.status, expectedStatus, output);
  return output;
}
const cli = (url, args, expectedStatus = 0) => command(url, "pnpm", args, expectedStatus);
const migrate = url => cli(url, ["migrate"]);

// Reproduce Docker's /app/dist + /app/drizzle layout using the actual built entry point.
async function initContainer(url, expectedStatus = 0) {
  const temp = await mkdtemp(path.join(os.tmpdir(), "rangemaster-init-container-"));
  try {
    await mkdir(path.join(temp, "dist"));
    await mkdir(path.join(temp, "drizzle/meta"), { recursive: true });
    await copyFile(path.join(root, "artifacts/api-server/dist/migrate.mjs"), path.join(temp, "dist/migrate.mjs"));
    await copyFile(path.join(folder, "meta/_journal.json"), path.join(temp, "drizzle/meta/_journal.json"));
    for (const entry of journal.entries) {
      await copyFile(path.join(folder, `${entry.tag}.sql`), path.join(temp, "drizzle", `${entry.tag}.sql`));
    }
    return command(url, "node", [path.join(temp, "dist/migrate.mjs")], expectedStatus);
  } finally {
    await rm(temp, { recursive: true, force: true });
  }
}

async function beforeGameday(url) {
  const temp = await mkdtemp(path.join(os.tmpdir(), "rangemaster-migration-"));
  try {
    await mkdir(path.join(temp, "meta"));
    const earlier = { ...journal, entries: journal.entries.filter(e => e.idx < 9) };
    await writeFile(path.join(temp, "meta/_journal.json"), JSON.stringify(earlier));
    for (const entry of earlier.entries) {
      await copyFile(path.join(folder, `${entry.tag}.sql`), path.join(temp, `${entry.tag}.sql`));
    }
    const config = path.join(temp, "config.cjs");
    await writeFile(config, `module.exports = {dialect: "postgresql", out: ${JSON.stringify(temp)}, dbCredentials: {url: process.env.DATABASE_URL}};`);
    cli(url, ["--filter", "@workspace/db", "exec", "drizzle-kit", "migrate", "--config", config]);
  } finally {
    await rm(temp, { recursive: true, force: true });
  }
}

async function state(pool) {
  const columns = (await pool.query(`
    SELECT table_schema,table_name,column_name,data_type,udt_name,is_nullable,column_default
    FROM information_schema.columns WHERE table_schema IN ('public','drizzle')
    ORDER BY table_schema,table_name,ordinal_position`)).rows;
  const constraints = (await pool.query(`
    SELECT t.relname AS table_name,c.conname,pg_get_constraintdef(c.oid) AS definition
    FROM pg_constraint c JOIN pg_class t ON t.oid=c.conrelid
    JOIN pg_namespace n ON n.oid=t.relnamespace WHERE n.nspname IN ('public','drizzle')
    ORDER BY t.relname,c.conname`)).rows;
  const indexes = (await pool.query(`
    SELECT schemaname,tablename,indexname,indexdef FROM pg_indexes
    WHERE schemaname IN ('public','drizzle') ORDER BY schemaname,tablename,indexname`)).rows;
  const enums = (await pool.query(`
    SELECT t.typname,e.enumlabel FROM pg_enum e JOIN pg_type t ON t.oid=e.enumtypid
    JOIN pg_namespace n ON n.oid=t.typnamespace WHERE n.nspname='public'
    ORDER BY t.typname,e.enumsortorder`)).rows;
  const tables = (await pool.query(`
    SELECT table_schema,table_name FROM information_schema.tables
    WHERE table_schema IN ('public','drizzle') AND table_type='BASE TABLE'
    ORDER BY table_schema,table_name`)).rows;
  const data = [];
  for (const table of tables) {
    const rows = await pool.query(`SELECT to_jsonb(t) AS row FROM ${quote(table.table_schema)}.${quote(table.table_name)} t ORDER BY to_jsonb(t)::text`);
    data.push({ ...table, rows: rows.rows });
  }
  const sequences = (await pool.query(`
    SELECT schemaname,sequencename,last_value FROM pg_sequences
    WHERE schemaname IN ('public','drizzle') ORDER BY schemaname,sequencename`)).rows;
  return { columns, constraints, indexes, enums, data, sequences };
}

async function applied(pool) {
  return Number((await pool.query('SELECT count(*) FROM drizzle.__drizzle_migrations')).rows[0].count);
}

async function fixture(pool, mode = "two-protected") {
  await pool.query("INSERT INTO api_keys(name,key,type) VALUES('migration fixture','fixture-only','SYNC')");
  for (const hour of [10, 11]) {
    await pool.query(`INSERT INTO terminal_config_backups
      (terminal_id,schema_version,ciphertext,iv,auth_tag,checksum,created_at,updated_at)
      VALUES ('test-terminal',1,'fixture','fixture','fixture','fixture',$1,$1)`,
      [`2026-10-12T${hour}:00:00+02:00`]);
  }
  // Even an expired, unused approval remains protected until explicitly revoked.
  await pool.query(`INSERT INTO terminal_restore_authorizations
    (backup_id,target_terminal_id,target_api_key_id,code_hash,expires_at)
    VALUES (1,'replacement',1,'fixture-1','2020-01-01')`);
  if (mode === "two-protected") {
    await pool.query(`INSERT INTO terminal_restore_authorizations
      (backup_id,target_terminal_id,target_api_key_id,code_hash,expires_at)
      VALUES (2,'replacement',1,'fixture-2','2099-01-01')`);
  }
}

function gamedaySchema(snapshot) {
  const extraColumns = {
    spieler: "is_maitre_de_tir", spiele: "completed_at", terminal_config_backups: "backup_date",
  };
  return {
    columns: snapshot.columns.filter(c => dayTables.includes(c.table_name) || extraColumns[c.table_name] === c.column_name),
    constraints: snapshot.constraints.filter(c => dayTables.includes(c.table_name)),
    indexes: snapshot.indexes.filter(i => dayTables.includes(i.tablename) || i.indexname === "terminal_config_backups_daily_unique"),
  };
}

test("Drizzle deployment migration regression", async t => {
  assert.deepEqual(journal.entries.map(e => e.idx), journal.entries.map((_, i) => i));
  assert.equal(journal.entries[9].tag, "0009_gameday_daily_close");
  assert.ok(journal.entries[9].when > journal.entries[8].when);
  assert.doesNotMatch(gamedaySql, /^(BEGIN|COMMIT);$/m, "Drizzle owns the transaction");
  assert.ok(!(await readdir(path.join(root, "lib/db/migrations"))).includes("20261013-gameday.sql"));

  await t.test("fresh full chain, repeated pnpm migrate, direct replay and schema push", async () => {
    await withDatabase(async (pool, url) => {
      migrate(url);
      assert.equal(await applied(pool), journal.entries.length);
      const before = await state(pool);
      for (const table of dayTables) assert.ok(before.columns.some(c => c.table_name === table), table);
      assert.equal((await pool.query("SELECT enabled FROM daily_report_settings WHERE id=1")).rows[0].enabled, false);
      assert.deepEqual((await pool.query("SELECT enabled,name_mode FROM public_page_settings WHERE id=1")).rows[0], { enabled: false, name_mode: "INITIAL" });
      migrate(url);
      assert.deepEqual(await state(pool), before, "Second migration run changes neither schema nor data nor journal");
      await initContainer(url);
      assert.deepEqual(await state(pool), before, "Docker entry point skips already journaled migrations");
      const client = await pool.connect();
      try {
        await client.query("BEGIN");
        await client.query(gamedaySql);
        await client.query("COMMIT");
      } finally {
        client.release();
      }
      assert.deepEqual(await state(pool), before, "SQL itself remains idempotent");
      const pushOutput = cli(url, ["--filter", "@workspace/db", "exec", "drizzle-kit", "push", "--config", "./drizzle.config.ts", "--force"]);
      assert.deepEqual(gamedaySchema(await state(pool)), gamedaySchema(before),
        `drizzle-kit push must not change the 0009 schema:\n${pushOutput}`);
      console.log("drizzle-kit push on disposable DB:", pushOutput.trim());
    });
  });

  await t.test("existing manual Gameday application is safely journaled", async () => {
    await withDatabase(async (pool, url) => {
      await beforeGameday(url);
      assert.equal(await applied(pool), 9);
      const client = await pool.connect();
      try {
        await client.query("BEGIN");
        await client.query(gamedaySql);
        await client.query("COMMIT");
      } finally {
        client.release();
      }
      const before = await state(pool);
      migrate(url);
      assert.equal(await applied(pool), journal.entries.length);
      const after = await state(pool);
      // Only Drizzle's migration receipt/sequence may change, not application state.
      assert.deepEqual(gamedaySchema(after), gamedaySchema(before));
      const previousTables=new Set(before.data.filter(t=>t.table_schema==="public").map(t=>t.table_name));
      assert.deepEqual(after.data.filter(t=>t.table_schema==="public" && previousTables.has(t.table_name)),
        before.data.filter(t=>t.table_schema==="public"));
      for(const table of ["terminal_log","terminal_game_manifest"])
        assert.deepEqual(after.data.find(t=>t.table_schema==="public" && t.table_name===table)?.rows,[]);
      migrate(url);
      assert.deepEqual(await state(pool), after);
    });
  });

  for (const mode of ["two-protected", "protected-older-plus-latest"]) {
    await t.test(`${mode}: abort, rollback, explicit revoke and successful retry`, async () => {
      await withDatabase(async (pool, url) => {
        await beforeGameday(url);
        await fixture(pool, mode);
        const before = await state(pool);
        cli(url, ["migrate"], 1);
        const output = await initContainer(url, 1);
        assert.match(output, mode === "two-protected" ? /code: 'P0001'/ : /code: '23505'/);
        assert.deepEqual(await state(pool), before, "Failed migration must leave data, approvals and journal unchanged");
        assert.equal(await applied(pool), 9);
        await pool.query("UPDATE terminal_restore_authorizations SET revoked_at=now() WHERE id=1 AND backup_id=1");
        await initContainer(url);
        assert.equal(await applied(pool), journal.entries.length);
        assert.deepEqual((await pool.query("SELECT id FROM terminal_config_backups ORDER BY id")).rows, [{ id: 2 }]);
        const after = await state(pool);
        migrate(url);
        assert.deepEqual(await state(pool), after);
      });
    });
  }
});

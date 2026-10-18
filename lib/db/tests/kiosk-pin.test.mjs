import test from "node:test";
import assert from "node:assert/strict";
import path from "node:path";
import os from "node:os";
import { mkdtemp, mkdir, copyFile, readFile, writeFile, rm } from "node:fs/promises";
import { createRequire } from "node:module";
import { randomUUID, createHash } from "node:crypto";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url), { Pool } = require("pg");
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
test("PIN/device access: forward + repeat migration, durable locks, epochs, origin and cookie contracts", { timeout: 60000 }, async () => {
  const admin = new Pool({ connectionString: process.env.DATABASE_URL });
  const name = `rm_kiosk_${randomUUID().replaceAll("-", "")}`;
  await admin.query(`CREATE DATABASE "${name}"`);
  const url = new URL(process.env.DATABASE_URL); url.pathname = `/${name}`;
  process.env.DATABASE_URL = url.href;
  let pool, server, staging;
  try {
    const database = await import("@workspace/db"); pool = database.pool;
    const { migrate } = await import("drizzle-orm/node-postgres/migrator");
    const folder = path.join(root, "lib/db/drizzle");
    const journal = JSON.parse(await readFile(path.join(folder, "meta/_journal.json"), "utf8"));
    staging = await mkdtemp(path.join(os.tmpdir(), "rm-kiosk-migrations-"));
    await mkdir(path.join(staging, "meta"));
    const previous = { ...journal, entries: journal.entries.filter(e => e.idx < 16) };
    await writeFile(path.join(staging, "meta/_journal.json"), JSON.stringify(previous));
    for (const entry of previous.entries) await copyFile(path.join(folder, `${entry.tag}.sql`), path.join(staging, `${entry.tag}.sql`));
    await migrate(database.db, { migrationsFolder: staging });
    await pool.query("INSERT INTO kiosk_tokens(name,token_hash,created_by) VALUES('Historical',repeat('a',64),1)");
    await migrate(database.db, { migrationsFolder: folder });
    assert.ok((await pool.query("SELECT revoked_at FROM kiosk_tokens")).rows[0].revoked_at);
    const count = (await pool.query("SELECT count(*)::int n FROM drizzle.__drizzle_migrations")).rows[0].n;
    await migrate(database.db, { migrationsFolder: folder });
    assert.equal((await pool.query("SELECT count(*)::int n FROM drizzle.__drizzle_migrations")).rows[0].n, count);
    assert.equal(count, journal.entries.length);
    const { getTableConfig } = await import("drizzle-orm/pg-core");
    for (const key of ["kioskDevicesTable", "kioskPinLimitsTable", "publicPageSettingsTable", "queueAuditTable"]) {
      const table = getTableConfig(database[key]);
      const actual = (await pool.query("SELECT column_name FROM information_schema.columns WHERE table_name=$1 AND table_schema='public'", [table.name])).rows.map(r => r.column_name).sort();
      assert.deepEqual(actual, table.columns.map(c => c.name).sort(), `${table.name} matches Drizzle schema`);
    }
    const jwt = require(path.join(root, "artifacts/api-server/node_modules/jsonwebtoken"));
    const player = (await pool.query(`INSERT INTO spieler(name,email,passwort_hash,aktiv,portal_aktiv,is_admin)
      VALUES('Access fixture','access@spec.invalid','test-only',true,true,true) RETURNING id`)).rows[0].id;
    const auth = `Bearer ${jwt.sign({ id: player }, process.env.JWT_SECRET ?? "trapmaster-dev-secret-change-in-prod")}`;
    const { default: app } = await import(path.join(root, "artifacts/api-server/src/app.ts"));
    server = app.listen(0, "127.0.0.1"); await new Promise(r => server.once("listening", r));
    const base = `http://127.0.0.1:${server.address().port}/api`;
    const pin = "628491", nextPin = "817365";
    const call = async (route, { method = "GET", body, cookie, admin: authorized, ip = "198.51.100.10", headers = {} } = {}) => {
      const response = await fetch(base + route, { method, headers: {
        "Content-Type": "application/json", "X-Forwarded-For": ip,
        ...(authorized ? { Authorization: auth } : {}), ...(cookie ? { Cookie: cookie } : {}), ...headers,
      }, body: body === undefined ? undefined : JSON.stringify(body) });
      const text = await response.text(); let data; try { data = JSON.parse(text); } catch { data = text; }
      assert.ok(!JSON.stringify(data).includes(pin) && !JSON.stringify(data).includes(nextPin), "PIN not echoed");
      return { status: response.status, data, headers: response.headers };
    };
    const unlock = (value = pin, options = {}) => call("/public/unlock", { method: "POST", body: { pin: value }, ...options });
    const cookieOf = result => result.headers.get("set-cookie")?.split(";")[0];
    assert.equal((await call("/public/queue")).status, 404);
    assert.equal((await unlock()).status, 404);
    await pool.query("UPDATE public_page_settings SET enabled=true,queue_enabled=true,groups_enabled=false WHERE id=1");
    assert.deepEqual((await call("/public/queue")).data.code, "kiosk_locked");
    assert.equal((await call("/admin/public-page/pin", { method: "PUT", body: { pin } })).status, 401);
    for (const value of ["12345", "abcdef", 628491]) {
      assert.equal((await call("/admin/public-page/pin", { method: "PUT", admin: true, body: { pin: value } })).status, 400);
    }
    const setPin = value => call("/admin/public-page/pin", { method: "PUT", admin: true, body: { pin: value } });
    assert.equal((await setPin(pin)).status, 200);
    const stored = (await pool.query("SELECT pin_hash,pin_epoch FROM public_page_settings")).rows[0];
    assert.match(stored.pin_hash, /^scrypt\$32768\$8\$1\$[0-9a-f]{32}\$[0-9a-f]{64}$/);
    const unlocked = await unlock();
    assert.equal(unlocked.status, 200);
    const cookie = cookieOf(unlocked); assert.ok(cookie);
    for (const flag of ["HttpOnly", "Secure", "SameSite=Lax", "Path=/", "Max-Age=31536000"]) assert.ok(unlocked.headers.get("set-cookie").includes(flag), flag);
    const raw = cookie.slice("rm_kiosk=".length); assert.equal(raw.length, 43);
    assert.equal((await pool.query("SELECT token_hash FROM kiosk_devices")).rows[0].token_hash, createHash("sha256").update(raw).digest("hex"));
    assert.equal((await call("/public/queue", { cookie })).status, 200);
    assert.equal((await call("/public/queue", { headers: { "X-Kiosk-Token": raw } })).data.code, "kiosk_locked");
    assert.equal((await call("/admin/kiosk-tokens", { admin: true })).status, 404, "old API removed");
    const devices = await call("/admin/public-page/devices", { admin: true });
    assert.equal(devices.data.devices.length, 1);
    const deviceId = devices.data.devices[0].id;
    assert.ok(!JSON.stringify(devices.data).includes(raw) && !JSON.stringify(devices.data).includes(stored.pin_hash));
    assert.equal((await call("/admin/public-page", { admin: true })).data.pinConfigured, true);
    await pool.query("UPDATE kiosk_devices SET last_used_at=now()-interval '2 days',last_renewed_at=now()-interval '2 days'");
    const renewed = await call("/public/queue", { cookie });
    assert.equal(cookieOf(renewed), cookie, "sliding renewal preserves credential");
    const stamp = (await pool.query("SELECT last_used_at FROM kiosk_devices")).rows[0].last_used_at;
    await call("/public/queue", { cookie });
    assert.equal((await pool.query("SELECT last_used_at FROM kiosk_devices")).rows[0].last_used_at.getTime(), stamp.getTime(), "30-second write throttle");
    await pool.query("UPDATE kiosk_devices SET last_renewed_at=now()-interval '2 days'");
    assert.ok(cookieOf(await call("/public/queue", { cookie })), "continuous use also renews");
    const mismatch = await call("/public/queue/signup", { cookie, method: "POST", body: {}, headers: { Origin: "https://evil.invalid" } });
    assert.equal(mismatch.status, 403); assert.equal(mismatch.data.code, "origin_mismatch");
    const group = await call("/public/groups", { cookie, method: "POST", body: { name: "Test", spielerIds: [player, player + 1] } });
    assert.equal(group.status, 422); assert.equal(group.data.code, "groups_disabled");
    const invalid = await call("/public/queue/signup", { cookie, method: "POST", body: { spielerId: player, modus: "NORMAL" } });
    assert.equal(invalid.status, 422); assert.equal(invalid.data.code, "queue_ineligible");
    assert.equal((await call("/public/queue", { cookie })).status, 200, "business/origin errors do not revoke access");
    const forwarded = await call("/public/groups", { cookie, method: "POST", body: {},
      headers: { Origin: "https://forwarded.spec.invalid", "X-Forwarded-Host": "forwarded.spec.invalid" } });
    assert.equal(forwarded.status, 422, "forwarded host accepted, business validation reached");
    process.env.PUBLIC_HOST = "configured.spec.invalid";
    assert.equal((await call("/public/groups", { cookie, method: "POST", body: {},
      headers: { Origin: "https://configured.spec.invalid" } })).status, 422, "configured host accepted");
    await pool.query(`INSERT INTO gameday_players(datum,spieler_id,added_at,terminal_id,occurred_at,external_id)
      VALUES((now() AT TIME ZONE 'Europe/Luxembourg')::date,$1,now(),'KIOSK-SPEC',now(),'kiosk-fixture-day')`, [player]);
    await pool.query(`INSERT INTO kredit_events(external_id,spieler_id,datum,typ,anzahl)
      VALUES('kiosk-fixture-credit',$1,(now() AT TIME ZONE 'Europe/Luxembourg')::date,'GRANT',10)`, [player]);
    const signup = { cookie, method: "POST", body: { spielerId: player, modus: "NORMAL" } };
    assert.equal((await call("/public/queue/signup", signup)).status, 201);
    const duplicate = await call("/public/queue/signup", signup);
    assert.equal(duplicate.status, 409); assert.equal(duplicate.data.code, "queue_duplicate");
    assert.equal((await setPin(nextPin)).status, 200);
    assert.equal((await call("/public/queue", { cookie })).data.code, "kiosk_locked");
    const second = cookieOf(await unlock(nextPin));
    assert.equal((await call(`/admin/public-page/devices/${(await call("/admin/public-page/devices", { admin: true })).data.devices[0].id}`, { admin: true, method: "DELETE" })).status, 200);
    assert.equal((await call("/public/queue", { cookie: second })).data.code, "kiosk_locked");
    const third = cookieOf(await unlock(nextPin));
    assert.equal((await call("/admin/public-page/devices/reset", { admin: true, method: "POST" })).status, 200);
    assert.equal((await call("/public/queue", { cookie: third })).data.code, "kiosk_locked");
    assert.equal((await call("/admin/public-page/devices", { admin: true })).status, 200, "portal admin session unaffected");
    await pool.query("DELETE FROM kiosk_pin_limits");
    for (const seconds of [900, 3600, 86400]) {
      for (let i = 1; i <= 5; i++) {
        const response = await unlock("000000");
        assert.equal(response.status, i === 5 ? 429 : 401);
        if (i === 5) assert.ok(response.data.retryAfterSec >= seconds - 1 && response.data.retryAfterSec <= seconds);
      }
      assert.equal((await unlock(nextPin)).status, 429, "correct PIN rejected during lock");
      await pool.query("UPDATE kiosk_pin_limits SET locked_until=now()-interval '1 second' WHERE key<>'global'");
    }
    assert.equal((await unlock(nextPin)).status, 200, "expiry permits correct PIN");
    await pool.query("DELETE FROM kiosk_pin_limits");
    for (let i = 0; i < 50; i++) {
      assert.equal((await unlock("000000", { ip: `198.51.100.${30 + i}` })).status, i === 49 ? 429 : 401);
    }
    assert.equal((await unlock(nextPin, { ip: "203.0.113.7" })).status, 429, "global failures limit all new unlocks");
    assert.equal((await pool.query("SELECT failures FROM kiosk_pin_limits WHERE key='global'")).rows[0].failures, 50);
    const audit = JSON.stringify((await pool.query("SELECT action,details FROM queue_audit")).rows);
    assert.ok(!audit.includes(pin) && !audit.includes(nextPin));
    assert.match(audit, /KIOSK_PIN_FAILED/); assert.match(audit, /KIOSK_PIN_LOCKED/);
    assert.equal((await pool.query("SELECT token_id FROM queue_audit WHERE device_id=$1", [deviceId])).rows.every(r => r.token_id === null), true);
  } finally {
    if (server) await new Promise(r => server.close(r));
    const pools = new Set(pool ? [pool] : []);
    try { pools.add(require("@workspace/db").pool); } catch { /* ESM-only loader */ }
    await Promise.all([...pools].filter(Boolean).map(p => p.end()));
    await admin.query(`DROP DATABASE "${name}" WITH (FORCE)`); await admin.end();
    if (staging) await rm(staging, { recursive: true, force: true });
  }
});

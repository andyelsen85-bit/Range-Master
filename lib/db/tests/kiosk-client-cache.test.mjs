import test from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { refreshKioskAccess, isKioskLocked } from "../../../artifacts/portal/src/lib/kiosk.ts";

const require = createRequire(new URL("../../../artifacts/portal/package.json", import.meta.url));
const { QueryClient, QueryObserver } = require("@tanstack/react-query");
test("post-reset PIN unlock clears old inactive auth errors before widgets remount", async () => {
  const client = new QueryClient({ defaultOptions: { queries: { retry: false, gcTime: Infinity } } });
  const queueKey = ["/api/public/queue"], lastKey = ["/api/public/last-game"], adminKey = ["/api/admin/public-page"];
  const locked = { status: 401, data: { code: "kiosk_locked" } };
  for (const key of [queueKey, lastKey]) {
    await client.fetchQuery({ queryKey: key, queryFn: async () => { throw locked; } }).catch(() => {});
    assert.equal(isKioskLocked(client.getQueryState(key).error), true);
  }
  client.setQueryData(adminKey, { enabled: true });
  const observer = new QueryObserver(client, { queryKey: queueKey, retry: false,
    queryFn: async () => ({ games: [], version: "new-cookie" }) });
  const unsubscribe = observer.subscribe(() => {});
  try {
    await refreshKioskAccess(client);
    assert.equal(client.getQueryState(queueKey).error, null);
    assert.equal(client.getQueryData(queueKey).version, "new-cookie");
    assert.equal(client.getQueryState(lastKey).error, null, "inactive error cannot broadcast a stale lock on mount");
    assert.equal(client.getQueryState(lastKey).status, "pending");
    assert.deepEqual(client.getQueryData(adminKey), { enabled: true }, "admin session/settings cache untouched");
    await client.fetchQuery({ queryKey: lastKey, queryFn: async () => null });
    assert.equal(client.getQueryState(lastKey).error, null);
  } finally { unsubscribe(); client.clear(); }
});
test("only exact kiosk_locked HTTP 401 is an access error", () => {
  assert.equal(isKioskLocked({ status: 401, data: { code: "kiosk_locked" } }), true);
  for (const status of [400, 403, 409, 422, 500]) {
    assert.equal(isKioskLocked({ status, data: { code: "kiosk_locked" } }), false);
  }
  assert.equal(isKioskLocked({ status: 401, data: { code: "pin_invalid" } }), false);
  assert.equal(isKioskLocked(new TypeError("network failed")), false);
});

import test from "node:test";
import assert from "node:assert/strict";
import { playerPurchaseScope } from "./spieler";

test("purchase history always scopes to the authenticated player", () => {
  assert.equal(playerPurchaseScope({ id: 17 }), 17);
});
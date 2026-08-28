import test from "node:test";
import assert from "node:assert/strict";
import { sameImmutableChildren } from "./immutable-events";

test("immutable event child comparison ignores order but detects divergent payloads", () => {
  const stored = [{ spielerId: 2, punkte: 7 }, { spielerId: 1, punkte: 4 }];
  assert.equal(sameImmutableChildren(stored, [...stored].reverse()), true);
  assert.equal(sameImmutableChildren(stored, [{ spielerId: 1, punkte: 4 }, { spielerId: 2, punkte: 8 }]), false);
});
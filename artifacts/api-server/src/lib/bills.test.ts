import test from "node:test";
import assert from "node:assert/strict";
import { isActivityAfterPayment, isBillableCreditEvent, isSettlementRedundant, periodSettlementState } from "./bills";

test("only consumed game credits are billable", () => {
  assert.equal(isBillableCreditEvent("USE"), true);
  assert.equal(isBillableCreditEvent("GRANT"), false);
});

test("a settlement is redundant only when no billable activity followed it", () => {
  assert.equal(isSettlementRedundant({ hasPayment: true, hasLaterBillableActivity: false }), true);
  assert.equal(isSettlementRedundant({ hasPayment: true, hasLaterBillableActivity: true }), false);
  assert.equal(isSettlementRedundant({ hasPayment: false, hasLaterBillableActivity: true }), false);
});

test("a post-payment upload reopens by server createdAt even when occurredAt is older", () => {
  const payment = new Date("2026-01-01T12:00:00.000Z");
  const terminalOccurredAt = new Date("2026-01-01T09:00:00.000Z");
  const serverCreatedAt = new Date("2026-01-01T12:01:00.000Z");
  assert.equal(terminalOccurredAt < payment, true);
  assert.equal(isActivityAfterPayment(serverCreatedAt, payment), true);
});

test("period state remains open when open amounts from different days net to zero", () => {
  assert.equal(periodSettlementState(2, 2), "OPEN");
  assert.equal(periodSettlementState(0, 2), "PAID");
  assert.equal(periodSettlementState(0, 0), "PENDING_NEUTRAL");
});
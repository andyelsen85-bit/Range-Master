import { beforeEach, describe, it } from 'node:test';
import assert from 'node:assert';
import { todayStr, useGameStore } from './src/store/gameStore';

describe('projected billing overlays', () => {
  const today = todayStr();

  beforeEach(() => {
    useGameStore.setState({
      kreditDatum: today,
      verkaufDatum: today,
      kredite: { 7: { gewaehrt: 4, verbraucht: 1 } },
      pendingKredite: [
        { externalId: 'grant', spielerId: 7, datum: today, typ: 'GRANT', anzahl: 4 },
        {
          externalId: 'use', spielerId: 7, datum: today, typ: 'USE', anzahl: 1,
          occurredAt: `${today}T10:00:00.000Z`, priceRevisionId: 10, unitPriceCents: 700,
        },
      ],
      pendingVerkaeufe: [
        { externalId: 'ammo', spielerId: 7, datum: today, productId: 2, priceRevisionId: 22, quantity: 2 },
      ],
      verkaeufe: [],
      pendingGames: [],
      paidBillCache: {},
      daySummary: {
        datum: today,
        players: [{
          spielerId: 7, spielerName: 'Ada', mitgliedNr: null, lines: [],
          categorySubtotals: {}, totalCents: 0,
          credit: { granted: 0, used: 0, remaining: 0 },
          games: 0, completedGames: 0,
          state: 'PAID', paymentExternalId: 'old-payment', paidAt: 'now',
        }],
        categorySubtotals: {}, productTotals: {}, generalTotalCents: 0,
        uniquePlayers: 1, paidPlayers: 1, games: 0, completedGames: 0, confirmedClays: 0,
      },
      produkte: [
        { id: 1, code: 'GAME_CREDIT', category: 'GAME_CREDIT', name: 'Credit', active: true, currentPrice: { id: 11, productId: 1, unitPriceCents: 600, effectiveFrom: today } },
        { id: 2, code: 'AMMO_CAL12', category: 'AMMO_CAL12', name: 'Ammo', active: true, currentPrice: { id: 22, productId: 2, unitPriceCents: 1000, effectiveFrom: today } },
      ],
    });
  });

  it('charges pending USE and sales once, never grants, and reopens a paid baseline', () => {
    const projected = useGameStore.getState().getProjectedDaySummary(7);
    assert.ok(projected);
    assert.strictEqual(projected.totalCents, 2700);
    assert.strictEqual(projected.state, 'OPEN');
    assert.strictEqual(projected.lines.length, 2);
    assert.ok(projected.lines.every(line => line.pending));
    assert.strictEqual(projected.lines.find(line => line.productId === 1)?.quantity, 1);
    assert.strictEqual(projected.lines.find(line => line.productId === 1)?.unitPriceCents, 700);
  });
});
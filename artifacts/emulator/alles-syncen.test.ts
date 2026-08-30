import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert';
import { todayStr, useGameStore } from './src/store/gameStore';

describe('Alles Syncen E2E', () => {
  const today = todayStr();
  let originalFetch: typeof globalThis.fetch;

  beforeEach(() => {
    originalFetch = globalThis.fetch;
    useGameStore.setState({
      apiUrl: 'http://localhost',
      apiKey: 'test-key',
      pendingGames: [
        {
          externalId: 'test-uuid-1',
          datum: '2024-01-01T12:00:00Z',
          modus: 'NORMAL',
          lauf: 1,
          taubenProLauf: 1,
          abgeschlossen: true,
          teilnahmen: [],
          ergebnisse: [],
          confirmedLaunches: 1,
        }
      ],
      pendingPayments: [
        { externalId: 'pay-accepted', spielerId: 10, datum: today },
        { externalId: 'pay-conflict', spielerId: 11, datum: today },
        { externalId: 'pay-skipped', spielerId: 12, datum: today },
      ],
      kredite: {
        10: { gewaehrt: 5, verbraucht: 5 },
        11: { gewaehrt: 5, verbraucht: 5 },
        12: { gewaehrt: 5, verbraucht: 5 },
        13: { gewaehrt: 5, verbraucht: 5 },
      },
      lineup: [
        { spielerId: 10, startPosten: 1 },
        { spielerId: 11, startPosten: 2 },
        { spielerId: 12, startPosten: 3 },
      ],
      spielerUpdates: [],
      pendingSpieler: [],
      pendingKredite: [],
      pendingVerkaeufe: [],
      verkaufDatum: today,
      kreditDatum: today,
    });
  });

  afterEach(() => {
    globalThis.fetch = originalFetch;
  });

  it('syncAllPending processes games and payments concurrently without short-circuiting', async () => {
    let gamesCalled = false;
    let paymentsCalled = false;

    globalThis.fetch = async (input, init) => {
      const url = input.toString();
      
      if (url.includes('/api/sync/spiele') && init?.method === 'POST') {
        gamesCalled = true;
        // simulate failure in games queue to prove non-short-circuiting
        return { ok: false, status: 500 } as any;
      }
      
      if (url.includes('/api/sync/payments') && init?.method === 'POST') {
        paymentsCalled = true;
        return {
          ok: true,
          json: async () => ({
            results: [
              { externalId: 'pay-accepted', status: 'accepted' },
              { externalId: 'pay-conflict', status: 'conflict' },
              { externalId: 'pay-skipped', status: 'skipped' }
            ]
          })
        } as any;
      }

      if (url.includes('/api/sync/spiele?limit=100') && (!init?.method || init?.method === 'GET')) {
        return {
          ok: true,
          json: async () => ({ spiele: [] })
        } as any;
      }
      
      if (url.includes('/api/sync/bills/day-summary')) {
        return {
          ok: true,
          json: async () => ({ datum: today, players: [] })
        } as any;
      }

      return { ok: true, json: async () => ({}) } as any;
    };

    await useGameStore.getState().syncAllPending();

    const state = useGameStore.getState();

    // Verify both requests were fired
    assert.strictEqual(gamesCalled, true, 'Games sync should have been called');
    assert.strictEqual(paymentsCalled, true, 'Payments sync should have been called despite game sync failure');

    // Verify syncStatus is 'error' because games failed
    assert.strictEqual(state.syncStatus, 'error');

    // Verify pending games are retained due to failure
    assert.strictEqual(state.pendingGames.length, 1);

    // Verify payments queue resolution
    // 'pay-accepted' and 'pay-skipped' should be removed, 'pay-conflict' retained
    assert.strictEqual(state.pendingPayments.length, 1);
    assert.strictEqual(state.pendingPayments[0].externalId, 'pay-conflict');

    // Verify players removed from current-day kredite & lineup for accepted/skipped
    assert.strictEqual(state.kredite[10], undefined, 'Player 10 (accepted) should be removed from kredite');
    assert.strictEqual(state.kredite[12], undefined, 'Player 12 (skipped) should be removed from kredite');
    assert.ok(state.kredite[11] !== undefined, 'Player 11 (conflict) should remain in kredite');
    assert.ok(state.kredite[13] !== undefined, 'Player 13 (no payment) should remain in kredite');

    assert.strictEqual(state.lineup.find(l => l.spielerId === 10), undefined);
    assert.strictEqual(state.lineup.find(l => l.spielerId === 12), undefined);
    assert.ok(state.lineup.find(l => l.spielerId === 11) !== undefined);
  });
});

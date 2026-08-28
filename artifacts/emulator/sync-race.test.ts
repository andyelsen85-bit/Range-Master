import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert';
import { useGameStore } from './src/store/gameStore';

describe('Sync Race Condition & Conflict Retention', () => {
  let originalFetch: typeof globalThis.fetch;

  beforeEach(() => {
    originalFetch = globalThis.fetch;
    useGameStore.setState({
      apiUrl: 'http://localhost',
      apiKey: 'test-key',
      pendingGames: [
        { externalId: 'g1', datum: '2024-01-01', modus: 'NORMAL', lauf: 1, taubenProLauf: 1, abgeschlossen: true, teilnahmen: [], ergebnisse: [], confirmedLaunches: 0 }
      ],
      pendingVerkaeufe: [
        { externalId: 'v1', spielerId: 10, datum: '2024-01-01', productId: 10, quantity: 1, priceRevisionId: 1 }
      ],
      pendingKredite: [
        { externalId: 'k1', spielerId: 10, datum: '2024-01-01', typ: 'GRANT', anzahl: 1 }
      ],
      produkte: [
        { id: -1, code: 'GAME_CREDIT', category: 'GAME_CREDIT', name: 'Credit', active: true, currentPrice: null },
        { id: 10, code: 'AMMO', category: 'AMMO', name: 'Ammo', active: true, currentPrice: { id: 1, productId: 10, unitPriceCents: 1000, effectiveFrom: '2024-01-01' } },
      ],
      kredite: {},
      verkaeufe: [],
      pendingPayments: [],
      paidBillCache: {},
      daySummary: null,
      spielerUpdates: [],
      pendingSpieler: [],
      verkaufDatum: '2024-01-01',
      kreditDatum: '2024-01-01',
    });
  });

  afterEach(() => {
    globalThis.fetch = originalFetch;
  });

  it('preserves items added to queues while sync request is in flight', async () => {
    let gamesResolver: (v: any) => void;
    let salesResolver: (v: any) => void;
    let krediteResolver: (v: any) => void;

    const gamesPromise = new Promise(r => gamesResolver = r);
    const salesPromise = new Promise(r => salesResolver = r);
    const kreditePromise = new Promise(r => krediteResolver = r);

    globalThis.fetch = async (input, init) => {
      const url = input.toString();
      if (url.includes('/api/sync/spiele') && init?.method === 'POST') {
        await gamesPromise;
        return { ok: true, json: async () => ({ results: [{ externalId: 'g1', status: 'conflict' }] }) } as any;
      }
      if (url.includes('/api/sync/sales') && init?.method === 'POST') {
        await salesPromise;
        return { ok: true, json: async () => ({}) } as any; // mock count-only return
      }
      if (url.includes('/api/sync/kredite') && init?.method === 'POST') {
        await kreditePromise;
        return { ok: true, json: async () => ({}) } as any; // mock count-only return
      }
      return { ok: true, json: async () => ({}) } as any;
    };

    // Start sync without awaiting immediately
    const syncPromise = useGameStore.getState().syncAllPending();

    // While in flight, add one of each to pending queues
    useGameStore.getState().addVerkauf(10, 10, 5); // Adds v2
    useGameStore.getState().addKredite(10, 5);     // Adds k2
    await new Promise(r => setTimeout(r, 10));
    useGameStore.setState(s => ({
      pendingGames: [...s.pendingGames, { externalId: 'g2', datum: '2024-01-01', modus: 'NORMAL', lauf: 1, taubenProLauf: 1, abgeschlossen: true, teilnahmen: [], ergebnisse: [], confirmedLaunches: 0 }]
    }));

    // Resolve network responses
    gamesResolver!({});
    salesResolver!({});
    krediteResolver!({});

    await syncPromise;

    const state = useGameStore.getState();

    // Verify v1/k1/g1 are gone (or preserved for conflict), and v2/k2/g2 remain
    assert.strictEqual(state.pendingVerkaeufe.length, 1);
    assert.strictEqual(state.pendingKredite.length, 1);
    // g1 should remain because of 'conflict', g2 because it was added in-flight
    // console.log(state.pendingGames);
    assert.strictEqual(state.pendingGames.length, 2);
    
    assert.ok(state.pendingVerkaeufe.every(v => v.quantity === 5));
    assert.ok(state.pendingKredite.every(k => k.anzahl === 5));
    assert.ok(state.pendingGames.some(g => g.externalId === 'g1'));
    assert.ok(state.pendingGames.some(g => g.externalId === 'g2'));
  });
});

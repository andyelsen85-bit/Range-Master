import { describe, it, beforeEach } from 'node:test';
import assert from 'node:assert';
import { useGameStore } from './src/store/gameStore';

describe('Local Bill & Payment Cache', () => {
  beforeEach(() => {
    const storage = new Map<string, string>();
    Object.defineProperty(globalThis, 'localStorage', {
      configurable: true,
      value: {
        getItem: (key: string) => storage.get(key) ?? null,
        setItem: (key: string, value: string) => storage.set(key, value),
        removeItem: (key: string) => storage.delete(key),
        clear: () => storage.clear(),
      },
    });
    useGameStore.setState({
      apiUrl: 'http://localhost',
      apiKey: 'test-key',
      kredite: {},
      pendingKredite: [],
      pendingVerkaeufe: [],
      pendingPayments: [],
      paidBillCache: {},
      daySummary: null,
      spielerUpdates: [],
      pendingSpieler: [],
      verkaufDatum: useGameStore.getState().verkaufDatum,
      kreditDatum: useGameStore.getState().kreditDatum,
      produkte: [
        { id: -1, code: 'GAME_CREDIT', category: 'GAME_CREDIT', name: 'Credit', active: true, currentPrice: null },
        { id: 10, code: 'AMMO', category: 'AMMO', name: 'Ammo', active: true, currentPrice: { id: 1, productId: 10, unitPriceCents: 1000, effectiveFrom: '2024-01-01' } },
      ],
      serverVerkaeufe: [],
    });
  });

  it('generates a projected bill for local player and retains it after payment sync', async () => {
    const store = useGameStore.getState();

    // 1. Grant local credits and sell ammo offline
    store.addKredite(99, 2);
    store.addVerkauf(99, 10, 1);
    
    // Validate projection exists
    const proj1 = useGameStore.getState().getProjectedDaySummary(99);
    assert.ok(proj1, 'Projected summary should exist');
    assert.strictEqual(proj1.credit.granted, 2);
    assert.strictEqual(proj1.lines.length, 1);
    assert.strictEqual(proj1.lines[0].totalCents, 1000);
    assert.strictEqual(proj1.totalCents, 1000);

    // 2. Mark bill as paid
    useGameStore.getState().markBillPaid(99);
    assert.strictEqual(useGameStore.getState().pendingPayments.length, 1);

    // 3. Mock syncAllPending behavior explicitly for accepted payment
    let fetchCalled = false;
    const originalFetch = globalThis.fetch;
    globalThis.fetch = async (input, init) => {
      if (input.toString().includes('/api/sync/payments') && init?.method === 'POST') {
        fetchCalled = true;
        return {
          ok: true,
          json: async () => ({
            results: [{ externalId: useGameStore.getState().pendingPayments[0].externalId, status: 'accepted' }]
          })
        } as any;
      }
      return { ok: true, json: async () => ({}) } as any;
    };

    try {
      // 4. Run sync
      await useGameStore.getState().syncAllPending();
      
      const afterSync = useGameStore.getState();
      assert.strictEqual(fetchCalled, true);
      assert.strictEqual(afterSync.pendingPayments.length, 0);
      assert.strictEqual(afterSync.kredite[99], undefined, 'Player should be removed from active kredite ledger');
      
      // 5. Verify local paid cache kicks in
      const proj2 = afterSync.getProjectedDaySummary(99);
      assert.ok(proj2, 'Projected summary should be retained from cache');
      assert.strictEqual(proj2.state, 'PAID');
      assert.strictEqual(proj2.totalCents, 1000);

      const persisted = JSON.parse(localStorage.getItem('rangemaster-paid-bill-cache') ?? '{}');
      assert.strictEqual(persisted.players['99'].totalCents, 1000);

      // A later purchase opens a fresh balance rather than charging the paid
      // receipt again.
      afterSync.addVerkauf(99, 10, 1);
      const reopened = useGameStore.getState().getProjectedDaySummary(99);
      assert.ok(reopened);
      assert.strictEqual(reopened.state, 'OPEN');
      assert.strictEqual(reopened.totalCents, 1000);
    } finally {
      globalThis.fetch = originalFetch;
    }
  });
});

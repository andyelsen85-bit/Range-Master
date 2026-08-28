import { describe, it } from 'node:test';
import assert from 'node:assert';
import { useGameStore, normalizeProdukte } from './src/store/gameStore';

describe('Catering PIN & Store logic', () => {
  it('prevents pin bypass without old pin', async () => {
    // initial state
    useGameStore.setState({
      kioskPinHash: null,
      kioskPinSalt: null,
      kioskFailedAttempts: 0,
      kioskLockoutUntil: null,
    });
    
    // Set a PIN
    const store = useGameStore.getState();
    await store.setKioskPin(null, '1234');
    
    const store2 = useGameStore.getState();
    assert.ok(store2.kioskPinHash !== null);
    
    // Try to change without old PIN
    const res = await store2.setKioskPin(null, '9999');
    assert.strictEqual(res.success, false);
    assert.strictEqual(res.error, 'Current PIN required');
    
    // Try to change with wrong old PIN
    const res2 = await store2.setKioskPin('0000', '9999');
    assert.strictEqual(res2.success, false);
    assert.strictEqual(res2.error, 'Incorrect current PIN');
    
    // Try to change with correct old PIN
    const res3 = await store2.setKioskPin('1234', '9999');
    assert.strictEqual(res3.success, true);
  });
  
  it('lockout after 5 wrong attempts', async () => {
    useGameStore.setState({
      kioskPinHash: null,
      kioskPinSalt: null,
      kioskFailedAttempts: 0,
      kioskLockoutUntil: null,
    });
    
    const store = useGameStore.getState();
    await store.setKioskPin(null, '1234');
    const s2 = useGameStore.getState();
    
    // 4 wrong attempts
    for (let i = 0; i < 4; i++) {
      const ok = await s2.verifyKioskPin('0000');
      assert.strictEqual(ok, false);
      assert.strictEqual(useGameStore.getState().kioskFailedAttempts, i + 1);
    }
    
    // 5th wrong attempt triggers lockout
    const ok = await s2.verifyKioskPin('0000');
    assert.strictEqual(ok, false);
    assert.strictEqual(useGameStore.getState().kioskFailedAttempts, 5);
    assert.ok(useGameStore.getState().kioskLockoutUntil !== null);
    
    // Lockout in effect - correct PIN is now rejected
    const ok2 = await s2.verifyKioskPin('1234');
    assert.strictEqual(ok2, false);
  });
  
  it('atomic queueing of catering basket', async () => {
    // setup valid player and product
    useGameStore.setState({
      kioskMode: 'CATERING',
      kredite: {
        100: { gewaehrt: 10, verbraucht: 0 },
        101: { gewaehrt: 10, verbraucht: 0 } // no matching player object
      },
      portalSpieler: [
        { id: 100, name: 'Active Player', portalAktiv: true },
        { id: 102, name: 'Inactive Player', portalAktiv: false }
      ],
      produkte: [
        { id: 99, category: 'FOOD', active: true, name: 'Burger', code: 'burger', currentPrice: { id: 1, unitPriceCents: 500, productId: 99, effectiveFrom: '' } }
      ],
      pendingVerkaeufe: [],
    });
    
    const s = useGameStore.getState();
    
    // Valid transaction
    const r1 = s.queueCateringBasket(100, { 99: 2 });
    assert.strictEqual(r1.success, true);
    
    const after1 = useGameStore.getState();
    assert.strictEqual(after1.pendingVerkaeufe.length, 1);
    assert.strictEqual(after1.pendingVerkaeufe[0].productId, 99);
    assert.strictEqual(after1.pendingVerkaeufe[0].quantity, 2);
    
    // Invalid transaction - stale player (no object)
    const r2 = after1.queueCateringBasket(101, { 99: 2 });
    assert.strictEqual(r2.success, false);
    assert.match(r2.error!, /Onbekannte Spiller/);
    
    // Invalid transaction - inactive player
    useGameStore.setState({ kredite: { ...after1.kredite, 102: { gewaehrt: 10, verbraucht: 0 } } });
    const after2 = useGameStore.getState();
    const r2_inactive = after2.queueCateringBasket(102, { 99: 2 });
    assert.strictEqual(r2_inactive.success, false);
    assert.match(r2_inactive.error!, /Spiller ass inaktiv/);
    
    // Invalid transaction - invalid quantity
    const r3 = after2.queueCateringBasket(100, { 99: -1 });
    assert.strictEqual(r3.success, false);
    assert.match(r3.error!, /Invalid quantity/);
    
    // State remains unchanged by failed transaction
    const after3 = useGameStore.getState();
    assert.strictEqual(after3.pendingVerkaeufe.length, 1); // still 1
  });

  it('normalizeProdukte preserves FOOD/DRINK and guarantees defaults', () => {
    const raw = [
      { id: 50, code: 'BEER', category: 'DRINK', name: 'Beer', active: true, currentPrice: { id: 2, unitPriceCents: 300, productId: 50, effectiveFrom: '' } },
      { id: -2, code: 'AMMO_CAL12', category: 'AMMO_CAL12', name: 'Cal 12 (Custom)', active: true, currentPrice: null }
    ];

    const result = normalizeProdukte(raw);
    
    // Defaults exist
    assert.ok(result.find(p => p.code === 'GAME_CREDIT'));
    assert.ok(result.find(p => p.code === 'AMMO_CAL12'));
    assert.ok(result.find(p => p.code === 'AMMO_CAL20'));
    
    // Values merged
    assert.strictEqual(result.find(p => p.code === 'AMMO_CAL12')?.name, 'Cal 12 (Custom)');
    
    // Custom preserved
    const beer = result.find(p => p.code === 'BEER');
    assert.ok(beer);
    assert.strictEqual(beer?.category, 'DRINK');
    assert.strictEqual(beer?.currentPrice?.unitPriceCents, 300);
  });
});

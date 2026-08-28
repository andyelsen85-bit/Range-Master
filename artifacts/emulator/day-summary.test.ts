import { describe, it, beforeEach } from 'node:test';
import assert from 'node:assert';
import { useGameStore } from './src/store/gameStore';

describe('Day Summary and Bill Parity', () => {
  beforeEach(() => {
    useGameStore.setState({
      screen: 'spiel',
      kredite: {},
      pendingPayments: [],
      spieler: [],
      lineup: [],
      verkaufDatum: '2024-01-01'
    });
  });

  it('marks a bill paid idempotently and adds pending payment', () => {
    useGameStore.setState({
      kredite: { 5: { gewaehrt: 10, verbraucht: 5 } },
      spieler: [{ id: 5, name: 'Test', punkte: 0, startPosten: 1 }],
      lineup: [{ spielerId: 5, startPosten: 1 }]
    });

    useGameStore.getState().markBillPaid(5);

    const s1 = useGameStore.getState();
    assert.strictEqual(s1.pendingPayments.length, 1);
    assert.strictEqual(s1.pendingPayments[0].spielerId, 5);
    
    // Should NOT remove from kredite/lineup yet since it stays offline until synced
    assert.ok(s1.kredite[5] !== undefined, 'Does not remove from kredite before sync');

    // Duplicate call shouldn't duplicate payment
    s1.markBillPaid(5);
    const s2 = useGameStore.getState();
    assert.strictEqual(s2.pendingPayments.length, 1, 'Should be idempotent');
  });
  
  it('counts confirmed clays correctly through game flow', () => {
    useGameStore.setState({
      screen: 'spiel',
      spieler: [{ id: 10, name: 'S1', punkte: 0, startPosten: 1 }],
      lineup: [{ spielerId: 10, startPosten: 1 }],
      ergebnisse: [],
      confirmedLaunches: 0,
      taubeGeworfen: false,
      taubeIndex: 0,
      spielerIndex: 0,
      lauf: 1,
      sequenz: [{ maschine: 'A' }]
    });
    
    // Score without ACK should NOT increment clay count
    useGameStore.getState().eintragenErgebnis(true, false);
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 0);

    // Undo the accidental score entry so we're back at the start
    useGameStore.getState().wiederholenTaube();

    // Now throw the clay (simulates ACK)
    useGameStore.getState().werfenTaube();
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 1);
    
    // Entering score now shouldn't double-increment
    useGameStore.getState().eintragenErgebnis(true, false);
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 1);
    
    // Replay decreases ergebnisse but NOT confirmedLaunches
    useGameStore.getState().wiederholenTaube();
    assert.strictEqual(useGameStore.getState().ergebnisse.length, 0);
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 1);

    // Throwing again increments again
    useGameStore.getState().werfenTaube();
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 2);
  });
});

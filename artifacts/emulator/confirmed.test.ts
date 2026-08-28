import { describe, it, beforeEach } from 'node:test';
import assert from 'node:assert';
import { useGameStore } from './src/store/gameStore';

describe('Confirmed Launches Logic', () => {
  beforeEach(() => {
    useGameStore.setState({
      screen: 'spiel',
      spieler: [{ id: 1, name: 'T1', punkte: 0, startPosten: 1 }],
      lineup: [{ spielerId: 1, startPosten: 1 }],
      ergebnisse: [],
      confirmedLaunches: 0,
      taubeGeworfen: false,
      taubeIndex: 0,
      spielerIndex: 0,
      lauf: 1,
      modus: 'NORMAL',
      sequenz: [{ maschine: 'A' }]
    });
  });

  it('score without ACK excludes clay count', () => {
    useGameStore.getState().eintragenErgebnis(true, false);
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 0);
  });

  it('ACK adds clay, double tap ACK does not duplicate', () => {
    useGameStore.getState().werfenTaube();
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 1);
    useGameStore.getState().werfenTaube(); // duplicate
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 1);
    
    useGameStore.getState().eintragenErgebnis(true, false);
    // Move to next player (but none left) -> next taube -> but none left -> next lauf -> none left -> resultate
    
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 1);
    assert.strictEqual(useGameStore.getState().taubeGeworfen, false); // should be reset
  });

  it('H/custom pairs acknowledge 2 clays', () => {
    useGameStore.setState({
      sequenz: [{ maschine: 'H', doubletteNr: 1, pairKind: 'h', partner: 'H' }]
    });
    useGameStore.getState().werfenTaube();
    assert.strictEqual(useGameStore.getState().confirmedLaunches, 2);
  });
});

import { beforeEach, afterEach, test } from 'node:test';
import assert from 'node:assert/strict';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { useGameStore, type DaySummary } from './src/store/gameStore';
import { TagesstatistikView } from './src/screens/TagesstatistikView';

const originalFetch = globalThis.fetch;
const today = useGameStore.getState().verkaufDatum;
const sample: DaySummary = {
  datum: today, players: [], categorySubtotals: { GETRAENKE: 1250 },
  productTotals: { Wasser: 1250 }, generalTotalCents: 1250,
  uniquePlayers: 5, paidPlayers: 2, games: 8, completedGames: 7, confirmedClays: 175,
};

beforeEach(() => {
  useGameStore.setState({
    apiUrl: 'http://test-portal', apiKey: 'synthetic-test-only-key',
    daySummary: sample, daySummaryError: null, daySummaryLaden: false, verkaufDatum: today,
  });
});
afterEach(() => { globalThis.fetch = originalFetch; });

test('refresh of unchanged totals succeeds without a guessed failure', async () => {
  globalThis.fetch = async () => ({ ok: true, json: async () => sample }) as Response;
  await useGameStore.getState().ladeDaySummary();
  assert.equal(useGameStore.getState().daySummaryError, null);
  assert.equal(useGameStore.getState().daySummaryLaden, false);
  assert.deepEqual(useGameStore.getState().daySummary, sample);
});

test('failed refresh retains last good report and exposes a real error', async () => {
  globalThis.fetch = async () => ({ ok: false, status: 401 }) as Response;
  await useGameStore.getState().ladeDaySummary();
  assert.equal(useGameStore.getState().daySummary, sample);
  assert.equal(useGameStore.getState().daySummaryError, 'HTTP 401');
  assert.equal(useGameStore.getState().daySummaryLaden, false);
});

test('a response for another date cannot replace the displayed report', async () => {
  globalThis.fetch = async () => ({ ok: true, json: async () => ({ ...sample, datum: '1999-01-01' }) }) as Response;
  await useGameStore.getState().ladeDaySummary();
  assert.equal(useGameStore.getState().daySummary, sample);
  assert.match(useGameStore.getState().daySummaryError!, /angeforderten Tag/);
});

test('readable cards and category/product amounts render from actual report data', () => {
  // SSR reads the vanilla store's initial snapshot rather than its current
  // browser snapshot; modify and restore that snapshot only inside this test.
  const initial = useGameStore.getInitialState();
  const saved = { ...initial };
  try {
    Object.assign(initial, useGameStore.getState());
    const html = renderToStaticMarkup(createElement(TagesstatistikView));
    for (const label of ['Gesamtumsatz', 'Spieler', 'Spiele', 'Abgeschlossen',
                         'Tauben bestätigt', 'Rechnungen bezahlt', 'Wasser', '12,50 €']) {
      assert.ok(html.includes(label), label);
    }
    assert.match(html, /data-testid="confirmed-clays"[^>]*>175</);
    Object.assign(initial, { daySummary: null });
    const empty = renderToStaticMarkup(createElement(TagesstatistikView));
    assert.match(empty, /Keine Tagesabrechnung/);
    assert.ok(!empty.includes('>175<'));
  } finally { Object.assign(initial, saved); }
});
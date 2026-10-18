import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScorecard } from "../src/index.ts";

const game = { datum: "2026-10-18", modus: "HARAKIRI", lauf: 1, taubenProLauf: 4 };
const players = [{ id: 1, name: "One" }, { id: 2, name: "Two" }];
const row = (id, clay, machine, post, score, run = 1) => ({
  spielerId: id, lauf: run, taube: clay, maschine: machine, posten: post, punkte: score, wiederholt: false,
});
test("Harakiri: independent shot orders align by machine, H partners stay distinct", () => {
  const ledger = [
    row(1,1,"H",1,2), row(1,2,"H",1,0), row(1,3,"A",2,1), row(1,4,"B",3,2),
    row(2,1,"B",2,0), row(2,2,"A",3,2), row(2,3,"H",4,0), row(2,4,"H",4,2),
  ];
  const card = buildScorecard(game, players, ledger);
  assert.deepEqual(card.columns.map(c => c.machine), ["A","B","H1","H2"]);
  assert.deepEqual(card.columns.map(c => c.pair), [null,null,"start","end"]);
  assert.deepEqual(card.players.map(p => p.cells), [[1,2,2,0],[2,0,0,2]]);
  assert.deepEqual(card.players.map(p => p.total), [5,4]);
});
test("Harakiri Custom: single machines and identical letters in the pair never merge", () => {
  const ledger = [
    row(1,1,"A",1,2), row(1,2,"F",1,0), row(1,3,"F",2,1), row(1,4,"A",3,0),
    row(2,1,"F",2,2), row(2,2,"A",3,1), row(2,3,"A",4,0), row(2,4,"F",4,2),
  ];
  const card = buildScorecard({...game,modus:"HARAKIRI_CUSTOM"}, players, ledger);
  assert.deepEqual(card.columns.map(c => c.machine), ["A","F","A","F"]);
  assert.deepEqual(card.columns.map(c => c.pair), [null,null,"start","end"]);
  assert.deepEqual(card.players.map(p => p.cells), [[0,1,2,0],[1,2,0,2]]);
});
test("Two runs, unordered rows and repeated attempts keep per-player machine totals", () => {
  const ledger = [
    row(1,1,"A",1,2),row(1,2,"B",2,1),row(2,1,"B",1,0),row(2,2,"A",2,2),
    row(1,1,"B",1,2,2),row(1,2,"A",2,0,2),row(2,1,"A",1,1,2),row(2,2,"B",2,2,2),
    {...row(1,1,"A",1,0),wiederholt:true},
  ].reverse();
  const card = buildScorecard({...game,lauf:2,taubenProLauf:2},players,ledger);
  assert.deepEqual(card.columns.map(c => c.machine), ["A","B","A","B"]);
  assert.deepEqual(card.players.map(p => p.cells), [[2,1,0,2],[2,0,1,2]]);
  assert.deepEqual(card.players.map(p => p.runTotals), [[3,2],[2,3]]);
  assert.deepEqual(card.players.map(p => p.total), [5,5]);
});
test("Normal and Custom 1-4 retain shared shooting-order columns", () => {
  for (const mode of ["NORMAL","CUSTOM_1","CUSTOM_2","CUSTOM_3","CUSTOM_4"]) {
    const card = buildScorecard({...game,modus:mode,taubenProLauf:2},players,
      [row(1,1,"B",1,2),row(1,2,"A",2,0),row(2,1,"B",2,1),row(2,2,"A",3,2)]);
    assert.deepEqual(card.columns.map(c => c.machine), ["B","A"]);
    assert.deepEqual(card.players.map(p => p.cells), [[2,0],[1,2]]);
  }
});

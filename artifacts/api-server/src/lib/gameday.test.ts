import { test } from "node:test";
import assert from "node:assert/strict";
import { randomUUID, createHash } from "node:crypto";
import { db, pool, spielerTable, spieleTable, saleEventsTable, productsTable, productPriceRevisionsTable,terminalConfigBackupsTable,terminalRestoreAuthorizationsTable } from "@workspace/db";
import { eq, sql } from "drizzle-orm";
import { applyDayEvent, DayConflict, inventoryState, inventoryInput } from "./inventory";
import { buildDailyReport, dailyReportMessage } from "./daily-report";
import { sendDailyReport, catchupDays } from "./daily-scheduler";
import { localDay, shiftDay, daySchema } from "./local-day";
import { buildScorecard, publicName } from "@workspace/scorecard";
import { pruneBackups } from "./backup-retention";

test("Luxembourg calendar is DST-safe, catchup is exactly three prior days", () => {
  assert.equal(localDay(new Date("2026-03-28T23:30:00Z")),"2026-03-29");
  assert.equal(localDay(new Date("2026-10-24T22:30:00Z")),"2026-10-25");
  assert.deepEqual(catchupDays("2026-03-30"),["2026-03-27","2026-03-28","2026-03-29"]);
  assert.equal(shiftDay("2026-10-25",1),"2026-10-26");
  assert.throws(()=>daySchema.parse("2026-02-30"));
});
test("saved ledger scorecard preserves paired clays, repeats and machine letters", () => {
  const card=buildScorecard({datum:"2026-10-12",completedAt:null,modus:"CUSTOM_1",lauf:1,taubenProLauf:3},
    [{id:7,name:"Test Player"}],[
      {spielerId:7,lauf:1,taube:1,maschine:"A",posten:1,punkte:2,wiederholt:false},
      {spielerId:7,lauf:1,taube:2,maschine:"B",posten:1,punkte:0,wiederholt:false},
      {spielerId:7,lauf:1,taube:3,maschine:"C",posten:7,punkte:1,wiederholt:false},
      {spielerId:7,lauf:1,taube:3,maschine:"C",posten:7,punkte:2,wiederholt:true}]);
  assert.deepEqual(card.columns.map(c=>c.machine),["A","B","C"]);
  assert.deepEqual(card.columns.map(c=>c.pair),["start","end",null]);
  assert.deepEqual(card.players[0].cells,[2,0,1]);assert.equal(card.players[0].total,3);
  assert.equal(publicName("Test Lastname","INITIAL"),"Test L.");
  assert.equal(publicName("Test Lastname","FULL"),"Test Lastname");
  assert.ok(!JSON.stringify(card).includes("spielerId"));
});
test("inventory and daily delivery integration: replay, LWW, zero, server sales, multi-replica claim/retry/empty", async () => {
  const day="2099-11-27",prefix=`gameday-test-${randomUUID()}`;let player=0,game=0,recipient=0;
  const unused=(await db.execute(sql`SELECT 1 FROM ammo_inventory_day WHERE datum=${day}`)).rows;
  assert.equal(unused.length,0,"Reserved test day must be empty");
  try {
    const [p]=await db.insert(spielerTable).values({name:prefix,isMaitreDeTir:true}).returning();player=p.id;
    const event=(suffix:string)=>({externalId:`${prefix}-${suffix}`,terminalId:"TEST-A",datum:day});
    await applyDayEvent("gameday",{...event("maitre"),maitreIds:[player]});
    const opening={...event("opening"),c12:10,c20:0};
    assert.equal((await applyDayEvent("opening",opening)).duplicate,false);
    assert.equal((await applyDayEvent("opening",opening)).duplicate,true);
    await assert.rejects(()=>applyDayEvent("opening",{...opening,c12:11}),DayConflict);
    await assert.rejects(()=>applyDayEvent("opening",{...opening,terminalId:"TEST-B"}),DayConflict);
    assert.equal(inventoryInput.safeParse({...opening,terminalId:"ESP32-P4-UNKNOWN"}).success,false);
    // Simulate a pre-fix accepted event; replay ACK must not rewrite its source
    // or apply its inventory value again, even after another terminal's update.
    const legacy={externalId:`${prefix}-legacy`,terminalId:"ESP32-P4-UNKNOWN",datum:day,
      c12:999,c20:0,takenOver:false,note:""};
    const legacyHash=createHash("sha256").update(JSON.stringify({kind:"closing",...legacy})).digest("hex");
    await db.execute(sql`INSERT INTO gameday_event(external_id,terminal_id,payload_hash)
      VALUES(${legacy.externalId},${legacy.terminalId},${legacyHash})`);
    const fixed={...legacy,terminalId:"ESP32-P4-102030405060"};
    assert.deepEqual(await applyDayEvent("closing",fixed),{success:true,duplicate:true});
    await assert.rejects(()=>applyDayEvent("closing",{...fixed,c12:998}),DayConflict);
    const [retained]=(await db.execute(sql`SELECT terminal_id,payload_hash FROM gameday_event
      WHERE external_id=${legacy.externalId}`)).rows;
    assert.equal(retained.terminal_id,legacy.terminalId);assert.equal(retained.payload_hash,legacyHash);
    assert.equal((await inventoryState(day)).today.closing,null);
    const [product]=await db.select().from(productsTable).where(eq(productsTable.category,"AMMO_CAL12")).limit(1);
    const [price]=await db.select().from(productPriceRevisionsTable).where(eq(productPriceRevisionsTable.productId,product.id)).limit(1);
    await db.insert(saleEventsTable).values({externalId:prefix,spielerId:player,datum:day,productId:product.id,priceRevisionId:price.id,
      productName:product.name,productCategory:"AMMO_CAL12",unitPriceCents:price.unitPriceCents,quantity:3});
    await applyDayEvent("closing",{...event("closing-a"),c12:9,c20:0,difference:-999});
    await applyDayEvent("closing",{...event("closing-b"),terminalId:"TEST-B",c12:6,c20:0});
    const state=await inventoryState(day);assert.equal(state.today.closing?.c12,6);assert.equal(state.today.closing?.terminal,"TEST-B");
    // Replaying an older accepted event must never overwrite the newer terminal.
    await applyDayEvent("closing",{...event("closing-a"),c12:9,c20:0,difference:-999});
    assert.equal((await inventoryState(day)).today.closing?.c12,6);
    const report=await buildDailyReport(day);assert.equal(report.ammunition[0].sold,3);
    assert.equal(report.ammunition[0].difference,1);assert.equal(report.ammunition[1].opening,0);
    assert.equal(report.maitres[0].name,prefix);assert.ok(report.exists);
    const message=dailyReportMessage(report,"https://portal.example.invalid");
    assert.ok(message.text.includes("Kaliber 12"));assert.ok(message.html.includes("Maître"));
    const [r]=(await db.execute(sql`INSERT INTO daily_report_recipient(email,active) VALUES(${`${prefix}@example.invalid`},true) RETURNING id`)).rows as {id:number}[];recipient=r.id;
    let deliveries=0;
    const delivery={portalUrl:async()=>"https://portal.example.invalid",send:async()=>{deliveries++;}};
    const claims=await Promise.all([sendDailyReport(day,false,delivery),sendDailyReport(day,false,delivery)]);
    assert.equal(deliveries,1);assert.equal(claims.filter(r=>r.sent).length,1);
    const failure={...delivery,send:async()=>{throw new Error("Simulated SMTP failure");}};
    assert.equal((await sendDailyReport(day,true,failure)).sent,false);
    assert.equal((await sendDailyReport(day,false,delivery)).sent,false);
    await db.execute(sql`UPDATE daily_report_log SET last_attempt_at=now()-INTERVAL '16 minutes',first_attempt_at=now()-INTERVAL '20 minutes' WHERE datum=${day}`);
    assert.equal((await sendDailyReport(day,false,delivery)).sent,true);assert.equal(deliveries,2);
    const hash=report.dataHash;
    await applyDayEvent("closing",{...event("late-close"),terminalId:"TEST-B",c12:5,c20:0});
    assert.notEqual((await buildDailyReport(day)).dataHash,hash);
    assert.equal((await sendDailyReport(day,false,delivery)).sent,false);assert.equal(deliveries,2);
    await db.execute(sql`UPDATE daily_report_log SET status='failed',last_attempt_at=now()-INTERVAL '16 minutes',first_attempt_at=now()-INTERVAL '7 hours' WHERE datum=${day}`);
    assert.equal((await sendDailyReport(day,false,delivery)).sent,false);assert.equal(deliveries,2);
    const empty=shiftDay(day,1);assert.equal((await sendDailyReport(empty,false,delivery)).skipped,"empty");
    assert.equal((await db.execute(sql`SELECT 1 FROM daily_report_log WHERE datum=${empty}`)).rows.length,0);
    const terminal=`${prefix}-backup`;
    const snapshots=[];
    for(let n=1;n<=4;n++){
      const [backup]=await db.insert(terminalConfigBackupsTable).values({terminalId:terminal,backupDate:`2000-01-0${n}`,
        schemaVersion:1,ciphertext:"test-fixture",iv:"test-fixture",authTag:"test-fixture",checksum:"test-fixture"}).returning();snapshots.push(backup);
    }
    await assert.rejects(()=>db.insert(terminalConfigBackupsTable).values({terminalId:terminal,backupDate:"2000-01-04",
      schemaVersion:1,ciphertext:"x",iv:"x",authTag:"x",checksum:"x"}),"The strict daily unique index rejects a second snapshot");
    const [key]=(await db.execute(sql`SELECT id FROM api_keys LIMIT 1`)).rows as {id:number}[];
    await db.insert(terminalRestoreAuthorizationsTable).values([
      {backupId:snapshots[1].id,targetTerminalId:terminal,targetApiKeyId:key.id,codeHash:prefix+"-expired",
        expiresAt:new Date("2001-01-01")}, // unconsumed, even though expired
      {backupId:snapshots[2].id,targetTerminalId:terminal,targetApiKeyId:key.id,codeHash:prefix+"-consumed",
        expiresAt:new Date("2099-01-01"),usedAt:new Date()}, // consumed but unexpired
    ]);
    await pruneBackups(localDay());
    const kept=await db.select().from(terminalConfigBackupsTable).where(eq(terminalConfigBackupsTable.terminalId,terminal));
    assert.deepEqual(kept.map(b=>b.backupDate).sort(),["2000-01-02","2000-01-03","2000-01-04"]);
  } finally {
    await db.execute(sql`DELETE FROM daily_report_log WHERE datum=${day}`);
    if(recipient)await db.execute(sql`DELETE FROM daily_report_recipient WHERE id=${recipient}`);
    await db.execute(sql`DELETE FROM gameday_event WHERE external_id LIKE ${prefix+"%"}`);
    await db.execute(sql`DELETE FROM gameday_maitre WHERE datum=${day}`);
    await db.execute(sql`DELETE FROM ammo_inventory_day WHERE datum=${day}`);
    await db.execute(sql`DELETE FROM sale_events WHERE external_id=${prefix}`);
    await db.delete(terminalConfigBackupsTable).where(eq(terminalConfigBackupsTable.terminalId,`${prefix}-backup`));
    if(game)await db.delete(spieleTable).where(eq(spieleTable.id,game));
    if(player)await db.delete(spielerTable).where(eq(spielerTable.id,player));
    await pool.end();
  }
});

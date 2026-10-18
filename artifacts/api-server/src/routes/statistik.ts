import { Router } from "express";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { z } from "zod";
import { authenticate } from "./auth";
import { computePlayerStats } from "../lib/player-stats";
const router=Router();
router.use(authenticate);
router.get("/:spielerId",async(req,res)=>res.json(await computePlayerStats(z.coerce.number().int().positive().parse(req.params.spielerId))));
router.get("/:spielerId/modus-breakdown",async(req,res)=>res.json({breakdown:(await computePlayerStats(z.coerce.number().int().positive().parse(req.params.spielerId))).breakdown}));
router.get("/:spielerId/verlauf",async(req,res)=>{
 const id=z.coerce.number().int().positive().parse(req.params.spielerId),limit=z.coerce.number().int().min(1).max(500).parse(req.query.limit??20);
 const rows=(await db.execute(sql`SELECT g.datum,g.modus,sum(t.punkte)::int punkte,g.tauben_pro_lauf*2*g.lauf "maxPunkte"
  FROM v_teilnahmen_visible t JOIN v_spiele_visible g ON g.id=t.spiel_id WHERE t.spieler_id=${id}
  GROUP BY g.id,g.datum,g.modus,g.tauben_pro_lauf,g.lauf ORDER BY g.datum DESC,g.id DESC LIMIT ${limit}`)).rows as any[];
 res.json({verlauf:rows.reverse().map(r=>({...r,datum:new Date(r.datum).toISOString().slice(0,10)}))});
});
export default router;

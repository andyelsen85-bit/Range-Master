import { Router } from "express";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
import { z } from "zod";
import { authenticate } from "./auth";
import { computeRangliste } from "./rangliste-compute";
const router=Router();
router.use((req,res,next)=>req.get("authorization")?authenticate(req,res,next):next());
router.get("/",async(req,res)=>{
 const year=z.coerce.number().int().min(1900).max(2200).parse(req.query.jahr??new Date().getFullYear());
 const limit=z.coerce.number().int().min(1).max(100).parse(req.query.limit??15),sort=z.enum(["best","prozent","punkte","spiele"]).parse(req.query.sort??"best");
 const mode=String(req.query.modus??"ALL");
 const games=(await db.execute(sql`SELECT id,modus,tauben_pro_lauf "taubenProLauf",lauf,datum FROM v_spiele_visible WHERE
  datum>=${`${year}-01-01`}::date AND datum<${`${year+1}-01-01`}::date ${mode!=="ALL"?sql`AND modus::text=${mode}`:sql``}`)).rows as any[];
 const parts=(await db.execute(sql`SELECT spieler_id "spielerId",spiel_id "spielId",punkte FROM v_teilnahmen_visible`)).rows as any[];
 const players=(await db.execute(sql`SELECT id,name FROM spieler`)).rows as any[];
 const all=computeRangliste(games,parts,players,{allModes:mode==="ALL",sort});
 const [first]=(await db.execute(sql`SELECT extract(year FROM min(datum))::int y FROM v_spiele_visible`)).rows as any[];
 const current=new Date().getFullYear(),years=Array.from({length:current-Math.min(current,first?.y??current)+1},(_,i)=>current-i);
 const own=all.find(p=>p.spielerId===(req as any).user?.id);
 res.json({rangliste:all.slice(0,limit),ownRow:own&&own.rang>limit?own:null,years});
});
export default router;

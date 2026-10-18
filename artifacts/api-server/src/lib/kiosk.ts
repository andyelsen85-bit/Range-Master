import { createHash,randomBytes,timingSafeEqual } from "node:crypto";
import type { RequestHandler } from "express";
import { db } from "@workspace/db";
import { sql } from "drizzle-orm";
export const tokenHash=(token:string)=>createHash("sha256").update(token).digest("hex");
export const kioskIp=(ip:string)=>tokenHash(`kiosk-ip:${ip}`);
export const newKioskToken=()=>randomBytes(32).toString("base64url");
export const requireKiosk: RequestHandler=async(req,res,next)=>{
  res.setHeader("Cache-Control","no-store");res.removeHeader("Access-Control-Allow-Origin");
  const origin=req.get("origin");
  if(origin) {
    let host="";try{host=new URL(origin).host;}catch{return void res.sendStatus(403);}
    if(![req.get("host"),req.get("x-forwarded-host")?.split(",")[0].trim()].includes(host))return void res.sendStatus(403);
  }
  const [settings]=(await db.execute(sql`SELECT *,EXISTS(SELECT 1 FROM kiosk_tokens WHERE revoked_at IS NULL) active_token
    FROM public_page_settings WHERE id=1`)).rows as any[];
  if(!settings?.enabled||!settings.active_token)return void res.sendStatus(404);
  const raw=req.get("X-Kiosk-Token");
  if(!raw||raw.length!==43)return void res.status(401).json({error:"Zugriff nur über den Stand-PC"});
  const hash=tokenHash(raw);
  const [row]=(await db.execute(sql`SELECT id,token_hash FROM kiosk_tokens WHERE token_hash=${hash} AND revoked_at IS NULL`)).rows as any[];
  if(!row||!timingSafeEqual(Buffer.from(hash,"hex"),Buffer.from(row.token_hash,"hex")))return void res.sendStatus(401);
  res.locals.kiosk={id:row.id,ipHash:kioskIp(req.ip??req.socket.remoteAddress??"unknown"),settings};
  await db.execute(sql`UPDATE kiosk_tokens SET last_used_at=now(),last_used_ip_hash=${res.locals.kiosk.ipHash} WHERE id=${row.id}
    AND (last_used_at IS NULL OR last_used_at<now()-interval '30 seconds')`);
  next();
};

import type { RequestHandler } from "express";
const windows = new Map<string, { at: number; count: number }>();
export function rateLimit(scope: string, count: number): RequestHandler {
  return (req,res,next) => {
    const now=Date.now(), ip=req.ip??req.socket.remoteAddress??"unknown";
    for(const [k,v] of windows)if(now-v.at>=60000)windows.delete(k);
    const key=`${scope}:${res.locals.kiosk?.id??"anonymous"}:${ip}`;
    if(!windows.has(key)&&windows.size>=8192)return void res.sendStatus(429);
    const value=windows.get(key)??{at:now,count:0};value.count++;windows.set(key,value);
    if(value.count>count){res.setHeader("Retry-After","60");return void res.status(429).json({error:"Bitte kurz warten."});}
    next();
  };
}

import { Router,type RequestHandler } from "express";
import { z } from "zod";
import { authenticate } from "./auth";
import { gameScorecard } from "../lib/game-scorecard";
import { playerPrintReport,playerReportDays } from "../lib/player-report";
const router=Router(),id=z.coerce.number().int().positive();
const owner:RequestHandler=(req,res,next)=>{
  const player=id.parse(req.params.id),user=(req as any).user;
  if(player!==user.id&&!user.isAdmin)return void res.status(403).json({error:"Nur eigene Ergebnisse sind zugänglich."});
  next();
};
router.get("/:id/spiele/:spielId/scorecard",authenticate,owner,async(req,res)=>{
  const card=await gameScorecard(id.parse(req.params.spielId),{onlySpielerId:id.parse(req.params.id)});
  if(!card)return void res.sendStatus(404);
  res.json({...card,maxPoints:card.runs*card.claysPerRun*2});
});
router.get("/:id/print-days",authenticate,owner,async(req,res)=>{
  res.json(await playerReportDays(id.parse(req.params.id)));
});
router.get("/:id/print",authenticate,owner,async(req,res)=>{
  const data=await playerPrintReport(id.parse(req.params.id),req.query);
  if(!data)return void res.sendStatus(404);
  res.json(data);
});
export default router;

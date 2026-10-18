import { Router, type IRouter } from "express";
import { HealthCheckResponse } from "@workspace/api-zod";
import { portalHealth } from "../lib/build-health";

const router: IRouter = Router();

router.get("/health",(_req,res)=>{
  res.setHeader("Cache-Control","no-store");
  res.json(portalHealth());
});

router.get("/healthz", (_req, res) => {
  const data = HealthCheckResponse.parse({ status: "ok" });
  res.json(data);
});

export default router;

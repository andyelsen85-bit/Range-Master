import express, { type Express } from "express";
import cors from "cors";
import pinoHttp from "pino-http";
import router from "./routes";
import { logger } from "./lib/logger";

const app: Express = express();

app.use(
  pinoHttp({
    logger,
    serializers: {
      req(req) {
        return {
          id: req.id,
          method: req.method,
          url: req.url?.split("?")[0],
        };
      },
      res(res) {
        return {
          statusCode: res.statusCode,
        };
      },
    },
  }),
);
app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Root-level liveness / readiness probe.
// K8s probes hit the pod directly (bypasses the ingress), so this must
// live at "/" on Express, not under a sub-path.
app.get("/healthz", (_req, res) => res.json({ status: "ok" }));

// Production ingress may strip /api while the Replit development proxy keeps
// it. Support both forms so the same bundle works in preview and production.
app.use("/api", router);
app.use(router);

// ── Global error handler ───────────────────────────────────────────────────────
// Catches Zod parse errors (thrown by .parse() in route handlers) and any other
// unhandled errors so clients get a clean 4xx/5xx instead of a process crash.
app.use((err: any, _req: any, res: any, _next: any) => {
  if (err?.name === "ZodError") {
    return res.status(400).json({ error: "Validation error", details: err.errors });
  }
  logger.error(err, "unhandled error");
  return res.status(500).json({ error: "Internal server error" });
});

export default app;

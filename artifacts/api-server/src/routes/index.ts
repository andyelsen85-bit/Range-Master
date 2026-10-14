import { Router, type IRouter } from "express";
import healthRouter from "./health";
import authRouter from "./auth";
import spielerRouter from "./spieler";
import ranglisteRouter from "./rangliste";
import statistikRouter from "./statistik";
import syncRouter from "./sync";
import adminRouter from "./admin";
import gamedayRouter from "./gameday";
import terminalLogsRouter from "./terminal-logs";

const router: IRouter = Router();

router.use(healthRouter);
router.use(gamedayRouter);
router.use(terminalLogsRouter);
router.use("/auth", authRouter);
router.use("/spieler", spielerRouter);
router.use("/rangliste", ranglisteRouter);
router.use("/statistik", statistikRouter);
router.use("/sync", syncRouter);
router.use("/admin", adminRouter);

export default router;

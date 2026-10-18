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
import { requireKiosk } from "../lib/kiosk";
import kioskQueueRouter from "./kiosk-queue";
import kioskAccessRouter from "./kiosk-access";
import managedGamesRouter from "./managed-games";
import { configureSyncRoutes } from "../lib/build-health";

const router: IRouter = Router();

router.use(healthRouter);
router.use(kioskAccessRouter);
router.use("/public",requireKiosk);
router.use(kioskQueueRouter);
router.use(managedGamesRouter);
router.use(gamedayRouter);
router.use(terminalLogsRouter);
router.use("/auth", authRouter);
router.use("/spieler", spielerRouter);
router.use("/rangliste", ranglisteRouter);
router.use("/statistik", statistikRouter);
router.use("/sync", syncRouter);
router.use("/admin", adminRouter);

configureSyncRoutes([
  {router:syncRouter,prefix:"/api/sync"},
  {router:kioskQueueRouter,prefix:"/api"},
  {router:gamedayRouter,prefix:"/api"},
  {router:terminalLogsRouter,prefix:"/api"},
]);

export default router;

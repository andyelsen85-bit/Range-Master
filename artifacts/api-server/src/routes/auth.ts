import { Router } from "express";
import bcrypt from "bcryptjs";
import jwt from "jsonwebtoken";
import { db, spielerTable } from "@workspace/db";
import { eq } from "drizzle-orm";
import { z } from "zod";

const router = Router();

const JWT_SECRET = process.env.JWT_SECRET ?? "trapmaster-dev-secret-change-in-prod";

// POST /api/auth/login
router.post("/login", async (req, res) => {
  const bodySchema = z.object({
    email: z.string().email(),
    passwort: z.string().min(6),
  });
  const parsed = bodySchema.safeParse(req.body);
  if (!parsed.success) {
    return res.status(400).json({ error: "Ungültige Eingabe" });
  }
  const { email, passwort } = parsed.data;

  const spieler = await db
    .select()
    .from(spielerTable)
    .where(eq(spielerTable.email, email))
    .limit(1);

  const s = spieler[0];
  if (!s || !s.passwortHash || !s.portalAktiv) {
    return res.status(401).json({ error: "Ungültige Anmeldedaten" });
  }
  const valid = await bcrypt.compare(passwort, s.passwortHash);
  if (!valid) {
    return res.status(401).json({ error: "Ungültige Anmeldedaten" });
  }

  const token = jwt.sign({ id: s.id, name: s.name, email: s.email }, JWT_SECRET, { expiresIn: "7d" });
  return res.json({
    token,
    spieler: { id: s.id, name: s.name, email: s.email, mitgliedNr: s.mitgliedNr, portalAktiv: s.portalAktiv, createdAt: s.createdAt },
  });
});

// GET /api/auth/me
router.get("/me", authenticate, async (req, res) => {
  const user = (req as any).user as { id: number };
  const rows = await db
    .select({ id: spielerTable.id, name: spielerTable.name, email: spielerTable.email, mitgliedNr: spielerTable.mitgliedNr, portalAktiv: spielerTable.portalAktiv, createdAt: spielerTable.createdAt })
    .from(spielerTable)
    .where(eq(spielerTable.id, user.id))
    .limit(1);
  if (!rows[0]) return res.status(404).json({ error: "Nicht gefunden" });
  return res.json(rows[0]);
});

// Middleware
export function authenticate(req: any, res: any, next: any) {
  const authHeader = req.headers.authorization;
  if (!authHeader?.startsWith("Bearer ")) {
    return res.status(401).json({ error: "Unauthorized" });
  }
  try {
    const token = authHeader.slice(7);
    req.user = jwt.verify(token, JWT_SECRET);
    next();
  } catch {
    return res.status(401).json({ error: "Unauthorized" });
  }
}

export function requireApiKey(req: any, res: any, next: any) {
  const key = process.env.SYNC_API_KEY;
  if (!key || req.headers["x-api-key"] !== key) {
    return res.status(401).json({ error: "Unauthorized" });
  }
  next();
}

export default router;

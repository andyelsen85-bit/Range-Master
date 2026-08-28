import { Router } from "express";
import bcrypt from "bcryptjs";
import jwt from "jsonwebtoken";
import { db, spielerTable, apiKeysTable } from "@workspace/db";
import { eq, and } from "drizzle-orm";
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

  const token = jwt.sign(
    { id: s.id, name: s.name, email: s.email, isAdmin: s.isAdmin },
    JWT_SECRET,
    { expiresIn: "30m" },
  );
  return res.json({
    token,
    spieler: {
      id: s.id,
      name: s.name,
      email: s.email,
      mitgliedNr: s.mitgliedNr,
      portalAktiv: s.portalAktiv,
      isAdmin: s.isAdmin,
      createdAt: s.createdAt,
    },
  });
});

// GET /api/auth/me
router.get("/me", authenticate, async (req, res) => {
  const user = (req as any).user as { id: number };
  const rows = await db
    .select({
      id: spielerTable.id,
      name: spielerTable.name,
      email: spielerTable.email,
      mitgliedNr: spielerTable.mitgliedNr,
      portalAktiv: spielerTable.portalAktiv,
      isAdmin: spielerTable.isAdmin,
      createdAt: spielerTable.createdAt,
    })
    .from(spielerTable)
    .where(eq(spielerTable.id, user.id))
    .limit(1);
  if (!rows[0]) return res.status(404).json({ error: "Nicht gefunden" });
  return res.json(rows[0]);
});

// PUT /api/auth/passwort — change own password
router.put("/passwort", authenticate, async (req, res) => {
  const schema = z.object({
    altPasswort: z.string().min(6),
    neuesPasswort: z.string().min(6),
  });
  const parsed = schema.safeParse(req.body);
  if (!parsed.success) return res.status(400).json({ error: "Ungültige Eingabe" });

  const { altPasswort, neuesPasswort } = parsed.data;
  const userId = (req as any).user.id;

  const rows = await db.select().from(spielerTable).where(eq(spielerTable.id, userId)).limit(1);
  const s = rows[0];
  if (!s || !s.passwortHash) return res.status(401).json({ error: "Unauthorized" });

  const valid = await bcrypt.compare(altPasswort, s.passwortHash);
  if (!valid) return res.status(401).json({ error: "Ale Passwuert ass falsch" });

  const passwortHash = await bcrypt.hash(neuesPasswort, 10);
  await db.update(spielerTable).set({ passwortHash }).where(eq(spielerTable.id, userId));

  return res.json({ success: true });
});

// ─── Middleware ────────────────────────────────────────────────────────────────

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

export function requireAdmin(req: any, res: any, next: any) {
  if (!req.user?.isAdmin) {
    return res.status(403).json({ error: "Forbidden" });
  }
  next();
}

export async function requireApiKey(req: any, res: any, next: any) {
  const apiKey = req.headers["x-api-key"];
  if (!apiKey || typeof apiKey !== "string") {
    return res.status(401).json({ error: "Unauthorized" });
  }
  const rows = await db
    .select({ id: apiKeysTable.id, type: apiKeysTable.type })
    .from(apiKeysTable)
    .where(and(eq(apiKeysTable.key, apiKey), eq(apiKeysTable.active, true)))
    .limit(1);
  if (!rows[0]) {
    return res.status(401).json({ error: "Unauthorized" });
  }
  req.apiKeyId = rows[0].id;
  req.apiKeyType = rows[0].type;
  next();
}

export default router;

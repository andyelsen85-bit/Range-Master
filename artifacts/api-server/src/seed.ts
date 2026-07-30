/**
 * First-run seed — compiled to dist/seed.mjs by esbuild.
 * Run as a Kubernetes initContainer after run-migrations.
 *
 * Creates one admin account if none exists yet, then exits.
 * The generated password is printed to stdout — read it once with:
 *   kubectl logs -n rangemaster <api-server-pod> -c run-seed
 */
import bcrypt from "bcryptjs";
import { db, pool, spielerTable } from "@workspace/db";
import { eq, count } from "drizzle-orm";

function randomPassword(length = 16): string {
  const chars =
    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789!@#$%";
  let out = "";
  // Use Math.random — this runs once at install time, not a security-critical RNG
  for (let i = 0; i < length; i++) {
    out += chars[Math.floor(Math.random() * chars.length)];
  }
  return out;
}

async function seed() {
  // Count existing admins
  const [{ value: adminCount }] = await db
    .select({ value: count() })
    .from(spielerTable)
    .where(eq(spielerTable.isAdmin, true));

  if (Number(adminCount) > 0) {
    console.log("[seed] Admin account already exists — skipping.");
    await pool.end();
    return;
  }

  const adminEmail =
    process.env.ADMIN_EMAIL ?? "admin@rangemaster.hostzone.lu";
  const password = process.env.ADMIN_INITIAL_PASSWORD ?? randomPassword();
  const passwortHash = await bcrypt.hash(password, 10);

  await db.insert(spielerTable).values({
    name: "Admin",
    email: adminEmail,
    portalAktiv: true,
    isAdmin: true,
    passwortHash,
    mitgliedNr: "WLZ000",
  });

  console.log("╔══════════════════════════════════════════════════╗");
  console.log("║          RANGEMASTER — INITIAL ADMIN             ║");
  console.log("╠══════════════════════════════════════════════════╣");
  console.log(`║  Email   : ${adminEmail.padEnd(38)}║`);
  console.log(`║  Password: ${password.padEnd(38)}║`);
  console.log("╠══════════════════════════════════════════════════╣");
  console.log("║  Change the password after first login!          ║");
  console.log("╚══════════════════════════════════════════════════╝");

  await pool.end();
}

seed().catch((err) => {
  console.error("[seed] Failed:", err);
  process.exit(1);
});

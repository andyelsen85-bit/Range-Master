/**
 * Database migration entry point — compiled to dist/migrate.mjs by esbuild.
 * Run inside the api-server Docker image as an initContainer:
 *   node /app/dist/migrate.mjs
 *
 * Migration SQL files are copied to /app/drizzle/ in the Docker image.
 * __dirname is injected by the esbuild banner and resolves to /app/dist/.
 */
import { migrate } from "drizzle-orm/node-postgres/migrator";
import { db, pool } from "@workspace/db";
import path from "path";

async function runMigrations() {
  console.log("[migrate] Starting database migrations…");
  // Drizzle SQL files live at /app/drizzle/ (one level up from /app/dist/)
  await migrate(db, { migrationsFolder: path.join(__dirname, "../drizzle") });
  console.log("[migrate] Migrations complete.");
  await pool.end();
}

runMigrations().catch((err) => {
  console.error("[migrate] Migration failed:", err);
  process.exit(1);
});

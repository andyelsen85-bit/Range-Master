import { randomBytes, scrypt, timingSafeEqual } from "node:crypto";
import { sql } from "drizzle-orm";
import type { Tx } from "./queue";

const derive = (pin: string, salt: Buffer) => new Promise<Buffer>((resolve, reject) =>
  scrypt(pin, salt, 32, { N: 32768, r: 8, p: 1, maxmem: 64 * 1024 * 1024 },
    (err, value) => err ? reject(err) : resolve(value)));
export const validKioskPin = (pin: unknown): pin is string => typeof pin === "string" && /^[0-9]{6,32}$/.test(pin);
export async function hashKioskPin(pin: string) {
  const salt = randomBytes(16);
  return `scrypt$32768$8$1$${salt.toString("hex")}$${(await derive(pin, salt)).toString("hex")}`;
}
export async function verifyKioskPin(encoded: unknown, pin: unknown) {
  const match = typeof encoded === "string" ? /^scrypt\$32768\$8\$1\$([a-f0-9]{32})\$([a-f0-9]{64})$/.exec(encoded) : null;
  const actual = await derive(validKioskPin(pin) ? pin : "\0", match ? Buffer.from(match[1], "hex") : Buffer.alloc(16));
  const equal = timingSafeEqual(actual, match ? Buffer.from(match[2], "hex") : Buffer.alloc(32));
  return validKioskPin(pin) && !!match && equal;
}
export const kioskPinLock = async (tx: Tx) => {
  await tx.execute(sql`SELECT pg_advisory_xact_lock(hashtextextended('kiosk-pin-auth',0))`);
};
export const lockDurationsSec = [15 * 60, 60 * 60, 24 * 60 * 60] as const;
export const remainingLock = (until: Date | string | null, now: number) =>
  until ? Math.max(0, Math.ceil((new Date(until).getTime() - now) / 1000)) : 0;

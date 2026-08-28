import { createCipheriv, createDecipheriv, createHash, randomBytes } from "node:crypto";
import { z } from "zod";

export const TERMINAL_CONFIG_SCHEMA_VERSION = 1;

const boundedString = (max: number) => z.string().max(max);

/**
 * Only operational settings belong in a backup. Identity, gateway sequence,
 * roster, credits, games, history, product cache, and all outboxes are
 * intentionally absent from this schema.
 */
export const terminalConfigurationSchema = z.object({
  modus: z.number().int().min(0).max(5),
  maschinenAktiv: z.array(z.boolean()).length(8),
  apiUrl: boundedString(255),
  gatewayUrl: boundedString(255),
  gatewayToken: boundedString(255),
  wifiSsid: boundedString(64),
  wifiPass: boundedString(128),
  autoSyncEnabled: z.boolean(),
  autoSyncSeconds: z.number().int().min(10).max(86400),
  clickSoundEnabled: z.boolean(),
  customSequenzen: z.array(z.array(z.object({
    maschine: z.number().int().min(0).max(7),
    isDoublette: z.boolean(),
    partner: z.number().int().min(0).max(7),
    delayMs: z.number().int().min(0).max(10000),
  })).max(16)).length(4),
  customLaeufe: z.array(z.number().int().min(1).max(2)).length(4),
});

export type TerminalConfiguration = z.infer<typeof terminalConfigurationSchema>;

function encryptionKey(): Buffer {
  const secret = process.env.SESSION_SECRET ?? process.env.JWT_SECRET;
  if (!secret) throw new Error("Terminal configuration encryption secret is not configured");
  return createHash("sha256").update(`trapmaster-terminal-config:${secret}`).digest();
}

export function encryptTerminalConfiguration(configuration: TerminalConfiguration) {
  const iv = randomBytes(12);
  const cipher = createCipheriv("aes-256-gcm", encryptionKey(), iv);
  const ciphertext = Buffer.concat([
    cipher.update(JSON.stringify(configuration), "utf8"),
    cipher.final(),
  ]);
  return {
    ciphertext: ciphertext.toString("base64"),
    iv: iv.toString("base64"),
    authTag: cipher.getAuthTag().toString("base64"),
    checksum: createHash("sha256").update(ciphertext).digest("hex"),
  };
}

export function decryptTerminalConfiguration(input: {
  ciphertext: string;
  iv: string;
  authTag: string;
  checksum: string;
}): TerminalConfiguration {
  const ciphertext = Buffer.from(input.ciphertext, "base64");
  const checksum = createHash("sha256").update(ciphertext).digest("hex");
  if (checksum !== input.checksum) throw new Error("Configuration checksum mismatch");
  const decipher = createDecipheriv(
    "aes-256-gcm",
    encryptionKey(),
    Buffer.from(input.iv, "base64"),
  );
  decipher.setAuthTag(Buffer.from(input.authTag, "base64"));
  const plaintext = Buffer.concat([decipher.update(ciphertext), decipher.final()]).toString("utf8");
  return terminalConfigurationSchema.parse(JSON.parse(plaintext));
}

export function hashRestoreCode(code: string): string {
  return createHash("sha256").update(`trapmaster-restore:${code}`).digest("hex");
}

export function createRestoreCode(): string {
  return randomBytes(6).toString("hex").toUpperCase();
}
import nodemailer from "nodemailer";
import { randomBytes, randomInt } from "crypto";
import { db, smtpSettingsTable, type SmtpSettings } from "@workspace/db";

/** Load the (single-row) SMTP settings, or null when not configured */
export async function getSmtpSettings(): Promise<SmtpSettings | null> {
  const rows = await db.select().from(smtpSettingsTable).limit(1);
  const s = rows[0];
  if (!s || !s.host || !s.fromAddress) return null;
  return s;
}

export function buildTransport(s: SmtpSettings) {
  return nodemailer.createTransport({
    host: s.host,
    port: s.port,
    secure: s.verschluesselung === "SSL",
    ignoreTLS: s.verschluesselung === "NONE",
    requireTLS: s.verschluesselung === "STARTTLS",
    auth: s.username ? { user: s.username, pass: s.passwort } : undefined,
    tls: s.ignoreTlsErrors ? { rejectUnauthorized: false } : undefined,
    connectionTimeout: 10_000,
  });
}

/**
 * Send a mail using stored settings.
 * Throws with a descriptive message when SMTP is not configured or sending fails.
 */
export async function sendMail(to: string, subject: string, text: string): Promise<void> {
  const s = await getSmtpSettings();
  if (!s) throw new Error("SMTP net konfiguréiert");
  const transport = buildTransport(s);
  await transport.sendMail({ from: s.fromAddress, to, subject, text });
}

/** Generate a readable random password (12 chars, no ambiguous characters) */
export function generatePassword(): string {
  const chars = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKMNPQRSTUVWXYZ23456789";
  let pw = "";
  for (let i = 0; i < 12; i++) pw += chars[randomInt(chars.length)];
  return pw;
}

export function invitationEmail(name: string, email: string, passwort: string, portalUrl: string) {
  return {
    subject: "Wëllkomm am Range-Master Portal",
    text:
      `Moien ${name},\n\n` +
      `Däin Zougang zum Range-Master Portal ass aktivéiert.\n\n` +
      `Portal: ${portalUrl || "(URL vum Admin ufroen)"}\n` +
      `Email: ${email}\n` +
      `Passwuert: ${passwort}\n\n` +
      `Änner w.e.g. däi Passwuert no der éischter Umeldung (Profil → Passwuert).\n\n` +
      `Vill Erfolleg!\nRange-Master`,
  };
}

export function resetEmail(name: string, passwort: string, portalUrl: string) {
  return {
    subject: "Range-Master Portal — Neit Passwuert",
    text:
      `Moien ${name},\n\n` +
      `Däi Passwuert gouf zréckgesat.\n\n` +
      `Neit Passwuert: ${passwort}\n` +
      (portalUrl ? `Portal: ${portalUrl}\n` : "") +
      `\nÄnner w.e.g. däi Passwuert no der Umeldung (Profil → Passwuert).\n\nRange-Master`,
  };
}

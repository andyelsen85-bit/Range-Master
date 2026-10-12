import { z } from "zod";
export const CLUB_TIMEZONE = "Europe/Luxembourg";
export function localDay(date = new Date()): string {
  return new Intl.DateTimeFormat("en-CA", { timeZone: CLUB_TIMEZONE, year: "numeric", month: "2-digit", day: "2-digit" }).format(date);
}
export function shiftDay(day: string, offset: number): string {
  const d = new Date(`${day}T12:00:00Z`); d.setUTCDate(d.getUTCDate() + offset);
  return d.toISOString().slice(0, 10);
}
export const daySchema = z.string().regex(/^\d{4}-\d{2}-\d{2}$/).refine(s => {
  const d = new Date(`${s}T12:00:00Z`); return Number.isFinite(d.getTime()) && d.toISOString().slice(0, 10) === s;
}, "Ungültiges Kalenderdatum");

/**
 * Playwright script to capture portal screenshots for the handbook.
 * Uses the playwright chromium headless shell with NixOS library paths.
 */
import { chromium } from 'playwright-chromium';
import path from 'path';
import { fileURLToPath } from 'url';
import fs from 'fs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const outDir = path.resolve(__dirname, '../artifacts/handbook/public/screenshots');
fs.mkdirSync(outDir, { recursive: true });

const PORTAL_URL = 'http://localhost:25265';
const TOKEN = process.env.PORTAL_TOKEN;
const USER_STR = process.env.PORTAL_USER; // keep as raw JSON string for page.evaluate

async function delay(ms) {
  return new Promise(r => setTimeout(r, ms));
}

async function snap(page, filename) {
  const fullPath = path.join(outDir, filename);
  await page.screenshot({ path: fullPath, type: 'jpeg', quality: 90 });
  console.log(`✓ ${filename}`);
}

async function navigateSPA(page, navPath) {
  // Use React/Wouter's history API to navigate without a full page reload
  await page.evaluate((p) => {
    window.history.pushState({}, '', p);
    window.dispatchEvent(new PopStateEvent('popstate', { state: {} }));
  }, navPath);
  await delay(2000);
}

async function main() {
  const browser = await chromium.launch({
    headless: true,
    args: [
      '--no-sandbox',
      '--disable-setuid-sandbox',
      '--disable-dev-shm-usage',
      '--disable-gpu',
    ],
  });

  const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const page = await ctx.newPage();

  // Capture console messages for debugging
  page.on('console', msg => {
    if (msg.type() === 'error') console.log('PAGE ERR:', msg.text());
  });

  // Navigate to portal root and seed auth
  await page.goto(PORTAL_URL, { waitUntil: 'networkidle' });
  
  // Set auth in localStorage — wrap args in one object (Playwright only allows one arg)
  await page.evaluate(({ token, userStr }) => {
    localStorage.setItem('rangemaster-token', token);
    localStorage.setItem('rangemaster-user', userStr);
    const parsed = JSON.parse(userStr);
    console.log('Auth set: isAdmin=' + parsed.isAdmin + ', name=' + parsed.name);
  }, { token: TOKEN, userStr: USER_STR });

  // Now navigate to the landing page with auth set — use a full reload so React initialises with the token in localStorage
  await page.goto(`${PORTAL_URL}`, { waitUntil: 'networkidle' });
  await delay(1000);

  // Check auth state
  const authState = await page.evaluate(() => {
    const user = JSON.parse(localStorage.getItem('rangemaster-user') || 'null');
    const token = localStorage.getItem('rangemaster-token');
    return { isAdmin: user?.isAdmin, hasToken: !!token, name: user?.name };
  });
  console.log('Auth state after reload:', JSON.stringify(authState));

  // ── Rankings ───────────────────────────────────────────────────────────────
  await navigateSPA(page, '/rangliste');
  await delay(1500);
  await snap(page, 'portal-rangliste.jpg');

  // ── Player statistics ──────────────────────────────────────────────────────
  await navigateSPA(page, '/statistiken');
  await delay(1500);
  await snap(page, 'portal-statistiken.jpg');

  // ── Admin — player list ────────────────────────────────────────────────────
  await navigateSPA(page, '/admin');
  await delay(1500);
  const adminUrl = await page.evaluate(() => window.location.pathname);
  console.log('After nav to /admin, current path:', adminUrl);
  await snap(page, 'portal-admin-spieler.jpg');

  // ── Admin — API-Keys / settings ────────────────────────────────────────────
  await navigateSPA(page, '/admin/api-schluesselen');
  await delay(1500);
  await snap(page, 'portal-admin-settings.jpg');

  await browser.close();
  console.log('Done. Screenshots saved to', outDir);
}

main().catch(err => { console.error(err); process.exit(1); });

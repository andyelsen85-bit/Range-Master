import assert from "node:assert/strict";
import test from "node:test";
import {
  createRestoreCode,
  decryptTerminalConfiguration,
  encryptTerminalConfiguration,
  hashRestoreCode,
  terminalConfigurationSchema,
} from "./terminal-config";

process.env.SESSION_SECRET ??= "terminal-config-test-secret";

const configuration = terminalConfigurationSchema.parse({
  modus: 2,
  maschinenAktiv: [true, true, false, true, true, false, true, true],
  apiUrl: "https://portal.example.test",
  gatewayUrl: "http://192.168.1.50",
  gatewayToken: "gateway-secret",
  wifiSsid: "Range WiFi",
  wifiPass: "wifi-secret",
  autoSyncEnabled: true,
  autoSyncSeconds: 300,
  clickSoundEnabled: false,
  customSequenzen: [
    [{ maschine: 0, isDoublette: true, partner: 1, delayMs: 500 }],
    [], [], [],
  ],
  customLaeufe: [2, 2, 1, 1],
});

test("terminal configuration encrypts and decrypts without exposing plaintext", () => {
  const encrypted = encryptTerminalConfiguration(configuration);
  assert.deepEqual(decryptTerminalConfiguration(encrypted), configuration);
  assert.equal(encrypted.ciphertext.includes(configuration.wifiPass), false);
  assert.equal(encrypted.ciphertext.includes(configuration.gatewayToken), false);
});

test("tampered configuration is rejected before restore", () => {
  const encrypted = encryptTerminalConfiguration(configuration);
  assert.throws(() => decryptTerminalConfiguration({
    ...encrypted,
    ciphertext: `${encrypted.ciphertext.slice(0, -2)}AA`,
  }));
});

test("restore codes are fixed-length and only persisted as hashes", () => {
  const code = createRestoreCode();
  assert.match(code, /^[A-F0-9]{12}$/);
  assert.notEqual(hashRestoreCode(code), code);
  assert.equal(hashRestoreCode(code), hashRestoreCode(code));
});

test("configuration schema rejects fields outside firmware bounds", () => {
  assert.equal(terminalConfigurationSchema.safeParse({ ...configuration, autoSyncSeconds: 9 }).success, false);
  assert.equal(terminalConfigurationSchema.safeParse({
    ...configuration,
    customSequenzen: [[{ maschine: 0, isDoublette: true, partner: 1, delayMs: 10001 }], [], [], []],
  }).success, false);
});
// UNKNOWN was shared by all affected P4 terminals. It is historical metadata,
// never an alias for a particular replacement chip or a new restore target.
export const LEGACY_UNKNOWN_TERMINAL = "ESP32-P4-UNKNOWN";
export function isAttributableTerminal(id: string) {
  return id.trim().toUpperCase() !== LEGACY_UNKNOWN_TERMINAL;
}
export function isConcreteP4Terminal(id: string) {
  return /^ESP32-P4-[0-9A-F]{12}$/.test(id) &&
    !id.endsWith("000000000000") && !id.endsWith("FFFFFFFFFFFF");
}

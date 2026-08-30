import { createHash } from "node:crypto";

/**
 * Deterministic JSON encoding for sync revision inputs. Object keys are sorted,
 * while array order remains significant (as it is in the API response).
 */
export function canonicalJson(value: unknown): string {
  const seen = new Set<object>();

  const encode = (item: unknown): string => {
    if (item === null) return "null";
    if (item instanceof Date) return JSON.stringify(item.toISOString());
    switch (typeof item) {
      case "string":
      case "boolean":
        return JSON.stringify(item);
      case "number":
        if (!Number.isFinite(item)) throw new TypeError("Revision input contains a non-finite number");
        return JSON.stringify(item);
      case "bigint":
        return JSON.stringify(item.toString());
      case "undefined":
        return "null";
      case "object": {
        if (seen.has(item)) throw new TypeError("Revision input must not be circular");
        seen.add(item);
        const encoded = Array.isArray(item)
          ? `[${item.map(encode).join(",")}]`
          : `{${Object.keys(item as Record<string, unknown>).sort().flatMap((key) => {
            const child = (item as Record<string, unknown>)[key];
            // Match JSON.stringify's object behavior for undefined fields.
            return child === undefined ? [] : [`${JSON.stringify(key)}:${encode(child)}`];
          }).join(",")}}`;
        seen.delete(item);
        return encoded;
      }
      default:
        throw new TypeError(`Unsupported revision input type: ${typeof item}`);
    }
  };

  return encode(value);
}

/** Opaque, content-addressed revision token for an existing sync response. */
export function revisionToken(payload: unknown): string {
  return `sha256:${createHash("sha256").update(canonicalJson(payload)).digest("hex")}`;
}

export const SYNC_MANIFEST_SCHEMA_VERSION = 1;
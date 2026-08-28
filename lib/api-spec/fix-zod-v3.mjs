import { readFile, writeFile } from "node:fs/promises";

const target = new URL("../api-zod/src/generated/api.ts", import.meta.url);
const generated = await readFile(target, "utf8");
const compatible = generated
  .replaceAll("zod.int()", "zod.number().int()")
  .replaceAll("zod.email()", "zod.string().email()")
  .replaceAll("zod.uuid()", "zod.string().uuid()");

await writeFile(target, compatible);

// Orval regenerates this barrel with both schema values and TS-only types.
// Several operation/body names legitimately exist in both outputs, so a
// wildcard re-export makes TypeScript reject the package as ambiguous.
const barrel = new URL("../api-zod/src/index.ts", import.meta.url);
const barrelSource = await readFile(barrel, "utf8");
await writeFile(barrel, barrelSource.replace(/\nexport \* from ['"]\.\/generated\/types['"];?\n?/, "\n"));
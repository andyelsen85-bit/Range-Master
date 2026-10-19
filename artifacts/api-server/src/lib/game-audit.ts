/** Audit snapshots must remain attributable after the game is purged. */
export function gameAuditSnapshot(state: unknown, game: {
  id: number; external_id?: string | null; datum: unknown;
}) {
  return {
    ...(state && typeof state === "object" && !Array.isArray(state) ? state : { state }),
    game: { id: game.id, externalId: game.external_id ?? null, date: game.datum },
  };
}

/** Stable comparison for child rows in idempotent immutable-event replays. */
export function sameImmutableChildren(left: unknown[], right: unknown[]): boolean {
  const canonical = (items: unknown[]) => JSON.stringify([...items]
    .map(item => JSON.stringify(item))
    .sort());
  return canonical(left) === canonical(right);
}
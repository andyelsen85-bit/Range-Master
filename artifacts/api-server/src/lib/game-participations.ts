export interface Participation {
  spielerId: number; startPosten: number; punkte: number; lauf: number;
}
export interface ParticipationShot {
  spielerId: number; lauf: number; taube: number; posten: number;
  maschine: string; punkte: number; wiederholt: boolean;
}

/** Canonical per-player/per-run projection of the immutable shot ledger. */
export function deriveParticipations(sent: Participation[], shots: ParticipationShot[], runs?: number): Participation[] {
  const key = (p: {spielerId: number; lauf: number}) => `${p.spielerId}:${p.lauf}`;
  const rows = new Map<string, Participation>();
  for (const p of sent) {
    const row = {...p, punkte: shots.length ? 0 : p.punkte};
    const previous = rows.get(key(p));
    if (previous && (previous.startPosten !== row.startPosten || previous.punkte !== row.punkte))
      throw new Error("Widersprüchliche Teilnahme ohne eindeutigen Startposten");
    rows.set(key(p), row);
  }
  if (shots.length) {
    const count=runs ?? Math.max(...sent.map(p=>p.lauf),...shots.map(p=>p.lauf));
    for (const id of new Set([...sent,...shots].map(p=>p.spielerId))) {
      for(let lauf=1;lauf<=count;lauf++) {
        const k=key({spielerId:id,lauf});
        if(!rows.has(k))rows.set(k,{spielerId:id,lauf,punkte:0,
          startPosten:sent.filter(p=>p.spielerId===id).sort((a,b)=>a.lauf-b.lauf)[0]?.startPosten ?? 1});
      }
    }
  }
  const groups = new Map<string, ParticipationShot[]>();
  for (const shot of shots) {
    const k = key(shot);
    if (!groups.has(k)) groups.set(k, []);
    groups.get(k)!.push(shot);
  }
  for (const [k, group] of groups) {
    // Same deterministic first-clay rule as the migration and firmware;
    // independent of payload order and of the final Lauf's rotated position.
    const first = [...group].sort((a,b) => a.taube-b.taube ||
      Number(a.wiederholt)-Number(b.wiederholt) || a.posten-b.posten ||
      a.maschine.localeCompare(b.maschine))[0];
    rows.set(k, {spielerId:first.spielerId, startPosten:first.posten || rows.get(k)?.startPosten || 1,
      punkte:group.reduce((n,e) => n + (e.wiederholt ? 0 : e.punkte), 0), lauf:first.lauf});
  }
  return [...rows.values()].sort((a,b) => a.spielerId-b.spielerId || a.lauf-b.lauf)
    .map(p => ({spielerId:p.spielerId,startPosten:p.startPosten,punkte:p.punkte,lauf:p.lauf}));
}

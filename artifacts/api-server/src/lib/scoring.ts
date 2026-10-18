export function computeTaubePunkte(shot:{schuss1:boolean;schuss2:boolean;wiederholt?:boolean},pair=false):number {
  if(shot.wiederholt)return 0;
  return shot.schuss1?2:shot.schuss2?(pair?2:1):0;
}
export function isPairClay<T extends {spielerId:number;lauf:number;taube:number;posten:number;maschine:string}>(shot:T,shots:T[]) {
  return shot.maschine==="H"||shots.some(s=>s.spielerId===shot.spielerId&&s.lauf===shot.lauf
    &&Math.abs(s.taube-shot.taube)===1&&s.posten===shot.posten);
}

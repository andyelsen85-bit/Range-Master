export interface SpielRow {id:number;modus:string;taubenProLauf:number;lauf:number;datum?:Date|string}
export interface TeilnahmeRow {spielerId:number;spielId:number;punkte:number}
export interface SpielerRow {id:number;name:string}
export interface RanglisteEntry {rang:number;spielerId:number;name:string;gesamtPunkte:number;anzahlSpiele:number;durchschnitt:number;
 durchschnittProzent:number;bestPunkte:number;bestMaximum:number;bestProzent:number}
export function computeRangliste(spiele:SpielRow[],teilnahmen:TeilnahmeRow[],spieler:SpielerRow[],
 options:{allModes?:boolean;sort?:"best"|"prozent"|"punkte"|"spiele"}={}):RanglisteEntry[] {
 const games=new Map(spiele.map(g=>[g.id,g])),sums=new Map<string,{id:number;total:number;maximum:number;at:number}>();
 for(const t of teilnahmen) {
  const game=games.get(t.spielId);if(!game)continue;
  const key=`${t.spielerId}:${t.spielId}`;
  if(!sums.has(key))sums.set(key,{id:t.spielerId,total:0,maximum:game.taubenProLauf*2*game.lauf,at:game.datum?new Date(game.datum).getTime():game.id});
  sums.get(key)!.total+=t.punkte;
 }
 const names=new Map(spieler.map(p=>[p.id,p.name])),groups=new Map<number,typeof sums extends Map<any,infer V>?V[]:never>();
 for(const g of sums.values()){if(!groups.has(g.id))groups.set(g.id,[]);groups.get(g.id)!.push(g);}
 const pct=(g:{total:number;maximum:number})=>g.maximum?g.total/g.maximum:0;
 const entries=[...groups].map(([id,list])=>{
  list.sort((a,b)=>(options.allModes!==false?pct(b)-pct(a):b.total-a.total)||pct(b)-pct(a)||a.at-b.at);
  const best=list[0],total=list.reduce((n,g)=>n+g.total,0),maximum=list.reduce((n,g)=>n+g.maximum,0);
  return {spielerId:id,name:names.get(id)??"Unbekannt",gesamtPunkte:total,anzahlSpiele:list.length,durchschnitt:Math.round(total/list.length*10)/10,
   durchschnittProzent:maximum?Math.round(total/maximum*1000)/10:0,bestPunkte:best.total,bestMaximum:best.maximum,bestProzent:Math.round(pct(best)*1000)/10,
   _percentage:pct(best),_at:best.at};
 });
 entries.sort((a,b)=>{
  const sort=options.sort??"best";
  const first=sort==="prozent"?b.durchschnittProzent-a.durchschnittProzent:sort==="punkte"?b.gesamtPunkte-a.gesamtPunkte:
   sort==="spiele"?b.anzahlSpiele-a.anzahlSpiele:options.allModes!==false?b._percentage-a._percentage:b.bestPunkte-a.bestPunkte;
  return first||b._percentage-a._percentage||a._at-b._at||a.name.localeCompare(b.name,"de")||a.spielerId-b.spielerId;
 });
 return entries.map(({_percentage,_at,...entry},i)=>({rang:i+1,...entry}));
}

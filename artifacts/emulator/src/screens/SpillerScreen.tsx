import React, { useState } from 'react';
import { useGameStore, PortalSpieler, SpielerUpdate } from '@/store/gameStore';
import { PlayerSearch } from '@/components/PlayerSearch';
import { TouchButton } from '@/components/TouchButton';
import {
  ArrowLeft, Save, RefreshCw, KeyRound, CheckCircle2, AlertTriangle,
  Clock, MailCheck, X, UserCog, Trash2,
} from 'lucide-react';
import { cn } from '@/lib/utils';

function StatusBadge({ u }: { u: SpielerUpdate }) {
  const map = {
    pending:      { icon: Clock,        text: 'Waart op Sync',   cls: 'bg-yellow-500/15 text-yellow-400 border-yellow-500/40' },
    synced:       { icon: CheckCircle2, text: 'Synchroniséiert', cls: 'bg-blue-500/15 text-blue-400 border-blue-500/40' },
    email_sent:   { icon: MailCheck,    text: 'Email verschéckt', cls: 'bg-green-500/15 text-green-400 border-green-500/40' },
    email_failed: { icon: AlertTriangle, text: 'Email-Feeler',    cls: 'bg-red-500/15 text-red-400 border-red-500/40' },
  } as const;
  const { icon: Icon, text, cls } = map[u.status];
  return (
    <span className={cn('inline-flex items-center gap-1.5 px-2.5 py-1 rounded-md border text-xs font-bold', cls)} title={u.fehler}>
      <Icon className="w-3.5 h-3.5" /> {text}
    </span>
  );
}

export function SpillerScreen() {
  const store = useGameStore();
  const [selected, setSelected] = useState<PortalSpieler | null>(null);
  const [name, setName] = useState('');
  const [email, setEmail] = useState('');
  const [portalAktiv, setPortalAktiv] = useState(false);
  const [saved, setSaved] = useState(false);

  const select = (p: PortalSpieler) => {
    setSelected(p);
    setName(p.name);
    setEmail(p.email ?? '');
    setPortalAktiv(p.portalAktiv ?? false);
    setSaved(false);
  };

  const isLokal = !!selected && selected.id <= 0;
  const emailValid = email === '' || /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
  const dirty = !!selected && (
    name.trim() !== selected.name ||
    (email || null) !== (selected.email ?? null) ||
    portalAktiv !== (selected.portalAktiv ?? false)
  );

  const save = () => {
    if (!selected || !name.trim() || !emailValid) return;
    store.queueSpielerUpdate(selected.id, { name: name.trim(), email: email || null, portalAktiv });
    setSelected({ ...selected, name: name.trim(), email: email || null, portalAktiv });
    setSaved(true);
    setTimeout(() => setSaved(false), 3000);
  };

  const resetPasswort = () => {
    if (!selected) return;
    store.queuePasswortReset(selected.id);
  };

  const inputCls = "w-full bg-background border-2 border-border rounded-lg h-14 px-4 text-base focus:border-primary focus:outline-none";
  const pendingCount = store.spielerUpdates.filter(u => u.status === 'pending').length;

  return (
    <div className="flex h-full w-full bg-background flex-col">
      <header className="h-16 border-b-2 border-border flex items-center px-6 bg-card gap-4 shrink-0">
        <TouchButton onClick={() => store.setScreen('dashboard')} className="w-12 h-12 p-0">
          <ArrowLeft className="w-6 h-6" />
        </TouchButton>
        <UserCog className="w-6 h-6 text-primary" />
        <h1 className="text-xl font-bold tracking-wider text-primary">SPILLERVERWALTUNG</h1>
        <div className="ml-auto flex items-center gap-3">
          {pendingCount > 0 && (
            <span className="text-sm font-bold text-yellow-400 bg-yellow-500/10 border border-yellow-500/40 rounded-lg px-3 py-1.5">
              {pendingCount} Ännerung{pendingCount > 1 ? 'en' : ''} net synchroniséiert
            </span>
          )}
          <TouchButton variant="outline" className="h-12 px-4 gap-2"
            onClick={() => { void store.syncAllPending(); }}
            disabled={store.syncStatus === 'syncing'}>
            <RefreshCw className={cn('w-5 h-5', store.syncStatus === 'syncing' && 'animate-spin')} />
            Sync
          </TouchButton>
        </div>
      </header>

      <div className="flex-1 overflow-hidden flex gap-6 p-6">
        {/* ── Left: search + edit form ── */}
        <div className="w-[520px] shrink-0 flex flex-col gap-5 overflow-y-auto pr-1">
          <div className="bg-card border-2 border-border rounded-xl p-5">
            <div className="text-[10px] font-bold uppercase tracking-widest text-muted-foreground mb-2">
              Spiller sichen
            </div>
            <PlayerSearch onSelect={select} autoFocus placeholder="Numm sichen…" />
          </div>

          {selected ? (
            <div className="bg-card border-2 border-border rounded-xl p-5 flex flex-col gap-4">
              <div className="flex items-center justify-between">
                <h2 className="text-base font-bold uppercase tracking-widest">
                  {selected.name}
                  {selected.mitgliedNr && <span className="ml-2 text-muted-foreground font-mono text-sm">{selected.mitgliedNr}</span>}
                </h2>
                <button onClick={() => setSelected(null)} className="text-muted-foreground hover:text-foreground">
                  <X className="w-5 h-5" />
                </button>
              </div>

              {isLokal && (
                <div className="text-xs font-bold text-yellow-400 bg-yellow-500/10 border border-yellow-500/40 rounded-lg px-3 py-2">
                  Lokale Spiller — Ännerunge gi gequeued a beim nächste Sync applizéiert
                </div>
              )}

              <div>
                <label className="block text-sm font-bold text-muted-foreground mb-2 uppercase tracking-wider">Numm</label>
                <input type="text" value={name} onChange={(e) => setName(e.target.value)} className={inputCls} />
              </div>

              <div>
                <label className="block text-sm font-bold text-muted-foreground mb-2 uppercase tracking-wider">Email</label>
                <input type="email" value={email} onChange={(e) => setEmail(e.target.value)}
                  placeholder="spiller@beispill.lu" className={cn(inputCls, 'font-mono', !emailValid && 'border-destructive')} />
                {!emailValid && <div className="text-xs text-destructive font-bold mt-1">Ongëlteg Email-Adress</div>}
              </div>

              <button type="button" onClick={() => setPortalAktiv(!portalAktiv)}
                className="flex items-center gap-3 text-base font-bold">
                <div className={cn('w-12 h-7 rounded-full transition-colors relative', portalAktiv ? 'bg-primary' : 'bg-secondary')}>
                  <div className={cn('absolute top-1 w-5 h-5 rounded-full bg-white shadow transition-transform', portalAktiv ? 'translate-x-6' : 'translate-x-1')} />
                </div>
                Portal Aktiv
              </button>
              {portalAktiv && !(selected.portalAktiv ?? false) && !email && (
                <div className="text-xs text-yellow-400 font-bold">
                  Ouni Email-Adress kann keng Invitatioun verschéckt ginn.
                </div>
              )}
              {portalAktiv && !(selected.portalAktiv ?? false) && email && emailValid && (
                <div className="text-xs text-muted-foreground">
                  No dem Sync kritt de Spiller eng Email mat Zougangsdaten a Portal-Link.
                </div>
              )}

              <div className="flex gap-3 pt-1">
                <TouchButton size="lg" variant="primary" className="flex-1 gap-2" onClick={save}
                  disabled={!dirty || !name.trim() || !emailValid}>
                  {saved ? <CheckCircle2 className="w-5 h-5" /> : <Save className="w-5 h-5" />}
                  {saved ? 'Gequeued' : 'Späicheren'}
                </TouchButton>
                <TouchButton size="lg" variant="outline" className="flex-1 gap-2" onClick={resetPasswort}
                  disabled={isLokal || !(selected.email || email) || !(selected.portalAktiv || portalAktiv)}>
                  <KeyRound className="w-5 h-5" /> Passwuert Reset
                </TouchButton>
              </div>
              <div className="text-xs text-muted-foreground">
                Passwuert-Reset: no dem Sync schéckt de Portal eng Email mat engem neie Passwuert.
              </div>
            </div>
          ) : (
            <div className="bg-card/50 border-2 border-dashed border-border rounded-xl p-8 text-center text-muted-foreground text-sm">
              Sicht e Spiller fir Numm, Email a Portal-Zougang ze änneren.
            </div>
          )}
        </div>

        {/* ── Right: change queue ── */}
        <div className="flex-1 bg-card border-2 border-border rounded-xl p-5 flex flex-col overflow-hidden">
          <div className="flex items-center justify-between mb-3 shrink-0">
            <h2 className="text-base font-bold uppercase tracking-widest">Ännerungen</h2>
            <div className="flex gap-2">
              <TouchButton variant="ghost" className="h-9 px-3 text-xs gap-1.5"
                onClick={() => { void store.refreshSpielerUpdateStatus(); }}
                disabled={!store.spielerUpdates.some(u => u.status === 'synced' || u.status === 'email_failed')}>
                <RefreshCw className="w-3.5 h-3.5" /> Status aktualiséieren
              </TouchButton>
              <TouchButton variant="ghost" className="h-9 px-3 text-xs gap-1.5 text-destructive hover:bg-destructive/10"
                onClick={store.clearErledegtSpielerUpdates}
                disabled={!store.spielerUpdates.some(u => u.status === 'email_sent' || (u.status === 'synced' && u.typ === 'UPDATE'))}>
                <Trash2 className="w-3.5 h-3.5" /> Erledegt läschen
              </TouchButton>
            </div>
          </div>

          <div className="flex-1 overflow-y-auto flex flex-col gap-2">
            {store.spielerUpdates.length === 0 && (
              <div className="text-muted-foreground/60 text-sm italic text-center py-10">
                Keng Ännerunge gequeued
              </div>
            )}
            {store.spielerUpdates.map(u => (
              <div key={u.externalId} className="bg-background border-2 border-border rounded-lg p-3 flex items-center gap-3">
                <div className="flex-1 min-w-0">
                  <div className="font-bold truncate">
                    {u.spielerName}
                    <span className="ml-2 text-xs font-normal text-muted-foreground uppercase tracking-wider">
                      {u.typ === 'PASSWORT_RESET' ? 'Passwuert Reset' : 'Ännerung'}
                    </span>
                  </div>
                  <div className="text-xs text-muted-foreground font-mono truncate">
                    {u.typ === 'UPDATE'
                      ? [u.name, u.email ?? 'keng Email', u.portalAktiv ? 'Portal: aktiv' : 'Portal: aus'].join(' · ')
                      : new Date(u.queuedAt).toLocaleString('de-LU')}
                    {u.fehler && <span className="text-red-400"> — {u.fehler}</span>}
                  </div>
                </div>
                <StatusBadge u={u} />
              </div>
            ))}
          </div>

          {store.syncStatus === 'error' && (
            <div className="mt-3 shrink-0 p-3 rounded-lg bg-red-500/15 text-red-400 border border-red-500/40 text-sm font-bold flex items-center gap-2">
              <AlertTriangle className="w-4 h-4" /> Sync feelgeschloen — Ännerunge bleiwen an der Queue
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

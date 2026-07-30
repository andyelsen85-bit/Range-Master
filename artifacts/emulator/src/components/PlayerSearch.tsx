import React, { useEffect, useMemo, useRef, useState } from 'react';
import { useGameStore, PortalSpieler } from '@/store/gameStore';
import { Search, UserPlus, Check, Loader2, Download, AlertCircle, X } from 'lucide-react';
import { cn } from '@/lib/utils';

interface PlayerSearchProps {
  /** Called when the operator picks (or creates) a player */
  onSelect: (spieler: PortalSpieler) => void;
  /** IDs that are already taken (shown but not selectable) */
  disabledIds?: number[];
  placeholder?: string;
  /** Auto-focus the input when mounted (default true) */
  autoFocus?: boolean;
  className?: string;
}

/**
 * Reusable player search with dropdown.
 * - Filters the synced player cache as the operator types
 * - Full keyboard support: type-to-filter, ArrowUp/Down, Enter, Escape
 * - If no player matches, offers to create the typed name as a new local player
 *   (queued for portal upload on the next sync)
 */
export function PlayerSearch({
  onSelect,
  disabledIds = [],
  placeholder = 'Numm sichen oder aginn…',
  autoFocus = true,
  className,
}: PlayerSearchProps) {
  const store = useGameStore();
  const [query, setQuery] = useState('');
  const [highlight, setHighlight] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);
  const listRef = useRef<HTMLDivElement>(null);

  // Load players on first mount if the cache is empty
  useEffect(() => {
    if (store.portalSpieler.length === 0 && !store.portalLaden) {
      void store.ladeSpielerVomPortal();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Focus reliably — also when re-rendered into a new picker
  useEffect(() => {
    if (!autoFocus) return undefined;
    const t = setTimeout(() => inputRef.current?.focus(), 50);
    return () => clearTimeout(t);
  }, [autoFocus]);

  const q = query.trim().toLowerCase();

  const matches = useMemo(() => {
    if (!q) return store.portalSpieler;
    // Prefix matches first, then substring matches
    const prefix: PortalSpieler[] = [];
    const substr: PortalSpieler[] = [];
    for (const p of store.portalSpieler) {
      const name = p.name.toLowerCase();
      if (name.startsWith(q)) prefix.push(p);
      else if (name.includes(q)) substr.push(p);
    }
    return [...prefix, ...substr];
  }, [q, store.portalSpieler]);

  const exactMatch = matches.some(p => p.name.toLowerCase() === q);
  const canCreate = q.length > 0 && !exactMatch;

  // Options: matches + optional "create new" row at the end
  const optionCount = matches.length + (canCreate ? 1 : 0);

  // Keep highlight within bounds whenever the query or option list changes
  useEffect(() => {
    setHighlight(h => Math.min(Math.max(h, 0), Math.max(optionCount - 1, 0)));
  }, [q, optionCount]);

  // Keep the highlighted option scrolled into view
  useEffect(() => {
    const el = listRef.current?.querySelector<HTMLElement>(`[data-idx="${highlight}"]`);
    el?.scrollIntoView({ block: 'nearest' });
  }, [highlight]);

  const pick = (idx: number) => {
    if (idx < 0 || idx >= optionCount) return;
    if (idx < matches.length) {
      const p = matches[idx];
      if (disabledIds.includes(p.id)) return;
      onSelect(p);
      setQuery('');
    } else if (canCreate) {
      const neu = store.addLocalSpieler(query);
      onSelect(neu);
      setQuery('');
    }
  };

  const onKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      setHighlight(h => Math.min(h + 1, Math.max(optionCount - 1, 0)));
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      setHighlight(h => Math.max(h - 1, 0));
    } else if (e.key === 'Enter') {
      e.preventDefault();
      if (optionCount > 0) pick(highlight);
    } else if (e.key === 'Escape') {
      setQuery('');
    }
  };

  return (
    <div className={cn('flex flex-col gap-2', className)}>
      {/* Search input */}
      <div className="relative">
        <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground pointer-events-none" />
        <input
          ref={inputRef}
          type="text"
          value={query}
          onChange={e => setQuery(e.target.value)}
          onKeyDown={onKeyDown}
          placeholder={placeholder}
          role="combobox"
          aria-expanded={optionCount > 0}
          aria-autocomplete="list"
          className="w-full bg-background border-2 border-border rounded-lg h-11 pl-9 pr-9 text-sm font-bold focus:border-primary focus:outline-none"
        />
        {query && (
          <button
            onClick={() => { setQuery(''); inputRef.current?.focus(); }}
            className="absolute right-2 top-1/2 -translate-y-1/2 w-6 h-6 flex items-center justify-center text-muted-foreground hover:text-foreground"
            tabIndex={-1}
          >
            <X className="w-4 h-4" />
          </button>
        )}
      </div>

      {/* Status row */}
      <div className="flex items-center justify-between">
        <span className="text-xs text-muted-foreground">
          {store.portalLaden
            ? 'Lueden…'
            : store.spielerAusCache
              ? `📦 Offline-Cache · ${store.portalSpieler.length} Spillesch`
              : store.portalSpieler.length > 0
                ? `${matches.length}/${store.portalSpieler.length} Spillesch`
                : 'Keng Spillesch'}
        </span>
        <button
          onClick={() => void store.ladeSpielerVomPortal()}
          disabled={store.portalLaden}
          className="h-7 px-2 flex items-center gap-1 text-xs text-muted-foreground hover:text-foreground border border-border rounded-lg"
        >
          {store.portalLaden
            ? <Loader2 className="w-3 h-3 animate-spin" />
            : <Download className="w-3 h-3" />}
          Laden
        </button>
      </div>

      {store.portalFehler && (
        <div className="flex items-center gap-2 text-xs text-red-400 bg-red-500/10 border border-red-500/30 rounded-lg px-3 py-2">
          <AlertCircle className="w-3 h-3 shrink-0" />
          {store.portalFehler}
        </div>
      )}

      {/* Results dropdown */}
      {optionCount > 0 ? (
        <div ref={listRef} className="flex flex-col gap-1 max-h-48 overflow-y-auto pr-1" role="listbox">
          {matches.map((p, idx) => {
            const taken = disabledIds.includes(p.id);
            const active = highlight === idx;
            return (
              <button
                key={p.id}
                data-idx={idx}
                role="option"
                aria-selected={active}
                onClick={() => pick(idx)}
                onMouseEnter={() => setHighlight(idx)}
                disabled={taken}
                className={cn(
                  'flex items-center gap-3 px-3 py-2.5 rounded-lg border text-left transition-all min-h-[44px]',
                  taken
                    ? 'border-green-500/30 bg-green-500/5 text-green-500/60 cursor-default'
                    : active
                      ? 'border-primary bg-primary/15 cursor-pointer'
                      : 'border-border hover:border-primary/60 hover:bg-primary/10 cursor-pointer active:scale-[0.98]',
                )}
              >
                {taken
                  ? <Check className="w-4 h-4 text-green-500/60 shrink-0" />
                  : <UserPlus className="w-4 h-4 text-muted-foreground shrink-0" />}
                <span className="font-bold text-sm flex-1 truncate">{p.name}</span>
                {p.lokal && (
                  <span className="text-[10px] font-bold uppercase text-amber-400 bg-amber-500/10 border border-amber-500/30 rounded px-1.5 py-0.5">
                    Nei
                  </span>
                )}
                {p.mitgliedNr && (
                  <span className="text-xs text-muted-foreground font-mono">{p.mitgliedNr}</span>
                )}
              </button>
            );
          })}

          {canCreate && (
            <button
              data-idx={matches.length}
              role="option"
              aria-selected={highlight === matches.length}
              onClick={() => pick(matches.length)}
              onMouseEnter={() => setHighlight(matches.length)}
              className={cn(
                'flex items-center gap-3 px-3 py-2.5 rounded-lg border-2 border-dashed text-left transition-all min-h-[44px]',
                highlight === matches.length
                  ? 'border-primary bg-primary/15 cursor-pointer'
                  : 'border-primary/40 hover:border-primary hover:bg-primary/10 cursor-pointer active:scale-[0.98]',
              )}
            >
              <UserPlus className="w-4 h-4 text-primary shrink-0" />
              <span className="text-sm flex-1">
                <span className="text-muted-foreground">Neie Spiller uleeën: </span>
                <span className="font-bold text-primary">„{query.trim()}"</span>
              </span>
            </button>
          )}
        </div>
      ) : !store.portalLaden && (
        <div className="text-xs text-muted-foreground italic text-center py-3">
          {q ? 'Kee Spiller fonnt' : 'Keng Spillesch am Cache — dréckt "Laden"'}
        </div>
      )}
    </div>
  );
}

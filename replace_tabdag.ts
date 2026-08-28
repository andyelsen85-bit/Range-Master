import fs from "fs";

const file = fs.readFileSync("artifacts/portal/src/pages/admin-kredite.tsx", "utf-8");

const start = file.indexOf("function TabDag({ token }: { token: string | null }) {");
const end = file.indexOf("// ── Tab: Joer");

const newContent = `function TabDag({ token }: { token: string | null }) {
  const [datum, setDatum] = useState(todayStr());
  const qc = useQueryClient();
  const { toast } = useToast();

  const { data: creditsData, isLoading: creditsLoading } = useQuery<{ datum: string; kredite: KreditRow[] }>({
    queryKey: ["admin-kredite-dag", datum],
    queryFn: async () => {
      const res = await fetch(\`/api/admin/kredite?datum=\${datum}\`, {
        headers: { Authorization: \`Bearer \${token}\` },
      });
      const json = await res.json();
      if (!res.ok) throw new Error(json.error || \`HTTP \${res.status}\`);
      return json;
    },
  });

  const { data: salesData, isLoading: salesLoading } = useGetAdminDaySales(
    { datum },
    { query: { enabled: !!datum, queryKey: getGetAdminDaySalesQueryKey({ datum }) } }
  );

  const { data: productsData, isLoading: productsLoading } = useListAdminProducts();

  const adjustCredit = useMutation({
    mutationFn: async ({ spielerId, delta }: { spielerId: number; delta: 1 | -1 }) => {
      const res = await fetch(\`/api/admin/kredite/adjust\`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: \`Bearer \${token}\`
        },
        body: JSON.stringify({
          spielerId,
          datum,
          delta,
          externalId: \`portal-\${Date.now()}-\${Math.random().toString(36).substring(2, 9)}\`
        })
      });
      const json = await res.json();
      if (!res.ok) throw new Error(json.error || \`HTTP \${res.status}\`);
      return json;
    },
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["admin-kredite-dag", datum] });
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
    },
    onError: (err) => toast({ title: "Feeler", description: err.message, variant: "destructive" })
  });

  const adjustAmmo = useMutation({
    mutationFn: async ({ spielerId, productId, delta }: { spielerId: number; productId: number; delta: 1 | -1 }) => {
      const res = await fetch(\`/api/admin/ammo/adjust\`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: \`Bearer \${token}\`
        },
        body: JSON.stringify({
          spielerId,
          datum,
          productId,
          delta,
          externalId: \`portal-\${Date.now()}-\${Math.random().toString(36).substring(2, 9)}\`
        })
      });
      const json = await res.json();
      if (!res.ok) throw new Error(json.error || \`HTTP \${res.status}\`);
      return json;
    },
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
    },
    onError: (err) => toast({ title: "Feeler", description: err.message, variant: "destructive" })
  });

  const isLoading = creditsLoading || salesLoading || productsLoading;
  const isToday = datum === todayStr();
  const products = productsData?.products ?? [];
  const ammo12Prod = products.find(p => p.category === "AMMO_CAL12");
  const ammo20Prod = products.find(p => p.category === "AMMO_CAL20");

  const combinedMap = new Map<number, any>();

  const rows = creditsData?.kredite ?? [];
  for (const r of rows) {
    combinedMap.set(r.spielerId, {
      ...r,
      rest: r.gewaehrt - r.verbraucht,
      ammo12: 0,
      ammo20: 0,
    });
  }

  const sales = salesData?.sales ?? [];
  for (const s of sales) {
    let row = combinedMap.get(s.spielerId);
    if (!row) {
      row = {
        spielerId: s.spielerId,
        name: s.spielerName,
        mitgliedNr: null,
        gewaehrt: 0,
        verbraucht: 0,
        rest: 0,
        ammo12: 0,
        ammo20: 0,
      };
      combinedMap.set(s.spielerId, row);
    }
    if (ammo12Prod && s.productId === ammo12Prod.id) row.ammo12 += s.quantity;
    if (ammo20Prod && s.productId === ammo20Prod.id) row.ammo20 += s.quantity;
  }

  const combinedRows = Array.from(combinedMap.values()).sort((a, b) => a.name.localeCompare(b.name));

  const totalGewaehrt = combinedRows.reduce((s, r) => s + r.gewaehrt, 0);
  const totalVerbraucht = combinedRows.reduce((s, r) => s + r.verbraucht, 0);
  const totalRest = totalGewaehrt - totalVerbraucht;

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between flex-wrap gap-4">
        <p className="text-sm text-muted-foreground font-medium">
          Virbezuelte Spiller pro Dag, inklusiv Munitioun.{isToday && " Fir haut kënnen dës hei ugepasst ginn."}
        </p>
        <input
          type="date"
          value={datum}
          onChange={(e) => e.target.value && setDatum(e.target.value)}
          className="bg-background border border-border/60 rounded-lg px-4 py-2.5 text-sm font-medium focus:outline-none focus:ring-2 focus:ring-primary/40 transition-colors"
        />
      </div>

      <div className="grid grid-cols-3 gap-4">
        <StatCard label="Kaaft" value={totalGewaehrt} />
        <StatCard label="Gespillt" value={totalVerbraucht} />
        <StatCard label={isToday ? "Nach oppen" : "Net benotzt"} value={totalRest} />
      </div>

      <div className="bg-card border border-border/50 rounded-xl overflow-hidden shadow-sm">
        {isLoading ? (
          <div className="p-6 space-y-3">{[1, 2, 3].map(i => <Skeleton key={i} className="h-14 w-full rounded-md bg-secondary/30" />)}</div>
        ) : combinedRows.length === 0 ? (
          <div className="p-12 text-center text-muted-foreground">
            <Coins size={32} className="mx-auto mb-3 opacity-30" />
            <p className="text-sm font-medium">Keng Kreditten oder Munitioun fir den {datum}.</p>
          </div>
        ) : (
          <div className="overflow-x-auto">
            <Table>
              <TableHeader className="bg-secondary/20">
                <TableRow className="border-border/50 hover:bg-transparent">
                  <TableHead className="text-xs uppercase tracking-widest font-bold">Spiller</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Kreditter (Rescht)</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Gespillt</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Cal. 12</TableHead>
                  <TableHead className="text-center text-xs uppercase tracking-widest font-bold">Cal. 20</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {combinedRows.map((r) => {
                  return (
                    <TableRow key={r.spielerId} className="border-border/30 hover:bg-secondary/20 transition-colors">
                      <TableCell>
                        <div className="font-bold text-foreground truncate max-w-[180px]" title={r.name}>{r.name}</div>
                        {r.mitgliedNr && <div className="text-muted-foreground text-[10px] font-mono">{r.mitgliedNr}</div>}
                      </TableCell>
                      <TableCell className="text-center font-mono">
                        {isToday ? (
                          <div className="flex items-center justify-center gap-1.5">
                            <button
                              disabled={r.rest <= 0 || adjustCredit.isPending}
                              onClick={() => adjustCredit.mutate({ spielerId: r.spielerId, delta: -1 })}
                              className="w-7 h-7 flex items-center justify-center rounded-md bg-secondary text-secondary-foreground hover:bg-secondary/80 disabled:opacity-30 disabled:pointer-events-none transition-colors border border-border/50"
                              aria-label="Kredit -1"
                              title="Kredit -1"
                              data-testid={`credit-minus-${r.spielerId}`}
                            ><Minus size={14} strokeWidth={3} /></button>
                            <span className={cn("w-6 text-center font-bold text-base", r.rest > 0 ? "text-primary" : "text-muted-foreground/40")}>{r.rest}</span>
                            <button
                              disabled={adjustCredit.isPending}
                              onClick={() => adjustCredit.mutate({ spielerId: r.spielerId, delta: 1 })}
                              className="w-7 h-7 flex items-center justify-center rounded-md bg-secondary text-secondary-foreground hover:bg-secondary/80 disabled:opacity-30 disabled:pointer-events-none transition-colors border border-border/50"
                              aria-label="Kredit +1"
                              title="Kredit +1"
                              data-testid={`credit-plus-${r.spielerId}`}
                            ><Plus size={14} strokeWidth={3} /></button>
                          </div>
                        ) : (
                          <span className="font-bold">{r.rest > 0 ? <span className="text-primary">{r.rest}</span> : <span className="text-muted-foreground/40">0</span>}</span>
                        )}
                      </TableCell>
                      <TableCell className="text-center font-mono text-muted-foreground">
                        {r.verbraucht}
                      </TableCell>
                      <TableCell className="text-center font-mono">
                        {isToday ? (
                          <div className="flex items-center justify-center gap-1.5">
                            <button
                              disabled={r.ammo12 <= 0 || !ammo12Prod || adjustAmmo.isPending}
                              onClick={() => ammo12Prod && adjustAmmo.mutate({ spielerId: r.spielerId, productId: ammo12Prod.id, delta: -1 })}
                              className="w-7 h-7 flex items-center justify-center rounded-md bg-secondary text-secondary-foreground hover:bg-secondary/80 disabled:opacity-30 disabled:pointer-events-none transition-colors border border-border/50"
                              aria-label="Cal.12 -1"
                              title="Cal.12 -1"
                              data-testid={`ammo12-minus-${r.spielerId}`}
                            ><Minus size={14} strokeWidth={3} /></button>
                            <span className={cn("w-6 text-center font-bold text-base", r.ammo12 > 0 ? "text-amber-500" : "text-muted-foreground/40")}>{r.ammo12}</span>
                            <button
                              disabled={!ammo12Prod || adjustAmmo.isPending}
                              onClick={() => ammo12Prod && adjustAmmo.mutate({ spielerId: r.spielerId, productId: ammo12Prod.id, delta: 1 })}
                              className="w-7 h-7 flex items-center justify-center rounded-md bg-secondary text-secondary-foreground hover:bg-secondary/80 disabled:opacity-30 disabled:pointer-events-none transition-colors border border-border/50"
                              aria-label="Cal.12 +1"
                              title="Cal.12 +1"
                              data-testid={`ammo12-plus-${r.spielerId}`}
                            ><Plus size={14} strokeWidth={3} /></button>
                          </div>
                        ) : (
                          <span className="font-bold">{r.ammo12 > 0 ? <span className="text-amber-500">{r.ammo12}</span> : <span className="text-muted-foreground/40">0</span>}</span>
                        )}
                      </TableCell>
                      <TableCell className="text-center font-mono">
                        {isToday ? (
                          <div className="flex items-center justify-center gap-1.5">
                            <button
                              disabled={r.ammo20 <= 0 || !ammo20Prod || adjustAmmo.isPending}
                              onClick={() => ammo20Prod && adjustAmmo.mutate({ spielerId: r.spielerId, productId: ammo20Prod.id, delta: -1 })}
                              className="w-7 h-7 flex items-center justify-center rounded-md bg-secondary text-secondary-foreground hover:bg-secondary/80 disabled:opacity-30 disabled:pointer-events-none transition-colors border border-border/50"
                              aria-label="Cal.20 -1"
                              title="Cal.20 -1"
                              data-testid={`ammo20-minus-${r.spielerId}`}
                            ><Minus size={14} strokeWidth={3} /></button>
                            <span className={cn("w-6 text-center font-bold text-base", r.ammo20 > 0 ? "text-amber-500" : "text-muted-foreground/40")}>{r.ammo20}</span>
                            <button
                              disabled={!ammo20Prod || adjustAmmo.isPending}
                              onClick={() => ammo20Prod && adjustAmmo.mutate({ spielerId: r.spielerId, productId: ammo20Prod.id, delta: 1 })}
                              className="w-7 h-7 flex items-center justify-center rounded-md bg-secondary text-secondary-foreground hover:bg-secondary/80 disabled:opacity-30 disabled:pointer-events-none transition-colors border border-border/50"
                              aria-label="Cal.20 +1"
                              title="Cal.20 +1"
                              data-testid={`ammo20-plus-${r.spielerId}`}
                            ><Plus size={14} strokeWidth={3} /></button>
                          </div>
                        ) : (
                          <span className="font-bold">{r.ammo20 > 0 ? <span className="text-amber-500">{r.ammo20}</span> : <span className="text-muted-foreground/40">0</span>}</span>
                        )}
                      </TableCell>
                    </TableRow>
                  );
                })}
              </TableBody>
            </Table>
          </div>
        )}
      </div>
    </div>
  );
}

`

fs.writeFileSync("artifacts/portal/src/pages/admin-kredite.tsx", file.substring(0, start) + newContent + file.substring(end));

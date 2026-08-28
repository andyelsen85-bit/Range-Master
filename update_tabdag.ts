import fs from "fs";

const file = fs.readFileSync("artifacts/portal/src/pages/admin-kredite.tsx", "utf-8");

const start = file.indexOf("function TabDag({ token }: { token: string | null }) {");
const end = file.indexOf("// ── Tab: Joer");

let importsContent = file.substring(0, start);
importsContent = importsContent.replace("Plus, Minus", "Plus, Minus, RotateCcw, Loader2");

const newContent = `type OpKey = string;
type OpState = { externalId: string; status: "pending" | "failed" };

function TabDag({ token }: { token: string | null }) {
  const [datum, setDatum] = useState(todayStr());
  const qc = useQueryClient();
  const { toast } = useToast();

  const [ops, setOps] = useState<Record<OpKey, OpState>>({});

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
    mutationFn: async ({ spielerId, delta, externalId }: { spielerId: number; delta: 1 | -1; externalId: string; opKey: string }) => {
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
          externalId
        })
      });
      const json = await res.json();
      if (!res.ok) {
        const err = new Error(json.error || \`HTTP \${res.status}\`);
        (err as any).status = res.status;
        throw err;
      }
      return json;
    },
    onSuccess: (data, variables) => {
      qc.invalidateQueries({ queryKey: ["admin-kredite-dag", datum] });
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
      toast({ title: "Späicheren erfollegräich", description: "D'Kreditter goufen ugepasst." });
      setOps(prev => { const next = { ...prev }; delete next[variables.opKey]; return next; });
    },
    onError: (err: any, variables) => {
      toast({ title: "Feeler", description: err.message, variant: "destructive" });
      const status = err.status;
      if (status && status >= 400 && status < 500 && status !== 408 && status !== 429) {
        setOps(prev => { const next = { ...prev }; delete next[variables.opKey]; return next; });
      } else {
        setOps(prev => ({ ...prev, [variables.opKey]: { ...prev[variables.opKey], status: "failed" } }));
      }
    }
  });

  const adjustAmmo = useMutation({
    mutationFn: async ({ spielerId, productId, delta, externalId }: { spielerId: number; productId: number; delta: 1 | -1; externalId: string; opKey: string }) => {
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
          externalId
        })
      });
      const json = await res.json();
      if (!res.ok) {
        const err = new Error(json.error || \`HTTP \${res.status}\`);
        (err as any).status = res.status;
        throw err;
      }
      return json;
    },
    onSuccess: (data, variables) => {
      qc.invalidateQueries({ queryKey: getGetAdminDaySalesQueryKey({ datum }) });
      toast({ title: "Späicheren erfollegräich", description: "D'Munitioun gouf ugepasst." });
      setOps(prev => { const next = { ...prev }; delete next[variables.opKey]; return next; });
    },
    onError: (err: any, variables) => {
      toast({ title: "Feeler", description: err.message, variant: "destructive" });
      const status = err.status;
      if (status && status >= 400 && status < 500 && status !== 408 && status !== 429) {
        setOps(prev => { const next = { ...prev }; delete next[variables.opKey]; return next; });
      } else {
        setOps(prev => ({ ...prev, [variables.opKey]: { ...prev[variables.opKey], status: "failed" } }));
      }
    }
  });

  const handleAdjustCredit = (spielerId: number, delta: 1 | -1) => {
    const opKey = \`\${spielerId}-credit-\${delta}\`;
    let externalId = ops[opKey]?.externalId;
    if (!externalId || ops[opKey]?.status !== "failed") {
      externalId = \`portal-\${Date.now()}-\${Math.random().toString(36).substring(2, 9)}\`;
    }
    setOps(prev => ({ ...prev, [opKey]: { externalId, status: "pending" } }));
    adjustCredit.mutate({ spielerId, delta, externalId, opKey });
  };

  const handleAdjustAmmo = (spielerId: number, productId: number, delta: 1 | -1, ammoType: '12' | '20') => {
    const opKey = \`\${spielerId}-ammo\${ammoType}-\${delta}\`;
    let externalId = ops[opKey]?.externalId;
    if (!externalId || ops[opKey]?.status !== "failed") {
      externalId = \`portal-\${Date.now()}-\${Math.random().toString(36).substring(2, 9)}\`;
    }
    setOps(prev => ({ ...prev, [opKey]: { externalId, status: "pending" } }));
    adjustAmmo.mutate({ spielerId, productId, delta, externalId, opKey });
  };

  const renderIcon = (opKey: string, DefaultIcon: any) => {
    const state = ops[opKey];
    if (state?.status === "pending") return <Loader2 size={14} strokeWidth={3} className="animate-spin" />;
    if (state?.status === "failed") return <RotateCcw size={14} strokeWidth={3} />;
    return <DefaultIcon size={14} strokeWidth={3} />;
  };

  const getBtnClass = (opKey: string) => {
    const state = ops[opKey];
    if (state?.status === "failed") return "border-destructive text-destructive bg-destructive/10 hover:bg-destructive/20";
    return "bg-secondary text-secondary-foreground hover:bg-secondary/80 border-border/50";
  };

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
                  const creditMinusKey = \`\${r.spielerId}-credit--1\`;
                  const creditPlusKey = \`\${r.spielerId}-credit-1\`;
                  const ammo12MinusKey = \`\${r.spielerId}-ammo12--1\`;
                  const ammo12PlusKey = \`\${r.spielerId}-ammo12-1\`;
                  const ammo20MinusKey = \`\${r.spielerId}-ammo20--1\`;
                  const ammo20PlusKey = \`\${r.spielerId}-ammo20-1\`;

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
                              disabled={r.rest <= 0 || ops[creditMinusKey]?.status === "pending"}
                              onClick={() => handleAdjustCredit(r.spielerId, -1)}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(creditMinusKey))}
                              aria-label="Kredit -1"
                              title="Kredit -1"
                              data-testid={\`credit-minus-\${r.spielerId}\`}
                            >{renderIcon(creditMinusKey, Minus)}</button>
                            <span className={cn("w-6 text-center font-bold text-base", r.rest > 0 ? "text-primary" : "text-muted-foreground/40")}>{r.rest}</span>
                            <button
                              disabled={ops[creditPlusKey]?.status === "pending"}
                              onClick={() => handleAdjustCredit(r.spielerId, 1)}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(creditPlusKey))}
                              aria-label="Kredit +1"
                              title="Kredit +1"
                              data-testid={\`credit-plus-\${r.spielerId}\`}
                            >{renderIcon(creditPlusKey, Plus)}</button>
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
                              disabled={r.ammo12 <= 0 || !ammo12Prod || ops[ammo12MinusKey]?.status === "pending"}
                              onClick={() => ammo12Prod && handleAdjustAmmo(r.spielerId, ammo12Prod.id, -1, '12')}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(ammo12MinusKey))}
                              aria-label="Cal.12 -1"
                              title="Cal.12 -1"
                              data-testid={\`ammo12-minus-\${r.spielerId}\`}
                            >{renderIcon(ammo12MinusKey, Minus)}</button>
                            <span className={cn("w-6 text-center font-bold text-base", r.ammo12 > 0 ? "text-amber-500" : "text-muted-foreground/40")}>{r.ammo12}</span>
                            <button
                              disabled={!ammo12Prod || ops[ammo12PlusKey]?.status === "pending"}
                              onClick={() => ammo12Prod && handleAdjustAmmo(r.spielerId, ammo12Prod.id, 1, '12')}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(ammo12PlusKey))}
                              aria-label="Cal.12 +1"
                              title="Cal.12 +1"
                              data-testid={\`ammo12-plus-\${r.spielerId}\`}
                            >{renderIcon(ammo12PlusKey, Plus)}</button>
                          </div>
                        ) : (
                          <span className="font-bold">{r.ammo12 > 0 ? <span className="text-amber-500">{r.ammo12}</span> : <span className="text-muted-foreground/40">0</span>}</span>
                        )}
                      </TableCell>
                      <TableCell className="text-center font-mono">
                        {isToday ? (
                          <div className="flex items-center justify-center gap-1.5">
                            <button
                              disabled={r.ammo20 <= 0 || !ammo20Prod || ops[ammo20MinusKey]?.status === "pending"}
                              onClick={() => ammo20Prod && handleAdjustAmmo(r.spielerId, ammo20Prod.id, -1, '20')}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(ammo20MinusKey))}
                              aria-label="Cal.20 -1"
                              title="Cal.20 -1"
                              data-testid={\`ammo20-minus-\${r.spielerId}\`}
                            >{renderIcon(ammo20MinusKey, Minus)}</button>
                            <span className={cn("w-6 text-center font-bold text-base", r.ammo20 > 0 ? "text-amber-500" : "text-muted-foreground/40")}>{r.ammo20}</span>
                            <button
                              disabled={!ammo20Prod || ops[ammo20PlusKey]?.status === "pending"}
                              onClick={() => ammo20Prod && handleAdjustAmmo(r.spielerId, ammo20Prod.id, 1, '20')}
                              className={cn("w-7 h-7 flex items-center justify-center rounded-md disabled:opacity-30 disabled:pointer-events-none transition-colors border", getBtnClass(ammo20PlusKey))}
                              aria-label="Cal.20 +1"
                              title="Cal.20 +1"
                              data-testid={\`ammo20-plus-\${r.spielerId}\`}
                            >{renderIcon(ammo20PlusKey, Plus)}</button>
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
\n`;

fs.writeFileSync("artifacts/portal/src/pages/admin-kredite.tsx", importsContent + newContent + file.substring(end));

import { useGetPortalHealth } from "@workspace/api-client-react";

export function PortalBuildStatus() {
  const health=useGetPortalHealth({query:{queryKey:["portal-build-health"],staleTime:30_000,refetchInterval:60_000,retry:1}});
  const info=health.data?.build && typeof health.data.build.commit==="string" &&
    Array.isArray(health.data.syncRoutes) && health.data.syncRoutes.every(route=>
      typeof route?.method==="string" && typeof route?.path==="string") ? health.data : undefined;
  const unavailable=health.isError || (!!health.data && !info);
  return <details className="rounded-lg border border-border/60 bg-card p-4">
    <summary className="cursor-pointer text-sm font-semibold">
      Portal-Version und Sync-Schnittstellen
      {info && <span className="ml-3 font-mono text-xs text-muted-foreground">
        {info.build.commit.slice(0,12)}{info.build.dirty?" (lokal verändert)":""}
      </span>}
      {unavailable && <span className="ml-3 text-xs text-destructive">Version nicht prüfbar</span>}
    </summary>
    <div className="mt-3 space-y-2 text-sm">
      {health.isPending && <p>Portal-Version wird geladen…</p>}
      {unavailable && <p role="alert" className="text-destructive">
        Portal-Version nicht verfügbar. Bei HTTP 404 ist das Portal möglicherweise veraltet.
      </p>}
      {info && <>
        <p className="break-all">Commit: <code>{info.build.commit}</code></p>
        <p>Build: {info.build.builtAt?new Date(info.build.builtAt).toLocaleString("de-LU"):"Ungebündelter Entwicklungsserver"}</p>
        <p>{info.syncRoutes.length} registrierte Sync-Schnittstellen</p>
        <pre className="max-h-64 overflow-auto rounded bg-secondary p-3 text-xs">
          {info.syncRoutes.map(route=>`${route.method} ${route.path}`).join("\n")}
        </pre>
      </>}
    </div>
  </details>;
}

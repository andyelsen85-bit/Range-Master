import type { IRouter } from "express";

declare const __PORTAL_BUILD__: {commit:string;builtAt:string;dirty:boolean};
export const portalBuild = typeof __PORTAL_BUILD__ === "undefined"
  ? {commit:"unbundled",builtAt:null,dirty:true} : __PORTAL_BUILD__;
export interface SyncRoute { method:string; path:string }
let syncRoutes:SyncRoute[]=[];
let reportRoutes:SyncRoute[]=[];

/** Inspect the actual mounted routers, not a separately maintained route list. */
export function configureSyncRoutes(mounts:{router:IRouter;prefix:string}[]) {
  const routes=new Map<string,SyncRoute>();
  for(const {router,prefix} of mounts)for(const layer of router.stack) {
    const route=layer.route;if(!route)continue;
    for(const path of Array.isArray(route.path)?route.path:[route.path]) {
      if(typeof path!=="string")continue;
      const full=prefix+path;
      if(!full.startsWith("/api/sync/"))continue;
      const methods=(route as unknown as {methods:Record<string,boolean>}).methods;
      for(const [method,enabled] of Object.entries(methods))if(enabled) {
        const value={method:method.toUpperCase(),path:full};
        routes.set(`${value.method} ${full}`,value);
      }
    }
  }
  syncRoutes=[...routes.values()].sort((a,b)=>a.path.localeCompare(b.path)||a.method.localeCompare(b.method));
}
export function configureReportRoutes(mounts:{router:IRouter;prefix:string}[]) {
  reportRoutes=mounts.flatMap(({router,prefix})=>router.stack.flatMap(layer=>{
    const route=layer.route;
    if(!route||typeof route.path!=="string"||!/(scorecard|print|days)/.test(route.path))return [];
    return Object.entries((route as unknown as {methods:Record<string,boolean>}).methods)
      .filter(([,enabled])=>enabled).map(([method])=>({method:method.toUpperCase(),path:prefix+route.path}));
  }));
}
export function portalHealth() { return {status:"ok" as const,build:portalBuild,syncRoutes,reportRoutes}; }

import { ReactNode } from "react";
import { Link, useLocation } from "wouter";
import { useAuthStore } from "@/store/use-auth-store";
import { LogOut, LayoutDashboard, List, Trophy, BarChart3, Target, Shield, User, KeyRound } from "lucide-react";
import { cn } from "@/lib/utils";

export default function Layout({ children }: { children: ReactNode }) {
  const [location] = useLocation();
  const logout = useAuthStore((s) => s.logout);
  const user = useAuthStore((s) => s.user);

  const navItems = [
    { href: "/", label: "Dashboard", icon: LayoutDashboard },
    { href: "/resultater", label: "Meng Resultater", icon: List },
    { href: "/rangliste", label: "Ranglischt", icon: Trophy },
    { href: "/statistiken", label: "Statistiken", icon: BarChart3 },
    ...(user?.isAdmin ? [
      { href: "/admin", label: "Spillerverwaltung", icon: Shield },
      { href: "/admin/api-schluesselen", label: "API Schlësselen", icon: KeyRound },
    ] : []),
  ];

  return (
    <div className="min-h-screen bg-background text-foreground flex flex-col md:flex-row font-sans">
      <aside className="w-full md:w-64 bg-card border-b md:border-b-0 md:border-r border-border/50 flex flex-col shrink-0">
        <div className="p-6 flex items-center gap-3 border-b border-border/50 bg-secondary/20">
          <div className="w-10 h-10 rounded-full bg-primary flex items-center justify-center text-primary-foreground shadow-lg shadow-primary/20">
            <Target size={22} strokeWidth={2.5} />
          </div>
          <div>
            <h1 className="font-bold text-lg leading-none tracking-tight text-foreground uppercase">TrapMaster</h1>
            <p className="text-[10px] text-primary mt-0.5 uppercase tracking-widest font-mono font-bold">Wolz Sektioun</p>
          </div>
        </div>

        <nav className="flex-1 p-4 space-y-1.5 overflow-y-auto">
          {navItems.map((item) => {
            const isActive = location === item.href;
            const Icon = item.icon;
            return (
              <Link key={item.href} href={item.href}>
                <div
                  className={cn(
                    "flex items-center gap-3 px-3 py-2.5 rounded-lg cursor-pointer transition-all duration-200 text-sm font-semibold tracking-wide",
                    isActive
                      ? "bg-primary/10 text-primary border border-primary/20 shadow-sm"
                      : "text-muted-foreground hover:bg-secondary/80 hover:text-foreground border border-transparent",
                  )}
                  data-testid={`nav-${item.label.toLowerCase().replace(/ /g, "-")}`}
                >
                  <Icon size={18} className={isActive ? "text-primary" : "opacity-60"} strokeWidth={isActive ? 2.5 : 2} />
                  {item.label}
                </div>
              </Link>
            );
          })}
        </nav>

        <div className="p-4 border-t border-border/50 space-y-1">
          {/* Profil link */}
          <Link href="/profil">
            <div className={cn(
              "flex items-center gap-3 px-3 py-2.5 rounded-lg cursor-pointer transition-all duration-200 text-sm font-semibold tracking-wide",
              location === "/profil"
                ? "bg-primary/10 text-primary border border-primary/20"
                : "text-muted-foreground hover:bg-secondary/80 hover:text-foreground border border-transparent",
            )}>
              <User size={18} className="opacity-60" strokeWidth={2} />
              Mäi Profil
            </div>
          </Link>
          <button
            onClick={logout}
            className="flex items-center gap-3 px-3 py-2.5 rounded-lg w-full text-left text-sm font-semibold tracking-wide text-muted-foreground hover:bg-destructive/10 hover:text-destructive hover:border-destructive/20 border border-transparent transition-all"
            data-testid="button-logout"
          >
            <LogOut size={18} className="opacity-60" strokeWidth={2} />
            Ausloggen
          </button>
        </div>
      </aside>
      <main className="flex-1 overflow-auto p-4 md:p-8 bg-background relative">
        <div className="max-w-5xl mx-auto relative z-10">
          {children}
        </div>
      </main>
    </div>
  );
}

import { useState, useEffect } from "react";
import { useLocation } from "wouter";
import { useAuthStore } from "@/store/use-auth-store";
import { useLogin } from "@workspace/api-client-react";
import { Target, Loader2 } from "lucide-react";
import { useToast } from "@/hooks/use-toast";
import { Input } from "@/components/ui/input";
import { Button } from "@/components/ui/button";
import { Label } from "@/components/ui/label";

export default function Login() {
  const [, setLocation] = useLocation();
  const setAuth = useAuthStore((s) => s.setAuth);
  const { toast } = useToast();

  useEffect(() => {
    if (sessionStorage.getItem('rangemaster-session-expired')) {
      sessionStorage.removeItem('rangemaster-session-expired');
      setTimeout(() => {
        toast({
          title: "Sëtzung ofgelaf",
          description: "Är 30-Minutten Sëtzung ass ofgelaf. Loggt iech nees an.",
          variant: "destructive",
        });
      }, 300);
    }
  }, []);
  
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");

  const loginMutation = useLogin();

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    loginMutation.mutate(
      { data: { email, passwort: password } },
      {
        onSuccess: (res) => {
          setAuth(res.spieler, res.token);
          toast({ title: "Umellen erfollegräich", description: "Wëllkomm zréck am Range-Master!" });
          setLocation("/");
        },
        onError: () => {
          toast({ 
            title: "Feeler beim Umellen", 
            description: "Iwwerpréift w.e.g. Är Email an Äert Passwuert.",
            variant: "destructive" 
          });
        }
      }
    );
  };

  return (
    <div className="min-h-screen bg-background flex flex-col justify-center items-center p-4 relative overflow-hidden font-sans">
      <div className="absolute top-[-20%] left-[-10%] w-[500px] h-[500px] bg-primary/10 rounded-full blur-[120px] pointer-events-none" />
      <div className="absolute bottom-[-20%] right-[-10%] w-[600px] h-[600px] bg-chart-3/10 rounded-full blur-[150px] pointer-events-none" />

      <div className="w-full max-w-md bg-card/80 backdrop-blur-xl border border-border/50 rounded-2xl shadow-2xl shadow-black/50 overflow-hidden relative z-10">
        <div className="p-10 pb-8 border-b border-border/50 bg-secondary/30 flex flex-col items-center">
          <div className="w-20 h-20 rounded-full bg-primary flex items-center justify-center text-primary-foreground mb-6 shadow-xl shadow-primary/20 ring-4 ring-primary/20">
            <Target size={40} strokeWidth={2} />
          </div>
          <h1 className="text-3xl font-bold tracking-tight uppercase text-foreground mb-1">Range-Master</h1>
          <div className="h-0.5 w-12 bg-primary my-2 rounded-full" />
          <p className="text-xs text-muted-foreground uppercase tracking-[0.2em] font-bold">F.S.H.C.L. Sektioun Wolz</p>
        </div>
        
        <form onSubmit={handleSubmit} className="p-10 space-y-6">
          <div className="space-y-3">
            <Label htmlFor="email" className="text-xs uppercase tracking-wider font-bold text-muted-foreground">Email Adress</Label>
            <Input 
              id="email" 
              type="email" 
              placeholder="z.B. jemp@example.lu"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              required
              className="h-12 bg-background/50 border-border focus:border-primary/50 text-base"
              data-testid="input-email"
            />
          </div>
          
          <div className="space-y-3">
            <Label htmlFor="password" className="text-xs uppercase tracking-wider font-bold text-muted-foreground">Passwuert</Label>
            <Input 
              id="password" 
              type="password" 
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              required
              className="h-12 bg-background/50 border-border focus:border-primary/50 text-base"
              data-testid="input-password"
            />
          </div>

          <Button 
            type="submit" 
            className="w-full h-14 text-base font-bold uppercase tracking-wider mt-4 shadow-lg shadow-primary/20"
            disabled={loginMutation.isPending}
            data-testid="button-submit"
          >
            {loginMutation.isPending ? <Loader2 className="animate-spin mr-2" /> : null}
            Umellen
          </Button>
        </form>
      </div>
    </div>
  );
}

import { useEffect } from 'react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { Toaster } from '@/components/ui/toaster';
import { TooltipProvider } from '@/components/ui/tooltip';
import { Route, Switch, Router as WouterRouter, useLocation } from 'wouter';
import { setAuthTokenGetter, setBaseUrl } from '@workspace/api-client-react/custom-fetch';
import { setOnUnauthorized } from '@workspace/api-client-react/custom-fetch';
import { useAuthStore } from '@/store/use-auth-store';

import Layout from '@/components/layout';
import Login from '@/pages/login';
import Dashboard from '@/pages/dashboard';
import Resultater from '@/pages/resultater';
import Rangliste from '@/pages/rangliste';
import Statistiken from '@/pages/statistiken';
import Admin from '@/pages/admin';
import AdminApiKeys from '@/pages/admin-api-keys';
import AdminSpielrProfil from '@/pages/admin-spieler-profil';
import Profil from '@/pages/profil';
import NotFound from '@/pages/not-found';

const queryClient = new QueryClient();

setBaseUrl(null);
setAuthTokenGetter(() => useAuthStore.getState().token);

// Auto-logout: any 401 from the API clears the session and redirects to /login
setOnUnauthorized(() => {
  if (useAuthStore.getState().isAuthenticated) {
    useAuthStore.getState().logout();
    sessionStorage.setItem('trapmaster-session-expired', '1');
    const base = import.meta.env.BASE_URL?.replace(/\/$/, '') ?? '';
    window.location.replace(`${base}/login`);
  }
});

function PrivateRoute({ component: Component, ...rest }: any) {
  const isAuthenticated = useAuthStore((s) => s.isAuthenticated);
  const [, setLocation] = useLocation();
  useEffect(() => {
    if (!isAuthenticated) setLocation('/login');
  }, [isAuthenticated, setLocation]);
  if (!isAuthenticated) return null;
  return <Component {...rest} />;
}

function AdminRoute({ component: Component, ...rest }: any) {
  const isAuthenticated = useAuthStore((s) => s.isAuthenticated);
  const user = useAuthStore((s) => s.user);
  const [, setLocation] = useLocation();
  useEffect(() => {
    if (!isAuthenticated) setLocation('/login');
    else if (!user?.isAdmin) setLocation('/');
  }, [isAuthenticated, user, setLocation]);
  if (!isAuthenticated || !user?.isAdmin) return null;
  return <Component {...rest} />;
}

function Router() {
  return (
    <Switch>
      <Route path="/login" component={Login} />
      <Route>
        <Layout>
          <Switch>
            <Route path="/" component={() => <PrivateRoute component={Dashboard} />} />
            <Route path="/resultater" component={() => <PrivateRoute component={Resultater} />} />
            <Route path="/rangliste" component={() => <PrivateRoute component={Rangliste} />} />
            <Route path="/statistiken" component={() => <PrivateRoute component={Statistiken} />} />
            <Route path="/profil" component={() => <PrivateRoute component={Profil} />} />
            <Route path="/admin" component={() => <AdminRoute component={Admin} />} />
            <Route path="/admin/spieler/:id" component={(props) => <AdminRoute component={AdminSpielrProfil} {...props} />} />
            <Route path="/admin/api-schluesselen" component={() => <AdminRoute component={AdminApiKeys} />} />
            <Route component={NotFound} />
          </Switch>
        </Layout>
      </Route>
    </Switch>
  );
}

function App() {
  useEffect(() => {
    document.documentElement.classList.add('dark');
  }, []);
  return (
    <QueryClientProvider client={queryClient}>
      <TooltipProvider>
        <WouterRouter base={import.meta.env.BASE_URL.replace(/\/$/, '')}>
          <Router />
        </WouterRouter>
        <Toaster />
      </TooltipProvider>
    </QueryClientProvider>
  );
}

export default App;

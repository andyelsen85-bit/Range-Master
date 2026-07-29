import React, { useState } from 'react';
import { useGameStore } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { ArrowLeft, Save, RefreshCw, CheckCircle2, AlertTriangle } from 'lucide-react';

export function EinstellungenScreen() {
  const store = useGameStore();
  
  const [url, setUrl] = useState(store.apiUrl);
  const [key, setKey] = useState(store.apiKey);
  
  const save = () => {
    store.setApiSettings(url, key);
  };

  const sync = async () => {
    await store.syncPortal();
  };

  return (
    <div className="flex h-full w-full bg-background flex-col">
      <header className="h-20 border-b-2 border-border flex items-center px-6 bg-card gap-6">
        <TouchButton onClick={() => store.setScreen('dashboard')} className="w-16 h-16 p-0">
          <ArrowLeft className="w-8 h-8" />
        </TouchButton>
        <h1 className="text-2xl font-bold tracking-wider text-primary">EINSTELLUNGEN</h1>
      </header>

      <div className="flex-1 p-8 overflow-y-auto">
        <div className="max-w-4xl mx-auto flex flex-col gap-8">
          
          <div className="bg-card border-2 border-border rounded-xl p-8 flex flex-col gap-8">
            <div>
              <h2 className="text-2xl font-bold uppercase tracking-widest mb-6">Portal API Konfiguration</h2>
              
              <div className="space-y-6">
                <div>
                  <label className="block text-lg font-bold text-muted-foreground mb-3 uppercase">API Endpoint URL</label>
                  <input 
                    type="text" 
                    value={url}
                    onChange={(e) => setUrl(e.target.value)}
                    className="w-full bg-background border-2 border-border rounded-lg h-16 px-6 text-xl font-mono focus:border-primary focus:outline-none"
                  />
                </div>
                
                <div>
                  <label className="block text-lg font-bold text-muted-foreground mb-3 uppercase">API Key</label>
                  <input 
                    type="password" 
                    value={key}
                    onChange={(e) => setKey(e.target.value)}
                    className="w-full bg-background border-2 border-border rounded-lg h-16 px-6 text-xl font-mono focus:border-primary focus:outline-none"
                  />
                </div>

                <div className="flex gap-4 pt-4">
                  <TouchButton size="lg" variant="primary" className="flex-1 gap-3" onClick={save}>
                    <Save className="w-6 h-6" /> Speichern
                  </TouchButton>
                  <TouchButton size="lg" variant="outline" className="flex-1 gap-3" onClick={sync}>
                    <RefreshCw className={`w-6 h-6 ${store.syncStatus === 'syncing' ? 'animate-spin' : ''}`} /> 
                    Verbindung testen
                  </TouchButton>
                </div>

                {store.syncStatus !== 'idle' && (
                  <div className={`mt-4 p-4 rounded-lg flex items-center gap-4 text-lg font-bold ${
                    store.syncStatus === 'success' ? 'bg-green-500/20 text-green-500 border border-green-500/50' : 
                    store.syncStatus === 'error' ? 'bg-red-500/20 text-red-500 border border-red-500/50' :
                    'bg-yellow-500/20 text-yellow-500 border border-yellow-500/50'
                  }`}>
                    {store.syncStatus === 'success' && <><CheckCircle2 className="w-6 h-6" /> Verbindung erfolgreich hergestellt</>}
                    {store.syncStatus === 'error' && <><AlertTriangle className="w-6 h-6" /> Verbindungsfehler. Bitte URL und Key prüfen.</>}
                    {store.syncStatus === 'syncing' && <><RefreshCw className="w-6 h-6 animate-spin" /> Verbinde mit Portal...</>}
                  </div>
                )}
              </div>
            </div>
          </div>

          <div className="bg-card border-2 border-border rounded-xl p-8 flex flex-col gap-8">
            <h2 className="text-2xl font-bold uppercase tracking-widest mb-2">System Info</h2>
            <div className="grid grid-cols-2 gap-4 font-mono text-lg">
              <div className="p-4 bg-background border-2 border-border rounded-lg">
                <span className="text-muted-foreground block mb-1 text-sm uppercase font-sans">Version</span>
                v1.4.2 (Build 4209)
              </div>
              <div className="p-4 bg-background border-2 border-border rounded-lg">
                <span className="text-muted-foreground block mb-1 text-sm uppercase font-sans">IP Addresse</span>
                192.168.1.144
              </div>
              <div className="p-4 bg-background border-2 border-border rounded-lg">
                <span className="text-muted-foreground block mb-1 text-sm uppercase font-sans">MAC Addresse</span>
                00:1B:44:11:3A:B7
              </div>
              <div className="p-4 bg-background border-2 border-border rounded-lg">
                <span className="text-muted-foreground block mb-1 text-sm uppercase font-sans">Uptime</span>
                14d 02h 45m
              </div>
            </div>
          </div>

        </div>
      </div>
    </div>
  );
}

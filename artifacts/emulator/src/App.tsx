import React, { useState, useEffect } from 'react';
import { useGameStore } from '@/store/gameStore';
import { DashboardScreen } from '@/screens/DashboardScreen';
import { StartScreen } from '@/screens/StartScreen';  
import { SpielScreen } from '@/screens/SpielScreen';
import { EinstellungenScreen } from '@/screens/EinstellungenScreen';
import { ResultateScreen } from '@/screens/ResultateScreen';
import { SpillgeschichteScreen } from '@/screens/SpillgeschichteScreen';
import { KrediteScreen } from '@/screens/KrediteScreen';
import { SpillerScreen } from '@/screens/SpillerScreen';
import { CateringScreen } from '@/screens/CateringScreen';
import { SimControls } from '@/components/SimControls';

export default function App() {
  const screen = useGameStore(s => s.screen);
  const kioskMode = useGameStore(s => s.kioskMode);
  const autoSyncEnabled = useGameStore(s => s.autoSyncEnabled);
  const autoSyncSeconds = useGameStore(s => s.autoSyncSeconds);
  const billingSyncSeconds = useGameStore(s => s.billingSyncSeconds);
  const [scale, setScale] = useState(1);

  // Auto-scale to fit window while preserving exactly 1280x800 layout
  useEffect(() => {
    const handleResize = () => {
      const scaleX = window.innerWidth / 1280;
      const scaleY = window.innerHeight / 800;
      // Allow scaling down, but not scaling up beyond 1.5x to avoid blur
      setScale(Math.min(scaleX, scaleY, 1.2));
    };
    handleResize();
    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  useEffect(() => {
    if (!autoSyncEnabled) return;
    const timer = window.setInterval(() => {
      const store = useGameStore.getState();
      if (store.apiUrl && store.apiKey) void store.syncAllPending();
    }, autoSyncSeconds * 1000);
    return () => window.clearInterval(timer);
  }, [autoSyncEnabled, autoSyncSeconds]);

  useEffect(() => {
    if (!autoSyncEnabled) return;
    const timer = window.setInterval(() => {
      const store = useGameStore.getState();
      if (store.apiUrl && store.apiKey) void store.syncBillingPending();
    }, billingSyncSeconds * 1000);
    return () => window.clearInterval(timer);
  }, [autoSyncEnabled, billingSyncSeconds]);
  
  return (
    <div className="fixed inset-0 bg-black flex items-center justify-center overflow-hidden">
      <div 
        className="w-[1280px] h-[800px] bg-background text-foreground font-mono relative overflow-hidden shadow-[0_0_50px_rgba(232,103,10,0.1)] outline outline-1 outline-primary/20 shrink-0 flex flex-col"
        style={{ transform: `scale(${scale})`, transformOrigin: 'center' }}
      >
        {kioskMode === 'CATERING' ? (
          <CateringScreen />
        ) : (
          <>
            {screen === 'dashboard' && <DashboardScreen />}
            {screen === 'start' && <StartScreen />}
            {screen === 'spiel' && <SpielScreen />}
            {screen === 'einstellungen' && <EinstellungenScreen />}
            {screen === 'resultate' && <ResultateScreen />}
            {screen === 'geschichte' && <SpillgeschichteScreen />}
            {screen === 'kredite' && <KrediteScreen />}
            {screen === 'spillerverwaltung' && <SpillerScreen />}
          </>
        )}
        
        {/* Emulator overlay controls */}
        {kioskMode !== 'CATERING' && <SimControls />}
      </div>
    </div>
  );
}

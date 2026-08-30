import React, { useState, useEffect, useRef } from 'react';
import { useGameStore, Produkt } from '@/store/gameStore';
import { TouchButton } from '@/components/TouchButton';
import { ArrowLeft, Coffee, Lock, CheckCircle2, X } from 'lucide-react';
import { cn } from '@/lib/utils';

interface CartItem {
  produkt: Produkt;
  quantity: number;
}

export function CateringScreen() {
  const store = useGameStore();
  const [selectedPlayerId, setSelectedPlayerId] = useState<number | null>(null);
  const [cart, setCart] = useState<Record<number, number>>({});
  const [showConfirm, setShowConfirm] = useState(false);
  const [showExit, setShowExit] = useState(false);
  const [pin, setPin] = useState('');
  const [pinError, setPinError] = useState(false);
  const [isConfirming, setIsConfirming] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  // Inactivity timeout
  const timerRef = useRef<NodeJS.Timeout | null>(null);

  const resetInactivity = () => {
    if (timerRef.current) clearTimeout(timerRef.current);
    timerRef.current = setTimeout(() => {
      setSelectedPlayerId(null);
      setCart({});
      setShowConfirm(false);
      setShowExit(false);
      setPin('');
      setPinError(false);
      setIsConfirming(false);
      setErrorMessage(null);
    }, 45000); // 45 seconds of inactivity
  };

  useEffect(() => {
    resetInactivity();
    const handlers = ['click', 'touchstart', 'mousemove', 'keydown'];
    const handleAction = () => resetInactivity();
    handlers.forEach(event => window.addEventListener(event, handleAction));
    
    // Load fresh data if missing
    void store.ladeProdukte();
    void store.ladeVerkaeufe();
    
    return () => {
      if (timerRef.current) clearTimeout(timerRef.current);
      handlers.forEach(event => window.removeEventListener(event, handleAction));
    };
  }, []);

  const players = Object.keys(store.kredite)
    .map(Number)
    .map(id => {
      const p = store.portalSpieler.find(s => s.id === id) || store.spieler.find(s => s.id === id);
      return p ? { id, name: p.name, portalAktiv: 'portalAktiv' in p ? p.portalAktiv : true } : null;
    })
    .filter((p): p is { id: number; name: string; portalAktiv: boolean | undefined } => p !== null && p.portalAktiv !== false)
    .sort((a, b) => a.name.localeCompare(b.name));

  const foodDrinks = store.produkte.filter(p => p.active && (p.category === 'FOOD' || p.category === 'DRINK'));
  const selectedPurchaseHistory = selectedPlayerId === null ? [] : [
    ...store.serverVerkaeufe
      .filter(row => row.spielerId === selectedPlayerId)
      .map(row => ({
        productId: row.productId,
        name: row.productName ?? store.produkte.find(product => product.id === row.productId)?.name ?? `Produkt #${row.productId}`,
        quantity: row.quantity,
        totalCents: row.totalCents,
        pending: false,
      })),
    ...store.pendingVerkaeufe
      .filter(event => event.spielerId === selectedPlayerId && event.datum === store.verkaufDatum)
      .map(event => {
        const product = store.produkte.find(candidate => candidate.id === event.productId);
        return {
          productId: event.productId,
          name: product?.name ?? `Produkt #${event.productId}`,
          quantity: event.quantity,
          totalCents: event.quantity * (product?.currentPrice?.unitPriceCents ?? 0),
          pending: true,
        };
      }),
  ].filter(item => item.quantity !== 0);

  // Reset if player becomes stale
  useEffect(() => {
    if (selectedPlayerId !== null && !players.find(p => p.id === selectedPlayerId)) {
      setSelectedPlayerId(null);
      setCart({});
      setShowConfirm(false);
      setErrorMessage(null);
    }
  }, [players, selectedPlayerId]);

  // Reset if cart contains stale products
  useEffect(() => {
    const invalidProduct = Object.keys(cart).some(pId => {
      const p = foodDrinks.find(prod => prod.id === Number(pId));
      return !p;
    });
    if (invalidProduct) {
      setCart({});
      setShowConfirm(false);
      setErrorMessage('E Produkt an der Bestellung ass net méi disponibel');
    }
  }, [foodDrinks, cart]);

  const totalCents = Object.entries(cart).reduce((sum, [pId, qty]) => {
    const p = foodDrinks.find(prod => prod.id === Number(pId));
    if (!p || !p.currentPrice) return sum;
    return sum + (p.currentPrice.unitPriceCents * qty);
  }, 0);

  const totalStr = (totalCents / 100).toFixed(2);

  const handleSelectPlayer = (id: number) => {
    setSelectedPlayerId(id);
    setCart({});
    setShowConfirm(false);
  };

  const handleAdjustCart = (pId: number, delta: number) => {
    setCart(prev => {
      const current = prev[pId] || 0;
      const next = Math.max(0, current + delta);
      if (next === 0) {
        const copy = { ...prev };
        delete copy[pId];
        return copy;
      }
      return { ...prev, [pId]: next };
    });
  };

  const handleConfirm = () => {
    if (Object.keys(cart).length === 0 || !selectedPlayerId || isConfirming) return;
    setIsConfirming(true);
    setErrorMessage(null);
    
    const result = store.queueCateringBasket(selectedPlayerId, cart);
    if (!result.success) {
      setErrorMessage(result.error || 'Fehler beim Späicheren');
      setIsConfirming(false);
      return;
    }
    
    // Process order
    setSelectedPlayerId(null);
    setCart({});
    setShowConfirm(false);
    setIsConfirming(false);
  };

  const attemptExit = async () => {
    const isOk = await store.verifyKioskPin(pin);
    if (isOk) {
      store.setKioskMode('GAME');
    } else {
      setPinError(true);
      setPin('');
    }
  };

  const isLockedOut = !!store.kioskLockoutUntil && Date.now() < store.kioskLockoutUntil;
  const lockoutSeconds = store.kioskLockoutUntil ? Math.max(0, Math.ceil((store.kioskLockoutUntil - Date.now()) / 1000)) : 0;

  // force re-render for countdown
  useEffect(() => {
    if (isLockedOut) {
      const interval = setInterval(() => {
        setPinError(true); // hacky way to force re-render
        if (store.kioskLockoutUntil && Date.now() >= store.kioskLockoutUntil) {
          clearInterval(interval);
        }
      }, 1000);
      return () => clearInterval(interval);
    }
    return undefined;
  }, [isLockedOut, store.kioskLockoutUntil]);

  if (showExit) {
    return (
      <div className="flex h-full w-full bg-background items-center justify-center relative p-8">
        <div className="absolute inset-0 bg-background/90 backdrop-blur-sm"></div>
        <div className="relative bg-card border-2 border-border p-8 rounded-2xl w-full max-w-md shadow-2xl flex flex-col items-center gap-6">
          <Lock className="w-12 h-12 text-primary" />
          <h2 className="text-xl font-bold tracking-widest text-center">EXIT CATERING MODE</h2>
          <div className="flex gap-2">
            {[...Array(4)].map((_, i) => (
              <div key={i} className={cn(
                "w-12 h-12 rounded-xl border-2 flex items-center justify-center text-xl font-bold",
                pin.length > i ? "border-primary text-primary" : "border-border text-transparent"
              )}>
                {pin.length > i ? '●' : ''}
              </div>
            ))}
          </div>
          {isLockedOut ? (
            <div className="text-destructive font-bold text-sm">Locked for {lockoutSeconds}s</div>
          ) : (
            pinError && <div className="text-destructive font-bold text-sm">Wrong PIN</div>
          )}
          <div className="grid grid-cols-3 gap-2 w-full mt-4">
            {[1,2,3,4,5,6,7,8,9].map(n => (
              <button 
                key={n} 
                className="h-16 bg-background border-2 border-border rounded-xl text-xl font-black active:scale-95 transition-transform disabled:opacity-50"
                onClick={() => setPin(prev => prev.length < 4 ? prev + n : prev)}
                disabled={isLockedOut}
              >
                {n}
              </button>
            ))}
            <button className="h-16 text-muted-foreground font-bold" onClick={() => setShowExit(false)}>
              CANCEL
            </button>
            <button 
              className="h-16 bg-background border-2 border-border rounded-xl text-xl font-black active:scale-95 transition-transform disabled:opacity-50"
              onClick={() => setPin(prev => prev.length < 4 ? prev + '0' : prev)}
              disabled={isLockedOut}
            >
              0
            </button>
            <button 
              className="h-16 text-destructive font-bold disabled:opacity-50"
              onClick={() => setPin(prev => prev.slice(0, -1))}
              disabled={isLockedOut}
            >
              DEL
            </button>
          </div>
          <TouchButton 
            variant="primary" 
            size="lg" 
            className="w-full mt-2 h-14"
            disabled={pin.length < 4 || isLockedOut}
            onClick={attemptExit}
          >
            UNLOCK
          </TouchButton>
        </div>
      </div>
    );
  }

  return (
    <div className="flex h-full w-full bg-background flex-col" data-testid="catering-screen">
      <header className="h-20 border-b-2 border-border flex items-center px-8 bg-card gap-4 justify-between shrink-0">
        <div className="flex items-center gap-4">
          <Coffee className="w-8 h-8 text-primary" />
          <h1 className="text-2xl font-bold tracking-wider text-primary">BAR / CATERING</h1>
        </div>
        <button 
          data-testid="button-exit-kiosk"
          onClick={() => setShowExit(true)}
          className="text-muted-foreground/30 hover:text-muted-foreground active:scale-95 p-4 transition-all"
        >
          <Lock className="w-6 h-6" />
        </button>
      </header>

      <div className="flex flex-1 overflow-hidden">
        {/* Left: Players Grid */}
        <div className={cn(
          "flex-col border-r-2 border-border p-6 transition-all duration-300",
          selectedPlayerId ? "w-1/3 opacity-50 grayscale pointer-events-none" : "w-full"
        )}>
          <h2 className="text-xl font-bold tracking-widest text-foreground/80 mb-6">Wien bezillt?</h2>
          {players.length === 0 ? (
            <div className="text-center text-muted-foreground italic mt-20 text-lg">
              Keng Spiller vum Dag ugemellt
            </div>
          ) : (
            <div className="grid grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-4 overflow-y-auto pr-2 pb-10">
              {players.map(p => (
                <button
                  key={p.id}
                  data-testid={`player-card-${p.id}`}
                  onClick={() => handleSelectPlayer(p.id)}
                  className="h-24 bg-card border-2 border-border rounded-xl font-bold text-lg p-4 text-left hover:border-primary/50 hover:bg-primary/5 active:scale-95 transition-all flex flex-col justify-between"
                >
                  <span className="truncate w-full block">{p.name}</span>
                </button>
              ))}
            </div>
          )}
        </div>

        {/* Right: Order taking */}
        {selectedPlayerId && (
          <div className="flex-1 flex flex-col bg-background/50">
            {showConfirm ? (
              <div className="flex-1 p-8 flex flex-col gap-6 overflow-y-auto" data-testid="confirmation-view">
                <div className="bg-card border-2 border-primary/50 rounded-2xl p-8 flex flex-col flex-1 shadow-2xl">
                  <h2 className="text-2xl font-bold text-center mb-2">Bestätegung</h2>
                  <p className="text-center text-muted-foreground mb-8 text-lg">
                    Fir <span className="text-foreground font-black">{players.find(p => p.id === selectedPlayerId)?.name}</span>
                  </p>
                  
                  <div className="flex-1 flex flex-col gap-3 overflow-y-auto max-w-lg mx-auto w-full">
                    {Object.entries(cart).map(([pId, qty]) => {
                      const p = foodDrinks.find(prod => prod.id === Number(pId));
                      if (!p || qty === 0) return null;
                      return (
                        <div key={pId} className="flex justify-between items-center text-xl py-2 border-b border-border/50">
                          <span>{qty}x {p.name}</span>
                          <span className="font-mono font-bold">{(qty * (p.currentPrice?.unitPriceCents || 0) / 100).toFixed(2)} €</span>
                        </div>
                      );
                    })}
                  </div>
                  
                  <div className="mt-8 border-t-2 border-border pt-6 flex justify-between items-center max-w-lg mx-auto w-full">
                    <span className="text-2xl font-bold">Total</span>
                    <span className="text-4xl font-black text-primary font-mono" data-testid="total-price">{totalStr} €</span>
                  </div>
                  
                  {errorMessage && (
                    <div className="mt-4 max-w-lg mx-auto w-full bg-destructive/10 border-2 border-destructive/50 text-destructive text-center font-bold p-3 rounded-xl">
                      {errorMessage}
                    </div>
                  )}
                  
                  <div className="flex gap-4 mt-10">
                    <TouchButton 
                      size="xl" 
                      variant="outline" 
                      className="flex-1 h-20 text-xl font-bold"
                      onClick={() => setShowConfirm(false)}
                      disabled={isConfirming}
                      data-testid="button-cancel-confirm"
                    >
                      <X className="w-8 h-8 mr-2" /> Zréck
                    </TouchButton>
                    <TouchButton 
                      size="xl" 
                      variant="primary" 
                      className="flex-1 h-20 text-xl font-bold"
                      onClick={handleConfirm}
                      disabled={isConfirming}
                      data-testid="button-confirm-order"
                    >
                      <CheckCircle2 className="w-8 h-8 mr-2" /> {isConfirming ? 'Späicheren...' : 'Confirméieren'}
                    </TouchButton>
                  </div>
                </div>
              </div>
            ) : (
              <>
                <div className="p-6 border-b-2 border-border bg-card shrink-0 flex items-center justify-between">
                  <div>
                    <h2 className="text-xl font-bold text-foreground">
                      Bestellung: <span className="text-primary">{players.find(p => p.id === selectedPlayerId)?.name}</span>
                    </h2>
                    <p className="text-sm text-muted-foreground mt-1">Gedrénks an Iessen dobäisetzen</p>
                    <div className="mt-3 text-xs text-muted-foreground" data-testid="today-purchase-history">
                      <span className="font-bold uppercase tracking-wider">Haut kaf:</span>{' '}
                      {selectedPurchaseHistory.length === 0 ? (
                        <span>nach näischt</span>
                      ) : selectedPurchaseHistory.map((item, index) => (
                        <span key={`${item.productId}-${index}`} className="mr-2">
                          {item.quantity}× {item.name} ({(item.totalCents / 100).toFixed(2)} €)
                          {item.pending && <em className="not-italic text-amber-400 font-bold"> · pending</em>}
                        </span>
                      ))}
                    </div>
                  </div>
                  <button 
                    onClick={() => {
                      setSelectedPlayerId(null);
                      setCart({});
                    }}
                    className="p-3 border-2 border-border rounded-xl hover:bg-white/5 active:scale-95 transition-transform"
                    data-testid="button-cancel-order"
                  >
                    <X className="w-6 h-6" />
                  </button>
                </div>
                
                <div className="flex-1 overflow-y-auto p-6">
                  {foodDrinks.length === 0 ? (
                    <div className="text-center text-muted-foreground italic mt-20">Keng Produkter disponibel</div>
                  ) : (
                    <div className="grid grid-cols-2 gap-4">
                      {foodDrinks.map(p => {
                        const qty = cart[p.id] || 0;
                        const price = p.currentPrice ? (p.currentPrice.unitPriceCents / 100).toFixed(2) : '0.00';
                        return (
                          <div key={p.id} className="bg-card border-2 border-border rounded-xl p-4 flex flex-col justify-between min-h-[140px]" data-testid={`product-card-${p.id}`}>
                            <div className="flex justify-between items-start mb-4">
                              <span className="font-bold text-lg pr-2 leading-tight">{p.name}</span>
                              <span className="font-mono text-muted-foreground whitespace-nowrap">{price} €</span>
                            </div>
                            
                            {qty === 0 ? (
                              <TouchButton 
                                variant="outline" 
                                className="w-full h-14 font-bold border-primary/40 text-primary hover:bg-primary/10"
                                onClick={() => handleAdjustCart(p.id, 1)}
                                data-testid={`button-add-${p.id}`}
                              >
                                Dobäisetzen
                              </TouchButton>
                            ) : (
                              <div className="flex items-center gap-2 h-14">
                                <TouchButton 
                                  variant="outline" 
                                  className="flex-1 h-full text-xl" 
                                  onClick={() => handleAdjustCart(p.id, -1)}
                                  data-testid={`button-minus-${p.id}`}
                                >
                                  −
                                </TouchButton>
                                <div className="w-16 text-center text-2xl font-black font-mono">{qty}</div>
                                <TouchButton 
                                  variant="primary" 
                                  className="flex-1 h-full text-xl" 
                                  onClick={() => handleAdjustCart(p.id, 1)}
                                  data-testid={`button-plus-${p.id}`}
                                >
                                  +
                                </TouchButton>
                              </div>
                            )}
                          </div>
                        );
                      })}
                    </div>
                  )}
                </div>
                
                <div className="p-6 border-t-2 border-border bg-card shrink-0 flex items-center justify-between">
                  <div className="flex flex-col">
                    <span className="text-sm font-bold text-muted-foreground uppercase tracking-widest">Total</span>
                    <span className="text-3xl font-black text-primary font-mono">{totalStr} €</span>
                  </div>
                  <TouchButton 
                    variant="primary" 
                    size="xl" 
                    className="px-10 h-16 text-xl font-bold"
                    disabled={Object.keys(cart).length === 0}
                    onClick={() => setShowConfirm(true)}
                    data-testid="button-review-order"
                  >
                    Weider
                  </TouchButton>
                </div>
              </>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

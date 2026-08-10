import { createContext, useCallback, useContext, useEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import { Delete, ChevronUp, Check } from "lucide-react";
import { cn } from "@/lib/utils";

// ─── Types ────────────────────────────────────────────────────────────────────

interface VkSession {
  getValue: () => string;
  setValue: (v: string) => void;
}

interface VkContextValue {
  open: (session: VkSession) => void;
  close: () => void;
  isOpen: boolean;
}

// ─── Context ─────────────────────────────────────────────────────────────────

const VkContext = createContext<VkContextValue>({
  open: () => {},
  close: () => {},
  isOpen: false,
});

export function useVirtualKeyboard() {
  return useContext(VkContext);
}

// ─── Layout ───────────────────────────────────────────────────────────────────

const ROWS = [
  ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
  ["Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P"],
  ["A", "S", "D", "F", "G", "H", "J", "K", "L", "@"],
  ["Y", "X", "C", "V", "B", "N", "M", ".", "_", "-"],
];

// ─── Key button ───────────────────────────────────────────────────────────────

function Key({
  label,
  onPress,
  wide,
  accent,
  danger,
  icon,
}: {
  label?: string;
  onPress: () => void;
  wide?: boolean;
  accent?: boolean;
  danger?: boolean;
  icon?: React.ReactNode;
}) {
  return (
    <button
      type="button"
      onPointerDown={(e) => {
        e.preventDefault(); // don't steal focus from the real input
        onPress();
      }}
      className={cn(
        "flex items-center justify-center rounded-lg text-sm font-bold select-none transition-colors active:scale-95",
        "h-12 min-w-0",
        wide ? "flex-1" : "flex-1 max-w-[2.75rem]",
        accent
          ? "bg-primary text-primary-foreground hover:bg-primary/90"
          : danger
          ? "bg-destructive/80 text-destructive-foreground hover:bg-destructive"
          : "bg-secondary/70 text-foreground hover:bg-secondary border border-border/40",
      )}
    >
      {icon ?? label}
    </button>
  );
}

// ─── Keyboard panel ───────────────────────────────────────────────────────────

function KeyboardPanel({
  session,
  onClose,
}: {
  session: VkSession;
  onClose: () => void;
}) {
  const [shift, setShift] = useState(true); // start with shift on (caps for first letter)
  const [caps, setCaps] = useState(false);

  const insert = (char: string) => {
    const cur = session.getValue();
    session.setValue(cur + char);
    // auto-drop shift after one char (unless caps-lock is on)
    if (shift && !caps) setShift(false);
  };

  const backspace = () => {
    const cur = session.getValue();
    session.setValue(cur.slice(0, -1));
  };

  const handleShift = () => {
    if (shift && !caps) {
      // shift was on (not caps) → turn on caps lock
      setCaps(true);
    } else if (caps) {
      // caps on → turn everything off
      setCaps(false);
      setShift(false);
    } else {
      // both off → turn shift on
      setShift(true);
    }
  };

  const upper = shift || caps;

  return (
    <div
      className={cn(
        "fixed bottom-0 left-0 right-0 z-[9999]",
        "bg-card border-t border-border/60 shadow-2xl",
        "px-2 pt-3 pb-safe-or-3",
        "animate-in slide-in-from-bottom duration-200",
      )}
      style={{ paddingBottom: "max(0.75rem, env(safe-area-inset-bottom))" }}
    >
      {/* Close strip */}
      <div className="flex justify-end mb-2 px-1">
        <button
          type="button"
          onPointerDown={(e) => { e.preventDefault(); onClose(); }}
          className="text-xs font-bold text-muted-foreground hover:text-foreground flex items-center gap-1 px-3 py-1 rounded-lg hover:bg-secondary/60 transition-colors"
        >
          Zoumaachen ✕
        </button>
      </div>

      <div className="space-y-1.5 max-w-2xl mx-auto">
        {/* Letter / number rows */}
        {ROWS.map((row, ri) => (
          <div key={ri} className="flex gap-1 justify-center">
            {ri === 3 && (
              <Key
                onPress={handleShift}
                icon={
                  <span className={cn("flex items-center gap-0.5", caps ? "text-primary" : shift ? "text-primary/70" : "")}>
                    <ChevronUp size={16} strokeWidth={3} />
                    {caps && <ChevronUp size={16} strokeWidth={3} className="-ml-2" />}
                  </span>
                }
              />
            )}
            {row.map((k) => (
              <Key
                key={k}
                label={upper && k.match(/[A-Z]/) ? k : k.toLowerCase()}
                onPress={() => insert(upper && k.match(/[A-Z]/) ? k : k.toLowerCase())}
              />
            ))}
            {ri === 3 && (
              <Key
                onPress={backspace}
                danger
                icon={<Delete size={16} />}
              />
            )}
          </div>
        ))}

        {/* Bottom row: space + done */}
        <div className="flex gap-1 justify-center pt-0.5">
          <Key wide label="Leerzeichen" onPress={() => insert(" ")} />
          <Key
            accent
            onPress={onClose}
            icon={
              <span className="flex items-center gap-1.5">
                <Check size={15} strokeWidth={3} /> Fertig
              </span>
            }
          />
        </div>
      </div>
    </div>
  );
}

// ─── Provider ─────────────────────────────────────────────────────────────────

export function VirtualKeyboardProvider({ children }: { children: React.ReactNode }) {
  const [session, setSession] = useState<VkSession | null>(null);
  const closeTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const open = useCallback((s: VkSession) => {
    if (closeTimerRef.current) clearTimeout(closeTimerRef.current);
    setSession(s);
  }, []);

  const close = useCallback(() => {
    setSession(null);
  }, []);

  // Close on Escape
  useEffect(() => {
    if (!session) return;
    const handler = (e: KeyboardEvent) => { if (e.key === "Escape") close(); };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [session, close]);

  return (
    <VkContext.Provider value={{ open, close, isOpen: !!session }}>
      {children}
      {session &&
        createPortal(
          <KeyboardPanel session={session} onClose={close} />,
          document.body,
        )}
    </VkContext.Provider>
  );
}

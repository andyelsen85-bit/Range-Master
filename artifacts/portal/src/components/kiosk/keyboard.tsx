import { Delete, Space, X } from "lucide-react";
import { cn } from "@/lib/utils";

const ROWS = [
  ["Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "Ü"],
  ["A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä"],
  ["Y", "X", "C", "V", "B", "N", "M", "-", "'", "."],
  ["É", "È", "Ë", "Ê", "À", "Â", "Ç", "Ï", "Ô", "ß"],
];

interface Props {
  value: string;
  onChange: (v: string) => void;
  className?: string;
}

/** On-screen keyboard for the touch PC (letters, accents/umlauts, space, backspace). */
export function KioskKeyboard({ value, onChange, className }: Props) {
  const type = (ch: string) => {
    const lowerAfter = value.length > 0 && !value.endsWith(" ") && !value.endsWith("-");
    onChange(value + (lowerAfter && ch !== "ß" ? ch.toLocaleLowerCase("de") : ch));
  };
  return (
    <div className={cn("space-y-1.5 select-none", className)} data-testid="kiosk-keyboard">
      {ROWS.map((row, ri) => (
        <div key={ri} className="flex gap-1.5 justify-center">
          {row.map((k) => (
            <button key={k} type="button" onClick={() => type(k)} data-testid={`key-${k}`}
              className={cn(
                "kiosk-key h-14 flex-1 max-w-[4.25rem] rounded-lg text-2xl font-bold",
                ri === 3 && "text-primary",
              )}>
              {k}
            </button>
          ))}
        </div>
      ))}
      <div className="flex gap-1.5">
        <button type="button" onClick={() => onChange("")} data-testid="key-clear"
          className="kiosk-key h-14 px-5 rounded-lg text-lg font-bold flex items-center gap-2 text-muted-foreground">
          <X size={22} /> Leeren
        </button>
        <button type="button" onClick={() => onChange(value + " ")} data-testid="key-space"
          className="kiosk-key h-14 flex-1 rounded-lg text-lg font-bold flex items-center justify-center gap-2 text-muted-foreground">
          <Space size={24} /> Leerzeichen
        </button>
        <button type="button" onClick={() => onChange(value.slice(0, -1))} data-testid="key-backspace" aria-label="Löschen"
          className="kiosk-key h-14 px-6 rounded-lg flex items-center justify-center">
          <Delete size={26} />
        </button>
      </div>
    </div>
  );
}

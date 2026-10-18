// @vitest-environment jsdom
/**
 * Kiosk refresh tests (spec 2.2 / 8.4).
 * Run: pnpm --filter @workspace/portal exec vitest run
 * (vitest config needs the "@" alias -> ./src, e.g. reuse vite.config.ts.)
 */
import { describe, it, expect, vi, beforeEach } from "vitest";
import { act } from "react";
import { createRoot, type Root } from "react-dom/client";

// Minimal DOM helpers (@testing-library/dom peer is not installed).
(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
let root: Root | null = null;
let container: HTMLElement;
function render(ui: React.ReactNode) {
  root?.unmount();
  container = document.createElement("div");
  document.body.innerHTML = "";
  document.body.appendChild(container);
  root = createRoot(container);
  act(() => { root!.render(ui); });
  return { rerender: (next: React.ReactNode) => root!.render(next) };
}
const screen = {
  getByTestId(id: string): HTMLElement {
    const el = container.querySelector(`[data-testid="${id}"]`);
    if (!el) throw new Error(`testid not found: ${id}`);
    return el as HTMLElement;
  },
};
const fireEvent = { click: (el: HTMLElement) => act(() => { el.click(); }) };
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";

// Mutable fake server state returned by the mocked generated hooks.
const { state, noopMutation } = vi.hoisted(() => ({
  state: { queue: {} as Record<string, unknown>, players: {} as Record<string, unknown> },
  noopMutation: () => ({ mutate: () => {}, isPending: false }),
}));

vi.mock("@workspace/api-client-react", () => ({
  useGetKioskQueue: () => ({ data: state.queue, isLoading: false, isSuccess: true, error: null }),
  getGetKioskQueueQueryKey: () => ["/api/public/queue"],
  useGetKioskPlayers: () => ({ data: state.players, isLoading: false, isSuccess: true, isFetching: false }),
  getGetKioskPlayersQueryKey: (p: unknown) => ["/api/public/queue/players", p],
  useGetKioskGroups: () => ({ data: { groups: [] }, isLoading: false, isSuccess: true }),
  getGetKioskGroupsQueryKey: () => ["/api/public/groups"],
  useGetKioskPrintDays: () => ({ data: { days: [] }, isLoading: false, isSuccess: true }),
  getGetKioskPrintDaysQueryKey: (id: number) => ["/api/public/player", id, "days"],
  useKioskSignup: noopMutation, useKioskCancel: noopMutation,
  useCreateKioskGroup: noopMutation, useUpdateKioskGroup: noopMutation,
  useDeleteKioskGroup: noopMutation, useKioskGroupSignup: noopMutation,
  useGetKioskLiveGame: () => ({ data: null }),
  getGetKioskLiveGameQueryKey: () => ["/api/public/live-game"],
}));

import { QueuePanel } from "../src/components/kiosk/queue-panel";
import { LiveGameTable } from "../src/components/kiosk/live-game-panel";
import type { LiveGame } from "../src/components/kiosk/types";

const settings = { queueEnabled: true, groupsEnabled: true, showRanking: false, maxQueueGames: 6, enabledModes: ["NORMAL"] };

function setServer(version: string, extraPlayer: boolean) {
  state.queue = {
    version,
    settings,
    games: [{ queueGameId: 1, position: 1, modus: "NORMAL", status: "OPEN", free: extraPlayer ? 4 : 5,
      players: [{ entryId: 10, spielerId: 9, name: "Paul K.", groupId: null },
        ...(extraPlayer ? [{ entryId: 11, spielerId: 8, name: "Lena S.", groupId: null }] : [])] }],
  };
  state.players = {
    players: [
      { id: 1, displayName: "Marc W.", activeSignup: false, running: false },
      { id: 2, displayName: "Marie T.", activeSignup: false, running: false },
      ...(extraPlayer ? [{ id: 3, displayName: "Mara L.", activeSignup: false, running: false }] : []),
    ],
  };
}

function wrap(ui: React.ReactNode) {
  const qc = new QueryClient();
  return <QueryClientProvider client={qc}>{ui}</QueryClientProvider>;
}

describe("QueuePanel refetch", () => {
  beforeEach(() => setServer("v1", false));

  it("keeps selection, search text and the same DOM node across a refetch", () => {
    const { rerender } = render(wrap(<QueuePanel enabled resetSignal={0} />));
    fireEvent.click(screen.getByTestId("button-kiosk-start-single"));
    fireEvent.click(screen.getByTestId("key-M"));
    fireEvent.click(screen.getByTestId("key-A"));
    fireEvent.click(screen.getByTestId("button-player-2"));
    const before = screen.getByTestId("button-player-2");
    expect(before.getAttribute("aria-pressed")).toBe("true");

    // Simulated refetch: changed queue + changed results list.
    setServer("v2", true);
    act(() => { rerender(wrap(<QueuePanel enabled resetSignal={0} />)); });

    const after = screen.getByTestId("button-player-2");
    expect(after).toBe(before);
    expect(after.getAttribute("aria-pressed")).toBe("true");
    expect(screen.getByTestId("text-kiosk-search").textContent).toBe("Ma");
    expect(screen.getByTestId("text-kiosk-selected").textContent).toBe("Marie T.");
  });

  it("shows an inline note instead of closing when the selected player disappears", () => {
    const { rerender } = render(wrap(<QueuePanel enabled resetSignal={0} />));
    fireEvent.click(screen.getByTestId("button-kiosk-start-single"));
    fireEvent.click(screen.getByTestId("button-player-1"));
    state.players = { players: [{ id: 2, displayName: "Marie T.", activeSignup: false, running: false }] };
    act(() => { rerender(wrap(<QueuePanel enabled resetSignal={0} />)); });
    expect(screen.getByTestId("panel-kiosk-selected")).toBeTruthy();
    expect(screen.getByTestId("text-kiosk-inline").textContent).toContain("nicht mehr verfügbar");
  });

  it("idle reset signal returns to the start state", () => {
    const { rerender } = render(wrap(<QueuePanel enabled resetSignal={0} />));
    fireEvent.click(screen.getByTestId("button-kiosk-start-single"));
    fireEvent.click(screen.getByTestId("button-player-1"));
    act(() => { rerender(wrap(<QueuePanel enabled resetSignal={1} />)); });
    expect(screen.getByTestId("view-kiosk-start")).toBeTruthy();
  });
});

describe("LiveGameTable", () => {
  const base: LiveGame = {
    terminalId: "T1", seq: 1, state: "RUNNING", spielExternalId: "x", modus: "NORMAL", lauf: 1, taubenProLauf: 3,
    startedAt: null, currentSpielerId: 1, currentTaube: 2, updatedAt: new Date().toISOString(),
    players: [{ spielerId: 1, name: "Marc W.", posten: 1 }, { spielerId: 2, name: "Marie T.", posten: 2 }],
    results: [{ spielerId: 1, lauf: 1, taube: 1, maschine: "A", schuss1: true, schuss2: false, punkte: 2 }],
  };

  it("updates a single cell without remounting the others, and keeps queue selection", () => {
    setServer("v1", false);
    const ui = (g: LiveGame) => wrap(<><LiveGameTable game={g} /><QueuePanel enabled resetSignal={0} /></>);
    const { rerender } = render(ui(base));
    fireEvent.click(screen.getByTestId("button-kiosk-start-single"));
    fireEvent.click(screen.getByTestId("button-player-2"));
    const selectedNode = screen.getByTestId("button-player-2");
    const fired = screen.getByTestId("cell-live-1-1-1");
    const empty = screen.getByTestId("cell-live-1-1-2");
    const otherRow = screen.getByTestId("row-live-2");
    expect(empty.textContent).toBe("");

    const next: LiveGame = { ...base, seq: 2, currentTaube: 3,
      results: [...base.results, { spielerId: 1, lauf: 1, taube: 2, maschine: "B", schuss1: false, schuss2: true, punkte: 1 }] };
    act(() => { rerender(ui(next)); });

    expect(screen.getByTestId("cell-live-1-1-1")).toBe(fired);
    expect(screen.getByTestId("cell-live-1-1-2")).toBe(empty);
    expect(empty.textContent).toContain("1");
    expect(screen.getByTestId("row-live-2")).toBe(otherRow);
    expect(screen.getByTestId("text-live-total-1").textContent).toBe("3");
    expect(screen.getByTestId("button-player-2")).toBe(selectedNode);
    expect(selectedNode.getAttribute("aria-pressed")).toBe("true");
  });
});

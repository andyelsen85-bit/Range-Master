import { keepPreviousData } from "@tanstack/react-query";
import { useGetKioskQueue, getGetKioskQueueQueryKey } from "@workspace/api-client-react";

export type QueuePlayer = import("@workspace/api-client-react").KioskQueuePlayer;
export type QueueGame = import("@workspace/api-client-react").KioskQueueGame;
export type QueueSettings = import("@workspace/api-client-react").KioskQueueSettings;
export type QueueData = import("@workspace/api-client-react").KioskQueueSnapshot;
export type PickerPlayer = import("@workspace/api-client-react").KioskPickerPlayer;
export type KioskGroup = import("@workspace/api-client-react").KioskGroup;
export type LivePlayer = import("@workspace/api-client-react").KioskLivePlayer;
export type LiveResult = import("@workspace/api-client-react").KioskLiveResult;
export type LiveGame = import("@workspace/api-client-react").KioskLiveGame;
export type TaubeStat = import("@workspace/api-client-react").SpielerStatistikTauben[string];
export type DayStats = Omit<import("@workspace/api-client-react").SpielerStatistik,"spielerId">;

export const MAX_PLAYERS = 6;

export const DEFAULT_SETTINGS: QueueSettings = {
  queueEnabled: false, groupsEnabled: true, showRanking: false, maxQueueGames: 6, enabledModes: ["NORMAL"],
};

export function asQueue(d: unknown): QueueData | undefined {
  if (!d || typeof d !== "object") return undefined;
  const q = d as Partial<QueueData>;
  return {
    version: String(q.version ?? ""),
    games: Array.isArray(q.games) ? q.games : [],
    settings: { ...DEFAULT_SETTINGS, ...(q.settings ?? {}) },
  };
}

/** Shared queue query (deduplicated by react-query between page and panel). */
export function useKioskQueue(enabled: boolean) {
  return useGetKioskQueue({
    query: {
      queryKey: getGetKioskQueueQueryKey(),
      enabled,
      refetchInterval: 5_000,
      refetchIntervalInBackground: true,
      placeholderData: keepPreviousData,
      retry: 1,
    },
  });
}

export function parseDays(d: unknown): string[] {
  const days = (d as { days?: string[] } | undefined)?.days;
  return Array.isArray(days) ? days : [];
}

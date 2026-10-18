#pragma once
#include <stddef.h>
#include <stdint.h>
struct DayPlayerEvent {
    char externalId[40],datum[11],occurredAt[25];
    int spielerId;
    uint8_t add,state; // 0 prepared, 1 ready, 2 rejected (retained, no automatic retry)
};
constexpr int DAY_PLAYER_EVENTS_MAX=128;
void day_players_init();
bool day_players_prepare(int player,bool add);
void day_players_finish(bool committed);
int day_players_snapshot(DayPlayerEvent *out,int capacity);
bool day_players_ack(const char *id,bool rejected=false);
void day_players_remap(int oldId,int newId);
bool day_players_flush();
int day_players_pending();
bool day_players_repair_safe();
bool day_players_has_event(int player,const char *date);
bool day_players_retry_rejected();

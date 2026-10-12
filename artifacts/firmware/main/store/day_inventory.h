#pragma once
#include "nvs.h"
#include "cJSON.h"

enum DayEventKind : uint8_t { DAY_MAITRES=1, DAY_OPENING=2, DAY_CLOSING=3 };
struct DayEvent {
    char externalId[40], datum[11];
    DayEventKind kind;
    bool takenOver;
    int c12,c20,maitreIds[4],maitreCount;
    char note[241];
};
struct DayInventory {
    char datum[11], previousDate[11], openingAt[32], closingAt[32];
    char openingTerminal[65],closingTerminal[65];
    char closingNote[241];
    bool previousValid,fromOpening,hasOpening,hasClosing,takenOver;
    int previous12,previous20,opening12,opening20,closing12,closing20;
    int maitreIds[4],maitreCount;
    char backupDate[11];
};
void day_inventory_init(nvs_handle_t handle);
void day_inventory_snapshot(DayInventory *out);
bool day_inventory_queue(DayEventKind kind,int c12,int c20,const int *maitres,int count,bool takenOver,const char *note);
bool day_inventory_peek(DayEvent *out);
bool day_inventory_ack(const char *id);
bool day_inventory_apply(cJSON *root,const char *requestedDate=nullptr);
bool day_inventory_persist();
bool day_inventory_mark_backup(const char *date);
bool day_inventory_closed_today();
bool day_inventory_today(char *out,size_t capacity);

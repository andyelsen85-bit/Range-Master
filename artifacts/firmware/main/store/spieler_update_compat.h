#pragma once
#include "game_store.h"
#include <stddef.h>
#include <string.h>
// Released NVS counted blobs contain these exact rows without portalId.
// Never discard their CREATE externalIds when upgrading.
struct LegacySpielerUpdateEntry {
    bool used;
    int spielerId;
    SpielerUpdateTyp typ;
    char externalId[36],name[MAX_NAME_LEN],email[MAX_EMAIL_LEN];
    bool portalAktiv;
};
static_assert(offsetof(SpielerUpdateEntry,portalId)==sizeof(LegacySpielerUpdateEntry),
              "Legacy player-update prefix must remain stable");
inline void spieler_update_upgrade(const LegacySpielerUpdateEntry &old,SpielerUpdateEntry *out) {
    *out={};
    memcpy(out,&old,sizeof(old));
    out->portalId=0;
}

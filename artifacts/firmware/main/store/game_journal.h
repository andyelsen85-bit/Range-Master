#pragma once
#include "game_store.h"
bool game_journal_init();
bool game_journal_save(const FinishedGame *game);
bool game_journal_ack(const char *external_id);
int game_journal_recover(unsigned days);
bool game_journal_remap(int old_id,int new_id);
bool game_journal_ready();
int game_journal_manifest(const char *day,char (*ids)[40],int capacity);
int game_journal_reload_pending();

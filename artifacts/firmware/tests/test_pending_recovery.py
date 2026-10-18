#!/usr/bin/env python3
"""Exercise production recovery snapshots outside a sync-worker handshake."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",Path(__file__).with_name("test_machine_batch.py"))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
source=(ROOT/"main/net/http_sync.cpp").read_text()
functions="\n".join(fixture.function(source,s) for s in [
    "static bool pending_record(","static int pending_ids(","static bool remove_ram_pending("])
code=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "game_store.h"
#include "storage_budget.h"
GameStore g_store{};
static bool can_lock=true;static int saves=0;
bool ui_manager_snapshot_lock(bool *acquired){*acquired=can_lock;return can_lock;}
void ui_manager_unlock(){}
bool store_sync_commit_begin(){assert(false&&"recovery must not require sync/UI handshake");return false;}
bool game_journal_read(const char*,FinishedGame*){return false;}
int game_journal_pending_ids(char(*)[40],int){return -1;}
void game_store_save(){++saves;}
''' + functions + r'''
int main(){
    g_store.syncStatus=SYNC_IDLE;g_store.pendingGamesCount=1;
    strcpy(g_store.pendingGames[0].externalId,"local-unsent");
    char ids[STORAGE_JOURNAL_SLOTS][40];assert(pending_ids(ids)==1&&!strcmp(ids[0],"local-unsent"));
    FinishedGame game{};assert(pending_record("local-unsent",&game));
    can_lock=false;assert(pending_ids(ids)==-1);assert(!remove_ram_pending("local-unsent"));
    assert(g_store.pendingGamesCount==1&&saves==0);
    can_lock=true;assert(remove_ram_pending("local-unsent"));assert(g_store.pendingGamesCount==0&&saves==1);
    puts("PASS standalone recovery with SYNC_IDLE, RAM fallback, busy-UI refusal and no sync/UI handshake");
}
'''
with tempfile.TemporaryDirectory(prefix="rm-pending-recovery-") as tmp:
    path=Path(tmp);(path/"test.cpp").write_text(code)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT/"main/store"),
                    str(path/"test.cpp"),"-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)

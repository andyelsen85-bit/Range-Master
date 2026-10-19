#!/usr/bin/env python3
"""Host regression for terminal layout math, labels and local timestamps (Part A)."""
from pathlib import Path
import subprocess, tempfile, os

ROOT = Path(__file__).resolve().parents[1]
SRC = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "scorecard_layout.h"
#include "ui_time_fmt.h"
int main(){
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();
    char t[32];fmt_local_time("2026-10-19T17:53:32.000Z",t,sizeof(t));
    assert(!strcmp(t,"19.10.2026 19:53"));
    fmt_local_time("2026-01-05T08:07:00.000Z",t,sizeof(t));assert(!strcmp(t,"05.01.2026 09:07"));
    assert(!strchr(t,'T')&&!strchr(t,'Z'));
    // Ergebnisse table: 1248 wide, 598 high (screen - header - winner - pad).
    for(int players=1;players<=6;players++)for(int cols:{10,16,18,20,24}){
        for(int width:{1248,950,700}){
            auto l=scorecard_layout(width,598,players,cols);
            assert(l.name+l.total+cols*l.clay==width);          // full width, no scroll
            assert(l.clay>=l.font/2+4);                           // digit fits centred
            assert(l.font>=12&&l.font<=20);
        }
    }
    auto six=scorecard_layout(1248,598,6,18);assert(six.clay>=40);
    // TOTAL needs measured width + 2*2 px padding + border slack.
    assert(scorecard_total_needed(46,2)==52);
    assert(!strcmp(ui_sync_chip_text(UI_SYNC_ERROR,true),"SYNC FEHLER"));
    assert(!strcmp(ui_sync_chip_text(UI_SYNC_OK,false),"CACHE FEHLT"));
    assert(!strcmp(ui_sync_chip_text(UI_SYNC_OK,true),"SYNCHRONISIERT"));
    assert(!strcmp(ui_sync_chip_text(UI_SYNC_IDLE,true),"WARTET AUF SYNC"));
    char ls[64];ui_last_sync_text(t,ls,sizeof ls);assert(!strcmp(ls,"Letzter Sync 05.01.2026 09:07"));
    ui_last_sync_text("",ls,sizeof ls);assert(!strcmp(ls,"Letzter Sync noch nie"));
    // Spielmodus 4+3 grid, right column 752 px.
    for(int i=0;i<7;i++){auto c=ui_grid_cell(752,4,56,8,i);auto f=ui_grid_cell(752,4,56,8,0);
        assert(c.w==f.w&&c.h==56&&c.x+c.w<=752&&c.x>=0);assert(c.y==(i<4?0:64));}
    char b[64];
    ui_pending_actions_text(0,b,sizeof b);assert(!strcmp(b,"Offline-Warteschlange: 0 Aktionen"));
    ui_pending_actions_text(1,b,sizeof b);assert(!strcmp(b,"Offline-Warteschlange: 1 Aktion"));
    ui_pending_actions_text(7,b,sizeof b);assert(!strcmp(b,"Offline-Warteschlange: 7 Aktionen"));
    ui_queue_button_text(0,b,sizeof b);assert(!strcmp(b,"WARTESCHLANGE (0)"));
    puts("PASS ui layout: local time, equal scorecard cells fill width, measured TOTAL width, sync card lines, 4+3 mode grid, labels");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=os.path.join(d,"t.cpp");open(c,"w").write(SRC)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT/"main/ui"),c,"-o",d+"/t"],check=True)
    subprocess.run([d+"/t"],check=True)

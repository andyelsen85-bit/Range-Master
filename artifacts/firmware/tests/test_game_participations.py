#!/usr/bin/env python3
"""Real on-flash types + wire projection; no changes to the persisted ABI."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r"""
#include <cassert>
#include <cstdio>
#include <algorithm>
#include "game_participations.h"
#include "history_scorecard.h"
int main() {
    static_assert(sizeof(FinishedGame)==7052,"Do not break existing journals");
    for(int mode=0;mode<=6;mode++)for(int runs=1;runs<=2;runs++) {
        FinishedGame finished{};auto &g=finished.base;
        g.modus=(Modus)mode;g.lauf=runs;g.taubenProLauf=9;
        for(int p=1;p<=MAX_SPIELER;p++) {
            game_merge_participation(&g,p,5,18*runs,runs);
            for(int run=1;run<=runs;run++)for(int clay=1;clay<=9;clay++) {
                auto &e=g.ergebnisse[g.ergebnisse_count++];
                e.spielerId=p;e.lauf=run;e.taube=clay;e.punkte=2;e.schuss1=true;
                e.posten=((p-1)+(run-1)*9+clay-1)%5+1;e.maschine=MASCHINE_A;
            }
        }
        // Old uploaded participation had only the final run and the wrong total.
        for(int p=0;p<MAX_SPIELER;p++)g.teilnahmen[p].punkte=18;
        RoundParticipation rows[MAX_ROUND_PARTICIPATIONS]{};
        assert(game_round_participations(&g,rows)==MAX_SPIELER*runs);
        for(int p=0;p<MAX_SPIELER;p++)for(int run=1;run<=runs;run++) {
            const auto &r=rows[p*runs+run-1];
            assert(r.spielerId==p+1 && r.lauf==run && r.punkte==18);
            assert(r.startPosten==(p+(run-1)*9)%5+1);
        }
        if(mode==MODUS_NORMAL)assert(history_scorecard_total(&finished,1)==18*runs);
        std::reverse(g.ergebnisse,g.ergebnisse+g.ergebnisse_count);
        assert(game_round_participations(&g,rows)==MAX_SPIELER*runs);
        assert(rows[0].startPosten==1 && rows[0].punkte==18);
        // Fold all 12 portal rows into SIX aggregate slots, not the first six rows.
        PendingGame pulled{};pulled.lauf=runs;
        for(int i=MAX_SPIELER*runs-1;i>=0;i--)
            game_merge_participation(&pulled,rows[i].spielerId,rows[i].startPosten,rows[i].punkte,rows[i].lauf);
        assert(pulled.teilnahmen_count==MAX_SPIELER);
        for(int i=0;i<MAX_SPIELER;i++) {
            assert(pulled.teilnahmen[i].punkte==18*runs);
            assert(pulled.teilnahmen[i].lauf==1);
            assert(pulled.teilnahmen[i].startPosten==(pulled.teilnahmen[i].spielerId-1)%5+1);
        }
        // Repeated attempts contribute nothing, even in old records with nonzero points.
        auto repeat=g.ergebnisse[0];repeat.wiederholt=true;
        g.ergebnisse[g.ergebnisse_count++]=repeat;
        assert(game_round_participations(&g,rows)==MAX_SPIELER*runs);
        for(int i=0;i<MAX_SPIELER*runs;i++)assert(rows[i].punkte==18);
    }
    PendingGame legacy{};game_merge_participation(&legacy,42,3,36,2);
    RoundParticipation rows[MAX_ROUND_PARTICIPATIONS]{};
    assert(game_round_participations(&legacy,rows)==1 && rows[0].punkte==36);
    puts("PASS: six players x two runs, seven modes, rotated posts, repeats, unordered shots, 12-row download and unchanged 7052-byte FinishedGame");
}
"""
with tempfile.TemporaryDirectory(prefix="rm-rounds-") as directory:
    source=Path(directory)/"test.cpp";source.write_text(SOURCE)
    binary=Path(directory)/"test"
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
                    "-I",str(ROOT/"main/store"),"-I",str(ROOT/"main/ui"),
                    str(source),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

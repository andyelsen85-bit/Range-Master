#!/usr/bin/env python3
"""Compile actual sequence/scoring code with store types; never fire hardware."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("fixture", Path(__file__).with_name("test_machine_batch.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

STUBS = r"""
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include "game_store.h"
GameStore g_store = {};
static int finished = 0;
void game_store_save(void) {}
static void _store_finish_game(void) { ++finished; }
"""

MAIN = r"""
int main() {
    static_assert(MODUS_CUSTOM_1 == 2 && MODUS_CUSTOM_4 == 5 && MODUS_HARAKIRI_CUSTOM == 6,
                  "Existing persistent mode IDs must not move");
    bool enabled[MASCHINE_COUNT]; for (bool &value : enabled) value = true;
    CustomSequenzEintrag pair{MASCHINE_A, MASCHINE_F, true, 1250};
    SequenzEintrag sequence[MAX_SEQUENZ];
    std::set<int> pair_positions;
    for (int seed=0; seed<100; ++seed) {
        srand(seed);
        int length=generate_sequenz(sequence,MODUS_HARAKIRI_CUSTOM,enabled,&pair,1);
        assert(length==9);
        int counts[MASCHINE_COUNT]={};
        for(int i=0;i<length;++i) {
            auto &entry=sequence[i]; ++counts[entry.maschine];
            if(entry.isPair && !entry.isDoublette) {
                pair_positions.insert(i);
                assert(i+1<length && entry.maschine==MASCHINE_A && entry.partner==MASCHINE_F);
                assert(sequence[i+1].maschine==MASCHINE_F && sequence[i+1].isDoublette);
                assert(entry.delayMs==1250 && sequence[i+1].delayMs==1250);
            }
        }
        for(int m=0;m<MASCHINE_H;++m) assert(counts[m]==(m==MASCHINE_A || m==MASCHINE_F ? 2 : 1));
        assert(counts[MASCHINE_H]==0);
    }
    assert(pair_positions.size()>1); // pair is shuffled as a unit, not stuck at the end
    enabled[MASCHINE_H]=false;
    assert(generate_sequenz(sequence,MODUS_HARAKIRI_CUSTOM,enabled,&pair,1)==9);
    enabled[MASCHINE_G]=false;
    assert(generate_sequenz(sequence,MODUS_HARAKIRI_CUSTOM,enabled,&pair,1)==8);
    enabled[MASCHINE_A]=false;
    assert(generate_sequenz(sequence,MODUS_HARAKIRI_CUSTOM,enabled,&pair,1)==0);
    enabled[MASCHINE_A]=true;
    assert(generate_sequenz(sequence,MODUS_HARAKIRI_CUSTOM,enabled,nullptr,0)==0);
    auto invalid=pair; invalid.partner=invalid.maschine;
    assert(generate_sequenz(sequence,MODUS_HARAKIRI_CUSTOM,enabled,&invalid,1)==0);
    for(int mode=MODUS_NORMAL; mode<MODUS_COUNT; ++mode) {
        for(int h=0;h<2;++h) {
            memset(&g_store,0,sizeof(g_store)); finished=0;
            g_store.modus=(Modus)mode; g_store.lauf=1; g_store.spielerCount=1;
            g_store.spieler[0].id=11; g_store.spieler[0].startPosten=1;
            g_store.sequenzLen=append_pair(g_store.sequenz,&g_store.taubeIndex,
                h ? MASCHINE_H : MASCHINE_A,h ? MASCHINE_H : MASCHINE_F,1250) ? 2 : 0;
            g_store.taubeIndex=0;
            for(int i=0;i<5;++i)g_store.customLaeufe[i]=1;
            store_eintragen(1);
            assert(g_store.ergebnisseCount==0 && g_store.taubeIndex==0 && g_store.spieler[0].punkte==0);
            store_eintragen(2);store_eintragen(2);
            assert(g_store.ergebnisseCount==2 && g_store.spieler[0].punkte==4);
            assert(g_store.ergebnisse[0].punkte==2 && g_store.ergebnisse[1].punkte==2);
            assert(g_store.ergebnisse[0].schuss1 && !g_store.ergebnisse[0].schuss2);
            assert(g_store.ergebnisse[0].posten==g_store.ergebnisse[1].posten);
        }
    }
    memset(&g_store,0,sizeof(g_store));
    g_store.modus=MODUS_CUSTOM_1; g_store.lauf=1; g_store.customLaeufe[0]=1;
    g_store.spielerCount=1;g_store.spieler[0].startPosten=1;
    int length=0;append_pair(g_store.sequenz,&length,MASCHINE_A,MASCHINE_F,0);
    g_store.sequenzLen=length;store_eintragen(0);store_eintragen(2);
    assert(g_store.ergebnisseCount==2 && g_store.spieler[0].punkte==2);
    assert(!g_store.ergebnisse[0].schuss1 && !g_store.ergebnisse[0].schuss2);
    puts("PASS: random A-G singles plus one intact configured pair, no H; inactive/missing/invalid pairs rejected");
    puts("PASS: every H/Custom doublette in all seven modes rejects 1, scores 2/0 per clay, maximum 4 per pair");
}
"""

if __name__ == "__main__":
    source = (ROOT / "main/store/game_store.cpp").read_text()
    functions = "\n".join(fixture.function(source, signature) for signature in [
        "static void shuffle", "static bool append_single", "static bool append_pair",
        "static int generate_sequenz", "static int count_h2_before", "void store_eintragen"])
    start = fixture.function(source, "bool store_start_spiel")
    assert start.index("Harakiri Custom: Doublette") < start.index("// Check all players have credits")
    for filename in ["screen_resultate.cpp", "screen_geschichte.cpp"]:
        ui = (ROOT / "main/ui" / filename).read_text()
        assert "lv_color_hex(0xFFFFFF), LV_PART_ITEMS" in ui
    api = (ROOT / "main/net/http_sync.cpp").read_text()
    assert 'case MODUS_HARAKIRI_CUSTOM: return "HARAKIRI_CUSTOM"' in api
    assert "sizeof(g_store.customSequenzen[0]) * 4" in source
    with tempfile.TemporaryDirectory(prefix="tm-harakiri-custom-") as directory:
        path = Path(directory)
        (path / "test.cpp").write_text(STUBS + functions + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Os", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "main/store"), str(path / "test.cpp"),
                        "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
#!/usr/bin/env python3
"""Real production builders and POSIX lifecycle, with an 8.3-only open guard."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",Path(__file__).with_name("test_machine_batch.py"))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
sources={
 "active":("store/active_game.cpp","static void path("),
 "day":("store/day_player_events.cpp","void path("),
 "queue":("net/portal_runtime.cpp","static void filePath("),
 "journal":("store/game_journal.cpp","static void sibling_path("),
}
builders=""
for namespace,(name,signature) in sources.items():
    source=(ROOT/"main"/name).read_text()
    builders+=f"namespace {namespace} {{\n"+fixture.function(source,signature)+"\n}\n"
# No second-dot writer or hand-built FAT file path may be reintroduced.
for name in ["store/active_game.cpp","store/day_player_events.cpp","store/game_journal.cpp",
             "store/offline_cache.cpp","net/portal_runtime.cpp","net/portal_storage.h"]:
    source=(ROOT/"main"/name).read_text()
    assert '"%s.tmp"' not in source and '"%s.part"' not in source and '"%s.bak"' not in source,name
    assert 'snprintf(path,sizeof(path),"/fatfs/' not in source,name

code=r'''
#include <cassert>
#include <string>
#include <sys/stat.h>
#include "fat_patterns.h"
#include "failure_log_gate.h"
#define ACTIVE_GAME_ROOT "/fatfs"
#define DAY_PLAYERS_ROOT "/fatfs"
static bool fail_sync=false;
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){if(fail_sync){errno=EIO;return -1;}return __real_fsync(fd);}
static void valid(const char *path) {
    const char *leaf=strrchr(path,'/');assert(leaf);
    char rebuilt[192];assert(fat_name(rebuilt,sizeof(rebuilt),leaf+1));
    assert(std::string(rebuilt)==std::string("/fatfs/")+(leaf+1));
}
''' + builders + r'''
int main(int argc,char **argv) {
    assert(argc==2);char path[192],part[192],error[180];
    for(int slot=0;slot<2;slot++) {
        active::path(slot,path,sizeof(path));valid(path);
        assert(fat_name(part,sizeof(part),path,"TMP"));valid(part);
        day::path(slot,path,sizeof(path));valid(path);
        queue::filePath(slot,path);valid(path);
    }
    for(int slot=0;slot<200;slot++) {
        char base[9];snprintf(base,sizeof(base),"J%03d",slot);
        assert(fat_name(path,sizeof(path),base,"REC"));valid(path);
        for(const char *ext:{"REC","PRT","BAK"}){journal::sibling_path(path,ext,part,sizeof(part));valid(part);}
    }
    for(const auto &pattern:FAT_PATTERNS){assert(fat_name(path,sizeof(path),pattern.base,pattern.extension));valid(path);}
    assert(fat_name(path,sizeof(path),"ACTIVE0.BIN","TMP"));assert(!strcmp(path,"/fatfs/ACTIVE0.TMP"));
    assert(!fat_name(path,sizeof(path),"ACTIVE0.BIN.TMP"));
    assert(!fat_name(path,sizeof(path),"TOO_LONG_NAME","BIN"));
    assert(!fat_name(path,sizeof(path),"ACTIVE0","PART"));
    std::string cache=std::string(argv[1])+"/TMCACHE";assert(mkdir(cache.c_str(),0700)==0);
    assert(fat_patterns_self_test(argv[1],error,sizeof(error)));
    fail_sync=true;
    assert(!fat_patterns_self_test(argv[1],error,sizeof(error)));
    assert(strstr(error,"ACTIVE0.BIN")&&strstr(error,"fsync")&&strstr(error,"errno=5"));
    fail_sync=false;assert(fat_patterns_self_test(argv[1],error,sizeof(error)));
    FailureLogGate gate;FailureLogCount count;
    assert(gate.admit(1,1,&count));
    for(int i=1;i<825;i++)assert(!gate.admit(1,i*1000,&count));
    assert(gate.admit(2,1000000,&count));
    assert(gate.admit(1,60000001,&count));assert(count.count==826&&count.first==1&&count.last==60000001);
    puts("PASS 200 journal slots, all production slot builders/patterns, replacement extensions, lifecycle/fsync fault and 60s distinct-error aggregation");
}
'''
with tempfile.TemporaryDirectory(prefix="rm-fat-paths-") as tmp:
    path=Path(tmp);(path/"test.cpp").write_text(code)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT/"main/store"),
                    str(path/"test.cpp"),"-Wl,--wrap=fsync","-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test"),tmp],check=True)

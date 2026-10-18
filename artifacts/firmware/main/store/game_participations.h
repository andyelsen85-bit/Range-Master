#pragma once
#include "game_store.h"

// Wire projection only. Never enlarge PendingGame/FinishedGame: existing FAT
// journals, active snapshots and the emergency NVS record use their binary ABI.
struct RoundParticipation { int spielerId,startPosten,punkte,lauf; };
static constexpr int MAX_ROUND_PARTICIPATIONS=MAX_SPIELER*2;
inline int game_round_participations(const PendingGame *game,RoundParticipation *rows) {
    int count=0;
    if(!game->ergebnisse_count) {
        for(int i=0;i<game->teilnahmen_count && i<MAX_SPIELER;i++) {
            const auto &p=game->teilnahmen[i];
            rows[count++]={p.spielerId,p.startPosten,p.punkte,p.lauf};
        }
        return count; // old aggregate-only history cannot be split reliably
    }
    int ids[MAX_SPIELER]{},players=0;
    auto player=[&](int id) {
        for(int i=0;i<players;i++)if(ids[i]==id)return;
        if(players<MAX_SPIELER)ids[players++]=id;
    };
    for(int i=0;i<game->teilnahmen_count && i<MAX_SPIELER;i++)player(game->teilnahmen[i].spielerId);
    for(int i=0;i<game->ergebnisse_count && i<MAX_ERGEBNISSE;i++)player(game->ergebnisse[i].spielerId);
    for(int i=0;i<players;i++)for(int j=i+1;j<players;j++)
        if(ids[j]<ids[i]){int old=ids[i];ids[i]=ids[j];ids[j]=old;}
    for(int p=0;p<players;p++)for(int run=1;run<=game->lauf && run<=2;run++) {
        int points=0,post=1;const Ergebnis *first=nullptr;
        for(int i=0;i<game->teilnahmen_count && i<MAX_SPIELER;i++)
            if(game->teilnahmen[i].spielerId==ids[p]){post=game->teilnahmen[i].startPosten;break;}
        for(int i=0;i<game->teilnahmen_count && i<MAX_SPIELER;i++)
            if(game->teilnahmen[i].spielerId==ids[p] && game->teilnahmen[i].lauf==run)
                post=game->teilnahmen[i].startPosten;
        for(int i=0;i<game->ergebnisse_count && i<MAX_ERGEBNISSE;i++) {
            const auto &e=game->ergebnisse[i];
            if(e.spielerId!=ids[p] || e.lauf!=run)continue;
            if(!e.wiederholt)points+=e.punkte;
            if(!first || e.taube<first->taube ||
               (e.taube==first->taube && (e.wiederholt<first->wiederholt ||
                (e.wiederholt==first->wiederholt && (e.posten<first->posten ||
                 (e.posten==first->posten && e.maschine<first->maschine))))))first=&e;
        }
        rows[count++]={ids[p],first?first->posten:post,points,run};
    }
    return count;
}
inline void game_merge_participation(PendingGame *game,int player,int post,int points,int run) {
    for(int i=0;i<game->teilnahmen_count;i++)if(game->teilnahmen[i].spielerId==player) {
        auto &p=game->teilnahmen[i];p.punkte+=points;
        if(run<p.lauf){p.lauf=run;p.startPosten=post;}
        return;
    }
    if(game->teilnahmen_count<MAX_SPIELER) {
        auto &p=game->teilnahmen[game->teilnahmen_count++];
        p.spielerId=player;p.startPosten=post;p.punkte=points;p.lauf=run;
    }
}

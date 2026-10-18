#pragma once
#include <stdio.h>
#include "game_store.h"

struct HistoryScorecardShape {
    int runs;
    int clays;
};

static inline HistoryScorecardShape history_scorecard_shape(const FinishedGame *game)
{
    int runs = game->base.lauf;
    int clays = game->base.taubenProLauf;
    for (int i = 0; i < game->base.ergebnisse_count && i < MAX_ERGEBNISSE; ++i) {
        const Ergebnis *result = &game->base.ergebnisse[i];
        if (result->lauf > runs) runs = result->lauf;
        if (result->taube > clays) clays = result->taube;
    }
    if (clays < 0) clays = 0;
    if (clays > MAX_SEQUENZ) clays = MAX_SEQUENZ;
    if (runs < 1) runs = 1;
    // A game can store at most this many per-clay results. Bound malformed
    // metadata without allocating thousands of empty LVGL cells.
    int max_runs = clays ? MAX_ERGEBNISSE / clays : 1;
    if (runs > max_runs) runs = max_runs;
    return {runs, clays};
}

static inline const Ergebnis *history_scorecard_raw_result(const FinishedGame *game,
                                                       int player, int run, int clay)
{
    const Ergebnis *found = NULL;
    for (int i = 0; i < game->base.ergebnisse_count && i < MAX_ERGEBNISSE; ++i) {
        const Ergebnis *result = &game->base.ergebnisse[i];
        if (!result->wiederholt && result->spielerId == player &&
            result->lauf == run && result->taube == clay) found = result;
    }
    return found;
}

static inline bool history_scorecard_harakiri(const FinishedGame *game) {
    return game->base.modus==MODUS_HARAKIRI || game->base.modus==MODUS_HARAKIRI_CUSTOM;
}
// Signature separates a single A from the A in an A/F doublette. Infer the
// pair from each player's own adjacent ledger entries, not another player's
// shot position or today's configuration.
static inline int history_scorecard_key(const FinishedGame *game,const Ergebnis *r) {
    if(!r || r->maschine<MASCHINE_A || r->maschine>=MASCHINE_COUNT)return -1;
    const Ergebnis *next=history_scorecard_raw_result(game,r->spielerId,r->lauf,r->taube+1);
    const Ergebnis *prev=history_scorecard_raw_result(game,r->spielerId,r->lauf,r->taube-1);
    int role=0;
    if(r->posten>=1 && r->posten<=6) {
        if(next && next->posten==r->posten)role=1;
        else if(prev && prev->posten==r->posten)role=2;
    }
    return role*MASCHINE_COUNT+(int)r->maschine;
}
static inline int history_scorecard_column_key(const FinishedGame *game,int run,int clay) {
    bool seen[MASCHINE_COUNT*3]={};
    for(int i=0;i<game->base.ergebnisse_count && i<MAX_ERGEBNISSE;i++) {
        const Ergebnis *r=&game->base.ergebnisse[i];
        if(r->lauf!=run || r->wiederholt)continue;
        int key=history_scorecard_key(game,r);if(key>=0)seen[key]=true;
    }
    int column=0;
    for(int key=0;key<MASCHINE_COUNT*3;key++)
        if(seen[key] && ++column==clay)return key;
    return -1;
}
static inline const Ergebnis *history_scorecard_result(const FinishedGame *game,int player,int run,int clay) {
    if(!history_scorecard_harakiri(game))return history_scorecard_raw_result(game,player,run,clay);
    int key=history_scorecard_column_key(game,run,clay);
    const Ergebnis *found=NULL;
    for(int i=0;i<game->base.ergebnisse_count && i<MAX_ERGEBNISSE;i++) {
        const Ergebnis *r=&game->base.ergebnisse[i];
        if(!r->wiederholt && r->spielerId==player && r->lauf==run &&
           history_scorecard_key(game,r)==key && key>=0)found=r;
    }
    return found;
}

static inline void history_scorecard_heading(const FinishedGame *game, int run,
                                              int clay, char *text, size_t capacity)
{
    if(history_scorecard_harakiri(game)) {
        int key=history_scorecard_column_key(game,run,clay);
        if(key<0)snprintf(text,capacity,"-");
        else if(key%MASCHINE_COUNT==MASCHINE_H && key>=MASCHINE_COUNT)
            snprintf(text,capacity,"H%d",key/MASCHINE_COUNT);
        else snprintf(text,capacity,"%c",'A'+key%MASCHINE_COUNT);
        return;
    }
    for (int i = 0; i < game->base.ergebnisse_count && i < MAX_ERGEBNISSE; ++i) {
        const Ergebnis *result = &game->base.ergebnisse[i];
        if (result->lauf == run && result->taube == clay &&
            result->maschine >= MASCHINE_A && result->maschine <= MASCHINE_H) {
            snprintf(text, capacity, "%c", 'A' + result->maschine);
            return;
        }
    }
    // Missing records must not invent a machine from today's configuration.
    snprintf(text, capacity, "-");
}

static inline bool history_scorecard_pair_at(const FinishedGame *game, int run, int first_clay)
{
    if(history_scorecard_harakiri(game)) {
        int key=history_scorecard_column_key(game,run,first_clay);
        return key>=MASCHINE_COUNT && key<2*MASCHINE_COUNT;
    }
    if (first_clay < 1 || first_clay >= MAX_SEQUENZ) return false;
    // Each player's two pair results share one physical stand. Singles
    // advance the stand, including consecutive single H targets. Inspect the
    // saved ledger, never today's mode/configuration or machine letters alone.
    for (int i = 0; i < game->base.ergebnisse_count && i < MAX_ERGEBNISSE; ++i) {
        const Ergebnis *candidate = &game->base.ergebnisse[i];
        if (candidate->lauf != run || candidate->taube != first_clay) continue;
        const Ergebnis *first = history_scorecard_result(game, candidate->spielerId, run, first_clay);
        const Ergebnis *second = history_scorecard_result(game, candidate->spielerId, run, first_clay + 1);
        if (first && second && first->posten >= 1 && first->posten <= 6 &&
            first->posten == second->posten) return true;
    }
    return false;
}

static inline bool history_scorecard_doublette(const FinishedGame *game, int run, int clay)
{
    return history_scorecard_pair_at(game, run, clay - 1) ||
           history_scorecard_pair_at(game, run, clay);
}

static inline int history_scorecard_total(const FinishedGame *game, int player)
{
    if(!game->base.ergebnisse_count) {
        for (int i = 0; i < game->base.teilnahmen_count && i < MAX_SPIELER; ++i)
            if (game->base.teilnahmen[i].spielerId == player)
                return game->base.teilnahmen[i].punkte;
    }
    HistoryScorecardShape shape = history_scorecard_shape(game);
    int total = 0;
    for (int run = 1; run <= shape.runs; ++run)
        for (int clay = 1; clay <= shape.clays; ++clay) {
            const Ergebnis *result = history_scorecard_result(game, player, run, clay);
            if (result) total += result->punkte;
        }
    return total;
}
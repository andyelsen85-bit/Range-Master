#pragma once
#include <stdio.h>
#include <string.h>
#include "history_scorecard.h"
// View-only projection supplies the full stable machine/pair column plan,
// including unscored cells. Never persisted or sent as a game result.
inline int active_scorecard_project(const GameStore *s,FinishedGame *out) {
    memset(out,0,sizeof(*out));
    out->base.modus=s->modus;out->base.taubenProLauf=s->sequenzLen;
    out->base.lauf=s->modus==MODUS_HARAKIRI_CUSTOM?s->customLaeufe[4]:
        (s->modus>=MODUS_CUSTOM_1&&s->modus<=MODUS_CUSTOM_4?s->customLaeufe[s->modus-MODUS_CUSTOM_1]:2);
    out->spieler_count=s->spielerCount;
    for(int p=0;p<s->spielerCount;p++) {
        out->spielerIds[p]=s->spieler[p].id;
        snprintf(out->spielerNamen[p],sizeof(out->spielerNamen[p]),"%s",s->spieler[p].name);
        for(int run=1;run<=out->base.lauf;run++) {
            int unit=0;
            for(int c=0;c<s->sequenzLen&&out->base.ergebnisse_count<MAX_ERGEBNISSE;c++) {
                const auto *se=store_sequence_entry(s,p,c);if(!se)continue;
                auto &r=out->base.ergebnisse[out->base.ergebnisse_count++];
                r.spielerId=s->spieler[p].id;r.lauf=run;r.taube=c+1;r.maschine=se->maschine;
                r.posten=unit%5+1;r.punkte=-1;
                for(int i=0;i<s->ergebnisseCount;i++) {
                    const auto &actual=s->ergebnisse[i];
                    if(!actual.wiederholt&&actual.spielerId==r.spielerId&&actual.lauf==run&&actual.taube==c+1) {
                        // Keep the plan's physical-unit key for unambiguous pair grouping.
                        int post=r.posten;r=actual;r.posten=post;break;
                    }
                }
                if(!se->isPair||se->isDoublette)unit++;
            }
        }
    }
    if(!s->harakiriOrdersReady)return (s->lauf-1)*s->sequenzLen+s->taubeIndex+1;
    const auto *current=history_scorecard_raw_result(out,s->spieler[s->spielerIndex].id,s->lauf,s->taubeIndex+1);
    int key=history_scorecard_key(out,current);
    for(int c=1;c<=s->sequenzLen;c++)
        if(history_scorecard_column_key(out,s->lauf,c)==key)return (s->lauf-1)*s->sequenzLen+c;
    return -1;
}

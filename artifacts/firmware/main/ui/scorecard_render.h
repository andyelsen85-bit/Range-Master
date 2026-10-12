#pragma once
#include "history_scorecard.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
struct ScorecardRenderState { uint8_t pair[MAX_ERGEBNISSE+2]; };
static inline void scorecard_render_draw(lv_event_t *event,const ScorecardRenderState *state) {
    lv_draw_task_t *task=lv_event_get_draw_task(event);if(!task)return;
    lv_draw_dsc_base_t *base=(lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(task);
    if(!base||base->part!=LV_PART_ITEMS||base->id2>=MAX_ERGEBNISSE+2||!state->pair[base->id2])return;
    lv_draw_fill_dsc_t *fill=lv_draw_task_get_fill_dsc(task);
    if(fill){fill->color=lv_color_hex(0x343C49);fill->opa=LV_OPA_COVER;}
    lv_draw_border_dsc_t *border=lv_draw_task_get_border_dsc(task);
    if(border)border->side=(lv_border_side_t)(border->side&~(state->pair[base->id2]==1?LV_BORDER_SIDE_RIGHT:LV_BORDER_SIDE_LEFT));
}
static inline void scorecard_render(lv_obj_t *table,const FinishedGame *game,ScorecardRenderState *state) {
    HistoryScorecardShape shape=history_scorecard_shape(game);
    int n=game->spieler_count;if(n<0)n=0;if(n>MAX_SPIELER)n=MAX_SPIELER;
    int columns=shape.runs*shape.clays;memset(state,0,sizeof(*state));
    lv_table_set_col_cnt(table,columns+2);lv_table_set_row_cnt(table,n+1);
    lv_table_set_col_width(table,0,150);lv_table_set_cell_value(table,0,0,"SPIELER");
    lv_obj_set_style_text_color(table,lv_color_hex(0xFFFFFF),LV_PART_ITEMS);
    lv_obj_set_style_text_color(table,lv_color_hex(0xFFFFFF),LV_PART_ITEMS|LV_STATE_PRESSED);
    for(int col=0;col<columns;col++){
        int run=col/shape.clays+1,clay=col%shape.clays+1;char heading[32];
        history_scorecard_heading(game,run,clay,heading,sizeof(heading));
        if(history_scorecard_pair_at(game,run,clay))state->pair[col+1]=1;
        else if(history_scorecard_pair_at(game,run,clay-1))state->pair[col+1]=2;
        lv_table_set_col_width(table,col+1,34);lv_table_set_cell_value(table,0,col+1,heading);
    }
    lv_table_set_col_width(table,columns+1,76);lv_table_set_cell_value(table,0,columns+1,"GESAMT");
    for(int p=0;p<n;p++){
        int sid=game->spielerIds[p];lv_table_set_cell_value(table,p+1,0,game->spielerNamen[p]);
        lv_table_set_cell_ctrl(table,p+1,0,LV_TABLE_CELL_CTRL_TEXT_CROP);
        for(int col=0;col<columns;col++){
            const Ergebnis *r=history_scorecard_result(game,sid,col/shape.clays+1,col%shape.clays+1);char score[16];
            if(r)snprintf(score,sizeof(score),"%d",r->punkte);else snprintf(score,sizeof(score),"-");
            lv_table_set_cell_value(table,p+1,col+1,score);
        }
        char total[16];snprintf(total,sizeof(total),"%d",history_scorecard_total(game,sid));lv_table_set_cell_value(table,p+1,columns+1,total);
    }
}

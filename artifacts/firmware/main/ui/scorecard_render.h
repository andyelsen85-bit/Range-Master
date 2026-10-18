#pragma once
#include "history_scorecard.h"
#include "lvgl.h"
#include "ui_fonts.h"
#include "score_colors.h"
#include "scorecard_layout.h"
#include <stdio.h>
#include <string.h>
struct ScorecardRenderState {
    uint8_t pair[MAX_ERGEBNISSE+2];
    int8_t points[MAX_SPIELER][MAX_ERGEBNISSE];
    int currentRow,currentColumn,columns;
    const lv_font_t *nameFont[MAX_SPIELER];
};
static inline void scorecard_render_draw(lv_event_t *event,const ScorecardRenderState *state) {
    lv_draw_task_t *task=lv_event_get_draw_task(event);if(!task)return;
    lv_draw_dsc_base_t *base=(lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(task);
    if(!base||base->part!=LV_PART_ITEMS||base->id2>=MAX_ERGEBNISSE+2)return;
    bool scored=base->id1>0&&base->id1<=MAX_SPIELER&&base->id2>0&&
        base->id2<=(unsigned)state->columns&&state->points[base->id1-1][base->id2-1]>=0;
    int point=scored?state->points[base->id1-1][base->id2-1]:-1;
    lv_draw_fill_dsc_t *fill=lv_draw_task_get_fill_dsc(task);
    if(fill&&(scored||state->pair[base->id2])) {
        fill->color=lv_color_hex(scored?score_color(point):0x343C49);fill->opa=LV_OPA_COVER;
    }
    auto *label=lv_draw_task_get_label_dsc(task);
    if(label){
        label->color=lv_color_hex(scored&&point>0?0x000000:0xFFFFFF);
        if(base->id2==0&&base->id1>0&&base->id1<=MAX_SPIELER&&state->nameFont[base->id1-1])
            label->font=state->nameFont[base->id1-1];
    }
    lv_draw_border_dsc_t *border=lv_draw_task_get_border_dsc(task);
    if(border) {
        if(state->pair[base->id2])border->side=(lv_border_side_t)(border->side&~(state->pair[base->id2]==1?LV_BORDER_SIDE_RIGHT:LV_BORDER_SIDE_LEFT));
        if((int)base->id1==state->currentRow&&(int)base->id2==state->currentColumn) {
            border->side=LV_BORDER_SIDE_FULL;border->width=3;border->color=lv_color_hex(0xFFFFFF);
        } else if(scored&&point==0){border->width=1;border->color=lv_color_hex(0xCBD5E1);}
    }
}
static inline void scorecard_render(lv_obj_t *table,const FinishedGame *game,ScorecardRenderState *state,
                                   bool fit=false,int currentRow=-1,int currentColumn=-1) {
    HistoryScorecardShape shape=history_scorecard_shape(game);
    int n=game->spieler_count;if(n<0)n=0;if(n>MAX_SPIELER)n=MAX_SPIELER;
    int columns=shape.runs*shape.clays;memset(state,0,sizeof(*state));
    memset(state->points,-1,sizeof(state->points));
    state->columns=columns;state->currentRow=currentRow;state->currentColumn=currentColumn;
    lv_obj_update_layout(table);
    auto layout=scorecard_layout(lv_obj_get_content_width(table),lv_obj_get_content_height(table),n,columns);
    if(fit) {
        const lv_font_t *font=layout.font>=20?UI_FONT_20:layout.font>=18?UI_FONT_18:
            layout.font>=16?UI_FONT_16:layout.font>=14?UI_FONT_14:UI_FONT_12;
        lv_obj_set_style_text_font(table,font,LV_PART_ITEMS);
        lv_obj_set_style_pad_hor(table,2,LV_PART_ITEMS);
        lv_obj_set_style_pad_ver(table,std::max(1,(layout.row-(int)font->line_height-2)/2),LV_PART_ITEMS);
        lv_obj_clear_flag(table,LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_table_set_col_cnt(table,columns+2);lv_table_set_row_cnt(table,n+1);
    lv_table_set_col_width(table,0,fit?layout.name:150);lv_table_set_cell_value(table,0,0,"SPIELER");
    lv_obj_set_style_text_color(table,lv_color_hex(0xFFFFFF),LV_PART_ITEMS);
    lv_obj_set_style_text_color(table,lv_color_hex(0xFFFFFF),LV_PART_ITEMS|LV_STATE_PRESSED);
    for(int col=0;col<columns;col++){
        int run=col/shape.clays+1,clay=col%shape.clays+1;char heading[32];
        history_scorecard_heading(game,run,clay,heading,sizeof(heading));
        if(history_scorecard_pair_at(game,run,clay))state->pair[col+1]=1;
        else if(history_scorecard_pair_at(game,run,clay-1))state->pair[col+1]=2;
        lv_table_set_col_width(table,col+1,fit?layout.clay:34);lv_table_set_cell_value(table,0,col+1,heading);
    }
    lv_table_set_col_width(table,columns+1,fit?layout.total:76);lv_table_set_cell_value(table,0,columns+1,"TOTAL");
    for(int p=0;p<n;p++){
        int sid=game->spielerIds[p];
        char shown[sizeof(game->spielerNamen[p])];
        snprintf(shown,sizeof(shown),"%s",game->spielerNamen[p]);
        auto *font=lv_obj_get_style_text_font(table,LV_PART_ITEMS);
        lv_point_t size{};
        int available=(fit?layout.name:150)-(fit?8:24);
        lv_text_get_size(&size,shown,font,0,0,32767,LV_TEXT_FLAG_NONE);
        if(size.x>available)font=UI_FONT_12;
        lv_text_get_size(&size,shown,font,0,0,32767,LV_TEXT_FLAG_NONE);
        size_t length=strlen(shown);
        if(length>sizeof(shown)-4)length=sizeof(shown)-4;
        while(size.x>available&&length) {
            --length;
            while(length&&((unsigned char)game->spielerNamen[p][length]&0xC0)==0x80)--length;
            snprintf(shown,sizeof(shown),"%.*s...",(int)length,game->spielerNamen[p]);
            lv_text_get_size(&size,shown,font,0,0,32767,LV_TEXT_FLAG_NONE);
        }
        state->nameFont[p]=font;
        lv_table_set_cell_value(table,p+1,0,shown);
        lv_table_set_cell_ctrl(table,p+1,0,LV_TABLE_CELL_CTRL_TEXT_CROP);
        for(int col=0;col<columns;col++){
            const Ergebnis *r=history_scorecard_result(game,sid,col/shape.clays+1,col%shape.clays+1);char score[16];
            if(r&&r->punkte>=0){snprintf(score,sizeof(score),"%d",r->punkte);state->points[p][col]=r->punkte;}
            else snprintf(score,sizeof(score),"-");
            lv_table_set_cell_value(table,p+1,col+1,score);
        }
        char total[16];snprintf(total,sizeof(total),"%d",history_scorecard_total(game,sid));lv_table_set_cell_value(table,p+1,columns+1,total);
    }
}

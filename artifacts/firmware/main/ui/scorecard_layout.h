#pragma once
#include <algorithm>
#include "ui_layout.h"
// Width the TOTAL column needs: measured text + both paddings + 1 px border slack.
inline int scorecard_total_needed(int text_w,int pad){return text_w+2*pad+2;}
struct ScorecardLayout { int name,total,clay,row,font; };
// Every shot column gets the same width; total and name widths are derived from
// the available width so the table always fills it without horizontal scroll.
// Remaining pixels after integer division go to the name column only.
inline ScorecardLayout scorecard_layout(int width,int height,int players,int columns) {
    int row=std::max(20,height/std::max(1,players+1));
    int font=std::min(20,std::max(12,row-12));
    int name=std::min(240,std::max(110,width/5));
    int total=std::max(40,ui_estimate_text_w(5,font)+8);
    int clay=columns?std::max(1,(width-name-total)/columns):0;
    // Narrow shot cells: step the font down so a centered digit always fits.
    while(columns>0&&font>12&&clay<font+6){font-=2;total=std::max(40,ui_estimate_text_w(5,font)+8);
        clay=std::max(1,(width-name-total)/columns);}
    if(columns>0&&clay<26)font=12;
    if(columns>0)name=std::max(1,width-total-clay*columns);
    return {name,total,clay,row,font};
}

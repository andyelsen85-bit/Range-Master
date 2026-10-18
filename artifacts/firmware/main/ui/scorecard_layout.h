#pragma once
#include <algorithm>
struct ScorecardLayout { int name,total,clay,row,font; };
inline ScorecardLayout scorecard_layout(int width,int height,int players,int columns) {
    int total=76,name=std::min(260,std::max(150,width/4));
    int clay=columns?std::max(1,(width-name-total)/columns):0;
    int row=std::max(20,height/std::max(1,players+1));
    int font=std::min(20,std::max(12,row-12));
    if(columns>0&&clay<26)font=12;
    return {name,total,clay,row,font};
}

#pragma once

// Physical panel coordinates never change. Portrait is a clockwise quarter
// turn relative to the usual 800x340 landscape UI, including touch and text.
struct ScreenOrientation {
    bool portrait=false;
    int width()const{return portrait?340:800;}
    int height()const{return portrait?800:340;}
    int native_x(int x,int y)const{return portrait?x:y;}
    int native_y(int x,int y)const{return portrait?y:799-x;}
    int logical_x(int x,int y)const{return portrait?x:799-y;}
    int logical_y(int x,int y)const{return portrait?y:x;}
};

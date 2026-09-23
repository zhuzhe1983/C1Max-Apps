#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace gomoku {
namespace {
constexpr int axes[4][2]={{1,0},{0,1},{1,1},{1,-1}};
bool wins(const Game &g,int x,int y,int player){
    for(const auto &d:axes){
        int n=1;
        for(int sign:{-1,1}){
            for(int step=1;step<side;++step){
                if(g.at(x+sign*step*d[0],y+sign*step*d[1])!=player)break;
                ++n;
            }
        }
        if(n>=5)return true;
    }
    return false;
}
int score(const Game &g,int x,int y,int player){
    int sum=0;
    for(const auto &d:axes){
        int count=1,open=0;
        for(int sign:{-1,1})for(int step=1;step<side;++step){
            int p=g.at(x+sign*step*d[0],y+sign*step*d[1]);
            if(p==player){++count;continue;}
            if(p==0)++open;break;
        }
        if(count>=5)return 1000000;
        if(!open)continue;
        if(count==4)sum+=open==2?80000:12000;
        else if(count==3)sum+=open==2?7000:500;
        else if(count==2)sum+=open==2?350:35;
        else sum+=open==2?12:2;
    }
    return sum;
}
}
bool Game::place(int x,int y){
    if(winner||at(x,y)!=0)return false;
    int player=turn();board[y*side+x]=player;moves.push_back({x,y});
    if(wins(*this,x,y,player))winner=player;
    else if(moves.size()==cells)winner=3;
    return true;
}
bool Game::undo(){
    if(moves.empty())return false;
    size_t count=computer&&moves.size()%2==0?2:1;
    while(count--&&!moves.empty()){auto m=moves.back();board[m.y*side+m.x]=0;moves.pop_back();}
    winner=0;return true;
}
Move Game::best_move() const{
    if(winner)return {-1,-1};
    if(moves.empty())return {side/2,side/2};
    const int player=turn(),other=3-player;
    Move choice{-1,-1};long best=-1;
    for(int y=0;y<side;++y)for(int x=0;x<side;++x){
        if(at(x,y))continue;
        if(wins(*this,x,y,player))return {x,y};
    }
    for(int y=0;y<side;++y)for(int x=0;x<side;++x){
        if(at(x,y))continue;
        if(wins(*this,x,y,other))return {x,y};
        bool near=false;
        for(int dy=-2;dy<=2&&!near;++dy)for(int dx=-2;dx<=2;++dx)if(at(x+dx,y+dy)>0)near=true;
        if(!near)continue;
        long value=score(*this,x,y,player)*11L+score(*this,x,y,other)*10L;
        value+=28-std::abs(x-7)-std::abs(y-7);
        if(value>best){best=value;choice={x,y};}
    }
    return choice;
}
std::string Game::serialize() const{
    std::ostringstream out;out<<"C1MAX_GOMOKU_V1 "<<computer<<'\n';
    for(const auto &m:moves)out<<m.x<<' '<<m.y<<'\n';return out.str();
}
Game Game::parse(const std::string &text){
    if(text.size()>4096)throw std::runtime_error("棋谱文件过大");
    std::istringstream in(text);std::string magic;int mode;
    if(!(in>>magic>>mode)||magic!="C1MAX_GOMOKU_V1"||(mode!=0&&mode!=1))throw std::runtime_error("棋谱格式无效");
    Game g;g.computer=mode;
    while(true){in>>std::ws;if(in.eof())break;int x,y;if(!(in>>x>>y)||!g.place(x,y))throw std::runtime_error("棋谱落子无效");}
    return g;
}
}

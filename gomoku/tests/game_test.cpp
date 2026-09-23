#include "game.hpp"
#include <cassert>
#include <iostream>
using namespace gomoku;
int main(){
    Game g;assert(g.best_move().x==7&&g.best_move().y==7);
    assert(!g.place(-1,0)&&!g.place(15,0));assert(g.place(7,7));assert(!g.place(7,7));assert(g.place(7,8));
    assert(g.undo()&&g.moves.empty());assert(!g.undo());
    for(auto direction: {Move{1,0},Move{0,1},Move{1,1},Move{1,-1}}){
        Game line;line.computer=false;
        for(int i=0;i<4;++i){assert(line.place(4+i*direction.x,8+i*direction.y));assert(line.place(i,0));}
        assert(line.place(4+4*direction.x,8+4*direction.y));assert(line.winner==1);
        assert(!line.place(14,14));assert(line.undo()&&line.winner==0);
    }
    Game defense;defense.place(4,7);defense.place(3,7);defense.place(5,7);defense.place(0,0);defense.place(6,7);defense.place(0,1);defense.place(7,7);
    auto block=defense.best_move();assert(block.x==8&&block.y==7);
    Game attack;attack.place(14,14);attack.place(4,7);attack.place(14,13);attack.place(5,7);attack.place(0,0);attack.place(6,7);attack.place(0,1);attack.place(7,7);attack.place(0,2);
    auto win=attack.best_move();assert(attack.place(win.x,win.y)&&attack.winner==2);
    auto saved=attack.serialize();auto loaded=Game::parse(saved);assert(loaded.board==attack.board&&loaded.winner==2);
    for(auto bad:{"","C1MAX_GOMOKU_V1 2","C1MAX_GOMOKU_V1 1\n-1 2","C1MAX_GOMOKU_V1 1\n1 1\n1 1","C1MAX_GOMOKU_V1 1\n2"}){
        bool rejected=false;try{Game::parse(bad);}catch(...){rejected=true;}assert(rejected);
    }
    // Deterministic full self play: all AI moves are legal and terminate.
    Game play;
    while(!play.winner){auto move=play.best_move();assert(play.place(move.x,move.y));assert(play.moves.size()<=cells);}
    std::cout<<"PASS rules, four win axes, occupied cells, undo, AI win/block, persistence, corruption, complete game\n";
}

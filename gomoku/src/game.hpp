#pragma once
#include <array>
#include <string>
#include <vector>
namespace gomoku {
constexpr int side=15, cells=side*side;
struct Move { int x,y; };
struct Game {
    std::array<unsigned char,cells> board{};
    std::vector<Move> moves;
    bool computer=true;
    int winner=0; // 0 playing, 1 black, 2 white, 3 draw. Freestyle, no forbidden moves.
    int turn() const { return 1+int(moves.size()%2); }
    int at(int x,int y) const { return x>=0&&x<side&&y>=0&&y<side?board[y*side+x]:-1; }
    bool place(int x,int y);
    bool undo();
    Move best_move() const;
    std::string serialize() const;
    static Game parse(const std::string &);
};
}

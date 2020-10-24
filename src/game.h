/*
 * c-chess-cli, a command line interface for UCI chess engines. Copyright 2020 lucasart.
 *
 * c-chess-cli is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * c-chess-cli is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If
 * not, see <http://www.gnu.org/licenses/>.
*/
#pragma once
#include "position.h"
#include "engine.h"
#include "options.h"
#include "vec.h"

enum {
    STATE_NONE,

    // All possible ways to lose
    STATE_CHECKMATE,  // lost by being checkmated
    STATE_TIME_LOSS,  // lost on time
    STATE_ILLEGAL_MOVE,  // lost by playing an illegal move
    STATE_RESIGN,  // resigned on behalf of the engine

    STATE_SEPARATOR,  // invalid result, just a market to separate losses from draws

    // All possible ways to draw
    STATE_STALEMATE,  // draw by stalemate
    STATE_THREEFOLD,  // draw by 3 position repetition
    STATE_FIFTY_MOVES,  // draw by 50 moves rule
    STATE_INSUFFICIENT_MATERIAL,  // draw due to insufficient material to deliver checkmate
    STATE_DRAW_ADJUDICATION  // draw by adjudication
};

typedef struct {
    Position pos;
    int score;  // score returned by the engine (in cp)
    int result;  // game result from pos.turn's pov
} Sample;

typedef struct {
    str_t names[NB_COLOR];  // names of players, by color
    Position *pos;  // list of positions (including moves) since game start
    Sample *samples;  // list of samples when generating training data
    int ply, state;
} Game;

Game game_new(const str_t *fen);
void game_del(Game *g);

int game_play(Game *g, const GameOptions *go, const Engine engines[2], const EngineOptions *eo[2],
    bool reverse);

void game_decode_state(const Game *g, str_t *result, str_t *reason);
void game_pgn(const Game *g, str_t *pgn);

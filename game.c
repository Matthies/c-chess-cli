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
#include <stdlib.h>
#include "game.h"
#include "gen.h"
#include "str.h"

void uci_position_command(const Game *g, str_t *cmd)
// Builds a string of the form "position fen ... [moves ...]". Implements rule50 pruning: start from
// the last position that reset the rule50 counter, to reduce the move list to the minimum, without
// losing information.
{
    // Index of the starting FEN, where rule50 was last reset
    const int ply0 = max(g->ply - g->pos[g->ply].rule50, 0);

    str_cpy(cmd, "position fen ");
    str_t fen = pos_get(&g->pos[ply0]);
    str_cat_s(cmd, &fen);
    str_delete(&fen);

    if (ply0 < g->ply) {
        str_cat(cmd, " moves");

        for (int ply = ply0 + 1; ply <= g->ply; ply++) {
            str_t lan = pos_move_to_lan(&g->pos[ply - 1], g->pos[ply].lastMove, g->go.chess960);
            str_cat(cmd, " ", lan.buf);
            str_delete(&lan);
        }
    }
}

int game_result(const Game *g, move_t *begin, move_t **end)
// See if the current position is game over by chess rules
{
    const Position *pos = &g->pos[g->ply];

    *end = gen_all_moves(pos, begin);

    if (*end == begin)
        return pos->checkers ? RESULT_CHECKMATE : RESULT_STALEMATE;
    else if (pos->rule50 >= 100) {
        assert(pos->rule50 == 100);
        return RESULT_FIFTY_MOVES;
    } else if (pos_insufficient_material(pos))
        return RESULT_INSUFFICIENT_MATERIAL;
    else {
        // Scan for 3 repetitions
        int repetitions = 1;

        for (int i = 4; i <= pos->rule50 && i <= g->ply; i += 2)
            if (g->pos[g->ply - i].key == pos->key && ++repetitions >= 3)
                return RESULT_THREEFOLD;
    }

    return RESULT_NONE;
}

bool illegal_move(move_t move, const move_t *begin, const move_t *end)
{
    for (const move_t *m = begin; m != end; m++)
        if (*m == move)
            return false;

    return true;
}

Game game_new(const char *fen, const GameOptions *go)
{
    Game g;

    g.names[WHITE] = str_new();
    g.names[BLACK] = str_new();

    g.ply = 0;
    g.maxPly = 256;
    g.pos = malloc(g.maxPly * sizeof(Position));
    pos_set(&g.pos[0], fen);

    g.result = RESULT_NONE;

    memcpy(&g.go, go, sizeof(*go));

    return g;
}

void game_delete(Game *g)
{
    free(g->pos), g->pos = NULL;
    str_delete(&g->names[WHITE], &g->names[BLACK]);
}

void game_play(Game *g, const Engine *first, const Engine *second)
{
    const Engine *engines[2] = {first, second};  // more practical for loops

    for (int color = WHITE; color <= BLACK; color++)
        str_cpy_s(&g->names[color], &engines[color ^ g->pos[0].turn]->name);

    for (int i = 0; i < 2; i++) {
        if (g->go.chess960)
            engine_writeln(engines[i], "setoption name UCI_Chess960 value true");

        engine_writeln(engines[i], "ucinewgame");
    }

    move_t played = 0;
    str_t goCmd[2] = {str_dup("go"), str_dup("go")};

    for (int i = 0; i < 2; i++) {
        if (g->go.nodes[i])
            str_cat_fmt(&goCmd[i], " nodes %u", (int)g->go.nodes[i]);

        if (g->go.depth[i])
            str_cat_fmt(&goCmd[i], " depth %i", g->go.depth[i]);

        if (g->go.movetime[i])
            str_cat_fmt(&goCmd[i], " movetime %i", g->go.movetime[i]);
    }

    str_t posCmd = str_new();
    int drawPlyCount = 0;
    int resignCount[NB_COLOR] = {0};

    for (g->ply = 0; ; g->ply++) {
        if (g->ply >= g->maxPly) {
            g->maxPly *= 2;
            g->pos = realloc(g->pos, g->maxPly * sizeof(Position));
        }

        if (played)
            pos_move(&g->pos[g->ply], &g->pos[g->ply - 1], played);

        move_t moves[MAX_MOVES], *end;

        if ((g->result = game_result(g, moves, &end)))
            break;

        const int turn = g->ply % 2;  // turn=0/1 means first/second, not white/black
        const Engine *e = engines[turn];

        uci_position_command(g, &posCmd);
        engine_writeln(e, posCmd.buf);

        engine_sync(e);

        engine_writeln(e, goCmd[turn].buf);

        int score;
        str_t lan = engine_bestmove(e, &score);

        played = pos_lan_to_move(&g->pos[g->ply], lan.buf, g->go.chess960);
        str_delete(&lan);

        if (illegal_move(played, moves, end)) {
            g->result = RESULT_ILLEGAL_MOVE;
            break;
        }

        // Apply draw adjudication rule
        if (g->go.drawCount && abs(score) <= g->go.drawScore) {
            if (++drawPlyCount >= 2 * g->go.drawCount) {
                g->result = RESULT_DRAW_ADJUDICATION;
                break;
            }
        } else
            drawPlyCount = 0;

        // Apply resign rule
        if (g->go.resignCount && score <= -g->go.resignScore) {
            if (++resignCount[turn] >= g->go.resignCount) {
                g->result = RESULT_RESIGN;
                break;
            }
        } else
            resignCount[turn] = 0;
    }

    str_delete(&goCmd[0], &goCmd[1], &posCmd);
    assert(g->result);
}

str_t game_decode_result(const Game *g, str_t *reason)
{
    str_t result = str_new();
    str_cpy(reason, "");  // default: termination by chess rules

    if (g->result == RESULT_NONE) {
        str_cpy(&result, "*");
        str_cpy(reason, "unterminated");
    } else if (g->result == RESULT_CHECKMATE) {
        str_cpy(&result, g->pos[g->ply].turn == WHITE ? "0-1" : "1-0");
        str_cpy(reason, "checkmate");
    } else if (g->result == RESULT_STALEMATE) {
        str_cpy(&result, "1/2-1/2");
        str_cpy(reason, "stalemate");
    } else if (g->result == RESULT_THREEFOLD) {
        str_cpy(&result, "1/2-1/2");
        str_cpy(reason, "3 repetitions");
    } else if (g->result == RESULT_FIFTY_MOVES) {
        str_cpy(&result, "1/2-1/2");
        str_cpy(reason, "50 move rule");
    } else if (g->result ==RESULT_INSUFFICIENT_MATERIAL) {
        str_cpy(&result, "1/2-1/2");
        str_cpy(reason, "insufficient material");
    } else if (g->result == RESULT_ILLEGAL_MOVE) {
        str_cpy(&result, g->pos[g->ply].turn == WHITE ? "0-1" : "1-0");
        str_cpy(reason, "illegal move");
    } else if (g->result == RESULT_DRAW_ADJUDICATION) {
        str_cpy(&result, "1/2-1/2");
        str_cpy(reason, "draw by adjudication");
    } else if (g->result == RESULT_RESIGN) {
        str_cpy(&result, g->pos[g->ply].turn == WHITE ? "0-1" : "1-0");
        str_cpy(reason, g->pos[g->ply].turn == WHITE ? "white resigns" : "black resigns");
    } else
        assert(false);

    return result;
}

str_t game_pgn(const Game *g)
{
    str_t pgn = str_new();

    str_cat_fmt(&pgn, "[White \"%S\"]\n", &g->names[WHITE]);
    str_cat_fmt(&pgn, "[Black \"%S\"]\n", &g->names[BLACK]);

    // Result in PGN format "1-0", "0-1", "1/2-1/2" (from white pov)
    str_t reason = str_new();
    str_t result = game_decode_result(g, &reason);
    str_cat_fmt(&pgn, "[Result \"%S\"]\n", &result);
    str_cat_fmt(&pgn, "[Termination \"%S\"]\n", &reason);

    str_t fen = pos_get(&g->pos[0]);
    str_cat_fmt(&pgn, "[FEN \"%S\"]\n", &fen);

    if (g->go.chess960)
        str_cat(&pgn, "[Variant \"Chess960\"]\n");

    str_cat_fmt(&pgn, "[PlyCount \"%i\"]\n\n", g->ply);

    for (int ply = 1; ply <= g->ply; ply++) {
        // Write move number
        if (g->pos[ply - 1].turn == WHITE || ply == 1)
            str_cat_fmt(&pgn, g->pos[ply - 1].turn == WHITE ? "%i. " : "%i.. ",
                g->pos[ply - 1].fullMove);

        // Prepare SAN base
        str_t san = pos_move_to_san(&g->pos[ply - 1], g->pos[ply].lastMove);

        // Append check markers to SAN
        if (g->pos[ply].checkers) {
            if (ply == g->ply && g->result == RESULT_CHECKMATE)
                str_putc(&san, '#');  // checkmate
            else
                str_putc(&san, '+');  // normal check
        }

        str_cat(&pgn, san.buf, ply % 10 == 0 ? "\n" : " ");
        str_delete(&san);
    }

    str_cat(&pgn, result.buf, "\n\n");
    str_delete(&result, &reason, &fen);
    return pgn;
}

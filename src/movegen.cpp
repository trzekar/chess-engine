#include "movegen.h"
#include "attacks.h"
#include "bitboard.h"

namespace {
// CAPTURES also includes the quiet queen promotion (see generate_captures).
enum class GenType { ALL, CAPTURES };

void add_moves(MoveList &list, Square from, Bitboard targets, MoveFlag flag) {
    while (targets) {
        list.add(Move(from, pop_lsb(targets), flag));
    }
}

// Pawns and castling have their own generators.
void generate_piece_moves(const Position &pos, MoveList &list, PieceType pt, GenType type) {
    Color piece_color = pos.side_to_move;
    Bitboard enemy_occ = pos.occupancy[~piece_color];
    Bitboard occ = pos.occupied();

    Bitboard piece_type_places = pos.pieces[piece_color][pt];

    while (piece_type_places) {
        Square s = pop_lsb(piece_type_places);
        Bitboard attacks = 0;

        switch (pt) {
        case KNIGHT:
            attacks = KNIGHT_ATTACKS[s];
            break;
        case BISHOP:
            attacks = bishop_attacks(s, occ);
            break;
        case ROOK:
            attacks = rook_attacks(s, occ);
            break;
        case QUEEN:
            attacks = queen_attacks(s, occ);
            break;
        case KING:
            attacks = KING_ATTACKS[s];
            break;
        default:
            assert(false && "pawns are generated separately");
        }

        add_moves(list, s, attacks & enemy_occ, CAPTURE);
        if (type == GenType::ALL) {
            add_moves(list, s, attacks & ~occ, QUIET);
        }
    }
}

void generate_pawn_moves(const Position &pos, MoveList &list, GenType type) {
    Color piece_color = pos.side_to_move;
    Bitboard enemy_occ = pos.occupancy[~piece_color];
    Bitboard occ = pos.occupied();

    int forward = piece_color == WHITE ? 1 : -1;
    int start_rank = piece_color == WHITE ? 1 : 6;
    int promotion_rank = piece_color == WHITE ? 7 : 0;

    Bitboard pawn_places = pos.pieces[piece_color][PAWN];

    while (pawn_places) {
        Square s = pop_lsb(pawn_places);
        Square push = make_square(file_of(s), rank_of(s) + forward);
        Bitboard captures = PAWN_ATTACKS[piece_color][s] & enemy_occ;
        bool push_free = !(occ & square_bb(push));

        // CAPTURES keeps every capture promotion but only the quiet queen promotion: a quiet
        // underpromotion almost never changes the score.
        if (rank_of(push) == promotion_rank) {
            for (MoveFlag flag :
                 {KNIGHT_PROMOTION, BISHOP_PROMOTION, ROOK_PROMOTION, QUEEN_PROMOTION}) {
                if (push_free && (type == GenType::ALL || flag == QUEEN_PROMOTION))
                    list.add(Move(s, push, flag));
                add_moves(list, s, captures, MoveFlag(flag | CAPTURE));
            }
            continue;
        }

        add_moves(list, s, captures, CAPTURE);

        if (type == GenType::CAPTURES || !push_free) {
            continue;
        }

        list.add(Move(s, push, QUIET));

        // The square in front is already known to be empty, so only the target is left to check.
        if (rank_of(s) == start_rank) {
            Square double_push = make_square(file_of(s), rank_of(s) + 2 * forward);
            if (!(occ & square_bb(double_push)))
                list.add(Move(s, double_push, DOUBLE_PUSH));
        }
    }

    // Our pawns that can capture en passant stand exactly on the squares that an enemy pawn
    // on the en passant square would attack.
    if (pos.ep_square != NO_SQUARE) {
        Bitboard ep_attackers =
            PAWN_ATTACKS[~piece_color][pos.ep_square] & pos.pieces[piece_color][PAWN];
        while (ep_attackers) {
            list.add(Move(pop_lsb(ep_attackers), pos.ep_square, EN_PASSANT_CAPTURE));
        }
    }
}

void generate_castling(const Position &pos, MoveList &list) {
    Color piece_color = pos.side_to_move;
    Color enemy_color = ~piece_color;
    Bitboard occ = pos.occupied();

    struct Castle {
        CastlingRight right;
        Square king_from;
        Square king_crosses;
        Square king_to;
        Bitboard must_be_empty;
        MoveFlag flag;
    };

    // The king's destination is not checked here: make_move rejects every move that leaves
    // the king in check. b1 and b8 must be empty but may be attacked, as only the rook crosses
    // them. A castling right implies the king and rook are still on their starting squares.
    static constexpr Castle CASTLES[COLOR_NB][2] = {
        {{WHITE_KINGSIDE, E1, F1, G1, square_bb(F1) | square_bb(G1), KING_CASTLE},
         {WHITE_QUEENSIDE, E1, D1, C1, square_bb(D1) | square_bb(C1) | square_bb(B1),
          QUEEN_CASTLE}},
        {{BLACK_KINGSIDE, E8, F8, G8, square_bb(F8) | square_bb(G8), KING_CASTLE},
         {BLACK_QUEENSIDE, E8, D8, C8, square_bb(D8) | square_bb(C8) | square_bb(B8),
          QUEEN_CASTLE}},
    };

    for (const Castle &c : CASTLES[piece_color]) {
        if (!(pos.castling_rights & c.right) || (occ & c.must_be_empty))
            continue;
        if (pos.is_attacked(c.king_from, enemy_color) ||
            pos.is_attacked(c.king_crosses, enemy_color))
            continue;
        list.add(Move(c.king_from, c.king_to, c.flag));
    }
}

} // namespace

void generate_moves(const Position &pos, MoveList &list) {
    generate_piece_moves(pos, list, KING, GenType::ALL);
    generate_piece_moves(pos, list, QUEEN, GenType::ALL);
    generate_piece_moves(pos, list, ROOK, GenType::ALL);
    generate_piece_moves(pos, list, BISHOP, GenType::ALL);
    generate_piece_moves(pos, list, KNIGHT, GenType::ALL);
    generate_pawn_moves(pos, list, GenType::ALL);
    generate_castling(pos, list);
}

void generate_captures(const Position &pos, MoveList &list) {
    generate_piece_moves(pos, list, KING, GenType::CAPTURES);
    generate_piece_moves(pos, list, QUEEN, GenType::CAPTURES);
    generate_piece_moves(pos, list, ROOK, GenType::CAPTURES);
    generate_piece_moves(pos, list, BISHOP, GenType::CAPTURES);
    generate_piece_moves(pos, list, KNIGHT, GenType::CAPTURES);
    generate_pawn_moves(pos, list, GenType::CAPTURES);
}

std::string to_uci(Move m) {
    // UCI writes the null move as 0000.
    if (m == Move())
        return "0000";

    std::string uci;
    for (Square s : {m.from(), m.to()}) {
        uci += char('a' + file_of(s));
        uci += char('1' + rank_of(s));
    }
    if (m.is_promotion())
        uci += "nbrq"[m.promotion_type() - KNIGHT];
    return uci;
}

Move parse_uci_move(const Position &pos, std::string_view uci) {
    MoveList list;
    generate_moves(pos, list);
    for (const Move m : list) {
        if (to_uci(m) == uci)
            return m;
    }
    return Move();
}
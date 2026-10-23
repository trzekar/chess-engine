#include "position.h"
#include "attacks.h"
#include "zobrist.h"

#include <charconv>
#include <iostream>
#include <iterator>
#include <vector>

constexpr std::string_view PIECE_CHARS = "PNBRQKpnbrqk";
constexpr std::string_view WHITESPACE = " \t\r\n";

struct CastlingInfo {
    char symbol;
    CastlingRight right;
    Color color;
    Square king;
    Square rook;
};

constexpr CastlingInfo CASTLING_INFO[] = {
    {'K', WHITE_KINGSIDE, WHITE, E1, H1},
    {'Q', WHITE_QUEENSIDE, WHITE, E1, A1},
    {'k', BLACK_KINGSIDE, BLACK, E8, H8},
    {'q', BLACK_QUEENSIDE, BLACK, E8, A8},
};

// Every move ANDs the masks of its two squares into castling_rights. `from` covers the
// own king or rook leaving home, `to` covers a rook captured on its home square.
// A1 loses Q, E1 loses K and Q, H1 loses K
// A8 loses q, E8 loses k and q, H8 loses k
// Other squares don't cause loss of any castling rights.
const uint8_t CASTLING_MASK[64] = {13, 15, 15, 15, 12, 15, 15, 14, 15, 15, 15, 15, 15, 15, 15, 15,
                                   15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                                   15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                                   15, 15, 15, 15, 15, 15, 15, 15, 7,  15, 15, 15, 3,  15, 15, 11};

static std::vector<std::string_view> split_fields(std::string_view text) {
    std::vector<std::string_view> fields;
    size_t start = text.find_first_not_of(WHITESPACE);
    while (start != std::string_view::npos) {
        const size_t end = text.find_first_of(WHITESPACE, start);
        fields.push_back(text.substr(start, end - start));
        start = text.find_first_not_of(WHITESPACE, end);
    }
    return fields;
}

static bool parse_board(std::string_view text, Position &pos) {
    int rank = 7;
    int file = 0;
    for (char ch : text) {
        if (ch == '/') {
            if (file != 8 || rank == 0)
                return false;
            --rank;
            file = 0;
        } else if (ch >= '1' && ch <= '8') {
            file += ch - '0';
            if (file > 8)
                return false;
        } else {
            const size_t index = PIECE_CHARS.find(ch);
            if (index == std::string_view::npos || file >= 8)
                return false;
            pos.put_piece(Color(index / PIECE_TYPE_NB), PieceType(index % PIECE_TYPE_NB),
                          make_square(file, rank));
            ++file;
        }
    }
    if (rank != 0 || file != 8)
        return false;
    if (popcount(pos.pieces[WHITE][KING]) != 1 || popcount(pos.pieces[BLACK][KING]) != 1)
        return false;
    const Bitboard pawns = pos.pieces[WHITE][PAWN] | pos.pieces[BLACK][PAWN];
    return (pawns & (RANK_1 | RANK_8)) == 0;
}

static bool parse_side(std::string_view text, Position &pos) {
    if (text == "w")
        pos.side_to_move = WHITE;
    else if (text == "b")
        pos.side_to_move = BLACK;
    else
        return false;
    return true;
}

static bool parse_castling(std::string_view text, Position &pos) {
    if (text == "-")
        return true;
    size_t next = 0;
    for (char ch : text) {
        while (next < std::size(CASTLING_INFO) && CASTLING_INFO[next].symbol != ch)
            ++next;
        if (next == std::size(CASTLING_INFO))
            return false;
        const CastlingInfo &info = CASTLING_INFO[next++];
        const bool king_home = pos.pieces[info.color][KING] & square_bb(info.king);
        const bool rook_home = pos.pieces[info.color][ROOK] & square_bb(info.rook);
        if (!king_home || !rook_home)
            return false;
        pos.castling_rights = uint8_t(pos.castling_rights | info.right);
    }
    return true;
}

static bool parse_en_passant(std::string_view text, Position &pos) {
    if (text == "-")
        return true;
    if (text.size() != 2)
        return false;
    const int file = text[0] - 'a';
    const int rank = text[1] - '1';
    const bool white_to_move = pos.side_to_move == WHITE;
    if (file < 0 || file > 7 || rank != (white_to_move ? 5 : 2))
        return false;
    const Square target = make_square(file, rank);
    const Square pawn = make_square(file, white_to_move ? 4 : 3);
    if (pos.piece_at(target) != NO_PIECE_TYPE)
        return false;
    if (!(pos.pieces[~pos.side_to_move][PAWN] & square_bb(pawn)))
        return false;
    pos.ep_square = target;
    return true;
}

static bool parse_number(std::string_view text, int min, int max, int &value) {
    const char *end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    return ec == std::errc() && ptr == end && value >= min && value <= max;
}

static char piece_char(const Position &pos, Square s) {
    const PieceType pt = pos.piece_at(s);
    if (pt == NO_PIECE_TYPE)
        return '.';
    const Color c = (pos.occupancy[WHITE] & square_bb(s)) ? WHITE : BLACK;
    return PIECE_CHARS[size_t(c) * PIECE_TYPE_NB + size_t(pt)];
}

bool parse_fen(std::string_view fen, Position &pos) {
    const std::vector<std::string_view> fields = split_fields(fen);
    if (fields.size() != 4 && fields.size() != 6)
        return false;

    Position result;
    if (!parse_board(fields[0], result) || !parse_side(fields[1], result) ||
        !parse_castling(fields[2], result) || !parse_en_passant(fields[3], result))
        return false;

    if (fields.size() == 6) {
        int half_moves = 0;
        int moves = 0;
        if (!parse_number(fields[4], 0, UINT8_MAX, half_moves) ||
            !parse_number(fields[5], 1, UINT16_MAX, moves))
            return false;
        result.half_move_counter = uint8_t(half_moves);
        result.move_number = uint16_t(moves);
    }

    result.hash = compute_hash(result);
    pos = result;
    return true;
}

std::string to_fen(const Position &pos) {
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const char ch = piece_char(pos, make_square(file, rank));
            if (ch == '.') {
                ++empty;
                continue;
            }
            if (empty > 0)
                fen += char('0' + empty);
            empty = 0;
            fen += ch;
        }
        if (empty > 0)
            fen += char('0' + empty);
        if (rank > 0)
            fen += '/';
    }

    fen += pos.side_to_move == WHITE ? " w " : " b ";

    std::string castling;
    for (const CastlingInfo &info : CASTLING_INFO)
        if (pos.castling_rights & info.right)
            castling += info.symbol;
    fen += castling.empty() ? "-" : castling;

    fen += ' ';
    if (pos.ep_square == NO_SQUARE) {
        fen += '-';
    } else {
        fen += char('a' + file_of(pos.ep_square));
        fen += char('1' + rank_of(pos.ep_square));
    }

    fen += ' ' + std::to_string(pos.half_move_counter);
    fen += ' ' + std::to_string(pos.move_number);
    return fen;
}

void print_position(const Position &pos) {
    for (int rank = 7; rank >= 0; --rank) {
        std::cout << rank + 1 << ' ';
        for (int file = 0; file < 8; ++file)
            std::cout << piece_char(pos, make_square(file, rank)) << ' ';
        std::cout << '\n';
    }
    std::cout << "  a b c d e f g h\n";
    std::cout << to_fen(pos) << '\n';
}

bool Position::is_attacked(Square s, Color by) const {
    if (KNIGHT_ATTACKS[s] & pieces[by][KNIGHT])
        return true;

    Bitboard occ = occupied();

    Bitboard rooks_and_queens = (pieces[by][ROOK] | pieces[by][QUEEN]);
    if (rook_attacks(s, occ) & rooks_and_queens)
        return true;

    Bitboard bishops_and_queens = (pieces[by][BISHOP] | pieces[by][QUEEN]);
    if (bishop_attacks(s, occ) & bishops_and_queens)
        return true;

    if (KING_ATTACKS[s] & pieces[by][KING])
        return true;

    if (PAWN_ATTACKS[~by][s] & pieces[by][PAWN])
        return true;

    return false;
}

bool Position::in_check(Color c) const {
    Bitboard king_bb = pieces[c][KING];
    return is_attacked(lsb(king_bb), ~c);
}

bool Position::make_move(Move m) {
    Color us = side_to_move;
    Color them = ~side_to_move;
    Square from = m.from();
    Square to = m.to();
    PieceType pt = piece_at(from);
    MoveFlag flag = m.flag();

    // Every move clears the old en passant square and counts toward the fifty-move rule.
    // A double push sets a new square below, a capture or a pawn move resets the counter.
    if (ep_square != NO_SQUARE) {
        hash ^= ZOBRIST.ep_file[file_of(ep_square)];
    }
    ep_square = NO_SQUARE;
    half_move_counter++;

    if (m.is_capture()) {
        if (flag == EN_PASSANT_CAPTURE) {
            // The only capture whose victim is not on `to`: the pawn stands on the file of `to`
            // and the rank of `from`.
            Square captured_s = make_square(file_of(to), rank_of(from));
            hash ^= ZOBRIST.piece[them][PAWN][captured_s];
            remove_piece(them, piece_at(captured_s), captured_s);
        } else {
            hash ^= ZOBRIST.piece[them][piece_at(to)][to];
            remove_piece(them, piece_at(to), to);
        }
        half_move_counter = 0;
    }

    hash ^= ZOBRIST.piece[us][pt][from];
    remove_piece(us, pt, from);

    if (m.is_promotion()) {
        hash ^= ZOBRIST.piece[us][m.promotion_type()][to];
        put_piece(us, m.promotion_type(), to);
    } else {
        hash ^= ZOBRIST.piece[us][pt][to];
        put_piece(us, pt, to);
    }

    // The king has already moved like any other piece; castling only adds the rook's jump.
    if (flag == QUEEN_CASTLE) {
        Square from_sq = make_square(0, rank_of(from));
        Square to_sq = make_square(3, rank_of(from));
        hash ^= ZOBRIST.piece[us][ROOK][from_sq];
        hash ^= ZOBRIST.piece[us][ROOK][to_sq];
        remove_piece(us, ROOK, from_sq);
        put_piece(us, ROOK, to_sq);
    } else if (flag == KING_CASTLE) {
        Square from_sq = make_square(7, rank_of(from));
        Square to_sq = make_square(5, rank_of(from));
        hash ^= ZOBRIST.piece[us][ROOK][from_sq];
        hash ^= ZOBRIST.piece[us][ROOK][to_sq];
        remove_piece(us, ROOK, from_sq);
        put_piece(us, ROOK, to_sq);
    }

    if (pt == PAWN) {
        half_move_counter = 0;
    }

    // The en passant square is the one the pawn skipped, halfway between `from` and `to`.
    if (flag == DOUBLE_PUSH) {
        ep_square = Square((from + to) / 2);
        hash ^= ZOBRIST.ep_file[file_of(ep_square)];
    }

    hash ^= ZOBRIST.castling[castling_rights];
    castling_rights &= CASTLING_MASK[from] & CASTLING_MASK[to];
    hash ^= ZOBRIST.castling[castling_rights];

    if (us == BLACK) {
        move_number++;
    }

    hash ^= ZOBRIST.side;
    side_to_move = ~side_to_move;

    return !is_attacked(lsb(pieces[us][KING]), them);
}

void Position::make_null_move() {
    if (ep_square != NO_SQUARE) {
        hash ^= ZOBRIST.ep_file[file_of(ep_square)];
        ep_square = NO_SQUARE;
    }

    if (side_to_move == BLACK) {
        move_number++;
    }

    hash ^= ZOBRIST.side;
    side_to_move = ~side_to_move;

    // Zeroing the counter stops the repetition scan of alphabeta at this null move: it looks back
    // only half_move_counter plies, and a position "repeated" across a pass is no real repetition,
    // as a game cannot pass. The cost: below the null move the fifty-move rule counts from zero,
    // and a search almost never reaches 100 plies past it.
    half_move_counter = 0;
}
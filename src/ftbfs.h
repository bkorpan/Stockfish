#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>

#include "position.h"
#include "movepick.h"
#include "types.h"
#include "evaluate.h"
#include "misc.h"

using namespace Stockfish;
using Eval::evaluate;

enum NodeType { NonPV, PV, Root };

template <NodeType nodeType>
  Value qsearch_ftbfs(Position& pos, Value alpha, Value beta, Depth depth, int ply, Move& prevMove) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(depth <= 0);

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    Move move, bestMove;
    Value bestValue, value, futilityValue, futilityBase;
    bool givesCheck, capture;
    int moveCount;

    bestMove = MOVE_NONE;
    moveCount = 0;

    // Check for an immediate draw or maximum ply reached
    if (   pos.is_draw(ply)
        || ply >= MAX_PLY)
        return (ply >= MAX_PLY && !pos.checkers()) ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ply && ply < MAX_PLY);

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions, and other checks (only if depth >= DEPTH_QS_CHECKS)
    // will be generated.
    Square prevSq = to_sq(prevMove);
    MovePicker_ftbfs mp(pos, depth, prevSq);

    int quietCheckEvasions = 0;

    // Loop through the moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move()) != MOVE_NONE)
    {
      assert(is_ok(move));

      // Check for legality
      if (!pos.legal(move))
          continue;

      givesCheck = pos.gives_check(move);
      capture = pos.capture(move);

      moveCount++;

      // Futility pruning and moveCount pruning (~5 Elo)
      if (    bestValue > VALUE_TB_LOSS_IN_MAX_PLY
          && !givesCheck
          &&  to_sq(move) != prevSq
          &&  futilityBase > -VALUE_KNOWN_WIN
          &&  type_of(move) != PROMOTION)
      {

          if (moveCount > 2)
              continue;

          futilityValue = futilityBase + PieceValue[EG][pos.piece_on(to_sq(move))];

          if (futilityValue <= alpha)
          {
              bestValue = std::max(bestValue, futilityValue);
              continue;
          }

          if (futilityBase <= alpha && !pos.see_ge(move, VALUE_ZERO + 1))
          {
              bestValue = std::max(bestValue, futilityBase);
              continue;
          }
      }

      // Do not search moves with negative SEE values (~5 Elo)
      if (    bestValue > VALUE_TB_LOSS_IN_MAX_PLY
          && !pos.see_ge(move))
          continue;

      // movecount pruning for quiet check evasions
      if (   bestValue > VALUE_TB_LOSS_IN_MAX_PLY
          && quietCheckEvasions > 1
          && !capture
          && pos.checkers())
          continue;

      quietCheckEvasions += !capture && pos.checkers();

      // Make and search the move
      pos.do_move(move, st, givesCheck);
      value = -qsearch_ftbfs<PV>(pos, -beta, -alpha, depth - 1, ply, move);
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Check for a new best move
      if (value > bestValue)
      {
          bestValue = value;

          if (value > alpha)
          {
              bestMove = move;

              if (PvNode && value < beta) // Update alpha here!
                  alpha = value;
              else
                  break; // Fail high
          }
       }
    }

    // All legal moves have been searched. A special case: if we're in check
    // and no legal moves were found, it is checkmate.
    if (pos.checkers() && bestValue == -VALUE_INFINITE)
    {
        assert(!MoveList<LEGAL>(pos).size());

        return mated_in(ply); // Plies to mate from the root
    }

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }

class Node {
private:
  int num_edges;

  Node* parent;

  Node** edges;
  Value* values;
  Move* moves;

  // Use qsearch to put initial valuations on edges
  void init_edge_values(Position& pos, int ply) {
    for (int i = 0; i < num_edges; i++) {
      StateInfo st;
      pos.do_move(moves[i], st);
      values[i] = qsearch_ftbfs<PV>(pos, -VALUE_INFINITE, VALUE_INFINITE, 0, ply, moves[i]);
      pos.undo_move(moves[i]);
    }
  }

  int get_best_idx() {
    Value best_value = -VALUE_INFINITE;
    int best_idx = 0;
    for (int i = 0; i < num_edges; i++) {
      if (values[i] > best_value) {
        best_value = values[i];
        best_idx = i;
      }
    }
    return best_idx;
  }

  void update_value(Value updated_value) {
    values[get_best_idx()] = updated_value;
  }

public:

  Node(Position &pos, Node* parent, int ply) {
    // Generate legal moves from this node
    ExtMove moves_array[121];
    ExtMove* moves_start = moves_array;
    ExtMove* moves_end = NULL;
    generate<LEGAL>(pos, moves_start);

    // Set values and do allocations
    num_edges = (int)(moves_end - moves_start) / sizeof(ExtMove);

    // Do allocations
    edges = (Node**)malloc(num_edges * sizeof(Node*));
    values = (Value*)malloc(num_edges * sizeof(Value));
    moves = (Move*)malloc(num_edges * sizeof(Move));

    // Initialize arrays
    memset(edges, 0, num_edges * sizeof(Node*));
    init_edge_values(pos, ply);
    for (int i = 0; i < num_edges; i++) {
      moves[i] = moves_array[i].move;
    }

    this->parent = parent;
  }

  ~Node() {
    for (int i = 0; i < num_edges; i++) {
      delete edges[i];
    }
    free(edges);
    free(values);
    free(moves);
  }

  Move get_best_move() {
    return moves[get_best_idx()];
  }

  Node* expand_best(Position &pos, int ply) {
    StateInfo st;
    pos.do_move(moves[get_best_idx()], st);
    return new Node(pos, this, ply);
  }

  Value get_value() {
    return values[get_best_idx()];
  }

  Value get_second_best_value() {
    Value best_value = -VALUE_INFINITE;
    Value second_best_value = -VALUE_INFINITE;
    for (int i = 0; i < num_edges; i++) {
      if (values[i] > best_value) {
        second_best_value = best_value;
        best_value = values[i];
      } else if (values[i] > second_best_value) {
        second_best_value = values[i];
      }
    }
    return second_best_value;
  }

  Node* backtrack(Position &pos, Value updated_value) {
    pos.undo_move(moves[get_best_idx()]);
    parent->update_value(updated_value);
    return parent;
  }

  Node* get_best_child() {
    return edges[get_best_idx()];
  }

  int get_pv_depth() {
    Node* best_child = get_best_child();
    if (best_child != NULL) {
      return best_child->get_pv_depth() + 1;
    } else {
      return 0;
    }
  }
};

void swap_and_negate(Value &a, Value &b) {
  Value tmp = -b;
  b = -a;
  a = tmp;
}

Node* ftbfs(Position& pos, int n) {
  // Root node
  Node* root = new Node(pos, NULL, 0);
  Node* node = root;

  // Initialize values
  Value value = -VALUE_INFINITE;
  Value alpha = -VALUE_INFINITE;
  Value beta = VALUE_INFINITE;
  Value epsilon = static_cast<Value>(100);

  int d = 0;

  // Search loop
  for (int i = 0; i < n; i++) {
    // Expand best move of current best node
    node = node->expand_best(pos, d);
    value = node->get_value();
    d++;

    // Backtrack if needed, otherwise update alpha
    if (value > beta + epsilon || value < alpha - epsilon) {
      while (value != alpha) {
        value = -value;
        swap_and_negate(alpha, beta);
        node = node->backtrack(pos, value);
        if (node->get_value() == alpha) {
          value = alpha;
        }
        d--;
      }
      alpha = -VALUE_INFINITE;
      Node* path = root;
      for (int k = 0; path != node; path = path->get_best_child(), k++) {
        if ((k & 1) == (d & 1)) {
          alpha = std::max(alpha, path->get_value());
        }
      }
    } else if (alpha < node->get_second_best_value()) {
      alpha = node->get_second_best_value();
    }
  }

  return root;
}


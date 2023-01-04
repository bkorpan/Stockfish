#include "ftbfs.h"

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

    value = -VALUE_INFINITE;
    bestValue = -VALUE_INFINITE;
    bestMove = MOVE_NONE;
    moveCount = 0;

    // Check for an immediate draw or maximum ply reached
    if (   pos.is_draw(ply)
        || ply >= MAX_PLY)
        return (ply >= MAX_PLY && !pos.checkers()) ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ply && ply < MAX_PLY);

    // Evaluate the position statically
    if (pos.checkers())
    {
        bestValue = futilityBase = -VALUE_INFINITE;
    }
    else
    {
        // In case of null move search use previous static eval with a different sign
        bestValue = evaluate(pos);

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            return bestValue;
        }

        if (PvNode && bestValue > alpha)
            alpha = bestValue;

        futilityBase = bestValue + 118;
    }

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
      value = -qsearch_ftbfs<PV>(pos, -beta, -alpha, depth - 1, ply + 1, move);
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

// Use qsearch to put initial valuations on edges
void Node::init_edge_values(Position& pos, int ply) {
  for (int i = 0; i < num_edges; i++) {
    StateInfo st;
    pos.do_move(moves[i], st);
    values[i] = -qsearch_ftbfs<PV>(pos, -VALUE_INFINITE, VALUE_INFINITE, 0, ply + 1, moves[i]);
    pos.undo_move(moves[i]);
  }
}


void swap_and_negate(Value& a, Value& b) {
  Value tmp = -b;
  b = -a;
  a = tmp;
}

Node* ftbfs(Node* root, Position& pos, const int n, int& maxDepth) {
  // Initialize values
  Node* node = root;

  Value value = root->get_value();
  Value alpha = root->get_second_best_value();
  Value beta = VALUE_INFINITE;
  Value epsilon = static_cast<Value>(0);

  int d = 0;

  // Search loop
  for (int i = 0; i < n; i++) {
    // Expand best move of current best node
    d++;
    if (!node->get_num_edges()) {
      return root;
    } else {
      node = node->expand_best(pos, d);
    }
    value = node->get_value();
    swap_and_negate(alpha, beta);
    maxDepth = std::max(maxDepth, d);

    // Backtrack if needed
    if (value > beta + epsilon || value < alpha - epsilon) {
      while (value != alpha) {
        swap_and_negate(alpha, beta);
        node = node->backtrack(pos);
        node->update_value(-value);
        value = node->get_value();
        d--;
      }
      alpha = -VALUE_INFINITE;
      beta = VALUE_INFINITE;
      for (Node* path = root; path != node; path = path->get_best_child()) {
        alpha = std::max(alpha, path->get_second_best_value());
        swap_and_negate(alpha, beta);
      }
    }

    // Update alpha
    if (alpha < node->get_second_best_value()) {
      alpha = node->get_second_best_value();
    }
  }

  // Backtrack to root
  while (node != root) {
    node = node->backtrack(pos);
  }

  return root;
}


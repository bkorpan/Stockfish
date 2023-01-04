#ifndef FTBFS_H
#define FTBFS_H

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>

#include "position.h"
#include "movepick.h"
#include "types.h"
#include "evaluate.h"
#include "misc.h"
#include "search.h"

using namespace Stockfish;
using Eval::evaluate;


class Node {
private:
  int num_edges;

  Node* parent;

  Node** edges;
  Move* moves;
  Value* values;

  StateInfo stateInfo;

  void init_edge_values(Position& pos, int ply);

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

public:

  Node(Position &pos, Node* parent, int ply) {
    // Generate legal moves from this node
    ExtMove moves_array[121];
    ExtMove* moves_start = moves_array;
    ExtMove* moves_end = generate<LEGAL>(pos, moves_start);

    // Set values and do allocations
    num_edges = (int)(moves_end - moves_start);

    // Do allocations
    edges = (Node**)malloc(num_edges * sizeof(Node*));
    moves = (Move*)malloc(num_edges * sizeof(Move));
    values = (Value*)malloc(num_edges * sizeof(Value));

    // Initialize arrays
    memset(edges, 0, num_edges * sizeof(Node*));
    for (int i = 0; i < num_edges; i++) {
      moves[i] = moves_array[i].move;
    }
    init_edge_values(pos, ply);

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
    const int best_idx = get_best_idx();
    pos.do_move(moves[best_idx], stateInfo);
    if (edges[best_idx] == NULL) {
      edges[best_idx] = new Node(pos, this, ply);
    }
    return edges[best_idx];
  }

  Value get_value() {
    if (num_edges > 0) {
      return values[get_best_idx()];
    } else {
      return -parent->get_value();
    }
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

  void update_value(Value updated_value) {
    values[get_best_idx()] = updated_value;
  }

  Node* backtrack(Position &pos) {
    pos.undo_move(parent->get_best_move());
    return parent;
  }

  Node* get_best_child() {
    return edges[get_best_idx()];
  }

  int get_pv_depth() {
    if (num_edges > 0) {
      Node* best_child = get_best_child();
      if (best_child != NULL)
        return best_child->get_pv_depth() + 1;
      else
        return 0;
    } else {
      return 0;
    }
  }

  int get_num_edges() {
    return num_edges;
  }
};

Node* ftbfs(Node* root, Position& pos, const int n, int& maxDepth);

#endif

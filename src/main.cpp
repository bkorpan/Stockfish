/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>

#include "bitboard.h"
#include "endgame.h"
#include "position.h"
#include "psqt.h"
#include "search.h"
#include "syzygy/tbprobe.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "ftbfs.h"

using namespace Stockfish;
using namespace std;

int main(int argc, char* argv[]) {

  char fen[100];
  int nodes;

  cout << "Position FEN: ";
  cin >> fen;
  cout << "Nodes to search: ";
  cin >> nodes;

  cout << "ass\n";

  Position pos;
  StateInfo st;
  pos.set(fen, false, &st, NULL);

  Node* root = ftbfs(pos, nodes);
  Move best_move = root->get_best_move();

  cout << "Best move: " << UCI::move(best_move, false) << endl;
  cout << "Depth of PV: " << root->get_pv_depth() << endl;


  /*std::cout << engine_info() << std::endl;

  CommandLine::init(argc, argv);
  UCI::init(Options);
  Tune::init();
  PSQT::init();
  Bitboards::init();
  Position::init();
  Bitbases::init();
  Endgames::init();
  Threads.set(size_t(Options["Threads"]));
  Search::clear(); // After threads are up
  Eval::NNUE::init();

  UCI::loop(argc, argv);

  Threads.set(0);*/
  return 0;
}

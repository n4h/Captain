/*
Copyright 2022, Narbeh Mouradian

Captain is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Captain is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.

*/

#ifndef UCI_H
#define UCI_H

#include <vector>
#include <string>
#include <atomic>
#include <future>

#include "board.hpp"
#include "engine.hpp"
#include "searchflags.hpp"


// from Stockfish
enum SyncCout {ioLock, ioUnlock};
std::ostream& operator<<(std::ostream&, SyncCout);
#define sync_cout std::cout << ioLock
#define sync_endl std::endl << ioUnlock

namespace uci
{
	class UCIProtocol
	{
	public:
		void UCIStartup();
		void UCIStartLoop();
		UCIProtocol() {};
	private:
		void UCIPositionCommand(const std::vector<std::string>&);
		void UCIGoCommand(const std::vector<std::string>&);
		void UCIStopCommand();
		std::string UCIName = "Captain";
		std::string UCIAuthor = "Narbeh Mouradian";
		bool bitboardsInitialized = false;
		board::Board b;
		engine::Engine e;
		std::future<void> engineResult;
		searchFlags sf;

		friend class engine::Engine;
	};

	board::Move uciMove2boardMove(const board::Board&, const std::string&);
}


#endif

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

#ifndef ENGINE_H
#define ENGINE_H

#include <algorithm>
#include <limits>
#include <utility>
#include <cstdint>
#include <chrono>
#include <atomic>
#include <iterator>
#include <array>
#include <string>

#include "board.hpp"
#include "movegen.hpp"
#include "eval.hpp"
#include "auxiliary.hpp"
#include "searchFlags.hpp"
#include "transpositiontable.hpp"

#define MAKE_MOVE_AND_UPDATE_HASH(Move, Board, Null) hash ^= tt->incrementalUpdatePre(Move, Board, Null);\
Board.makeMove<wToMove, Null>(Move);\
hash ^= tt->incrementalUpdatePost(Move, Board, Null);

namespace engine
{
	using namespace std::literals::chrono_literals;

	// 500000 is arbitrary 
	constexpr auto negInf = -500000;
	constexpr auto posInf = 500000;

	struct SearchSettings
	{
		std::size_t maxDepth = std::numeric_limits<std::size_t>::max();
		std::size_t maxNodes = std::numeric_limits<std::size_t>::max();
		std::size_t movestogo = std::numeric_limits<std::size_t>::max();
		bool infiniteSearch = false;
		bool ponder = false;
		std::chrono::milliseconds maxTime = std::chrono::milliseconds::max();
		std::chrono::milliseconds wmsec = 0ms;
		std::chrono::milliseconds bmsec = 0ms;
		std::chrono::milliseconds winc = 0ms;
		std::chrono::milliseconds binc = 0ms;
	};

	class Engine
	{
	public:
		void playBestMove(const board::Board& bCopy, std::chrono::time_point<std::chrono::steady_clock> s);
		double getEval();
		Engine() {}
		void setSettings(SearchSettings ss) noexcept { settings = ss; }
		void setTTable(TTable::TTable*);
	private:
		std::string move2uciFormat(board::Move);
		SearchSettings settings;
		std::chrono::time_point<std::chrono::steady_clock> searchStart;
		std::size_t nodes = 0;
		std::size_t hash = 0;
		std::size_t currIDdepth = 0;
		bool engineW = true;
		board::QBB b;
		std::chrono::milliseconds moveTime = 0ms;
		TTable::TTable* tt = nullptr;
		bool shouldStop() noexcept;
		void initialHash();

		enum SearchType {ABSearch, QSearch};

		template<bool wToMove>
		board::Move rootSearch()
		{
			std::array<board::Move, 256> moves;
			std::size_t j = movegen::genMoves<wToMove>(b, moves, 0);

			for (std::size_t i = 0; i != j; ++i)
			{
				rootMoves[i].first = moves[i];
				rootMoves[i].second = negInf;
			}

			std::int32_t worstCase = negInf;

			for (unsigned int k = 1; k <= posInf; ++k)
			{
				sync_cout << "info string iterative deepening " << k << sync_endl;
				currIDdepth = k;
				worstCase = negInf;
				std::int32_t score = negInf;

				for (std::size_t i = 0; i != j; ++i)
				{
					if (!searchFlags::searching.test())
					{
						return rootMoves[0].first;
					}
					auto oldHash = hash;
					MAKE_MOVE_AND_UPDATE_HASH(rootMoves[i].first, b, false);

					sync_cout << "info currmove " << move2uciFormat(rootMoves[i].first) << sync_endl;
					sync_cout << "info nodes " << nodes << sync_endl;
					rootMoves[i].second = -1 * alphaBetaSearch<!wToMove, ABSearch>(negInf, -1 * worstCase, k - 1);
					score = std::max(score, rootMoves[i].second);
					b.unmakeMove<wToMove>(rootMoves[i].first);
					hash = oldHash;
					if (score > worstCase)
						worstCase = score;
				}

				std::stable_sort(rootMoves.begin(), rootMoves.begin() + j, [](const auto& a, const auto& b) {
					return a.second > b.second;
					});
			}
			return rootMoves[0].first;
		}

		template<bool wToMove, SearchType s, bool prevMoveNull = false>
		std::int32_t alphaBetaSearch(std::int32_t alpha, std::int32_t beta, int depth)
		{
			++nodes;
			const auto oldAlpha = alpha;
			if (shouldStop())
				searchFlags::searching.clear();
			std::int32_t checkPos;
			if ((b.flags & board::Board::ply50FlagMask) == 50)
				return 0;
			if constexpr (s == ABSearch)
			{
				if (depth <= 0)
					return alphaBetaSearch<wToMove, QSearch>(alpha, beta, depth - 1);
			}
			else // s == QSearch
			{
				checkPos = eval::evaluate<wToMove>(b);
				if (checkPos > beta)
					return checkPos;
				alpha = std::max(alpha, checkPos);
			}

			if (tt != nullptr)
				if ((*tt)[hash].key == hash)
					if ((*tt)[hash].depth > depth)
					{
						auto nt = (*tt)[hash].nodeType;
						auto e = (*tt)[hash].eval;
						if constexpr (wToMove)
						{
							if (nt == 0)
								return e;
							else if (nt == 1 && e < alpha)
								return e;
							else if (nt == 2 && e > beta)
								return e;
						}
						else
						{
							if (nt == 0)
								return -1 * e;
							else if (nt == 1 && e < alpha)
								return -1 * e;
							else if (nt == 2 && e > beta)
								return -1 * e;
						}
					}


			std::array<board::Move, 256> moves;

			std::size_t j = movegen::genMoves<wToMove>(b, moves, 0);

			if constexpr (s == ABSearch)
			{
				std::sort(moves.begin(), moves.begin() + j, [](const auto& a, const auto& b) {
					return a > b;
					});
			}
			else // s == QSearch
			{
				auto new_j = std::remove_if(moves.begin(), moves.begin() + j, [](auto k) {return !board::isCapture(k); });
				j = std::distance(moves.begin(), new_j);

				std::sort(moves.begin(), moves.begin() + j, [this](const auto& a, const auto& c) {
					const auto atA = aux::setbit(board::getMoveInfo<constants::fromMask>(a));
					const auto atC = aux::setbit(board::getMoveInfo<constants::fromMask>(c));

					return (eval::getCaptureValue(a) - board::getPieceValue(b.getPieceType(atA)))
					> (eval::getCaptureValue(c) - board::getPieceValue(b.getPieceType(atC)));
					});
			}

			std::int32_t currEval;
			if constexpr (s == ABSearch)
			{
				currEval = negInf;
			}
			else // s == QSearch
			{
				currEval = alpha;
			}
			if constexpr (s == ABSearch && !prevMoveNull)
			{
				board::Move m = b.getHeading();
				auto oldHash = hash;
				MAKE_MOVE_AND_UPDATE_HASH(m, b, true);
				currEval = std::max(currEval, -1 * alphaBetaSearch<!wToMove, ABSearch, true>(-1 * beta, -1 * alpha, depth - 2));
				b.unmakeMove<wToMove, true>(m);
				hash = oldHash;
				alpha = std::max(alpha, currEval);
				if (alpha > beta)
					return currEval;
			}
			bool legalMoveFound;
			if constexpr (s == ABSearch) legalMoveFound = false;
			else legalMoveFound = true;
			for (std::size_t i = 0; i != j; ++i)
			{
				if (!searchFlags::searching.test())
				{
					if constexpr (s == ABSearch)
					{
						return eval::evaluate<wToMove>(b);
					}
					else
					{
						return std::max(checkPos, currEval);
					}
				}

				if constexpr (s == QSearch)
				{
					if ((checkPos + (std::int32_t)eval::getCaptureValue(moves[i])) < alpha)
						continue;
				}
				auto oldHash = hash;
				MAKE_MOVE_AND_UPDATE_HASH(moves[i], b, false);

				legalMoveFound = true;
				currEval = std::max(currEval, -1 * alphaBetaSearch<!wToMove, s>(-1 * beta, -1 * alpha, depth - 1));
				b.unmakeMove<wToMove>(moves[i]);
				hash = oldHash;
				alpha = std::max(currEval, alpha);
				if (alpha > beta)
				{
					if (tt != nullptr && s == ABSearch)
					{
						(*tt)[hash].key = hash;
						(*tt)[hash].depth = depth;
						if constexpr (wToMove)
							(*tt)[hash].eval = currEval;
						else
							(*tt)[hash].eval = -1 * currEval;
						(*tt)[hash].nodeType = 2;
					}
					return currEval;
				}
			}
			if (!legalMoveFound)
				currEval = movegen::isInCheck<wToMove>(b) ? negInf + (std::int32_t)(currIDdepth - depth) : 0;
			if (tt != nullptr && s == ABSearch)
			{
				(*tt)[hash].key = hash;
				(*tt)[hash].depth = depth;
				if constexpr (wToMove)
					(*tt)[hash].eval = currEval;
				else
					(*tt)[hash].eval = -1 * currEval;
				(*tt)[hash].nodeType = alpha > oldAlpha ? 0 : 1;
			}
			return currEval;
		}
		
		std::int32_t eval = 0;
		// 218 = current max number of moves in chess position
		// 256 = leeway for pseudolegal move generation
		std::array<std::pair<board::Move, std::int32_t>, 256> rootMoves;
		std::array<board::Move, 256> currline;
	};
}
#endif
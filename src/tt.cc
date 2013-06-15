/*
 * DiscoCheck, an UCI chess engine. Copyright (C) 2011-2013 Lucas Braesch.
 *
 * DiscoCheck is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * DiscoCheck is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If not,
 * see <http://www.gnu.org/licenses/>.
 *
 * Credits:
 * - TT entry replacement scheme replicates what Stockfish does. Thanks to Tord Romstad and Marco
 * Costalba.
*/
#include <cstring>
#include "tt.h"
#include "move.h"

void *aligned_malloc(size_t size, size_t align)
{
	void *mem = malloc(size + (align-1) + sizeof(void*));
	if (!mem) throw std::bad_alloc();

	char *amem = ((char*)mem) + sizeof(void*);
	amem += align - ((uintptr_t)amem & (align - 1));

	((void**)amem)[-1] = mem;
	return amem;
}
 
void aligned_free(void *mem)
{
	free(((void**)mem)[-1]);
}

TTable::~TTable()
{
	if (count)
		aligned_free(cluster);

	cluster = NULL;
	generation = 0;
	count = 0;
}

void TTable::alloc(uint64_t size)
{
	// calculate the number of clusters allocate (count must be a power of two)
	size_t new_count = 1ULL << msb(size / sizeof(Cluster));
	
	// nothing to do if already allocated to the given size
	if (new_count == count)
		return;
	
	if (cluster)
		aligned_free(cluster);

	// Allocate the cluster array. On failure, std::bad_alloc is thrown and not caught, which
	// terminates the program. It's not a bug, it's a "feature".
	cluster = (Cluster *)aligned_malloc(new_count * sizeof(Cluster), 64);

	count = new_count;
	clear();
}

void TTable::clear()
{
	memset(cluster, 0, count * sizeof(Cluster));
	generation = 0;
}

void TTable::new_search()
{
	++generation;
}

const TTable::Entry *TTable::probe(Key key) const
{
	const Entry *e = cluster[key & (count-1)].entry;

	for (size_t i = 0; i < 4; ++i, ++e)
		if (e->key_match(key))
			return e;

	return NULL;
}

void TTable::store(Key key, uint8_t bound, int8_t depth, int16_t score, int16_t eval, move_t move)
{
	Entry *e = cluster[key & (count-1)].entry, *replace = e;

	for (size_t i = 0; i < 4; ++i, ++e) {
		// overwrite empty or old
		if (!e->key_bound || e->key_match(key)) {
			replace = e;
			if (!move)
				move = e->move;
			break;
		}

		// Stockfish replacement strategy
		int c1 = generation == replace->generation ? 2 : 0;
		int c2 = e->generation == generation || e->bound() == BOUND_EXACT ? -2 : 0;
		int c3 = e->depth < replace->depth ? 1 : 0;
		if (c1 + c2 + c3 > 0)
			replace = e;
	}

	replace->save(key, generation, bound, depth, score, eval, move);
}

std::string TTable::get_pv(Board& B, int max_len) const
{
	std::string s;
	B.set_unwind();
	
	for (int i = 0; i < max_len; i++) {
		const Entry *tte = probe(B.get_key());

		if (tte && tte->move && !B.is_draw()) {
			s += ' ' + move_to_string(tte->move);
			B.play(tte->move);
		} else
			break;
	}
	
	B.unwind();
	return s;
}
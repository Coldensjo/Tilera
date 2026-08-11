//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_TERRAFORM_H_
#define RME_TERRAFORM_H_

#include "main.h"

#include "position.h"

class BaseMap;
class GroundBrush;

// A fill/top ground-brush pair used by the terraforming tools (raise/lower/flatten):
// "fill" is the body of a raised column (e.g. mountain), "top" its walkable surface
// (e.g. grass). Names reference ground brushes from grounds.xml; resolution is lazy
// so the load order of material files does not matter.
struct TerraformPair {
	std::string name;
	std::string fillName;
	std::string topName;

	GroundBrush* fill() const;
	GroundBrush* top() const;
};

// Registry of pairs defined in data/<version>/terraform.xml. Cleared on client
// version unload. When no pairs are defined, getActive() falls back to a
// synthesized mountain/grass pair (whose brushes may still fail to resolve for
// versions lacking those grounds - callers must null-check fill()/top()).
class TerraformPairs {
public:
	void clear();
	void add(const TerraformPair& pair);

	const TerraformPair* getActive() const;
	void setActiveIndex(size_t index);
	size_t getActiveIndex() const {
		return active_index;
	}
	const std::vector<TerraformPair>& getPairs() const {
		return pairs;
	}

private:
	std::vector<TerraformPair> pairs;
	size_t active_index = 0;
};

extern TerraformPairs g_terraform_pairs;

// Column geometry. A terrain column is anchored at ground-layer (floor 7)
// coordinates; its height-k cell sits directly above it at (anchor.x,
// anchor.y, GROUND_LAYER - k) - no diagonal offset. This matches hand-built
// Tibia mountains: the upper-floor ground shares the footprint of the fill
// below it (user-verified; both +-1,+-1 diagonal variants looked wrong).

constexpr int TERRAFORM_VOID = -1; // column has no ground on any floor
constexpr int TERRAFORM_MAX_HEIGHT = GROUND_LAYER; // height 7 = ground at z 0

// Projects view-floor coordinates onto the ground layer. Returns an invalid
// position for underground floors (> GROUND_LAYER) - callers must check
// isValid().
Position terraformAnchor(int x, int y, int floor);

// The map position of the column's height-k cell.
Position terraformCell(const Position& anchor, int k);

// Height of the column: the largest k whose cell has ground, or TERRAFORM_VOID.
int terraformResolveHeight(BaseMap& map, const Position& anchor);

#endif

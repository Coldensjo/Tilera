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

#include "main.h"

#include "terraform.h"
#include "brush.h"
#include "ground_brush.h"
#include "basemap.h"
#include "tile.h"

TerraformPairs g_terraform_pairs;

static GroundBrush* resolveGroundBrush(const std::string& name) {
	if (name.empty()) {
		return nullptr;
	}
	Brush* brush = g_brushes.getBrush(name);
	return brush ? brush->asGround() : nullptr;
}

GroundBrush* TerraformPair::fill() const {
	return resolveGroundBrush(fillName);
}

GroundBrush* TerraformPair::top() const {
	return resolveGroundBrush(topName);
}

void TerraformPairs::clear() {
	pairs.clear();
	active_index = 0;
}

void TerraformPairs::add(const TerraformPair& pair) {
	pairs.push_back(pair);
}

const TerraformPair* TerraformPairs::getActive() const {
	static const TerraformPair fallback = { "mountain", "mountain", "grass" };
	if (pairs.empty()) {
		return &fallback;
	}
	return &pairs[active_index < pairs.size() ? active_index : 0];
}

void TerraformPairs::setActiveIndex(size_t index) {
	if (index < pairs.size()) {
		active_index = index;
	}
}

Position terraformAnchor(int x, int y, int floor) {
	if (floor < 0 || floor > GROUND_LAYER) {
		return Position(-1, -1, -1);
	}
	return Position(x, y, GROUND_LAYER);
}

Position terraformCell(const Position& anchor, int k) {
	return Position(anchor.x, anchor.y, GROUND_LAYER - k);
}

int terraformResolveHeight(BaseMap& map, const Position& anchor) {
	for (int k = TERRAFORM_MAX_HEIGHT; k >= 0; --k) {
		const Position pos = terraformCell(anchor, k);
		if (!pos.isValid()) {
			continue;
		}
		Tile* tile = map.getTile(pos);
		if (tile && tile->hasGround()) {
			return k;
		}
	}
	return TERRAFORM_VOID;
}

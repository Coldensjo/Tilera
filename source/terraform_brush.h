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

#ifndef RME_TERRAFORM_BRUSH_H_
#define RME_TERRAFORM_BRUSH_H_

#include "brush.h"

// Raise/lower/flatten terrain columns (see terraform.h for the column model).
// The brush object is only a UI token: draw/undraw are never reached because
// the cross-floor orchestration lives in Editor::terraformInternal.
class TerraformBrush : public Brush {
public:
	enum Mode {
		RAISE,
		LOWER,
		FLATTEN,
	};

	explicit TerraformBrush(Mode mode);
	virtual ~TerraformBrush();

	bool isTerraform() const {
		return true;
	}
	TerraformBrush* asTerraform() {
		return this;
	}

	virtual void draw(BaseMap* map, Tile* tile, void* parameter) { }
	virtual void undraw(BaseMap* map, Tile* tile) { }
	virtual bool canDraw(BaseMap* map, const Position& position) const;

	virtual std::string getName() const;
	virtual int getLookID() const;

	virtual bool needBorders() const {
		return true;
	}
	virtual bool canSmear() const {
		return true;
	}
	virtual bool canDrag() const {
		return false;
	}

	Mode getMode() const {
		return mode;
	}

	// One stroke changes each column by at most one step: anchors already
	// touched by the current stroke are skipped until the mouse is released.
	void beginStroke() {
		visited.clear();
	}
	void endStroke() {
		visited.clear();
	}
	bool isVisited(const Position& anchor) const {
		return visited.count(anchor) != 0;
	}
	void markVisited(const Position& anchor) {
		visited.insert(anchor);
	}

protected:
	Mode mode;
	std::set<Position> visited;
};

#endif

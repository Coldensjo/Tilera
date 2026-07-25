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

#ifndef __POSITION_HPP__
#define __POSITION_HPP__

#include <ostream>
#include <cstdint>
#include <vector>
#include <list>

class SmallPosition;

class Position {
public:
	// We use int since it's the native machine type and can be several times faster than
	// the other integer types in most cases, also, the position may be negative in some
	// cases
	int x, y, z;

	Position() : x(0), y(0), z(0) { }
	Position(int _x, int _y, int _z) : x(_x), y(_y), z(_z) { }

	bool operator<(const Position& p) const {
		if (z != p.z) {
			return z < p.z;
		}
		if (y != p.y) {
			return y < p.y;
		}
		return x < p.x;
	}

	bool operator>(const Position& p) const {
		if (z != p.z) {
			return z > p.z;
		}
		if (y != p.y) {
			return y > p.y;
		}
		return x > p.x;
	}

	Position operator-(const Position& p) const {
		return Position(x - p.x, y - p.y, z - p.z);
	}

	Position operator+(const Position& p) const {
		return Position(x + p.x, y + p.y, z + p.z);
	}

	Position& operator+=(const Position& p) {
		x += p.x;
		y += p.y;
		z += p.z;
		return *this;
	}

	bool operator==(const Position& p) const {
		return p.x == x && p.y == y && p.z == z;
	}

	bool operator!=(const Position& p) const {
		return !(*this == p);
	}

	bool isValid() const;
};

inline std::ostream& operator<<(std::ostream& os, const Position& pos) {
	os << pos.x << ':' << pos.y << ':' << pos.z;
	return os;
}

inline std::istream& operator>>(std::istream& is, Position& pos) {
	char a, b;
	int x, y, z;
	is >> x;
	if (!is) {
		return is;
	}
	is >> a;
	if (!is || a != ':') {
		return is;
	}
	is >> y;
	if (!is) {
		return is;
	}
	is >> b;
	if (!is || b != ':') {
		return is;
	}
	is >> z;
	if (!is) {
		return is;
	}

	pos.x = x;
	pos.y = y;
	pos.z = z;

	return is;
}

inline bool Position::isValid() const {
	return x >= 0 && x <= MAP_MAX_WIDTH && y >= 0 && y <= MAP_MAX_HEIGHT && z >= 0 && z <= MAP_MAX_LAYER;
}

inline Position abs(const Position& position) {
	return Position(
		std::abs(position.x),
		std::abs(position.y),
		std::abs(position.z)
	);
}

// Geometric transformations that can be applied to a rectangular area of the map
// (the selection, or the pending paste in the copybuffer)
enum class MapTransform {
	RotateClockwise,
	RotateCounterClockwise,
	FlipHorizontal,
	FlipVertical,
};

// Maps a position inside the [min_pos, max_pos] bounding box onto its transformed
// position. The upper-left corner stays anchored; rotations swap width and height.
inline Position transformPosition(const Position& pos, MapTransform transform, const Position& min_pos, const Position& max_pos) {
	const int span_x = max_pos.x - min_pos.x;
	const int span_y = max_pos.y - min_pos.y;
	const int rx = pos.x - min_pos.x;
	const int ry = pos.y - min_pos.y;

	switch (transform) {
		case MapTransform::RotateClockwise:
			return Position(min_pos.x + (span_y - ry), min_pos.y + rx, pos.z);
		case MapTransform::RotateCounterClockwise:
			return Position(min_pos.x + ry, min_pos.y + (span_x - rx), pos.z);
		case MapTransform::FlipHorizontal:
			return Position(min_pos.x + (span_x - rx), pos.y, pos.z);
		case MapTransform::FlipVertical:
			return Position(pos.x, min_pos.y + (span_y - ry), pos.z);
	}
	return pos;
}

// How many clockwise orientation steps rotatable items (chairs, statues, ...) take
// so that they turn along with the area. Flips leave item orientation untouched.
inline int itemRotationSteps(MapTransform transform) {
	switch (transform) {
		case MapTransform::RotateClockwise:
			return 1;
		case MapTransform::RotateCounterClockwise:
			return 3;
		default:
			return 0;
	}
}

typedef std::vector<Position> PositionVector;
typedef std::list<Position> PositionList;

#endif

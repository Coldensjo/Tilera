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

#include "terraform_brush.h"
#include "terraform.h"
#include "sprites.h"

TerraformBrush::TerraformBrush(Mode mode) : mode(mode) {
	////
}

TerraformBrush::~TerraformBrush() {
	////
}

std::string TerraformBrush::getName() const {
	switch (mode) {
		case RAISE:
			return "Raise Ground";
		case LOWER:
			return "Lower Ground";
		case FLATTEN:
			return "Flatten Ground";
	}
	return "Terraform";
}

int TerraformBrush::getLookID() const {
	switch (mode) {
		case RAISE:
			return EDITOR_SPRITE_TERRAFORM_RAISE;
		case LOWER:
			return EDITOR_SPRITE_TERRAFORM_LOWER;
		case FLATTEN:
			return EDITOR_SPRITE_TERRAFORM_FLATTEN;
	}
	return EDITOR_SPRITE_TERRAFORM_RAISE;
}

bool TerraformBrush::canDraw(BaseMap* map, const Position& position) const {
	return terraformAnchor(position.x, position.y, position.z).isValid();
}

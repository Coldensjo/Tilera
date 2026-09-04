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

#include "sprites.h"
#include "graphics.h"
#include "filehandle.h"
#include "settings.h"
#include "gui.h"
#include "otml.h"

#include <wx/mstream.h>
#include <wx/stopwatch.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include "pngfiles.h"


// All 133 template colors
static uint32_t TemplateOutfitLookupTable[] = {
	0xFFFFFF,
	0xFFD4BF,
	0xFFE9BF,
	0xFFFFBF,
	0xE9FFBF,
	0xD4FFBF,
	0xBFFFBF,
	0xBFFFD4,
	0xBFFFE9,
	0xBFFFFF,
	0xBFE9FF,
	0xBFD4FF,
	0xBFBFFF,
	0xD4BFFF,
	0xE9BFFF,
	0xFFBFFF,
	0xFFBFE9,
	0xFFBFD4,
	0xFFBFBF,
	0xDADADA,
	0xBF9F8F,
	0xBFAF8F,
	0xBFBF8F,
	0xAFBF8F,
	0x9FBF8F,
	0x8FBF8F,
	0x8FBF9F,
	0x8FBFAF,
	0x8FBFBF,
	0x8FAFBF,
	0x8F9FBF,
	0x8F8FBF,
	0x9F8FBF,
	0xAF8FBF,
	0xBF8FBF,
	0xBF8FAF,
	0xBF8F9F,
	0xBF8F8F,
	0xB6B6B6,
	0xBF7F5F,
	0xBFAF8F,
	0xBFBF5F,
	0x9FBF5F,
	0x7FBF5F,
	0x5FBF5F,
	0x5FBF7F,
	0x5FBF9F,
	0x5FBFBF,
	0x5F9FBF,
	0x5F7FBF,
	0x5F5FBF,
	0x7F5FBF,
	0x9F5FBF,
	0xBF5FBF,
	0xBF5F9F,
	0xBF5F7F,
	0xBF5F5F,
	0x919191,
	0xBF6A3F,
	0xBF943F,
	0xBFBF3F,
	0x94BF3F,
	0x6ABF3F,
	0x3FBF3F,
	0x3FBF6A,
	0x3FBF94,
	0x3FBFBF,
	0x3F94BF,
	0x3F6ABF,
	0x3F3FBF,
	0x6A3FBF,
	0x943FBF,
	0xBF3FBF,
	0xBF3F94,
	0xBF3F6A,
	0xBF3F3F,
	0x6D6D6D,
	0xFF5500,
	0xFFAA00,
	0xFFFF00,
	0xAAFF00,
	0x54FF00,
	0x00FF00,
	0x00FF54,
	0x00FFAA,
	0x00FFFF,
	0x00A9FF,
	0x0055FF,
	0x0000FF,
	0x5500FF,
	0xA900FF,
	0xFE00FF,
	0xFF00AA,
	0xFF0055,
	0xFF0000,
	0x484848,
	0xBF3F00,
	0xBF7F00,
	0xBFBF00,
	0x7FBF00,
	0x3FBF00,
	0x00BF00,
	0x00BF3F,
	0x00BF7F,
	0x00BFBF,
	0x007FBF,
	0x003FBF,
	0x0000BF,
	0x3F00BF,
	0x7F00BF,
	0xBF00BF,
	0xBF007F,
	0xBF003F,
	0xBF0000,
	0x242424,
	0x7F2A00,
	0x7F5500,
	0x7F7F00,
	0x557F00,
	0x2A7F00,
	0x007F00,
	0x007F2A,
	0x007F55,
	0x007F7F,
	0x00547F,
	0x002A7F,
	0x00007F,
	0x2A007F,
	0x54007F,
	0x7F007F,
	0x7F0055,
	0x7F002A,
	0x7F0000,
};

GraphicManager::GraphicManager() :
	client_version(nullptr),
	unloaded(true),
	dat_format(DAT_FORMAT_UNKNOWN),
	otfi_found(false),
	is_extended(false),
	has_transparency(false),
	has_frame_durations(false),
	has_frame_groups(false),
	loaded_textures(0),
	lastclean(0),
	warning_sign_texture(0),
	palette_refresh_needed(false) {
	animation_timer = newd wxStopWatch();
	animation_timer->Start();
}

GraphicManager::~GraphicManager() {
	for (SpriteMap::iterator iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		delete iter->second;
	}

	for (ImageMap::iterator iter = image_space.begin(); iter != image_space.end(); ++iter) {
		delete iter->second;
	}

	delete animation_timer;
}

bool GraphicManager::hasTransparency() const {
	return has_transparency;
}

bool GraphicManager::isUnloaded() const {
	return unloaded;
}

GLuint GraphicManager::getFreeTextureID() {
	static GLuint id_counter = 0x10000000;
	return id_counter++; // This should (hopefully) never run out
}

GLuint GraphicManager::getWarningSignTexture() {
	if (warning_sign_texture != 0) {
		return warning_sign_texture;
	}

	wxMemoryInputStream stream(exclamation_png, sizeof(exclamation_png));
	wxImage image(stream, wxBITMAP_TYPE_PNG);
	if (!image.IsOk()) {
		return 0;
	}

	const int width = image.GetWidth();
	const int height = image.GetHeight();
	const uint8_t* rgb = image.GetData();
	const uint8_t* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;

	std::vector<uint8_t> rgba(size_t(width) * height * 4);
	for (int i = 0; i < width * height; ++i) {
		rgba[i * 4 + 0] = rgb[i * 3 + 0];
		rgba[i * 4 + 1] = rgb[i * 3 + 1];
		rgba[i * 4 + 2] = rgb[i * 3 + 2];
		rgba[i * 4 + 3] = alpha ? alpha[i] : 255;
	}

	const GLuint texture = getFreeTextureID();
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // Nearest-neighbor: no anti-aliasing
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // Nearest-neighbor: no anti-aliasing
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F); // GL_CLAMP_TO_EDGE
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

	warning_sign_texture = texture;
	return warning_sign_texture;
}

void GraphicManager::clear() {
	SpriteMap new_sprite_space;
	for (SpriteMap::iterator iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		if (iter->first >= 0) { // Don't clean internal sprites
			delete iter->second;
		} else {
			new_sprite_space.insert(std::make_pair(iter->first, iter->second));
		}
	}

	for (ImageMap::iterator iter = image_space.begin(); iter != image_space.end(); ++iter) {
		delete iter->second;
	}

	sprite_space.swap(new_sprite_space);
	image_space.clear();
	cleanup_list.clear();

	item_count = 0;
	creature_count = 0;
	loaded_textures = 0;
	lastclean = time(nullptr);
	spritefile = "";

	if (warning_sign_texture != 0) {
		glDeleteTextures(1, &warning_sign_texture);
		warning_sign_texture = 0;
	}

	unloaded = true;
}

void GraphicManager::cleanSoftwareSprites() {
	for (SpriteMap::iterator iter = sprite_space.begin(); iter != sprite_space.end(); ++iter) {
		if (iter->first >= 0) { // Don't clean internal sprites
			iter->second->unloadDC();
		}
	}
	palette_refresh_needed = true;
}

Sprite* GraphicManager::getSprite(int id) {
	SpriteMap::iterator it = sprite_space.find(id);
	if (it != sprite_space.end()) {
		return it->second;
	}
	return nullptr;
}

GameSprite* GraphicManager::getCreatureSprite(int id) {
	if (id < 0) {
		return nullptr;
	}

	SpriteMap::iterator it = sprite_space.find(id + item_count);
	if (it != sprite_space.end()) {
		return static_cast<GameSprite*>(it->second);
	}
	return nullptr;
}

uint16_t GraphicManager::getItemSpriteMaxID() const {
	return item_count;
}

uint16_t GraphicManager::getCreatureSpriteMaxID() const {
	return creature_count;
}

#define loadPNGFile(name) _wxGetBitmapFromMemory(name, sizeof(name))
inline wxBitmap* _wxGetBitmapFromMemory(const unsigned char* data, int length) {
	wxMemoryInputStream is(data, length);
	wxImage img(is, "image/png");
	if (!img.IsOk()) {
		return nullptr;
	}
	return newd wxBitmap(img, -1);
}

namespace {

// The selection marker is a 50% checkerboard dither of a single colour, so it
// is generated rather than stored as an image.
wxBitmap* createSelectionMarker(int size) {
	wxImage image(size, size);
	image.InitAlpha();
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			image.SetRGB(x, y, 0x00, 0x00, 0x80);
			image.SetAlpha(x, y, (x + y) % 2 == 1 ? wxIMAGE_ALPHA_OPAQUE : wxIMAGE_ALPHA_TRANSPARENT);
		}
	}
	return newd wxBitmap(image);
}

} // namespace

bool GraphicManager::loadEditorSprites() {
	// Unused graphics MIGHT be loaded here, but it's a neglectable loss
	sprite_space[EDITOR_SPRITE_SELECTION_MARKER] = newd EditorSprite(
		createSelectionMarker(16),
		createSelectionMarker(32)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_1x1] = newd EditorSprite(
		loadPNGFile(circular_1_small_png),
		loadPNGFile(circular_1_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_3x3] = newd EditorSprite(
		loadPNGFile(circular_2_small_png),
		loadPNGFile(circular_2_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_5x5] = newd EditorSprite(
		loadPNGFile(circular_3_small_png),
		loadPNGFile(circular_3_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_7x7] = newd EditorSprite(
		loadPNGFile(circular_4_small_png),
		loadPNGFile(circular_4_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_9x9] = newd EditorSprite(
		loadPNGFile(circular_5_small_png),
		loadPNGFile(circular_5_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_15x15] = newd EditorSprite(
		loadPNGFile(circular_6_small_png),
		loadPNGFile(circular_6_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_CD_19x19] = newd EditorSprite(
		loadPNGFile(circular_7_small_png),
		loadPNGFile(circular_7_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_1x1] = newd EditorSprite(
		loadPNGFile(rectangular_1_small_png),
		loadPNGFile(rectangular_1_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_2x2] = newd EditorSprite(
		loadPNGFile(rectangular_2_small_png),
		loadPNGFile(rectangular_2_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_3x3] = newd EditorSprite(
		loadPNGFile(rectangular_3_small_png),
		loadPNGFile(rectangular_3_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_5x5] = newd EditorSprite(
		loadPNGFile(rectangular_4_small_png),
		loadPNGFile(rectangular_4_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_7x7] = newd EditorSprite(
		loadPNGFile(rectangular_5_small_png),
		loadPNGFile(rectangular_5_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_9x9] = newd EditorSprite(
		loadPNGFile(rectangular_6_small_png),
		loadPNGFile(rectangular_6_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_15x15] = newd EditorSprite(
		loadPNGFile(rectangular_7_small_png),
		loadPNGFile(rectangular_7_png)
	);
	sprite_space[EDITOR_SPRITE_BRUSH_SD_19x19] = newd EditorSprite(
		loadPNGFile(rectangular_7_small_png),
		loadPNGFile(rectangular_7_png)
	);

	sprite_space[EDITOR_SPRITE_OPTIONAL_BORDER_TOOL] = newd EditorSprite(
		loadPNGFile(optional_border_small_png),
		loadPNGFile(optional_border_png)
	);
	sprite_space[EDITOR_SPRITE_ERASER] = newd EditorSprite(
		loadPNGFile(eraser_small_png),
		loadPNGFile(eraser_png)
	);
	sprite_space[EDITOR_SPRITE_PZ_TOOL] = newd EditorSprite(
		loadPNGFile(protection_zone_small_png),
		loadPNGFile(protection_zone_png)
	);
	sprite_space[EDITOR_SPRITE_PVPZ_TOOL] = newd EditorSprite(
		loadPNGFile(pvp_zone_small_png),
		loadPNGFile(pvp_zone_png)
	);
	sprite_space[EDITOR_SPRITE_NOLOG_TOOL] = newd EditorSprite(
		loadPNGFile(no_logout_small_png),
		loadPNGFile(no_logout_png)
	);
	sprite_space[EDITOR_SPRITE_NOPVP_TOOL] = newd EditorSprite(
		loadPNGFile(no_pvp_small_png),
		loadPNGFile(no_pvp_png)
	);
	sprite_space[EDITOR_SPRITE_REFRESH_TOOL] = newd EditorSprite(
		loadPNGFile(refresh_small_png),
		loadPNGFile(refresh_png)
	);

	sprite_space[EDITOR_SPRITE_DOOR_NORMAL] = newd EditorSprite(
		loadPNGFile(door_normal_small_png),
		loadPNGFile(door_normal_png)
	);
	sprite_space[EDITOR_SPRITE_DOOR_LOCKED] = newd EditorSprite(
		loadPNGFile(door_locked_small_png),
		loadPNGFile(door_locked_png)
	);
	sprite_space[EDITOR_SPRITE_DOOR_MAGIC] = newd EditorSprite(
		loadPNGFile(door_magic_small_png),
		loadPNGFile(door_magic_png)
	);
	sprite_space[EDITOR_SPRITE_DOOR_QUEST] = newd EditorSprite(
		loadPNGFile(door_quest_small_png),
		loadPNGFile(door_quest_png)
	);
	sprite_space[EDITOR_SPRITE_DOOR_NORMAL_ALT] = newd EditorSprite(
		loadPNGFile(door_normal_alt_small_png),
		loadPNGFile(door_normal_alt_png)
	);
	sprite_space[EDITOR_SPRITE_DOOR_ARCHWAY] = newd EditorSprite(
		loadPNGFile(door_archway_small_png),
		loadPNGFile(door_archway_png)
	);
	sprite_space[EDITOR_SPRITE_WINDOW_NORMAL] = newd EditorSprite(
		loadPNGFile(window_normal_small_png),
		loadPNGFile(window_normal_png)
	);
	sprite_space[EDITOR_SPRITE_WINDOW_HATCH] = newd EditorSprite(
		loadPNGFile(window_hatch_small_png),
		loadPNGFile(window_hatch_png)
	);

	sprite_space[EDITOR_SPRITE_SELECTION_GEM] = newd EditorSprite(
		loadPNGFile(gem_edit_png),
		nullptr
	);
	sprite_space[EDITOR_SPRITE_DRAWING_GEM] = newd EditorSprite(
		loadPNGFile(gem_move_png),
		nullptr
	);

	sprite_space[EDITOR_SPRITE_TERRAFORM_RAISE] = newd EditorSprite(
		loadPNGFile(terraform_raise_small_png),
		loadPNGFile(terraform_raise_png)
	);
	sprite_space[EDITOR_SPRITE_TERRAFORM_LOWER] = newd EditorSprite(
		loadPNGFile(terraform_lower_small_png),
		loadPNGFile(terraform_lower_png)
	);
	sprite_space[EDITOR_SPRITE_TERRAFORM_FLATTEN] = newd EditorSprite(
		loadPNGFile(terraform_flatten_small_png),
		loadPNGFile(terraform_flatten_png)
	);

	return true;
}

bool GraphicManager::loadOTFI(const FileName& filename, wxString& error, wxArrayString& warnings) {
	wxDir dir(filename.GetFullPath());
	wxString otfi_file;

	otfi_found = false;

	if (dir.GetFirst(&otfi_file, "*.otfi", wxDIR_FILES)) {
		wxFileName otfi(filename.GetFullPath(), otfi_file);
		OTMLDocumentPtr doc = OTMLDocument::parse(otfi.GetFullPath().ToStdString());
		if (doc->size() == 0 || !doc->hasChildAt("DatSpr")) {
			error += "'DatSpr' tag not found";
			return false;
		}

		OTMLNodePtr node = doc->get("DatSpr");
		is_extended = node->valueAt<bool>("extended");
		has_transparency = node->valueAt<bool>("transparency");
		has_frame_durations = node->valueAt<bool>("frame-durations");
		has_frame_groups = node->valueAt<bool>("frame-groups");
		std::string metadata = node->valueAt<std::string>("metadata-file", std::string(ASSETS_NAME) + ".dat");
		std::string sprites = node->valueAt<std::string>("sprites-file", std::string(ASSETS_NAME) + ".spr");
		metadata_file = wxFileName(filename.GetFullPath(), wxString(metadata));
		sprites_file = wxFileName(filename.GetFullPath(), wxString(sprites));
		otfi_found = true;
	}

	if (!otfi_found) {
		is_extended = false;
		has_transparency = false;
		has_frame_durations = false;
		has_frame_groups = false;
		metadata_file = wxFileName(filename.GetFullPath(), wxString(ASSETS_NAME) + ".dat");
		sprites_file = wxFileName(filename.GetFullPath(), wxString(ASSETS_NAME) + ".spr");
	}

	return true;
}

bool GraphicManager::loadSpriteMetadata(const FileName& datafile, wxString& error, wxArrayString& warnings) {
	// items.otb has most of the info we need. This only loads the GameSprite metadata
	FileReadHandle file(nstr(datafile.GetFullPath()));

	if (!file.isOk()) {
		error += "Failed to open " + datafile.GetFullPath() + " for reading\nThe error reported was:" + wxstr(file.getErrorMessage());
		return false;
	}

	uint16_t effect_count, distance_count;

	uint32_t datSignature;
	file.getU32(datSignature);
	// get max id
	file.getU16(item_count);
	file.getU16(creature_count);
	file.getU16(effect_count);
	file.getU16(distance_count);

	uint32_t minID = 100; // items start with id 100
	// We don't load distance/effects, if we would, just add effect_count & distance_count here
	uint32_t maxID = item_count + creature_count;

	dat_format = client_version->getDatFormatForSignature(datSignature);

	if (!otfi_found) {
		is_extended = dat_format >= DAT_FORMAT_96;
		has_frame_durations = dat_format >= DAT_FORMAT_1050;
		has_frame_groups = dat_format >= DAT_FORMAT_1057;
	}

	uint16_t id = minID;
	// loop through all ItemDatabase until we reach the end of file
	while (id <= maxID) {
		GameSprite* sType = newd GameSprite();
		sprite_space[id] = sType;

		sType->id = id;

		// Load the sprite flags
		if (!loadSpriteMetadataFlags(file, sType, error, warnings)) {
			wxString msg;
			msg << "Failed to load flags for sprite " << sType->id;
			warnings.push_back(msg);
		}

		// Reads the group count
		uint8_t group_count = 1;
		if (has_frame_groups && id > item_count) {
			file.getU8(group_count);
		}

		for (uint32_t k = 0; k < group_count; ++k) {
			// Skipping the group type
			if (has_frame_groups && id > item_count) {
				file.skip(1);
			}

			// Size and GameSprite data
			file.getByte(sType->width);
			file.getByte(sType->height);

			// Skipping the exact size
			if ((sType->width > 1) || (sType->height > 1)) {
				file.skip(1);
			}

			file.getU8(sType->layers); // Number of blendframes (some sprites consist of several merged sprites)
			file.getU8(sType->pattern_x);
			file.getU8(sType->pattern_y);
			if (dat_format <= DAT_FORMAT_74) {
				sType->pattern_z = 1;
			} else {
				file.getU8(sType->pattern_z);
			}
			file.getU8(sType->frames); // Length of animation

			if (sType->frames > 1) {
				uint8_t async = 0;
				int loop_count = 0;
				int8_t start_frame = 0;
				if (has_frame_durations) {
					file.getByte(async);
					file.get32(loop_count);
					file.getSByte(start_frame);
				}
				sType->animator = newd Animator(sType->frames, start_frame, loop_count, async == 1);
				if (has_frame_durations) {
					for (int i = 0; i < sType->frames; i++) {
						uint32_t min;
						uint32_t max;
						file.getU32(min);
						file.getU32(max);
						FrameDuration* frame_duration = sType->animator->getFrameDuration(i);
						frame_duration->setValues(int(min), int(max));
					}
					sType->animator->reset();
				}
			}

			sType->numsprites = (int)sType->width * (int)sType->height * (int)sType->layers * (int)sType->pattern_x * (int)sType->pattern_y * sType->pattern_z * (int)sType->frames;

			// Read the sprite ids
			for (uint32_t i = 0; i < sType->numsprites; ++i) {
				uint32_t sprite_id;
				if (is_extended) {
					file.getU32(sprite_id);
				} else {
					uint16_t u16 = 0;
					file.getU16(u16);
					sprite_id = u16;
				}

				if (image_space[sprite_id] == nullptr) {
					GameSprite::NormalImage* img = newd GameSprite::NormalImage();
					img->id = sprite_id;
					image_space[sprite_id] = img;
				}
				sType->spriteList.push_back(static_cast<GameSprite::NormalImage*>(image_space[sprite_id]));
			}
		}
		++id;
	}

	return true;
}

bool GraphicManager::loadSpriteMetadataFlags(FileReadHandle& file, GameSprite* sType, wxString& error, wxArrayString& warnings) {
	uint8_t prev_flag = 0;
	uint8_t flag = DatFlagLast;

	for (int i = 0; i < DatFlagLast; ++i) {
		prev_flag = flag;
		file.getU8(flag);

		if (flag == DatFlagLast) {
			return true;
		}
		if (dat_format >= DAT_FORMAT_1010) {
			/* In 10.10+ all attributes from 16 and up were
			 * incremented by 1 to make space for 16 as
			 * "No Movement Animation" flag.
			 */
			if (flag == 16) {
				flag = DatFlagNoMoveAnimation;
			} else if (flag > 16) {
				flag -= 1;
			}
		} else if (dat_format >= DAT_FORMAT_86) {
			/* Default attribute values follow
			 * the format of 8.6-9.86.
			 * Therefore no changes here.
			 */
		} else if (dat_format >= DAT_FORMAT_78) {
			/* In 7.80-8.54 all attributes from 8 and higher were
			 * incremented by 1 to make space for 8 as
			 * "Item Charges" flag.
			 */
			if (flag == 8) {
				flag = DatFlagChargeable;
			} else if (flag > 8) {
				flag -= 1;
			}
		} else if (dat_format >= DAT_FORMAT_755) {
			/* In 7.55-7.72 attributes 23 is "Floor Change". */
			if (flag == 23) {
				flag = DatFlagFloorChange;
			}
		} else if (dat_format >= DAT_FORMAT_74) {
			/* In 7.4-7.5 attribute "Ground Border" did not exist
			 * attributes 1-15 have to be adjusted.
			 * Several other changes in the format.
			 */
			if (flag > 0 && flag <= 15) {
				flag += 1;
			} else if (flag == 16) {
				flag = DatFlagLight;
			} else if (flag == 17) {
				flag = DatFlagFloorChange;
			} else if (flag == 18) {
				flag = DatFlagFullGround;
			} else if (flag == 19) {
				flag = DatFlagElevation;
			} else if (flag == 20) {
				flag = DatFlagDisplacement;
			} else if (flag == 22) {
				flag = DatFlagMinimapColor;
			} else if (flag == 23) {
				flag = DatFlagRotateable;
			} else if (flag == 24) {
				flag = DatFlagLyingCorpse;
			} else if (flag == 25) {
				flag = DatFlagHangable;
			} else if (flag == 26) {
				flag = DatFlagHookSouth;
			} else if (flag == 27) {
				flag = DatFlagHookEast;
			} else if (flag == 28) {
				flag = DatFlagAnimateAlways;
			}

			/* "Multi Use" and "Force Use" are swapped */
			if (flag == DatFlagMultiUse) {
				flag = DatFlagForceUse;
			} else if (flag == DatFlagForceUse) {
				flag = DatFlagMultiUse;
			}
		}

		switch (flag) {
			case DatFlagGroundBorder:
			case DatFlagOnBottom:
			case DatFlagOnTop:
			case DatFlagContainer:
			case DatFlagStackable:
			case DatFlagForceUse:
			case DatFlagMultiUse:
			case DatFlagFluidContainer:
			case DatFlagSplash:
			case DatFlagNotWalkable:
			case DatFlagNotMoveable:
			case DatFlagBlockProjectile:
			case DatFlagNotPathable:
			case DatFlagPickupable:
			case DatFlagHangable:
			case DatFlagHookSouth:
			case DatFlagHookEast:
			case DatFlagRotateable:
			case DatFlagDontHide:
			case DatFlagTranslucent:
			case DatFlagLyingCorpse:
			case DatFlagAnimateAlways:
			case DatFlagFullGround:
			case DatFlagLook:
			case DatFlagWrappable:
			case DatFlagUnwrappable:
			case DatFlagTopEffect:
			case DatFlagFloorChange:
			case DatFlagNoMoveAnimation:
			case DatFlagChargeable:
				break;

			case DatFlagGround:
			case DatFlagWritable:
			case DatFlagWritableOnce:
			case DatFlagCloth:
			case DatFlagLensHelp:
			case DatFlagUsable:
				file.skip(2);
				break;

			case DatFlagLight: {
				SpriteLight light;
				uint16_t intensity;
				uint16_t color;
				file.getU16(intensity);
				file.getU16(color);
				sType->has_light = true;
				sType->light = SpriteLight { static_cast<uint8_t>(intensity), static_cast<uint8_t>(color) };
				break;
			}

			case DatFlagDisplacement: {
				if (dat_format >= DAT_FORMAT_755) {
					uint16_t offset_x;
					uint16_t offset_y;
					file.getU16(offset_x);
					file.getU16(offset_y);

					sType->drawoffset_x = offset_x;
					sType->drawoffset_y = offset_y;
				} else {
					sType->drawoffset_x = 8;
					sType->drawoffset_y = 8;
				}
				break;
			}

			case DatFlagElevation: {
				uint16_t draw_height;
				file.getU16(draw_height);
				sType->draw_height = draw_height;
				break;
			}

			case DatFlagMinimapColor: {
				uint16_t minimap_color;
				file.getU16(minimap_color);
				sType->minimap_color = minimap_color;
				break;
			}

			case DatFlagMarket: {
				file.skip(6);
				std::string marketName;
				file.getString(marketName);
				file.skip(4);
				break;
			}

			default: {
				wxString err;
				err << "Metadata: Unknown flag: " << i2ws(flag) << ". Previous flag: " << i2ws(prev_flag) << ".";
				warnings.push_back(err);
				break;
			}
		}
	}

	return true;
}

namespace {

// Mirrors the per-version transformations in loadSpriteMetadataFlags:
// the byte stored in the .dat file -> canonical DatFlags value.
uint8_t datFileByteToFlag(DatFormat format, uint8_t flag) {
	if (format >= DAT_FORMAT_1010) {
		if (flag == 16) {
			return DatFlagNoMoveAnimation;
		}
		if (flag > 16) {
			return flag - 1;
		}
		return flag;
	} else if (format >= DAT_FORMAT_86) {
		return flag;
	} else if (format >= DAT_FORMAT_78) {
		if (flag == 8) {
			return DatFlagChargeable;
		}
		if (flag > 8) {
			return flag - 1;
		}
		return flag;
	} else if (format >= DAT_FORMAT_755) {
		if (flag == 23) {
			return DatFlagFloorChange;
		}
		return flag;
	}
	// 7.4-7.5
	uint8_t result = flag;
	if (flag > 0 && flag <= 15) {
		result = flag + 1;
	} else if (flag == 16) {
		result = DatFlagLight;
	} else if (flag == 17) {
		result = DatFlagFloorChange;
	} else if (flag == 18) {
		result = DatFlagFullGround;
	} else if (flag == 19) {
		result = DatFlagElevation;
	} else if (flag == 20) {
		result = DatFlagDisplacement;
	} else if (flag == 22) {
		result = DatFlagMinimapColor;
	} else if (flag == 23) {
		result = DatFlagRotateable;
	} else if (flag == 24) {
		result = DatFlagLyingCorpse;
	} else if (flag == 25) {
		result = DatFlagHangable;
	} else if (flag == 26) {
		result = DatFlagHookSouth;
	} else if (flag == 27) {
		result = DatFlagHookEast;
	} else if (flag == 28) {
		result = DatFlagAnimateAlways;
	}
	if (result == DatFlagMultiUse) {
		return DatFlagForceUse;
	}
	if (result == DatFlagForceUse) {
		return DatFlagMultiUse;
	}
	return result;
}

// Inverse of datFileByteToFlag: canonical DatFlags value -> the byte this
// .dat version stores, or -1 if the flag is not representable in it.
int datFlagToFileByte(DatFormat format, uint8_t flag) {
	if (format >= DAT_FORMAT_1010) {
		if (flag == DatFlagNoMoveAnimation) {
			return 16;
		}
		if (flag >= DatFlagFloorChange) {
			return -1;
		}
		if (flag >= 16) {
			return flag + 1;
		}
		return flag;
	} else if (format >= DAT_FORMAT_86) {
		if (flag >= DatFlagFloorChange) {
			return -1;
		}
		return flag;
	} else if (format >= DAT_FORMAT_78) {
		if (flag == DatFlagChargeable) {
			return 8;
		}
		if (flag >= DatFlagFloorChange) {
			return -1;
		}
		if (flag >= 8) {
			return flag + 1;
		}
		return flag;
	} else if (format >= DAT_FORMAT_755) {
		if (flag == DatFlagFloorChange) {
			return 23;
		}
		if (flag >= DatFlagNoMoveAnimation) {
			return -1;
		}
		return flag;
	}
	// 7.4-7.5 (including the ForceUse/MultiUse swap)
	if (flag == DatFlagForceUse) {
		flag = DatFlagMultiUse;
	} else if (flag == DatFlagMultiUse) {
		flag = DatFlagForceUse;
	}
	switch (flag) {
		case DatFlagGround:
			return 0;
		case DatFlagLight:
			return 16;
		case DatFlagFloorChange:
			return 17;
		case DatFlagFullGround:
			return 18;
		case DatFlagElevation:
			return 19;
		case DatFlagDisplacement:
			return 20;
		case DatFlagMinimapColor:
			return 22;
		case DatFlagRotateable:
			return 23;
		case DatFlagLyingCorpse:
			return 24;
		case DatFlagHangable:
			return 25;
		case DatFlagHookSouth:
			return 26;
		case DatFlagHookEast:
			return 27;
		case DatFlagAnimateAlways:
			return 28;
		default:
			if (flag >= 2 && flag <= 16) {
				return flag - 1;
			}
			return -1;
	}
}

} // namespace

bool GraphicManager::patchSpriteMetadataFlags(const wxString& datafile, uint16_t clientID, const std::vector<DatFlagPatch>& changes, wxString& error) {
	wxFile input(datafile, wxFile::read);
	if (!input.IsOpened()) {
		error = wxString::Format("Could not open '%s'.", datafile);
		return false;
	}
	std::vector<uint8_t> buffer(static_cast<size_t>(input.Length()));
	if (input.Read(buffer.data(), buffer.size()) != static_cast<ssize_t>(buffer.size())) {
		error = wxString::Format("Could not read '%s'.", datafile);
		return false;
	}
	input.Close();

	if (buffer.size() < 12) {
		error = "The .dat file is too small to be valid.";
		return false;
	}
	const uint16_t itemCount = buffer[4] | (buffer[5] << 8);
	if (clientID < 100 || clientID > itemCount) {
		error = wxString::Format("Client id %u is not an item sprite in this .dat file.", static_cast<unsigned>(clientID));
		return false;
	}

	const wxString premature = "The .dat file ended unexpectedly (corrupt file or unsupported format?).";
	size_t pos = 12;
	const auto need = [&](size_t n) {
		return pos + n <= buffer.size();
	};

	// Raw flag entries of the target item: file byte + span in 'buffer'
	// (a synthetic entry added below has offset SIZE_MAX and carries its
	// payload bytes in 'payload' instead).
	struct RawFlag {
		uint8_t byte;
		size_t offset;
		size_t length;
		const std::vector<uint8_t>* payload = nullptr;
	};
	std::vector<RawFlag> rawFlags;
	size_t flagsStart = 0;
	size_t terminatorPos = 0; // offset of the target's 0xFF flag terminator

	for (uint16_t id = 100; id <= clientID; ++id) {
		const bool target = (id == clientID);
		if (target) {
			flagsStart = pos;
		}

		// Flag list, terminated by 0xFF (same walk as loadSpriteMetadataFlags).
		while (true) {
			if (!need(1)) {
				error = premature;
				return false;
			}
			const size_t entryStart = pos;
			const uint8_t raw = buffer[pos++];
			if (raw == 0xFF) {
				if (target) {
					terminatorPos = entryStart;
				}
				break;
			}
			size_t dataLen = 0;
			switch (datFileByteToFlag(dat_format, raw)) {
				case DatFlagGround:
				case DatFlagWritable:
				case DatFlagWritableOnce:
				case DatFlagCloth:
				case DatFlagLensHelp:
				case DatFlagUsable:
				case DatFlagElevation:
				case DatFlagMinimapColor:
					dataLen = 2;
					break;
				case DatFlagLight:
					dataLen = 4;
					break;
				case DatFlagDisplacement:
					dataLen = (dat_format >= DAT_FORMAT_755) ? 4 : 0;
					break;
				case DatFlagMarket: {
					if (!need(8)) {
						error = premature;
						return false;
					}
					const uint16_t nameLength = buffer[pos + 6] | (buffer[pos + 7] << 8);
					dataLen = 6 + 2 + nameLength + 4;
					break;
				}
				default:
					dataLen = 0;
					break;
			}
			if (!need(dataLen)) {
				error = premature;
				return false;
			}
			pos += dataLen;
			if (target) {
				rawFlags.push_back({ raw, entryStart, pos - entryStart });
			}
		}
		if (target) {
			break;
		}

		// Sprite section (items never have frame groups; mirrors loadSpriteMetadata).
		if (!need(2)) {
			error = premature;
			return false;
		}
		const uint8_t width = buffer[pos++];
		const uint8_t height = buffer[pos++];
		if (width > 1 || height > 1) {
			if (!need(1)) {
				error = premature;
				return false;
			}
			++pos; // exact size
		}
		if (!need(dat_format <= DAT_FORMAT_74 ? 4 : 5)) {
			error = premature;
			return false;
		}
		const uint8_t layers = buffer[pos++];
		const uint8_t pattern_x = buffer[pos++];
		const uint8_t pattern_y = buffer[pos++];
		const uint8_t pattern_z = (dat_format <= DAT_FORMAT_74) ? 1 : buffer[pos++];
		const uint8_t frames = buffer[pos++];
		if (frames > 1 && has_frame_durations) {
			if (!need(6 + size_t(frames) * 8)) {
				error = premature;
				return false;
			}
			pos += 6 + size_t(frames) * 8; // async, loop count, start frame, min/max per frame
		}
		const size_t numSprites = size_t(width) * height * layers * pattern_x * pattern_y * pattern_z * frames;
		if (!need(numSprites * (is_extended ? 4 : 2))) {
			error = premature;
			return false;
		}
		pos += numSprites * (is_extended ? 4 : 2);
	}

	// Apply the changes to the raw entry list, keeping it sorted by file byte.
	for (const DatFlagPatch& change : changes) {
		const int fileByte = datFlagToFileByte(dat_format, change.flag);
		if (fileByte < 0) {
			continue; // not representable in this .dat version
		}
		rawFlags.erase(
			std::remove_if(rawFlags.begin(), rawFlags.end(), [&](const RawFlag& rawFlag) { return rawFlag.byte == fileByte; }),
			rawFlags.end()
		);
		if (change.present) {
			auto insertAt = rawFlags.begin();
			while (insertAt != rawFlags.end() && insertAt->byte < fileByte) {
				++insertAt;
			}
			rawFlags.insert(insertAt, { static_cast<uint8_t>(fileByte), SIZE_MAX, 1 + change.data.size(), &change.data });
		}
	}

	// Splice the new flag list into an otherwise verbatim copy of the file.
	std::vector<uint8_t> output;
	output.reserve(buffer.size() + changes.size() * 8);
	output.insert(output.end(), buffer.begin(), buffer.begin() + flagsStart);
	for (const RawFlag& rawFlag : rawFlags) {
		if (rawFlag.offset == SIZE_MAX) {
			output.push_back(rawFlag.byte);
			if (rawFlag.payload) {
				output.insert(output.end(), rawFlag.payload->begin(), rawFlag.payload->end());
			}
		} else {
			output.insert(output.end(), buffer.begin() + rawFlag.offset, buffer.begin() + rawFlag.offset + rawFlag.length);
		}
	}
	output.push_back(0xFF);
	output.insert(output.end(), buffer.begin() + terminatorPos + 1, buffer.end());

	const wxString tmpFilename = datafile + ".tmp";
	{
		wxFile out(tmpFilename, wxFile::write);
		if (!out.IsOpened() || out.Write(output.data(), output.size()) != output.size()) {
			error = wxString::Format("Could not write '%s'.", tmpFilename);
			return false;
		}
	}

	const wxString backup = datafile + ".bak";
	if (!wxFileExists(backup)) {
		wxCopyFile(datafile, backup, false);
	}
	if (!wxRemoveFile(datafile) || !wxRenameFile(tmpFilename, datafile)) {
		error = "Could not replace the .dat file (is it locked by another program?).";
		return false;
	}
	return true;
}

namespace {

// dst(x, y) <- src(sx, sy) for one 32x32 RGBA image.
void transformRGBA32(const uint8_t* src, uint8_t* dst, GraphicManager::SpriteTransform transform) {
	for (int y = 0; y < 32; ++y) {
		for (int x = 0; x < 32; ++x) {
			int sx, sy;
			switch (transform) {
				case GraphicManager::SPRITE_TRANSFORM_ROTATE_90_CW:
					sx = y;
					sy = 31 - x;
					break;
				case GraphicManager::SPRITE_TRANSFORM_FLIP_HORIZONTAL:
					sx = 31 - x;
					sy = y;
					break;
				default: // vertical flip
					sx = x;
					sy = 31 - y;
					break;
			}
			std::memcpy(dst + (y * 32 + x) * 4, src + (sy * 32 + sx) * 4, 4);
		}
	}
}

// RLE-compress one 32x32 RGBA image into .spr dump format: repeated
// [u16 transparent count][u16 colored count][colored pixels], trailing
// transparency implicit. Inverse of NormalImage::getRGBAData.
std::vector<uint8_t> compressSpriteRGBA(const uint8_t* rgba, bool use_alpha) {
	std::vector<uint8_t> out;
	const int total = 32 * 32;
	int i = 0;
	while (i < total) {
		int transparent = 0;
		while (i < total && rgba[i * 4 + 3] == 0) {
			++transparent;
			++i;
		}
		int colored_start = i;
		int colored = 0;
		while (i < total && rgba[i * 4 + 3] != 0) {
			++colored;
			++i;
		}
		if (colored == 0) {
			break; // trailing transparency is implicit
		}
		out.push_back(transparent & 0xFF);
		out.push_back((transparent >> 8) & 0xFF);
		out.push_back(colored & 0xFF);
		out.push_back((colored >> 8) & 0xFF);
		for (int p = colored_start; p < colored_start + colored; ++p) {
			out.push_back(rgba[p * 4 + 0]);
			out.push_back(rgba[p * 4 + 1]);
			out.push_back(rgba[p * 4 + 2]);
			if (use_alpha) {
				out.push_back(rgba[p * 4 + 3]);
			}
		}
	}
	return out;
}

} // namespace

bool GraphicManager::transformSpriteImages(uint16_t clientID, SpriteTransform transform, wxString& error) {
	GameSprite* sprite = dynamic_cast<GameSprite*>(getSprite(clientID));
	if (!sprite || sprite->spriteList.empty()) {
		error = wxString::Format("No sprite loaded for client id %u.", static_cast<unsigned>(clientID));
		return false;
	}
	const int tilesWide = sprite->width;
	const int tilesHigh = sprite->height;
	if (transform == SPRITE_TRANSFORM_ROTATE_90_CW && tilesWide != tilesHigh) {
		error = "Only sprites with a square tile layout can be rotated 90 degrees.";
		return false;
	}

	// Grab every sub-sprite's pixels up front — tile permutation reads
	// positions that may also be written.
	std::vector<std::vector<uint8_t>> sourcePixels(sprite->spriteList.size());
	for (size_t i = 0; i < sprite->spriteList.size(); ++i) {
		GameSprite::NormalImage* image = sprite->spriteList[i];
		if (!image || image->id == 0) {
			continue;
		}
		uint8_t* rgba = image->getRGBAData();
		if (!rgba) {
			error = "Could not load the sprite's pixel data.";
			return false;
		}
		sourcePixels[i].assign(rgba, rgba + 32 * 32 * 4);
		delete[] rgba;
	}

	const bool use_alpha = hasTransparency();
	std::map<uint32_t, std::vector<uint8_t>> newDumps;
	for (int f = 0; f < sprite->frames; ++f) {
		for (int z = 0; z < sprite->pattern_z; ++z) {
			for (int py = 0; py < sprite->pattern_y; ++py) {
				for (int px = 0; px < sprite->pattern_x; ++px) {
					for (int l = 0; l < sprite->layers; ++l) {
						for (int th = 0; th < tilesHigh; ++th) {
							for (int tw = 0; tw < tilesWide; ++tw) {
								int sw, sh;
								switch (transform) {
									case SPRITE_TRANSFORM_ROTATE_90_CW:
										sw = th;
										sh = tilesHigh - 1 - tw;
										break;
									case SPRITE_TRANSFORM_FLIP_HORIZONTAL:
										sw = tilesWide - 1 - tw;
										sh = th;
										break;
									default: // vertical flip
										sw = tw;
										sh = tilesHigh - 1 - th;
										break;
								}
								const size_t dstIndex = sprite->getIndex(tw, th, l, px, py, z, f);
								const size_t srcIndex = sprite->getIndex(sw, sh, l, px, py, z, f);
								if (dstIndex >= sprite->spriteList.size() || srcIndex >= sprite->spriteList.size()) {
									continue;
								}
								GameSprite::NormalImage* dst = sprite->spriteList[dstIndex];
								const std::vector<uint8_t>& src = sourcePixels[srcIndex];
								if (!dst || dst->id == 0) {
									if (!src.empty()) {
										error = "This sprite has empty sub-sprites in its layout and cannot be transformed.";
										return false;
									}
									continue;
								}
								std::vector<uint8_t> outPixels(32 * 32 * 4, 0);
								if (!src.empty()) {
									transformRGBA32(src.data(), outPixels.data(), transform);
								}
								newDumps[dst->id] = compressSpriteRGBA(outPixels.data(), use_alpha);
							}
						}
					}
				}
			}
		}
	}

	if (newDumps.empty()) {
		error = "Nothing to transform.";
		return false;
	}
	return applySpriteImageReplacements(sprite, newDumps, error);
}

bool GraphicManager::importSpriteImage(uint16_t clientID, const wxImage& source, wxString& error) {
	GameSprite* sprite = dynamic_cast<GameSprite*>(getSprite(clientID));
	if (!sprite || sprite->spriteList.empty()) {
		error = wxString::Format("No sprite loaded for client id %u.", static_cast<unsigned>(clientID));
		return false;
	}
	if (!source.IsOk()) {
		error = "The image could not be loaded.";
		return false;
	}

	const int tilesWide = sprite->width;
	const int tilesHigh = sprite->height;
	const int targetWidth = tilesWide * 32;
	const int targetHeight = tilesHigh * 32;

	wxImage image = source;
	if (image.GetWidth() != targetWidth || image.GetHeight() != targetHeight) {
		image = image.Scale(targetWidth, targetHeight, wxIMAGE_QUALITY_NEAREST);
	}
	const unsigned char* rgb = image.GetData();
	const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
	if (!rgb) {
		error = "The image has no pixel data.";
		return false;
	}

	const bool use_alpha = hasTransparency();
	std::map<uint32_t, std::vector<uint8_t>> newDumps;

	for (int th = 0; th < tilesHigh; ++th) {
		for (int tw = 0; tw < tilesWide; ++tw) {
			// Tile (0, 0) is the bottom-right block: it is drawn at the
			// anchor, and higher tile indices extend left/up.
			const int x0 = targetWidth - (tw + 1) * 32;
			const int y0 = targetHeight - (th + 1) * 32;
			std::vector<uint8_t> rgba(32 * 32 * 4, 0);
			bool tileEmpty = true;
			for (int y = 0; y < 32; ++y) {
				for (int x = 0; x < 32; ++x) {
					const size_t srcIndex = static_cast<size_t>(y0 + y) * targetWidth + (x0 + x);
					const unsigned char r = rgb[srcIndex * 3 + 0];
					const unsigned char g = rgb[srcIndex * 3 + 1];
					const unsigned char b = rgb[srcIndex * 3 + 2];
					uint8_t a;
					if (alpha) {
						a = alpha[srcIndex];
					} else {
						a = (r == 255 && g == 0 && b == 255) ? 0 : 255; // magenta = transparent
					}
					if (!use_alpha) {
						a = (a < 128) ? 0 : 255; // no partial alpha without transparency support
					}
					if (a != 0) {
						tileEmpty = false;
					}
					uint8_t* pixel = rgba.data() + (static_cast<size_t>(y) * 32 + x) * 4;
					pixel[0] = r;
					pixel[1] = g;
					pixel[2] = b;
					pixel[3] = a;
				}
			}

			// Write the tile into layer 0 of every frame and pattern; other
			// layers (blend masks) are left untouched.
			for (int f = 0; f < sprite->frames; ++f) {
				for (int z = 0; z < sprite->pattern_z; ++z) {
					for (int py = 0; py < sprite->pattern_y; ++py) {
						for (int px = 0; px < sprite->pattern_x; ++px) {
							const size_t index = sprite->getIndex(tw, th, 0, px, py, z, f);
							if (index >= sprite->spriteList.size()) {
								continue;
							}
							GameSprite::NormalImage* dst = sprite->spriteList[index];
							if (!dst || dst->id == 0) {
								if (!tileEmpty) {
									error = "This sprite has empty sub-sprites in its layout and cannot be replaced.";
									return false;
								}
								continue;
							}
							newDumps[dst->id] = compressSpriteRGBA(rgba.data(), use_alpha);
						}
					}
				}
			}
		}
	}

	if (newDumps.empty()) {
		error = "Nothing to import.";
		return false;
	}
	return applySpriteImageReplacements(sprite, newDumps, error);
}

namespace {

uint32_t readLE32(const std::vector<uint8_t>& buffer, size_t pos) {
	return buffer[pos] | (buffer[pos + 1] << 8) | (buffer[pos + 2] << 16) | (static_cast<uint32_t>(buffer[pos + 3]) << 24);
}

void appendLE16(std::vector<uint8_t>& out, uint32_t value) {
	out.push_back(value & 0xFF);
	out.push_back((value >> 8) & 0xFF);
}

void appendLE32(std::vector<uint8_t>& out, uint32_t value) {
	appendLE16(out, value & 0xFFFF);
	appendLE16(out, value >> 16);
}

bool writeReplacingFile(const wxString& path, const std::vector<uint8_t>& contents, wxString& error) {
	const wxString tmpFilename = path + ".tmp";
	{
		wxFile out(tmpFilename, wxFile::write);
		if (!out.IsOpened() || out.Write(contents.data(), contents.size()) != contents.size()) {
			error = wxString::Format("Could not write '%s'.", tmpFilename);
			return false;
		}
	}
	const wxString backup = path + ".bak";
	if (!wxFileExists(backup)) {
		wxCopyFile(path, backup, false);
	}
	if (!wxRemoveFile(path) || !wxRenameFile(tmpFilename, path)) {
		error = wxString::Format("Could not replace '%s' (is it locked by another program?).", path);
		return false;
	}
	return true;
}

} // namespace

bool GraphicManager::duplicateItemSprite(uint16_t sourceClientId, uint16_t& newClientId, wxString& error) {
	GameSprite* source = dynamic_cast<GameSprite*>(getSprite(sourceClientId));
	if (!source || source->spriteList.empty()) {
		error = wxString::Format("No sprite loaded for client id %u.", static_cast<unsigned>(sourceClientId));
		return false;
	}
	if (sourceClientId < 100 || sourceClientId > item_count) {
		error = wxString::Format("Client id %u is not an item sprite.", static_cast<unsigned>(sourceClientId));
		return false;
	}

	// ---- gather the source's compressed sprite dumps up front
	std::vector<uint32_t> oldIds; // unique, in first-seen order
	std::map<uint32_t, std::vector<uint8_t>> dumps;
	for (GameSprite::NormalImage* image : source->spriteList) {
		if (!image || image->id == 0 || dumps.count(image->id) != 0) {
			continue;
		}
		oldIds.push_back(image->id);
		std::vector<uint8_t>& dump = dumps[image->id];
		if (image->dump && image->size > 0) {
			dump.assign(image->dump, image->dump + image->size);
		} else {
			uint8_t* data = nullptr;
			uint16_t size = 0;
			if (loadSpriteDump(data, size, image->id) && data) {
				dump.assign(data, data + size);
				delete[] data;
			}
			// an empty dump is a fully transparent sprite - valid.
		}
	}
	if (oldIds.empty()) {
		error = "The sprite has no pixel data to copy.";
		return false;
	}

	// ---- .spr: renumber nothing, just grow the offset table and append the
	// copied sprite data as brand-new sprite ids
	const wxString sprPath = sprites_file.GetFullPath();
	std::vector<uint8_t> spr;
	{
		wxFile input(sprPath, wxFile::read);
		if (!input.IsOpened()) {
			error = wxString::Format("Could not open '%s'.", sprPath);
			return false;
		}
		spr.resize(static_cast<size_t>(input.Length()));
		if (input.Read(spr.data(), spr.size()) != static_cast<ssize_t>(spr.size())) {
			error = wxString::Format("Could not read '%s'.", sprPath);
			return false;
		}
	}

	const size_t countSize = is_extended ? 4 : 2;
	if (spr.size() < 4 + countSize) {
		error = "The .spr file is too small to be valid.";
		return false;
	}
	const uint32_t spriteCount = is_extended ? readLE32(spr, 4) : (spr[4] | (spr[5] << 8));
	const size_t newSpriteCount = oldIds.size();
	if (!is_extended && spriteCount + newSpriteCount > 0xFFFF) {
		error = "The .spr sprite id space is full (non-extended clients hold at most 65535 sprites).";
		return false;
	}
	const size_t tableStart = 4 + countSize;
	const size_t tableEnd = tableStart + static_cast<size_t>(spriteCount) * 4;
	if (tableEnd > spr.size()) {
		error = "The .spr file is corrupt (offset table exceeds the file).";
		return false;
	}

	// old sprite id -> newly assigned id (0 stays 0)
	std::map<uint32_t, uint32_t> spriteIdMap;
	for (size_t i = 0; i < oldIds.size(); ++i) {
		spriteIdMap[oldIds[i]] = spriteCount + 1 + static_cast<uint32_t>(i);
	}

	const uint32_t tableGrowth = static_cast<uint32_t>(newSpriteCount * 4);
	std::vector<uint8_t> sprOut;
	sprOut.reserve(spr.size() + tableGrowth + newSpriteCount * 64);
	sprOut.insert(sprOut.end(), spr.begin(), spr.begin() + 4); // signature
	if (is_extended) {
		appendLE32(sprOut, spriteCount + static_cast<uint32_t>(newSpriteCount));
	} else {
		appendLE16(sprOut, spriteCount + static_cast<uint32_t>(newSpriteCount));
	}
	// existing offsets, shifted by the table growth (0 = empty sprite stays 0)
	for (uint32_t i = 0; i < spriteCount; ++i) {
		const uint32_t offset = readLE32(spr, tableStart + static_cast<size_t>(i) * 4);
		appendLE32(sprOut, offset == 0 ? 0 : offset + tableGrowth);
	}
	// table entries for the new sprites, pointing past the (shifted) old data
	uint32_t appendPos = static_cast<uint32_t>(spr.size()) + tableGrowth;
	for (uint32_t oldId : oldIds) {
		appendLE32(sprOut, appendPos);
		appendPos += static_cast<uint32_t>(5 + dumps[oldId].size());
	}
	// old sprite data verbatim, then the copied sprites
	sprOut.insert(sprOut.end(), spr.begin() + tableEnd, spr.end());
	for (uint32_t oldId : oldIds) {
		const std::vector<uint8_t>& dump = dumps[oldId];
		sprOut.push_back(0xFF);
		sprOut.push_back(0x00);
		sprOut.push_back(0xFF);
		appendLE16(sprOut, static_cast<uint32_t>(dump.size()));
		sprOut.insert(sprOut.end(), dump.begin(), dump.end());
	}
	spr.clear();
	spr.shrink_to_fit();

	// ---- .dat: clone the source entry (sprite ids remapped) at the end of
	// the item section, item count + 1
	const wxString datPath = metadata_file.GetFullPath();
	std::vector<uint8_t> dat;
	{
		wxFile input(datPath, wxFile::read);
		if (!input.IsOpened()) {
			error = wxString::Format("Could not open '%s'.", datPath);
			return false;
		}
		dat.resize(static_cast<size_t>(input.Length()));
		if (input.Read(dat.data(), dat.size()) != static_cast<ssize_t>(dat.size())) {
			error = wxString::Format("Could not read '%s'.", datPath);
			return false;
		}
	}
	if (dat.size() < 12) {
		error = "The .dat file is too small to be valid.";
		return false;
	}
	const uint16_t datItemCount = dat[4] | (dat[5] << 8);
	if (sourceClientId > datItemCount) {
		error = "The .dat file does not contain the source sprite.";
		return false;
	}

	const wxString premature = "The .dat file ended unexpectedly (corrupt file or unsupported format?).";
	size_t pos = 12;
	const auto need = [&](size_t n) {
		return pos + n <= dat.size();
	};

	size_t sourceStart = 0;
	size_t sourceEnd = 0;
	std::vector<std::pair<size_t, uint32_t>> spriteIdFields; // offset in dat + old id

	for (uint16_t id = 100; id <= datItemCount; ++id) {
		const bool target = (id == sourceClientId);
		if (target) {
			sourceStart = pos;
		}
		// flag list (same walk as patchSpriteMetadataFlags)
		while (true) {
			if (!need(1)) {
				error = premature;
				return false;
			}
			const uint8_t raw = dat[pos++];
			if (raw == 0xFF) {
				break;
			}
			size_t dataLen = 0;
			switch (datFileByteToFlag(dat_format, raw)) {
				case DatFlagGround:
				case DatFlagWritable:
				case DatFlagWritableOnce:
				case DatFlagCloth:
				case DatFlagLensHelp:
				case DatFlagUsable:
				case DatFlagElevation:
				case DatFlagMinimapColor:
					dataLen = 2;
					break;
				case DatFlagLight:
					dataLen = 4;
					break;
				case DatFlagDisplacement:
					dataLen = (dat_format >= DAT_FORMAT_755) ? 4 : 0;
					break;
				case DatFlagMarket: {
					if (!need(8)) {
						error = premature;
						return false;
					}
					const uint16_t nameLength = dat[pos + 6] | (dat[pos + 7] << 8);
					dataLen = 6 + 2 + nameLength + 4;
					break;
				}
				default:
					break;
			}
			if (!need(dataLen)) {
				error = premature;
				return false;
			}
			pos += dataLen;
		}
		// sprite section
		if (!need(2)) {
			error = premature;
			return false;
		}
		const uint8_t width = dat[pos++];
		const uint8_t height = dat[pos++];
		if (width > 1 || height > 1) {
			if (!need(1)) {
				error = premature;
				return false;
			}
			++pos; // exact size
		}
		if (!need(dat_format <= DAT_FORMAT_74 ? 4 : 5)) {
			error = premature;
			return false;
		}
		const uint8_t layers = dat[pos++];
		const uint8_t pattern_x = dat[pos++];
		const uint8_t pattern_y = dat[pos++];
		const uint8_t pattern_z = (dat_format <= DAT_FORMAT_74) ? 1 : dat[pos++];
		const uint8_t frames = dat[pos++];
		if (frames > 1 && has_frame_durations) {
			if (!need(6 + static_cast<size_t>(frames) * 8)) {
				error = premature;
				return false;
			}
			pos += 6 + static_cast<size_t>(frames) * 8;
		}
		const size_t numSprites = static_cast<size_t>(width) * height * layers * pattern_x * pattern_y * pattern_z * frames;
		const size_t idSize = is_extended ? 4 : 2;
		if (!need(numSprites * idSize)) {
			error = premature;
			return false;
		}
		if (target) {
			for (size_t i = 0; i < numSprites; ++i) {
				const size_t fieldPos = pos + i * idSize;
				const uint32_t oldId = is_extended ? readLE32(dat, fieldPos) : (dat[fieldPos] | (dat[fieldPos + 1] << 8));
				spriteIdFields.push_back({ fieldPos, oldId });
			}
		}
		pos += numSprites * idSize;
		if (target) {
			sourceEnd = pos;
		}
	}
	const size_t itemSectionEnd = pos;
	if (sourceEnd == 0) {
		error = "Could not locate the source sprite's .dat entry.";
		return false;
	}

	// clone the entry with remapped sprite ids
	std::vector<uint8_t> newEntry(dat.begin() + sourceStart, dat.begin() + sourceEnd);
	for (const std::pair<size_t, uint32_t>& field : spriteIdFields) {
		const size_t relative = field.first - sourceStart;
		const uint32_t mapped = (field.second == 0) ? 0 : spriteIdMap[field.second];
		newEntry[relative] = mapped & 0xFF;
		newEntry[relative + 1] = (mapped >> 8) & 0xFF;
		if (is_extended) {
			newEntry[relative + 2] = (mapped >> 16) & 0xFF;
			newEntry[relative + 3] = (mapped >> 24) & 0xFF;
		}
	}

	std::vector<uint8_t> datOut;
	datOut.reserve(dat.size() + newEntry.size());
	datOut.insert(datOut.end(), dat.begin(), dat.begin() + 4); // signature
	appendLE16(datOut, static_cast<uint32_t>(datItemCount) + 1);
	datOut.insert(datOut.end(), dat.begin() + 6, dat.begin() + itemSectionEnd);
	datOut.insert(datOut.end(), newEntry.begin(), newEntry.end());
	datOut.insert(datOut.end(), dat.begin() + itemSectionEnd, dat.end());

	// ---- write both files (spr first: a .dat referencing missing sprites is
	// worse than an orphaned sprite tail)
	if (!writeReplacingFile(sprPath, sprOut, error)) {
		return false;
	}
	if (!writeReplacingFile(datPath, datOut, error)) {
		return false;
	}

	// ---- register the new sprite in memory
	++item_count;
	newClientId = item_count;

	for (uint32_t oldId : oldIds) {
		const uint32_t newId = spriteIdMap[oldId];
		GameSprite::NormalImage* image = newd GameSprite::NormalImage();
		image->id = newId;
		const std::vector<uint8_t>& dump = dumps[oldId];
		image->size = static_cast<uint16_t>(dump.size());
		if (image->size > 0) {
			image->dump = newd uint8_t[image->size];
			std::memcpy(image->dump, dump.data(), image->size);
		}
		image_space[newId] = image;
	}

	GameSprite* clone = newd GameSprite();
	clone->id = newClientId;
	clone->width = source->width;
	clone->height = source->height;
	clone->layers = source->layers;
	clone->pattern_x = source->pattern_x;
	clone->pattern_y = source->pattern_y;
	clone->pattern_z = source->pattern_z;
	clone->frames = source->frames;
	clone->numsprites = source->numsprites;
	clone->draw_height = source->draw_height;
	clone->drawoffset_x = source->drawoffset_x;
	clone->drawoffset_y = source->drawoffset_y;
	clone->minimap_color = source->minimap_color;
	clone->has_light = source->has_light;
	clone->light = source->light;
	if (source->animator) {
		clone->animator = newd Animator(source->frames, source->animator->getStartFrame(), source->animator->getLoopCount(), source->animator->isAsync());
		for (int i = 0; i < source->frames; ++i) {
			FrameDuration* from = source->animator->getFrameDuration(i);
			clone->animator->getFrameDuration(i)->setValues(from->min, from->max);
		}
		clone->animator->reset();
	}
	for (GameSprite::NormalImage* image : source->spriteList) {
		uint32_t mappedId = 0;
		if (image && image->id != 0) {
			mappedId = spriteIdMap[image->id];
		}
		if (image_space[mappedId] == nullptr) {
			GameSprite::NormalImage* empty = newd GameSprite::NormalImage();
			empty->id = mappedId;
			image_space[mappedId] = empty;
		}
		clone->spriteList.push_back(static_cast<GameSprite::NormalImage*>(image_space[mappedId]));
	}
	sprite_space[newClientId] = clone;
	return true;
}

bool GraphicManager::applySpriteImageReplacements(GameSprite* sprite, const std::map<uint32_t, std::vector<uint8_t>>& newDumps, wxString& error) {
	std::vector<std::pair<uint32_t, std::vector<uint8_t>>> replacements(newDumps.begin(), newDumps.end());
	if (!patchSpriteImages(replacements, error)) {
		return false;
	}

	// Refresh the in-memory images so the editor redraws with the new pixels.
	for (const std::pair<uint32_t, std::vector<uint8_t>>& replacement : replacements) {
		GameSprite::NormalImage* image = dynamic_cast<GameSprite::NormalImage*>(image_space[replacement.first]);
		if (!image) {
			continue;
		}
		delete[] image->dump;
		image->size = static_cast<uint16_t>(replacement.second.size());
		if (image->size > 0) {
			image->dump = newd uint8_t[image->size];
			std::memcpy(image->dump, replacement.second.data(), image->size);
		} else {
			image->dump = nullptr;
		}
		if (image->isGLLoaded) {
			image->unloadGLTexture(0);
		}
	}
	sprite->unloadDC();
	palette_refresh_needed = true;
	return true;
}

bool GraphicManager::patchSpriteImages(const std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& replacements, wxString& error) {
	const wxString path = sprites_file.GetFullPath();
	if (path.IsEmpty() || !wxFileExists(path)) {
		error = "The .spr file could not be located.";
		return false;
	}

	const wxString backup = path + ".bak";
	if (!wxFileExists(backup)) {
		wxCopyFile(path, backup, false);
	}

	wxFile file(path, wxFile::read_write);
	if (!file.IsOpened()) {
		error = wxString::Format("Could not open '%s' for writing.", path);
		return false;
	}

	for (const std::pair<uint32_t, std::vector<uint8_t>>& replacement : replacements) {
		// Append [3-byte color key][u16 size][dump] at the end of the file...
		const wxFileOffset newOffset = file.Seek(0, wxFromEnd);
		if (newOffset == wxInvalidOffset || newOffset > 0xFFFFFFFFll) {
			error = "The .spr file is too large to append to.";
			return false;
		}
		const uint16_t dumpSize = static_cast<uint16_t>(replacement.second.size());
		const uint8_t header[5] = { 0xFF, 0x00, 0xFF, static_cast<uint8_t>(dumpSize & 0xFF), static_cast<uint8_t>((dumpSize >> 8) & 0xFF) };
		if (file.Write(header, 5) != 5 || (dumpSize > 0 && file.Write(replacement.second.data(), dumpSize) != dumpSize)) {
			error = "Could not append sprite data to the .spr file.";
			return false;
		}

		// ...then re-point this sprite's offset table entry at it (same
		// location loadSpriteDump reads from).
		const uint32_t offsetValue = static_cast<uint32_t>(newOffset);
		const uint8_t offsetBytes[4] = {
			static_cast<uint8_t>(offsetValue & 0xFF),
			static_cast<uint8_t>((offsetValue >> 8) & 0xFF),
			static_cast<uint8_t>((offsetValue >> 16) & 0xFF),
			static_cast<uint8_t>((offsetValue >> 24) & 0xFF)
		};
		if (file.Seek((is_extended ? 4 : 2) + replacement.first * sizeof(uint32_t)) == wxInvalidOffset || file.Write(offsetBytes, 4) != 4) {
			error = "Could not update the .spr sprite offset table.";
			return false;
		}
	}
	return true;
}

bool GraphicManager::loadSpriteData(const FileName& datafile, wxString& error, wxArrayString& warnings) {
	FileReadHandle fh(nstr(datafile.GetFullPath()));

	if (!fh.isOk()) {
		error = "Failed to open file for reading";
		return false;
	}

#define safe_get(func, ...)                      \
	do {                                         \
		if (!fh.get##func(__VA_ARGS__)) {        \
			error = wxstr(fh.getErrorMessage()); \
			return false;                        \
		}                                        \
	} while (false)

	uint32_t sprSignature;
	safe_get(U32, sprSignature);

	uint32_t total_pics = 0;
	if (is_extended) {
		safe_get(U32, total_pics);
	} else {
		uint16_t u16 = 0;
		safe_get(U16, u16);
		total_pics = u16;
	}

	if (!g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
		spritefile = nstr(datafile.GetFullPath());
		unloaded = false;
		return true;
	}

	std::vector<uint32_t> sprite_indexes;
	for (uint32_t i = 0; i < total_pics; ++i) {
		uint32_t index;
		safe_get(U32, index);
		sprite_indexes.push_back(index);
	}

	// Now read individual sprites
	int id = 1;
	for (std::vector<uint32_t>::iterator sprite_iter = sprite_indexes.begin(); sprite_iter != sprite_indexes.end(); ++sprite_iter, ++id) {
		uint32_t index = *sprite_iter + 3;
		fh.seek(index);
		uint16_t size;
		safe_get(U16, size);

		ImageMap::iterator it = image_space.find(id);
		if (it != image_space.end()) {
			GameSprite::NormalImage* spr = dynamic_cast<GameSprite::NormalImage*>(it->second);
			if (spr && size > 0) {
				if (spr->size > 0) {
					wxString ss;
					ss << "items.spr: Duplicate GameSprite id " << id;
					warnings.push_back(ss);
					fh.seekRelative(size);
				} else {
					spr->id = id;
					spr->size = size;
					spr->dump = newd uint8_t[size];
					if (!fh.getRAW(spr->dump, size)) {
						error = wxstr(fh.getErrorMessage());
						return false;
					}
				}
			}
		} else {
			fh.seekRelative(size);
		}
	}
#undef safe_get
	unloaded = false;
	return true;
}

bool GraphicManager::loadSpriteDump(uint8_t*& target, uint16_t& size, int sprite_id) {
	if (g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
		return false;
	}

	if (sprite_id == 0) {
		// Empty GameSprite
		size = 0;
		target = nullptr;
		return true;
	}

	FileReadHandle fh(spritefile);
	if (!fh.isOk()) {
		return false;
	}
	unloaded = false;

	if (!fh.seek((is_extended ? 4 : 2) + sprite_id * sizeof(uint32_t))) {
		return false;
	}

	uint32_t to_seek = 0;
	if (fh.getU32(to_seek)) {
		fh.seek(to_seek + 3);
		uint16_t sprite_size;
		if (fh.getU16(sprite_size)) {
			target = newd uint8_t[sprite_size];
			if (fh.getRAW(target, sprite_size)) {
				size = sprite_size;
				return true;
			}
			delete[] target;
			target = nullptr;
		}
	}
	return false;
}

void GraphicManager::addSpriteToCleanup(GameSprite* spr) {
	cleanup_list.push_back(spr);
	// Clean if needed
	if (cleanup_list.size() > std::max<uint32_t>(100, g_settings.getInteger(Config::SOFTWARE_CLEAN_THRESHOLD))) {
		const int clean_count = g_settings.getInteger(Config::SOFTWARE_CLEAN_SIZE);
		for (int i = 0; i < clean_count && !cleanup_list.empty(); ++i) {
			GameSprite* victim = cleanup_list.front();
			cleanup_list.pop_front();
			if (victim == spr) {
				// GameSprite::getDC() calls this right after building a DC for
				// `spr` and returns dc[size][frame] afterwards. A sprite is
				// pushed once per (size, frame) it renders, so an *older* entry
				// for `spr` can sit near the front here; unloading it would
				// destroy the DC the caller is about to hand out and leave it
				// indexing an emptied vector (use-after-free while scrolling
				// the palette). Drop the stale entry without unloading - the
				// entry just pushed above keeps it scheduled for later.
				continue;
			}
			victim->unloadDC();
		}
		palette_refresh_needed = true;
	}
}

bool GraphicManager::takePaletteRefreshNeeded() {
	const bool needed = palette_refresh_needed;
	palette_refresh_needed = false;
	return needed;
}

void GraphicManager::garbageCollection() {
	if (g_settings.getInteger(Config::TEXTURE_MANAGEMENT)) {
		int t = time(nullptr);
		if (loaded_textures > g_settings.getInteger(Config::TEXTURE_CLEAN_THRESHOLD) && t - lastclean > g_settings.getInteger(Config::TEXTURE_CLEAN_PULSE)) {
			ImageMap::iterator iit = image_space.begin();
			while (iit != image_space.end()) {
				iit->second->clean(t);
				++iit;
			}
			SpriteMap::iterator sit = sprite_space.begin();
			while (sit != sprite_space.end()) {
				GameSprite* gs = dynamic_cast<GameSprite*>(sit->second);
				if (gs) {
					gs->clean(t);
				}
				++sit;
			}
			lastclean = t;
		}
	}
}

EditorSprite::EditorSprite(wxBitmap* b16x16, wxBitmap* b32x32) {
	bm[SPRITE_SIZE_16x16] = b16x16;
	bm[SPRITE_SIZE_32x32] = b32x32;
	bm[SPRITE_SIZE_ACTUAL] = nullptr;
}

EditorSprite::~EditorSprite() {
	unloadDC();
}

void EditorSprite::DrawTo(wxDC* dc, SpriteSize sz, int start_x, int start_y, int width, int height, int /*frame*/) {
	wxBitmap* sp = bm[sz];
	if (sp) {
		// If width and height are specified and larger than bitmap size, scale the bitmap
		if (width > 0 && height > 0 && (width != sp->GetWidth() || height != sp->GetHeight())) {
			wxImage img = sp->ConvertToImage();
			img.Rescale(width, height, wxIMAGE_QUALITY_NEAREST);
			wxBitmap scaledBmp(img);
			dc->DrawBitmap(scaledBmp, start_x, start_y, true);
		} else {
			dc->DrawBitmap(*sp, start_x, start_y, true);
		}
	}
}

wxBitmap* EditorSprite::getBitmap(SpriteSize sz, int /*frame*/) {
	if (sz == SPRITE_SIZE_ACTUAL) {
		sz = SPRITE_SIZE_32x32;
	}
	return bm[sz];
}

void EditorSprite::unloadDC() {
	delete bm[SPRITE_SIZE_16x16];
	delete bm[SPRITE_SIZE_32x32];
	bm[SPRITE_SIZE_16x16] = nullptr;
	bm[SPRITE_SIZE_32x32] = nullptr;
	bm[SPRITE_SIZE_ACTUAL] = nullptr;
}

GameSprite::GameSprite() :
	id(0),
	height(0),
	width(0),
	layers(0),
	pattern_x(0),
	pattern_y(0),
	pattern_z(0),
	frames(0),
	numsprites(0),
	animator(nullptr),
	draw_height(0),
	drawoffset_x(0),
	drawoffset_y(0),
	minimap_color(0) {
	////
}

GameSprite::~GameSprite() {
	unloadDC();
	for (std::list<TemplateImage*>::iterator iter = instanced_templates.begin(); iter != instanced_templates.end(); ++iter) {
		delete *iter;
	}

	delete animator;
}

void GameSprite::clean(int time) {
	for (std::list<TemplateImage*>::iterator iter = instanced_templates.begin();
		 iter != instanced_templates.end();
		 ++iter) {
		(*iter)->clean(time);
	}
}

void GameSprite::unloadDC() {
	for (int size = 0; size < SPRITE_SIZE_COUNT; ++size) {
		for (wxMemoryDC* mdc : dc[size]) {
			delete mdc;
		}
		dc[size].clear();
		for (wxBitmap* bitmap : bm[size]) {
			delete bitmap;
		}
		bm[size].clear();
	}
}

wxBitmap* GameSprite::getBitmap(SpriteSize sz, int frame) {
	if (!getDC(sz, frame)) {
		return nullptr;
	}
	const int frame_count = std::max<int>(1, frames);
	frame = ((frame % frame_count) + frame_count) % frame_count;
	return bm[sz][frame];
}

int GameSprite::getCurrentFrame() {
	if (!isAnimated()) {
		return 0;
	}
	return animator->getFrame();
}

int GameSprite::getDrawHeight() const {
	return draw_height;
}

std::pair<int, int> GameSprite::getDrawOffset() const {
	return std::make_pair(drawoffset_x, drawoffset_y);
}

uint8_t GameSprite::getMiniMapColor() const {
	return minimap_color;
}

int GameSprite::getIndex(int width, int height, int layer, int pattern_x, int pattern_y, int pattern_z, int frame) const {
	return ((((((frame % this->frames) * this->pattern_z + pattern_z) * this->pattern_y + pattern_y) * this->pattern_x + pattern_x) * this->layers + layer) * this->height + height) * this->width + width;
}

GLuint GameSprite::getHardwareID(int _x, int _y, int _layer, int _count, int _pattern_x, int _pattern_y, int _pattern_z, int _frame) {
	uint32_t v;
	if (_count >= 0 && height <= 1 && width <= 1 && layers <= 1) {
		v = _count;
	} else {
		v = ((((((_frame % frames) * pattern_z + _pattern_z) * pattern_y + _pattern_y) * pattern_x + _pattern_x) * layers + _layer) * height + _y) * width + _x;
	}
	if (v >= numsprites) {
		if (numsprites == 1) {
			v = 0;
		} else {
			v %= numsprites;
		}
	}
	return spriteList[v]->getHardwareID();
}

GameSprite::TemplateImage* GameSprite::getTemplateImage(int sprite_index, const Outfit& outfit) {
	if (instanced_templates.empty()) {
		TemplateImage* img = newd TemplateImage(this, sprite_index, outfit);
		instanced_templates.push_back(img);
		return img;
	}
	// While this is linear lookup, it is very rare for the list to contain more than 4-8 entries, so it's faster than a hashmap anyways.
	for (std::list<TemplateImage*>::iterator iter = instanced_templates.begin(); iter != instanced_templates.end(); ++iter) {
		TemplateImage* img = *iter;
		if (img->sprite_index == sprite_index) {
			uint32_t lookHash = img->lookHead << 24 | img->lookBody << 16 | img->lookLegs << 8 | img->lookFeet;
			if (outfit.getColorHash() == lookHash) {
				return img;
			}
		}
	}
	TemplateImage* img = newd TemplateImage(this, sprite_index, outfit);
	instanced_templates.push_back(img);
	return img;
}

GLuint GameSprite::getHardwareID(int _x, int _y, int _dir, int _addon, int _pattern_z, const Outfit& _outfit, int _frame) {
	uint32_t v = getIndex(_x, _y, 0, _dir, _addon, _pattern_z, _frame);
	if (v >= numsprites) {
		if (numsprites == 1) {
			v = 0;
		} else {
			v %= numsprites;
		}
	}
	if (layers > 1) { // Template
		TemplateImage* img = getTemplateImage(v, _outfit);
		return img->getHardwareID();
	}
	return spriteList[v]->getHardwareID();
}

wxMemoryDC* GameSprite::getDC(SpriteSize size, int frame) {
	ASSERT(size == SPRITE_SIZE_16x16 || size == SPRITE_SIZE_32x32 || size == SPRITE_SIZE_ACTUAL);

	const int frame_count = std::max<int>(1, frames);
	frame = ((frame % frame_count) + frame_count) % frame_count;
	if (dc[size].size() < static_cast<size_t>(frame_count)) {
		dc[size].resize(frame_count, nullptr);
		bm[size].resize(frame_count, nullptr);
	}

	if (!dc[size][frame]) {
		ASSERT(width >= 1 && height >= 1);

		const int bgshade = g_settings.getInteger(Config::ICON_BACKGROUND);

		const int image_size = std::max<int>(width, height) * SPRITE_PIXELS;
		wxImage image(image_size, image_size);
		if (bgshade < 0) {
			unsigned char* data = image.GetData();
			const int bytes = image_size * image_size * 3;
			for (int i = 0; i < bytes; i += 3) {
				data[i] = 0xFF;
				data[i + 1] = 0x00;
				data[i + 2] = 0xFF;
			}
			image.SetMaskColour(0xFF, 0x00, 0xFF);
		} else {
			image.Clear(bgshade);
		}

		for (uint8_t l = 0; l < layers; l++) {
			for (uint8_t w = 0; w < width; w++) {
				for (uint8_t h = 0; h < height; h++) {
					const int i = getIndex(w, h, l, 0, 0, 0, frame);
					// Some sprite metadata declares more pattern/frame slots than
					// the .spr file actually stores; getHardwareID() clamps for the
					// same reason. Skip out-of-range indices instead of reading
					// past the end of spriteList.
					if (i < 0 || static_cast<uint32_t>(i) >= numsprites || static_cast<size_t>(i) >= spriteList.size()) {
						continue;
					}
					uint8_t* data = spriteList[i]->getRGBData();
					if (data) {
						// Blend manually instead of wxImage::Paste + SetMaskColour: Paste does not
						// reliably skip transparent (magenta) source pixels, which erases
						// already-drawn lower layers when compositing multi-layer sprites (e.g. a
						// door's frame layer getting wiped out by the front layer's empty space).
						const int dst_x = (width - w - 1) * SPRITE_PIXELS;
						const int dst_y = (height - h - 1) * SPRITE_PIXELS;
						for (int py = 0; py < SPRITE_PIXELS; ++py) {
							for (int px = 0; px < SPRITE_PIXELS; ++px) {
								const int si = (py * SPRITE_PIXELS + px) * 3;
								const uint8_t r = data[si + 0];
								const uint8_t g = data[si + 1];
								const uint8_t b = data[si + 2];
								if (r == 0xFF && g == 0x00 && b == 0xFF) {
									continue;
								}
								image.SetRGB(dst_x + px, dst_y + py, r, g, b);
							}
						}
						delete[] data;
					}
				}
			}
		}

		if (size == SPRITE_SIZE_16x16) {
			image.Rescale(16, 16, wxIMAGE_QUALITY_NEAREST);
		} else if (size == SPRITE_SIZE_32x32) {
			if (image.GetWidth() != SPRITE_PIXELS || image.GetHeight() != SPRITE_PIXELS) {
				image.Rescale(32, 32, wxIMAGE_QUALITY_NEAREST);
			}
		}
		bm[size][frame] = newd wxBitmap(image, -1);
		dc[size][frame] = newd wxMemoryDC(*bm[size][frame]);
		g_gui.gfx.addSpriteToCleanup(this);
		image.Destroy();
	}
	return dc[size][frame];
}

wxImage GameSprite::getCreatureImage(int dir, int addon, int pattern_z, const Outfit& outfit, int frame) {
	const int bgshade = g_settings.getInteger(Config::ICON_BACKGROUND);

	const int image_size = std::max<int>(width, height) * SPRITE_PIXELS;
	wxImage image(image_size, image_size);
	if (bgshade < 0) {
		unsigned char* data = image.GetData();
		const int bytes = image_size * image_size * 3;
		for (int i = 0; i < bytes; i += 3) {
			data[i] = 0xFF;
			data[i + 1] = 0x00;
			data[i + 2] = 0xFF;
		}
		image.SetMaskColour(0xFF, 0x00, 0xFF);
	} else {
		image.Clear(bgshade);
	}

	for (uint8_t w = 0; w < width; w++) {
		for (uint8_t h = 0; h < height; h++) {
			const int i = getIndex(w, h, 0, dir, addon, pattern_z, frame);
			uint8_t* data = nullptr;
			if (layers > 1) {
				data = getTemplateImage(i, outfit)->getRGBData();
			} else if (i < int(numsprites)) {
				data = spriteList[i]->getRGBData();
			}
			if (data) {
				wxImage img(SPRITE_PIXELS, SPRITE_PIXELS, data);
				img.SetMaskColour(0xFF, 0x00, 0xFF);
				image.Paste(img, (width - w - 1) * SPRITE_PIXELS, (height - h - 1) * SPRITE_PIXELS);
				img.Destroy();
			}
		}
	}
	return image;
}

void GameSprite::DrawTo(wxDC* dc, SpriteSize sz, int start_x, int start_y, int width, int height, int frame) {
	int draw_width = width;
	int draw_height = height;
	if (sz == SPRITE_SIZE_ACTUAL) {
		int actual_width = std::max<int>(1, int(this->width)) * SPRITE_PIXELS;
		int actual_height = std::max<int>(1, int(this->height)) * SPRITE_PIXELS;
		if (draw_width == -1) {
			draw_width = actual_width;
		}
		if (draw_height == -1) {
			draw_height = actual_height;
		}
	} else {
		if (draw_width == -1) {
			draw_width = sz == SPRITE_SIZE_32x32 ? 32 : 16;
		}
		if (draw_height == -1) {
			draw_height = sz == SPRITE_SIZE_32x32 ? 32 : 16;
		}
	}
	wxDC* sdc = getDC(sz, frame);
	if (sdc) {
		dc->Blit(start_x, start_y, draw_width, draw_height, sdc, 0, 0, wxCOPY, true);
	} else {
		const wxBrush& b = dc->GetBrush();
		dc->SetBrush(*wxRED_BRUSH);
		dc->DrawRectangle(start_x, start_y, draw_width, draw_height);
		dc->SetBrush(b);
	}
}

GameSprite::Image::Image() :
	isGLLoaded(false),
	lastaccess(0) {
	////
}

GameSprite::Image::~Image() {
	unloadGLTexture(0);
}

void GameSprite::Image::createGLTexture(GLuint whatid) {
	ASSERT(!isGLLoaded);

	uint8_t* rgba = getRGBAData();
	if (!rgba) {
		return;
	}

	isGLLoaded = true;
	g_gui.gfx.loaded_textures += 1;

	glBindTexture(GL_TEXTURE_2D, whatid);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // Nearest-neighbor
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // Nearest-neighbor
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F); // GL_CLAMP_TO_EDGE
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F); // GL_CLAMP_TO_EDGE
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE_PIXELS, SPRITE_PIXELS, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	delete[] rgba;
#undef SPRITE_SIZE
}

void GameSprite::Image::unloadGLTexture(GLuint whatid) {
	isGLLoaded = false;
	g_gui.gfx.loaded_textures -= 1;
	glDeleteTextures(1, &whatid);
}

void GameSprite::Image::visit() {
	lastaccess = time(nullptr);
}

void GameSprite::Image::clean(int time) {
	if (isGLLoaded && time - lastaccess > g_settings.getInteger(Config::TEXTURE_LONGEVITY)) {
		unloadGLTexture(0);
	}
}

GameSprite::NormalImage::NormalImage() :
	id(0),
	size(0),
	dump(nullptr),
	gl_tid(0) {
	////
}

GameSprite::NormalImage::~NormalImage() {
	delete[] dump;
}

void GameSprite::NormalImage::clean(int time) {
	Image::clean(time);
	if (time - lastaccess > 5 && !g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) { // We keep dumps around for 5 seconds.
		delete[] dump;
		dump = nullptr;
	}
}

uint8_t* GameSprite::NormalImage::getRGBData() {
	if (!dump) {
		if (g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
			return nullptr;
		}

		if (!g_gui.gfx.loadSpriteDump(dump, size, id)) {
			return nullptr;
		}
	}

	const int pixels_data_size = SPRITE_PIXELS * SPRITE_PIXELS * 3;
	uint8_t* data = newd uint8_t[pixels_data_size];
	uint8_t bpp = g_gui.gfx.hasTransparency() ? 4 : 3;
	int write = 0;
	int read = 0;

	// decompress pixels
	while (read < size && write < pixels_data_size) {
		int transparent = dump[read] | dump[read + 1] << 8;
		read += 2;
		for (int i = 0; i < transparent && write < pixels_data_size; i++) {
			data[write + 0] = 0xFF; // red
			data[write + 1] = 0x00; // green
			data[write + 2] = 0xFF; // blue
			write += 3;
		}

		int colored = dump[read] | dump[read + 1] << 8;
		read += 2;
		for (int i = 0; i < colored && write < pixels_data_size; i++) {
			data[write + 0] = dump[read + 0]; // red
			data[write + 1] = dump[read + 1]; // green
			data[write + 2] = dump[read + 2]; // blue
			write += 3;
			read += bpp;
		}
	}

	// fill remaining pixels
	while (write < pixels_data_size) {
		data[write + 0] = 0xFF; // red
		data[write + 1] = 0x00; // green
		data[write + 2] = 0xFF; // blue
		write += 3;
	}
	return data;
}

uint8_t* GameSprite::NormalImage::getRGBAData() {
	if (!dump) {
		if (g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
			return nullptr;
		}

		if (!g_gui.gfx.loadSpriteDump(dump, size, id)) {
			return nullptr;
		}
	}

	const int pixels_data_size = SPRITE_PIXELS_SIZE * 4;
	uint8_t* data = newd uint8_t[pixels_data_size];
	bool use_alpha = g_gui.gfx.hasTransparency();
	uint8_t bpp = use_alpha ? 4 : 3;
	int write = 0;
	int read = 0;

	// decompress pixels
	while (read < size && write < pixels_data_size) {
		int transparent = dump[read] | dump[read + 1] << 8;
		if (use_alpha && transparent >= SPRITE_PIXELS_SIZE) { // Corrupted sprite?
			break;
		}
		read += 2;
		for (int i = 0; i < transparent && write < pixels_data_size; i++) {
			data[write + 0] = 0x00; // red
			data[write + 1] = 0x00; // green
			data[write + 2] = 0x00; // blue
			data[write + 3] = 0x00; // alpha
			write += 4;
		}

		int colored = dump[read] | dump[read + 1] << 8;
		read += 2;
		for (int i = 0; i < colored && write < pixels_data_size; i++) {
			data[write + 0] = dump[read + 0]; // red
			data[write + 1] = dump[read + 1]; // green
			data[write + 2] = dump[read + 2]; // blue
			data[write + 3] = use_alpha ? dump[read + 3] : 0xFF; // alpha
			write += 4;
			read += bpp;
		}
	}

	// fill remaining pixels
	while (write < pixels_data_size) {
		data[write + 0] = 0x00; // red
		data[write + 1] = 0x00; // green
		data[write + 2] = 0x00; // blue
		data[write + 3] = 0x00; // alpha
		write += 4;
	}
	return data;
}

GLuint GameSprite::NormalImage::getGLTextureID() {
	// Only return a valid texture ID if the texture is actually loaded
	if (!isGLLoaded) {
		return 0;
	}
	// For sprite IDs below 100, use getFreeTextureID() to avoid collisions with system/UI texture IDs
	// This ensures we get a unique texture ID that won't conflict with other textures
	if (id < 100) {
		// gl_tid should be allocated and set during createGLTexture
		return gl_tid != 0 ? gl_tid : 0;
	}
	return id;
}

GLuint GameSprite::NormalImage::getHardwareID() {
	if (!isGLLoaded) {
		// For sprite IDs below 100, allocate gl_tid before creating texture
		if (id < 100 && gl_tid == 0) {
			gl_tid = g_gui.gfx.getFreeTextureID();
		}
		createGLTexture(0);
		// If texture creation failed (isGLLoaded is still false), reset gl_tid
		// and return 0 to indicate the texture doesn't exist
		if (!isGLLoaded) {
			if (id < 100) {
				gl_tid = 0;
			}
			return 0;
		}
	}
	visit();
	return getGLTextureID();
}

void GameSprite::NormalImage::createGLTexture(GLuint ignored) {
	// Get the texture ID to use (gl_tid should already be allocated for sprite IDs < 100)
	GLuint texture_id;
	if (id < 100) {
		// gl_tid should be set by getHardwareID() before this is called
		// But allocate it here as a safety measure
		if (gl_tid == 0) {
			gl_tid = g_gui.gfx.getFreeTextureID();
		}
		texture_id = gl_tid;
	} else {
		texture_id = id;
	}
	Image::createGLTexture(texture_id);
}

void GameSprite::NormalImage::unloadGLTexture(GLuint ignored) {
	GLuint texture_id = getGLTextureID();
	if (texture_id != 0) {
		Image::unloadGLTexture(texture_id);
	}
	// Reset gl_tid when unloading so it can be reallocated if needed
	if (id < 100) {
		gl_tid = 0;
	}
}

GameSprite::TemplateImage::TemplateImage(GameSprite* parent, int v, const Outfit& outfit) :
	gl_tid(0),
	parent(parent),
	sprite_index(v),
	lookHead(outfit.lookHead),
	lookBody(outfit.lookBody),
	lookLegs(outfit.lookLegs),
	lookFeet(outfit.lookFeet) {
	////
}

GameSprite::TemplateImage::~TemplateImage() {
	////
}

void GameSprite::TemplateImage::colorizePixel(uint8_t color, uint8_t& red, uint8_t& green, uint8_t& blue) {
	// Thanks! Khaos, or was it mips? Hmmm... =)
	uint8_t ro = (TemplateOutfitLookupTable[color] & 0xFF0000) >> 16; // rgb outfit
	uint8_t go = (TemplateOutfitLookupTable[color] & 0xFF00) >> 8;
	uint8_t bo = (TemplateOutfitLookupTable[color] & 0xFF);
	red = (uint8_t)(red * (ro / 255.f));
	green = (uint8_t)(green * (go / 255.f));
	blue = (uint8_t)(blue * (bo / 255.f));
}

uint8_t* GameSprite::TemplateImage::getRGBData() {
	const size_t template_index = static_cast<size_t>(sprite_index) + static_cast<size_t>(parent->height) * parent->width;
	if (sprite_index < 0 || template_index >= parent->spriteList.size()) {
		return nullptr;
	}
	uint8_t* rgbdata = parent->spriteList[sprite_index]->getRGBData();
	uint8_t* template_rgbdata = parent->spriteList[template_index]->getRGBData();

	if (!rgbdata) {
		delete[] template_rgbdata;
		return nullptr;
	}
	if (!template_rgbdata) {
		delete[] rgbdata;
		return nullptr;
	}

	if (lookHead >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookHead = 0;
	}
	if (lookBody >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookBody = 0;
	}
	if (lookLegs >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookLegs = 0;
	}
	if (lookFeet >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookFeet = 0;
	}

	for (int y = 0; y < SPRITE_PIXELS; ++y) {
		for (int x = 0; x < SPRITE_PIXELS; ++x) {
			uint8_t& red = rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 0];
			uint8_t& green = rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 1];
			uint8_t& blue = rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 2];

			uint8_t& tred = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 0];
			uint8_t& tgreen = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 1];
			uint8_t& tblue = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 2];

			if (tred && tgreen && !tblue) { // yellow => head
				colorizePixel(lookHead, red, green, blue);
			} else if (tred && !tgreen && !tblue) { // red => body
				colorizePixel(lookBody, red, green, blue);
			} else if (!tred && tgreen && !tblue) { // green => legs
				colorizePixel(lookLegs, red, green, blue);
			} else if (!tred && !tgreen && tblue) { // blue => feet
				colorizePixel(lookFeet, red, green, blue);
			}
		}
	}
	delete[] template_rgbdata;
	return rgbdata;
}

uint8_t* GameSprite::TemplateImage::getRGBAData() {
	const size_t template_index = static_cast<size_t>(sprite_index) + static_cast<size_t>(parent->height) * parent->width;
	if (sprite_index < 0 || template_index >= parent->spriteList.size()) {
		return nullptr;
	}
	uint8_t* rgbadata = parent->spriteList[sprite_index]->getRGBAData();
	uint8_t* template_rgbdata = parent->spriteList[template_index]->getRGBData();

	if (!rgbadata) {
		delete[] template_rgbdata;
		return nullptr;
	}
	if (!template_rgbdata) {
		delete[] rgbadata;
		return nullptr;
	}

	if (lookHead >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookHead = 0;
	}
	if (lookBody >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookBody = 0;
	}
	if (lookLegs >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookLegs = 0;
	}
	if (lookFeet >= (sizeof(TemplateOutfitLookupTable) / sizeof(TemplateOutfitLookupTable[0]))) {
		lookFeet = 0;
	}

	for (int y = 0; y < SPRITE_PIXELS; ++y) {
		for (int x = 0; x < SPRITE_PIXELS; ++x) {
			uint8_t& red = rgbadata[y * SPRITE_PIXELS * 4 + x * 4 + 0];
			uint8_t& green = rgbadata[y * SPRITE_PIXELS * 4 + x * 4 + 1];
			uint8_t& blue = rgbadata[y * SPRITE_PIXELS * 4 + x * 4 + 2];

			uint8_t& tred = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 0];
			uint8_t& tgreen = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 1];
			uint8_t& tblue = template_rgbdata[y * SPRITE_PIXELS * 3 + x * 3 + 2];

			if (tred && tgreen && !tblue) { // yellow => head
				colorizePixel(lookHead, red, green, blue);
			} else if (tred && !tgreen && !tblue) { // red => body
				colorizePixel(lookBody, red, green, blue);
			} else if (!tred && tgreen && !tblue) { // green => legs
				colorizePixel(lookLegs, red, green, blue);
			} else if (!tred && !tgreen && tblue) { // blue => feet
				colorizePixel(lookFeet, red, green, blue);
			}
		}
	}
	delete[] template_rgbdata;
	return rgbadata;
}

GLuint GameSprite::TemplateImage::getHardwareID() {
	if (!isGLLoaded) {
		if (gl_tid == 0) {
			gl_tid = g_gui.gfx.getFreeTextureID();
		}
		createGLTexture(gl_tid);
		if (!isGLLoaded) {
			return 0;
		}
	}
	visit();
	return gl_tid;
}

void GameSprite::TemplateImage::createGLTexture(GLuint unused) {
	Image::createGLTexture(gl_tid);
}

void GameSprite::TemplateImage::unloadGLTexture(GLuint unused) {
	Image::unloadGLTexture(gl_tid);
}

// ============================================================================
// Animator

Animator::Animator(int frame_count, int start_frame, int loop_count, bool async) :
	frame_count(frame_count),
	start_frame(start_frame),
	loop_count(loop_count),
	async(async),
	current_frame(0),
	current_loop(0),
	current_duration(0),
	total_duration(0),
	direction(ANIMATION_FORWARD),
	last_time(0),
	is_complete(false) {
	ASSERT(start_frame >= -1 && start_frame < frame_count);

	for (int i = 0; i < frame_count; i++) {
		durations.push_back(newd FrameDuration(ITEM_FRAME_DURATION, ITEM_FRAME_DURATION));
	}

	reset();
}

Animator::~Animator() {
	for (int i = 0; i < frame_count; i++) {
		delete durations[i];
	}
	durations.clear();
}

int Animator::getStartFrame() const {
	if (start_frame > -1) {
		return start_frame;
	}
	return uniform_random(0, frame_count - 1);
}

FrameDuration* Animator::getFrameDuration(int frame) {
	ASSERT(frame >= 0 && frame < frame_count);
	return durations[frame];
}

int Animator::getFrame() {
	long time = g_gui.gfx.getElapsedTime();
	if (time != last_time && !is_complete) {
		long elapsed = time - last_time;
		if (elapsed >= current_duration) {
			int frame = 0;
			if (loop_count < 0) {
				frame = getPingPongFrame();
			} else {
				frame = getLoopFrame();
			}

			if (current_frame != frame) {
				int duration = getDuration(frame) - (elapsed - current_duration);
				if (duration < 0 && !async) {
					calculateSynchronous();
				} else {
					current_frame = frame;
					current_duration = std::max<int>(0, duration);
				}
			} else {
				is_complete = true;
			}
		} else {
			current_duration -= elapsed;
		}

		last_time = time;
	}
	return current_frame;
}

void Animator::setFrame(int frame) {
	ASSERT(frame == -1 || frame == 255 || frame == 254 || (frame >= 0 && frame < frame_count));

	if (current_frame == frame) {
		return;
	}

	if (async) {
		if (frame == 255) { // Async mode
			current_frame = 0;
		} else if (frame == 254) { // Random mode
			current_frame = uniform_random(0, frame_count - 1);
		} else if (frame >= 0 && frame < frame_count) {
			current_frame = frame;
		} else {
			current_frame = getStartFrame();
		}

		is_complete = false;
		last_time = g_gui.gfx.getElapsedTime();
		current_duration = getDuration(current_frame);
		current_loop = 0;
	} else {
		calculateSynchronous();
	}
}

void Animator::reset() {
	total_duration = 0;
	for (int i = 0; i < frame_count; i++) {
		total_duration += durations[i]->max;
	}

	is_complete = false;
	direction = ANIMATION_FORWARD;
	current_loop = 0;
	async = false;
	setFrame(-1);
}

int Animator::getDuration(int frame) const {
	ASSERT(frame >= 0 && frame < frame_count);
	return durations[frame]->getDuration();
}

int Animator::getPingPongFrame() {
	int count = direction == ANIMATION_FORWARD ? 1 : -1;
	int next_frame = current_frame + count;
	if (next_frame < 0 || next_frame >= frame_count) {
		direction = direction == ANIMATION_FORWARD ? ANIMATION_BACKWARD : ANIMATION_FORWARD;
		count *= -1;
	}
	return current_frame + count;
}

int Animator::getLoopFrame() {
	int next_phase = current_frame + 1;
	if (next_phase < frame_count) {
		return next_phase;
	}

	if (loop_count == 0) {
		return 0;
	}

	if (current_loop < (loop_count - 1)) {
		current_loop++;
		return 0;
	}
	return current_frame;
}

void Animator::calculateSynchronous() {
	long time = g_gui.gfx.getElapsedTime();
	if (time > 0 && total_duration > 0) {
		long elapsed = time % total_duration;
		int total_time = 0;
		for (int i = 0; i < frame_count; i++) {
			int duration = getDuration(i);
			if (elapsed >= total_time && elapsed < total_time + duration) {
				current_frame = i;
				current_duration = duration - (elapsed - total_time);
				break;
			}
			total_time += duration;
		}
		last_time = time;
	}
}

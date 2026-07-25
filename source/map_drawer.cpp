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

#include <cmath>

#ifdef __APPLE__
	#include <GLUT/glut.h>
#else
	#include <GL/glut.h>
#endif

#include "editor.h"
#include "gui.h"
#include "live_client.h"
#include "live_socket.h"
#include "map_comment.h"
#include "sprites.h"
#include "map_drawer.h"
#include "map_display.h"
#include "copybuffer.h"
#include "graphics.h"

#include "doodad_brush.h"
#include "creature_brush.h"
#include "house_exit_brush.h"
#include "house_brush.h"
#include "spawn_brush.h"
#include "wall_brush.h"
#include "carpet_brush.h"
#include "raw_brush.h"
#include "table_brush.h"
#include "light_drawer.h"
#include "complexitem.h"

#include <vector>

namespace {

constexpr uint32_t TOOLTIP_ICON_TEXT = 1954; // paper
constexpr uint32_t TOOLTIP_ICON_DESTINATION = 1387; // portal
constexpr uint32_t TOOLTIP_ICON_ACTION = 1945; // lever (aid)
constexpr uint32_t TOOLTIP_ICON_UNIQUE = 1946; // lever2 (uid)
constexpr float TOOLTIP_ICON_SCALE = 0.65f;
constexpr float TOOLTIP_ICON_CELL = 18.0f;
constexpr float TOOLTIP_ICON_OFFSET_X = -4.0f;
constexpr float TOOLTIP_ICON_OFFSET_Y = -4.0f;
constexpr float TOOLTIP_ICON_BG_PAD = 2.0f;
constexpr float TOOLTIP_ICON_OUTLINE = 1.0f;

bool isRampWarningItemId(uint32_t id) {
	switch (id) {
		case 1388:
		case 1390:
		case 1392:
		case 1394:
		case 1398:
		case 1400:
		case 1402:
		case 1404:
		case 1553:
		case 1555:
		case 1557:
		case 1559:
			return true;
		default:
			return false;
	}
}

bool isInvalidItemSetWarningItemId(uint32_t id) {
	switch (id) {
		case 4542:
		case 4543:
		case 4544:
		case 4545:
		case 4608:
			return true;
		default:
			return false;
	}
}

bool isNonWalkableItem(const Item* item) {
	if (!item) {
		return false;
	}
	// Walls and anything that blocks movement/pathfinding count as non-walkable.
	// Note: we intentionally do NOT skip borders here. Decorative (walkable)
	// borders carry no blocking flags so they are excluded naturally, while
	// blocking borders (mountain/rock edges, stone, lava) must still be caught.
	if (item->isWall()) {
		return true;
	}
	return item->hasProperty(BLOCKSOLID) || item->hasProperty(BLOCKPATHFIND);
}

int getItemStackSize(const Tile* tile) {
	int size = 0;
	if (tile->hasGround()) {
		++size;
	}
	size += static_cast<int>(tile->items.size());
	return size;
}

bool tileHasInvalidItemSet(const Tile* tile) {
	if (!tile) {
		return false;
	}

	uint8_t found = 0;
	const int stackSize = getItemStackSize(tile);
	for (int i = 0; i < stackSize; ++i) {
		Item* item = tile->getItemAt(i);
		if (!item || !isInvalidItemSetWarningItemId(item->getID())) {
			continue;
		}

		switch (item->getID()) {
			case 4542:
				found |= 1;
				break;
			case 4543:
				found |= 2;
				break;
			case 4544:
				found |= 4;
				break;
			case 4545:
				found |= 8;
				break;
			case 4608:
				found |= 16;
				break;
		}
	}

	return found == 31;
}

bool tileHasInvalidRamp(const Tile* tile, const BaseMap& map) {
	if (!tile) {
		return false;
	}

	const int stackSize = getItemStackSize(tile);
	for (int i = 0; i < stackSize; ++i) {
		Item* ramp = tile->getItemAt(i);
		if (!ramp || !isRampWarningItemId(ramp->getID())) {
			continue;
		}

		if (tile->hasGround() && tile->ground != ramp && isNonWalkableItem(tile->ground)) {
			return true;
		}

		for (int j = 0; j < stackSize; ++j) {
			if (j == i) {
				continue;
			}

			Item* other = tile->getItemAt(j);
			if (other == tile->ground) {
				continue;
			}

			if (isNonWalkableItem(other)) {
				return true;
			}
		}

		if (!tile->hasGround()) {
			const Position pos = tile->getPosition();
			if (pos.z == 0) {
				return true;
			}

			Position belowPos = pos;
			--belowPos.z;
			const Tile* belowTile = map.getTile(belowPos);
			if (!belowTile || !belowTile->hasGround() || belowTile->ground->isBlocking()) {
				return true;
			}

			for (const Item* belowItem : belowTile->items) {
				if (isNonWalkableItem(belowItem)) {
					return true;
				}
			}
		}
	}

	return false;
}

// Only "real" doors (normal/locked/quest/magic) are walkable passages that must
// stay reachable, so a blocking item beneath one is a genuine mistake. Archways,
// windows and hatch windows share the OTB "door" item group but are decorative
// wall pieces -- they must not raise the door-on-blocking warning.
bool isRealDoorItem(const Item* item) {
	if (!item) {
		return false;
	}
	const Door* door = item->asDoor();
	return door && door->isRealDoor();
}

bool tileHasDoorOnBlockingItem(const Tile* tile) {
	if (!tile) {
		return false;
	}

	const int stackSize = getItemStackSize(tile);
	for (int i = 0; i < stackSize; ++i) {
		Item* door = tile->getItemAt(i);
		if (!door || !isRealDoorItem(door)) {
			continue;
		}

		for (int j = 0; j < i; ++j) {
			Item* below = tile->getItemAt(j);
			if (below && below->isBlocking()) {
				return true;
			}
		}
	}

	return false;
}

void getTooltipIconDrawPosition(uint32_t spriteId, float slotCenterX, float slotCenterY, float& anchorX, float& anchorY) {
	GameSprite* spr = g_items[spriteId].sprite;
	if (spr == nullptr) {
		anchorX = slotCenterX;
		anchorY = slotCenterY;
		return;
	}

	anchorX = slotCenterX + spr->getDrawOffset().first + float(spr->width - 1) * TileSize / 2.0f - TileSize / 2.0f;
	anchorY = slotCenterY + spr->getDrawOffset().second + float(spr->height - 1) * TileSize / 2.0f - TileSize / 2.0f;
}

void PushScreenSpaceGL(int screenWidth, int screenHeight) {
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, screenWidth, screenHeight, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
}

void PopScreenSpaceGL() {
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}

struct LiveParticipantPanelLayout {
	float startX;
	float startY;
	float boxWidth;
	float boxHeight;
	float padding;
	float headerHeight;
	float lineHeight;
	float textOffset;
	float iconSpace; // room reserved at the right of each row for the go-to icon
};

LiveParticipantPanelLayout computeLiveParticipantPanelLayout(int screenWidth, const std::vector<LiveParticipant>& participants) {
	const float swatchSize = 10.0f;
	LiveParticipantPanelLayout layout;
	layout.padding = 8.0f;
	layout.headerHeight = 22.0f;
	layout.lineHeight = 22.0f;
	layout.textOffset = swatchSize + 8.0f;
	layout.iconSpace = 24.0f;
	layout.boxWidth = 170.0f;

	for (const LiveParticipant& participant : participants) {
		// " (you)" suffix can add up to 6 characters to the longest name
		const float nameWidth = float(participant.name.length() + 6) * 7.0f + layout.textOffset + layout.iconSpace + layout.padding * 2.0f;
		layout.boxWidth = std::max(layout.boxWidth, nameWidth);
	}

	layout.boxHeight = layout.headerHeight + layout.lineHeight * participants.size() + layout.padding;
	layout.startX = screenWidth - layout.boxWidth - layout.padding;
	layout.startY = layout.padding;
	return layout;
}

bool tryGetLiveAuthorColor(const Editor& editor, const std::string& author, uint8_t& r, uint8_t& g, uint8_t& b) {
	if (!editor.IsLive()) {
		return false;
	}

	const wxString authorName = wxstr(author);
	for (const LiveParticipant& participant : editor.GetLive().getParticipantList()) {
		if (participant.name == authorName) {
			r = participant.color.Red();
			g = participant.color.Green();
			b = participant.color.Blue();
			return true;
		}
	}

	if (author == editor.getMapCommentAuthor()) {
		const wxColor color = editor.getLiveCursorColor();
		r = color.Red();
		g = color.Green();
		b = color.Blue();
		return true;
	}

	return false;
}

void DrawScreenTextLabel(float right, float top, const wxString& text) {
	if (text.empty()) {
		return;
	}

	float textWidth = 0.0f;
	for (size_t i = 0; i < text.length(); ++i) {
		textWidth += glutBitmapWidth(GLUT_BITMAP_HELVETICA_10, static_cast<unsigned char>(text[i]));
	}

	const float textHeight = 10.0f;
	const float pad = 2.0f;
	const float labelRight = right - pad;
	const float labelTop = top + pad;
	const float labelLeft = labelRight - textWidth - pad * 2.0f;
	const float labelBottom = labelTop + textHeight + pad;

	glColor4ub(0, 0, 0, 200);
	glBegin(GL_QUADS);
	glVertex2f(labelLeft, labelTop);
	glVertex2f(labelRight, labelTop);
	glVertex2f(labelRight, labelBottom);
	glVertex2f(labelLeft, labelBottom);
	glEnd();

	glColor4ub(255, 255, 255, 255);
	glRasterPos2f(labelLeft + pad, labelTop + textHeight);
	for (size_t i = 0; i < text.length(); ++i) {
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, static_cast<unsigned char>(text[i]));
	}
}

} // namespace
DrawingOptions::DrawingOptions() {
	SetDefault();
}

void DrawingOptions::SetDefault() {
	transparent_floors = false;
	transparent_items = false;
	transparent_void_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_light_str = true;
	show_tech_items = true;
	ingame = false;
	dragging = false;

	show_grid = 0;
	show_chunk_boundaries = false;
	show_tile_coordinates = false;
	show_mouse_crosshair = false;
	mouse_crosshair_color = wxColor(255, 255, 255);
	viewport_background_color = wxColor(0, 0, 0);
	show_all_floors = true;
	show_all_floors_underground = true;
	show_creatures = true;
	show_spawns = true;
	show_houses = true;
	show_shade = true;
	show_special_tiles = true;
	show_items = true;

	highlight_items = false;
	highlight_locked_doors = true;
	show_blocking = false;
	show_tooltips = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_preview = true;
	show_hooks = false;
	show_fishable_water = false;
	show_borders = true;
	show_walls = true;
	show_ground = true;
	hide_items_when_zoomed = true;

	show_towns = false;
	always_show_zones = false;
	extended_house_shader = false;
	show_invisible_items = true;
	show_creature_spawn_time = false;
	show_hud = true;
	show_autoborder_indicator = true;
}

void DrawingOptions::SetIngame() {
	transparent_floors = false;
	transparent_items = false;
	transparent_void_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_light_str = false;
	show_tech_items = false;
	ingame = true;
	dragging = false;

	show_grid = 0;
	show_chunk_boundaries = false;
	show_tile_coordinates = false;
	show_mouse_crosshair = false;
	mouse_crosshair_color = wxColor(255, 255, 255);
	viewport_background_color = wxColor(0, 0, 0);
	show_all_floors = true;
	show_all_floors_underground = true;
	show_creatures = true;
	show_spawns = false;
	show_houses = false;
	show_shade = false;
	show_special_tiles = false;
	show_items = true;

	highlight_items = false;
	highlight_locked_doors = false;
	show_blocking = false;
	show_tooltips = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_preview = false;
	show_hooks = false;
	show_fishable_water = false;
	show_borders = true;
	show_walls = true;
	show_ground = true;
	hide_items_when_zoomed = false;

	show_towns = false;
	always_show_zones = false;
	extended_house_shader = false;
	show_invisible_items = false;
	show_creature_spawn_time = false;
	show_hud = false;
	show_autoborder_indicator = false;
}

bool DrawingOptions::isDrawLight() const noexcept {
	return show_lights;
}

MapDrawer::MapDrawer(MapCanvas* canvas) : canvas(canvas), editor(canvas->editor) {
	light_drawer = std::make_shared<LightDrawer>();
	tooltips.reserve(32);
	tooltip_markers.reserve(64);
	ramp_warning_markers.reserve(32);
}

MapDrawer::~MapDrawer() {
	Release();
}

void MapDrawer::SetupVars() {
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);
	canvas->GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	dragging = canvas->dragging;
	dragging_draw = canvas->dragging_draw;
	dragging_draw_line = canvas->dragging_draw_line;

	zoom = (float)canvas->GetZoom();
	tile_size = int(TileSize / zoom); // after zoom
	floor = canvas->GetFloor();

	if (options.show_all_floors) {
		if (floor <= GROUND_LAYER) {
			start_z = GROUND_LAYER;
		} else if (options.show_all_floors_underground) {
			start_z = MAP_MAX_LAYER;
		} else {
			start_z = std::min(MAP_MAX_LAYER, floor + 2);
		}
	} else {
		start_z = floor;
	}

	end_z = floor;
	superend_z = (floor > GROUND_LAYER ? 8 : 0);

	start_x = view_scroll_x / TileSize;
	start_y = view_scroll_y / TileSize;

	if (floor > GROUND_LAYER) {
		start_x -= 2;
		start_y -= 2;
	}

	end_x = start_x + screensize_x / tile_size + 2;
	end_y = start_y + screensize_y / tile_size + 2;
}

void MapDrawer::SetupGL() {
	glViewport(0, 0, screensize_x, screensize_y);

	// Enable 2D mode
	int vPort[4];

	glGetIntegerv(GL_VIEWPORT, vPort);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, vPort[2] * zoom, vPort[3] * zoom, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(0.375f, 0.375f, 0.0f);
}

void MapDrawer::Release() {
	tooltips.clear();
	tooltip_markers.clear();
	ramp_warning_markers.clear();

	if (light_drawer) {
		light_drawer->clear();
	}

	// Disable 2D mode
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

void MapDrawer::Draw() {
	DrawBackground();
	DrawMap();
	DrawTooltipIcons();
	if (options.isDrawLight()) {
		DrawLight();
	}
	DrawDraggingShadow();
	DrawHigherFloors();
	DrawRampWarningIcons();
	if (options.dragging) {
		DrawSelectionBox();
	}
	DrawBrush();
	if (options.show_grid) {
		DrawGrid();
	}
	if (options.show_chunk_boundaries) {
		DrawChunkBoundaries();
	}
	if (options.show_tile_coordinates) {
		DrawTileCoordinates();
	}
	if (options.show_mouse_crosshair) {
		DrawMouseCrosshair();
	}
	if (options.show_ingame_box) {
		DrawIngameBox();
	}
	DrawMapCommentTooltips();
	DrawHoveredItemTooltips();
	if (options.show_tooltips || !tooltips.empty()) {
		DrawTooltips();
	}
	DrawLiveCursors();
	DrawLivePings();
	DrawLiveParticipants();
	DrawMapComments();

	if (options.show_hud) {
		DrawHUD();
	}

	if (options.show_autoborder_indicator) {
		DrawAutoborderIndicator();
	}

	if (editor.live_client) {
		editor.live_client->sendNodeRequests();
	}
}

void MapDrawer::DrawBackground() {
	// Solid background colour
	const wxColor& background = options.viewport_background_color;
	glClearColor(
		background.Red() / 255.0f,
		background.Green() / 255.0f,
		background.Blue() / 255.0f,
		0.0f
	);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	// glAlphaFunc(GL_GEQUAL, 0.9f);
	// glEnable(GL_ALPHA_TEST);
}

inline int getFloorAdjustment(int floor) {
	if (floor > GROUND_LAYER) { // Underground
		return 0; // No adjustment
	} else {
		return TileSize * (GROUND_LAYER - floor);
	}
}

void MapDrawer::DrawMap() {
	int center_x = start_x + int(screensize_x * zoom / 64);
	int center_y = start_y + int(screensize_y * zoom / 64);
	int offset_y = 2;
	int box_start_map_x = center_x - view_scroll_x;
	int box_start_map_y = center_y - view_scroll_x + offset_y;
	int box_end_map_x = center_x + ClientMapWidth;
	int box_end_map_y = center_y + ClientMapHeight + offset_y;


	Brush* brush = g_gui.GetCurrentBrush();

	// The current house we're drawing
	current_house_id = 0;
	if (brush) {
		if (brush->isHouse()) {
			current_house_id = brush->asHouse()->getHouseID();
		} else if (brush->isHouseExit()) {
			current_house_id = brush->asHouseExit()->getHouseID();
		}
	}

	bool only_colors = options.show_as_minimap || options.show_only_colors;

	// Enable texture mode
	if (!only_colors) {
		glEnable(GL_TEXTURE_2D);
	}

	LiveClient* live_client = editor.live_client;
	if (live_client && live_client->consumeViewportRefresh()) {
		live_client->invalidateViewport(start_x, start_y, end_x, end_y);
	}

	for (int map_z = start_z; map_z >= superend_z; map_z--) {
		if (map_z == end_z && start_z != end_z && options.show_shade) {
			// Draw shade
			if (!only_colors) {
				glDisable(GL_TEXTURE_2D);
			}

			glColor4ub(0, 0, 0, 128);
			glBegin(GL_QUADS);
			glVertex2f(0, int(screensize_y * zoom));
			glVertex2f(int(screensize_x * zoom), int(screensize_y * zoom));
			glVertex2f(int(screensize_x * zoom), 0);
			glVertex2f(0, 0);
			glEnd();

			if (!only_colors) {
				glEnable(GL_TEXTURE_2D);
			}
		}

		if (map_z >= end_z) {
			int nd_start_x = start_x & ~3;
			int nd_start_y = start_y & ~3;
			int nd_end_x = (end_x & ~3) + 4;
			int nd_end_y = (end_y & ~3) + 4;

			const bool underground = map_z > GROUND_LAYER;
			const LiveSessionBounds* session_bounds = nullptr;
			if (editor.IsLive() && editor.GetLive().getSessionBounds().enabled) {
				session_bounds = &editor.GetLive().getSessionBounds();
			}

			for (int nd_map_x = nd_start_x; nd_map_x <= nd_end_x; nd_map_x += 4) {
				for (int nd_map_y = nd_start_y; nd_map_y <= nd_end_y; nd_map_y += 4) {
					if (session_bounds && !session_bounds->intersectsLeaf(nd_map_x, nd_map_y)) {
						for (int map_x = 0; map_x < 4; ++map_x) {
							for (int map_y = 0; map_y < 4; ++map_y) {
								drawSessionBlackTile(nd_map_x + map_x, nd_map_y + map_y, map_z, only_colors);
							}
						}
						continue;
					}

					QTreeNode* nd = editor.map.getLeaf(nd_map_x, nd_map_y);
					if (live_client) {
						if (!nd || !nd->isVisible(underground)) {
							if (!nd || !nd->isRequested(underground)) {
								if (nd) {
									nd->setRequested(underground, true);
								}
								live_client->queryNode(nd_map_x, nd_map_y, underground);
							}
							continue;
						}
					} else if (!nd) {
						continue;
					}

					for (int map_x = 0; map_x < 4; ++map_x) {
						for (int map_y = 0; map_y < 4; ++map_y) {
							const int tile_x = nd_map_x + map_x;
							const int tile_y = nd_map_y + map_y;
							if (session_bounds && !session_bounds->contains(tile_x, tile_y)) {
								drawSessionBlackTile(tile_x, tile_y, map_z, only_colors);
								continue;
							}
							TileLocation* location = nd->getTile(map_x, map_y, map_z);
							DrawTile(location);
							// draw light, but only if not zoomed too far
							if (location && options.isDrawLight() && zoom <= 10.0) {
								AddLight(location);
							}
						}
					}
				}
			}
		}

		if (only_colors) {
			glEnable(GL_TEXTURE_2D);
		}

		// Draws the doodad preview or the paste preview (or import preview)
		if (g_gui.secondary_map != nullptr && !options.ingame) {
			Position normalPos;
			Position to(mouse_map_x, mouse_map_y, floor);

			if (canvas->isPasting()) {
				normalPos = editor.copybuffer.getPosition();
			} else if (brush && brush->isDoodad()) {
				normalPos = Position(0x8000, 0x8000, 0x8);
			}

			for (int map_x = start_x; map_x <= end_x; map_x++) {
				for (int map_y = start_y; map_y <= end_y; map_y++) {
					Position final(map_x, map_y, map_z);
					Position pos = normalPos + final - to;
					// Position pos = topos + copypos - Position(map_x, map_y, map_z);
					if (pos.z >= MAP_LAYERS || pos.z < 0) {
						continue;
					}

					Tile* tile = g_gui.secondary_map->getTile(pos);
					if (tile) {
						// Compensate for underground/overground
						int offset;
						if (map_z <= GROUND_LAYER) {
							offset = (GROUND_LAYER - map_z) * TileSize;
						} else {
							offset = TileSize * (floor - map_z);
						}

						int draw_x = ((map_x * TileSize) - view_scroll_x) - offset;
						int draw_y = ((map_y * TileSize) - view_scroll_y) - offset;

						// Draw ground
						uint8_t r = 160, g = 160, b = 160;
						if (tile->ground && options.show_ground) {
							if (tile->isBlocking() && options.show_blocking) {
								g = g / 3 * 2;
								b = b / 3 * 2;
							}
							if (tile->isHouseTile() && options.show_houses) {
								if ((int)tile->getHouseID() == current_house_id) {
									r /= 2;
								} else {
									r /= 2;
									g /= 2;
								}
							} else if (options.show_special_tiles && tile->isPZ()) {
								r /= 2;
								b /= 2;
							}
							if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_PVPZONE) {
								r = r / 3 * 2;
								b = r / 3 * 2;
							}
							if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
								b /= 2;
							}
							if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOPVP) {
								g /= 2;
							}
						if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_REFRESH) {
							r = 255;
							g = 165;
							b = 0;
						}
							BlitItem(draw_x, draw_y, tile, tile->ground, true, r, g, b, 160);
						}

						// Draw items on the tile
						if (zoom <= 10.0 || !options.hide_items_when_zoomed) {
							ItemVector::iterator it;
							for (it = tile->items.begin(); it != tile->items.end(); it++) {
								if ((*it)->isBorder() && !options.show_borders) {
									continue;
								}
								if ((*it)->isBorder()) {
									BlitItem(draw_x, draw_y, tile, *it, true, 160, r, g, b);
								} else {
									BlitItem(draw_x, draw_y, tile, *it, true, 160, 160, 160, 160);
								}
							}
							if (tile->creature && options.show_creatures) {
								BlitCreature(draw_x, draw_y, tile->creature);
							}
						}
					}
				}
			}
		}

		--start_x;
		--start_y;
		++end_x;
		++end_y;
	}

	if (!only_colors) {
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawIngameBox() {
	int center_x = start_x + int(screensize_x * zoom / 64);
	int center_y = start_y + int(screensize_y * zoom / 64);

	int offset_y = 2;
	int box_start_map_x = center_x;
	int box_start_map_y = center_y + offset_y;
	int box_end_map_x = center_x + ClientMapWidth;
	int box_end_map_y = center_y + ClientMapHeight + offset_y;

	int box_start_x = box_start_map_x * TileSize - view_scroll_x;
	int box_start_y = box_start_map_y * TileSize - view_scroll_y;
	int box_end_x = box_end_map_x * TileSize - view_scroll_x;
	int box_end_y = box_end_map_y * TileSize - view_scroll_y;

	static wxColor side_color(0, 0, 0, 200);

	glDisable(GL_TEXTURE_2D);

	// left side
	if (box_start_map_x >= start_x) {
		drawFilledRect(0, 0, box_start_x, screensize_y * zoom, side_color);
	}

	// right side
	if (box_end_map_x < end_x) {
		drawFilledRect(box_end_x, 0, screensize_x * zoom, screensize_y * zoom, side_color);
	}

	// top side
	if (box_start_map_y >= start_y) {
		drawFilledRect(box_start_x, 0, box_end_x - box_start_x, box_start_y, side_color);
	}

	// bottom side
	if (box_end_map_y < end_y) {
		drawFilledRect(box_start_x, box_end_y, box_end_x - box_start_x, screensize_y * zoom, side_color);
	}

	// hidden tiles
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxRED);

	// visible tiles
	box_start_x += TileSize;
	box_start_y += TileSize;
	box_end_x -= 1 * TileSize;
	box_end_y -= 1 * TileSize;
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxGREEN);

	// player position
	box_start_x += (ClientMapWidth - 3) / 2 * TileSize;
	box_start_y += (ClientMapHeight - 3) / 2 * TileSize;
	box_end_x = box_start_x + TileSize;
	box_end_y = box_start_y + TileSize;
	drawRect(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, *wxGREEN);

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawGrid() {
	for (int y = start_y; y < end_y; ++y) {
		glColor4ub(255, 255, 255, 128);
		glBegin(GL_LINES);
		glVertex2f(start_x * TileSize - view_scroll_x, y * TileSize - view_scroll_y);
		glVertex2f(end_x * TileSize - view_scroll_x, y * TileSize - view_scroll_y);
		glEnd();
	}

	for (int x = start_x; x < end_x; ++x) {
		glColor4ub(255, 255, 255, 128);
		glBegin(GL_LINES);
		glVertex2f(x * TileSize - view_scroll_x, start_y * TileSize - view_scroll_y);
		glVertex2f(x * TileSize - view_scroll_x, end_y * TileSize - view_scroll_y);
		glEnd();
	}
}

void MapDrawer::DrawChunkBoundaries() {
	const int chunkSize = 256;
	if (chunkSize <= 0) {
		return;
	}

	auto alignToChunk = [chunkSize](int value) {
		int remainder = value % chunkSize;
		if (remainder < 0) {
			remainder += chunkSize;
		}
		return value - remainder;
	};

	const int verticalStart = alignToChunk(start_x);
	int verticalEnd = alignToChunk(end_x);
	if (verticalEnd < end_x) {
		verticalEnd += chunkSize;
	}

	const int horizontalStart = alignToChunk(start_y);
	int horizontalEnd = alignToChunk(end_y);
	if (horizontalEnd < end_y) {
		horizontalEnd += chunkSize;
	}

	const float top = start_y * TileSize - view_scroll_y;
	const float bottom = end_y * TileSize - view_scroll_y;
	const float left = start_x * TileSize - view_scroll_x;
	const float right = end_x * TileSize - view_scroll_x;

	glLineWidth(2.0f);

	for (int x = verticalStart; x <= verticalEnd; x += chunkSize) {
		const float px = x * TileSize - view_scroll_x;
		if (px < left - TileSize || px > right + TileSize) {
			continue;
		}

		if (x == 0) {
			glColor4ub(255, 128, 0, 220);
		} else {
			glColor4ub(255, 215, 0, 140);
		}

		glBegin(GL_LINES);
		glVertex2f(px, top);
		glVertex2f(px, bottom);
		glEnd();
	}

	for (int y = horizontalStart; y <= horizontalEnd; y += chunkSize) {
		const float py = y * TileSize - view_scroll_y;
		if (py < top - TileSize || py > bottom + TileSize) {
			continue;
		}

		if (y == 0) {
			glColor4ub(255, 128, 0, 220);
		} else {
			glColor4ub(255, 215, 0, 140);
		}

		glBegin(GL_LINES);
		glVertex2f(left, py);
		glVertex2f(right, py);
		glEnd();
	}

	glLineWidth(1.0f);

	// Draw a small marker where both zero boundaries intersect if visible
	if (verticalStart <= 0 && verticalEnd >= 0 && horizontalStart <= 0 && horizontalEnd >= 0) {
		const float markerX = -view_scroll_x;
		const float markerY = -view_scroll_y;
		const int markerSize = TileSize / 4;
		drawFilledRect(int(markerX) - markerSize / 2, int(markerY) - markerSize / 2, markerSize, markerSize, wxColour(255, 140, 0, 180));
	}
}

void MapDrawer::DrawMouseCrosshair() {
	const float viewportWidth = screensize_x * zoom;
	const float viewportHeight = screensize_y * zoom;

	const int floorAdjustment = getFloorAdjustment(floor);
	const float tileLeft = float(mouse_map_x * TileSize - view_scroll_x - floorAdjustment);
	const float tileTop = float(mouse_map_y * TileSize - view_scroll_y - floorAdjustment);
	const float tileRight = tileLeft + TileSize;
	const float tileBottom = tileTop + TileSize;

	if (tileRight < 0.0f || tileBottom < 0.0f || tileLeft > viewportWidth || tileTop > viewportHeight) {
		return;
	}

	const float tileCenterX = tileLeft + TileSize / 2.0f;
	const float tileCenterY = tileTop + TileSize / 2.0f;

	const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
	if (textureWasEnabled) {
		glDisable(GL_TEXTURE_2D);
	}

	glLineWidth(1.0f);
	glColor4ub(options.mouse_crosshair_color.Red(), options.mouse_crosshair_color.Green(), options.mouse_crosshair_color.Blue(), 180);

	glBegin(GL_LINES);
	glVertex2f(tileCenterX, 0.0f);
	glVertex2f(tileCenterX, viewportHeight);
	glVertex2f(0.0f, tileCenterY);
	glVertex2f(viewportWidth, tileCenterY);
	glEnd();

	glBegin(GL_LINE_LOOP);
	glVertex2f(tileLeft, tileTop);
	glVertex2f(tileRight, tileTop);
	glVertex2f(tileRight, tileBottom);
	glVertex2f(tileLeft, tileBottom);
	glEnd();

	if (textureWasEnabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawDraggingShadow() {
	glEnable(GL_TEXTURE_2D);

	// Draw dragging shadow
	if (!editor.selection.isBusy() && dragging && !options.ingame) {
		for (TileSet::iterator tit = editor.selection.begin(); tit != editor.selection.end(); tit++) {
			Tile* tile = *tit;
			Position pos = tile->getPosition();

			int move_x, move_y, move_z;
			move_x = canvas->drag_start_x - mouse_map_x;
			move_y = canvas->drag_start_y - mouse_map_y;
			move_z = canvas->drag_start_z - floor;

			pos.x -= move_x;
			pos.y -= move_y;
			pos.z -= move_z;

			if (pos.z < 0 || pos.z >= MAP_LAYERS) {
				continue;
			}

			// On screen and dragging?
			if (pos.x + 2 > start_x && pos.x < end_x && pos.y + 2 > start_y && pos.y < end_y && (move_x != 0 || move_y != 0 || move_z != 0)) {
				int offset;
				if (pos.z <= GROUND_LAYER) {
					offset = (GROUND_LAYER - pos.z) * TileSize;
				} else {
					offset = TileSize * (floor - pos.z);
				}

				int draw_x = ((pos.x * TileSize) - view_scroll_x) - offset;
				int draw_y = ((pos.y * TileSize) - view_scroll_y) - offset;

				// save performance when moving large chunks unzoomed
				ItemVector toRender = tile->getSelectedItems(zoom > 3.0);
				Tile* desttile = editor.map.getTile(pos);
				for (ItemVector::const_iterator iit = toRender.begin(); iit != toRender.end(); iit++) {
					if (desttile) {
						BlitItem(draw_x, draw_y, desttile, *iit, true, 160, 160, 160, 160);
					} else {
						BlitItem(draw_x, draw_y, pos, *iit, true, 160, 160, 160, 160);
					}
				}

				// save performance when moving large chunks unzoomed
				if (zoom <= 3.0) {
					if (tile->creature && tile->creature->isSelected() && options.show_creatures) {
						BlitCreature(draw_x, draw_y, tile->creature);
					}
					if (tile->spawn && tile->spawn->isSelected()) {
						BlitSpriteType(draw_x, draw_y, SPRITE_SPAWN, 160, 160, 160, 160);
					}
				}
			}
		}
	}

	glDisable(GL_TEXTURE_2D);
}

void MapDrawer::DrawHigherFloors() {
	glEnable(GL_TEXTURE_2D);

	// Draw "transparent higher floor"
	if (floor != 8 && floor != 0 && options.transparent_floors) {
		int map_z = floor - 1;
		for (int map_x = start_x; map_x <= end_x; map_x++) {
			for (int map_y = start_y; map_y <= end_y; map_y++) {
				Tile* tile = editor.map.getTile(map_x, map_y, map_z);
				if (tile) {
					int offset;
					if (map_z <= GROUND_LAYER) {
						offset = (GROUND_LAYER - map_z) * TileSize;
					} else {
						offset = TileSize * (floor - map_z);
					}

					int draw_x = ((map_x * TileSize) - view_scroll_x) - offset;
					int draw_y = ((map_y * TileSize) - view_scroll_y) - offset;

					// Position pos = tile->getPosition();

					if (tile->ground && options.show_ground) {
						if (tile->isPZ()) {
							BlitItem(draw_x, draw_y, tile, tile->ground, false, 128, 255, 128, 96);
						} else if (tile->isRefresh()) {
							BlitItem(draw_x, draw_y, tile, tile->ground, false, 128, 128, 255, 96);
						} else {
							BlitItem(draw_x, draw_y, tile, tile->ground, false, 255, 255, 255, 96);
						}
					}
					if (zoom <= 10.0 || !options.hide_items_when_zoomed) {
						ItemVector::iterator it;
						for (it = tile->items.begin(); it != tile->items.end(); it++) {
							BlitItem(draw_x, draw_y, tile, *it, false, 255, 255, 255, 96);
						}
					}
				}
			}
		}
	}

	glDisable(GL_TEXTURE_2D);
}

void MapDrawer::DrawSelectionBox() {
	if (options.ingame) {
		return;
	}

	// Draw bounding box

	int last_click_rx = canvas->last_click_abs_x - view_scroll_x;
	int last_click_ry = canvas->last_click_abs_y - view_scroll_y;
	double cursor_rx = canvas->cursor_x * zoom;
	double cursor_ry = canvas->cursor_y * zoom;

	double lines[4][4];

	lines[0][0] = last_click_rx;
	lines[0][1] = last_click_ry;
	lines[0][2] = cursor_rx;
	lines[0][3] = last_click_ry;

	lines[1][0] = cursor_rx;
	lines[1][1] = last_click_ry;
	lines[1][2] = cursor_rx;
	lines[1][3] = cursor_ry;

	lines[2][0] = cursor_rx;
	lines[2][1] = cursor_ry;
	lines[2][2] = last_click_rx;
	lines[2][3] = cursor_ry;

	lines[3][0] = last_click_rx;
	lines[3][1] = cursor_ry;
	lines[3][2] = last_click_rx;
	lines[3][3] = last_click_ry;

	glEnable(GL_LINE_STIPPLE);
	glLineStipple(1, 0xf0);
	glLineWidth(1.0);
	if (canvas->boundbox_deselection) {
		glColor4f(1.0f, 0.35f, 0.35f, 1.0f); // red box = rectangular deselect
	} else {
		glColor4f(1.0, 1.0, 1.0, 1.0);
	}
	glBegin(GL_LINES);
	for (int i = 0; i < 4; i++) {
		glVertex2f(lines[i][0], lines[i][1]);
		glVertex2f(lines[i][2], lines[i][3]);
	}
	glEnd();
	glDisable(GL_LINE_STIPPLE);
}

void MapDrawer::DrawLiveCursors() {
	if (!editor.IsLive()) {
		return;
	}

	LiveSocket& live = editor.GetLive();
	const std::vector<LiveCursor> cursorList = live.getCursorList();
	if (cursorList.empty()) {
		return;
	}

	const std::vector<LiveParticipant>& participants = live.getParticipantList();
	const uint32_t ownClientId = live.getOwnClientId();
	const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
	if (textureWasEnabled) {
		glDisable(GL_TEXTURE_2D);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct CursorLabel {
		float screenRight;
		float screenTop;
		wxString name;
	};
	std::vector<CursorLabel> cursorLabels;
	cursorLabels.reserve(cursorList.size());

	for (const LiveCursor& cursor : cursorList) {
		if (cursor.id == ownClientId) {
			continue;
		}
		if (!isLiveMarkerVisibleOnFloor(floor, cursor.pos.z)) {
			continue;
		}

		wxColor color = cursor.color;
		wxString userName;
		for (const LiveParticipant& participant : participants) {
			if (participant.id == cursor.id) {
				color = participant.color;
				userName = participant.name;
				break;
			}
		}

		const int offset = getFloorAdjustment(cursor.pos.z);
		const float tileLeft = float(cursor.pos.x * TileSize - view_scroll_x - offset);
		const float tileTop = float(cursor.pos.y * TileSize - view_scroll_y - offset);
		const float tileRight = tileLeft + TileSize;
		const float tileBottom = tileTop + TileSize;

		glColor4ub(color.Red(), color.Green(), color.Blue(), 150);
		glBegin(GL_QUADS);
		glVertex2f(tileLeft, tileTop);
		glVertex2f(tileRight, tileTop);
		glVertex2f(tileRight, tileBottom);
		glVertex2f(tileLeft, tileBottom);
		glEnd();

		glLineWidth(2.0f);
		glColor4ub(color.Red(), color.Green(), color.Blue(), 255);
		glBegin(GL_LINE_LOOP);
		glVertex2f(tileLeft, tileTop);
		glVertex2f(tileRight, tileTop);
		glVertex2f(tileRight, tileBottom);
		glVertex2f(tileLeft, tileBottom);
		glEnd();

		if (!userName.empty()) {
			cursorLabels.push_back({
				tileRight / zoom,
				tileTop / zoom,
				userName
			});
		}
	}

	if (!cursorLabels.empty()) {
		PushScreenSpaceGL(screensize_x, screensize_y);
		for (const CursorLabel& label : cursorLabels) {
			DrawScreenTextLabel(label.screenRight, label.screenTop, label.name);
		}
		PopScreenSpaceGL();
	}

	glLineWidth(1.0f);
	if (textureWasEnabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

namespace {
void drawPingCircle(float centerX, float centerY, float radius) {
	constexpr int segments = 32;
	glBegin(GL_LINE_LOOP);
	for (int i = 0; i < segments; ++i) {
		const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
		glVertex2f(centerX + radius * std::cos(angle), centerY + radius * std::sin(angle));
	}
	glEnd();
}

void drawPingDot(float centerX, float centerY, float radius) {
	constexpr int segments = 16;
	glBegin(GL_TRIANGLE_FAN);
	glVertex2f(centerX, centerY);
	for (int i = 0; i <= segments; ++i) {
		const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
		glVertex2f(centerX + radius * std::cos(angle), centerY + radius * std::sin(angle));
	}
	glEnd();
}
}

void MapDrawer::DrawLivePings() {
	if (!editor.IsLive()) {
		return;
	}

	LiveSocket& live = editor.GetLive();
	if (!live.hasActivePings()) {
		return;
	}

	const std::vector<LiveParticipant>& participants = live.getParticipantList();
	const auto now = std::chrono::steady_clock::now();
	const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
	if (textureWasEnabled) {
		glDisable(GL_TEXTURE_2D);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct PingLabel {
		float screenX;
		float screenY;
		wxString name;
		uint8_t alpha;
	};
	std::vector<PingLabel> pingLabels;

	for (const ActiveLivePing& active : live.getActivePings()) {
		const LivePing& ping = active.ping;
		if (!isLiveMarkerVisibleOnFloor(floor, ping.pos.z)) {
			continue;
		}

		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - active.startTime);
		if (elapsed.count() >= 4000) {
			continue;
		}

		wxColor color = ping.color;
		wxString userName;
		for (const LiveParticipant& participant : participants) {
			if (participant.id == ping.id) {
				color = participant.color;
				userName = participant.name;
				break;
			}
		}

		const int offset = getFloorAdjustment(ping.pos.z);
		const float tileLeft = float(ping.pos.x * TileSize - view_scroll_x - offset);
		const float tileTop = float(ping.pos.y * TileSize - view_scroll_y - offset);
		const float centerX = tileLeft + TileSize * 0.5f;
		const float centerY = tileTop + TileSize * 0.5f;

		for (int ring = 0; ring < 3; ++ring) {
			const int ringStartMs = ring * 400;
			const int ringElapsedMs = static_cast<int>(elapsed.count()) - ringStartMs;
			if (ringElapsedMs < 0 || ringElapsedMs > 1200) {
				continue;
			}

			const float progress = static_cast<float>(ringElapsedMs) / 1200.0f;
			const float radius = 8.0f + progress * 52.0f;
			const uint8_t alpha = static_cast<uint8_t>(220.0f * (1.0f - progress));
			glLineWidth(2.5f - ring * 0.5f);
			glColor4ub(color.Red(), color.Green(), color.Blue(), alpha);
			drawPingCircle(centerX, centerY, radius);
		}

		if (elapsed.count() < 2000) {
			const float dotRadius = 6.0f;
			const uint8_t dotAlpha = static_cast<uint8_t>(255.0f * (1.0f - elapsed.count() / 2000.0f));
			glColor4ub(color.Red(), color.Green(), color.Blue(), dotAlpha);
			drawPingDot(centerX, centerY, dotRadius);
		}

		if (!userName.empty() && elapsed.count() < 2500) {
			const uint8_t labelAlpha = static_cast<uint8_t>(255.0f * (1.0f - elapsed.count() / 2500.0f));
			pingLabels.push_back({
				(centerX + 10.0f) / zoom,
				(centerY - 8.0f) / zoom,
				userName,
				labelAlpha
			});
		}
	}

	if (!pingLabels.empty()) {
		PushScreenSpaceGL(screensize_x, screensize_y);
		for (const PingLabel& label : pingLabels) {
			glColor4ub(255, 255, 255, label.alpha);
			DrawScreenTextLabel(label.screenX, label.screenY, label.name);
		}
		PopScreenSpaceGL();
	}

	glLineWidth(1.0f);
	if (textureWasEnabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawLiveParticipants() {
	if (!editor.IsLive()) {
		return;
	}

	const std::vector<LiveParticipant>& participants = editor.GetLive().getParticipantList();
	if (participants.empty()) {
		return;
	}

	const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
	if (textureWasEnabled) {
		glDisable(GL_TEXTURE_2D);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	const LiveParticipantPanelLayout layout = computeLiveParticipantPanelLayout(screensize_x, participants);
	const float swatchSize = 10.0f;
	const uint32_t ownId = editor.GetLive().getOwnClientId();
	const float right = layout.startX + layout.boxWidth;
	const float bottom = layout.startY + layout.boxHeight;

	PushScreenSpaceGL(screensize_x, screensize_y);

	// Panel background
	glColor4ub(20, 22, 26, 225);
	glBegin(GL_QUADS);
	glVertex2f(layout.startX, layout.startY);
	glVertex2f(right, layout.startY);
	glVertex2f(right, bottom);
	glVertex2f(layout.startX, bottom);
	glEnd();

	// Header band
	const float headerBottom = layout.startY + layout.headerHeight;
	glColor4ub(45, 50, 60, 235);
	glBegin(GL_QUADS);
	glVertex2f(layout.startX, layout.startY);
	glVertex2f(right, layout.startY);
	glVertex2f(right, headerBottom);
	glVertex2f(layout.startX, headerBottom);
	glEnd();

	glColor4ub(230, 230, 230, 255);
	glRasterPos2f(layout.startX + layout.padding, layout.startY + 15.0f);
	const wxString headerText = "Connected Users";
	for (size_t i = 0; i < headerText.length(); ++i) {
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, static_cast<unsigned char>(headerText[i]));
	}

	float rowTop = headerBottom;
	for (const LiveParticipant& participant : participants) {
		const float rowBottom = rowTop + layout.lineHeight;
		const bool isSelf = participant.id == ownId;
		const bool isHovered = !isSelf && participant.id == hovered_live_participant;

		if (isHovered) {
			glColor4ub(70, 110, 180, 160);
			glBegin(GL_QUADS);
			glVertex2f(layout.startX + 2.0f, rowTop + 1.0f);
			glVertex2f(right - 2.0f, rowTop + 1.0f);
			glVertex2f(right - 2.0f, rowBottom - 1.0f);
			glVertex2f(layout.startX + 2.0f, rowBottom - 1.0f);
			glEnd();
		}

		const float swatchTop = rowTop + (layout.lineHeight - swatchSize) / 2.0f;
		glColor4ub(participant.color.Red(), participant.color.Green(), participant.color.Blue(), 255);
		glBegin(GL_QUADS);
		glVertex2f(layout.startX + layout.padding, swatchTop);
		glVertex2f(layout.startX + layout.padding + swatchSize, swatchTop);
		glVertex2f(layout.startX + layout.padding + swatchSize, swatchTop + swatchSize);
		glVertex2f(layout.startX + layout.padding, swatchTop + swatchSize);
		glEnd();

		// Name text, dimmed for our own (non-clickable) row
		if (isSelf) {
			glColor4ub(160, 160, 160, 255);
		} else {
			glColor4ub(240, 240, 240, 255);
		}
		glRasterPos2f(layout.startX + layout.padding + layout.textOffset, rowTop + 15.0f);
		wxString label = participant.name;
		if (isSelf) {
			label << " (you - change color)";
		}
		for (size_t i = 0; i < label.length(); ++i) {
			glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, static_cast<unsigned char>(label[i]));
		}

		// Go-to crosshair icon on clickable rows
		if (!isSelf) {
			const float cx = right - layout.iconSpace / 2.0f - 4.0f;
			const float cy = rowTop + layout.lineHeight / 2.0f;
			const float radius = 5.0f;
			if (isHovered) {
				glColor4ub(255, 255, 255, 255);
			} else {
				glColor4ub(170, 180, 195, 220);
			}
			glLineWidth(1.0f);
			glBegin(GL_LINE_LOOP);
			for (int seg = 0; seg < 12; ++seg) {
				const float angle = seg * (2.0f * 3.14159265f / 12.0f);
				glVertex2f(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
			}
			glEnd();
			glBegin(GL_LINES);
			glVertex2f(cx - radius - 2.0f, cy);
			glVertex2f(cx - radius + 2.0f, cy);
			glVertex2f(cx + radius - 2.0f, cy);
			glVertex2f(cx + radius + 2.0f, cy);
			glVertex2f(cx, cy - radius - 2.0f);
			glVertex2f(cx, cy - radius + 2.0f);
			glVertex2f(cx, cy + radius - 2.0f);
			glVertex2f(cx, cy + radius + 2.0f);
			glEnd();
		}

		rowTop = rowBottom;
	}

	// Panel border
	glColor4ub(255, 255, 255, 150);
	glLineWidth(1.0f);
	glBegin(GL_LINE_LOOP);
	glVertex2f(layout.startX, layout.startY);
	glVertex2f(right, layout.startY);
	glVertex2f(right, bottom);
	glVertex2f(layout.startX, bottom);
	glEnd();

	PopScreenSpaceGL();

	if (textureWasEnabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

bool MapDrawer::HitTestLiveParticipant(int screenX, int screenY, uint32_t& participantId) {
	participantId = 0;

	if (!editor.IsLive()) {
		return false;
	}

	const std::vector<LiveParticipant>& participants = editor.GetLive().getParticipantList();
	if (participants.empty()) {
		return false;
	}

	const float scale = canvas->GetContentScaleFactor();
	screenX = int(screenX * scale);
	screenY = int(screenY * scale);

	int viewScrollX = 0;
	int viewScrollY = 0;
	int screenWidth = 0;
	int screenHeight = 0;
	canvas->GetViewBox(&viewScrollX, &viewScrollY, &screenWidth, &screenHeight);

	const LiveParticipantPanelLayout layout = computeLiveParticipantPanelLayout(screenWidth, participants);
	if (screenX < layout.startX || screenX >= layout.startX + layout.boxWidth || screenY < layout.startY || screenY >= layout.startY + layout.boxHeight) {
		return false;
	}

	// Row layout must match DrawLiveParticipants (rows start below the header band).
	const float rowsTop = layout.startY + layout.headerHeight;
	const int row = int((screenY - rowsTop) / layout.lineHeight);
	if (screenY >= rowsTop && row >= 0 && row < static_cast<int>(participants.size())) {
		participantId = participants[row].id;
	}

	// Consume any click inside the panel so it never falls through to map editing.
	return true;
}

bool MapDrawer::UpdateLiveParticipantHover(int screenX, int screenY) {
	uint32_t hoveredId = 0;
	if (editor.IsLive()) {
		uint32_t hitId = 0;
		if (HitTestLiveParticipant(screenX, screenY, hitId) && hitId != editor.GetLive().getOwnClientId()) {
			hoveredId = hitId;
		}
	}

	if (hoveredId == hovered_live_participant) {
		return false;
	}
	hovered_live_participant = hoveredId;
	return true;
}

void MapDrawer::DrawMapComments() {
	const MapComments& comments = editor.map.getComments();
	if (comments.empty()) {
		return;
	}

	const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
	if (textureWasEnabled) {
		glDisable(GL_TEXTURE_2D);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct TileComments {
		int x;
		int y;
		std::vector<const MapComment*> entries;
	};
	std::vector<TileComments> tiles;

	for (const MapComment& comment : comments.all()) {
		if (comment.pos.z != floor) {
			continue;
		}

		bool found = false;
		for (TileComments& tile : tiles) {
			if (tile.x == comment.pos.x && tile.y == comment.pos.y) {
				tile.entries.push_back(&comment);
				found = true;
				break;
			}
		}
		if (!found) {
			tiles.push_back({ comment.pos.x, comment.pos.y, { &comment } });
		}
	}

	struct CommentLabel {
		float screenRight;
		float screenTop;
		wxString text;
	};
	std::vector<CommentLabel> labels;
	labels.reserve(tiles.size());

	for (const TileComments& tile : tiles) {
		const int offset = getFloorAdjustment(floor);
		const float tileLeft = float(tile.x * TileSize - view_scroll_x - offset);
		const float tileTop = float(tile.y * TileSize - view_scroll_y - offset);
		const float tileRight = tileLeft + TileSize;
		const float tileBottom = tileTop + TileSize;
		const float inset = TileSize * 0.15f;
		const float markerLeft = tileLeft + inset;
		const float markerTop = tileTop + inset;
		const float markerRight = tileRight - inset;
		const float markerBottom = tileBottom - inset;

		uint8_t r = 255;
		uint8_t g = 210;
		uint8_t b = 64;
		tryGetLiveAuthorColor(editor, tile.entries.front()->author, r, g, b);

		glColor4ub(r, g, b, 210);
		glBegin(GL_QUADS);
		glVertex2f(markerLeft, markerTop);
		glVertex2f(markerRight, markerTop);
		glVertex2f(markerRight, markerBottom);
		glVertex2f(markerLeft, markerBottom);
		glEnd();

		glLineWidth(2.0f);
		glColor4ub(
			static_cast<uint8_t>(r * 0.7f),
			static_cast<uint8_t>(g * 0.7f),
			static_cast<uint8_t>(b * 0.7f),
			255
		);
		glBegin(GL_LINE_LOOP);
		glVertex2f(markerLeft, markerTop);
		glVertex2f(markerRight, markerTop);
		glVertex2f(markerRight, markerBottom);
		glVertex2f(markerLeft, markerBottom);
		glEnd();

		wxString preview = wxstr(tile.entries.front()->author);
		if (tile.entries.size() > 1) {
			preview << " (+" << (tile.entries.size() - 1) << ")";
		}
		labels.push_back({
			tileRight / zoom,
			tileTop / zoom,
			preview
		});
	}

	if (!labels.empty()) {
		PushScreenSpaceGL(screensize_x, screensize_y);
		for (const CommentLabel& label : labels) {
			DrawScreenTextLabel(label.screenRight, label.screenTop, label.text);
		}
		PopScreenSpaceGL();
	}

	glLineWidth(1.0f);
	if (textureWasEnabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawMapCommentTooltips() {
	const std::vector<const MapComment*> tileComments = editor.map.getComments().atPosition(mouse_map_x, mouse_map_y, floor);
	if (tileComments.empty()) {
		return;
	}

	const int offset = getFloorAdjustment(floor);
	const int draw_x = mouse_map_x * TileSize - view_scroll_x - offset;
	const int draw_y = mouse_map_y * TileSize - view_scroll_y - offset;

	std::ostringstream tip;
	for (size_t i = 0; i < tileComments.size(); ++i) {
		const MapComment* comment = tileComments[i];
		if (i > 0) {
			tip << "\n\n";
		}
		tip << comment->author << " (" << nstr(formatMapCommentTime(comment->createdUnix)) << ")\n";
		tip << comment->text;
	}

	MakeTooltip(draw_x, draw_y, tip.str(), 255, 220, 120, false);
}

void MapDrawer::DrawHoveredItemTooltips() {
	if (!options.show_tooltips) {
		return;
	}

	for (const MapTooltipMarker& marker : tooltip_markers) {
		if (marker.map_x == mouse_map_x && marker.map_y == mouse_map_y) {
			MakeTooltip(marker.draw_x, marker.draw_y, marker.text);
			break;
		}
	}
}

void MapDrawer::DrawTooltipIcons() {
	if (!options.show_tooltips) {
		return;
	}

	for (const MapTooltipMarker& marker : tooltip_markers) {
		std::vector<uint32_t> icons;
		icons.reserve(4);
		if (marker.has_text) {
			icons.push_back(TOOLTIP_ICON_TEXT);
		}
		if (marker.has_destination) {
			icons.push_back(TOOLTIP_ICON_DESTINATION);
		}
		if (marker.has_action) {
			icons.push_back(TOOLTIP_ICON_ACTION);
		}
		if (marker.has_unique) {
			icons.push_back(TOOLTIP_ICON_UNIQUE);
		}
		if (icons.empty()) {
			continue;
		}

		const int columns = icons.size() > 2 ? 2 : static_cast<int>(icons.size());
		const int rows = static_cast<int>((icons.size() + columns - 1) / columns);
		const float bgLeft = float(marker.draw_x) + TOOLTIP_ICON_OFFSET_X;
		const float bgTop = float(marker.draw_y) + TOOLTIP_ICON_OFFSET_Y;
		const float bgRight = bgLeft + float(columns) * TOOLTIP_ICON_CELL + TOOLTIP_ICON_BG_PAD * 2.0f;
		const float bgBottom = bgTop + float(rows) * TOOLTIP_ICON_CELL + TOOLTIP_ICON_BG_PAD * 2.0f;

		GLboolean texturesEnabled = glIsEnabled(GL_TEXTURE_2D);
		if (texturesEnabled) {
			glDisable(GL_TEXTURE_2D);
		}
		glColor4ub(72, 72, 72, 150);
		glBegin(GL_QUADS);
		glVertex2f(bgLeft, bgTop);
		glVertex2f(bgRight, bgTop);
		glVertex2f(bgRight, bgBottom);
		glVertex2f(bgLeft, bgBottom);
		glEnd();
		if (texturesEnabled) {
			glEnable(GL_TEXTURE_2D);
		}

		const auto drawScaledIcon = [&](float slotCenterX, float slotCenterY, uint32_t spriteId, float offsetX, float offsetY, uint8_t red, uint8_t green, uint8_t blue) {
			const float pivotX = slotCenterX + offsetX;
			const float pivotY = slotCenterY + offsetY;
			float anchorX = pivotX;
			float anchorY = pivotY;
			getTooltipIconDrawPosition(spriteId, pivotX, pivotY, anchorX, anchorY);

			glPushMatrix();
			glTranslatef(pivotX, pivotY, 0.0f);
			glScalef(TOOLTIP_ICON_SCALE, TOOLTIP_ICON_SCALE, 1.0f);
			glTranslatef(-pivotX, -pivotY, 0.0f);
			BlitSpriteType(int(anchorX), int(anchorY), spriteId, red, green, blue, 255);
			glPopMatrix();
		};

		for (size_t i = 0; i < icons.size(); ++i) {
			const int col = static_cast<int>(i) % columns;
			const int row = static_cast<int>(i) / columns;
			const float slotCenterX = bgLeft + TOOLTIP_ICON_BG_PAD + (float(col) + 0.5f) * TOOLTIP_ICON_CELL;
			const float slotCenterY = bgTop + TOOLTIP_ICON_BG_PAD + (float(row) + 0.5f) * TOOLTIP_ICON_CELL;

			static const float outlineOffsets[][2] = {
				{-TOOLTIP_ICON_OUTLINE, 0.0f},
				{TOOLTIP_ICON_OUTLINE, 0.0f},
				{0.0f, -TOOLTIP_ICON_OUTLINE},
				{0.0f, TOOLTIP_ICON_OUTLINE},
				{-TOOLTIP_ICON_OUTLINE, -TOOLTIP_ICON_OUTLINE},
				{TOOLTIP_ICON_OUTLINE, -TOOLTIP_ICON_OUTLINE},
				{-TOOLTIP_ICON_OUTLINE, TOOLTIP_ICON_OUTLINE},
				{TOOLTIP_ICON_OUTLINE, TOOLTIP_ICON_OUTLINE},
			};

			for (const float* offset : outlineOffsets) {
				drawScaledIcon(slotCenterX, slotCenterY, icons[i], offset[0], offset[1], 0, 0, 0);
			}
			drawScaledIcon(slotCenterX, slotCenterY, icons[i], 0.0f, 0.0f, 255, 255, 255);
		}
	}
}

void MapDrawer::DrawRampWarningIcons() {
	if (ramp_warning_markers.empty()) {
		return;
	}

	const GLuint warningTexture = g_gui.gfx.getWarningSignTexture();
	if (warningTexture == 0) {
		return;
	}

	const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
	glEnable(GL_TEXTURE_2D);
	for (const MapRampWarningMarker& marker : ramp_warning_markers) {
		glBlitTexture(marker.draw_x, marker.draw_y, warningTexture, 255, 255, 255, 220);
	}
	if (!textureWasEnabled) {
		glDisable(GL_TEXTURE_2D);
	}
}


void MapDrawer::DrawBrush() {
	if (!g_gui.IsDrawingMode()) {
		return;
	}
	if (!g_gui.GetCurrentBrush()) {
		return;
	}
	if (options.ingame) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();

	BrushColor brushColor = COLOR_BLANK;
	if (brush->isTerrain() || brush->isTable() || brush->isCarpet()) {
		brushColor = COLOR_BRUSH;
	} else if (brush->isHouse()) {
		brushColor = COLOR_HOUSE_BRUSH;
	} else if (brush->isFlag()) {
		brushColor = COLOR_FLAG_BRUSH;
	} else if (brush->isSpawn()) {
		brushColor = COLOR_SPAWN_BRUSH;
	} else if (brush->isEraser()) {
		brushColor = COLOR_ERASER;
	}

	if (dragging_draw) {
		ASSERT(brush->canDrag());

		if (dragging_draw_line && !brush->isSpawn()) {
			// Alt-drag: preview the line that will be drawn, brush size and shape
			// included. Takes precedence over the wall rectangle.
			PositionVector tilestodraw;
			canvas->getLineTilesToDraw(canvas->last_click_map_x, canvas->last_click_map_y, mouse_map_x, mouse_map_y, floor, tilestodraw);

			RAWBrush* raw_brush = brush->isRaw() ? brush->asRaw() : nullptr;
			if (raw_brush) {
				glEnable(GL_TEXTURE_2D);
			}

			for (const Position& pos : tilestodraw) {
				int cx = pos.x * TileSize - view_scroll_x - getFloorAdjustment(floor);
				int cy = pos.y * TileSize - view_scroll_y - getFloorAdjustment(floor);
				if (raw_brush) {
					DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
				} else {
					glColor(brushColor);
					glBegin(GL_QUADS);
					glVertex2f(cx, cy + TileSize);
					glVertex2f(cx + TileSize, cy + TileSize);
					glVertex2f(cx + TileSize, cy);
					glVertex2f(cx, cy);
					glEnd();
				}
			}

			if (raw_brush) {
				glDisable(GL_TEXTURE_2D);
			}
		} else if (brush->isWall()) {
			int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
			int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
			int last_click_end_map_x = std::max(canvas->last_click_map_x, mouse_map_x) + 1;
			int last_click_end_map_y = std::max(canvas->last_click_map_y, mouse_map_y) + 1;

			int last_click_start_sx = last_click_start_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
			int last_click_start_sy = last_click_start_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);
			int last_click_end_sx = last_click_end_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
			int last_click_end_sy = last_click_end_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);

			int delta_x = last_click_end_sx - last_click_start_sx;
			int delta_y = last_click_end_sy - last_click_start_sy;

			glColor(brushColor);
			glBegin(GL_QUADS);
			{
				glVertex2f(last_click_start_sx, last_click_start_sy + TileSize);
				glVertex2f(last_click_end_sx, last_click_start_sy + TileSize);
				glVertex2f(last_click_end_sx, last_click_start_sy);
				glVertex2f(last_click_start_sx, last_click_start_sy);
			}

			if (delta_y > TileSize) {
				glVertex2f(last_click_start_sx, last_click_end_sy - TileSize);
				glVertex2f(last_click_start_sx + TileSize, last_click_end_sy - TileSize);
				glVertex2f(last_click_start_sx + TileSize, last_click_start_sy + TileSize);
				glVertex2f(last_click_start_sx, last_click_start_sy + TileSize);
			}

			if (delta_x > TileSize && delta_y > TileSize) {
				glVertex2f(last_click_end_sx - TileSize, last_click_start_sy + TileSize);
				glVertex2f(last_click_end_sx, last_click_start_sy + TileSize);
				glVertex2f(last_click_end_sx, last_click_end_sy - TileSize);
				glVertex2f(last_click_end_sx - TileSize, last_click_end_sy - TileSize);
			}

			if (delta_y > TileSize) {
				glVertex2f(last_click_start_sx, last_click_end_sy - TileSize);
				glVertex2f(last_click_end_sx, last_click_end_sy - TileSize);
				glVertex2f(last_click_end_sx, last_click_end_sy);
				glVertex2f(last_click_start_sx, last_click_end_sy);
			}
			glEnd();
		} else {
			if (brush->isRaw()) {
				glEnable(GL_TEXTURE_2D);
			}

			if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE || brush->isSpawn() /* Spawn brush is always square */) {
				if (brush->isRaw() || brush->isOptionalBorder()) {
					int start_x, end_x;
					int start_y, end_y;

					if (mouse_map_x < canvas->last_click_map_x) {
						start_x = mouse_map_x;
						end_x = canvas->last_click_map_x;
					} else {
						start_x = canvas->last_click_map_x;
						end_x = mouse_map_x;
					}
					if (mouse_map_y < canvas->last_click_map_y) {
						start_y = mouse_map_y;
						end_y = canvas->last_click_map_y;
					} else {
						start_y = canvas->last_click_map_y;
						end_y = mouse_map_y;
					}

					RAWBrush* raw_brush = nullptr;
					if (brush->isRaw()) {
						raw_brush = brush->asRaw();
					}

					for (int y = start_y; y <= end_y; y++) {
						int cy = y * TileSize - view_scroll_y - getFloorAdjustment(floor);
						for (int x = start_x; x <= end_x; x++) {
							int cx = x * TileSize - view_scroll_x - getFloorAdjustment(floor);
							if (brush->isOptionalBorder()) {
								glColorCheck(brush, Position(x, y, floor));
							} else {
								DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
							}
						}
					}
				} else {
					int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
					int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
					int last_click_end_map_x = std::max(canvas->last_click_map_x, mouse_map_x) + 1;
					int last_click_end_map_y = std::max(canvas->last_click_map_y, mouse_map_y) + 1;

					int last_click_start_sx = last_click_start_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
					int last_click_start_sy = last_click_start_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);
					int last_click_end_sx = last_click_end_map_x * TileSize - view_scroll_x - getFloorAdjustment(floor);
					int last_click_end_sy = last_click_end_map_y * TileSize - view_scroll_y - getFloorAdjustment(floor);

					glColor(brushColor);
					glBegin(GL_QUADS);
					glVertex2f(last_click_start_sx, last_click_start_sy);
					glVertex2f(last_click_end_sx, last_click_start_sy);
					glVertex2f(last_click_end_sx, last_click_end_sy);
					glVertex2f(last_click_start_sx, last_click_end_sy);
					glEnd();
				}
			} else if (g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
				// Calculate drawing offsets
				int start_x, end_x;
				int start_y, end_y;
				int width = std::max(
					std::abs(std::max(mouse_map_y, canvas->last_click_map_y) - std::min(mouse_map_y, canvas->last_click_map_y)),
					std::abs(std::max(mouse_map_x, canvas->last_click_map_x) - std::min(mouse_map_x, canvas->last_click_map_x))
				);

				if (mouse_map_x < canvas->last_click_map_x) {
					start_x = canvas->last_click_map_x - width;
					end_x = canvas->last_click_map_x;
				} else {
					start_x = canvas->last_click_map_x;
					end_x = canvas->last_click_map_x + width;
				}

				if (mouse_map_y < canvas->last_click_map_y) {
					start_y = canvas->last_click_map_y - width;
					end_y = canvas->last_click_map_y;
				} else {
					start_y = canvas->last_click_map_y;
					end_y = canvas->last_click_map_y + width;
				}

				int center_x = start_x + (end_x - start_x) / 2;
				int center_y = start_y + (end_y - start_y) / 2;
				float radii = width / 2.0f + 0.005f;

				RAWBrush* raw_brush = nullptr;
				if (brush->isRaw()) {
					raw_brush = brush->asRaw();
				}

				for (int y = start_y - 1; y <= end_y + 1; y++) {
					int cy = y * TileSize - view_scroll_y - getFloorAdjustment(floor);
					float dy = center_y - y;
					for (int x = start_x - 1; x <= end_x + 1; x++) {
						int cx = x * TileSize - view_scroll_x - getFloorAdjustment(floor);

						float dx = center_x - x;
						// printf("%f;%f\n", dx, dy);
						float distance = sqrt(dx * dx + dy * dy);
						if (distance < radii) {
							if (brush->isRaw()) {
								DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
							} else {
								glColor(brushColor);
								glBegin(GL_QUADS);
								glVertex2f(cx, cy + TileSize);
								glVertex2f(cx + TileSize, cy + TileSize);
								glVertex2f(cx + TileSize, cy);
								glVertex2f(cx, cy);
								glEnd();
							}
						}
					}
				}
			}

			if (brush->isRaw()) {
				glDisable(GL_TEXTURE_2D);
			}
		}
	} else {
		if (brush->isWall()) {
			const int squareMinOffset = g_gui.GetSquareBrushMinOffset();
			const int squareMaxOffset = g_gui.GetSquareBrushMaxOffset();
			int start_sx = (mouse_map_x + squareMinOffset) * TileSize - view_scroll_x - getFloorAdjustment(floor);
			int start_sy = (mouse_map_y + squareMinOffset) * TileSize - view_scroll_y - getFloorAdjustment(floor);
			int end_sx = (mouse_map_x + squareMaxOffset + 1) * TileSize - view_scroll_x - getFloorAdjustment(floor);
			int end_sy = (mouse_map_y + squareMaxOffset + 1) * TileSize - view_scroll_y - getFloorAdjustment(floor);

			int delta_x = end_sx - start_sx;
			int delta_y = end_sy - start_sy;

			glColor(brushColor);
			glBegin(GL_QUADS);
			{
				glVertex2f(start_sx, start_sy + TileSize);
				glVertex2f(end_sx, start_sy + TileSize);
				glVertex2f(end_sx, start_sy);
				glVertex2f(start_sx, start_sy);
			}

			if (delta_y > TileSize) {
				glVertex2f(start_sx, end_sy - TileSize);
				glVertex2f(start_sx + TileSize, end_sy - TileSize);
				glVertex2f(start_sx + TileSize, start_sy + TileSize);
				glVertex2f(start_sx, start_sy + TileSize);
			}

			if (delta_x > TileSize && delta_y > TileSize) {
				glVertex2f(end_sx - TileSize, start_sy + TileSize);
				glVertex2f(end_sx, start_sy + TileSize);
				glVertex2f(end_sx, end_sy - TileSize);
				glVertex2f(end_sx - TileSize, end_sy - TileSize);
			}

			if (delta_y > TileSize) {
				glVertex2f(start_sx, end_sy - TileSize);
				glVertex2f(end_sx, end_sy - TileSize);
				glVertex2f(end_sx, end_sy);
				glVertex2f(start_sx, end_sy);
			}
			glEnd();
		} else if (brush->isDoor()) {
			int cx = (mouse_map_x)*TileSize - view_scroll_x - getFloorAdjustment(floor);
			int cy = (mouse_map_y)*TileSize - view_scroll_y - getFloorAdjustment(floor);

			glColorCheck(brush, Position(mouse_map_x, mouse_map_y, floor));
			glBegin(GL_QUADS);
			glVertex2f(cx, cy + TileSize);
			glVertex2f(cx + TileSize, cy + TileSize);
			glVertex2f(cx + TileSize, cy);
			glVertex2f(cx, cy);
			glEnd();
		} else if (brush->isCreature()) {
			glEnable(GL_TEXTURE_2D);
			int cy = (mouse_map_y)*TileSize - view_scroll_y - getFloorAdjustment(floor);
			int cx = (mouse_map_x)*TileSize - view_scroll_x - getFloorAdjustment(floor);
			CreatureBrush* creature_brush = brush->asCreature();
			if (creature_brush->canDraw(&editor.map, Position(mouse_map_x, mouse_map_y, floor))) {
				BlitCreature(cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 255, 255, 160);
			} else {
				BlitCreature(cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 64, 64, 160);
			}
			glDisable(GL_TEXTURE_2D);
		} else if (!brush->isDoodad()) {
			RAWBrush* raw_brush = nullptr;
			if (brush->isRaw()) { // Textured brush
				glEnable(GL_TEXTURE_2D);
				raw_brush = brush->asRaw();
			}

			const int squareMinOffset = g_gui.GetSquareBrushMinOffset();
			const int squareMaxOffset = g_gui.GetSquareBrushMaxOffset();
			const int outerMinOffset = g_gui.GetSquareBrushOuterMinOffset();
			const int outerMaxOffset = g_gui.GetSquareBrushOuterMaxOffset();

			for (int y = outerMinOffset; y <= outerMaxOffset; y++) {
				int cy = (mouse_map_y + y) * TileSize - view_scroll_y - getFloorAdjustment(floor);
				for (int x = outerMinOffset; x <= outerMaxOffset; x++) {
					int cx = (mouse_map_x + x) * TileSize - view_scroll_x - getFloorAdjustment(floor);
					if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
						if (x >= squareMinOffset && x <= squareMaxOffset && y >= squareMinOffset && y <= squareMaxOffset) {
							if (brush->isRaw()) {
								DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
							} else {
								if (brush->isHouseExit() || brush->isOptionalBorder()) {
									glColorCheck(brush, Position(mouse_map_x + x, mouse_map_y + y, floor));
								} else {
									glColor(brushColor);
								}

								glBegin(GL_QUADS);
								glVertex2f(cx, cy + TileSize);
								glVertex2f(cx + TileSize, cy + TileSize);
								glVertex2f(cx + TileSize, cy);
								glVertex2f(cx, cy);
								glEnd();
							}
						}
					} else if (g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
						double distance = sqrt(double(x * x) + double(y * y));
						if (distance < g_gui.GetBrushSize() + 0.005) {
							if (brush->isRaw()) {
								DrawRawBrush(cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
							} else {
								if (brush->isHouseExit() || brush->isOptionalBorder()) {
									glColorCheck(brush, Position(mouse_map_x + x, mouse_map_y + y, floor));
								} else {
									glColor(brushColor);
								}

								glBegin(GL_QUADS);
								glVertex2f(cx, cy + TileSize);
								glVertex2f(cx + TileSize, cy + TileSize);
								glVertex2f(cx + TileSize, cy);
								glVertex2f(cx, cy);
								glEnd();
							}
						}
					}
				}
			}

			if (brush->isRaw()) { // Textured brush
				glDisable(GL_TEXTURE_2D);
			}
		}
	}
}

void MapDrawer::BlitItem(int& draw_x, int& draw_y, const Tile* tile, Item* item, bool ephemeral, int red, int green, int blue, int alpha) {
	const Position& pos = tile->getPosition();
	BlitItem(draw_x, draw_y, pos, item, ephemeral, red, green, blue, alpha, tile);
}

void MapDrawer::BlitItem(int& draw_x, int& draw_y, const Position& pos, Item* item, bool ephemeral, int red, int green, int blue, int alpha, const Tile* tile) {
	ItemType& it = g_items[item->getID()];

	// Locked door indicator
	if (!options.ingame && options.highlight_locked_doors && it.isDoor() && it.isLocked) {
		blue /= 2;
		green /= 2;
	}

	if (!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	// item sprite
	GameSprite* spr = it.sprite;

	// Display invisible and invalid items
	// Ugly hacks. :)
	if (!options.ingame && options.show_tech_items) {
		// Red invalid client id
		if (it.id == 0) {
			BlitSquare(draw_x, draw_y, red, 0, 0, alpha);
			return;
		}

		if (options.show_invisible_items) {
			// light
			/*if (it.clientID >= 7485 && it.clientID <= 7491 || it.clientID >= 9376 && it.clientID <= 9435) {
				spr = g_items[SPRITE_LIGHT].sprite;
				alpha = 160;
			}*/
			// stair
			if (it.clientID == 469) {
				spr = g_items[SPRITE_STAIR].sprite;
				alpha = 160;
			}

			// bridge
			if (it.clientID == 470) {
				spr = g_items[SPRITE_BRIDGE].sprite;
				alpha = 160;
			}

			// los
			if (it.clientID == 2187) {
				spr = g_items[SPRITE_LOS].sprite;
				alpha = 160;
			}

			// quest
			if (it.clientID == 7690) {
				spr = g_items[SPRITE_QUEST].sprite;
				alpha = 160;
			}

			// teleport
			if (it.clientID == 8652) {
				spr = g_items[SPRITE_TP].sprite;
				alpha = 160;
			}

			// script
			if (it.clientID == 8885) {
				spr = g_items[SPRITE_SCRIPT].sprite;
				alpha = 160;
			}

			// wall
			if (it.clientID == 9307) {
				spr = g_items[SPRITE_WALL].sprite;
				alpha = 160;
			}

			// path
			if (it.clientID == 9449) {
				spr = g_items[SPRITE_PATH].sprite;
				alpha = 160;
			}

			// use trigger
			if (it.clientID == 9498) {
				spr = g_items[SPRITE_USE].sprite;
				alpha = 160;
			}

			// move trigger
			if (it.clientID == 9501) {
				spr = g_items[SPRITE_MOVE].sprite;
				alpha = 160;
			}
		}
	}

	// metaItem, sprite not found or not hidden
	if (it.isMetaItem() || spr == nullptr || !ephemeral && it.pickupable && !options.show_items) {
		return;
	}

	if (item->isWall() && !options.show_walls) {
		return;
	}

	int screenx = draw_x - spr->getDrawOffset().first;
	int screeny = draw_y - spr->getDrawOffset().second;

	// Set the newd drawing height accordingly
	draw_x -= spr->getDrawHeight();
	draw_y -= spr->getDrawHeight();

	int subtype = -1;

	int pattern_x = pos.x % spr->pattern_x;
	int pattern_y = pos.y % spr->pattern_y;
	int pattern_z = pos.z % spr->pattern_z;

	if (it.isSplash() || it.isFluidContainer()) {
		subtype = Item::getLiquidColor(static_cast<uint8_t>(item->getSubtype()));
	} else if (it.isHangable) {
		if (tile && tile->hasProperty(HOOK_SOUTH)) {
			pattern_x = 1;
		} else if (tile && tile->hasProperty(HOOK_EAST)) {
			pattern_x = 2;
		} else {
			pattern_x = 0;
		}
	} else if (it.stackable) {
		if (item->getSubtype() <= 1) {
			subtype = 0;
		} else if (item->getSubtype() <= 2) {
			subtype = 1;
		} else if (item->getSubtype() <= 3) {
			subtype = 2;
		} else if (item->getSubtype() <= 4) {
			subtype = 3;
		} else if (item->getSubtype() < 10) {
			subtype = 4;
		} else if (item->getSubtype() < 25) {
			subtype = 5;
		} else if (item->getSubtype() < 50) {
			subtype = 6;
		} else {
			subtype = 7;
		}
	}

	bool shouldMakeTransparent = false;
	if (!ephemeral) {
		if (options.transparent_items && (!it.isGroundTile() || spr->width > 1 || spr->height > 1) && !it.isSplash() && (!it.isBorder || spr->width > 1 || spr->height > 1)) {
			shouldMakeTransparent = true;
		} else if (options.transparent_void_items && tile && !tile->hasGround() && !it.isGroundTile() && !it.isSplash()) {
			shouldMakeTransparent = true;
		}
	}

	if (shouldMakeTransparent) {
		alpha /= 2;
	}

	Podium* podium = dynamic_cast<Podium*>(item);
	if (it.isPodium() && !podium->hasShowPlatform() && !options.ingame) {
		if (options.show_tech_items) {
			alpha /= 2;
		} else {
			alpha = 0;
		}
	}

	int frame = item->getFrame();
	for (int cx = 0; cx != spr->width; cx++) {
		for (int cy = 0; cy != spr->height; cy++) {
			for (int cf = 0; cf != spr->layers; cf++) {
				int texnum = spr->getHardwareID(cx, cy, cf, subtype, pattern_x, pattern_y, pattern_z, frame);
				glBlitTexture(screenx - cx * TileSize, screeny - cy * TileSize, texnum, red, green, blue, alpha);
			}
		}
	}

	// zoomed out very far, avoid drawing stuff barely visible
	if (zoom > 3.0) {
		return;
	}

	if (it.isPodium()) {
		Outfit outfit = podium->getOutfit();
		if (!podium->hasShowOutfit()) {
			if (podium->hasShowMount()) {
				outfit.lookType = outfit.lookMount;
				outfit.lookHead = outfit.lookMountHead;
				outfit.lookBody = outfit.lookMountBody;
				outfit.lookLegs = outfit.lookMountLegs;
				outfit.lookFeet = outfit.lookMountFeet;
				outfit.lookAddon = 0;
				outfit.lookMount = 0;
			} else {
				outfit.lookType = 0;
			}
		}
		if (!podium->hasShowMount()) {
			outfit.lookMount = 0;
		}

		BlitCreature(draw_x, draw_y, outfit, static_cast<Direction>(podium->getDirection()), red, green, blue, 255);
	}

	// draw wall hook
	if (!options.ingame && options.show_hooks && (it.hookSouth || it.hookEast)) {
		DrawHookIndicator(draw_x, draw_y, it);
	}

	// draw light color indicator
	if (!options.ingame && options.show_light_str) {
		const SpriteLight& light = item->getLight();
		if (light.intensity > 0) {
			wxColor lightColor = colorFromEightBit(light.color);
			uint8_t byteR = lightColor.Red();
			uint8_t byteG = lightColor.Green();
			uint8_t byteB = lightColor.Blue();
			uint8_t byteA = 255;

			int startOffset = std::max<int>(16, 32 - light.intensity);
			int sqSize = TileSize - startOffset;
			glDisable(GL_TEXTURE_2D);
			glBlitSquare(draw_x + startOffset - 2, draw_y + startOffset - 2, 0, 0, 0, byteA, sqSize + 2);
			glBlitSquare(draw_x + startOffset - 1, draw_y + startOffset - 1, byteR, byteG, byteB, byteA, sqSize);
			glEnable(GL_TEXTURE_2D);
		}
	}

	// draw container indicator (small bag icon for containers with items)
	if (!options.ingame && zoom <= 3.0) {
		if (Container* container = dynamic_cast<Container*>(item)) {
			if (container->getItemCount() > 0) {
				// Draw a small bag icon centered over the container item
				// Use backpack sprite (1987) as the bag icon
				const uint32_t BAG_SPRITE_ID = 1987;
				GameSprite* bagSpr = g_items[BAG_SPRITE_ID].sprite;
				if (bagSpr != nullptr) {
					// Position centered over the container item
					// Account for the sprite's draw offset to find the visual center
					// screenx/screeny already subtract the offset, so add it back to get visual center
					float iconX = float(screenx) + float(spr->getDrawOffset().first) + float(spr->width - 1) * TileSize / 2.0f;
					float iconY = float(screeny) + float(spr->getDrawOffset().second) + float(spr->height - 1) * TileSize / 2.0f;

					// Draw the sprite scaled down using OpenGL matrix transformation
					glPushMatrix();
					// Translate to the icon position
					glTranslatef(iconX, iconY, 0.0f);
					// Scale down to 50% size
					glScalef(0.5f, 0.5f, 1.0f);
					// Translate back to account for sprite offset
					glTranslatef(-iconX, -iconY, 0.0f);

					// Draw the sprite
					BlitSpriteType(int(iconX), int(iconY), BAG_SPRITE_ID, 255, 255, 255, 220);

					// Restore matrix
					glPopMatrix();
				}
			}
		}
	}

	// draw fishing spot indicator (small fish icon for fishing spots)
	if (!options.ingame && options.show_fishable_water && zoom <= 3.0) {
		uint32_t itemId = item->getID();
		bool isFishingSpot = (itemId == 4608 || itemId == 4609 || itemId == 4610 || itemId == 4611 || itemId == 4612 || itemId == 4613 ||
		                      itemId == 490 ||
		                      itemId == 4820 || itemId == 4821 || itemId == 4822 || itemId == 4823 || itemId == 4824 || itemId == 4825);

		if (isFishingSpot) {
			// Draw a small fish icon centered over the fishing spot item
			// Use fish sprite (2667) as the fish icon
			const uint32_t FISH_SPRITE_ID = 2667;
			GameSprite* fishSpr = g_items[FISH_SPRITE_ID].sprite;
			if (fishSpr != nullptr) {
				// Position centered over the fishing spot item
				// Account for the sprite's draw offset to find the visual center
				// screenx/screeny already subtract the offset, so add it back to get visual center
				float iconX = float(screenx) + float(spr->getDrawOffset().first) + float(spr->width - 1) * TileSize / 2.0f;
				float iconY = float(screeny) + float(spr->getDrawOffset().second) + float(spr->height - 1) * TileSize / 2.0f;

				// Draw the sprite scaled down using OpenGL matrix transformation
				glPushMatrix();
				// Translate to the icon position
				glTranslatef(iconX, iconY, 0.0f);
				// Scale down to 50% size
				glScalef(0.5f, 0.5f, 1.0f);
				// Translate back to account for sprite offset
				glTranslatef(-iconX, -iconY, 0.0f);

				// Draw the sprite
				BlitSpriteType(int(iconX), int(iconY), FISH_SPRITE_ID, 255, 255, 255, 220);

				// Restore matrix
				glPopMatrix();
			}
		}
	}
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, uint32_t spriteid, int red, int green, int blue, int alpha) {
	GameSprite* spr = g_items[spriteid].sprite;
	if (spr == nullptr) {
		return;
	}
	screenx -= spr->getDrawOffset().first;
	screeny -= spr->getDrawOffset().second;

	int tme = 0; // GetTime() % itype->FPA;
	for (int cx = 0; cx != spr->width; ++cx) {
		for (int cy = 0; cy != spr->height; ++cy) {
			for (int cf = 0; cf != spr->layers; ++cf) {
				int texnum = spr->getHardwareID(cx, cy, cf, -1, 0, 0, 0, tme);
				// printf("CF: %d\tTexturenum: %d\n", cf, texnum);
				glBlitTexture(screenx - cx * TileSize, screeny - cy * TileSize, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, GameSprite* spr, int red, int green, int blue, int alpha) {
	if (spr == nullptr) {
		return;
	}
	screenx -= spr->getDrawOffset().first;
	screeny -= spr->getDrawOffset().second;

	int tme = 0; // GetTime() % itype->FPA;
	for (int cx = 0; cx != spr->width; ++cx) {
		for (int cy = 0; cy != spr->height; ++cy) {
			for (int cf = 0; cf != spr->layers; ++cf) {
				int texnum = spr->getHardwareID(cx, cy, cf, -1, 0, 0, 0, tme);
				// printf("CF: %d\tTexturenum: %d\n", cf, texnum);
				glBlitTexture(screenx - cx * TileSize, screeny - cy * TileSize, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Outfit& outfit, Direction dir, int red, int green, int blue, int alpha) {
	if (outfit.lookItem != 0) {
		ItemType& it = g_items[outfit.lookItem];
		BlitSpriteType(screenx, screeny, it.sprite, red, green, blue, alpha);
	} else {
		// get outfit sprite
		GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType);
		if (!spr || outfit.lookType == 0) {
			return;
		}

		int tme = 0; // GetTime() % itype->FPA;

		// mount and addon drawing thanks to otc code
		// mount colors by Zbizu
		int pattern_z = 0;
		if (outfit.lookMount != 0) {
			if (GameSprite* mountSpr = g_gui.gfx.getCreatureSprite(outfit.lookMount)) {
				// generate mount colors
				Outfit mountOutfit;
				mountOutfit.lookType = outfit.lookMount;
				mountOutfit.lookHead = outfit.lookMountHead;
				mountOutfit.lookBody = outfit.lookMountBody;
				mountOutfit.lookLegs = outfit.lookMountLegs;
				mountOutfit.lookFeet = outfit.lookMountFeet;

				for (int cx = 0; cx != mountSpr->width; ++cx) {
					for (int cy = 0; cy != mountSpr->height; ++cy) {
						int texnum = mountSpr->getHardwareID(cx, cy, (int)dir, 0, 0, mountOutfit, tme);
						glBlitTexture(screenx - cx * TileSize, screeny - cy * TileSize, texnum, red, green, blue, alpha);
					}
				}

				pattern_z = std::min<int>(1, spr->pattern_z - 1);
			}
		}

		// pattern_y => creature addon
		for (int pattern_y = 0; pattern_y < spr->pattern_y; pattern_y++) {

			// continue if we dont have this addon
			if (pattern_y > 0 && !(outfit.lookAddon & (1 << (pattern_y - 1)))) {
				continue;
			}

			for (int cx = 0; cx != spr->width; ++cx) {
				for (int cy = 0; cy != spr->height; ++cy) {
					int texnum = spr->getHardwareID(cx, cy, (int)dir, pattern_y, pattern_z, outfit, tme);
					glBlitTexture(screenx - cx * TileSize, screeny - cy * TileSize, texnum, red, green, blue, alpha);
				}
			}
		}
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Creature* c, int red, int green, int blue, int alpha) {
	if (!options.ingame && c->isSelected()) {
		red /= 2;
		green /= 2;
		blue /= 2;
	}
	BlitCreature(screenx, screeny, c->getLookType(), c->getDirection(), red, green, blue, alpha);
	DrawCreatureSpawnTime(screenx, screeny, c);
}

void MapDrawer::DrawCreatureSpawnTime(int screenx, int screeny, const Creature* creature) {
	if (!options.show_creature_spawn_time || !creature) {
		return;
	}

	const int spawn_time = creature->getSpawnTime();
	if (spawn_time <= 0) {
		return;
	}

	std::string text = i2s(spawn_time);

	float text_width = 0.0f;
	for (const char ch : text) {
		text_width += glutBitmapWidth(GLUT_BITMAP_HELVETICA_10, ch);
	}

	const float padding = 2.0f;
	const float background_width = text_width + padding * 2.0f;
	const float background_height = 10.0f + padding * 2.0f;
	const float background_x = screenx + padding;
	const float background_y = screeny + padding;

	GLboolean textures_enabled = glIsEnabled(GL_TEXTURE_2D);
	if (textures_enabled) {
		glDisable(GL_TEXTURE_2D);
	}
	glColor4ub(0, 0, 0, 160);
	glBegin(GL_QUADS);
	glVertex2f(background_x, background_y);
	glVertex2f(background_x + background_width, background_y);
	glVertex2f(background_x + background_width, background_y + background_height);
	glVertex2f(background_x, background_y + background_height);
	glEnd();

	glColor4ub(255, 255, 255, 255);
	glRasterPos2f(background_x + padding, background_y + background_height - padding);
	for (const char ch : text) {
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, ch);
	}
	if (textures_enabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::BlitSquare(int sx, int sy, int red, int green, int blue, int alpha, int size) {
	if (size == 0) {
		size = TileSize;
	}

	GameSprite* spr = g_items[SPRITE_ZONE].sprite;
	if (!spr) {
		return;
	}

	int texnum = spr->getHardwareID(0, 0, 0, -1, 0, 0, 0, 0);
	if (texnum == 0) {
		return;
	}

	glBindTexture(GL_TEXTURE_2D, texnum);
	glColor4ub(uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha));
	glBegin(GL_QUADS);
	glTexCoord2f(0.f, 0.f);
	glVertex2f(sx, sy);
	glTexCoord2f(1.f, 0.f);
	glVertex2f(sx + TileSize, sy);
	glTexCoord2f(1.f, 1.f);
	glVertex2f(sx + TileSize, sy + TileSize);
	glTexCoord2f(0.f, 1.f);
	glVertex2f(sx, sy + TileSize);
	glEnd();
}

void MapDrawer::DrawRawBrush(int screenx, int screeny, ItemType* itemType, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	GameSprite* spr = itemType->sprite;
	uint16_t cid = itemType->clientID;

	if (options.show_invisible_items) {
		// light
		/*if (cid >= 7485 && cid <= 7491 || cid >= 9376 && cid <= 9435) {
			spr = g_items[SPRITE_LIGHT].sprite;
			alpha = 160;
		}*/

		// stair
		if (cid == 469) {
			spr = g_items[SPRITE_STAIR].sprite;
			alpha = 160;
		}

		// bridge
		if (cid == 470) {
			spr = g_items[SPRITE_BRIDGE].sprite;
			alpha = 160;
		}

		// los
		if (cid == 2187) {
			spr = g_items[SPRITE_LOS].sprite;
			alpha = 160;
		}

		// quest
		if (cid == 7690) {
			spr = g_items[SPRITE_QUEST].sprite;
			alpha = 160;
		}

		// teleport
		if (cid == 8652) {
			spr = g_items[SPRITE_TP].sprite;
			alpha = 160;
		}

		// script
		if (cid == 8885) {
			spr = g_items[SPRITE_SCRIPT].sprite;
			alpha = 160;
		}

		// wall
		if (cid == 9307) {
			spr = g_items[SPRITE_WALL].sprite;
			alpha = 160;
		}

		// path
		if (cid == 9449) {
			spr = g_items[SPRITE_PATH].sprite;
			alpha = 160;
		}

		// use trigger
		if (cid == 9498) {
			spr = g_items[SPRITE_USE].sprite;
			alpha = 160;
		}

		// move trigger
		if (cid == 9501) {
			spr = g_items[SPRITE_MOVE].sprite;
			alpha = 160;
		}

		BlitSpriteType(screenx, screeny, spr, r, g, b, alpha);
	}
}

void MapDrawer::WriteTooltip(Item* item, TileTooltipData& data, bool isHouseTile) {
	if (item == nullptr) {
		return;
	}

	const uint16_t id = item->getID();
	if (id < 100) {
		return;
	}

	const uint16_t unique = item->getUniqueID();
	const uint16_t action = item->getActionID();
	const std::string& text = item->getText();
	uint8_t doorId = 0;
	bool hasDoorProperties = false;

	if (isHouseTile && item->isDoor()) {
		if (const Door* door = item->asDoor()) {
			if (door->isRealDoor()) {
				doorId = door->getDoorID();
			}
		}
	}

	// Check for door properties before early return
	if (const Door* door = item->asDoor()) {
		if (door->getKeyHoleNumber() > 0 || door->getDoorLevel() > 0 || 
		    door->getQuestNumber() > 0 || door->getQuestValue() > 0) {
			hasDoorProperties = true;
		}
	}

	const Teleport* tp = item->asTeleport();
	if (unique == 0 && action == 0 && doorId == 0 && text.empty() && !tp && !hasDoorProperties) {
		return;
	}

	if (data.stream.tellp() > 0) {
		data.stream << "\n";
	}

	data.stream << "id: " << id << "\n";

	if (action > 0) {
		data.has_action = true;
		data.stream << "aid: " << action << "\n";
	}
	if (unique > 0) {
		data.has_unique = true;
		data.stream << "uid: " << unique << "\n";
	}
	if (doorId > 0) {
		data.stream << "door id: " << static_cast<int>(doorId) << "\n";
	}
	if (!text.empty()) {
		data.has_text = true;
		data.stream << "text: " << text << "\n";
	}
	if (tp) {
		data.has_destination = true;
		Position dest = tp->getDestination();
		data.stream << "destination: " << dest.x << ", " << dest.y << ", " << dest.z << "\n";
	}

	// Key number
	if (g_settings.getBoolean(Config::SHOW_TOOLTIP_KEY_NUMBER)) {
		if (const Key* key = item->asKey()) {
			uint16_t keyNumber = key->getKeyNumber();
			if (keyNumber > 0) {
				data.stream << "key number: " << keyNumber << "\n";
			}
		}
	}

	// Door properties (always show like Action ID and Unique ID)
	if (const Door* door = item->asDoor()) {
		uint16_t keyHoleNumber = door->getKeyHoleNumber();
		uint16_t doorLevel = door->getDoorLevel();
		uint16_t questNumber = door->getQuestNumber();
		uint16_t questValue = door->getQuestValue();

		if (keyHoleNumber > 0) {
			data.stream << "key hole: " << keyHoleNumber << "\n";
		}
		if (doorLevel > 0) {
			data.stream << "door level: " << doorLevel << "\n";
		}
		if (questNumber > 0) {
			data.stream << "quest number: " << questNumber << "\n";
		}
		if (questValue > 0) {
			data.stream << "quest value: " << questValue << "\n";
		}
	}
}

void MapDrawer::DrawTile(TileLocation* location) {
	if (!location) {
		return;
	}
	Tile* tile = location->get();

	if (!tile) {
		return;
	}

	if (options.show_only_modified && !tile->isModified()) {
		return;
	}

	int map_x = location->getX();
	int map_y = location->getY();
	int map_z = location->getZ();

	bool as_minimap = options.show_as_minimap;
	bool only_colors = as_minimap || options.show_only_colors;

	int offset;
	if (map_z <= GROUND_LAYER) {
		offset = (GROUND_LAYER - map_z) * TileSize;
	} else {
		offset = TileSize * (floor - map_z);
	}

	int draw_x = ((map_x * TileSize) - view_scroll_x) - offset;
	int draw_y = ((map_y * TileSize) - view_scroll_y) - offset;

	uint8_t r = 255, g = 255, b = 255;

	// begin filters for ground tile
	if (!as_minimap) {
		bool showspecial = options.show_only_colors || options.show_special_tiles;

		if (options.show_blocking && tile->isBlocking() && tile->size() > 0) {
			g = g / 3 * 2;
			b = b / 3 * 2;
		}

		int item_count = tile->items.size();
		if (options.highlight_items && item_count > 0 && !tile->items.back()->isBorder()) {
			static const float factor[5] = { 0.75f, 0.6f, 0.48f, 0.40f, 0.33f };
			int idx = (item_count < 5 ? item_count : 5) - 1;
			g = int(g * factor[idx]);
			r = int(r * factor[idx]);
		}

		if (options.show_spawns && location->getSpawnCount() > 0) {
			float f = 1.0f;
			for (uint32_t i = 0; i < location->getSpawnCount(); ++i) {
				f *= 0.7f;
			}
			g = uint8_t(g * f);
			b = uint8_t(b * f);
		}

		if (options.show_houses && tile->isHouseTile()) {
			if ((int)tile->getHouseID() == current_house_id) {
				r /= 2;
			} else {
				r /= 2;
				g /= 2;
			}
		} else if (showspecial && tile->isPZ()) {
			r /= 2;
			b /= 2;
		}

		if (showspecial && tile->getMapFlags() & TILESTATE_PVPZONE) {
			g = r / 4;
			b = b / 3 * 2;
		}

		if (showspecial && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
			r = std::min<int>(r * 1.2f, 255);
			g /= 2;
			b /= 3;
		}

		if (showspecial && tile->getMapFlags() & TILESTATE_NOPVP) {
			g /= 2;
		}

		if (showspecial && tile->getMapFlags() & TILESTATE_REFRESH) {
			r = 255;
			g = 165;
			b = 0;
		}
	}

	if (only_colors) {
		if (as_minimap) {
			uint8_t color = tile->getMiniMapColor();
			r = (uint8_t)(int(color / 36) % 6 * 51);
			g = (uint8_t)(int(color / 6) % 6 * 51);
			b = (uint8_t)(color % 6 * 51);
			BlitSquare(draw_x, draw_y, r, g, b, 255);
		} else if (r != 255 || g != 255 || b != 255) {
			BlitSquare(draw_x, draw_y, r, g, b, 128);
		}
	} else {
		if (tile->ground && options.show_ground) {
			if (options.show_preview && zoom <= 2.0) {
				tile->ground->animate();
			}

			BlitItem(draw_x, draw_y, tile, tile->ground, false, r, g, b);
		} else if (options.always_show_zones && (r != 255 || g != 255 || b != 255)) {
			DrawRawBrush(draw_x, draw_y, &g_items[SPRITE_ZONE], r, g, b, 60);
		}
	}

	TileTooltipData tileTooltip;

	if (options.show_tooltips && map_z == floor && tile->ground) {
		WriteTooltip(tile->ground, tileTooltip);
	}
	// end filters for ground tile

	if (!only_colors) {
		if (zoom < 10.0 || !options.hide_items_when_zoomed) {
			// items on tile
			for (ItemVector::iterator it = tile->items.begin(); it != tile->items.end(); it++) {
				if ((*it)->isBorder() && !options.show_borders) {
					continue;
				}
				// item tooltip
				if (options.show_tooltips && map_z == floor) {
					WriteTooltip(*it, tileTooltip, tile->isHouseTile());
				}

				// item animation
				if (options.show_preview && zoom <= 2.0) {
					(*it)->animate();
				}

				// item sprite
				if ((*it)->isBorder()) {
					BlitItem(draw_x, draw_y, tile, *it, false, r, g, b);
				} else {
					r = 255, g = 255, b = 255;

					if (options.extended_house_shader && options.show_houses && tile->isHouseTile()) {
						if ((int)tile->getHouseID() == current_house_id) {
							r /= 2;
						} else {
							r /= 2;
							g /= 2;
						}
					}
					BlitItem(draw_x, draw_y, tile, *it, false, r, g, b);
				}
			}
			// monster/npc on tile
			if (tile->creature && options.show_creatures) {
				BlitCreature(draw_x, draw_y, tile->creature);
			}
		}

		if (zoom < 10.0) {
			// house exit (blue splash)
			if (tile->isHouseExit() && options.show_houses) {
				if (tile->hasHouseExit(current_house_id)) {
					BlitSpriteType(draw_x, draw_y, SPRITE_HOUSE_EXIT, 64, 255, 255);
				} else {
					BlitSpriteType(draw_x, draw_y, SPRITE_HOUSE_EXIT, 64, 64, 255);
				}
			}

			// town temple (gray flag)
			if (options.show_towns && tile->isTownExit(editor.map)) {
				BlitSpriteType(draw_x, draw_y, SPRITE_TOWN_TEMPLE, 255, 255, 64, 170);
			}

			// spawn (purple flame)
			if (tile->spawn && options.show_spawns) {
				if (tile->spawn->isSelected()) {
					BlitSpriteType(draw_x, draw_y, SPRITE_SPAWN, 128, 128, 128, 120);
				} else {
					BlitSpriteType(draw_x, draw_y, SPRITE_SPAWN, 255, 255, 255, 120);
				}
			}

			// invalid ramp / door placement (warning sign drawn in DrawRampWarningIcons after the map)
			if (!options.ingame && tileHasInvalidRamp(tile, editor.map)) {
				ramp_warning_markers.push_back(MapRampWarningMarker {
					draw_x,
					draw_y - TileSize / 2, //pushes the warning sign up to the top of the tile
				});
			} else if (!options.ingame && tileHasInvalidItemSet(tile)) {
				ramp_warning_markers.push_back(MapRampWarningMarker {
					draw_x,
					draw_y,
				});
			} else if (!options.ingame && tileHasDoorOnBlockingItem(tile)) {
				ramp_warning_markers.push_back(MapRampWarningMarker {
					draw_x,
					draw_y,
				});
			}

			// tooltip markers (icons drawn in DrawTooltipIcons after the map)
			if (options.show_tooltips && tileTooltip.hasContent()) {
				tooltip_markers.push_back(MapTooltipMarker {
					map_x,
					map_y,
					draw_x,
					draw_y,
					tileTooltip.str(),
					tileTooltip.has_text,
					tileTooltip.has_destination,
					tileTooltip.has_action,
					tileTooltip.has_unique,
				});
			}
		}
	}
}

void MapDrawer::DrawBrushIndicator(int x, int y, Brush* brush, uint8_t r, uint8_t g, uint8_t b) {
	x += (TileSize / 2);
	y += (TileSize / 2);

	// 7----0----1
	// |         |
	// 6--5  3--2
	//     \/
	//     4
	static int vertexes[9][2] = {
		{ -15, -20 }, // 0
		{ 15, -20 }, // 1
		{ 15, -5 }, // 2
		{ 5, -5 }, // 3
		{ 0, 0 }, // 4
		{ -5, -5 }, // 5
		{ -15, -5 }, // 6
		{ -15, -20 }, // 7
		{ -15, -20 }, // 0
	};

	// circle
	glBegin(GL_TRIANGLE_FAN);
	glColor4ub(0x00, 0x00, 0x00, 0x50);
	glVertex2i(x, y);
	for (int i = 0; i <= 30; i++) {
		float angle = i * 2.0f * PI / 30;
		glVertex2f(cos(angle) * (TileSize / 2) + x, sin(angle) * (TileSize / 2) + y);
	}
	glEnd();

	// background
	glColor4ub(r, g, b, 0xB4);
	glBegin(GL_POLYGON);
	for (int i = 0; i < 8; ++i) {
		glVertex2i(vertexes[i][0] + x, vertexes[i][1] + y);
	}
	glEnd();

	// borders
	glColor4ub(0x00, 0x00, 0x00, 0xB4);
	glLineWidth(1.0);
	glBegin(GL_LINES);
	for (int i = 0; i < 8; ++i) {
		glVertex2i(vertexes[i][0] + x, vertexes[i][1] + y);
		glVertex2i(vertexes[i + 1][0] + x, vertexes[i + 1][1] + y);
	}
	glEnd();
}

void MapDrawer::DrawHookIndicator(int x, int y, const ItemType& type) {
	glDisable(GL_TEXTURE_2D);
	glColor4ub(uint8_t(0), uint8_t(0), uint8_t(255), uint8_t(200));
	glBegin(GL_QUADS);
	if (type.hookSouth) {
		x -= 10;
		y += 10;
		glVertex2f(x, y);
		glVertex2f(x + 10, y);
		glVertex2f(x + 20, y + 10);
		glVertex2f(x + 10, y + 10);
	} else if (type.hookEast) {
		x += 10;
		y -= 10;
		glVertex2f(x, y);
		glVertex2f(x + 10, y + 10);
		glVertex2f(x + 10, y + 20);
		glVertex2f(x, y + 10);
	}
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawTileCoordinates() {
	const float scaledTile = TileSize * zoom;
	if (zoom > 1.0f) {
		return;
	}
	if (scaledTile < 16.0f) {
		return;
	}

	int step = 1;
	if (scaledTile < 64.0f) {
		step = 2;
	}
	if (scaledTile < 32.0f) {
		step = 4;
	}

	auto alignToStep = [step](int value) {
		if (step <= 1) {
			return value;
		}
		int remainder = value % step;
		if (remainder < 0) {
			remainder += step;
		}
		return remainder == 0 ? value : value + (step - remainder);
	};

	const int alignedStartX = alignToStep(start_x);
	const int alignedStartY = alignToStep(start_y);
	const float padding = 2.0f;
	const float fontHeight = 10.0f;
	const float minX = -TileSize;
	const float minY = -TileSize;
	const float maxX = screensize_x * zoom + TileSize;
	const float maxY = screensize_y * zoom + TileSize;

	GLboolean texturesEnabled = glIsEnabled(GL_TEXTURE_2D);
	if (texturesEnabled) {
		glDisable(GL_TEXTURE_2D);
	}

	for (int map_y = alignedStartY; map_y < end_y; map_y += step) {
		for (int map_x = alignedStartX; map_x < end_x; map_x += step) {
			const float sx = map_x * TileSize - view_scroll_x;
			const float sy = map_y * TileSize - view_scroll_y;
			if (sx + TileSize < minX || sy + TileSize < minY || sx > maxX || sy > maxY) {
				continue;
			}

			std::string text = i2s(map_x) + "," + i2s(map_y) + "," + i2s(floor);

			float textWidth = 0.0f;
			for (char ch : text) {
				textWidth += glutBitmapWidth(GLUT_BITMAP_HELVETICA_10, ch);
			}

			const float backgroundWidth = textWidth + padding * 2.0f;
			const float backgroundHeight = fontHeight + padding * 2.0f;
			const float centerX = sx + TileSize / 2.0f;
			const float centerY = sy + TileSize / 2.0f;
			const float backgroundX = centerX - (backgroundWidth / 2.0f);
			const float backgroundY = centerY - (backgroundHeight / 2.0f);

			glColor4ub(0, 0, 0, 160);
			glBegin(GL_QUADS);
			glVertex2f(backgroundX, backgroundY);
			glVertex2f(backgroundX + backgroundWidth, backgroundY);
			glVertex2f(backgroundX + backgroundWidth, backgroundY + backgroundHeight);
			glVertex2f(backgroundX, backgroundY + backgroundHeight);
			glEnd();

			glColor4ub(255, 255, 255, 240);
			glRasterPos2f(backgroundX + padding, backgroundY + backgroundHeight - padding);
			for (char ch : text) {
				glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, ch);
			}
		}
	}

	if (texturesEnabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawHUD() {
	// On-canvas heads-up display showing the centered map position, current
	// floor and zoom. Drawn in screen space, top-left corner.
	const int center_x = start_x + int(screensize_x * zoom / 64);
	const int center_y = start_y + int(screensize_y * zoom / 64);
	const int zoom_percent = int(100.0f / zoom + 0.5f);

	const wxString lines[] = {
		wxString::Format("X: %d  Y: %d  Z: %d", center_x, center_y, floor),
		wxString::Format("Floor %d    Zoom %d%%", floor, zoom_percent),
	};
	const size_t line_count = sizeof(lines) / sizeof(lines[0]);

	const float padding = 5.0f;
	const float lineHeight = 14.0f;

	float textWidth = 0.0f;
	for (const wxString& line : lines) {
		float width = 0.0f;
		for (size_t i = 0; i < line.length(); ++i) {
			width += glutBitmapWidth(GLUT_BITMAP_HELVETICA_12, static_cast<unsigned char>(line[i]));
		}
		textWidth = std::max(textWidth, width);
	}

	const float boxLeft = padding;
	const float boxTop = padding;
	const float boxWidth = textWidth + padding * 2.0f;
	const float boxHeight = lineHeight * line_count + padding * 2.0f;

	const GLboolean texturesEnabled = glIsEnabled(GL_TEXTURE_2D);
	if (texturesEnabled) {
		glDisable(GL_TEXTURE_2D);
	}
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	PushScreenSpaceGL(screensize_x, screensize_y);

	glColor4ub(0, 0, 0, 180);
	glBegin(GL_QUADS);
	glVertex2f(boxLeft, boxTop);
	glVertex2f(boxLeft + boxWidth, boxTop);
	glVertex2f(boxLeft + boxWidth, boxTop + boxHeight);
	glVertex2f(boxLeft, boxTop + boxHeight);
	glEnd();

	glColor4ub(255, 255, 255, 240);
	for (size_t i = 0; i < line_count; ++i) {
		glRasterPos2f(boxLeft + padding, boxTop + padding + lineHeight * (i + 1) - 3.0f);
		const wxString& line = lines[i];
		for (size_t j = 0; j < line.length(); ++j) {
			glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, static_cast<unsigned char>(line[j]));
		}
	}

	PopScreenSpaceGL();

	if (texturesEnabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawAutoborderIndicator() {
	// On-canvas indicator, top-left corner (below the HUD box, if visible),
	// showing whether Automagic (autoborder) is currently enabled.
	const bool automagic_on = g_settings.getInteger(Config::USE_AUTOMAGIC) != 0;
	const wxString text = automagic_on ? "Automagic: ON" : "Automagic: OFF";

	const float padding = 5.0f;
	const float lineHeight = 14.0f;

	float textWidth = 0.0f;
	for (size_t i = 0; i < text.length(); ++i) {
		textWidth += glutBitmapWidth(GLUT_BITMAP_HELVETICA_12, static_cast<unsigned char>(text[i]));
	}

	const float boxWidth = textWidth + padding * 2.0f;
	const float boxHeight = lineHeight + padding * 2.0f;
	const float boxLeft = padding;
	const float hudHeight = options.show_hud ? (lineHeight * 2.0f + padding * 2.0f + padding) : 0.0f;
	const float boxTop = padding + hudHeight;

	const GLboolean texturesEnabled = glIsEnabled(GL_TEXTURE_2D);
	if (texturesEnabled) {
		glDisable(GL_TEXTURE_2D);
	}
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	PushScreenSpaceGL(screensize_x, screensize_y);

	glColor4ub(0, 0, 0, 180);
	glBegin(GL_QUADS);
	glVertex2f(boxLeft, boxTop);
	glVertex2f(boxLeft + boxWidth, boxTop);
	glVertex2f(boxLeft + boxWidth, boxTop + boxHeight);
	glVertex2f(boxLeft, boxTop + boxHeight);
	glEnd();

	if (automagic_on) {
		glColor4ub(120, 255, 120, 240);
	} else {
		glColor4ub(220, 220, 220, 200);
	}
	glRasterPos2f(boxLeft + padding, boxTop + padding + lineHeight - 3.0f);
	for (size_t i = 0; i < text.length(); ++i) {
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, static_cast<unsigned char>(text[i]));
	}

	PopScreenSpaceGL();

	if (texturesEnabled) {
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawTooltips() {
	for (const MapTooltip& tooltip : tooltips) {
		const char* text = tooltip.text.c_str();
		float line_width = 0.0f;
		float width = 2.0f;
		float height = 14.0f;
		int char_count = 0;
		int line_char_count = 0;

		for (const char* c = text; *c != '\0'; c++) {
			if (*c == '\n' || (line_char_count >= MapTooltip::MAX_CHARS_PER_LINE && *c == ' ')) {
				height += 14.0f;
				line_width = 0.0f;
				line_char_count = 0;
			} else {
				line_width += glutBitmapWidth(GLUT_BITMAP_HELVETICA_12, *c);
			}
			width = std::max<float>(width, line_width);
			char_count++;
			line_char_count++;

			if (tooltip.ellipsis && char_count > (MapTooltip::MAX_CHARS + 3)) {
				break;
			}
		}

		float scale = zoom < 1.0f ? zoom : 1.0f;

		width = (width + 8.0f) * scale;
		height = (height + 4.0f) * scale;

		float x = tooltip.x + (TileSize / 2.0f);
		float y = tooltip.y;
		float center = width / 2.0f;
		float space = (7.0f * scale);
		float startx = x - center;
		float endx = x + center;
		float starty = y - (height + space);
		float endy = y - space;

		// 7----0----1
		// |         |
		// 6--5  3--2
		//     \/
		//     4
		float vertexes[9][2] = {
			{ x, starty }, // 0
			{ endx, starty }, // 1
			{ endx, endy }, // 2
			{ x + space, endy }, // 3
			{ x, y }, // 4
			{ x - space, endy }, // 5
			{ startx, endy }, // 6
			{ startx, starty }, // 7
			{ x, starty }, // 0
		};

		// background
		glColor4ub(tooltip.r, tooltip.g, tooltip.b, 255);
		glBegin(GL_POLYGON);
		for (int i = 0; i < 8; ++i) {
			glVertex2f(vertexes[i][0], vertexes[i][1]);
		}
		glEnd();

		// borders
		glColor4ub(0, 0, 0, 255);
		glLineWidth(1.0);
		glBegin(GL_LINES);
		for (int i = 0; i < 8; ++i) {
			glVertex2f(vertexes[i][0], vertexes[i][1]);
			glVertex2f(vertexes[i + 1][0], vertexes[i + 1][1]);
		}
		glEnd();

		// text
		if (zoom <= 1.0) {
			startx += (3.0f * scale);
			starty += (14.0f * scale);
			glColor4ub(0, 0, 0, 255);
			glRasterPos2f(startx, starty);
			char_count = 0;
			line_char_count = 0;
			for (const char* c = text; *c != '\0'; c++) {
				if (*c == '\n' || (line_char_count >= MapTooltip::MAX_CHARS_PER_LINE && *c == ' ')) {
					starty += (14.0f * scale);
					glRasterPos2f(startx, starty);
					line_char_count = 0;
				}
				char_count++;
				line_char_count++;

			if (tooltip.ellipsis && char_count >= MapTooltip::MAX_CHARS) {
					glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, '.');
					if (char_count >= (MapTooltip::MAX_CHARS + 2)) {
						break;
					}
				} else if (!iscntrl(*c)) {
					glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
				}
			}
		}
	}
}

void MapDrawer::DrawLight() {
	// draw in-game light
	light_drawer->draw(start_x, start_y, end_x, end_y, view_scroll_x, view_scroll_y);
}

void MapDrawer::MakeTooltip(int screenx, int screeny, const std::string& text, uint8_t r, uint8_t g, uint8_t b, bool truncateText) {
	if (text.empty()) {
		return;
	}

	tooltips.emplace_back(screenx, screeny, text, r, g, b);
	if (!truncateText) {
		tooltips.back().ellipsis = false;
	}
	tooltips.back().checkLineEnding();
}

void MapDrawer::AddLight(TileLocation* location) {
	if (!options.isDrawLight() || !location) {
		return;
	}

	auto tile = location->get();
	if (!tile) {
		return;
	}

	auto position = location->getPosition();

	// Solid ground blocks the view of floors below it, so lights on lower
	// floors are only visible through openings (tiles with no ground).
	if (tile->hasGround()) {
		light_drawer->addOpaqueGround(position.x, position.y, position.z);
	}

	if (tile->ground) {
		if (tile->ground->hasLight()) {
			light_drawer->addLight(position.x, position.y, position.z, tile->ground->getLight());
		}
	}

	bool hidden = options.hide_items_when_zoomed && zoom > 10.f;
	if (!hidden && !tile->items.empty()) {
		for (auto item : tile->items) {
			if (item->hasLight()) {
				light_drawer->addLight(position.x, position.y, position.z, item->getLight());
			}
		}
	}
}

void MapDrawer::getColor(Brush* brush, const Position& position, uint8_t& r, uint8_t& g, uint8_t& b) {
	if (brush->canDraw(&editor.map, position)) {
		r = 0x00;
		g = 0x00, b = 0xff;
	} else {
		r = 0xff;
		g = 0x00, b = 0x00;
	}
}

void MapDrawer::TakeScreenshot(uint8_t* screenshot_buffer) {
	glFinish(); // Wait for the operation to finish

	glPixelStorei(GL_PACK_ALIGNMENT, 1); // 1 byte alignment

	for (int i = 0; i < screensize_y; ++i) {
		glReadPixels(0, screensize_y - i, screensize_x, 1, GL_RGB, GL_UNSIGNED_BYTE, (GLubyte*)(screenshot_buffer) + 3 * screensize_x * i);
	}
}

void MapDrawer::glBlitTexture(int sx, int sy, int texture_number, int red, int green, int blue, int alpha) {
	if (texture_number != 0) {
		glBindTexture(GL_TEXTURE_2D, texture_number);
		glColor4ub(uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha));
		glBegin(GL_QUADS);
		glTexCoord2f(0.f, 0.f);
		glVertex2f(sx, sy);
		glTexCoord2f(1.f, 0.f);
		glVertex2f(sx + TileSize, sy);
		glTexCoord2f(1.f, 1.f);
		glVertex2f(sx + TileSize, sy + TileSize);
		glTexCoord2f(0.f, 1.f);
		glVertex2f(sx, sy + TileSize);
		glEnd();
	}
}

void MapDrawer::glBlitSquare(int sx, int sy, int red, int green, int blue, int alpha, int size) {
	if (size == 0) {
		size = TileSize;
	}

	glColor4ub(uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha));
	glBegin(GL_QUADS);
	glVertex2f(sx, sy);
	glVertex2f(sx + size, sy);
	glVertex2f(sx + size, sy + size);
	glVertex2f(sx, sy + size);
	glEnd();
}

void MapDrawer::glColor(wxColor color) {
	glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
}

void MapDrawer::glColor(MapDrawer::BrushColor color) {
	switch (color) {
		case COLOR_BRUSH:
			glColor4ub(
				g_settings.getInteger(Config::CURSOR_RED),
				g_settings.getInteger(Config::CURSOR_GREEN),
				g_settings.getInteger(Config::CURSOR_BLUE),
				g_settings.getInteger(Config::CURSOR_ALPHA)
			);
			break;

		case COLOR_FLAG_BRUSH:
		case COLOR_HOUSE_BRUSH:
			glColor4ub(
				g_settings.getInteger(Config::CURSOR_ALT_RED),
				g_settings.getInteger(Config::CURSOR_ALT_GREEN),
				g_settings.getInteger(Config::CURSOR_ALT_BLUE),
				g_settings.getInteger(Config::CURSOR_ALT_ALPHA)
			);
			break;

		case COLOR_SPAWN_BRUSH:
			glColor4ub(166, 0, 0, 128);
			break;

		case COLOR_ERASER:
			glColor4ub(166, 0, 0, 128);
			break;

		case COLOR_VALID:
			glColor4ub(0, 166, 0, 128);
			break;

		case COLOR_INVALID:
			glColor4ub(166, 0, 0, 128);
			break;

		default:
			glColor4ub(255, 255, 255, 128);
			break;
	}
}

void MapDrawer::glColorCheck(Brush* brush, const Position& pos) {
	if (brush->canDraw(&editor.map, pos)) {
		glColor(COLOR_VALID);
	} else {
		glColor(COLOR_INVALID);
	}
}

void MapDrawer::drawRect(int x, int y, int w, int h, const wxColor& color, int width) {
	glLineWidth(width);
	glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
	glBegin(GL_LINE_STRIP);
	glVertex2f(x, y);
	glVertex2f(x + w, y);
	glVertex2f(x + w, y + h);
	glVertex2f(x, y + h);
	glVertex2f(x, y);
	glEnd();
}

void MapDrawer::drawFilledRect(int x, int y, int w, int h, const wxColor& color) {
	glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
	glBegin(GL_QUADS);
	glVertex2f(x, y);
	glVertex2f(x + w, y);
	glVertex2f(x + w, y + h);
	glVertex2f(x, y + h);
	glEnd();
}

void MapDrawer::drawSessionBlackTile(int map_x, int map_y, int map_z, bool only_colors) {
	int offset;
	if (map_z <= GROUND_LAYER) {
		offset = (GROUND_LAYER - map_z) * TileSize;
	} else {
		offset = TileSize * (floor - map_z);
	}

	const int draw_x = ((map_x * TileSize) - view_scroll_x) - offset;
	const int draw_y = ((map_y * TileSize) - view_scroll_y) - offset;

	if (!only_colors) {
		glDisable(GL_TEXTURE_2D);
	}
	drawFilledRect(draw_x, draw_y, TileSize, TileSize, wxColour(0, 0, 0, 255));
	if (!only_colors) {
		glEnable(GL_TEXTURE_2D);
	}
}

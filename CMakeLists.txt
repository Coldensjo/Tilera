cmake_minimum_required(VERSION 3.16)
project(Tilera CXX)

# Linux/native build. The canonical Windows build is the pair of standalone solutions
# vcproj/Editor/Editor.sln and vcproj/MapServer/MapServer.sln (see AGENTS.md).
# This CMake build targets Linux using system packages (apt) instead of vcpkg.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Outputs land in the repo root, matching the Windows build layout.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

find_package(PkgConfig REQUIRED)
find_package(wxWidgets REQUIRED COMPONENTS gl aui html net core base)
include(${wxWidgets_USE_FILE})

find_package(OpenGL REQUIRED)
find_package(Boost REQUIRED COMPONENTS thread)
find_package(Threads REQUIRED)

pkg_check_modules(GLUT REQUIRED glut)
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
pkg_check_modules(TOMLPLUSPLUS REQUIRED tomlplusplus)
pkg_check_modules(FMT REQUIRED fmt)

set(RME_COMMON_SOURCES
	source/about_window.cpp
	source/action.cpp
	source/add_item_window.cpp
	source/add_tileset_window.cpp
	source/add_to_tileset_window.cpp
	source/application.cpp
	source/artprovider.cpp
	source/basemap.cpp
	source/browse_tile_window.cpp
	source/brush.cpp
	source/brush_edit.cpp
	source/brush_tables.cpp
	source/carpet_brush.cpp
	source/client_version.cpp
	source/comments_window.cpp
	source/common.cpp
	source/common_windows.cpp
	source/complexitem.cpp
	source/container_properties_window.cpp
	source/copybuffer.cpp
	source/creature_brush.cpp
	source/creature.cpp
	source/creatures.cpp
	source/dat_debug_view.cpp
	source/dcbutton.cpp
	source/doodad_brush.cpp
	source/edit_brush_window.cpp
	source/editor.cpp
	source/editor_tabs.cpp
	source/edit_tileset_window.cpp
	source/eraser_brush.cpp
	source/extension.cpp
	source/extension_window.cpp
	source/filehandle.cpp
	source/find_border_window.cpp
	source/find_brush_window.cpp
	source/find_item_window.cpp
	source/graphics.cpp
	source/ground_brush.cpp
	source/gui.cpp
	source/house_brush.cpp
	source/house.cpp
	source/house_exit_brush.cpp
	source/iomap.cpp
	source/iomap_otbm.cpp
	source/item_attributes.cpp
	source/item.cpp
	source/items.cpp
	source/json/json_spirit_reader.cpp
	source/json/json_spirit_value.cpp
	source/json/json_spirit_writer.cpp
	source/light_drawer.cpp
	source/live_action.cpp
	source/live_assets.cpp
	source/live_client.cpp
	source/live_item_blocklist.cpp
	source/live_map_backup.cpp
	source/live_peer.cpp
	source/live_server.cpp
	source/live_socket.cpp
	source/live_tab.cpp
	source/live_update.cpp
	source/main_menubar.cpp
	source/main_toolbar.cpp
	source/map_comment.cpp
	source/map.cpp
	source/map_display.cpp
	source/map_drawer.cpp
	source/map_region.cpp
	source/map_tab.cpp
	source/map_view_state.cpp
	source/map_window.cpp
	source/materials.cpp
	source/minimap_window.cpp
	source/mt_rand.cpp
	source/net_connection.cpp
	source/numbertextctrl.cpp
	source/old_properties_window.cpp
	source/palette_brushlist.cpp
	source/palette_button.cpp
	source/palette_common.cpp
	source/palette_creature.cpp
	source/palette_house.cpp
	source/palette_window.cpp
	source/pngfiles.cpp
	source/positionctrl.cpp
	source/preferences.cpp
	source/process_com.cpp
	source/properties_window.cpp
	source/raw_brush.cpp
	source/replace_ground_window.cpp
	source/replace_items_window.cpp
	source/replace_wall_window.cpp
	source/result_window.cpp
	source/selection.cpp
	source/settings.cpp
	source/spawn_brush.cpp
	source/spawn.cpp
	source/table_brush.cpp
	source/theme.cpp
	source/tile.cpp
	source/tileset.cpp
	source/tileset_edit.cpp
	source/tileset_window.cpp
	source/tomlnode.cpp
	source/tomlutil.cpp
	source/town.cpp
	source/wall_brush.cpp
	source/wall_creator_window.cpp
	source/welcome_dialog.cpp
	source/ext/pugixml.cpp
)

function(rme_configure_target target_name)
	target_include_directories(${target_name} PRIVATE source ${LIBARCHIVE_INCLUDE_DIRS} ${TOMLPLUSPLUS_INCLUDE_DIRS} ${FMT_INCLUDE_DIRS})
	target_compile_definitions(${target_name} PRIVATE ${wxWidgets_DEFINITIONS} OTGZ_SUPPORT)
	target_compile_options(${target_name} PRIVATE ${wxWidgets_CXX_FLAGS} ${LIBARCHIVE_CFLAGS_OTHER})
	target_link_libraries(${target_name} PRIVATE
		${wxWidgets_LIBRARIES}
		OpenGL::GL
		${GLUT_LIBRARIES}
		${LIBARCHIVE_LIBRARIES}
		${FMT_LIBRARIES}
		Boost::boost
		Boost::thread
		Threads::Threads
	)
	target_link_directories(${target_name} PRIVATE ${GLUT_LIBRARY_DIRS} ${LIBARCHIVE_LIBRARY_DIRS} ${FMT_LIBRARY_DIRS})
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		target_compile_definitions(${target_name} PRIVATE _DEBUG __DEBUG__)
	else()
		target_compile_definitions(${target_name} PRIVATE NDEBUG __RELEASE__)
	endif()
endfunction()

add_executable(Editor ${RME_COMMON_SOURCES})
rme_configure_target(Editor)

add_executable(MapServer ${RME_COMMON_SOURCES} source/live_server_main.cpp)
target_compile_definitions(MapServer PRIVATE __LIVE_SERVER__)
rme_configure_target(MapServer)

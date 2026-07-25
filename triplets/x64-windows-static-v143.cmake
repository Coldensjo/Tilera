# Static x64 triplet pinned to the MSVC v143 toolset.
#
# Same as the built-in x64-windows-static, but forces VCPKG_PLATFORM_TOOLSET so
# vcpkg compiles dependencies with the *same* toolset the .vcxproj files declare
# (<PlatformToolset>v143</PlatformToolset>).
#
# Without this pin, vcpkg uses the newest installed compiler. On a machine that
# also has a newer VS, wxWidgets ends up referencing STL helpers (e.g.
# __std_find_last_not_of_trivial_pos_2) that only exist in the newer libcpmt.lib,
# and the static link against v143 fails with LNK2001. The dynamic build never
# hit this because the redistributable msvcp140.dll resolved them at runtime.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v143)

cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(NativeQuestHook LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# An .asi: Ultimate ASI Loader (the dinput8.dll beside the game) loads every one it finds.
add_library(NativeQuestHook SHARED src/main.cpp src/engine.cpp src/listener.cpp src/log.cpp)
set_target_properties(NativeQuestHook PROPERTIES SUFFIX ".asi")
target_compile_options(NativeQuestHook PRIVATE /W4 /utf-8 /permissive-)
target_compile_definitions(NativeQuestHook PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

# Each build installs the .asi and the script mod into the game
set(W3_GAME_DIR "F:/Steam/steamapps/common/The Witcher 3" CACHE PATH "The game's folder")
set(NQH_MOD "${CMAKE_CURRENT_SOURCE_DIR}/mod/modNativeQuestHook")
add_custom_command(TARGET NativeQuestHook POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${W3_GAME_DIR}/mods/modNativeQuestHook/content/scripts"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${NQH_MOD}" "${W3_GAME_DIR}/mods/modNativeQuestHook"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:NativeQuestHook>" "${W3_GAME_DIR}/bin/x64_dx12/NativeQuestHook.asi"
	COMMENT "Installing NativeQuestHook.asi and modNativeQuestHook")

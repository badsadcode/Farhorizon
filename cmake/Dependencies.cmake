# Pinned external libraries. Every entry must also be listed in docs/DEPENDENCIES.md.
include(FetchContent)

find_package(Vulkan REQUIRED)   # headers + loader only; we write our own renderer

if(FH_SYSTEM_SDL3)
  find_package(SDL3 REQUIRED CONFIG)
else()
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.8
    GIT_SHALLOW    TRUE
    SYSTEM)
  FetchContent_MakeAvailable(SDL3)
endif()

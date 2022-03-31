# LiveTraffic - Set up to be used in the provided docker environment to build lin, mac, and win

cmake_minimum_required(VERSION 3.16)
project(LiveTraffic
        VERSION 3.0.0
        DESCRIPTION "LiveTraffic X-Plane plugin")

# Provide compile macros from the above project version definition
add_compile_definitions(
    LIVETRAFFIC_VERSION_BETA=0
    LIVETRAFFIC_VER_MAJOR=${PROJECT_VERSION_MAJOR}
    LIVETRAFFIC_VER_MINOR=${PROJECT_VERSION_MINOR}
    LIVETRAFFIC_VER_PATCH=${PROJECT_VERSION_PATCH}
)

################################################################################
# Target Systems
################################################################################

# Windows: Target Windows 7.0 and later
if (WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601)
    if (NOT DEFINED ENV{platform})
        set(ENV{platform} "win")
    endif()
elseif(APPLE)
    add_compile_options(-mmacosx-version-min=10.11)
    add_link_options(-mmacosx-version-min=10.11)
endif()

################################################################################
# C++ Standard required
################################################################################

set(CMAKE_CXX_STANDARD 17)
set_property(GLOBAL PROPERTY CXX_STANDARD_REQUIRED 17)
set_property(GLOBAL PROPERTY CXX_STANDARD 17)

################################################################################
# Compile Options
################################################################################

# Enable all X-Plane SDK APIs up to the newest version.
add_compile_definitions(XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1)

# Define platform macros.
add_compile_definitions(APL=$<BOOL:${APPLE}> IBM=$<BOOL:${WIN32}> LIN=$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>)

# Enable stricter warnings and then disable some we are not interested in.
if (MSVC)
    # Deprecation warning: once is enough / provide me with actual __cplusplus
    add_compile_options(/wo4996 /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wshadow -Wfloat-equal -Wextra)
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 9.0 AND NOT APPLE)
        add_compile_options(-Wno-stringop-truncation)
    endif()

    # Force-enable exception support. This is most likely redundant, although for C
    # code the default is the opposite. Since we are mixing C++ and C libraries,
    # safer to set it on?
    add_compile_options(-fexceptions)

    # Makes symbols non-exported by default.
    add_compile_options(-fvisibility=hidden)
endif()

# Debug vs Release build
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    add_compile_definitions(DEBUG=1)
    if (MSVC)
        add_compile_options(/Zi)
    else()
        add_compile_options(-O0 -g)
    endif()
else()
    add_compile_definitions(NDEBUG=1)
    if(MSVC)
        # Use highest optimization level in Release builds
        add_compile_options(/GL)
    elseif(APPLE)
        add_compile_options(-O3 -fPIC)
    elseif (UNIX OR MINGW)
        # Use position-independent code and highest optimization level (FPS!).
        add_compile_options(-O3 -fPIC)
        # Strip symbols during linking
        add_link_options(-s)
    endif()
endif()

# Mingw Threads
if (MINGW)
    option(MINGW_STDTHREADS_GENERATE_STDHEADERS "" ON)
    add_subdirectory(Lib/mingw-std-threads)
endif()

# Set include directories used by our code and dependencies.
include_directories("${CMAKE_SOURCE_DIR}/Include")
include_directories("${CMAKE_SOURCE_DIR}/Lib/base64")
include_directories("${CMAKE_SOURCE_DIR}/Lib/OpenGL")
include_directories("${CMAKE_SOURCE_DIR}/Lib/parson")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPMP2/XPMP2.framework/Versions/Current/Headers")
include_directories("${CMAKE_SOURCE_DIR}/Lib/LTAPI")
include_directories("${CMAKE_SOURCE_DIR}/Lib/SDK/CHeaders/XPLM")
include_directories("${CMAKE_SOURCE_DIR}/Lib/ImGui")
include_directories("${CMAKE_SOURCE_DIR}/Lib/ImGui/misc/cpp")
include_directories("${CMAKE_SOURCE_DIR}/Lib/ImgWindow")
include_directories("${CMAKE_SOURCE_DIR}/Lib/Font")

################################################################################
# Source groups
################################################################################
set(Header_Files
    Include/ACInfoWnd.h
    Include/ACTable.h
    Include/Constants.h
    Include/CoordCalc.h
    Include/DataRefs.h
    Include/InfoListWnd.h
    Include/LiveTraffic.h
    Include/LTADSBEx.h
    Include/LTAircraft.h
    Include/LTApt.h
    Include/LTChannel.h
    Include/LTFlightData.h
    Include/LTForeFlight.h
    Include/LTFSCharter.h
    Include/LTImgWindow.h
    Include/LTOpenGlider.h
    Include/LTOpenSky.h
    Include/LTRealTraffic.h
    Include/LTWeather.h
    Include/Network.h
    Include/SettingsUI.h
    Include/TextIO.h
    Lib/base64/base64.h
    Lib/parson/parson.h
    Lib/LTAPI/LTAPI.h
    Lib/ImGui/imconfig.h
    Lib/ImGui/imgui_internal.h
    Lib/ImGui/imgui.h
    Lib/ImGui/misc/cpp/imgui_stdlib.h
    Lib/ImGui/imstb_rectpack.h
    Lib/ImGui/imstb_textedit.h
    Lib/ImGui/imstb_truetype.h
    Lib/ImgWindow/ImgFontAtlas.h
    Lib/ImgWindow/ImgWindow.h
    Lib/ImgWindow/SystemGL.h
    Lib/Font/IconsFontAwesome5.h
    Lib/Font/fa-solid-900.inc
    Lib/XPMP2/XPMP2.framework/Versions/Current/Headers/XPCAircraft.h
    Lib/XPMP2/XPMP2.framework/Versions/Current/Headers/XPMPMultiplayer.h
    Lib/XPMP2/XPMP2.framework/Versions/Current/Headers/XPMPPlaneRenderer.h
)
source_group("Header Files" FILES ${Header_Files})

set(Source_Files
    Src/ACInfoWnd.cpp
    Src/ACTable.cpp
    Src/CoordCalc.cpp
    Src/DataRefs.cpp
    Src/InfoListWnd.cpp
    Src/LiveTraffic.cpp
    Src/LTADSBEx.cpp
    Src/LTAircraft.cpp
    Src/LTApt.cpp
    Src/LTChannel.cpp
    Src/LTFlightData.cpp
    Src/LTForeFlight.cpp
    Src/LTFSCharter.cpp
    Src/LTImgWindow.cpp
    Src/LTMain.cpp
    Src/LTOpenGlider.cpp
    Src/LTOpenSky.cpp
    Src/LTRealTraffic.cpp
    Src/LTVersion.cpp
    Src/LTWeather.cpp
    Src/Network.cpp
    Src/SettingsUI.cpp
    Src/TextIO.cpp
    Lib/base64/base64.c
    Lib/parson/parson.c
    Lib/ImGui/imgui_draw.cpp
    Lib/ImGui/imgui_widgets.cpp
    Lib/ImGui/imgui.cpp
    Lib/ImGui/misc/cpp/imgui_stdlib.cpp
    Lib/ImgWindow/ImgFontAtlas.cpp
    Lib/ImgWindow/ImgWindow.cpp
)
if (WIN32)
    list(APPEND Source_Files "LiveTraffic.rc")
endif()
source_group("Source Files" FILES ${Source_Files})

set(ALL_FILES  ${Header_Files} ${Source_Files})

add_library(LiveTraffic MODULE ${ALL_FILES})

# ImGui files do a lot of comparisons on floats
if (NOT MSVC)
    set_source_files_properties(
        Lib/ImGui/imgui_draw.cpp
        Lib/ImGui/imgui_widgets.cpp
        Lib/ImGui/imgui.cpp
        Lib/ImgWindow/ImgWindow.cpp
    PROPERTIES
        COMPILE_FLAGS -Wno-float-equal)
endif()

################################################################################
# Define pre-compiled header
################################################################################

target_precompile_headers(LiveTraffic PUBLIC Include/LiveTraffic.h)

# Exclude all the non-core-LiveTraffic modules from the pch
set_source_files_properties(
    Lib/base64/base64.c
    Lib/parson/parson.c
    Lib/ImGui/imgui_draw.cpp
    Lib/ImGui/imgui_widgets.cpp
    Lib/ImGui/imgui.cpp
    Lib/ImGui/misc/cpp/imgui_stdlib.cpp
    Lib/ImgWindow/ImgFontAtlas.cpp
    Lib/ImgWindow/ImgWindow.cpp
PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON)

  
################################################################################
# Link Libraries
################################################################################

# Specify library search locations.
if (APPLE)
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPMP2")
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/SDK/Libraries/Mac")
elseif (UNIX)
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPMP2/$ENV{platform}")
elseif (WIN32)
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/CURL")
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPMP2/win")
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/SDK/Libraries/Win")
endif ()

# Link the XPMP2 library
find_library(XPMP2_LIBRARY XPMP2 REQUIRED)
message ("   XPMP2_LIBRARY = ${XPMP2_LIBRARY}")
target_link_libraries(LiveTraffic ${XPMP2_LIBRARY})

# MSVC doesn't like to find a file named "libcurl.lib", so we just tell it where to look
if (MSVC)
    set(CURL_INCLUDE_DIR        "lib/CURL")
    set(CURL_LIBRARY_RELEASE    "${CMAKE_CURRENT_SOURCE_DIR}/Lib/CURL/libcurl.lib")
    set(CURL_LIBRARY_DEBUG      "${CMAKE_CURRENT_SOURCE_DIR}/Lib/CURL/libcurl.lib")
endif()

# Link libcurl
find_package(CURL REQUIRED)
message("   CURL_INCLUDE_DIRS    = ${CURL_INCLUDE_DIRS}")
message("   CURL_LIBRARIES       = ${CURL_LIBRARIES}")
if (WIN32)
    # We have built a static up-to-date version of CURL just for ourselves, compile/link it statically
    add_compile_definitions(CURL_STATICLIB)
endif ()
include_directories( ${CURL_INCLUDE_DIRS} )
target_link_libraries( LiveTraffic ${CURL_LIBRARIES} )

# Link OpenGL and OpenAL related libraries.
set (OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)  # apt install freeglut3-dev
include_directories( ${OpenGL_INCLUDE_DIRS} )
target_link_libraries( LiveTraffic ${OpenGL_LIBRARIES} )

# Link X-Plane plugin system libraries. They are only provided for OS X and Windows.
if (WIN32 OR APPLE)
    find_library(XPLM_LIBRARY NAMES XPLM XPLM_64.lib)
    target_link_libraries(LiveTraffic ${XPLM_LIBRARY} )
endif ()


if (WIN32)
    # Link platform-specific libraries especially for networking
    target_link_libraries(LiveTraffic ws2_32.lib iphlpapi wldap32.lib advapi32.lib crypt32.lib opengl32)
    if (MINGW)
        # Include MingW threads
        target_link_libraries(LiveTraffic mingw_stdthreads)
        # When cross-compiling we link the standard libraries statically
        target_link_options(LiveTraffic PRIVATE -static-libgcc -static-libstdc++)
    endif()
elseif (APPLE)
    # Link OS X core system libraries.
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation REQUIRED)
    find_library(Cocoa_LIBRARY Cocoa REQUIRED)
    find_library(Security_LIBRARY Security REQUIRED)
    find_library(GSS_LIBRARY GSS REQUIRED)
    find_library(OpenGL_LIBRARY OpenGL REQUIRED)
    target_link_libraries(LiveTraffic
        ${CORE_FOUNDATION_LIBRARY}
        ${Cocoa_LIBRARY}
        ${Security_LIBRARY}
        ${GSS_LIBRARY}
        ${OpenGL_LIBRARY}
    )

    # Restrict set of symbols exported from the plugin to the ones required by XPLM:
    target_link_libraries(LiveTraffic "-exported_symbols_list ${CMAKE_SOURCE_DIR}/Src/LiveTraffic.sym_mac")
elseif (UNIX)
    # Link library for dynamic loading of shared objects on UNIX systems.
    find_library(DL_LIBRARY dl REQUIRED)
    # Threads
    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)
    target_link_libraries(LiveTraffic ${DL_LIBRARY} Threads::Threads)
    # Specify additional runtime search paths for dynamically-linked libraries.
    # Restrict set of symbols exported from the plugin to the ones required by XPLM:
    target_link_libraries(LiveTraffic -Wl,--version-script -Wl,${CMAKE_SOURCE_DIR}/Src/LiveTraffic.sym)
endif ()

# Target directory and file name
if (WIN32)
   set_target_properties(LiveTraffic PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/win_x64")
elseif (APPLE)
   set_target_properties(LiveTraffic PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mac_x64")
elseif (UNIX)
   set_target_properties(LiveTraffic PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lin_x64")
endif ()

set_target_properties(LiveTraffic
    PROPERTIES
    PREFIX ""
    OUTPUT_NAME "LiveTraffic"
    SUFFIX ".xpl"
)

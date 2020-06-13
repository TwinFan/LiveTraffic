# LiveTraffic - Set up to be used in the provided docker environment to build lin and mac
#               mac does currently compile only, but fails linking.
#               Set up to be used in a Visual Studio environment to build win (File > Open > Folder, then VS recognized the CMAKE configuration)

cmake_minimum_required(VERSION 3.9)
project(LiveTraffic VERSION 1.22 DESCRIPTION "LiveTraffic X-Plane plugin" LANGUAGES C CXX)

# By default, use optimized release configuration.
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif ()

# Set include directories used by our code and dependencies.
include_directories("${CMAKE_SOURCE_DIR}/Include")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPMP2/XPMP2.framework/Versions/1.0/Headers")
include_directories("${CMAKE_SOURCE_DIR}/Lib/LTAPI")
include_directories("${CMAKE_SOURCE_DIR}/Lib/SDK/CHeaders/Widgets")
include_directories("${CMAKE_SOURCE_DIR}/Lib/SDK/CHeaders/XPLM")
include_directories("${CMAKE_SOURCE_DIR}/Lib/ImGui")
include_directories("${CMAKE_SOURCE_DIR}/Lib/ImGui/misc/cpp")
include_directories("${CMAKE_SOURCE_DIR}/Lib/ImgWindow")
include_directories("${CMAKE_SOURCE_DIR}/Lib/Font")

# Enable all necessary X-Plane SDK APIs
add_definitions(-DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1)

# Define platform macros.
add_definitions(-DAPL=$<BOOL:${APPLE}> -DIBM=$<BOOL:${WIN32}> -DLIN=$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>)

if (UNIX OR APPLE)
    # Force-enable exception support. This is most likely redundant, although for C
    # code the default is the opposite. Since we are mixing C++ and C libraries,
    # safer to set it on?
    add_compile_options(-fexceptions -fpermissive)

    # On UNIX systems this makes symbols non-exported by default. On Windows this
    # option is simply ignored, since symbol visibility works differently there.
    add_compile_options(-fvisibility=hidden)

    # Enable stricter warnings and then disable some we are not interested in.
    add_compile_options(-Wall -Wshadow -Wfloat-equal -Wextra)
    add_compile_options(-Wno-unused)

    # Always use position-independent code and highest optimization level (FPS!).
    add_compile_options(-O3 -fPIC)
elseif(WIN32)
    # Deprecation warning: once is enough
    add_compile_options(/wo4996)
endif()

################################################################################
# Source groups
################################################################################
set(Header_Files
    Include/ACInfoWnd.h
    Include/Constants.h
    Include/CoordCalc.h
    Include/DataRefs.h
    Include/LiveTraffic.h
    Include/LTADSBEx.h
    Include/LTAircraft.h
    Include/LTApt.h
    Include/LTChannel.h
    Include/LTFlightData.h
    Include/LTForeFlight.h
    Include/LTImgWindow.h
    Include/LTOpenSky.h
    Include/LTRealTraffic.h
    Include/Network.h
    Include/parson.h
    Include/SettingsUI.h
    Include/TextIO.h
    Include/TFWidgets.h
    Include/XPCompatibility.h
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
    Lib/XPMP2/XPMP2.framework/Versions/1.0/Headers/XPCAircraft.h
    Lib/XPMP2/XPMP2.framework/Versions/1.0/Headers/XPMPMultiplayer.h
    Lib/XPMP2/XPMP2.framework/Versions/1.0/Headers/XPMPPlaneRenderer.h
)
source_group("Header Files" FILES ${Header_Files})

set(Source_Files
    Src/ACInfoWnd.cpp
    Src/CoordCalc.cpp
    Src/DataRefs.cpp
    Src/LiveTraffic.cpp
    Src/LTADSBEx.cpp
    Src/LTAircraft.cpp
    Src/LTApt.cpp
    Src/LTChannel.cpp
    Src/LTFlightData.cpp
    Src/LTForeFlight.cpp
    Src/LTImgWindow.cpp
    Src/LTMain.cpp
    Src/LTOpenSky.cpp
    Src/LTRealTraffic.cpp
    Src/LTVersion.cpp
    Src/Network.cpp
    Src/parson.c
    Src/SettingsUI.cpp
    Src/TextIO.cpp
    Src/TFWidgets.cpp
    Src/XPCompatibility.cpp
    Lib/ImGui/imgui_draw.cpp
    Lib/ImGui/imgui_widgets.cpp
    Lib/ImGui/imgui.cpp
    Lib/ImGui/misc/cpp/imgui_stdlib.cpp
    Lib/ImgWindow/ImgFontAtlas.cpp
    Lib/ImgWindow/ImgWindow.cpp
)
source_group("Source Files" FILES ${Source_Files})

set(ALL_FILES  ${Header_Files} ${Source_Files})

add_library(LiveTraffic MODULE ${ALL_FILES})

# ImGui files do a lot of comparisons on floats
set_source_files_properties(Lib/ImGui/imgui_draw.cpp PROPERTIES COMPILE_FLAGS -Wno-float-equal)
set_source_files_properties(Lib/ImGui/imgui_widgets.cpp PROPERTIES COMPILE_FLAGS -Wno-float-equal)
set_source_files_properties(Lib/ImGui/imgui.cpp PROPERTIES COMPILE_FLAGS -Wno-float-equal)

################################################################################
# Link Libraries
################################################################################

# Specify library search locations.
if (APPLE)
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/CURL")
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPMP2")
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/SDK/Libraries/Mac")
elseif (UNIX)
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPMP2")
elseif (WIN32)
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/CURL")
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPMP2")
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/SDK/Libraries/Win")
endif ()

# Find the XPMP2 library
if (APPLE)
    find_library(XPMP2_LIBRARY NAMES XPMP2)
elseif (UNIX)
    find_library(XPMP2_LIBRARY NAMES libXPMP2.a)
elseif (WIN32)
    find_library(XPMP2_LIBRARY NAMES XPMP2.lib)
endif ()
target_link_libraries(LiveTraffic ${XPMP2_LIBRARY})

# CURL Library
if (WIN32)
    find_library(CURL_LIBRARIES NAMES libcurl.lib)
    include_directories("${CMAKE_SOURCE_DIR}/Lib/CURL/libcurl.framework/Versions/Release-7.65.3/Headers")
    add_definitions(-DCURL_STATICLIB)
else()
    find_package(CURL REQUIRED)  # sudo apt-get install curl
    include_directories( ${CURL_INCLUDE_DIRS} )
endif()
target_link_libraries( LiveTraffic ${CURL_LIBRARIES} )

# Link OpenGL and OpenAL related libraries.
set (OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)  # apt install freeglut3-dev
if ( OpenGL_FOUND )
    include_directories( ${OpenGL_INCLUDE_DIRS} )
    target_link_libraries( LiveTraffic ${OpenGL_LIBRARIES} )
endif( OpenGL_FOUND )

# Link X-Plane plugin system libraries. They are only provided for OS X and Windows.
if (WIN32 OR APPLE)
    find_library(XPLM_LIBRARY NAMES XPLM XPLM_64.lib)
	find_library(XPWIDGETS_LIBRARY NAMES XPWidgets XPWidgets_64.lib)
    target_link_libraries(LiveTraffic ${XPLM_LIBRARY} ${XPWIDGETS_LIBRARY})
endif ()


# Link library for dynamic loading of shared objects on UNIX systems.
if (UNIX)
    find_library(DL_LIBRARY dl)
    target_link_libraries(LiveTraffic ${DL_LIBRARY})
endif ()

# Link OS X core system libraries.
if (APPLE)
    find_library(APPLICATION_SERVICES ApplicationServices)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
    find_library(SECURITY_LIBRARY Security)
    find_library(GSS_LIBRARY GSS)
    find_library(Kerberos5_LIBRARY libgssapi_krb5.tbd)
    find_library(Cocoa_LIBRARY Cocoa)

    target_link_libraries(LiveTraffic
        ${APPLICATION_SERVICES}
        ${CORE_FOUNDATION_LIBRARY}
        ${SECURITY_LIBRARY}
        ${GSS_LIBRARY}
        ${Kerberos5_LIBRARY}
        ${OpenGL_LIBRARY}
        ${Cocoa_LIBRARY}
    )
endif ()

if (WIN32)
    # Link platform-specific libraries especially for networking
    target_link_libraries(LiveTraffic ws2_32.lib wldap32.lib advapi32.lib crypt32.lib)
elseif (APPLE)
    # X-Plane supports OS X 10.10+, so this should ensure FlyWithLua can run on
    # all supported versions.
    target_compile_options(LiveTraffic PUBLIC -mmacosx-version-min=10.11)
    target_link_libraries(LiveTraffic -mmacosx-version-min=10.11)

    # Restrict set of symbols exported from the plugin. This reduces changes of
    # conflict with other plugins, in particular ones with Lua interpreter
    # embedded.
    target_link_libraries(LiveTraffic "-exported_symbols_list ${CMAKE_SOURCE_DIR}/Src/LiveTraffic.sym_mac")
elseif (UNIX)
    # Restrict set of symbols exported from the plugin. This reduces changes of
    # conflict with other plugins, in particular ones with Lua interpreter
    # embedded.
    target_link_libraries(LiveTraffic -Wl,--version-script -Wl,${CMAKE_SOURCE_DIR}/Src/LiveTraffic.sym)
endif ()

# We need C++ 17
set(CMAKE_CXX_STANDARD 17)
target_compile_features(LiveTraffic PUBLIC cxx_std_17)
set_property(TARGET LiveTraffic PROPERTY CXX_STANDARD_REQUIRED 17)
set_property(TARGET LiveTraffic PROPERTY CXX_STANDARD 17)

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

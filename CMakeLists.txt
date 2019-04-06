# LiveTraffic build script.
#

cmake_minimum_required(VERSION 3.9)
project(LiveTraffic VERSION 1.1.9 DESCRIPTION "LiveTraffic X-Plane plugin")

# By default, use optimized release configuration.
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif ()

set(CMAKE_CXX_STANDARD 17)

# Set include directories used by our code and dependencies.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Src")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/Lib/xplanemp")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPSDK301/CHeaders/Widgets")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPSDK301/CHeaders/Wrappers")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPSDK301/CHeaders/XPLM")
message("CMAKE_CURRENT_SOURCE_DIR = "(${CMAKE_CURRENT_SOURCE_DIR}))
message("CMAKE_SOURCE_DIR = "(${CMAKE_SOURCE_DIR}))

# Specify library search locations.
if (WIN32)
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPSDK301/Libraries/Win")
elseif (APPLE)
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPSDK301/Libraries/Mac")
elseif (UNIX)

endif ()

# message (STATUS "CMAKE_LIBRARY_PATH" = ${CMAKE_LIBRARY_PATH})

# Enable all X-Plane SDK APIs up to the newest version.
add_definitions(-DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1)
# Define platform macros.
add_definitions(-DAPL=$<BOOL:${APPLE}> -DIBM=$<BOOL:${WIN32}> -DLIN=$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>)

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

# X-Plane plugin


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
    Include/LTChannel.h
    Include/LTFlightData.h
    Include/LTForeFlight.h
    Include/LTOpenSky.h
    Include/LTRealTraffic.h
    Include/Network.h
    Include/parson.h
    Include/SettingsUI.h
    Include/TextIO.h
    Include/TFWidgets.h
    Include/XPCompatibility.h
    Lib/xplanemp/XPCAircraft.h
    Lib/xplanemp/XPMPMultiplayer.h
    Lib/xplanemp/XPMPPlaneRenderer.h
)
source_group("Header Files" FILES ${Header_Files})

set(Source_Files
    Src/ACInfoWnd.cpp
    Src/CoordCalc.cpp
    Src/DataRefs.cpp
    Src/LiveTraffic.cpp
    Src/LTADSBEx.cpp
    Src/LTAircraft.cpp
    Src/LTChannel.cpp
    Src/LTFlightData.cpp
    Src/LTForeFlight.cpp
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
)
source_group("Source Files" FILES ${Source_Files})

set(ALL_FILES  ${Header_Files} ${Source_Files})

add_library(LiveTraffic SHARED ${ALL_FILES})

target_compile_features(LiveTraffic PUBLIC cxx_std_17)

if (APPLE)
    # X-Plane supports OS X 10.10+, so this should ensure FlyWithLua can run on
    # all supported versions.
    target_compile_options(LiveTraffic PUBLIC -mmacosx-version-min=10.10)
    target_link_libraries(LiveTraffic -mmacosx-version-min=10.10)
endif ()

# Link OpenGL and OpenAL related libraries.
find_package(OpenGL REQUIRED)  # apt install freeglut3-dev
if ( OpenGL_FOUND )
    include_directories( ${OpenGL_INCLUDE_DIRS} )
    target_link_libraries( LiveTraffic ${OpenGL_LIBRARIES} )
endif( OpenGL_FOUND )

find_package(OpenAL REQUIRED)  # apt install libopenal-dev
if ( OpenAL_FOUND )
    include_directories( ${OpenAL_INCLUDE_DIRS} )
    target_link_libraries( LiveTraffic ${OpenAL_LIBRARIES} )
endif( OpenAL_FOUND )

find_package(GLUT REQUIRED)  # apt install freeglut3-dev
if ( GLUT_FOUND )
    include_directories( ${GLUT_INCLUDE_DIRS} )
    target_link_libraries( LiveTraffic ${GLUT_LIBRARIES} )
endif( GLUT_FOUND )

find_package(ZLIB REQUIRED)  # sudo apt-get install zlib1g-dev
if ( ZLIB_FOUND )
    include_directories( ${ZLIB_INCLUDE_DIRS} )
    target_link_libraries( LiveTraffic ${ZLIB_LIBRARIES} )
endif( ZLIB_FOUND )

find_package(PNG REQUIRED)  # sudo apt-get install libpng-dev
if ( PNG_FOUND )
    include_directories( ${PNG_INCLUDE_DIRS} )
    target_link_libraries( LiveTraffic ${PNG_LIBRARIES} )
endif( PNG_FOUND )


# ****************************************************
# This workd correctly with cmake but not with docker
find_package(CURL REQUIRED)  # sudo apt-get install curl
if ( CURL_FOUND )
   include_directories( ${CURL_INCLUDE_DIRS} )
   target_link_libraries( LiveTraffic ${CURL_LIBRARIES} )
endif( CURL_FOUND )


# ***********************************************
# This will allows docker to build but get "undefined symbol: curl_easy_perform"
# When I try to run X-Plane
# FIND_PACKAGE(CURL)
# IF(CURL_FOUND)
#   INCLUDE_DIRECTORIES(${CURL_INCLUDE_DIR})
#   SET(requiredlibs ${requiredlibs} ${CURL_LIBRARIES} )
# ELSE(CURL_FOUND)
#   MESSAGE(FATAL_ERROR "Could not find the CURL library and development files.")
# ENDIF(CURL_FOUND)


# Link X-Plane plugin system libraries. They are only provided for OS X and Windows.
if (WIN32 OR APPLE)
# if (WIN32)
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
    find_library(IOKIT_LIBRARY IOKit)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
    find_library(GLUT_LIBRARY GLUT)
    find_library(OpenGL_LIBRARY OpenGL)
    find_library(Cocoa_LIBRARY Cocoa)

    target_link_libraries(LiveTraffic ${IOKIT_LIBRARY} ${CORE_FOUNDATION_LIBRARY} ${GLUT_LIBRARY})
    target_link_libraries(LiveTraffic ${OpenGL_LIBRARY} ${Cocoa_LIBRARY})
endif ()

if (WIN32)
    # Unlike OS X and Linux we build standard libraries statically since X-Plane does not provide them.
    target_link_libraries(LiveTraffic -static-libgcc -static-libstdc++ opengl32)
    # MSYS2 for some reason always links against winpthread, so we have to work around it by statically linking the
    # entire plugin. This allows building the plugin nativaly on Windows.
    target_link_libraries(LiveTraffic -static)
elseif (APPLE)
    # Restrict set of symbols exported from the plugin. This reduces changes of
    # conflict with other plugins, in particular ones with Lua interpreter
    # embedded.
    target_link_libraries(LiveTraffic "-exported_symbols_list ${CMAKE_SOURCE_DIR}/Src/LiveTraffic.sym_mac")
	target_link_libraries(LiveTraffic "-framework XPLM -framework XPWidgets -ldl")
elseif (UNIX)
    # Specify additional runtime search laths for dynamically-linked libraries.
    # Restrict set of symbols exported from the plugin. This reduces changes of
    # conflict with other plugins, in particular ones with Lua interpreter
    # embedded.
    message(${CMAKE_CURRENT_SOURCE_DIR})
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/xplanemp")

    find_library(XPLANEMP_LIBRARY NAMES libxplanemp.a)
    target_link_libraries(LiveTraffic ${XPLANEMP_LIBRARY})

    target_link_libraries(LiveTraffic -Wl,--version-script -Wl,${CMAKE_SOURCE_DIR}/Src/LiveTraffic.sym)
endif ()

set_target_properties(LiveTraffic PROPERTIES PREFIX "")
if (WIN32)
   set_target_properties(LiveTraffic PROPERTIES OUTPUT_NAME "win")
elseif (APPLE)
   set_target_properties(LiveTraffic PROPERTIES OUTPUT_NAME "mac")
elseif (UNIX)
   set_target_properties(LiveTraffic PROPERTIES OUTPUT_NAME "lin")
endif ()
set_target_properties(LiveTraffic PROPERTIES SUFFIX ".xpl")

# set_target_properties(LiveTraffic PROPERTIES PREFIX "")
# set_target_properties(LiveTraffic PROPERTIES OUTPUT_NAME "LiveTraffic")
# set_target_properties(LiveTraffic PROPERTIES SUFFIX ".xpl")

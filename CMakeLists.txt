# LiveTraffic - Set up to be used in the provided docker environment to build lin and mac
#               Set up to be used in a Visual Studio environment to build win (File > Open > Folder, then VS recognized the CMAKE configuration)

cmake_minimum_required(VERSION 3.9)
project(LiveTraffic VERSION 1.22 DESCRIPTION "LiveTraffic X-Plane plugin" LANGUAGES C CXX)

# By default, use optimized release configuration.
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif ()

# Set include directories used by our code and dependencies.
include_directories("${CMAKE_SOURCE_DIR}/Include")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPMP2/XPMP2.framework/Headers")
include_directories("${CMAKE_SOURCE_DIR}/Lib/LTAPI")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPSDK301/CHeaders/Widgets")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPSDK301/CHeaders/Wrappers")
include_directories("${CMAKE_SOURCE_DIR}/Lib/XPSDK301/CHeaders/XPLM")

# Enable all X-Plane SDK APIs up to the newest version.
add_definitions(-DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1)

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
    Include/LTOpenSky.h
    Include/LTRealTraffic.h
    Include/Network.h
    Include/parson.h
    Include/SettingsUI.h
    Include/TextIO.h
    Include/TFWidgets.h
    Include/XPCompatibility.h
    Lib/LTAPI/LTAPI.h
    Lib/XPMP2/XPMP2.framework/Headers/XPCAircraft.h
    Lib/XPMP2/XPMP2.framework/Headers/XPMPMultiplayer.h
    Lib/XPMP2/XPMP2.framework/Headers/XPMPPlaneRenderer.h
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

add_library(LiveTraffic MODULE ${ALL_FILES})

################################################################################
# Link Libraries
################################################################################

# Specify library search locations.
if (APPLE)
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/CURL")
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/libpng")
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPMP2")
    list(APPEND CMAKE_FRAMEWORK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPSDK301/Libraries/Mac")
else ()
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPMP2")
    list(APPEND CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/XPSDK301/Libraries/Win")
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

# Link OpenGL
set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)  # apt install freeglut3-dev
include_directories( ${OpenGL_INCLUDE_DIRS} )
target_link_libraries( LiveTraffic OpenGL::GL )

# ZLIB
find_package(ZLIB REQUIRED)  # sudo apt-get install zlib1g-dev
include_directories( ${ZLIB_INCLUDE_DIRS} )
target_link_libraries( LiveTraffic ${ZLIB_LIBRARIES} )

# PNG Library
find_package(PNG REQUIRED)  # sudo apt-get install libpng-dev
include_directories( ${PNG_INCLUDE_DIRS} )
target_link_libraries( LiveTraffic ${PNG_LIBRARIES} )

# CURL Library
find_package(CURL REQUIRED)  # sudo apt-get install curl
include_directories( ${CURL_INCLUDE_DIRS} )
target_link_libraries( LiveTraffic ${CURL_LIBRARIES} )


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
    find_library(SECURITY_LIBRARY Security)
    find_library(GSS_LIBRARY GSS)
    find_library(Kerberos5_LIBRARY libgssapi_krb5.tbd)
    find_library(OpenGL_LIBRARY OpenGL)
    find_library(Cocoa_LIBRARY Cocoa)

    target_link_libraries(LiveTraffic
        ${IOKIT_LIBRARY}
        ${CORE_FOUNDATION_LIBRARY}
        ${SECURITY_LIBRARY}
        ${GSS_LIBRARY}
        ${Kerberos5_LIBRARY}
        ${OpenGL_LIBRARY}
        ${Cocoa_LIBRARY}
    )
endif ()

if (WIN32)
    # Unlike OS X and Linux we build standard libraries statically since X-Plane does not provide them.
    # target_link_libraries(LiveTraffic -static-libgcc -static-libstdc++ opengl32)
    # MSYS2 for some reason always links against winpthread, so we have to work around it by statically linking the
    # entire plugin. This allows building the plugin nativaly on Windows.
    # target_link_libraries(LiveTraffic -static)
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

/// @file       LiveTraffic.h
/// @brief      Umbrella header file, including all others; defines global functions mainly implemented in `LTMain.cpp`
/// @details    All necessary header files: Standard C, Windows, Open GL, C++, X-Plane, libxplanemp and other libs, LTAPI, LiveTraffic\n
///             To be used for header pre-processing\n
///             Set of `LTMain...` functions, which control initialization and shutdown
///             Global utility functions: path helpers, opening URLs, string helpers\n
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2020 Birger Hoppe
/// @copyright  Permission is hereby granted, free of charge, to any person obtaining a
///             copy of this software and associated documentation files (the "Software"),
///             to deal in the Software without restriction, including without limitation
///             the rights to use, copy, modify, merge, publish, distribute, sublicense,
///             and/or sell copies of the Software, and to permit persons to whom the
///             Software is furnished to do so, subject to the following conditions:\n
///             The above copyright notice and this permission notice shall be included in
///             all copies or substantial portions of the Software.\n
///             THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
///             IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
///             FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
///             AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
///             LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
///             OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
///             THE SOFTWARE.

#ifndef LiveTraffic_h
#define LiveTraffic_h

// LiveTraffic is coded against SDK 3.01 (X-Plane 11.20 and above), so XPLM200, XPLM210, XPLM300, and XPLM301 must be defined
// XP10 compatibility is achieved by not using XP11 functions directly, see XPCompatibility.cpp.
// By defining up to XPLM301 all data types are available already. There are only very very rare cases when a structure
// gets extended in XPLM300 and later. Those rare cases are covered in code (see TextIO.cpp for an example).
#if !defined(XPLM200) || !defined(XPLM210) || !defined(XPLM300) || !defined(XPLM301)
#error This is made to be compiled at least against the XPLM301 SDK (X-plane 11.20 and above)
#endif

// MARK: Includes
// Standard C
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cassert>

// Windows
#if IBM
#include <windows.h>
// we prefer std::max/min of <algorithm>
#undef max
#undef min
#endif

// Open GL
#if LIN
#include <GL/gl.h>
#elif __GNUC__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

// C++
#include <utility>
#include <string>
#include <map>
#include <vector>
#include <list>
#include <deque>
#include <thread>
#include <future>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <regex>

// X-Plane SDK
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMUtilities.h"
#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMCamera.h"
#include "XPWidgets.h"
#include "XPWidgetUtils.h"
#include "XPStandardWidgets.h"

// XP Multiplayer API
#include "XPMPMultiplayer.h"

// LTAPI Includes, this defines the bulk transfer structure
#include "LTAPI.h"

// LiveTraffic Includes
#include "Constants.h"
#include "DataRefs.h"
#include "CoordCalc.h"
#include "TextIO.h"
#include "LTAircraft.h"
#include "LTFlightData.h"
#include "TFWidgets.h"
#include "SettingsUI.h"
#include "ACInfoWnd.h"
#include "XPCompatibility.h"
#include "LTApt.h"

// LiveTraffic channels
#include "Network.h"
#include "LTChannel.h"
#include "LTForeFlight.h"
#include "LTRealTraffic.h"
#include "LTOpenSky.h"
#include "LTADSBEx.h"

// MARK: Global variables
// Global DataRef object, which also includes 'global' variables
extern DataRefs dataRefs;

//MARK: Global Control functions
bool LTMainInit ();
bool LTMainEnable ();
bool LTMainShowAircraft ();
bool LTMainTryGetAIAircraft ();
void LTMainReleaseAIAircraft ();
void LTMainHideAircraft ();
void LTMainDisable ();
void LTMainStop ();

void MenuUpdateAllItemStatus();
void HandleNewVersionAvail ();
void HandleRefPointChanged ();      ///< Handles that the local coordinate's reference point has changed

#ifdef DEBUG
void LTErrorCB (const char* msg);
#endif

// MARK: Path helpers

// deal with paths: make a full one from a relative one or keep a full path
std::string LTCalcFullPath ( const std::string path );
std::string LTCalcFullPluginPath ( const std::string path );

// if path starts with the XP system path it is removed
std::string LTRemoveXPSystemPath (std::string path );

// given a path (in XPLM notation) returns number of files in the path
// or 0 in case of errors
int LTNumFilesInPath ( const std::string path );

/// @brief Read a text line from file, no matter if ended by CRLF or LF
std::istream& safeGetline(std::istream& is, std::string& t);

// MARK: URL/Help support

void LTOpenURL  (const std::string url);
void LTOpenHelp (const std::string path);

// MARK: String/Text Functions

// change a std::string to uppercase
std::string& str_toupper(std::string& s);
// are all chars alphanumeric?
bool str_isalnum(const std::string& s);
// format timestamp
std::string ts2string (time_t t);
// limits text to m characters, replacing the last ones with ... if too long
inline std::string strAtMost(const std::string s, size_t m) {
    return s.length() <= m ? s :
    s.substr(0, m-3) + "...";
}

// trimming of string
// https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring
// trim from end of string (right)
inline std::string& rtrim(std::string& s, const char* t = WHITESPACE)
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}
// trim from beginning of string (left)
inline std::string& ltrim(std::string& s, const char* t = WHITESPACE)
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}
// trim from both ends of string (right then left)
inline std::string& trim(std::string& s, const char* t = WHITESPACE)
{
    return ltrim(rtrim(s, t), t);
}

// last word of a string
std::string str_last_word (const std::string s);
// separates string into tokens
std::vector<std::string> str_tokenize (const std::string s,
                                       const std::string tokens,
                                       bool bSkipEmpty = true);
// returns first non-empty string, and "" in case all are empty
std::string str_first_non_empty ( std::initializer_list<const std::string> l);

// push a new item to the end only if it doesn't exist yet
template< class ContainerT>
void push_back_unique(ContainerT& list, typename ContainerT::const_reference key)
{
    if ( std::find(list.cbegin(),list.cend(),key) == list.cend() )
        list.push_back(key);
}

// MARK: Other Utility Functions

// convert a color value from int to float[4]
void conv_color ( int inCol, float outCol[4] );

// verifies if one container begins with the same content as the other
// https://stackoverflow.com/questions/931827/stdstring-comparison-check-whether-string-begins-with-another-string
template<class TContainer>
bool begins_with(const TContainer& input, const TContainer& match)
{
    return input.size() >= match.size()
    && std::equal(match.cbegin(), match.cend(), input.cbegin());
}

// comparing 2 doubles for near-equality
bool dequal ( const double d1, const double d2 );

// gets latest version info from X-Plane.org
bool FetchXPlaneOrgVersion ();

// default window open mode depends on XP10/11 and VR
TFWndMode GetDefaultWndOpenMode ();

// MARK: Compiler differences

#if APL == 1 || LIN == 1
// not quite the same but close enough for our purposes
inline void strcpy_s(char * dest, size_t destsz, const char * src)
{ strncpy(dest, src, destsz); dest[destsz-1]=0; }
inline void strcat_s(char * dest, size_t destsz, const char * src)
{ strncat(dest, src, destsz - strlen(dest) - 1); }

// these simulate the VC++ version, not the C standard versions!
inline struct tm *gmtime_s(struct tm * result, const time_t * time)
{ return gmtime_r(time, result); }
inline struct tm *localtime_s(struct tm * result, const time_t * time)
{ return localtime_r(time, result); }

#endif

/// Simpler access to strcpy_s if dest is a char array (not a pointer!)
#define STRCPY_S(dest,src) strcpy_s(dest,sizeof(dest),src)
#define STRCPY_ATMOST(dest,src) strcpy_s(dest,sizeof(dest),strAtMost(src,sizeof(dest)-1).c_str())

#if APL == 1
// XCode/Linux don't provide the _s functions, not even with __STDC_WANT_LIB_EXT1__ 1
inline int strerror_s( char *buf, size_t bufsz, int errnum )
{ return strerror_r(errnum, buf, bufsz); }
#endif
#if LIN == 1
inline int strerror_s( char *buf, size_t bufsz, int errnum )
{ strcpy_s(buf,bufsz,strerror(errnum)); return 0; }
#endif

#endif /* LiveTraffic_h */

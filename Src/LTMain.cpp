/// @file       LTMain.cpp
/// @brief      Central control functions (called from LiveTraffic.cpp) as well as misc utility functions
/// @details    Set of `LTMain...` functions, which control initialization and shutdown\n
///             LoopCBAircraftMaintenance() is called every second for aircraft maintenance (create, remove)\n
///             Various utility functions for file/path access, opening URLs, string handling.\n
///             Definitions for these functions are mostly in LiveTraffic.h.
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

#include "LiveTraffic.h"

#if IBM
#include <shellapi.h>           // for ShellExecuteA
#endif

// Puts some timestamps into the log for analysis purposes
void LogTimestamps ();

//MARK: Path helpers

// construct path: if passed-in base is a full path just take it
// otherwise it is relative to XP system path
std::string LTCalcFullPath ( const std::string path )
{
    // starts already with system path? -> nothing to so
    if (begins_with<std::string>(path, dataRefs.GetXPSystemPath()))
        return path;
    
    // starts with DirSeparator or [windows] second char is a colon?
    if (dataRefs.GetDirSeparator()[0] == path[0] ||
        (path.length() >= 2 && path[1] == ':' ) )
        // just take the given path, it is a full path already
        return path;

    // otherwise it is supposingly a local path relative to XP main
    // prepend with XP system path to make it a full path:
    return dataRefs.GetXPSystemPath() + path;
}

// same as above, but relative to plugin directory
std::string LTCalcFullPluginPath ( const std::string path )
{
    std::string ret;
    
    // starts with DirSeparator or [windows] second char is a colon?
    if (dataRefs.GetDirSeparator()[0] == path[0] ||
        (path.length() >= 2 && path[1] == ':' ) )
        // just take the given path, it is a full path already
        return path;

    // otherwise it shall be a local path relative to the plugin's dir
    // otherwise it is supposingly a local path relative to XP main
    // prepend with XP system path to make it a full path:
    return dataRefs.GetLTPluginPath() + path;
}

// if path starts with the XP system path it is removed
std::string LTRemoveXPSystemPath (std::string path)
{
    if (begins_with<std::string>(path, dataRefs.GetXPSystemPath()))
        path.erase(0, dataRefs.GetXPSystemPath().length());
    return path;
}

// given a path returns number of files in the path
// or 0 in case of errors
int LTNumFilesInPath ( const std::string path )
{
    char aszFileNames[2048] = "";
    int iTotalFiles = 0;
    if ( !XPLMGetDirectoryContents(path.c_str(), 0,
                                   aszFileNames, sizeof(aszFileNames),
                                   NULL, 0,
                                   &iTotalFiles, NULL) && !iTotalFiles)
    { LOG_MSG(logERR,ERR_DIR_CONTENT,path.c_str()); }
    
    return iTotalFiles;
}

/// Read a text line, handling both Windows (CRLF) and Unix (LF) ending
/// Code makes use of the fact that in both cases LF is the terminal character.
/// So we read from file until LF (_without_ widening!).
/// In case of CRLF files there then is a trailing CR, which we just remove.
std::istream& safeGetline(std::istream& is, std::string& t)
{
    // read a line until LF
    std::getline(is, t, '\n');
    
    // if last character is CR then remove it
    if (!t.empty() && t.back() == '\r')
        t.pop_back();
    
    return is;
}

//
// MARK: URL/Help support
//

void LTOpenURL  (const std::string url)
{
#if IBM
    // Windows implementation: ShellExecuteA
    // https://docs.microsoft.com/en-us/windows/desktop/api/shellapi/nf-shellapi-shellexecutea
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif LIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    // Unix uses xdg-open, package xdg-utils, pre-installed at least on Ubuntu
    (void)system((std::string("xdg-open ") + url).c_str());
#pragma GCC diagnostic pop
#else
    // Max use standard system/open
    system((std::string("open ") + url).c_str());
    // Code that causes warning goes here
#endif
}

// just prepend the given path with the base URL an open it
void LTOpenHelp (const std::string path)
{
    LTOpenURL(std::string(HELP_URL)+path);
}


//
//MARK: String/Text Functions
//

// change a string to uppercase
std::string& str_toupper(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) -> unsigned char { return (unsigned char) toupper(c); });
    return s;
}

bool str_isalnum(const std::string& s)
{
    return std::all_of(s.cbegin(), s.cend(), [](unsigned char c){return isalnum(c);});
}

// format timestamp
std::string ts2string (time_t t)
{
    // format it nicely
    char szBuf[50];
    struct tm tm;
    gmtime_s(&tm, &t);
    strftime(szBuf,
             sizeof(szBuf) - 1,
             "%F %T",
             &tm);
    return std::string(szBuf);
}

// last word of a string
std::string str_last_word (const std::string s)
{
    std::string::size_type p = s.find_last_of(' ');
    return (p == std::string::npos ? s :    // space not found? -> entire string
            s.substr(p+1));                 // otherwise all after string (can be empty!)
}

// separates string into tokens
std::vector<std::string> str_tokenize (const std::string s,
                                       const std::string tokens,
                                       bool bSkipEmpty)
{
    std::vector<std::string> v;
 
    // find all tokens before the last
    size_t b = 0;                                   // begin
    for (size_t e = s.find_first_of(tokens);        // end
         e != std::string::npos;
         b = e+1, e = s.find_first_of(tokens, b))
    {
        if (!bSkipEmpty || e != b)
            v.emplace_back(s.substr(b, e-b));
    }
    
    // add the last one: the remainder of the string (could be empty!)
    v.emplace_back(s.substr(b));
    
    return v;
}

// returns first non-empty string, and "" in case all are empty
std::string str_first_non_empty ( std::initializer_list<const std::string> l)
{
    for (const std::string& s: l)
        if (!s.empty())
            return s;
    return "";
}

// MARK: Other Utility Functions

// comparing 2 doubles for near-equality
bool dequal ( const double d1, const double d2 )
{
    const double epsilon = 0.00001;
    return ((d1 - epsilon) < d2) &&
    ((d1 + epsilon) > d2);
}

// default window open mode depends on XP10/11 and VR
TFWndMode GetDefaultWndOpenMode ()
{
    return
    !XPLMHasFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS") ?
        TF_MODE_CLASSIC :               // XP10
    dataRefs.IsVREnabled() ?
        TF_MODE_VR : TF_MODE_FLOAT;     // XP11, VR vs. non-VR
}

//
//MARK: Callbacks
//

// flight loop callback, will be called every 5th frame while showing aircraft;
// creates/destroys aircraft by looping the flight data map
float LoopCBAircraftMaintenance (float inElapsedSinceLastCall, float, int, void*)
{
    static float elapsedSinceLastAcMaint = 0.0f;
    do {
        // *** check for new positons that require terrain altitude (Y Probes) ***
        // LiveTraffic Top Level Exception handling: catch all, reinit if something happens
        try {
            // Update cached values
            dataRefs.UpdateCachedValues();
            
            // Check if some msg window needs to show
            CheckThenShowMsgWindow();

            // handle new network data (that func has a short-cut exit if nothing to do)
            LTFlightData::AppendAllNewPos();
            
            // all the rest we do only every 2s
            elapsedSinceLastAcMaint += inElapsedSinceLastCall;
            if (elapsedSinceLastAcMaint < AC_MAINT_INTVL)
                return FLIGHT_LOOP_INTVL;          // call me again
            
            // fall through to the expensive stuff
            elapsedSinceLastAcMaint = 0.0f;         // reset timing for a/c maintenance
            
        } catch (const std::exception& e) {
            // try re-init...
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            dataRefs.SetReInitAll(true);
        } catch (...) {
            // try re-init...
            dataRefs.SetReInitAll(true);
        }

        // *** Try recovery from something bad by re-initializing ourselves as much as possible ***
        // LiveTraffic Top Level Exception handling: catch all, die if something happens
        try {
            // asked for a generel re-initialization, e.g. due to time jumps?
            if (dataRefs.IsReInitAll()) {
                // force an initialization
                SHOW_MSG(logWARN, MSG_REINIT)
                dataRefs.SetUseHistData(dataRefs.GetUseHistData(), true);
                // and reset the re-init flag
                dataRefs.SetReInitAll(false);
                // Log a new timestamp
                LogTimestamps();
            }
        } catch (const std::exception& e) {
            // Exception during re-init...we give up and disable ourselves
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            LOG_MSG(logFATAL, MSG_DISABLE_MYSELF);
            dataRefs.SetReInitAll(false);
            XPLMDisablePlugin(dataRefs.GetMyPluginId());
            return 0;           // don't call me again
        }
        
        // LiveTraffic Top Level Exception handling: catch all, reinit if something happens
        try {
            // Refresh airport data from apt.dat (in case camera moved far)
            LTAptRefresh();
            // maintenance (add/remove)
            LTFlightDataAcMaintenance();
            // updates to menu item status
            MenuUpdateAllItemStatus();
        } catch (const std::exception& e) {
            // try re-init...
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            dataRefs.SetReInitAll(true);
        } catch (...) {
            // try re-init...
            dataRefs.SetReInitAll(true);
        }
    }
    while (dataRefs.IsReInitAll());
    
    // keep calling me
    return FLIGHT_LOOP_INTVL;
}

// Preferences functions for XPMP API
int   MPIntPrefsFunc   (const char*, const char* key, int   iDefault)
{
    // debug XPMP's CSL model matching if requested
    if (!strcmp(key, "model_matching")) {
        if constexpr (VERSION_BETA)         // force logging of model-matching in BETA versions
            return true;
        else
            return dataRefs.GetDebugModelMatching();
    }
    // allow asynch loading of models
    else if (!strcmp(key, "allow_obj8_async_load"))
       return 1;    // logging level to match ours
    // How many full a/c to draw at max?
    else if (!strcmp(key, "max_full_count"))
        return dataRefs.GetMaxFullNumAc();
    // also register the original libxplanemp dataRefs for CSL models?
    else if (!strcmp(key, "dr_libxplanemp"))
        return dataRefs.GetDrLibXplaneMP();
    
    // dont' know/care about the option, return the default value
    return iDefault;
}

float MPFloatPrefsFunc (const char* section, const char* key, float fDefault)
{
    // Max distance for drawing full a/c (as opposed to 'lights only')?
    if ( !strcmp(section,"planes") && !strcmp(key,"full_distance") )
    { return (float)dataRefs.GetFullDistance_nm(); }

    return fDefault;
}

// loops until the next enabled CSL path and verifies it is an existing path
std::string NextValidCSLPath (DataRefs::vecCSLPaths::const_iterator& cslIter,
                              DataRefs::vecCSLPaths::const_iterator cEnd)
{
    std::string ret;
    
    // loop over vector of CSL paths
    for ( ;cslIter != cEnd; ++cslIter) {
        // disabled?
        if (!cslIter->enabled()) {
            LOG_MSG(logMSG, ERR_CFG_CSL_DISABLED, cslIter->path.c_str());
            continue;
        }
        
        // enabled, path could be relative to X-Plane
        ret = LTCalcFullPath(cslIter->path);
        
        // exists, has files?
        if (LTNumFilesInPath(ret) < 1) {
            LOG_MSG(logMSG, ERR_CFG_CSL_EMPTY, cslIter->path.c_str());
            ret.erase();
            continue;
        }
        
        // looks like a possible path, return it
        // prepare for next call, move to next item
        ++cslIter;
        
        return ret;
    }
    
    // didn't find anything
    return std::string();
}

//
//MARK: Init/Destroy
//
bool LTMainInit ()
{
    LOG_ASSERT(dataRefs.pluginState == STATE_STOPPED);

    // Init fetching flight data
    if (!LTFlightDataInit()) return false;
    
    // These are the paths configured for CSL packages
    const DataRefs::vecCSLPaths& vCSLPaths = dataRefs.GetCSLPaths();
    DataRefs::vecCSLPaths::const_iterator cslIter = vCSLPaths.cbegin();
    const DataRefs::vecCSLPaths::const_iterator cslEnd = vCSLPaths.cend();
    std::string cslPath = NextValidCSLPath(cslIter, cslEnd);
    // Error if no valid path found...we continue anyway
    if (cslPath.empty())
        SHOW_MSG(logERR,ERR_CFG_CSL_NONE);
    
    // init Multiplayer API
    // apparently the legacy init is still necessary.
    // Otherwise the XPMP datarefs wouldn't be registered and hence the
    // planes' config would never change (states like flaps/gears are
    // communicated to the model via custom datarefs,
    // see XPMPMultiplayerObj8.cpp/obj_get_float)
    const std::string pathRelated (LTCalcFullPluginPath(PATH_RELATED_TXT));
    const std::string pathLights  (LTCalcFullPluginPath(PATH_LIGHTS_PNG));
    const std::string pathDoc8643 (LTCalcFullPluginPath(PATH_DOC8643_TXT));
    const char* cszResult = XPMPMultiplayerInitLegacyData
    (
        cslPath.c_str(),                // we pass in the first found CSL dir
        pathRelated.c_str(),
        pathLights.c_str(),
        pathDoc8643.c_str(),
        dataRefs.GetDefaultAcIcaoType().c_str(),
        &MPIntPrefsFunc, &MPFloatPrefsFunc
    );
    // Init of multiplayer library failed. Cleanup as much as possible and bail out
    if ( cszResult[0] ) {
        LOG_MSG(logFATAL,ERR_XPMP_ENABLE, cszResult);
        XPMPMultiplayerCleanup();
        LTFlightDataStop();
        return false;
    }
    
    // now register all other CSLs directories that we found earlier
    for (cslPath = NextValidCSLPath(cslIter, cslEnd);
         !cslPath.empty();
         cslPath = NextValidCSLPath(cslIter, cslEnd))
    {
        cszResult = XPMPLoadCSLPackage
        (
         cslPath.c_str(),
         pathRelated.c_str(),
         pathDoc8643.c_str()
         );
        // Addition of CSL package failed...that's not fatal as we did already
        // register one with the XPMPMultiplayerInitLegacyData call
        if ( cszResult[0] ) {
            LOG_MSG(logERR,ERR_XPMP_ADD_CSL, cszResult);
        }
    }
    
    // register flight loop callback, but don't call yet (see enable later)
    XPLMRegisterFlightLoopCallback(LoopCBAircraftMaintenance, 0, NULL);
    
    // Success
    dataRefs.pluginState = STATE_INIT;
    LOG_MSG(logDEBUG,DBG_LT_MAIN_INIT);
    return true;
}

// Enabling showing aircraft
bool LTMainEnable ()
{
    LOG_ASSERT(dataRefs.pluginState == STATE_INIT);

    // Initialize libxplanemp
    const std::string pathRes     (LTCalcFullPluginPath(PATH_RESOURCES) + dataRefs.GetDirSeparator());
    const char*cszResult = XPMPMultiplayerInit (&MPIntPrefsFunc,
                                                &MPFloatPrefsFunc,
                                                pathRes.c_str());
    if ( cszResult[0] ) {
        LOG_MSG(logFATAL,ERR_XPMP_ENABLE, cszResult);
        XPMPMultiplayerCleanup();
        return false;
    }
    
    // Enable fetching flight data
    if (!LTFlightDataEnable()) return false;

    // Success
    dataRefs.pluginState = STATE_ENABLED;
    LOG_MSG(logDEBUG,DBG_LT_MAIN_ENABLE);
    return true;
}

// Actually do show aircraft
bool LTMainShowAircraft ()
{
    LOG_ASSERT(dataRefs.pluginState >= STATE_ENABLED);
    
    // short cut if already showing
    if ( dataRefs.AreAircraftDisplayed() ) return true;
    
    // select aircraft for display
    dataRefs.ChTsOffsetReset();             // reset network time offset
    if ( !LTFlightDataShowAircraft() ) return false;

    // Now only enable multiplay lib - this acquires multiplayer planes
    //   and is the possible point of conflict with other plugins
    //   using xplanemp, so we push it out to as late as possible.
    
    // Refresh set of aircraft loaded
    XPMPLoadPlanesIfNecessary();
    
    // Enable Multiplayer plane drawing, acquire multiuser planes
    if (!dataRefs.IsAIonRequest())      // but only if not only on request
        LTMainToggleAI(true);
    
    // enable the flight loop callback to maintain aircraft
    XPLMSetFlightLoopCallbackInterval(LoopCBAircraftMaintenance,
                                      -1.0,     // initial call as fast as possible
                                      1,        // relative to now
                                      NULL);
    
    // success
    dataRefs.pluginState = STATE_SHOW_AC;
    return true;
}

// Enable Multiplayer plane drawing, acquire multiuser planes
bool LTMainTryGetAIAircraft ()
{
    // short-cut if we have control already
    if (dataRefs.HaveAIUnderControl())
        return true;
    
    const char* cszResult = XPMPMultiplayerEnable();
    if ( cszResult[0] ) { SHOW_MSG(logFATAL,ERR_XPMP_ENABLE, cszResult); return false; }
    
    // If we don't control AI aircraft we can't create TCAS blibs.
    if (!dataRefs.HaveAIUnderControl()) {
        // Failed! Because of whom?
        int total=0, active=0;
        XPLMPluginID who=0;
        char whoName[256];
        XPLMCountAircraft(&total, &active, &who);
        XPLMGetPluginInfo(who, whoName, nullptr, nullptr, nullptr);
        
        // inform the user about this fact, but otherwise continue
        SHOW_MSG(logWARN,ERR_NO_TCAS,whoName);
    }
    return true;
}

/// Disable Multiplayer place drawing, releasing multiuser planes
void LTMainReleaseAIAircraft ()
{
    // just pass on to libxplanemp
    XPMPMultiplayerDisable ();
}

/// Callback, which toggles AI control
static float CBToggleAI (float, float, int, void *)
{
    if (dataRefs.HaveAIUnderControl())
        LTMainReleaseAIAircraft();
    else
        LTMainTryGetAIAircraft();
    MenuUpdateAllItemStatus();
    return 0.0f;
}

/// @brief Show message about delay, then set callback to trigger getting/release AI
/// @details Getting and even more release AI means,
///          that X-Plane needs to load a couple of aircraft models,
///          which is done immediately and pauses the sim.
///          We show a message, but need one cycle so that it can actually be drawn,
///          then only must the actual change happen -> flight loop callback.
void LTMainToggleAI (bool bGetControl)
{
    // Short cut if there is no change
    if (bGetControl == bool(dataRefs.HaveAIUnderControl()))
        return;
    
    // Show a message
    CreateMsgWindow(1.0f, logMSG, MSG_AI_LOAD_ACF);
    
    // Create a flight loop callback to do the AI change
    static XPLMFlightLoopID aiID = nullptr;
    if (!aiID) {
        XPLMCreateFlightLoop_t aiCall = {
            sizeof(aiCall),
            xplm_FlightLoop_Phase_BeforeFlightModel,
            CBToggleAI,
            nullptr
        };
        aiID = XPLMCreateFlightLoop(&aiCall);
    }
    if (aiID)
        XPLMScheduleFlightLoop(aiID, 0.5f, 1);
    else                    // safeguard if for some reason we couldn't create a callback
        CBToggleAI(0.0f, 0.0f, 0, nullptr);
}

// Remove all aircraft
void LTMainHideAircraft ()
{
    LOG_ASSERT(dataRefs.pluginState >= STATE_ENABLED);

    // short cut if not showing
    if ( !dataRefs.AreAircraftDisplayed() ) return;
    
    // hide aircraft, disconnect internet streams
    LTFlightDataHideAircraft ();
    
    // Remove any message about seeing planes
    CreateMsgWindow(float(AC_MAINT_INTVL), 0, 0, -1);

    // disable the flight loop callback
    XPLMSetFlightLoopCallbackInterval(LoopCBAircraftMaintenance,
                                      0,            // disable
                                      1,            // relative to now
                                      NULL);
    
    // disable aircraft drawing, free up multiplayer planes
    // (the "soft way", which requires a few more drawing cycles,
    //  this will _not_ work while being shut down)
    LTMainToggleAI(false);
    
    // tell the user there are no more
    SHOW_MSG(logINFO, MSG_NUM_AC_ZERO);
    dataRefs.pluginState = STATE_ENABLED;
}

// Stop showing aircraft
void LTMainDisable ()
{
    LOG_ASSERT(dataRefs.pluginState >= STATE_ENABLED);

    // remove aircraft...just to be sure
    dataRefs.SetAircraftDisplayed(false);
    LTMainReleaseAIAircraft();      // to be absolutely sure
    
    // disable fetching flight data
    LTFlightDataDisable();
    
    // De-init libxplanemp
    XPMPMultiplayerCleanup();
    
    // save config file
    dataRefs.SaveConfigFile();
    
    // success
    dataRefs.pluginState = STATE_INIT;
}

// Cleanup work before shutting down
void LTMainStop ()
{
    LOG_ASSERT(dataRefs.pluginState == STATE_INIT);

    // unregister flight loop callback
    XPLMUnregisterFlightLoopCallback(LoopCBAircraftMaintenance, NULL);
    
    // Cleanup Multiplayer API
    XPMPMultiplayerCleanup();
    
    // Flight data
    LTFlightDataStop();

    // success
    dataRefs.pluginState = STATE_STOPPED;
}


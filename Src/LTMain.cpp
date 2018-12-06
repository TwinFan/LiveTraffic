//
//  LTMain.cpp
//  LiveTraffic

/*
 * Copyright (c) 2018, Birger Hoppe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "LiveTraffic.h"

//MARK: Path conversion

// Mac Only: taken from xplanemp/PlatformUtils.h, which can't be included due to an #error directive
#if APL
/*
 * Convert an HFS path to a POSIX Path.  Returns 0 for success, -1 for failure.
 * WARNING: this only works for EXISTING FILES!!!!!!!!!!!!!!!!!
 *
 */
int HFS2PosixPath(const char *path, char *result, int resultLen);
int Posix2HFSPath(const char *path, char *result, int resultLen);

// Convert HFS (as used by XPLM) to Posix (as used by XPMP)
std::string& LTHFS2Posix ( std::string& path )
{
    char szResultBuf[512];
    HFS2PosixPath(path.c_str(), szResultBuf, sizeof(szResultBuf));
    return path = szResultBuf;
}
#endif


// if necessary exchange the directory separator from / to a local one.
// (works only well on partial paths as defined in Constants.h!)
std::string LTPathToLocal ( const char* p, bool bXPMPStyle )
{
    std::string path(p);
    std::string dirSep(bXPMPStyle ?
                       dataRefs.GetDirSeparatorMP() :
                       dataRefs.GetDirSeparator());
    
    // if necessary exchange dir separator
    if ( dataRefs.GetDirSeparator() != PATH_STD_SEPARATOR )
    {
        // replace all "/" with DirSeparator
        for (std::string::size_type pos = path.find(PATH_STD_SEPARATOR);
             pos != std::string::npos;
             pos = path.find(PATH_STD_SEPARATOR))
            path.replace(pos,1,dirSep);
    }
    return path;
}

// construct path: if passed-in base is a full path just take it
// otherwise it is relative to XP system path
std::string LTCalcFullPath ( const char* path, bool bXPMPStyle )
{
    std::string ret;
    
    // starts with DirSeparator or [windows] second char is a colon?
    if (dataRefs.GetDirSeparator()[0] == path[0] ||
        (strlen(path) >= 2 && path[1] == ':' ) ) {
        // just take the given path
        ret = LTPathToLocal(path,false);
    } else {
        // otherwise it shall be a local path relative to XP main
        ret = dataRefs.GetXPSystemPath() + LTPathToLocal(path,false);
    }
    
#if APL
    // convert to Posix as expected by XPMP
    if (bXPMPStyle)
        LTHFS2Posix(ret);
#endif
    
    return ret;
}

// same as above, but relative to plugin directory
std::string LTCalcFullPluginPath ( const char* path, bool bXPMPStyle )
{
    std::string ret;
    
    // starts with DirSeparator or [windows] second char is a colon?
    if (dataRefs.GetDirSeparator()[0] == path[0] ||
        (strlen(path) >= 2 && path[1] == ':' ) ) {
        // just take the given path
        ret = LTPathToLocal(path,false);
    } else {
        // otherwise it shall be a local path relative to the plugin's dir
        ret = dataRefs.GetLTPluginPath() + LTPathToLocal(path,false);
    }
    
#if APL
    // convert to Posix as expected by XPMP
    if (bXPMPStyle)
        LTHFS2Posix(ret);
#endif

    return ret;
}

// given a path (in XPLM notation) returns number of files in the path
// or 0 in case of errors
int LTNumFilesInPath ( const char* path )
{
    char aszFileNames[2048] = "";
    int iTotalFiles = 0;
    if ( !XPLMGetDirectoryContents(path, 0,
                                   aszFileNames, sizeof(aszFileNames),
                                   NULL, 0,
                                   &iTotalFiles, NULL) && !iTotalFiles)
    { LOG_MSG(logERR,ERR_DIR_CONTENT,path); }
    
    return iTotalFiles;
}

// finds 'plugins/.../Resources/CSL' directories, returns in XPMP style
bool LTFindCSLDirectories (std::list<std::string>& lstCSLDirs)
{
    // nothing found yet
    lstCSLDirs.clear();
    
    // Get plugins directories content
    char aszFileNames[513] = "";
    int iTotalFiles = 1;
    
    // looping plugin directories (we do it one directory at a time...
    // not totally efficient, but OK for a one-time init)
    const std::string pluginsPath(LTCalcFullPath(PATH_RES_PLUGINS));
    for (int iCurrFile=0; iCurrFile < iTotalFiles; iCurrFile++) {
        // read the next directory in the plugins diretocry
        if ( !XPLMGetDirectoryContents(pluginsPath.c_str(),
                                       iCurrFile,
                                       aszFileNames, sizeof(aszFileNames),
                                       NULL, 0,
                                       &iTotalFiles, NULL))
        { LOG_MSG(logERR,ERR_DIR_CONTENT,pluginsPath.c_str()); return false;}
        
        // nothing found, end?
        if (!aszFileNames[0])
            break;
        
        // check for a Resources directory there
        std::string path (pluginsPath +
                          dataRefs.GetDirSeparator() + aszFileNames +
                          dataRefs.GetDirSeparator() + FILE_RESOURCES);
        if ( LTNumFilesInPath(path.c_str()) > 0) {
            // check for a CSL directory there
            path += dataRefs.GetDirSeparator() +  FILE_CSL;
            if ( LTNumFilesInPath( path.c_str()) > 0)
            {
                // found a Resources/CSL! Add it to the list of paths
#if APL
                // convert to Posix as expected by XPMP
                LTHFS2Posix(path);
#endif
                lstCSLDirs.emplace_back(std::move(path));
            }
        }
        
    }
    
    // no path found??? Log error msg
    if (lstCSLDirs.empty()) {
        LOG_MSG(logFATAL,ERR_RES_CSL_NOT_FOUND,pluginsPath.c_str());
        return false;
    }
    
    // success
    return true;
}

//
//MARK: Utility
//

// change a string to uppercase
std::string& str_toupper(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) -> unsigned char { return (unsigned char) toupper(c); });
    return s;
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

//
//MARK: Callbacks
//

// flight loop callback, will be called every second if enabled
// creates/destroys aircrafts by looping the flight data map
float LoopCBAircraftMaintenance (float, float, int, void*)
{
    do {
        // LiveTraffic Top Level Exception handling: catch all, reinit if something happens
        try {
            // asked for a generel re-initialization, e.g. due to time jumps?
            if (dataRefs.IsReInitAll()) {
                // force an initialization
                dataRefs.SetUseHistData(dataRefs.GetUseHistData(), true);
                // and reset the re-init flag
                dataRefs.SetReInitAll(false);
            }
        } catch (const std::exception& e) {
            // FIXME: Correct would be to disable the plugin...can I do that?
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            // No ideas to recover from here...die
            throw;
        }
        
        // LiveTraffic Top Level Exception handling: catch all, reinit if something happens
        try {
            // maintenance (add/remove)
            LTFlightDataAcMaintenance();
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
int   MPIntPrefsFunc   (const char* section, const char* key, int   iDefault)
{
    // debug XPMP's CSL model matching if requested
    if ( !strcmp(section,"debug") && !strcmp(key,"model_matching") )
    { return dataRefs.GetDebugModelMatching(); }
    
    // How many full a/c to draw at max?
    if ( !strcmp(section,"planes") && !strcmp(key,"max_full_count") )
    { return dataRefs.GetMaxFullNumAc(); }
    
    return iDefault;
}

float MPFloatPrefsFunc (const char* section, const char* key, float fDefault)
{
    // Max distance for drawing full a/c (as opposed to 'lights only')?
    if ( !strcmp(section,"planes") && !strcmp(key,"full_distance") )
    { return (float)dataRefs.GetFullDistance_nm(); }

    return fDefault;
}

//
//MARK: Init/Destroy
//
bool LTMainInit ()
{
    LOG_ASSERT(dataRefs.pluginState == STATE_STOPPED);

    // Init fetching flight data
    if (!LTFlightDataInit()) return false;
    
    // find all CSL directories (returns XPMP style, i.e. Posix on Mac)
    std::list<std::string> cslPaths;
    if (!LTFindCSLDirectories(cslPaths)) {
        LTFlightDataStop();                 // cleanup what we did so far
        return false;
    }
    LOG_ASSERT(cslPaths.size() >= 1);
    
    // init Multiplayer API
    // apparently the legacy init is still necessary.
    // Otherwise the XPMP datarefs wouldn't be registered and hence the
    // planes' config would never change (states like flaps/gears are
    // communicated to the model via custom datarefs,
    // see XPMPMultiplayerObj8.cpp/obj_get_float)
    const std::string pathRelated (LTCalcFullPluginPath(PATH_RELATED_TXT, true));
    const std::string pathLights  (LTCalcFullPluginPath(PATH_LIGHTS_PNG, true));
    const std::string pathDoc8643 (LTCalcFullPluginPath(PATH_DOC8643_TXT, true));
    std::list<std::string>::const_iterator cslIter = cslPaths.cbegin();
    const char* cszResult = XPMPMultiplayerInitLegacyData
    (
        cslIter->c_str(),            // we pass in the first found CSL dir
        pathRelated.c_str(),
        pathLights.c_str(),
        pathDoc8643.c_str(),
        CSL_DEFAULT_ICAO,
        &MPIntPrefsFunc, &MPFloatPrefsFunc
    );
    // Init of multiplayer library failed. Cleanup as much as possible and bail out
    if ( cszResult[0] ) {
        LOG_MSG(logFATAL,ERR_XPMP_ENABLE, cszResult);
        XPMPMultiplayerCleanup();
        LTFlightDataStop();
        return false;
    }
    
    // yet another init function...also necessary
    cszResult = XPMPMultiplayerInit ( &MPIntPrefsFunc, &MPFloatPrefsFunc );
    if ( cszResult[0] ) {
        LOG_MSG(logFATAL,ERR_XPMP_ENABLE, cszResult);
        XPMPMultiplayerCleanup();
        LTFlightDataStop();
        return false;
        
    }
    
    // now register all other CSLs directories that we found earlier
    for (++cslIter; cslIter != cslPaths.cend(); ++cslIter) {
        cszResult = XPMPLoadCSLPackage
        (
         cslIter->c_str(),            // we pass in the first found CSL dir
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

// Enabling showing aircrafts
bool LTMainEnable ()
{
    LOG_ASSERT(dataRefs.pluginState == STATE_INIT);

    // Enable fetching flight data
    if (!LTFlightDataEnable()) return false;

    // Success
    dataRefs.pluginState = STATE_ENABLED;
    LOG_MSG(logDEBUG,DBG_LT_MAIN_ENABLE);
    return true;
}

// Actually do show aircrafts
bool LTMainShowAircraft ()
{
    LOG_ASSERT(dataRefs.pluginState >= STATE_ENABLED);
    
    // short cut if already showing
    if ( dataRefs.GetAircraftsDisplayed() ) return true;
    
    // Now only enable multiplay lib - this acquires multiplayer planes
    //   and is the possible point of conflict with other plugins
    //   using xplanemp, so we push it out to as late as possible.
    
    // Refresh set of aircrafts loaded
    XPMPLoadPlanesIfNecessary();
    
    // Enable Multiplayer plane drawing, acquire multiuser planes
    const char* cszResult = XPMPMultiplayerEnable();
    if ( cszResult[0] ) { SHOW_MSG(logFATAL,ERR_XPMP_ENABLE, cszResult); return false; }
    
    // select aircrafts for display
    if ( !LTFlightDataShowAircraft() ) return false;

    // enable the flight loop callback to maintain aircrafts
    XPLMSetFlightLoopCallbackInterval(LoopCBAircraftMaintenance,
                                      FLIGHT_LOOP_INTVL,
                                      1,            // relative to now
                                      NULL);
    
    // success
    dataRefs.pluginState = STATE_SHOW_AC;
    return true;
}

// Remove all aircrafts
void LTMainHideAircraft ()
{
    LOG_ASSERT(dataRefs.pluginState >= STATE_ENABLED);

    // short cut if not showing
    if ( !dataRefs.GetAircraftsDisplayed() ) return;
    
    // hide aircrafts, disconnect internet streams
    LTFlightDataHideAircraft ();

    // disable the flight loop callback
    XPLMSetFlightLoopCallbackInterval(LoopCBAircraftMaintenance,
                                      0,            // disable
                                      1,            // relative to now
                                      NULL);
    
    // disable aircraft drawing, free up multiplayer planes
    XPMPMultiplayerDisable();
    
    // tell the user there are no more
    SHOW_MSG(logINFO, MSG_NUM_AC_ZERO);
    dataRefs.pluginState = STATE_ENABLED;
}

// Stop showing aircrafts
void LTMainDisable ()
{
    LOG_ASSERT(dataRefs.pluginState >= STATE_ENABLED);

    // remove aircrafts...just to be sure
    LTMainHideAircraft();
    
    // disable fetching flight data
    LTFlightDataDisable();
    
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


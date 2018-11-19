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
std::string LTCalcFullPath ( const char* path )
{
    std::string ret;
    
    // starts with DirSeparator or [windows] second char is a colon?
    if (dataRefs.GetDirSeparatorMP()[0] == path[0] ||
        (strlen(path) >= 2 && path[1] == ':' ) ) {
        // just take the given path
        ret = LTPathToLocal(path,false);
    } else {
        // otherwise it shall be a local path relative to XP main
        ret = dataRefs.GetXPSystemPath() + LTPathToLocal(path,false);
    }
    
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

// checks if the passed in directory (full path)
// includes the necessary directories/files
int LTHasDirectoryResources ( std::string& path )
{
    // Get Directory's content
    char aszFileNames[2048] = "";
    int iTotalFiles = 0;
    if ( !XPLMGetDirectoryContents(path.c_str(), 0,
                                   aszFileNames, sizeof(aszFileNames),
                                   NULL, 0,
                                   &iTotalFiles, NULL))
    { LOG_MSG(logERR,ERR_DIR_CONTENT,path.c_str()); return 0;}
    
    // check that all necessary files are included
    int iToBeFound = 4;             // we need to find 4 files
    // iterate over the returned list of files
    for (char* cur = aszFileNames;
         (iToBeFound > 0) && (*cur != 0);   // it ends with an empty string
         cur += strlen(cur)+1 )
    {
        if (!strcmp(cur,FILE_CSL))          iToBeFound--;
        if (!strcmp(cur,FILE_RELATED_TXT))  iToBeFound--;
        if (!strcmp(cur,FILE_DOC8643_TXT))  iToBeFound--;
        if (!strcmp(cur,FILE_LIGHTS_PNG))   iToBeFound--;
    }
    
    // All found?
    return iToBeFound <= 0;
}

// finds the directory with the necessary resources
std::string LTFindResourcesDirectory ()
{
    std::string path;
    // the paths to cycle (LiveTraffic, XSquawkBox)
    // TODO: X-IvAp path to include
    const char* aszPaths[] = {
        PATH_RESOURCES_LT, PATH_RESOURCES_XSB, ""
    };
    for ( int i=0; aszPaths[i][0] != 0; i++ )
    {
        // put together absolut path by prepending with XP System Path
        path = dataRefs.GetXPSystemPath() + LTPathToLocal(aszPaths[i],false);
        // check if it has all necessary files
        if (LTHasDirectoryResources (path))
            break;
        else
            path.clear();
    }
    
    // no path found??? Log error msg including all paths tried
    if ( path.empty() )
    {
        LOG_MSG(logFATAL,ERR_RES_NOT_FOUND);
        for ( int i=0; aszPaths[i][0] != 0; i++ )
            LOG_MSG(logERR,aszPaths[i]);
    }
    
    // empty in case of failure
    return path;
}

//
//MARK: Utility
//

// change a string to uppercase
std::string& str_toupper(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) -> unsigned char { return toupper(c); });
    return s;
}

// format timestamp
std::string ts2string (time_t t)
{
    // format it nicely
    char szBuf[50];
    struct tm tm;
#if !defined(WIN32)
    gmtime_r(&t, &tm);
#else
    gmtime_s(&tm, &t);
#endif
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
    
    // find a resource directory with the necessary files in it
    std::string path (LTFindResourcesDirectory());
    if (path.empty())
        return false;
    
#ifdef APL
    // convert to Posix as expected by XPMP API
    LTHFS2Posix(path);
#endif
    
    // append trailing directory separator (so we can add files later)
    path += dataRefs.GetDirSeparatorMP();
            
    // init Multiplayer API
    // apparently the legacy init is still necessary.
    // Otherwise the XPMP datarefs wouldn't be registered and hence the
    // planes' config would never change (states like flaps/gears are
    // communicated to the model via custom datarefs,
    // see XPMPMultiplayerObj8.cpp/obj_get_float)
    const char* cszResult = XPMPMultiplayerInitLegacyData
    (
     (path + LTPathToLocal(FILE_CSL,true)).c_str(),
     (path + LTPathToLocal(FILE_RELATED_TXT,true)).c_str(),
     (path + LTPathToLocal(FILE_LIGHTS_PNG,true)).c_str(),
     (path + LTPathToLocal(FILE_DOC8643_TXT,true)).c_str(),
     CSL_DEFAULT_ICAO,
     &MPIntPrefsFunc, &MPFloatPrefsFunc
    );
    if ( cszResult[0] ) { LOG_MSG(logFATAL,ERR_ENABLE_XPMP, cszResult); return false; }
    
    // yet another init function...also necessary
    cszResult = XPMPMultiplayerInit ( &MPIntPrefsFunc, &MPFloatPrefsFunc );
    if ( cszResult[0] ) { LOG_MSG(logFATAL,ERR_ENABLE_XPMP, cszResult); return false; }
    
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

    // Refresh set of aircrafts loaded
    XPMPLoadPlanesIfNecessary();

    // Enable Multiplayer plane drawing
    const char* cszResult = XPMPMultiplayerEnable();
    if ( cszResult[0] ) { LOG_MSG(logFATAL,ERR_ENABLE_XPMP, cszResult); return false; }

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
    
    // disable aircraft drawing
    XPMPMultiplayerDisable();
    
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


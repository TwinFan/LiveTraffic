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
std::string LTCalcFullPath ( const std::string& path )
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
std::string LTCalcFullPluginPath ( const std::string& path )
{
    // starts with DirSeparator or [windows] second char is a colon?
    if (dataRefs.GetDirSeparator()[0] == path[0] ||
        (path.length() >= 2 && path[1] == ':' ) )
        // just take the given path, it is a full path already
        return path;

    // otherwise it shall be a local path relative to the plugin's dir
    // prepend with plugin path to make it a full path:
    return dataRefs.GetLTPluginPath() + path;
}

// if path starts with the XP system path it is removed
std::string LTRemoveXPSystemPath (const std::string& path)
{
    std::string p(path);
    LTRemoveXPSystemPath(p);
    return p;
}

void LTRemoveXPSystemPath (std::string& path)
{
    const size_t sysPLen = dataRefs.GetXPSystemPath().length();
    if (path.length() > sysPLen &&      // only remove if path is actually longer
        begins_with<std::string>(path, dataRefs.GetXPSystemPath()))
        path.erase(0, sysPLen);
}

// given a path returns number of files in the path
// or 0 in case of errors
int LTNumFilesInPath ( const std::string& path )
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

// Windows is missing a few simple macro definitions
#if !defined(S_ISDIR)
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

// Is path a directory?
bool IsDir (const std::string& path)
{
    struct stat buffer;
    if (stat (path.c_str(), &buffer) != 0)  // get stats...error?
        return false;                       // doesn't exist...no directory either
    return S_ISDIR(buffer.st_mode);         // check for S_IFDIR mode flag
}

// List of files in a directory (wrapper around XPLMGetDirectoryContents)
std::vector<std::string> GetDirContents (const std::string& path, bool bDirOnly)
{
    std::vector<std::string> l;             // the list to be returned
    char szNames[4048];                     // buffer for file names
    char* indices[256];                     // buffer for indices to beginnings of names
    int start = 0;                          // first file to return
    int numFiles = 0;                       // number of files returned (per batch)
    bool bFinished = false;
    
    // does path not end with slash? Then we'll need to add one when testing for directories
    std::string addChar;
    if (!path.empty() && path.back() != PATH_DELIM)
        addChar = PATH_DELIM;
    
    // Call XPLMGetDirectoryContents as often as needed to read all directory content
    do {
        numFiles = 0;
        bFinished = XPLMGetDirectoryContents(path.c_str(),
                                             start,
                                             szNames, sizeof(szNames),
                                             indices, sizeof(indices)/sizeof(*indices),
                                             NULL, &numFiles);
        // process (the batch of) files we received now
        for (int i = 0; i < numFiles; ++i)
            if (indices[i][0] != '.' &&     // skip parent_dir and hidden entries
                // if requested: directories only
                (!bDirOnly || IsDir(path + addChar + indices[i])))
                l.push_back(indices[i]);
        // next batch start (if needed)
        start += numFiles;
    } while(!bFinished);
    
    // sort the list of files
    std::sort(l.begin(), l.end());
    
    // return the list of files
    return l;
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

// Get file's modification time
time_t GetFileModTime(const std::string& path)
{
    struct stat buffer;
    if (stat (path.c_str(), &buffer) != 0)  // get stats...error?
        return 0;                           // doesn't exist...no directory either
#if APL == 1
    return buffer.st_mtimespec.tv_sec;
#else
    return buffer.st_mtime;
#endif
}

// Lookup a record by key in a sorted binary record-based file
bool FileRecLookup (std::ifstream& f, size_t& n,
                    unsigned long key,
                    unsigned long& minKey, unsigned long& maxKey,
                    void* outRec, size_t recLen)
{
    // determin min/max key if not yet known
    if (f && maxKey == 0) {
        // Read first record to determine Al
        f.seekg(0);
        f.read((char*)&minKey, sizeof(minKey));
        if (!f) return false;
        // Read last record to determine Ar
        f.seekg(-std::streamoff(recLen), std::ios_base::end);
        f.read((char*)&maxKey, sizeof(maxKey));
        if (!f) return false;
    }

    // Determine number of records if not (yet) known
    if (f && n == 0) {
        f.seekg(0, std::ios_base::end);
        n = size_t(f.tellg()) / recLen;
        if (!f) return false;
    }
    
    // Binary search algorithm (using linear interpolation for the key,
    // and trying to reduce f.read operations as much as possible)
    size_t L = 0;
    size_t m;
    size_t R = n-1;
    unsigned long Al = minKey;      // key value at position L
    unsigned long* pAm = (unsigned long*)outRec;    // key value at position m (is at the beginning of the record, using outRec as buffer)
    unsigned long Ar = maxKey;      // key value at position R
    while (L != R) {
        // approximation by linear interpolation
        m = L + (size_t)std::floor(float(key-Al)/float(Ar-Al) * (R-L));
        
        // test if record at m is less than the key
        f.seekg((long long)(m * recLen));
        f.read((char*)outRec, (std::streamsize)recLen);
        if (!f) return false;
        if (*pAm == key) {
            return true;
        }
        else if (*pAm < key) {
            L = m+1;                // move to _next_ record as we are too small
            f.read((char*)outRec, (std::streamsize)recLen);
            Al = *(unsigned long*)outRec;
            if (Al == key)          // that next record is our value?
                return true;
            if (Al > key)           // that next value now is too big? Then key doesn't exist
                return false;
        } else {
            R = m;
            Ar = *pAm;
        }
    }
    // not found
    return false;
}


//
// MARK: URL/Help support
//

void LTOpenURL  (const std::string& _url)
{
    // Transiently, we allow to add the current camera position into the URL
    std::string url(_url);
    if (_url.find('%') != std::string::npos) {
        char buf[256];
        const positionTy camPos = dataRefs.GetViewPos();
        snprintf (buf, sizeof(buf), _url.c_str(),
                  camPos.lat(), camPos.lon());
        url = buf;
    }
    
#if IBM
    // Windows implementation: ShellExecuteA
    // https://docs.microsoft.com/en-us/windows/desktop/api/shellapi/nf-shellapi-shellexecutea
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif LIN
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    // Unix uses xdg-open, package xdg-utils, pre-installed at least on Ubuntu
    (void)system((std::string("xdg-open '") + url + "'").c_str());
#pragma GCC diagnostic pop
#else
    // Max use standard system/open
    system((std::string("open '") + url + "'").c_str());
    // Code that causes warning goes here
#endif
}

// just prepend the given path with the base URL an open it
void LTOpenHelp (const std::string& path)
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

// return a std::string copy converted to uppercase
std::string str_toupper_c(const std::string& s)
{
    std::string c(s);
    str_toupper(c);
    return c;
}

bool str_isalnum(const std::string& s)
{
    return std::all_of(s.cbegin(), s.cend(), [](unsigned char c){return isalnum(c);});
}

// Replace all occurences of one string with another
void str_replaceAll(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty() || str.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Continue search only _after_ the replacement position
    }
}

// Cut off everything after `from` from `s`, `from` including
std::string& cut_off(std::string& s, const std::string& from)
{
    const size_t pos = s.find(from);
    if (pos != std::string::npos)
        s.erase(pos);
    return s;
}

// last word of a string
std::string str_last_word (const std::string& s)
{
    std::string::size_type p = s.find_last_of(' ');
    return (p == std::string::npos ? s :    // space not found? -> entire string
            s.substr(p+1));                 // otherwise all after string (can be empty!)
}

// separates string into tokens
std::vector<std::string> str_tokenize (const std::string& s,
                                       const std::string& tokens,
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

// concatenates a vector of strings into one string (reverse of str_tokenize)
std::string str_concat (const std::vector<std::string>& vs, const std::string& separator)
{
    // empty? return empty string
    if (vs.empty()) return "";
    
    // put together the return string, start with the first element
    std::string s = vs.front();
    for (auto iter = std::next(vs.begin()); iter != vs.end(); ++iter)
    {
        s += separator;
        s += *iter;
    }
    return s;
}

// returns first non-empty string, and "" in case all are empty
std::string str_first_non_empty (const std::initializer_list<const std::string>& l)
{
    for (const std::string& s: l)
        if (!s.empty())
            return s;
    return "";
}

// Replaces personal information in the string, like email address
std::string& str_replPers (std::string& s)
{
    // replace email addresses
    static std::regex reEMail("\\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,}\\b", std::regex::icase);
    s = std::regex_replace(s, reEMail, "[email@ano.nym]");
    
    // Replace user's directory name in Linux
    static std::regex reHome("\\/home\\/[-_\\.a-z]+\\/", std::regex::icase);
    s = std::regex_replace(s, reHome, "/home/[user]/");

    // Replace user's directory name in MacOS or Windows
    static std::regex reUsers("[\\/\\\\]Users[\\/\\\\][-_\\.a-z]+[\\/\\\\]", std::regex::icase);
    s = std::regex_replace(s, reUsers, "/Users/[user]/");

    return s;
}

/// Base64 encoding
std::string EncodeBase64 (const std::string& _clear)
{
    // create a buffer for the result and do the encoding
    char* buf = new char [(unsigned)Base64encode_len((int)_clear.length())];
    Base64encode(buf, _clear.c_str(), (int)_clear.length());
    std::string ret (buf);
    delete [] buf;
    return ret;
}

/// Base64 decoding
std::string DecodeBase64 (const std::string& _encoded)
{
    // create a buffer for the result and do the decoding
    char* buf = new char [(unsigned)Base64decode_len(_encoded.c_str())];
    const int len = Base64decode(buf, _encoded.c_str());
    std::string ret (buf, (size_t)len);
    delete [] buf;
    return ret;
}

/// XOR a string with another one
std::string str_xor (const std::string& s, const char* t)
{
    const char* pc = t;
    std::string r ( s.size(), '\0');
    for (size_t i = 0; i < s.size(); ++i, ++pc) {
        if (!(*pc)) pc = t;                     // restart at end of string
        r[i] = s[i] ^ *pc;
    }
    return r;
}

/// Obfuscate a secret string for storing in the settings file
std::string Obfuscate (const std::string& _clear)
{
    // We XOR with a constant text, then base64-convert
    return EncodeBase64(str_xor(_clear, PLUGIN_SIGNATURE));
}

/// Undo obfuscation
std::string Cleartext (const std::string& _obfuscated)
{
    // We base64-decode, then XOR with a constant text
    return str_xor(DecodeBase64(_obfuscated), PLUGIN_SIGNATURE);
}


//
// MARK: Time Functions
//

// returns offset to UTC in seconds
/// @see https://stackoverflow.com/questions/13804095/get-the-time-zone-gmt-offset-in-c
int timeOffsetUTC()
{
    static int cachedOffset = INT_MIN;

    if (cachedOffset > INT_MIN)
        return cachedOffset;
    else {
        time_t gmt, rawtime = time(NULL);
        struct tm gbuf;
        gmtime_s(&gbuf, &rawtime);

        // Request that mktime() looks up dst in timezone database
        gbuf.tm_isdst = -1;
        gmt = mktime(&gbuf);

        return cachedOffset = (int)difftime(rawtime, gmt);
    }
}

// Converts a UTC time to epoch value, assuming today's date
time_t mktime_utc (int h, int min, int s)
{
    const time_t now = time(NULL);
    struct tm gbuf;
    gmtime_s(&gbuf, &now);
    gbuf.tm_hour = h;
    gbuf.tm_min  = min;
    gbuf.tm_sec  = s;
    gbuf.tm_isdst = -1;         // re-lookup timezone/DST information!
    time_t ret = mktime_utc(gbuf);
    // around midnight there will be corner-cases where the "current" date
    // might not be the right one for the given values.
    // Make sure difference to now is less than 24h
    while (now - ret > 86400)
        ret += 86400;
    while (now - ret < -86400)
        ret -= 86400;
    return ret;
}

// Convert time string "YYYY-MM-DD HH:MM:SS" to epoch value
time_t mktime_string (const std::string& s)
{
    static std::regex reTm ("(\\d{4})-(\\d{2})-(\\d{2}) (\\d{1,2}):(\\d{2}):(\\d{2})");
    std::smatch mTm;
    std::regex_search(s, mTm, reTm);
    if (mTm.size() != 7)
        return 0;
        
    struct tm gbuf;
    memset(&gbuf, 0, sizeof(gbuf));
    gbuf.tm_year = std::stoi(mTm.str(1)) - 1900;
    gbuf.tm_mon  = std::stoi(mTm.str(2)) -    1;
    gbuf.tm_mday = std::stoi(mTm.str(3));
    gbuf.tm_hour = std::stoi(mTm.str(4));
    gbuf.tm_min  = std::stoi(mTm.str(5));
    gbuf.tm_sec  = std::stoi(mTm.str(6));
    gbuf.tm_isdst = -1;         // re-lookup timezone/DST information!
    return mktime_utc(gbuf);
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
             "%Y-%m-%d %H:%M:%S",       // %F %T
             &tm);
    return std::string(szBuf);
}

// Converts an epoch timestamp to a Zulu time string incl. 10th of seconds
std::string ts2string (double _zt, int secDecimals)
{
    char s[100];
    snprintf(s, sizeof(s), "%s.%dZ",
             ts2string(time_t(_zt)).c_str(),
             int(std::fmod(_zt, 1.0f) * std::pow(10.0,secDecimals)) );
    return std::string(s);
}

// Convert an XP network time float to a string
std::string NetwTimeString (float runS)
{
    // Extract hours, minutes, and seconds (incl. fractions) from runS
    const unsigned runH = unsigned(runS / 3600.0f);
    runS -= runH * 3600.0f;
    const unsigned runM = unsigned(runS / 60.0f);
    runS -= runM * 60.0f;

    // Convert to string
    char s[20];
    snprintf(s, sizeof(s), "%u:%02u:%06.3f",
             runH, runM, runS);
    return std::string(s);
}

// Convenience function to check on something at most every x seconds
bool CheckEverySoOften (float& _lastCheck, float _interval, float _now)
{
    if (_lastCheck < 0.00001f ||
        _now >= _lastCheck + _interval) {
        _lastCheck = _now;
        return true;
    }
    return false;
}

// MARK: Other Utility Functions

// Fetch nearest airport id by location
std::string GetNearestAirportId (const positionTy& _pos,
                                 positionTy* outApPos)
{
    char airportId[33] = "";

    // Find the nearest airport
    float lat = (float)_pos.lat();
    float lon = (float)_pos.lon();
    XPLMNavRef navRef = XPLMFindNavAid(nullptr, nullptr,
                                       &lat, &lon, nullptr,
                                       xplm_Nav_Airport);
    if (navRef) {
        // fetch airport info
        float alt = 0.0f;
        XPLMGetNavAidInfo(navRef, nullptr, &lat, &lon, &alt, nullptr, nullptr,
                          airportId, nullptr, nullptr);
        // fill output structure
        if (outApPos) {
            outApPos->lat() = lat;
            outApPos->lon() = lon;
            outApPos->SetAltFt((double)alt);
        }
    }
    
    // return the id
    return airportId;
}

// Convert ADS-B Emitter Category to text
const char* GetADSBEmitterCat (const std::string& cat)
{
    // We expect 2 characters
    if (cat.length() != 2) return cat.c_str();
    
    switch (cat[0]) {
        case 'A':
            switch (cat[1]) {
                case '0': return "Category A - No Info";
                case '1': return "Light (<15500 lbs)";
                case '2': return "Small (15500-75000 lbs)";
                case '3': return "Large (75000-300000 lbs)";
                case '4': return "High-Vortex Large";
                case '5': return "Heavy (>300000 lbs)";
                case '6': return "High Performance";
                case '7': return "Rotorcraft";
            }
            break;
        case 'B':
            switch (cat[1]) {
                case '0': return "Category B - No Info";
                case '1': return "Glider / Sailplane";
                case '2': return "Lighter-than-Air";
                case '3': return "Parachutist / Skydiver";
                case '4': return "Ultralight / hang-glider / paraglider";
                case '6': return "Unmanned Aerial Vehicle";
                case '7': return "Space / Trans-atmospheric vehicle";
            }
            break;
        case 'C':
            switch (cat[1]) {
                case '0': return "Category C - No Info";
                case '1': return "Emergency Vehicle";
                case '2': return "Service Vehicle";
                case '3': return "Point Obstacle";
                case '4': return "Cluster Obstacle";
                case '5': return "Line Obstacle";
            }
            break;
        case 'D':
            switch (cat[1]) {
                case '0': return "Category D - No Info";
            }
            break;
    }
    
    // Shouldn't be here...
    return cat.c_str();
}

// comparing 2 doubles for near-equality
bool dequal ( const double d1, const double d2 )
{
    const double epsilon = 0.00001;
    return ((d1 - epsilon) < d2) &&
    ((d1 + epsilon) > d2);
}

//
//MARK: Callbacks
//

// collects all updates that need to be done up to every flight loop cycle
void LTRegularUpdates()
{
    // only update once per flight loop cycle
    static int lstCycleNum = -1;
    const int currCycleNum = XPLMGetCycleNumber();
    if (lstCycleNum == currCycleNum)
        return;
    lstCycleNum = currCycleNum;
    
    // all calls needed (up to) every flight loop:
    
    // Update cached values
    dataRefs.UpdateCachedValues();
    
    // Check if some msg window needs to show
    CheckThenShowMsgWindow();

    // handle new network data (that func has a short-cut exit if nothing to do)
    LTFlightData::AppendAllNewPos();

    // Flush out all non-written log messages
    FlushMsg();
}


// flight loop callback, will be called every 5th frame while showing aircraft;
// creates/destroys aircraft by looping the flight data map
float LoopCBAircraftMaintenance (float inElapsedSinceLastCall, float, int, void*)
{
    static float elapsedSinceLastAcMaint = 0.0f;
    do {
        // *** check for new positons that require terrain altitude (Y Probes) ***
        // LiveTraffic Top Level Exception handling: catch all, reinit if something happens
        try {
            // regular calls collected here
            LTRegularUpdates();
            
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
                dataRefs.ForceDataReload();
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
            // Potentially refresh weather information
            dataRefs.WeatherUpdate();
            // Refresh airport data from apt.dat (in case camera moved far)
            LTAptRefresh();
            // maintenance (add/remove)
            LTFlightDataAcMaintenance();
            // updates to menu item status
            MenuUpdateAllItemStatus();
            // Purge messages kept in local storage for display
            PurgeMsgList();
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
    if (!strcmp(key, XPMP_CFG_ITM_MODELMATCHING)) {
        if constexpr (LIVETRAFFIC_VERSION_BETA)         // force logging of model-matching in BETA versions
            return true;
        else
            return dataRefs.GetDebugModelMatching();
    }
    // logging level to match ours
    if (!strcmp(key, XPMP_CFG_ITM_LOGLEVEL)) {
        if constexpr (LIVETRAFFIC_VERSION_BETA)         // force DEBUG-level logging in BETA versions
            return logDEBUG;
        else
            return dataRefs.GetLogLevel();
    }
    // We don't want clamping to the ground, we take care of the ground ourselves
    if (!strcmp(key, XPMP_CFG_ITM_CLAMPALL)) return 0;
    // We want XPMP2 to assign unique modeS_ids if we feed duplicates (which can happen due to different id systems in use, especially ICAO vs FLARM)
    if (!strcmp(key, XPMP_CFG_ITM_HANDLE_DUP_ID)) return 1;
    // Copying .obj files is an advanced setting
    if (!strcmp(key, XPMP_CFG_ITM_REPLDATAREFS) ||
        !strcmp(key, XPMP_CFG_ITM_REPLTEXTURE))
        return dataRefs.ShallCpyObjFiles();
    // Support XPMP2 Remote Clinet?
    if (!strcmp(key, XPMP_CFG_ITM_SUPPORT_REMOTE))
        return dataRefs.GetRemoteSupport();
    
    // dont' know/care about the option, return the default value
    return iDefault;
}

// loops until the next enabled CSL path and verifies it is an existing path
std::string NextValidCSLPath (DataRefs::vecCSLPaths::const_iterator& cslIter,
                              DataRefs::vecCSLPaths::const_iterator cEnd)
{
    // loop over vector of CSL paths
    for ( ;cslIter != cEnd; ++cslIter) {
        // disabled?
        if (!cslIter->enabled()) {
            LOG_MSG(logMSG, ERR_CFG_CSL_DISABLED, cslIter->getPath().c_str());
            continue;
        }
        
        // enabled, does path exist?
        if (cslIter->exists())
            // return this path, but also inrement iterator for next call
            return LTCalcFullPath((cslIter++)->getPath());

        // doesn't exist or is empty
        LOG_MSG(logMSG, ERR_CFG_CSL_EMPTY, cslIter->getPath().c_str());
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
        
    // init Multiplayer API
    const char* cszResult = XPMPMultiplayerInit (LIVE_TRAFFIC,
                                                 LTCalcFullPluginPath(PATH_RESOURCES).c_str(),
                                                 &MPIntPrefsFunc,
                                                 dataRefs.GetDefaultAcIcaoType().c_str(),
                                                 LIVE_TRAFFIC_XPMP2);
    if ( cszResult[0] ) {
        LOG_MSG(logFATAL,ERR_INIT_XPMP, cszResult);
        XPMPMultiplayerCleanup();
        return false;
    }
    
    // These are the paths configured for CSL packages
    const DataRefs::vecCSLPaths& vCSLPaths = dataRefs.GetCSLPaths();
    DataRefs::vecCSLPaths::const_iterator cslIter = vCSLPaths.cbegin();
    const DataRefs::vecCSLPaths::const_iterator cslEnd = vCSLPaths.cend();

    // now register all other CSLs directories that we found earlier
    bool bAnyPathFound = false;
    for (std::string cslPath = NextValidCSLPath(cslIter, cslEnd);
         !cslPath.empty();
         cslPath = NextValidCSLPath(cslIter, cslEnd))
    {
        bAnyPathFound = true;
        cszResult = XPMPLoadCSLPackage (cslPath.c_str());
        // Addition of CSL package failed...that's not fatal as we did already
        // register one with the XPMPMultiplayerInitLegacyData call
        if ( cszResult[0] ) {
            LOG_MSG(logERR,ERR_XPMP_ADD_CSL, cslPath.c_str(), cszResult);
        }
    }
    
    // Error if no valid path found...we continue anyway
    if (!bAnyPathFound)
        SHOW_MSG(logERR,ERR_CFG_CSL_NONE);

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

    // enable the flight loop callback to maintain aircraft
    XPLMSetFlightLoopCallbackInterval(LoopCBAircraftMaintenance,
                                      -1.0,     // initial call as fast as possible
                                      1,        // relative to now
                                      NULL);
    
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
    
    // Verify number of installed CSL models...if it is 0 or 1 it's fishy!
    const int numModels = XPMPGetNumberOfInstalledModels();
    if (numModels <= 1) {
        if (numModels <= 0) {
            SHOW_MSG(logFATAL, ERR_CFG_CSL_ZERO_MODELS);
            return false;
        } else {
            // Exactly one model loaded...does this happen to be the car?
            std::string mdlName, mdlIcao, mdlAirline, mdlLivery;
            XPMPGetModelInfo2(0, mdlName, mdlIcao, mdlAirline, mdlLivery);
            if (mdlIcao == dataRefs.GetDefaultCarIcaoType())
                SHOW_MSG(logERR, ERR_CFG_CSL_ONLY_CAR)
            else
                SHOW_MSG(logWARN, ERR_CFG_CSL_ONLY_ONE, mdlName.c_str(), mdlIcao.c_str());
        }
        SHOW_MSG(logMSG, MSG_CFG_CSL_INSTALL);
    }
    
    // select aircraft for display
    dataRefs.ChTsOffsetReset();             // reset network time offset
    if ( !LTFlightDataShowAircraft() ) return false;

    // Now only enable multiplay lib - this acquires multiplayer planes
    //   and is the possible point of conflict with other plugins
    //   using xplanemp, so we push it out to as late as possible.
    
    // Enable Multiplayer plane drawing, acquire multiuser planes
    if (!dataRefs.IsAIonRequest())      // but only if not only on request
        LTMainToggleAI(true);
    
    // success
    dataRefs.pluginState = STATE_SHOW_AC;
    return true;
}

// Who controls AI?
std::string GetAIControlPluginName ()
{
    // XPLMCountAircraft tells us who is in control
    int total=0, active=0;
    XPLMPluginID who=0;
    XPLMCountAircraft(&total, &active, &who);
    
    // nobody?
    if (who < 0)
        return "";
    
    // get plugin info
    char whoName[256];
    XPLMGetPluginInfo(who, whoName, nullptr, nullptr, nullptr);
    return std::string(whoName);
}

/// Callback for when some other plugin released AI control
void CBRetryGetAI (void*)
{
    // We just try it again if we are still waiting
    if (dataRefs.AwaitingAIControl() &&
        !dataRefs.HaveAIUnderControl())
    {
        SHOW_MSG(logINFO, INFO_RETRY_GET_AI);
        LTMainToggleAI(true);
    }
}

// Enable Multiplayer plane drawing, acquire multiuser planes
bool LTMainTryGetAIAircraft ()
{
    // short-cut if we have control already
    if (dataRefs.HaveAIUnderControl())
        return true;
    
    // Try getting AI control, pass callback for the case we couldn't get it
    const char* cszResult = XPMPMultiplayerEnable(CBRetryGetAI);
    if ( cszResult[0] ) {
        SHOW_MSG(logWARN, "%s", cszResult);
        dataRefs.SetAwaitingAIControl(true);
        return false;
    } else if (dataRefs.HaveAIUnderControl()) {
        SHOW_MSG(logINFO, INFO_GOT_AI_CONTROL);
        dataRefs.SetAwaitingAIControl(false);
        return true;
    } else
        // Not expected to get here!
        return false;
}

/// Releasing AI/multiuser planes
void LTMainReleaseAIAircraft ()
{
    // short-cut if we aren't in control
    if (!dataRefs.HaveAIUnderControl())
        return;

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
    {
        // Don't have control...and don't want -> even cancel waiting
        if (!bGetControl) {
            dataRefs.SetAwaitingAIControl(false);
            MenuUpdateAllItemStatus();
        }
        return;
    }
    
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
    
    // disable the flight loop callback
    XPLMSetFlightLoopCallbackInterval(LoopCBAircraftMaintenance,
                                      0,            // disable
                                      1,            // relative to now
                                      NULL);
    
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


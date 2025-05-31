/// @file       LTSayIntentions.cpp
/// @brief      Channel to SayIntentions traffic map
/// @see        https://tracker.sayintentions.ai/
/// @details    Defines SayIntentionsConnection:\n
///             Takes traffic from https://lambda.sayintentions.ai/tracker/map
/// @author     Birger Hoppe
/// @copyright  (c) 2024 Birger Hoppe
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

//
// MARK: SayIntentions
//

// Constructor
SayIntentionsConnection::SayIntentionsConnection () :
LTFlightDataChannel(DR_CHANNEL_SAYINTENTIONS, SI_NAME)
{
    // purely informational
    urlName  = SI_CHECK_NAME;
    urlLink  = SI_CHECK_URL;
    urlPopup = SI_CHECK_POPUP;
}

// virtual thread main function
void SayIntentionsConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_SI", LC_ALL_MASK);
    
    while ( shallRun() ) {
        // LiveTraffic Top Level Exception Handling
        try {
            // basis for determining when to be called next
            tNextWakeup = std::chrono::steady_clock::now();
            
            // where are we right now?
            const positionTy pos (dataRefs.GetViewPos());
            
            // If the camera position is valid we can request data around it
            if (pos.isNormal()) {
                // Next wakeup is "refresh interval" from _now_
                tNextWakeup += std::chrono::seconds(dataRefs.GetFdRefreshIntvl());
                
                // fetch data and process it
                tsRequest = dataRefs.GetSimTime() + dataRefs.GetFdBufPeriod();
                if (FetchAllData(pos) && ProcessFetchedData())
                        // reduce error count if processed successfully
                        // as a chance to appear OK in the long run
                        DecErrCnt();
            }
            else {
                // Camera position is yet invalid, retry in a second
                tNextWakeup += std::chrono::seconds(1);
            }
            
            // sleep for FD_REFRESH_INTVL or if woken up for termination
            // by condition variable trigger
            {
                std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
                FDThreadSynchCV.wait_until(lk, tNextWakeup,
                                           [this]{return !shallRun();});
            }
            
        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            IncErrCnt();
        } catch (...) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown type)");
            IncErrCnt();
        }
    }
}


// returns the constant URL to SayIntentions traffic
std::string SayIntentionsConnection::GetURL (const positionTy&)
{
    return SI_URL_ALL;
}

// Process response, selecting traffic around us and forwarding to the processing queues
bool SayIntentionsConnection::ProcessFetchedData ()
{
    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) return true;
    
    // Only proceed in case HTTP response was OK
    if (httpResponse != HTTP_OK) {
        IncErrCnt();
        return false;
    }
    
    // now try to interpret it as JSON
    JSONRootPtr pRoot (netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // The entire JSON is an array of objects
    const JSON_Array* pArrAc = json_array(pRoot.get());
    if (!pArrAc) {
        LOG_MSG(logERR, "Expected a JSON Array, but got type %d",
                (int)json_type(pRoot.get()));
        IncErrCnt();
        return false;
    }
    
    // We need to calculate distance to current camera later on
    const positionTy viewPos = dataRefs.GetViewPos();
    // any a/c filter defined for debugging purposes?
    const std::string acFilter ( dataRefs.GetDebugAcFilter() );

    // Process all flights
    for (size_t i = 0; i < json_array_get_count(pArrAc); ++i)
    {
        const JSON_Object* pAc = json_array_get_object(pArrAc, i);
        if (!pAc) continue;
        
        // Displayname is matching? My own flight! -> Skip it
        if (jog_s(pAc, SI_DISPLAYNAME) == dataRefs.GetSIDisplayName())
            continue;

        // Key is the SayIntentions flight_id
        LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_SAYINTENTIONS,
                                     (unsigned long)jog_l(pAc, SI_KEY));
        // not matching a/c filter? -> skip it
        if (!acFilter.empty() && (fdKey != acFilter))
            continue;
        
        // Position
        positionTy pos (jog_n_nan(pAc, SI_LAT),
                        jog_n_nan(pAc, SI_LON),
                        jog_l(pAc, SI_ALT) * M_per_FT,
                        tsRequest,
                        jog_l(pAc, SI_HEADING));
        
        // SI returns all world's traffic, we restrict to what's within defined limits
        const double dist = pos.dist(viewPos);
        if (dist > dataRefs.GetFdStdDistance_m() )
            continue;
        
        // On ground?
        if (jog_l(pAc, SI_ALT_AGL) <= 0)
            pos.f.onGrnd = GND_ON;

        // from here on access to fdMap guarded by a mutex
        // until FD object is inserted and updated
        std::unique_lock<std::mutex> mapFdLock (mapFdMutex);

        // get the fd object from the map, key is the flight_id,
        // this fetches an existing or, if not existing, creates a new one
        LTFlightData& fd = mapFd[fdKey];

        // also get the data access lock once and for all
        // so following fetch/update calls only make quick recursive calls
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        // now that we have the detail lock we can release the global one
        mapFdLock.unlock();

        // completely new? fill key fields
        if ( fd.empty() )
            fd.SetKey(fdKey);
        
        // -- fill static data --
        LTFlightData::FDStaticData stat;
        stat.reg            = jog_s(pAc, SI_REG);
        stat.acTypeIcao     = jog_s(pAc, SI_AC_TYPE);
        stat.call           = jog_s(pAc, SI_CALL);      // try machine-readable field first
        if (stat.call.empty())                          // if empty try the 'spoken' version
            stat.call       = UnprocessCallSign(jog_s(pAc, SI_CALL_SPOKEN));
        stat.setOrigDest(jog_s(pAc, SI_ORIGIN),
                         jog_s(pAc, SI_DEST));
        stat.flight         = jog_s(pAc, SI_DISPLAYNAME);
        
        // -- dynamic data --
        LTFlightData::FDDynamicData dyn;
        dyn.gnd             = pos.IsOnGnd();
        dyn.heading         = pos.heading();
        dyn.spd             = jog_n_nan(pAc, SI_SPD);
        dyn.ts              = pos.ts();
        dyn.pChannel        = this;
        
        // update the a/c's master data
        fd.UpdateData(std::move(stat), dist);
        
        // position is rather important, we check for validity
        if ( pos.isNormal(true) ) {
            fd.AddDynData(dyn, 0, 0, &pos);
        }
        else
            LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
    }
    
    // all good
    return true;
}



/// Mapping table of keywords we can replace in the callsign
static std::map<std::string, std::string> mapTokens = {
    { "alfa",       "A" },
    { "alpha",      "A" },
    { "bravo",      "B" },
    { "charlie",    "C" },
    { "delta",      "D" },
    { "echo",       "E" },
    { "foxtrot",    "F" },
    { "golf",       "G" },
    { "hotel",      "H" },
    { "india",      "I" },
    { "juliett",    "J" },
    { "kilo",       "K" },
    { "lima",       "L" },
    { "mike",       "M" },
    { "november",   "N" },
    { "oscar",      "O" },
    { "papa",       "P" },
    { "quebec",     "Q" },
    { "romeo",      "R" },
    { "sierra",     "S" },
    { "tango",      "T" },
    { "uniform",    "U" },
    { "victor",     "V" },
    { "whiskey",    "W" },
    { "xray",       "X" },
    { "yankee",     "Y" },
    { "zulu",       "Z" },
    { "zero",       "0" },
    { "one",        "1" },
    { "two",        "2" },
    { "three",      "3" },
    { "four",       "4" },
    { "five",       "5" },
    { "six",        "6" },
    { "seven",      "7" },
    { "eight",      "8" },
    { "nine",       "9" },
    { "niner",      "9" },
    { "0",          "0" },
    { "1",          "1" },
    { "2",          "2" },
    { "3",          "3" },
    { "4",          "4" },
    { "5",          "5" },
    { "6",          "6" },
    { "7",          "7" },
    { "8",          "8" },
    { "9",          "9" },
};

// Converts "Piper-two-Five-Seven-papa" back to "Piper 257P"
std::string SayIntentionsConnection::UnprocessCallSign (std::string cs)
{
    // The call sign can be extended by something that looks like a tail number, remove that
    cs = cs.substr(0, cs.find(" "));
    
    // Cut it into pieces by the dash
    std::vector<std::string> t = str_tokenize(cs, "-");
    
    // Walk the tokens and let's see what we can convert
    std::string ret;
    for (const std::string& s: t)
    {
        try {
            ret += mapTokens.at(str_tolower_c(s));  // replace the token
        }
        catch (...)                             // didn't find token in map
        {
            if (!ret.empty()) ret += ' ';
            ret += s;                           // add it as is
            ret += ' ';                         // and separate by space
        }
    }
    
    // Remove a trailing space
    if (!ret.empty() && ret.back() == ' ')
        ret.pop_back();
    
    return ret;
}

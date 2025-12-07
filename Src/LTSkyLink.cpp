/// @file       LTSkyLink.cpp
/// @brief      SkyLink: Requests and processes live tracking data
/// @see        https://api.skylinkapi.com/docs
///             RAPID API: https://rapidapi.com/skylink-api-skylink-api-default/api/skylink-api
/// @details    Defines a base class handling the SkyLink data format,
/// @details    Defines SkyLinkConnection:\n
///             - Handles the API key\n
///             - Provides a proper REST-conform URL for both the original sevrer as well as for the Rapid API server.
/// @author     Birger Hoppe
/// @copyright  (c) 2025 Birger Hoppe
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
// MARK: SkyLink
//

SkyLinkConnection::SkyLinkConnection () :
LTFlightDataChannel(DR_CHANNEL_SKY_LINK, SKYLINK_NAME)
{
    // purely informational
    urlName  = SKYLINK_CHECK_NAME;
    urlLink  = SKYLINK_CHECK_URL;
    urlPopup = SKYLINK_CHECK_POPUP;
}


// put together the URL to fetch based on current view position
std::string SkyLinkConnection::GetURL (const positionTy& pos)
{
    char url[128] = "";
    snprintf(url, sizeof(url), SKYLINK_RAPIDAPI_URL, pos.lat(), pos.lon(),
             dataRefs.GetFdStdDistance_km());
    return std::string(url);
}


// get status info, including remaining requests
std::string SkyLinkConnection::GetStatusText () const
{
    std::string s = LTChannel::GetStatusText();
    if (IsValid() && IsEnabled() && dataRefs.SkyLinkRLimit > 0)
    {
        char t[25] = "";
        s += " | ";
        s += std::to_string(dataRefs.SkyLinkRRemain);
        s += " of ";
        s += std::to_string(dataRefs.SkyLinkRLimit);
        s += " RAPID API requests left, resets in ";
        if (dataRefs.SkyLinkRReset > 48*60*60)           // more than 2 days
            snprintf(t, sizeof(t), "%.1f days",
                     float(dataRefs.SkyLinkRReset) / (24.0f*60.0f*60.0f));
        else if (dataRefs.SkyLinkRReset >  2*60*60)      // more than 2 hours
            snprintf(t, sizeof(t), "%.1f hours",
                     float(dataRefs.SkyLinkRReset) / (      60.0f*60.0f));
        else                                            // less than 2 hours
            snprintf(t, sizeof(t), "%.1f minutes",
                     float(dataRefs.SkyLinkRReset) / (            60.0f));
        s += t;
    }
    return s;
}


// virtual thread main function
void SkyLinkConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_SkyLink", LC_ALL_MASK);
    
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

// update shared flight data structures with received flight data
bool SkyLinkConnection::ProcessFetchedData ()
{
    // received an UNAUTHOIZRED response? Then the key is invalid!
    if (httpResponse != HTTP_OK) {
        const std::string msg = netDataPos ? FetchDetail(netData).c_str() : "(no msg)";
        switch (httpResponse) {
            case HTTP_UNAUTHORIZED:             // authorization issues!
            case HTTP_FORBIDDEN:
                SHOW_MSG(logERR, ERR_SKYLINK_KEY_FAILED, msg.c_str());
                SetValid(false);
                return false;
            default:                            // some other error
                LOG_MSG(logERR, ERR_SKYLINK_OTHER, httpResponse, msg.c_str());
                IncErrCnt();
                return false;
        }
    }

    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) {
        IncErrCnt();
        return false;
    }
    
    // now try to interpret it as JSON
    JSONRootPtr pRoot (netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot.get());
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    
    // We need to calculate distance to current camera later on
    const positionTy viewPos = dataRefs.GetViewPos();
    
    // Cut-off time: We ignore tracking data, which is older than our buffering time
    const double now = double(std::time(nullptr));
    const double tCutOff = now - double(dataRefs.GetFdBufPeriod());
    
    // any a/c filter defined for debugging purposes?
    const std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // iterate all aircraft in the received flight data (can be 0 or even pJAcList == NULL!)
    JSON_Array* pJAcList = json_object_get_array(pObj, SKYLINK_AIRCRAFT_ARR);
    for ( size_t i=0; pJAcList && (i < json_array_get_count(pJAcList)); i++ )
    {
        // get the aircraft
        JSON_Object* pJAc = json_array_get_object(pJAcList,i);
        if (!pJAc) {
            LOG_MSG(logERR,ERR_JSON_AC,i+1,SKYLINK_AIRCRAFT_ARR);
            if (IncErrCnt())
                continue;
            else
                return false;
        }
        
        // ICAO key
        std::string hexKey = jog_s(pJAc, SKYLINK_KEY_ICAO);
        if (hexKey.empty()) {
            // no key doesn't work
            LOG_MSG(logERR, "Found no ICAO key in %ld. aircraft!", (long)i);
            if (!IncErrCnt())               // exit in case of too many errors
                return false;
            // but definitely skip this aircraft record
            continue;
        }
        LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO, hexKey);
        
        // not matching a/c filter? -> skip it
        if (!acFilter.empty() && (fdKey != acFilter))
            continue;

        // Process the details
        try {
            // Timestamp
            std::string s = jog_s(pJAc, SKYLINK_TIMESTAMP);
            double ts = now;
            if (!s.empty()) {
                try {
                    ts = std::chrono::duration<double>(parse_iso8601_utc(s).time_since_epoch()).count();
                    // Ignore if older than buffering cut-off
                    if (ts < tCutOff)
                        continue;
                }
                catch (...) {
                    LOG_MSG(logDEBUG, "Couldn't parse timestamp '%s', assuming 'now'",
                            s.c_str());
                    ts = now;
                }
            }
            
            // Altitude
            double alt_m = jog_n_nan(pJAc, SKYLINK_ALT) * M_per_FT;
            alt_m = BaroAltToGeoAlt_m(alt_m, dataRefs.GetPressureHPA());
            
            // Try getting best possible position information
            // Some fields can come back NAN if not defined
            positionTy pos (jog_n_nan(pJAc, SKYLINK_LAT),
                            jog_n_nan(pJAc, SKYLINK_LON),
                            alt_m,
                            ts,
                            jog_n_nan(pJAc, SKYLINK_TRACK));
            
            // On the ground?
            if (jog_b(pJAc, SKYLINK_ON_GND)) {
                pos.f.onGrnd = GND_ON;
                pos.alt_m() = NAN;              // to be determined based on scenery later
            } else {
                pos.f.onGrnd = GND_OFF;
            }
            
            // Ignore too far away planes
            const double dist = pos.dist(viewPos);
            if (dist > dataRefs.GetFdStdDistance_m() )
                continue;

            // from here on access to fdMap guarded by a mutex
            // until FD object is inserted and updated
            std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
            
            // get the fd object from the map, key is the transpIcao
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
            stat.reg =          jog_s(pJAc, SKYLINK_REG);
            stat.acTypeIcao =   jog_s(pJAc, SKYLINK_AC_TYPE_ICAO);
            stat.call =         jog_s(pJAc, SKYLINK_CALL);
            stat.op =           jog_s(pJAc, SKYLINK_AIRLINE);
            
            // -- dynamic data --
            LTFlightData::FDDynamicData dyn;
            
            // non-positional dynamic data
            dyn.gnd =               pos.IsOnGnd();
            dyn.heading =           pos.heading();
            dyn.spd =               jog_n_nan(pJAc, SKYLINK_SPD);
            dyn.vsi =               jog_n_nan(pJAc, SKYLINK_VSI);
            dyn.ts =                pos.ts();
            dyn.pChannel =          this;
            
            // update the a/c's master data
            fd.UpdateData(std::move(stat), dist);
            
            // position is rather important, we check for validity
            if ( pos.isNormal(true) ) {
                fd.AddDynData(dyn, 0, 0, &pos);
            }
            else
                LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        } catch(...) {
            LOG_MSG(logERR, "Exception while processing data for '%s'", hexKey.c_str());
        }
    }
    
    // success
    return true;
}


// Return the 'detail' or 'error'  content, if any
std::string SkyLinkConnection::FetchDetail (const char* buf)
{
    // try to interpret it as JSON, then fetch 'msg' field content
    JSONRootPtr pRoot (buf);
    if (!pRoot) return std::string();
    const JSON_Object* pObj = json_object(pRoot.get());
    if (!pObj) return std::string();
    
    // Check various possibilities for text messages
    std::string sMsg = jog_s(pObj, RAPIDAPI_MESSAGE);       // try 'message' first
    std::string s = jog_s(pObj, SKYLINK_DETAIL);            // try 'detail' next
    if (!s.empty()) {
        if (!sMsg.empty()) sMsg += " / ";
        sMsg += s;
    }
    s = jog_s(pObj, SKYLINK_ERROR);                         // try 'error' last
    if (!s.empty()) {
        if (!sMsg.empty()) sMsg += " / ";
        sMsg += s;
    }
    
    // Last resort, if we still have nothing, but there is _some_ "detail": copy the actual response back
    if (sMsg.empty() &&
        json_object_has_value(pObj, SKYLINK_DETAIL)) {
        sMsg = buf;
        if (sMsg.length() > 150) {
            sMsg.resize(150);
            sMsg += "...";
        }
    }
    
    return sMsg;
}


// add/cleanup API key
// (this is actually called prior to each request, so quite often)
bool SkyLinkConnection::InitCurl ()
{
    // we require an API key
    const std::string theKey (dataRefs.GetSkyLinkAPIKey());
    if (theKey.empty()) {
        apiKey.clear();
        SHOW_MSG(logERR, ERR_SKYLINK_NO_KEY_DEF);
        SetValid(false);
        return false;
    }
    
    // let's do the standard CURL init first
    if (!LTOnlineChannel::InitCurl())
        return false;

    // instruct to process response's headers
    curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, ReceiveHeader);

    // did the API key change?
    if (!slistKey || theKey != apiKey) {
        apiKey = theKey;
        CurlCleanupSlist(slistKey);
        slistKey = MakeCurlSList(apiKey);
    }
    
    // now add/overwrite the key
    LOG_ASSERT(slistKey);
    curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, slistKey);
    return true;
}

void SkyLinkConnection::CleanupCurl ()
{
    LTOnlineChannel::CleanupCurl();
    CurlCleanupSlist(slistKey);
}

// make list of HTTP header fields
struct curl_slist* SkyLinkConnection::MakeCurlSList (const std::string theKey)
{
    struct curl_slist* slist = curl_slist_append(NULL, SKYLINK_RAPIDAPI_HOST);
    return curl_slist_append(slist, (std::string(SKYLINK_RAPIDAPI_KEY)+theKey).c_str());
}

// read header and parse for request limit/remaining
/// @see https://docs.rapidapi.com/docs/response-headers
size_t SkyLinkConnection::ReceiveHeader(char *buffer, size_t size, size_t nitems, void *)
{
    static size_t lenRLimit  = strlen(SKYLINK_RAPIDAPI_RLIMIT);
    static size_t lenRRemain = strlen(SKYLINK_RAPIDAPI_RREMAIN);
    static size_t lenRReset  = strlen(SKYLINK_RAPIDAPI_RESET);

    const size_t len = nitems * size;
    const std::string hdr (buffer, len);                // copy to proper string
    if (stribeginwith(hdr, SKYLINK_RAPIDAPI_RLIMIT))
        dataRefs.SkyLinkRLimit = std::atol(hdr.c_str() + lenRLimit);
    else if (stribeginwith(hdr, SKYLINK_RAPIDAPI_RREMAIN))
        dataRefs.SkyLinkRRemain = std::atol(hdr.c_str() + lenRRemain);
    else if (stribeginwith(hdr, SKYLINK_RAPIDAPI_RESET))
        dataRefs.SkyLinkRReset = std::atol(hdr.c_str() + lenRReset);

    // always say we processed everything, otherwise HTTP processing would stop!
    return len;
}

//
// MARK: Static Test for SkyLink API key
//

std::future<bool> futSKYLINKKeyValid;
volatile bool bSkyLinkKeyTestRunning = false;

//  just quickly sends one simple request to SKYLINK and checks if the response is not "NO KEY"
void SkyLinkConnection::TestAPIKey (const std::string newKey)
{
    // this is not thread-safe if called from different threads...but we don't do that
    if (bSkyLinkKeyTestRunning)
        return;
    bSkyLinkKeyTestRunning = true;
    
    // call the blocking function in a separate thread and have the result delivered via future
    futSKYLINKKeyValid = std::async(std::launch::async, DoTestAPIKey, newKey);
}

// Fetch result of last test, which is running in a separate thread
// returns if the result is available. If available, actual result is returned in bIsKeyValid
bool SkyLinkConnection::TestAPIKeyResult (bool& bIsKeyValid)
{
    // did the check not yet come back?
    if (std::future_status::ready != futSKYLINKKeyValid.wait_for(std::chrono::microseconds(0)))
        return false;
    
    // is done, return the result
    bIsKeyValid = futSKYLINKKeyValid.get();
    return true;
}


// actual test, blocks, should by called via std::async
bool SkyLinkConnection::DoTestAPIKey (const std::string newKey)
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_TestSkyLink", LC_ALL_MASK);

    bool bResult = false;
    char curl_errtxt[CURL_ERROR_SIZE];
    std::string readBuf;
    
    // differentiate based on key type
    if (newKey.empty()) return false;
    
    // initialize the CURL handle
    CURL *pCurl = curl_easy_init();
    if (!pCurl) {
        LOG_MSG(logERR,ERR_CURL_EASY_INIT);
        return false;
    }
    
    // prepare the handle with the right options
    readBuf.reserve(CURL_MAX_WRITE_SIZE);
    curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, dataRefs.GetNetwTimeoutMax());
    curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, curl_errtxt);
    curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, ReceiveHeader);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, DoTestAPIKeyCB);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &readBuf);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    curl_easy_setopt(pCurl, CURLOPT_URL, SKYLINK_RAPIDAPI_HEALTH);
    
    // prepare the additional HTTP header required for API key
    struct curl_slist* slist = MakeCurlSList(newKey);
    LOG_ASSERT(slist);
    curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, slist);
    
    // perform the HTTP get request
    CURLcode cc = CURLE_OK;
    if ( (cc=curl_easy_perform(pCurl)) != CURLE_OK )
    {
        // problem with querying revocation list?
        if (IsRevocationError(curl_errtxt)) {
            // try not to query revoke list
            curl_easy_setopt(pCurl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
            LOG_MSG(logWARN, ERR_CURL_DISABLE_REV_QU, "TestSKYLINK");
            // and just give it another try
            cc = curl_easy_perform(pCurl);
        }
        
        // if (still) error, then log error
        if (cc != CURLE_OK)
            LOG_MSG(logERR, ERR_SKYLINK_KEY_TECH, cc, curl_errtxt);
    }
    
    if (cc == CURLE_OK)
    {
        // CURL was OK, now check HTTP response code
        long httpResponse = 0;
        curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);
        
        // get 'msg'
        const std::string msg = FetchDetail(readBuf.c_str());
        
        // Check HTTP return code
        switch (httpResponse) {
            case HTTP_OK:
                // check what we received in the buffer "status" and "connected" fields
                if (readBuf.find("\"" SKYLINK_HEALTH_STATUS "\"") != std::string::npos &&
                    readBuf.find("\"" SKYLINK_HEALTH_STATUS "\"") != std::string::npos)
                {
                    // looks like a valid response containing a/c info
                    bResult = true;
                    dataRefs.SetSkyLinkAPIKey(newKey);
                    dataRefs.SetChannelEnabled(DR_CHANNEL_SKY_LINK, true);
                    SHOW_MSG(logMSG, MSG_SKYLINK_KEY_SUCCESS);
                }
                else
                {
                    // somehow an unknown answer...
                    SHOW_MSG(logERR, ERR_SKYLINK_KEY_UNKNOWN, msg.c_str());
                }
                break;
                
            case HTTP_UNAUTHORIZED:
            case HTTP_FORBIDDEN:
                SHOW_MSG(logERR, ERR_SKYLINK_KEY_FAILED, msg.c_str());
                break;

            default:
                SHOW_MSG(logERR, ERR_SKYLINK_KEY_TECH, (int)httpResponse, msg.c_str());
        }
    }
    
    // cleanup CURL handle
    curl_easy_cleanup(pCurl);
    CurlCleanupSlist(slist);
    
    bSkyLinkKeyTestRunning = false;
    return bResult;
}

size_t SkyLinkConnection::DoTestAPIKeyCB (char *ptr, size_t, size_t nmemb, void* userdata)
{
    // add buffer to our std::string
    std::string& readBuf = *reinterpret_cast<std::string*>(userdata);
    readBuf.append(ptr, nmemb);
    
    // all consumed
    return nmemb;
}

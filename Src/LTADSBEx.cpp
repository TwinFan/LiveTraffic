/// @file       LTADSBEx.cpp
/// @brief      ADS-B Exchange and adsb.fi: Requests and processes live tracking data
/// @see        https://www.adsbexchange.com/
/// @see        https://github.com/adsbfi/opendata
/// @details    Defines a base class handling the ADSBEx data format,
///             which is shared by both ADS-B Exchange and adsb.fi.
/// @details    Defines ADSBExchangeConnection:\n
///             - Handles the API key\n
///             - Provides a proper REST-conform URL for both the original sevrer as well as for the Rapid API server.
/// @details    Defines ADSBfiConnection:\n
///             - Provides a proper REST-conform URL
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2024 Birger Hoppe
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
// MARK: Base class for ADSBEx format
//

/// Return the 'msg' content, if any
std::string ADSBBase::FetchMsg (const char* buf)
{
    // try to interpret it as JSON, then fetch 'msg' field content
    JSONRootPtr pRoot (buf);
    if (!pRoot) return std::string();
    const JSON_Object* pObj = json_object(pRoot.get());
    if (!pObj) return std::string();
    const std::string s = jog_s(pObj, ADSBEX_MESSAGE);  // try 'message' first
    if (!s.empty())
        return s;
    else
        return jog_s(pObj, ADSBEX_MSG);                 // else try 'msg' as per documentation
}


// update shared flight data structures with received flight data
bool ADSBBase::ProcessFetchedData ()
{
    // received an UNAUTHOIZRED response? Then the key is invalid!
    if (httpResponse == HTTP_UNAUTHORIZED || httpResponse == HTTP_FORBIDDEN) {
        SHOW_MSG(logERR, ERR_ADSBEX_KEY_FAILED,
                 netDataPos ? FetchMsg(netData).c_str() : "");
        SetValid(false);
        return false;
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
    
    // Test for any channel-specific errors
    if (!ProcessErrors(pObj)) return false;
    
    // We need to calculate distance to current camera later on
    const positionTy viewPos = dataRefs.GetViewPos();
    
    // for determining an offset as compared to network time we need to know network time
    // Also used later to calcualte the position's timestamp
    double adsbxTime = jog_n_nan(pObj, ADSBEX_NOW);
    if (std::isnan(adsbxTime))
        adsbxTime = jog_n(pObj, ADSBEX_TIME);
    // Convert a timestamp in milliseconds to a timestamp in seconds
    if (adsbxTime > 70000000000.0)
        adsbxTime /= 1000.0;
    
    // if reasonable add this to our time offset calculation
    if (adsbxTime > JAN_FIRST_2019)
        dataRefs.ChTsOffsetAdd(adsbxTime);
    
    // Cut-off time: We ignore tracking data, which is older than our buffering time
    const double tBufPeriod = (double) dataRefs.GetFdBufPeriod();
    
    // any a/c filter defined for debugging purposes?
    const std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // let's cycle the aircraft
    // fetch the aircraft array, adsb.fi defines a different aircraft key unfortunately
    JSON_Array* pJAcList = json_object_get_array(pObj, ADSBEX_AIRCRAFT_ARR);
    if (!pJAcList)
        pJAcList = json_object_get_array(pObj, ADSBFI_AIRCRAFT_ARR);
    // iterate all aircraft in the received flight data (can be 0 or even pJAcList == NULL!)
    for ( size_t i=0; pJAcList && (i < json_array_get_count(pJAcList)); i++ )
    {
        // get the aircraft
        JSON_Object* pJAc = json_array_get_object(pJAcList,i);
        if (!pJAc) {
            LOG_MSG(logERR,ERR_JSON_AC,i+1,ADSBEX_AIRCRAFT_ARR);
            if (IncErrCnt())
                continue;
            else
                return false;
        }
        
        // try version 2 first
        std::string hexKey = jog_s(pJAc, ADSBEX_V2_TRANSP_ICAO);
        if (hexKey.empty()) {
            // not found, try version 1
            hexKey = jog_s(pJAc, ADSBEX_V1_TRANSP_ICAO);
            if (!hexKey.empty()) {
                // Hm...this could be v1 data...since v4.2 we don't process that any longer
                LOG_MSG(logWARN, "%s: Received data looks like ADSBEx v1, which is no longer supported!",
                        ChName());
                IncErrCnt();
                return false;
            }
            // Either way, this can't be processed
            continue;
        }
        
        // the key: transponder Icao code or some other code
        LTFlightData::FDKeyType keyType = LTFlightData::KEY_ICAO;
        if (hexKey.front() == '~') {        // key is a non-icao code?
            hexKey.erase(0, 1);             // remove the ~
            keyType = LTFlightData::KEY_ADSBEX;
        }
        LTFlightData::FDKeyTy fdKey (keyType, hexKey);
        
        // not matching a/c filter? -> skip it
        if (!acFilter.empty() && (fdKey != acFilter))
            continue;

        // Process the details
        try {
            ProcessV2(pJAc, fdKey, tBufPeriod, adsbxTime, viewPos);
        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        } catch(...) {
            LOG_MSG(logERR, "Exception while processing data for '%s'", hexKey.c_str());
        }
    }
    
    // success
    return true;
}


// Process v2 data
void ADSBBase::ProcessV2 (const JSON_Object* pJAc,
                          LTFlightData::FDKeyTy& fdKey,
                          const double tBufPeriod,
                          const double adsbxTime,
                          const positionTy& viewPos)
{
    // skip stale data
    const double ageOfPos = jog_n(pJAc, ADSBEX_V2_SEE_POS);
    if (ageOfPos >= tBufPeriod)
        return;
    
    // skip TIS-B data...that is stuff that comes and goes with new ids every few requests, unusable
    if (!std::strcmp(jog_s(pJAc, ADSBEX_V2_TRANSP_TYPE),ADSBEX_V2_TYPE_TISB))
        return;
    
    // Try getting best possible position information
    // Some fields can come back NAN if not defined
    positionTy pos (jog_n_nan(pJAc, ADSBEX_V2_LAT),
                    jog_n_nan(pJAc, ADSBEX_V2_LON),
                    jog_n_nan(pJAc, ADSBEX_V2_ALT_GEOM) * M_per_FT,
                    adsbxTime - ageOfPos,
                    jog_n_nan(pJAc, ADSBEX_V2_HEADING));
    // If heading isn't available try track
    if (std::isnan(pos.heading()))
        pos.heading() = jog_n_nan(pJAc, ADSBEX_V2_TRACK);
    
    // If lat/lon isn't defined then the tracking data is stale: discard
    if (std::isnan(pos.lat()) || std::isnan(pos.lon()))
        return;
    
    // ADSBEx, especially the RAPID API version, returns
    // aircraft regardless of distance. To avoid planes
    // created and immediately removed due to distanced settings
    // we continue only if pos is within wanted range
    const double dist = pos.dist(viewPos);
    if (dist > dataRefs.GetFdStdDistance_m() )
        return;
    
    // The alt_baro field is string "ground" if on ground or can hold a baro altitude number
    const JSON_Value* pAltBaro = json_object_get_value(pJAc, ADSBEX_V2_ALT_BARO);
    if (pAltBaro) {
        switch (json_value_get_type(pAltBaro))
        {
            case JSONNumber:
            {
                pos.f.onGrnd = GND_OFF;         // we are definitely off ground
                // But we also process baro alt, potentially even overwriting a geo alt as baro alt is more accurate based on experience
                // try converting baro alt from given QNH, otherwise we use our own weather
                const double baro_alt = json_number(pAltBaro);
                const double qnh = jog_n_nan(pJAc, ADSBEX_V2_NAV_QNH);
                if (std::isnan(qnh))
                    pos.SetAltFt(BaroAltToGeoAlt_ft(baro_alt, dataRefs.GetPressureHPA()));
                else
                    pos.SetAltFt(BaroAltToGeoAlt_ft(baro_alt, qnh));
                break;
            }
                
            case JSONString:
                // There is just one string we are aware of: "ground"
                if (!strcmp(json_string(pAltBaro), "ground")) {
                    pos.f.onGrnd = GND_ON;
                    pos.alt_m() = NAN;
                }
                break;
        }
    }
    // _Some_ altitude info needs to be available now, otherwise skip data
    if (!pos.IsOnGnd() && std::isnan(pos.alt_m()))
        return;
    
    // Are we to skip static objects?
    std::string reg = jog_s(pJAc, ADSBEX_V2_REG);
    std::string acTy = jog_s(pJAc, ADSBEX_V2_AC_TYPE_ICAO);
    std::string cat = jog_s(pJAc, ADSBEX_V2_AC_CATEGORY);
    
    // Identify ground vehicles
    if (cat  == "C1"  || cat == "C2" ||
        reg  == "GND" ||
        acTy == "GND")
        acTy = dataRefs.GetDefaultCarIcaoType();
    // Mark all static objects equally, so they can optionally be hidden
    else if (reg  == STATIC_OBJECT_TYPE ||
             acTy == STATIC_OBJECT_TYPE ||
             cat  == "C3" ||
             // Everything else on the ground, which has no type, registration, category is considered static
             (pos.IsOnGnd() && acTy.empty() && reg.empty() && cat.empty()))

    {
        reg = acTy = STATIC_OBJECT_TYPE;
        cat = "C3";
    }

    // from here on access to fdMap guarded by a mutex
    // until FD object is inserted and updated
    std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
    
    // Check for duplicates with OGN/FLARM, potentially replaces the key type
    LTFlightData::CheckDupKey(fdKey, LTFlightData::KEY_FLARM);
    // Some internal codes sometimes overlap with RealTraffic
    if (fdKey.eKeyType == LTFlightData::KEY_ADSBEX)
        LTFlightData::CheckDupKey(fdKey, LTFlightData::KEY_RT);

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
    stat.reg =        reg;
    stat.acTypeIcao = acTy;
    stat.mil =        (jog_l(pJAc, ADSBEX_V2_FLAGS) & 0x01) == 0x01;
    stat.call =       jog_s(pJAc, ADSBEX_V2_FLIGHT);
    trim(stat.call);
    stat.catDescr = GetADSBEmitterCat(cat);
    stat.slug       = sSlugBase;
    stat.slug      += fdKey.key;
    
    // -- dynamic data --
    LTFlightData::FDDynamicData dyn;
    
    // non-positional dynamic data
    dyn.radar.code =        jog_sl(pJAc, ADSBEX_V2_RADAR_CODE);
    dyn.gnd =               pos.IsOnGnd();
    dyn.heading =           pos.heading();
    dyn.spd =               jog_n_nan(pJAc, ADSBEX_V2_SPD);
    dyn.vsi =               jog_n_nan(pJAc, ADSBEX_V2_VSI_GEOM);
    if (std::isnan(dyn.vsi))
        dyn.vsi =           jog_n_nan(pJAc, ADSBEX_V2_VSI_BARO);
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
}


//
// MARK: ADS-B Exchange
//

ADSBExchangeConnection::ADSBExchangeConnection () :
ADSBBase(DR_CHANNEL_ADSB_EXCHANGE_ONLINE, ADSBEX_NAME, ADSBEX_SLUG_BASE)
{
    // purely informational
    urlName  = ADSBEX_CHECK_NAME;
    urlLink  = ADSBEX_CHECK_URL;
    urlPopup = ADSBEX_CHECK_POPUP;
}


// put together the URL to fetch based on current view position
std::string ADSBExchangeConnection::GetURL (const positionTy& pos)
{
    char url[128] = "";
    snprintf(url, sizeof(url), ADSBEX_RAPIDAPI_URL, pos.lat(), pos.lon(),
             std::min(dataRefs.GetFdStdDistance_nm(), 250));    // max 250nm radius allowed
    return std::string(url);
}


// get status info, including remaining requests
std::string ADSBExchangeConnection::GetStatusText () const
{
    std::string s = LTChannel::GetStatusText();
    if (IsValid() && IsEnabled() && dataRefs.ADSBExRLimit > 0)
    {
        char t[25] = "";
        s += " | ";
        s += std::to_string(dataRefs.ADSBExRRemain);
        s += " of ";
        s += std::to_string(dataRefs.ADSBExRLimit);
        s += " RAPID API requests left, resets in ";
        if (dataRefs.ADSBExRReset > 48*60*60)           // more than 2 days
            snprintf(t, sizeof(t), "%.1f days",
                    float(dataRefs.ADSBExRReset) / (24.0f*60.0f*60.0f));
        else if (dataRefs.ADSBExRReset >  2*60*60)      // more than 2 hours
            snprintf(t, sizeof(t), "%.1f hours",
                    float(dataRefs.ADSBExRReset) / (      60.0f*60.0f));
        else                                            // less than 2 hours
            snprintf(t, sizeof(t), "%.1f minutes",
                    float(dataRefs.ADSBExRReset) / (            60.0f));
        s += t;
    }
    return s;
}


// virtual thread main function
void ADSBExchangeConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_ADSBEx", LC_ALL_MASK);
    
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


// add/cleanup API key
// (this is actually called prior to each request, so quite often)
bool ADSBExchangeConnection::InitCurl ()
{
    // we require an API key
    const std::string theKey (dataRefs.GetADSBExAPIKey());
    if (theKey.empty()) {
        apiKey.clear();
        SHOW_MSG(logERR, ERR_ADSBEX_NO_KEY_DEF);
        SetValid(false);
        return false;
    }
    
    // let's do the standard CURL init first
    if (!LTOnlineChannel::InitCurl())
        return false;

    // read headers
    curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, ReceiveHeader);

    // did the API key change?
    if (!slistKey || theKey != apiKey) {
        apiKey = theKey;
        if (slistKey) {
            curl_slist_free_all(slistKey);
            slistKey = NULL;
        }
        slistKey = MakeCurlSList(apiKey);
    }
    
    // now add/overwrite the key
    LOG_ASSERT(slistKey);
    curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, slistKey);
    return true;
}

void ADSBExchangeConnection::CleanupCurl ()
{
    LTOnlineChannel::CleanupCurl();
    curl_slist_free_all(slistKey);
    slistKey = NULL;
}

// Specific handling for authentication errors
bool ADSBExchangeConnection::ProcessErrors (const JSON_Object* pObj)
{
    // test for non-OK response in 'message' / 'msg'
    std::string errTxt = jog_s(pObj, ADSBEX_MESSAGE);
    if (errTxt.empty())
        errTxt = jog_s(pObj, ADSBEX_MSG);    
    if (!errTxt.empty() && errTxt != ADSBEX_SUCCESS)
    {
        LOG_MSG(logERR, ERR_ADSBEX_OTHER, errTxt.c_str());
        IncErrCnt();
        return false;
    }
    
    // Looks OK
    return true;
}


// make list of HTTP header fields
struct curl_slist* ADSBExchangeConnection::MakeCurlSList (const std::string theKey)
{
    struct curl_slist* slist = curl_slist_append(NULL, ADSBEX_RAPIDAPI_HOST);
    return curl_slist_append(slist, (std::string(ADSBEX_RAPIDAPI_KEY)+theKey).c_str());
}

// read header and parse for request limit/remaining
/// @see https://docs.rapidapi.com/docs/response-headers
size_t ADSBExchangeConnection::ReceiveHeader(char *buffer, size_t size, size_t nitems, void *)
{
    static size_t lenRLimit  = strlen(ADSBEX_RAPIDAPI_RLIMIT);
    static size_t lenRRemain = strlen(ADSBEX_RAPIDAPI_RREMAIN);
    static size_t lenRReset  = strlen(ADSBEX_RAPIDAPI_RESET);

    const size_t len = nitems * size;
    const std::string hdr (buffer, len);                // copy to proper string
    if (stribeginwith(hdr, ADSBEX_RAPIDAPI_RLIMIT))
        dataRefs.ADSBExRLimit = std::atol(hdr.c_str() + lenRLimit);
    else if (stribeginwith(hdr, ADSBEX_RAPIDAPI_RREMAIN))
        dataRefs.ADSBExRRemain = std::atol(hdr.c_str() + lenRRemain);
    else if (stribeginwith(hdr, ADSBEX_RAPIDAPI_RESET))
        dataRefs.ADSBExRReset = std::atol(hdr.c_str() + lenRReset);

    // always say we processed everything, otherwise HTTP processing would stop!
    return len;
}

//
// MARK: Static Test for ADSBEx key
//

std::future<bool> futADSBExKeyValid;
bool bADSBExKeyTestRunning = false;

//  just quickly sends one simple request to ADSBEx and checks if the response is not "NO KEY"
void ADSBExchangeConnection::TestADSBExAPIKey (const std::string newKey)
{
    // this is not thread-safe if called from different threads...but we don't do that
    if (bADSBExKeyTestRunning)
        return;
    bADSBExKeyTestRunning = true;
    
    // call the blocking function in a separate thread and have the result delivered via future
    futADSBExKeyValid = std::async(std::launch::async, DoTestADSBExAPIKey, newKey);
}

// Fetch result of last test, which is running in a separate thread
// returns if the result is available. If available, actual result is returned in bIsKeyValid
bool ADSBExchangeConnection::TestADSBExAPIKeyResult (bool& bIsKeyValid)
{
    // did the check not yet come back?
    if (std::future_status::ready != futADSBExKeyValid.wait_for(std::chrono::microseconds(0)))
        return false;
    
    // is done, return the result
    bIsKeyValid = futADSBExKeyValid.get();
    return true;
}


// actual test, blocks, should by called via std::async
bool ADSBExchangeConnection::DoTestADSBExAPIKey (const std::string newKey)
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_TestADSBEx", LC_ALL_MASK);

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
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, DoTestADSBExAPIKeyCB);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &readBuf);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    curl_easy_setopt(pCurl, CURLOPT_URL, ADSBEX_VERIFY_RAPIDAPI);
    
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
            LOG_MSG(logWARN, ERR_CURL_DISABLE_REV_QU, LT_DOWNLOAD_CH);
            // and just give it another try
            cc = curl_easy_perform(pCurl);
        }
        
        // if (still) error, then log error
        if (cc != CURLE_OK)
            LOG_MSG(logERR, ERR_ADSBEX_KEY_TECH, cc, curl_errtxt);
    }
    
    if (cc == CURLE_OK)
    {
        // CURL was OK, now check HTTP response code
        long httpResponse = 0;
        curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);
        
        // get 'msg'
        const std::string msg = FetchMsg(readBuf.c_str());
        
        // Check HTTP return code
        switch (httpResponse) {
            case HTTP_OK:
                // check what we received in the buffer: an "ac" array, or both 'total' and 'now'?
                if (readBuf.find("\"" ADSBEX_AIRCRAFT_ARR "\"") != std::string::npos ||
                         (readBuf.find(ADSBEX_TOTAL) != std::string::npos &&
                          readBuf.find(ADSBEX_NOW) != std::string::npos))
                {
                    // looks like a valid response containing a/c info
                    bResult = true;
                    dataRefs.SetADSBExAPIKey(newKey);
                    dataRefs.SetChannelEnabled(DR_CHANNEL_ADSB_EXCHANGE_ONLINE, true);
                    SHOW_MSG(logMSG, MSG_ADSBEX_KEY_SUCCESS);
                }
                else
                {
                    // somehow an unknown answer...
                    SHOW_MSG(logERR, ERR_ADSBEX_KEY_UNKNOWN, msg.c_str());
                }
                break;
                
            case HTTP_UNAUTHORIZED:
            case HTTP_FORBIDDEN:
                SHOW_MSG(logERR, ERR_ADSBEX_KEY_FAILED, msg.c_str());
                break;

            default:
                SHOW_MSG(logERR, ERR_ADSBEX_KEY_TECH, (int)httpResponse, msg.c_str());
        }
    }
    
    // cleanup CURL handle
    curl_easy_cleanup(pCurl);
    curl_slist_free_all(slist);
    
    bADSBExKeyTestRunning = false;
    return bResult;
}

size_t ADSBExchangeConnection::DoTestADSBExAPIKeyCB (char *ptr, size_t, size_t nmemb, void* userdata)
{
    // add buffer to our std::string
    std::string& readBuf = *reinterpret_cast<std::string*>(userdata);
    readBuf.append(ptr, nmemb);
    
    // all consumed
    return nmemb;
}

//
// MARK: adsb.fi
//

ADSBfiConnection::ADSBfiConnection () :
ADSBBase(DR_CHANNEL_ADSB_FI_ONLINE, ADSBFI_NAME, ADSBFI_SLUG_BASE)
{
    // purely informational
    urlName  = ADSBFI_CHECK_NAME;
    urlLink  = ADSBFI_CHECK_URL;
    urlPopup = ADSBFI_CHECK_POPUP;
}


// put together the URL to fetch based on current view position
std::string ADSBfiConnection::GetURL (const positionTy& pos)
{
    char url[128] = "";
    snprintf(url, sizeof(url), ADSBFI_URL, pos.lat(), pos.lon(),
             dataRefs.GetFdStdDistance_nm());
    return std::string(url);
}


// virtual thread main function
void ADSBfiConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_adsb_fi", LC_ALL_MASK);
    
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

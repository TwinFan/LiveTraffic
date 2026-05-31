/// @file       LTNavigraph.cpp
/// @brief      Navigraph/Flightradar24: Requests and processes live tracking data
/// @see        https://navigraph.com/blog/navigraph-flightradar24
/// @see        https://developers.navigraph.com/docs/authentication/device-authorization
/// @details    Implements NvgrFR24Connection:\n
///             - Handles the OAuth authentication protocol
///             - Provides a proper REST-conform URL\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
/// @details    The following are request rate limits as informed by Navigraph
///             ≤150 km = 1 req / 20 s
///             ≤300 km = 1 req / 40 s
///             ≤500 km (or beyond) = 1 req / 60 s
/// @author     Birger Hoppe
/// @copyright  (c) 2026 Birger Hoppe
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

// All includes are collected in one header
#include "LiveTraffic.h"

//
// MARK: Navigraph
//

// We need a client id/secret coming in from the build command line, or we just don't do Navigraph
#if !defined(NVGR_CLIENT_SECRET) || !defined(NVGR_CLIENT_ID)
#error NVGR_CLIENT_SECRET/NVGR_CLIENT_ID not defined! At least define them to be "INOP".
#endif

static const std::string gsNvgrClientId(NVGR_CLIENT_ID);
static const std::string gsNvgrClientSecret(NVGR_CLIENT_SECRET);

#undef NVGR_CLIENT_ID
#undef NVGR_CLIENT_SECRET

// Constructor
NvgrFR24Connection::NvgrFR24Connection () :
LTFlightDataChannel(DR_CHANNEL_NVGR_FR24, OPSKY_NAME)
{
    // purely informational
    urlName  = OPSKY_CHECK_NAME;
    urlLink  = OPSKY_CHECK_URL;
    urlPopup = OPSKY_CHECK_POPUP;
}


// used to force fetching a new token, e.g. after change of credentials
void NvgrFR24Connection::ResetStatus ()
{
    sErrMsg.clear();
    eState = NVGR_STATE_NONE;
}


// virtual thread main function
void NvgrFR24Connection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_Navigraph", LC_ALL_MASK);
    
    // Reset state
    ResetStatus();
    
    // Can't run if we don't have Client Secret/ID
    if (!IsBuiltIn()) {
        SHOW_MSG(logERR, "No Navigraph support built into this binary, can't start Navigraph/FR24");
        SetValid(false,false); SetEnable(false);
    }
    // Can't run if we don't have Refresh Token
    else if (!dataRefs.HaveNvgrRefreshToken()) {
        SHOW_MSG(logERR, "No Navigraph Refresh Token available. You need to authorize LiveTraffic first with Navigraph, see Settings.");
        SetValid(false,false); SetEnable(false);
    }

    while ( shallRun() ) {
        // LiveTraffic Top Level Exception Handling
        try {
            // basis for determining when to be called next
            tNextWakeup = std::chrono::steady_clock::now();
            
            // where are we right now?
            const positionTy pos (dataRefs.GetViewPos());
            
            // If the camera position is valid we can request data around it
            if (pos.isNormal()) {
                // fetch data and process it
                if (FetchAllData(pos) && ProcessFetchedData())
                        // reduce error count if processed successfully
                        // as a chance to appear OK in the long run
                        DecErrCnt();
                
                // Next Wakeup:
                // TODO: Fetch new tokens periodically
                // If we were fetching the access token only, then we continue immediately (don't add to tNextWakeup)...
                if (eState == NVGR_STATE_GETTING_TOKEN)
                    // ...fetching planes
                    eState = NVGR_STATE_GET_PLANES;
                else
                    // Next wakeup is "refresh interval" from _now_,
                    // however a minimum of 20s as imposed by Navigraph
                    tNextWakeup += std::chrono::seconds(std::max(dataRefs.GetFdRefreshIntvl(), NVGR_MIN_REFRESH_INTVL));
            }
            else {
                // Camera position is yet invalid, retry in a second
                tNextWakeup += std::chrono::seconds(1);
            }
            
            // sleep until scheduled wakeup or if woken up for termination
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
    
    // Cleanup
    CurlCleanupSlist(pHdrForm);
    CurlCleanupSlist(pHdrToken);
}


// Initialize CURL, adding OpenSky credentials
bool NvgrFR24Connection::InitCurl ()
{
    // Standard-init first (repeated call will just return true without effect)
    if (!LTOnlineChannel::InitCurl())
        return false;
    
    // Do we have a token that is about to expire and needs a refresh?
    if (!std::isnan(tTokenExpiration) &&
        dataRefs.GetMiscNetwTime() >= tTokenExpiration)
    {
        ResetStatus();
    }
    
    // The request we are about to send depends on our state
    // Initially, decide if we go for token or unauthenticated:
    if (eState == NVGR_STATE_NONE) {
        CurlCleanupSlist(pHdrToken);                    // clear token information
        tTokenExpiration = NAN;
        
        eState = NVGR_STATE_GETTING_TOKEN;              // need fresh token
    }
    
    // if fetching token then we need to set the content type
    if (eState == NVGR_STATE_GETTING_TOKEN) {
        // create the header list if it doesn't exist yet
        if (!pHdrForm) {
            pHdrForm = curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded");
        }
        curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHdrForm);
    }
    else {
        // in all other cases we may, if defined, set the access token header
        curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHdrToken);
    }
    return true;
}

// put together the URL to fetch based on current view position
std::string NvgrFR24Connection::GetURL (const positionTy& pos)
{
    // Do we need a token? Let's go for one:
    if (eState == NVGR_STATE_GETTING_TOKEN) {
        LOG_MSG(logDEBUG, "Refreshing token...");
        return NVGR_TOKEN_URL;
    }
    
    // Standard request to fetch planes:
    // TODO: 180km limit for 20s refresh period
    char url[128] = "";
    snprintf(url, sizeof(url),
             NVGR_TRAFFIC_URL,
             pos.lat(), pos.lon(),
             dataRefs.GetFdStdDistance_km());
    return std::string(url);
}

// only needed for token request, will then form token request body
void NvgrFR24Connection::ComputeBody (const positionTy& /*pos*/)
{
    if (eState == NVGR_STATE_GETTING_TOKEN) {
        // if we are to fetch a token then we need to put credentials into the body
        char s[256];
        snprintf(s, sizeof(s), NVGR_TOKEN_REFRESH_BODY,
                 gsNvgrClientId.c_str(), gsNvgrClientSecret.c_str(),
                 dataRefs.GetNvgrRefreshToken().c_str());
        requBody = s;
    }
    else {
        // in all other case we don't have a body and will send a GET request
        requBody.clear();
    }
}


// Tries to interpret pBuf as JSON and looks for "error" or similar
std::string NvgrFR24Connection::TryExtractErrorMsg (const JSON_Object* pMain)
{
    if (!pMain) return "";
    
    std::string s = jog_s(pMain, "error");
    return s;
}


// update shared flight data structures with received flight data
bool NvgrFR24Connection::ProcessFetchedData ()
{
    char buf[256];
    
    // Try to interpret response as JSON, might contain error information
    JSONRootPtr pRoot (netData);
    JSON_Object* pObj = pRoot ? json_object(pRoot.get()) : nullptr;
    std::string errMsg = TryExtractErrorMsg(pObj);
    
    // Only proceed in case HTTP response was OK
    switch (httpResponse)
    {
        // All OK
        case HTTP_OK:
            sErrMsg.clear();
            break;
            
        // Unauthorized? Also wrong token, or wrong credentials when trying to get the token
        case HTTP_BAD_REQUEST:
        case HTTP_UNAUTHORIZED:     // No valid access token
            if (eState == NVGR_STATE_GETTING_TOKEN) {
                sErrMsg = "Authorization failed: " + errMsg;
                SHOW_MSG(logERR, "%s: Authorization failed: %s",
                         pszChName, errMsg.c_str());
                SetValid(false,false);
                SetEnable(false);       // also disable to directly allow user/pwd change...and won't work on retry anyway
                return false;
            }
            else {
                sErrMsg = "Authorization failed or timed out, trying to get a new access token...";
                LOG_MSG(logERR, "%s: Bad or timed-out access token: %s",
                        pszChName, errMsg.c_str());
                ResetStatus();          // let's try with a new one
                IncErrCnt();
                return false;
            }

        // anything else is serious and treated as some problem
        default:
            IncErrCnt();
            return false;
    }
    
    // data is expected to be in netData string
    if ( !netDataPos ) {
        LOG_MSG(logERR, "No actual data received!");
        IncErrCnt();
        return false;
    }
    
    // --- Token ---
    // is this a token response?
    if (eState == NVGR_STATE_GETTING_TOKEN) {
        // Now we do need a JSON body!
        if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
        if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
        
        // Find the access token and type, that's required
        const std::string sToken = jog_s(pObj, NVGR_TOKEN_ACCESS);
        const std::string sType  = jog_s(pObj, NVGR_TOKEN_TYPE);
        if (sToken.empty() || sType.empty()) {
            LOG_MSG(logERR,"Token response is missing the %s or %s fields:\n%s",
                    NVGR_TOKEN_ACCESS, NVGR_TOKEN_TYPE, netData);
            IncErrCnt();
            return false;
        }
        
        // If we get a refresh token, too, we save that in the settings
        const std::string sRefresh = jog_s(pObj, NVGR_TOKEN_REFRESH);
        if (!sRefresh.empty())
            dataRefs.SetNvrgRefrshToken(sRefresh);
        
        // If we get a timeout we use that, otherwise a default
        long nTimeout = jog_l(pObj, NVGR_TOKEN_EXPIRES);
        if (!nTimeout) nTimeout = NVGR_AUTH_EXP_DEFAULT;
        tTokenExpiration = dataRefs.GetMiscNetwTime() + float(nTimeout - 2 * dataRefs.GetFdRefreshIntvl());
        
        // prepend the token with the header string and create the actual header list
        snprintf(buf, sizeof(buf), NVGR_AUTH_HEADER,
                 sType.c_str(), sToken.c_str());
        CurlCleanupSlist(pHdrToken);
        pHdrToken = curl_slist_append(nullptr, buf);

        return true;
    }
    
    // --- Planes ---
    // TODO: Implement
    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // Cut-off time: We ignore tracking data, which is "in the past" compared to simTime
    const double tsCutOff = dataRefs.GetSimTime();

    // We need to calculate distance to current camera later on
    const positionTy viewPos = dataRefs.GetViewPos();
/*
    // fetch the aircraft array
    JSON_Array* pJAcList = json_object_get_array(pObj, OPSKY_AIRCRAFT_ARR);
    if (!pJAcList) {
        // a/c array not found: can just mean it is 'null' as in
        // the empty result set: {"time":1541978120,"states":null}
        JSON_Value* pJSONVal = json_object_get_value(pObj, OPSKY_AIRCRAFT_ARR);
        if (!pJSONVal || json_type(pJSONVal) != JSONNull) {
            // well...it is something else, so it is malformed, bail out
            LOG_MSG(logERR,ERR_JSON_ACLIST,OPSKY_AIRCRAFT_ARR);
            IncErrCnt();
            return false;
        }
    }
    // iterate all aircraft in the received flight data (can be 0)
    else for ( size_t i=0; i < json_array_get_count(pJAcList); i++ )
    {
        // get the aircraft (which is just an array of values)
        JSON_Array* pJAc = json_array_get_array(pJAcList,i);
        if (!pJAc) {
            LOG_MSG(logERR,ERR_JSON_AC,i+1,OPSKY_AIRCRAFT_ARR);
            if (IncErrCnt())
                continue;
            else
                return false;
        }
        
        // the key: transponder Icao code
        LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO,
                                     jag_s(pJAc, OPSKY_TRANSP_ICAO));
        
        // not matching a/c filter? -> skip it
        if ((!acFilter.empty() && (fdKey != acFilter)) )
        {
            continue;
        }
        
        // position time
        const double posTime = jag_n(pJAc, OPSKY_POS_TIME);
        if (posTime <= tsCutOff)
            continue;
        
        try {
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
            
            // fill static data
            LTFlightData::FDStaticData stat;
            stat.country =    jag_s(pJAc, OPSKY_COUNTRY);
            stat.call    =    jag_s(pJAc, OPSKY_CALL);
            while (!stat.call.empty() && stat.call.back() == ' ')      // trim trailing spaces
                stat.call.pop_back();
            if (!fdKey.empty()) {
                snprintf(buf, sizeof(buf), OPSKY_SLUG_FMT, fdKey.num);
                stat.slug = buf;
            }
            
            // dynamic data
            {   // unconditional...block is only for limiting local variables
                LTFlightData::FDDynamicData dyn;
                
                // non-positional dynamic data
                dyn.radar.code =  (long)jag_sn(pJAc, OPSKY_RADAR_CODE);
                dyn.gnd =               jag_b(pJAc, OPSKY_GND);
                dyn.heading =           jag_n_nan(pJAc, OPSKY_HEADING);
                dyn.spd =               jag_n(pJAc, OPSKY_SPD);
                dyn.vsi =               jag_n(pJAc, OPSKY_VSI);
                dyn.ts =                posTime;
                dyn.pChannel =          this;
                
                // position
                const double baroAlt_m = jag_n_nan(pJAc, OPSKY_BARO_ALT);
                const double geoAlt_m = BaroAltToGeoAlt_m(baroAlt_m, dataRefs.GetPressureHPA());
                positionTy pos (jag_n_nan(pJAc, OPSKY_LAT),
                                jag_n_nan(pJAc, OPSKY_LON),
                                geoAlt_m,
                                posTime,
                                dyn.heading);
                pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;
                
                // Update static data
                fd.UpdateData(std::move(stat), pos.dist(viewPos));

                // position is rather important, we check for validity
                // (we do allow alt=NAN if on ground as this is what OpenSky returns)
                if ( pos.isNormal(true) )
                    fd.AddDynData(dyn, 0, 0, &pos);
                else
                    LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
            }
        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        }
    }
    */
    // success
    return true;
}


// get status info, including remaining requests
std::string NvgrFR24Connection::GetStatusText () const
{
    if (!IsBuiltIn())
        return "No support for Navigraph built into this binary";
    
    std::string s =
        eState == NVGR_STATE_GETTING_TOKEN ? "Getting access token..." : LTChannel::GetStatusText();
    if (!sErrMsg.empty()) {
        s += " | ";
        s += sErrMsg;
    }

    return s;
}

// Is Navigraph support built in, i.e. do we have a proper client secret/id?
bool NvgrFR24Connection::IsBuiltIn()
{
    static bool bBuiltIn = (gsNvgrClientId != "INOP") && (gsNvgrClientSecret != "INOP");
    return bBuiltIn;
}

/// @file       LTOpenSky.cpp
/// @brief      OpenSky Network: Requests and processes live tracking and aircraft master data
/// @see        https://opensky-network.org/
/// @details    Implements OpenSkyConnection and OpenSkyAcMasterdata:\n
///             - Provides a proper REST-conform URL\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
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

// All includes are collected in one header
#include "LiveTraffic.h"

#if IBM
#else
#include <dirent.h>
#endif

//
//MARK: OpenSky
//

// Constructor
OpenSkyConnection::OpenSkyConnection () :
LTFlightDataChannel(DR_CHANNEL_OPEN_SKY_ONLINE, OPSKY_NAME)
{
    // purely informational
    urlName  = OPSKY_CHECK_NAME;
    urlLink  = OPSKY_CHECK_URL;
    urlPopup = OPSKY_CHECK_POPUP;
}


// used to force fetching a new token, e.g. after change of credentials
void OpenSkyConnection::ResetStatus ()
{
    eState = OPSKY_STATE_NONE;
}


// virtual thread main function
void OpenSkyConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_OpSky", LC_ALL_MASK);
    
    // Reset state
    ResetStatus();
    
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
                // If we were fetching the access token only, then we continue immediately (don't add to tNextWakeup)...
                if (eState == OPSKY_STATE_GETTING_TOKEN)
                    // ...fetching planes
                    eState = OPSKY_STATE_GET_PLANES;
                else
                    // Next wakeup is "refresh interval" from _now_
                    tNextWakeup += std::chrono::seconds(dataRefs.GetFdRefreshIntvl());
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
    
    // Cleanup
    CurlCleanupSlist(pHdrForm);
    CurlCleanupSlist(pHdrToken);
}


// Initialize CURL, adding OpenSky credentials
bool OpenSkyConnection::InitCurl ()
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
    if (eState == OPSKY_STATE_NONE) {
        CurlCleanupSlist(pHdrToken);                    // clear token information
        tTokenExpiration = NAN;
        
        std::string clientId, clientSecret;             // check credentials
        dataRefs.GetOpenSkyCredentials(clientId, clientSecret);
        if (clientId.empty() || clientSecret.empty())   // don't have credentials?
            eState = OPSKY_STATE_GET_PLANES;            // use none
        else
            eState = OPSKY_STATE_GETTING_TOKEN;         // have credentials, need token
    }
    
    // if fetching token then we need to set the content type
    if (eState == OPSKY_STATE_GETTING_TOKEN) {
        // create the header list if it doesn't exist yet
        if (!pHdrForm) {
            pHdrForm = curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded");
        }
        curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHdrForm);
        curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, nullptr);
    }
    else {
        // in all other cases we may, if defined, set the access token header
        curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHdrToken);
        // want to read headers (for remaining requests info)
        curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, ReceiveHeader);
    }
    return true;
}

// read header and parse for request remaining
size_t OpenSkyConnection::ReceiveHeader(char *buffer, size_t size, size_t nitems, void *)
{
    const size_t len = nitems * size;
    static size_t lenRRemain = strlen(OPSKY_RREMAIN);
    static size_t lenRetry = strlen(OPSKY_RETRY);

    // create a copy of the header as we aren't allowed to change the buffer contents
    std::string sHdr(buffer, len);
    
    // Remaining?
    if (sHdr.length() > lenRRemain &&
        stribeginwith(sHdr, OPSKY_RREMAIN))
    {
        const long rRemain = std::atol(sHdr.c_str() + lenRRemain);
        // Issue a warning when coming close to the end
        if (rRemain != dataRefs.OpenSkyRRemain) {
            if (rRemain == 50 || rRemain == 10) {
                SHOW_MSG(logWARN, "OpenSky: Only %ld requests left today for ca. %ld minutes of data",
                         rRemain,
                         (rRemain * dataRefs.GetFdRefreshIntvl()) / 60);
            }
            dataRefs.OpenSkyRRemain = rRemain;
            if (rRemain > 0)
                dataRefs.OpenSkyRetryAt.clear();
        }
    }
    // Retry-after-seconds?
    else if (sHdr.length() > lenRetry &&
             stribeginwith(sHdr, OPSKY_RETRY))
    {
        char sTime[25];
        const long secRetry = std::atol(sHdr.c_str() + lenRetry);
        // convert that to a local timestamp for the user to use
        const std::time_t tRetry = std::time(nullptr) + secRetry;
        std::strftime(sTime, sizeof(sTime), "%d-%b %H:%M", std::localtime(&tRetry));
        dataRefs.OpenSkyRetryAt = sTime;
        dataRefs.OpenSkyRRemain = 0;
    }

    // always say we processed everything, otherwise HTTP processing would stop!
    return len;
}

// put together the URL to fetch based on current view position
std::string OpenSkyConnection::GetURL (const positionTy& pos)
{
    // Do we need a token? Let's go for one:
    if (eState == OPSKY_STATE_GETTING_TOKEN) {
        LOG_MSG(logDEBUG, "Requesting access token...");
        return OPSKY_URL_GETTOKEN;
    }
    
    // Standard request to fetch planes:
    
    // we add 10% to the bounding box to have some data ready once the plane is close enough for display
    boundingBoxTy box (pos, double(dataRefs.GetFdStdDistance_m()) * 1.10);
    char url[128] = "";
    snprintf(url, sizeof(url),
             OPSKY_URL_ALL,
             box.se.lat(),              // lamin
             box.nw.lon(),              // lomin
             box.nw.lat(),              // lamax
             box.se.lon() );            // lomax
    return std::string(url);
}

// only needed for token request, will then form token request body
void OpenSkyConnection::ComputeBody (const positionTy& /*pos*/)
{
    if (eState == OPSKY_STATE_GETTING_TOKEN) {
        // if we are to fetch a token then we need to put credentials into the body
        char s[128];
        std::string clientId, clientSecret;
        dataRefs.GetOpenSkyCredentials(clientId, clientSecret);
        snprintf(s, sizeof(s), OPSKY_BODY_GETTOKEN,
                 URLEncode(clientId).c_str(),
                 URLEncode(clientSecret).c_str());
        requBody = s;
    }
    else {
        // in all other case we don't have a body and will send a GET request
        requBody.clear();
    }
}


// update shared flight data structures with received flight data
// "a4d85d","UJC11   ","United States",1657226901,1657226901,-90.2035,38.8157,2758.44,false,128.1,269.54,-6.5,null,2895.6,"4102",false,0
bool OpenSkyConnection::ProcessFetchedData ()
{
    char buf[100];
    
    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // Only proceed in case HTTP response was OK
    switch (httpResponse)
    {
        // All OK
        case HTTP_OK:
            break;

        // Unauthorized? Also wrong token, or wrong credentials when trying to get the token
        case HTTP_BAD_REQUEST:      // OpenSky had returned 400 in case of bad credentials when asking for the token...seems it does no longer, but we handled it still
        case HTTP_UNAUTHORIZED:     // OpenSky returns 401 in case of a bad or timed-out token
            if (eState == OPSKY_STATE_GETTING_TOKEN) {
                SHOW_MSG(logERR, "%s: Authorization failed! Verify Client Id/Secret in settings.",
                         pszChName);
                SetValid(false,false);
                SetEnable(false);       // also disable to directly allow user/pwd change...and won't work on retry anyway
                return false;
            }
            else {
                LOG_MSG(logERR, "%s: Bad or timed-out access token",
                        pszChName);
                ResetStatus();          // let's try with a new one
                IncErrCnt();
                return false;
            }

        // Ran out of requests?
        case HTTP_TOO_MANY_REQU:
            SHOW_MSG(logERR, "%s: Used up request credit for today, try again on %s",
                     pszChName,
                     dataRefs.OpenSkyRetryAt.empty() ? "<?>" : dataRefs.OpenSkyRetryAt.c_str());
            SetValid(false,false);
            return false;
        
        // Timeouts are so common recently with OpenSky that we no longer treat them as errors,
        // but we inform the user every once in a while
        case HTTP_GATEWAY_TIMEOUT:
        case HTTP_TIMEOUT:
        {
            static std::chrono::time_point<std::chrono::steady_clock> lastTimeoutWarn;
            auto tNow = std::chrono::steady_clock::now();
            if (tNow > lastTimeoutWarn + std::chrono::minutes(5)) {
                lastTimeoutWarn = tNow;
                SHOW_MSG(logWARN, "%s communication unreliable due to timeouts!", pszChName);
            }
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
    
    // now try to interpret it as JSON
    JSONRootPtr pRoot (netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot.get());
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    
    // --- Token ---
    // is this a token response?
    std::string sTok = jog_s(pObj, OPSKY_ACCESS_TOKEN);
    if (!sTok.empty()) {
        // prepend the token with the header string and create the actual header list
        sTok.insert(0, OPSKY_AUTH_BEARER);
        CurlCleanupSlist(pHdrToken);
        pHdrToken = curl_slist_append(nullptr, sTok.c_str());

        // process the expiration time (reduce by two refresh periods just to be on the safe side)
        long expiresIn = jog_l(pObj, OPSKY_AUTH_EXPIRES);
        if (expiresIn <= 0) expiresIn = OPSKY_AUTH_EXP_DEFAULT;
        tTokenExpiration = dataRefs.GetMiscNetwTime() + float(expiresIn - 2 * dataRefs.GetFdRefreshIntvl());

        LOG_MSG(logDEBUG, "Received access token expiring in %ld seconds", expiresIn);
        return true;
    }
    
    // --- Planes ---
    // for determining an offset as compared to network time we need to know network time
    double opSkyTime = jog_n(pObj, OPSKY_TIME);
    if (opSkyTime > JAN_FIRST_2019)
        // if reasonable add this to our time offset calculation
        dataRefs.ChTsOffsetAdd(opSkyTime);
    
    // Cut-off time: We ignore tracking data, which is "in the past" compared to simTime
    const double tsCutOff = dataRefs.GetSimTime();

    // We need to calculate distance to current camera later on
    const positionTy viewPos = dataRefs.GetViewPos();

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
            
            // Check for duplicates with OGN/FLARM, potentially replaces the key type
            LTFlightData::CheckDupKey(fdKey, LTFlightData::KEY_FLARM);

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
    
    // success
    return true;
}


// get status info, including remaining requests
std::string OpenSkyConnection::GetStatusText () const
{
    std::string s = LTChannel::GetStatusText();
    if (IsValid()) {
        if (dataRefs.OpenSkyRRemain < LONG_MAX)
        {
            s += " | ";
            s += std::to_string(dataRefs.OpenSkyRRemain);
            s += " requests left today";
        }
    } else {
        if (!dataRefs.OpenSkyRetryAt.empty())
        {
            s += ", retry at ";
            s += dataRefs.OpenSkyRetryAt;
        }
    }

    return s;
}


// Process OpenSKy's 'crendetials.json' file to fetch User ID/Secret from it
/// @details The file is expected to contain just one single line of JSON,
///          but just to be sure we read everything in, then process.
///          Expected content is something like:
///          `{"clientId":"xyz-api-client","clientSecret":"abc...xyz"}`
bool OpenSkyConnection::ProcessCredentialsJson (const std::string& sFileName,
                                                std::string& sClientId,
                                                std::string& sSecret)
{
    std::string json;
    
    // read the actual file
    std::ifstream fCred(sFileName);
    for (int i = 0;
         fCred.good() && !fCred.eof() && i < 10;    // max ten lines...don't want to end up crashing with memory exhaustion
         ++i)
    {
        std::string ln;
        safeGetline(fCred, ln);
        json += ln;
    }
    fCred.close();
    
    // Process the actual JSON
    JSONRootPtr pRoot (json.c_str());
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); return false; }
    
    // first get the structre's main object
    const JSON_Object* pObj = json_object(pRoot.get());
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); return false; }
    
    // We expect two keys
    std::string clientId = jog_s(pObj, "clientId");
    std::string secret   = jog_s(pObj, "clientSecret");

    // We are good if both is filled
    if (!clientId.empty() && !secret.empty()) {
        sClientId = clientId;
        sSecret = secret;
        return true;
    }
    
    return false;
}

//
//MARK: OpenSkyAcMasterdata
//

// Constructor
OpenSkyAcMasterdata::OpenSkyAcMasterdata () :
LTACMasterdataChannel(DR_CHANNEL_OPEN_SKY_AC_MASTERDATA, OPSKY_MD_NAME)
{
    // purely informational
    urlName  = OPSKY_MD_CHECK_NAME;
    urlLink  = OPSKY_MD_CHECK_URL;
    urlPopup = OPSKY_MD_CHECK_POPUP;
}

// accept requests that aren't in the ignore lists
bool OpenSkyAcMasterdata::AcceptRequest (const acStatUpdateTy& r)
{
    if ((r.type == DATREQU_ROUTE ||                     // accepting all kinds of call signs
         r.acKey.eKeyType == LTFlightData::KEY_ICAO) && // but only ICAO-typed master data requests
        !ShallIgnore(r))
    {
        InsertRequest(r);
        return true;
    }
    return false;
}

// virtual thread main function
void OpenSkyAcMasterdata::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_OpSkyMaster", LC_ALL_MASK);
    RegisterMasterDataChn(this, false);         // Register myself as a master data channel
    tSetRequCleared = dataRefs.GetMiscNetwTime();
    
    while ( shallRun() ) {
        // LiveTraffic Top Level Exception Handling
        try {
            // if there is something to request, fetch the data and process it
            if (FetchNextRequest())
            {
                if (FetchAllData(positionTy()) && ProcessFetchedData()) {
                    // reduce error count if processed successfully, as a chance to appear OK in the long run
                    DecErrCnt();
                } else {
                    // Could not find the data here, pass on the request
                    PassOnRequest(this, currRequ);
                }
            }

            // We must wait a moment between any two requests just not to overload the server
            std::this_thread::sleep_for(OPSKY_WAIT_BETWEEN);
            
            // sleep a bit or until woken up for termination by condition variable trigger
            if (!HaveAnyRequest())
            {
                std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
                FDThreadSynchCV.wait_for(lk, OPSKY_WAIT_NOQUEUE,
                                         [this]{return !shallRun() || HaveAnyRequest();});
            }
            
            // Every 3s clear up outdated requests waiting in queue
            if (CheckEverySoOften(tSetRequCleared, 3.0f))
                MaintainMasterDataRequests();
            
        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            IncErrCnt();
        } catch (...) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown type)");
            IncErrCnt();
        }
    }
    
    // Before leaving clear the request queue
    std::unique_lock<std::recursive_mutex> lock (mtxMaster);
    if (bFDMainStop)                        // in case of all stopping just throw them away
        setAcStatRequ.clear();
    else
        MaintainMasterDataRequests();       // otherwise clear up and pass on
    UnregisterMasterDataChn(this);          // Unregister myself as a master data channel
}


// Returns the master data or route URL to query
std::string OpenSkyAcMasterdata::GetURL (const positionTy& /*pos*/)
{
    switch (currRequ.type) {
        case DATREQU_AC_MASTER:
            return std::string(OPSKY_MD_URL) + URLEncode(currRequ.acKey.key);
        case DATREQU_ROUTE:
            return std::string(OPSKY_ROUTE_URL) + URLEncode(currRequ.callSign);
        case DATREQU_NONE:
            return std::string();
    }
    return std::string();
}

// process each master data line read from OpenSky
bool OpenSkyAcMasterdata::ProcessFetchedData ()
{
    // If the requested data just wasn't found add it to the ignore list
    if (httpResponse == HTTP_NOT_FOUND) {
        AddIgnore();
        return false;
    }
    // Any other result or no message is technically not OK
    else if (httpResponse != HTTP_OK || !netDataPos) {
        IncErrCnt();
        return false;
    }
    
    // Try to interpret is as JSON and get the main JSON object
    JSONRootPtr pRoot (netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    JSON_Object* pObj = json_object(pRoot.get());
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    
    // Pass on the further processing depending on the request type
    switch (currRequ.type) {
        case DATREQU_AC_MASTER:
            return ProcessMasterData(pObj);
        case DATREQU_ROUTE:
            return ProcessRouteInfo(pObj);
        case DATREQU_NONE:
            break;
    }
    return false;
}


// Process received aircraft master data
bool OpenSkyAcMasterdata::ProcessMasterData (JSON_Object* pJAc)
{
    LTFlightData::FDKeyTy fdKey;        // the key: transponder Icao code, filled from response!
    LTFlightData::FDStaticData statDat; // here we collect the master data

    // fetch values from the online data
    fdKey.SetKey(LTFlightData::KEY_ICAO,
                 jog_s(pJAc, OPSKY_MD_TRANSP_ICAO));
    statDat.reg         = jog_s(pJAc, OPSKY_MD_REG);
    statDat.country     = jog_s(pJAc, OPSKY_MD_COUNTRY);
    statDat.acTypeIcao  = jog_s(pJAc, OPSKY_MD_AC_TYPE_ICAO);
    statDat.man         = jog_s(pJAc, OPSKY_MD_MAN);
    statDat.mdl         = jog_s(pJAc, OPSKY_MD_MDL);
    statDat.catDescr    = jog_s(pJAc, OPSKY_MD_CAT_DESCR);
    statDat.op          = jog_s(pJAc, OPSKY_MD_OP);
    statDat.opIcao      = jog_s(pJAc, OPSKY_MD_OP_ICAO);
            
    // -- Ground vehicle identification --
    // OpenSky only delivers "category description" and has a
    // pretty clear indicator for a ground vehicle
    if (statDat.acTypeIcao.empty() &&           // don't know a/c type yet
        (statDat.catDescr.find(OPSKY_MD_TEXT_VEHICLE) != std::string::npos ||
         // I'm having the feeling that if nearly all is empty and the category description is "No Info" then it's often also a ground vehicle
         (statDat.catDescr.find(OPSKY_MD_TEXT_NO_CAT) != std::string::npos &&
          statDat.man.empty() &&
          statDat.mdl.empty() &&
          statDat.opIcao.empty())))
    {
        // we assume ground vehicle
        statDat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        // The category description usually is something like
        // "Surface Vehicle – Service Vehicle"
        // Save the latter part if we have no model info yet
        if (statDat.mdl.empty() &&
            statDat.catDescr.find(OPSKY_MD_TEXT_VEHICLE) != std::string::npos &&
            statDat.catDescr.length() > OPSKY_MD_TEXT_VEHICLE_LEN)
        {
            statDat.mdl = statDat.catDescr.c_str() + OPSKY_MD_TEXT_VEHICLE_LEN;
        }
    }
    // Replace type GRND with our default car type, too
    else if (statDat.acTypeIcao == "GRND" || statDat.acTypeIcao == "GND")
        statDat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
    
    // Perform the update
    UpdateStaticData(fdKey, statDat);
    return true;
}
        

// Process received route info
bool OpenSkyAcMasterdata::ProcessRouteInfo (JSON_Object* pJRoute)
{
    LTFlightData::FDStaticData statDat; // here we collect the master data

    // fetch values from the online data
    // route is an array of typically 2 entries, but can have more
    //    "route":["EDDM","LIMC"]
    JSON_Array* pJRArr = json_object_get_array(pJRoute, OPSKY_ROUTE_ROUTE);
    if (pJRArr) {
        size_t cnt = json_array_get_count(pJRArr);
        for (size_t i = 0; i < cnt; i++)
            statDat.stops.push_back(jag_s(pJRArr, i));
    }
    
    // flight number: made up of IATA and actual number
    statDat.flight  = jog_s(pJRoute,OPSKY_ROUTE_OP_IATA);
    double flightNr = jog_n_nan(pJRoute,OPSKY_ROUTE_FLIGHT_NR);
    if (!std::isnan(flightNr))
        statDat.flight += std::to_string(lround(flightNr));
    
    // update the a/c's master data
    UpdateStaticData(currRequ.acKey, statDat);
    return true;
}

//
// MARK: OpenSky Master Data File
//


// Constructor
OpenSkyAcMasterFile::OpenSkyAcMasterFile () :
LTACMasterdataChannel(DR_CHANNEL_OPEN_SKY_AC_MASTERFILE, OPSKY_MDF_NAME)
{
    // purely informational
    urlName  = OPSKY_MD_CHECK_NAME;
    urlLink  = OPSKY_MD_CHECK_URL;
    urlPopup = OPSKY_MD_CHECK_POPUP;
}

// accept only master data requests for ICAO-type keys
bool OpenSkyAcMasterFile::AcceptRequest (const acStatUpdateTy& r)
{
    if (r.type == DATREQU_AC_MASTER &&
        r.acKey.eKeyType == LTFlightData::KEY_ICAO &&
        !ShallIgnore(r))
    {
        InsertRequest(r);
        return true;
    }
    return false;
}

// virtual thread main function
void OpenSkyAcMasterFile::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_OpSkyMstFile", LC_ALL_MASK);

    // Make sure we have an aircraft database file, and open it
    if (!OpenDatabaseFile()) {
        SHOW_MSG(logERR, "No OpenSky Aircraft Database file available!");
        SetValid(false,true);
        return;
    }
    
    // Loop to process requests
    RegisterMasterDataChn(this, true);      // Register myself as a master data channel to the beginning of the queue, because we don't need internet connection and are faster
    tSetRequCleared = dataRefs.GetMiscNetwTime();
    while ( shallRun() ) {
        // LiveTraffic Top Level Exception Handling
        try {
            // if there is something to request, fetch the data and process it
            if (FetchNextRequest()) {
                if (LookupData() && ProcessFetchedData()) {
                    // reduce error count if processed successfully, as a chance to appear OK in the long run
                    DecErrCnt();
                } else {
                    // Could not find the data here, pass on the request
                    PassOnRequest(this, currRequ);
                }
            }
            
            // sleep for OPSKY_WAIT_NOQUEUE or if woken up for termination
            // by condition variable trigger
            if (!HaveAnyRequest())
            {
                std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
                FDThreadSynchCV.wait_for(lk,OPSKY_WAIT_NOQUEUE,
                                         [this]{return !shallRun() || HaveAnyRequest();});
            }
            
            // Every 3s clear up outdated requests waiting in queue
            if (CheckEverySoOften(tSetRequCleared, 3.0f))
                MaintainMasterDataRequests();
            
        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            IncErrCnt();
        } catch (...) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown type)");
            IncErrCnt();
        }
    }
    
    // Before leaving clear the request queue
    std::unique_lock<std::recursive_mutex> lock (mtxMaster);
    if (bFDMainStop)                        // in case of all stopping just throw them away
        setAcStatRequ.clear();
    else
        MaintainMasterDataRequests();       // otherwise clear up and pass on
    UnregisterMasterDataChn(this);          // Unregister myself as a master data channel
}

/// @brief Parse one field off the input ln
/// @details Returns the field's content after de-mangling
std::string ParseField (const std::string& ln, std::string::const_iterator *pp = nullptr)
{
    std::string ret;
    
    // Sanity check
    if (ln.empty()) return "";
    
    // Pointer to the string position we work on
    // If passed in then we start there, otherwise at the beginning
    std::string::const_iterator p = pp ? *(pp) : ln.begin();
    
    // Encapsulated in some quotes?
    char qu = 0;
    if (*p == '\'') qu = '\'';
    else if (*p == '"') qu = '"';
    if (qu) ++p;
    
    // indicates that the previous char was the quote char
    // (so the next should either be the quote char again, or a comma)
    bool bPrevQu = false;
    
    // Process at max until end of string
    for (;p != ln.end(); ++p) {
        // if current char is the quote char
        if (qu && *p == qu) {
            if (bPrevQu) {              // ...and the previous one was, too
                ret += qu;              // then the result is a quote char
                bPrevQu = false;
            }
            else
                bPrevQu = true;         // otherwise we don't yet know, just set the flag that we just came across a quote char
        }
        else if (*p == ',') {           // comma _can_ separate fields, ie. end parsing this field
            if (!qu || bPrevQu) {       // if not encapsulated in quotes, or the previous char _was_ the quote char (which then had ended the string)
                ++p;                    // read over that comma
                break;                  // then stop
            }
            else
                ret += ',';             // within an encapsulated string it's just a comma to be added to the output
        }
        else {
            ret += *p;                  // all other chars we add verbatim
            bPrevQu = false;
        }
    }
    
    // Return the processing pointer (as input for a next call)?
    if (pp)
        *(pp) = p;
    
    return ret;
}

/// @brief Extract the hexId from an OpenSky ac database line, `0` if invalid
/// @details Line needs to start with a full 6-digit hex id in double quotes: "000001"
/// @returns 0x0F000000UL if input is invalid
unsigned long GetHexId (const std::string& ln)
{
    // must have exactly 6 chars
    if (ln.size() != 6)
        return 0x0F000000UL;
    
    // Test for all hex digits
    if (!std::all_of(ln.begin(),
                     ln.end(),
                     [](const char& ch){ return std::isxdigit(ch); }))
        return 0x0F000000UL;

    // convert text to number
    return std::strtoul(ln.c_str(), nullptr, 16);
}

/// Process looked up master data
/// @details Database file line is expected in `ln`
bool OpenSkyAcMasterFile::ProcessFetchedData ()
{
    try {
        // Parse the line field-by-field
        std::vector<std::string> v;
        for (std::string::const_iterator p = ln.begin();
             p != ln.end();)
            v.push_back(ParseField(ln, &p));
        // we expect a minimum number of fields
        if (v.size() < numFields) {
            LOG_MSG(logWARN, "A/c database file line has too few fields: %s", ln.c_str());
            AddIgnore();
            return false;
        }
        
        // Sanity check: Hex id must match the current search request
        if (std::strtoul(v[mapFieldPos[OPSKY_MDF_HEXID]].c_str(), nullptr, 16) != currRequ.acKey.num) {
            LOG_MSG(logERR, "A/c id of fetched db line (%s) doesn't match requested id (%s)",
                    v[mapFieldPos[OPSKY_MDF_HEXID]].c_str(), currRequ.acKey.c_str());
            AddIgnore();
            return false;
        }
        
        // Fill readable static data from the database line
        LTFlightData::FDStaticData stat;
        stat.acTypeIcao = v[mapFieldPos[OPSKY_MDF_ACTYPE]];
        if (stat.acTypeIcao == "GRND") stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        
        if (mapFieldPos[OPSKY_MDF_REG])         stat.reg        = v[mapFieldPos[OPSKY_MDF_REG]];
        if (mapFieldPos[OPSKY_MDF_MAN])         stat.man        = v[mapFieldPos[OPSKY_MDF_MAN]];
        if (stat.man.empty() && mapFieldPos[OPSKY_MDF_MANICAO])
                                                stat.man        = v[mapFieldPos[OPSKY_MDF_MANICAO]];
        if (mapFieldPos[OPSKY_MDF_MDL])         stat.mdl        = v[mapFieldPos[OPSKY_MDF_MDL]];
        if (mapFieldPos[OPSKY_MDF_CATDESCR])    stat.catDescr   = v[mapFieldPos[OPSKY_MDF_CATDESCR]];
        if (stat.acTypeIcao.empty()) {
            if (stat.catDescr == "Ultralight / hang-glider / paraglider")
                stat.acTypeIcao = "GLID";
            else if (stat.catDescr.compare(0, 15, "Surface Vehicle") == 0)
                stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        }
        if (mapFieldPos[OPSKY_MDF_OWNER])       stat.op        = v[mapFieldPos[OPSKY_MDF_OWNER]];
        if (stat.op.empty() && mapFieldPos[OPSKY_MDF_OP])
                                                stat.op        = v[mapFieldPos[OPSKY_MDF_OP]];
        if (mapFieldPos[OPSKY_MDF_OPICAO])      stat.opIcao    = v[mapFieldPos[OPSKY_MDF_OPICAO]];
        
        // Update flight data
        UpdateStaticData(currRequ.acKey, stat);
        return true;
    }
    catch (const std::runtime_error& e) {
        LOG_MSG(logERR, "Couldn't process record: %s", e.what());
        IncErrCnt();
    }
    return false;
}

/// perform the file lookup
/// @details Looks up a starting position in the map of position,
///          as to seek in the database file to a near but not exact position.
///          From there, read line-by-line through the database file until we find the record we are looking for.
bool OpenSkyAcMasterFile::LookupData ()
{
    ln.clear();
    try {
        // Not generally good to try? Not enabled, no file?
        if (!shallRun() || !fAcDb.is_open())
            return false;
        
        // find search starting position in the map of positions, based on on a/c identifier
        mapPosTy::const_iterator iPos = mapPos.lower_bound(currRequ.acKey.num);
        // iPos->first is now equal or larger to acKey, but we need equal or lower, so potentially decrement
        while (iPos->first > currRequ.acKey.num) {
            if (iPos == mapPos.begin()) {               // cannot decrement any longer, what's wrong here...a key smaller than the first entry in the database???
                LOG_MSG(logWARN, "A/c key %06lX is smaller than first entry in database %06lX",
                        currRequ.acKey.num, iPos->first);
                AddIgnore();
                return false;
            }
            --iPos;
        }
        
        // iPos->second now points to a database file location on or before the record we seek
        unsigned long lnKey = 0UL;
        fAcDb.seekg(iPos->second);
        do {
            safeGetline(fAcDb, ln);                     // read one line
            lnKey = GetHexId(ParseField(ln));           // get the a/c key from the line
        }
        // repeat while the line's key is smaller
        while (fAcDb.good() && (lnKey == 0x0F000000UL || lnKey < currRequ.acKey.num));
        
        // If this is not the right record then we don't have it
        if (lnKey != currRequ.acKey.num) {
            AddIgnore();
            return false;
        }
        // else this was the key!
        LOG_ASSERT(lnKey == currRequ.acKey.num);
        return true;
    }
    catch (const std::runtime_error& e) {
        LOG_MSG(logERR, "Couldn't look up record: %s", e.what());
        IncErrCnt();
    }
    // technical failure:
    return false;
}

// find an aircraft database file to open/download
bool OpenSkyAcMasterFile::OpenDatabaseFile ()
{
    // Get current month and year
    const time_t now = time(nullptr);
    struct std::tm tm = *gmtime(&now);
    
    // Normalize year and month to human-typical values
    tm.tm_year += 1900;
    tm.tm_mon++;
    
    // Try this month and two previous months
    for (int i = 2; i >= 0; --i) {
        if (TryOpenDbFile(tm.tm_year, tm.tm_mon))
            return true;
        // try previous month, potentially rolling back to previous year
        if (--tm.tm_mon < 1) {
            --tm.tm_year;
            tm.tm_mon = 12;
        }
    }
    
    // as a last resort: we _know_ that the file for FEB-2025 was there
    return TryOpenDbFile(2025, 2);
}


// open/download the aircraft database file for the given month
/// @details After opening the file, loop over all lines
///          (~580,000) and save position information every
///          250 lines, so that we can search faster later
///          when looking up a/c keys.
bool OpenSkyAcMasterFile::TryOpenDbFile (int year, int month)
{
    // filename of what this is about
    snprintf(sAcDbfileName, sizeof(sAcDbfileName), OPSKY_MDF_FILE,
             year, month);

    try {
        // Is the file available already?
        const std::string fileDir  = dataRefs.GetLTPluginPath() + PATH_RESOURCES + '/';
        const std::string filePath = fileDir + sAcDbfileName;

        // Just try to open and see what happens
        fAcDb.open(filePath);
        if (!fAcDb.is_open() || !fAcDb.good())
        {
            fAcDb.close();

            // file doesn't exist, try to download
            std::string url = OPSKY_MDF_URL;
            url += sAcDbfileName;
            LOG_MSG(logDEBUG, "Trying to download %s", url.c_str());
            if (!RemoteFileDownload(url,filePath)) {
                LOG_MSG(logWARN, "Download of %s unavailable", url.c_str());
                return false;
            }

            fAcDb.open(filePath);
            if (!fAcDb.is_open() || !fAcDb.good()) {    // download but not OK to read?
                std::remove(filePath.c_str());          // remove and bail
                fAcDb.close();
                return false;
            }
        }
        
        // File is open and good to read
        LOG_MSG(logDEBUG, "Processing %s as aircraft database", filePath.c_str());
        mapFieldPos.clear();
        mapPos.clear();
        fAcDb.seekg(0);
        
        // --- Loop the file and save every 250th position ---
        // 'icao24','timestamp','acars','adsb','built','categoryDescription','country','engines','firstFlightDate','firstSeen','icaoAircraftClass','lineNumber','manufacturerIcao','manufacturerName','model','modes','nextReg','operator','operatorCallsign','operatorIata','operatorIcao','owner','prevReg','regUntil','registered','registration','selCal','serialNumber','status','typecode','vdl'
        // '000000','2017-10-19 18:30:18',0,0,,'',,'',,,'','','','',unknow,0,'','','','','','','',,,'','','','','',0
        // '000023','2018-05-29 02:00:00',0,0,,Ultralight / hang-glider / paraglider,,'',,,'','','','','',0,'','','','','','','',,,'','','','','',0
        // '3c48e8','2017-09-15 00:10:03',0,0,,'',Germany,'',,,L2J,'',AIRBUS,Airbus,A319 112,0,'','',EUROWINGS,'',EWG,Eurowings,'',,,D-ABGH,'','3245','',A319,0
        // '3c8176','2024-06-25 12:35:50',0,0,,,Germany,,,,,,,,Airport Ground Vehicle,0,,'',,'','',,,,,DHL10,,,,GRND,0
        // a6c64d,'2022-10-14 19:00:00',0,0,'2008-01-01','',United States,SUPERIOR IO-360 SER,,,L1P,'','VAN''S',Werle Brian W,RV-7A,0,'','','','','',Walker Mickey R,'','2029-10-31',,N5357,'',WERLE001,'',RV7,0

        // first line is the header, defines all column names, we read that to learn the indexes of the fields we need
        mapFieldPos[OPSKY_MDF_HEXID] = SIZE_MAX;                // HexId is _expected_ to be at pos 0...so we prefill with an invalid value to be able to check later that it got overwritten
        if (fAcDb.good()) {
            safeGetline(fAcDb, ln);
            std::string::const_iterator p = ln.begin();
            for (numFields = 0; p != ln.end(); ++numFields)     // loop all fields in the column header
                mapFieldPos[ParseField(ln, &p)] = numFields;    // and remember the index per field names
        }
        // to be useful we need at minimum a Hex id and a typecode
        if (mapFieldPos[OPSKY_MDF_HEXID] == SIZE_MAX || !mapFieldPos[OPSKY_MDF_ACTYPE]) {
            fAcDb.close();
            LOG_MSG(logWARN, "Can't use '%s' as it doesn't provide vital columns like HexID, a/c type code",
                    filePath.c_str());
            return false;
        }
        
        unsigned long prevHexId = 0;
        unsigned long lnNr = 0;
        while (fAcDb.good()) {
            const std::ifstream::pos_type pos = fAcDb.tellg();  // current position _before_ reading the line
            safeGetline(fAcDb, ln);
            
            // Here, we are only interested in the very first field, the hexId
            unsigned long hexId = GetHexId(ParseField(ln));
            if (hexId != 0x0F000000UL) {
                if (hexId < prevHexId) {
                    // this id not larger than last --> NOT SORTED!
                    LOG_MSG(logERR, "A/c database file '%s' appears not sorted at line '%s'!",
                            filePath.c_str(), ln.c_str());
                    fAcDb.close();
                    // We don't delete the file...it's invalid, but if we'd delete it we would only re-download next start again, which we want to avoid
                    return false;
                }
                prevHexId = hexId;
                
                // Save the position every now and then
                if (lnNr % OPSKY_NUM_LN_PER_POS == 0)
                    mapPos.emplace(hexId, pos);
                ++lnNr;
            }
        }
        
        // looks good!
        fAcDb.clear();
        
        // Lastly, we remove all _other_ database files given that each takes up 50MB of disk space
        // (Can't use XPLMGetDirectoryContents in non-main thread,
        //  using std::filesystem crashed CURL...
        //  so we go back to basic POSIX C and native Windows)
        {
            std::vector<std::string> vToBeDeleted;
#if IBM
            WIN32_FIND_DATA data = { 0 };
            // Search already only for files that _look_ like database files
            HANDLE h = FindFirstFileA((fileDir + OPSKY_MDF_FILE_BEGIN + '*').c_str(), &data);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!striequal(data.cFileName, sAcDbfileName))  // Skip the actual file that we just processed
                        vToBeDeleted.emplace_back(data.cFileName);
                } while (FindNextFileA(h, &data));
                FindClose(h);
            }
#else
            // https://stackoverflow.com/a/4204758
            DIR *d = nullptr;
            struct dirent *dir = nullptr;
            d = opendir(fileDir.c_str());
            if (d) {
                while ((dir = readdir(d)) != NULL) {
                    // If begins like a database file but is not the one we just processed
                    std::string f = dir->d_name;
                    if (stribeginwith(f, OPSKY_MDF_FILE_BEGIN) &&
                        !striequal(f, sAcDbfileName))
                        vToBeDeleted.emplace_back(std::move(f));
                }
                closedir(d);
            }
#endif
            // Now delete what we remembered
            for (const std::string& p: vToBeDeleted)
                std::remove((fileDir+p).c_str());
        }
        
        LOG_MSG(logINFO, "Database file '%s' processed and ready to use", filePath.c_str());
        return true;
        
    } catch (const std::exception& e) {
        LOG_MSG(logERR, "Could not download/open a/c database file '%s': %s", sAcDbfileName, e.what());
    } catch (...) {
        LOG_MSG(logERR, "Could not download/open a/c database file '%s'", sAcDbfileName);
    }
    return false;
}


// adds the database date to the status text
std::string OpenSkyAcMasterFile::GetStatusText () const
{
    std::string s = LTChannel::GetStatusText();
    if (IsValid() && sAcDbfileName[0] && fAcDb.good()) {
        s += " | ";
        s += sAcDbfileName;
    }
    return s;
}

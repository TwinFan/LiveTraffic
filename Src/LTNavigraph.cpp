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
LTFlightDataChannel(DR_CHANNEL_NVGR_FR24, NVGR_NAME)
{
    // purely informational
    urlName  = NVGR_CHECK_NAME;
    urlLink  = NVGR_CHECK_URL;
    urlPopup = NVGR_CHECK_POPUP;
}

NvgrFR24Connection::~NvgrFR24Connection ()
{
    CurlCleanupSlist(pHdrForm);
    CurlCleanupSlist(pHdrToken);
}

// used to force fetching a new token, e.g. after change of credentials
void NvgrFR24Connection::ResetStatus ()
{
    sErrMsg.clear();
    CurlCleanupSlist(pHdrToken);
    tAccessExpiration = std::chrono::time_point<std::chrono::steady_clock>();
    eState = NVGR_STATE_GETTING_TOKEN;
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
            // when to wake up next?
            std::chrono::time_point<std::chrono::steady_clock> tNext;
            
            // where are we right now?
            const positionTy pos (dataRefs.GetViewPos());
            
            // If the camera position is valid we can request data around it
            if (pos.isNormal()) {
                // fetch data and process it
                if (FetchAllData(pos) && ProcessFetchedData())
                        // reduce error count if processed successfully
                        // as a chance to appear OK in the long run
                        DecErrCnt();
                
                // If next request is a refresh token request, we can do it immediately,
                // if is is a traffic request we must wait until valid
                if (eState == NVGR_STATE_GET_PLANES)
                    tNext = tNextWakeup;
            }
            else {
                // Camera position is yet invalid, retry in a second
                tNext = std::chrono::steady_clock::now() + std::chrono::seconds(1);
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
    if (tAccessExpiration.time_since_epoch().count() > 0 &&
        std::chrono::steady_clock::now() >= tAccessExpiration)
    {
        ResetStatus();
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
    
    std::string s = jog_s(pMain, NVGR_ERROR);           // try official 'error' first
    if (s.empty()) s = jog_s(pMain, NVGR_ERROR_MSG);    // else try 'message'
    return s;
}

std::string NvgrFR24Connection::TryExtractErrorMsg (const std::string& resp)
{
    // try reading a reason from the response
    JSONRootPtr pRoot (resp.c_str());
    return TryExtractErrorMsg(pRoot ? json_object(pRoot.get()) : nullptr);
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
                SHOW_MSG(logERR, "%s: Authorization failed: %s. You will need to re-authenticate Navigraph in Settings.",
                         pszChName, errMsg.c_str());
                SetValid(false,false);
                SetEnable(false);       // also disable to directly allow user/pwd change...and won't work on retry anyway
                dataRefs.SetNvgrRefrshToken("");
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
        
        // Save the refresh token
        const std::string sRefresh = jog_s(pObj, NVGR_TOKEN_REFRESH);
        dataRefs.SetNvgrRefrshToken(sRefresh);          // we save whatever we get
        if (sRefresh.empty())  {                        // but if we didn't get anything we've got a problem
            SHOW_MSG(logERR, "Did not receive a new Refresh Token in last Navigraph authorization response! You will need to re-authenticate in Setting.");
        }
            
        // Find the access token and type, that's required
        const std::string sToken = jog_s(pObj, NVGR_TOKEN_ACCESS);
        const std::string sType  = jog_s(pObj, NVGR_TOKEN_TYPE);
        if (sToken.empty() || sType.empty()) {
            LOG_MSG(logERR,"Token response is missing the %s or %s fields:\n%s",
                    NVGR_TOKEN_ACCESS, NVGR_TOKEN_TYPE, netData);
            IncErrCnt();
            return false;
        }
        
        // If we get a timeout value we use that, otherwise a default
        long nTimeout = jog_l(pObj, NVGR_TOKEN_EXPIRES);
        if (!nTimeout) nTimeout = NVGR_AUTH_EXP_DEFAULT;
        nTimeout -= 2 * dataRefs.GetFdRefreshIntvl();           // reduce a little so we make sure we get a new token before it expires
        tAccessExpiration = std::chrono::steady_clock::now() + std::chrono::seconds(nTimeout);
        
        // prepare the token with the header string and create the actual header list
        snprintf(buf, sizeof(buf), NVGR_AUTH_HEADER,
                 sType.c_str(), sToken.c_str());
        CurlCleanupSlist(pHdrToken);
        pHdrToken = curl_slist_append(nullptr, buf);
        LOG_MSG(logDEBUG, "Successfully refreshed the tokens.");
        
        // Now that we have an access token we can request traffic data
        eState = NVGR_STATE_GET_PLANES;

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
        
    // Next wakeup (for traffic data) is "refresh interval" from _now_,
    // however a minimum of 20s as imposed by Navigraph
    tNextWakeup = std::chrono::steady_clock::now() +
    std::chrono::seconds(std::max(dataRefs.GetFdRefreshIntvl(), NVGR_MIN_REFRESH_INTVL));

    // success
    return true;
}


// get status info, including remaining requests
std::string NvgrFR24Connection::GetStatusText () const
{
    if (!IsBuiltIn())
        return "No support for Navigraph built into this binary";
    
    std::string s;
    
    // Authorization Process underway?
    DevAuthState authState = AuthGetState();
    if (authState > NVGR_AUTH_NONE) {
        switch (authState) {
            case NVGR_AUTH_NONE:        break;
            case NVGR_AUTH_FETCHING:    s += "Fetching Device Authorization..."; break;
            case NVGR_AUTH_WAITING:     s += "Waiting for you to authorize LiveTraffic, see link/browser"; break;
            case NVGR_AUTH_ERROR:
                s += "Error during Device Authorization: ";
                s += sErrMsg;
                break;
            case NVGR_AUTH_TIMEOUT:     s += "Device Authorization timed out!"; break;
            case NVGR_AUTH_SUCCESS:     s += "Device Authorization successful"; break;
            case NVGR_AUTH_CANCEL:      s += "Device Authorization being cancelled..."; break;
        }
        return s;
    }
    
    // Normal traffic data processing
    if (eState == NVGR_STATE_GETTING_TOKEN)
        s = "Getting access token...";
    else
        s = LTChannel::GetStatusText();
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

//
// MARK: Device Authorization
//

/// Synchronization mutex between main and auth thread, e.g. writing to the static vars
static std::recursive_mutex gAuthMtx;
static std::mutex gAuthCVMtx;
static std::condition_variable gAuthCV;
std::thread NvgrFR24Connection::thrAuth;            // the authroization communication thread

// Current state of Device Authorization
NvgrFR24Connection::DevAuthState NvgrFR24Connection::eAuthState = NvgrFR24Connection::NVGR_AUTH_NONE;
NvgrFR24Connection::DevAuthUI NvgrFR24Connection::eAuthUI = NvgrFR24Connection::NVGR_AUTH_UI_NOTHING;
std::string NvgrFR24Connection::sErrMsg;            // last error message, empty if OK
std::string NvgrFR24Connection::sAuthVerifyURI;     // Verification URI, to be passed on to the user
struct curl_slist* NvgrFR24Connection::pHdrToken = nullptr;   // HTTP Header containing the bearer token
std::chrono::time_point<std::chrono::steady_clock> NvgrFR24Connection::tAccessExpiration;


void NvgrFR24Connection::AuthInit ()
{
    // If process is still underway, cancel it
    AuthCancelProcess();
    
    // Reset the status to the very beginning
    std::lock_guard<std::recursive_mutex> lk(gAuthMtx);
    eAuthState = NVGR_AUTH_NONE;
    eAuthUI = !IsBuiltIn()                      ?   NVGR_AUTH_UI_NOTHING :
              dataRefs.HaveNvgrRefreshToken()   ?   NVGR_AUTH_UI_REAUTH :
                                                    NVGR_AUTH_UI_AUTH;
    sAuthVerifyURI.clear();
}

// Triggers the process (if not NVGR_AUTH_FETCHING/WAITING)
bool NvgrFR24Connection::AuthStartProcess ()
{
    std::lock_guard<std::recursive_mutex> lk(gAuthMtx);
    // Can only start a new process if we aren't waiting for results just now
    if ((NVGR_AUTH_FETCHING <= AuthGetState() && AuthGetState() <= NVGR_AUTH_WAITING) &&
        thrAuth.joinable())
        return false;
    
    // Start the thread
    eAuthState = NVGR_AUTH_FETCHING;
    sAuthVerifyURI.clear();
    thrAuth = std::thread(AuthMain);
    return true;
}

// If process is underway, cancel it and wait for it to end
void NvgrFR24Connection::AuthCancelProcess ()
{
    if (thrAuth.joinable()) {
        LOG_MSG(logDEBUG, "Trying to shut down Navigraph Device Auth thread...");
        std::unique_lock<std::recursive_mutex> lk(gAuthMtx);
        eAuthState = NVGR_AUTH_CANCEL;
        lk.unlock();
        thrAuth.join();
        LOG_MSG(logDEBUG, "Navigraph Device Auth thread shut down.");
    }
}

// Get state of auth process
NvgrFR24Connection::DevAuthState NvgrFR24Connection::AuthGetState ()
{
    std::lock_guard<std::recursive_mutex> lk(gAuthMtx);
    return eAuthState;
}

// What to show the user just now?
NvgrFR24Connection::DevAuthUI NvgrFR24Connection::AuthGetUI ()
{
    std::lock_guard<std::recursive_mutex> lk(gAuthMtx);
    return eAuthUI;
}

// Return the verification URI, if the authorization process received and needs one
std::string NvgrFR24Connection::AuthGetVerifyURI ()
{
    std::string s;
    std::lock_guard<std::recursive_mutex> lk(gAuthMtx);
    if (AuthGetState() == NVGR_AUTH_WAITING)    // shall only have a Verification URI if we are waiting for the user to authorize it
        s = sAuthVerifyURI;
    return s;
}

// Sets the new state, lock-conrolled, and save: only overwrite eOld with eNew
bool NvgrFR24Connection::AuthSetState (DevAuthState eOld, DevAuthState eNew)
{
    std::lock_guard<std::recursive_mutex> lk(gAuthMtx);
    // Not the expected old state? Don't override...some other thread probably was faster
    if (eAuthState != eOld) {
        LOG_MSG(logWARN, "AuthStatus is not %d as expected, but %d, hence could not change status to %d",
                eOld, eAuthState, eNew);
        return false;
    }
    // Only change if there is a change
    if (eAuthState != eNew) {
        eAuthState = eNew;
        LOG_MSG(logDEBUG, "AuthStatus changed from %d to %d", eOld, eNew);
        switch (eAuthState) {
            case NVGR_AUTH_NONE:                // Nothing's going on right now, so offer a button to start the process
                eAuthUI = !IsBuiltIn()                      ?   NVGR_AUTH_UI_NOTHING :
                          dataRefs.HaveNvgrRefreshToken()   ?   NVGR_AUTH_UI_REAUTH :
                                                                NVGR_AUTH_UI_AUTH;
                break;
            case NVGR_AUTH_FETCHING:            // We are waiting for a server reply, just wait
            case NVGR_AUTH_CANCEL:              // We are waiting for the background process to shut down, just wait
                eAuthUI = NVGR_AUTH_UI_WAIT;
                break;
            case NVGR_AUTH_WAITING:             // We are waiting for the user to authorize, so have the user go authroize!
                eAuthUI = NVGR_AUTH_UI_VERIFY_URI;
                break;
            case NVGR_AUTH_ERROR:               // Any kind of final result: We're done.
            case NVGR_AUTH_TIMEOUT:
            case NVGR_AUTH_SUCCESS:
                eAuthUI = NVGR_AUTH_UI_DONE;
                break;
        }
    }
    return true;
}


// Thread main function running the auth process
void NvgrFR24Connection::AuthMain ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_NvgrAuth", LC_ALL_MASK);
    LOG_MSG(logDEBUG, "LT_NvgrAuth thread started");
    sErrMsg.clear();
    
    char szBody[512];                                       // Request body
    std::string PKCEverifier, PKCEchallenge;                // PKCE verifier & challenge
    std::string sDeviceCode;                                // Device code received from Navigraph
    std::string resp;                                       // Network response
    long httpResp;                                          // HTTP response code
    size_t tInterval = NVGR_AUTH_INTERVAL_DEFAULT;          // how often to query auth status?

    // Main loop
    while (true) {
        // State Machine
        DevAuthState authState = AuthGetState();            // lock-controlled

        // Need to send the initial request to initiate the flow?
        if (authState == NVGR_AUTH_FETCHING) {
            try {
                // Get a PKCE Verifier and challenge
                PKCEVerifierChallenge(PKCEverifier, PKCEchallenge);
                // Put together the POST body
                snprintf(szBody, sizeof(szBody), NVGR_AUTH_BODY,
                         gsNvgrClientId.c_str(),
                         gsNvgrClientSecret.c_str(),
                         PKCEchallenge.c_str());
                // Query Navigraph server, wait for the response
                URLGet(NVGR_AUTH_URL,
                       { "Content-Type: application/x-www-form-urlencoded" },
                       szBody, {}, resp, httpResp);
                
                // Read the response as JSON and fetch what we need
                JSONRootPtr pRoot (resp.c_str());
                if (!pRoot) { THROW_ERROR(logERR,ERR_JSON_PARSE); }
                JSON_Object* pObj = json_object(pRoot.get());
                if (!pObj) { THROW_ERROR(logERR,ERR_JSON_MAIN_OBJECT); }
                // Device Code
                sDeviceCode = jog_s(pObj, NVGR_AUTH_DEV_CODE);
                if (sDeviceCode.empty()) { THROW_ERROR(logERR, "Device Authorization response is missing the '" NVGR_AUTH_DEV_CODE "' field"); }
                // Verification URI
                std::lock_guard<std::recursive_mutex> lk(gAuthMtx);
                sAuthVerifyURI = jog_s(pObj, NVGR_AUTH_VERIFY_URI);
                if (sAuthVerifyURI.empty()) { THROW_ERROR(logERR, "Device Authorization response is missing the '" NVGR_AUTH_VERIFY_URI "' field"); }
                // Polling interval
                tInterval = (size_t)jog_l(pObj, NVGR_AUTH_INTERVAL);
                if (!tInterval) tInterval = NVGR_AUTH_INTERVAL_DEFAULT;
                
                // Next expected step: wait
                AuthSetState (NVGR_AUTH_FETCHING, NVGR_AUTH_WAITING);
            }
            catch (const std::exception& e) {
                sErrMsg = TryExtractErrorMsg(resp);
                if (sErrMsg.empty()) sErrMsg = e.what();
                AuthSetState (NVGR_AUTH_FETCHING, NVGR_AUTH_ERROR);
            }
        }
        
        // Need to periodically fetch the status?
        else if (authState == NVGR_AUTH_WAITING) {
            try {
                // Put together the POST body
                snprintf(szBody, sizeof(szBody), NVGR_TOKEN_POLL_BODY,
                         sDeviceCode.c_str(),
                         PKCEverifier.c_str(),
                         gsNvgrClientId.c_str(),
                         gsNvgrClientSecret.c_str());
                DevAuthState nextState = NVGR_AUTH_WAITING;
                AuthSetState(NVGR_AUTH_WAITING, nextState);
                // Query Navigraph server, wait for the response
                URLGet(NVGR_TOKEN_URL,
                       { "Content-Type: application/x-www-form-urlencoded" },
                       szBody,
                       { HTTP_BAD_REQUEST },        // all "errors", including expected "authorization_pending" come back as 400, which is a tad inconvenient, so we need to handle 400 all by ourselves
                       resp, httpResp);
                
                // Interpret the response as JSON (even a 400 response delivers a JSON)
                JSONRootPtr pRoot (resp.c_str());
                if (!pRoot) { THROW_ERROR(logERR,ERR_JSON_PARSE); }
                JSON_Object* pObj = json_object(pRoot.get());
                if (!pObj) { THROW_ERROR(logERR,ERR_JSON_MAIN_OBJECT); }
                
                // HTTP_BAD_REQUEST -> handle the expected stuff, throw the unexpected
                if (httpResp == HTTP_BAD_REQUEST) {
                    std::string sError = jog_s(pObj, NVGR_ERROR);
                    if (sError.empty()) sError = jog_s(pObj, NVGR_ERROR_MSG);       // unlikely...but better safe than sorry: we also try 'message' if 'error' was empty; at least good for final error reporting
                    if (sError == "authorization_pending") { /* do nothing...just keep polling */ }
                    else if (sError == "slow_down") { tInterval += NVGR_AUTH_INTERVAL_DEFAULT; }
                    else if (sError == "access_denied") {
                        dataRefs.SetNvgrRefrshToken("");            // clear any potentially saved refresh token
                        AuthSetState (NVGR_AUTH_WAITING, NVGR_AUTH_ERROR);
                        sErrMsg = "Access has been denied.";
                    }
                    else if (sError == "expired_token") { AuthSetState (NVGR_AUTH_WAITING, NVGR_AUTH_TIMEOUT); }
                    else {
                        THROW_ERROR(logERR, "Unexpected error while polling authorization: %s", sError.c_str());
                    }
                }
                // HTTP_OK
                else {
                    // temporary access token and type
                    const std::string accessToken = jog_s(pObj, NVGR_TOKEN_ACCESS);
                    if (accessToken.empty()) { THROW_ERROR(logERR, "Device Authorization response is missing the '" NVGR_TOKEN_ACCESS "' field"); }
                    const std::string accessType = jog_s(pObj, NVGR_TOKEN_TYPE);
                    if (accessType.empty()) { THROW_ERROR(logERR, "Device Authorization response is missing the '" NVGR_TOKEN_TYPE "' field"); }
                    // access token expiration
                    long tExpiresIn = jog_l(pObj, NVGR_TOKEN_EXPIRES);
                    if (!tExpiresIn) tExpiresIn = NVGR_AUTH_EXP_DEFAULT;
                    tExpiresIn -= 2 * dataRefs.GetFdRefreshIntvl();           // reduce a little so we make sure we get a new token before it expires
                    tAccessExpiration = std::chrono::steady_clock::now() + std::chrono::seconds(tExpiresIn);
                    // refresh token (this one's long-lived and we store it in the settings)
                    const std::string refreshToken = jog_s(pObj, NVGR_TOKEN_REFRESH);
                    if (refreshToken.empty()) { THROW_ERROR(logERR, "Device Authorization response is missing the '" NVGR_TOKEN_REFRESH "' field"); }
                    dataRefs.SetNvgrRefrshToken(refreshToken);
                    // Store the access token in a CURL header
                    snprintf(szBody, sizeof(szBody), NVGR_AUTH_HEADER,
                             accessType.c_str(), accessToken.c_str());
                    std::lock_guard<std::recursive_mutex> lk(gAuthMtx);
                    CurlCleanupSlist(pHdrToken);
                    curl_slist_append(pHdrToken, szBody);
                    
                    // We're done!
                    AuthSetState (NVGR_AUTH_WAITING, NVGR_AUTH_SUCCESS);
                }
            }
            catch (const std::exception& e) {
                // all extraction has been done in the code above already
                sErrMsg = e.what();
                AuthSetState (NVGR_AUTH_WAITING, NVGR_AUTH_ERROR);
            }
        }
        
        // Re-Fetch state now after above processing
        authState = AuthGetState();                         // lock-controlled
        if (authState > NVGR_AUTH_WAITING)                  // leave the loop?
            break;
        
        // Wait for a while before going back in loop
        {
            std::unique_lock<std::mutex> lk(gAuthCVMtx);
            gAuthCV.wait_for(lk, std::chrono::seconds(tInterval));
        }
    }
    
    LOG_MSG(logDEBUG, "LT_NvgrAuth ended");
}


// Enabled this module
bool NavigraphStart ()
{
    NvgrFR24Connection::AuthInit();
    return true;
}

/// Stop this module, makes sure the auth thread shuts down
void NavigraphStop ()
{
    NvgrFR24Connection::AuthCancelProcess();
}

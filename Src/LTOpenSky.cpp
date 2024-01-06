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

// virtual thread main function
void OpenSkyConnection::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_OpSky", LC_ALL_MASK);
    
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


// Initialize CURL, adding OpenSky credentials
bool OpenSkyConnection::InitCurl ()
{
    // Standard-init first (repeated call will just return true without effect)
    if (!LTOnlineChannel::InitCurl())
        return false;
    
    // if there are credentials then now is the moment to add them
    std::string usr, pwd;
    dataRefs.GetOpenSkyCredentials(usr, pwd);
    if (!usr.empty() && !pwd.empty()) {
        curl_easy_setopt(pCurl, CURLOPT_USERNAME, usr.data());
        curl_easy_setopt(pCurl, CURLOPT_PASSWORD, pwd.data());
    } else {
        curl_easy_setopt(pCurl, CURLOPT_USERNAME, nullptr);
        curl_easy_setopt(pCurl, CURLOPT_PASSWORD, nullptr);
    }
    
    // read headers (for remaining requests info)
    curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, ReceiveHeader);
    
    return true;
}

// read header and parse for request remaining
size_t OpenSkyConnection::ReceiveHeader(char *buffer, size_t size, size_t nitems, void *)
{
    const size_t len = nitems * size;
    static size_t lenRRemain = strlen(OPSKY_RREMAIN);
    static size_t lenRetry = strlen(OPSKY_RETRY);
    char num[50];

    // Remaining?
    if (len > lenRRemain &&
        memcmp(buffer, OPSKY_RREMAIN, lenRRemain) == 0)
    {
        const size_t copyCnt = std::min(len-lenRRemain,sizeof(num)-1);
        memcpy(num, buffer+lenRRemain, copyCnt);
        num[copyCnt]=0;                 // zero termination
        
        long rRemain = std::atol(num);
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
    else if (len > lenRetry &&
             memcmp(buffer, OPSKY_RETRY, lenRetry) == 0)
    {
        const size_t copyCnt = std::min(len-lenRetry,sizeof(num)-1);
        memcpy(num, buffer+lenRetry, copyCnt);
        num[copyCnt]=0;                 // zero termination
        long secRetry = std::atol(num); // seconds till retry
        // convert that to a local timestamp for the user to use
        const std::time_t tRetry = std::time(nullptr) + secRetry;
        std::strftime(num, sizeof(num), "%d-%b %H:%M", std::localtime(&tRetry));
        dataRefs.OpenSkyRetryAt = num;
        dataRefs.OpenSkyRRemain = 0;
    }

    // always say we processed everything, otherwise HTTP processing would stop!
    return len;
}

// put together the URL to fetch based on current view position
std::string OpenSkyConnection::GetURL (const positionTy& pos)
{
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

// update shared flight data structures with received flight data
// "a4d85d","UJC11   ","United States",1657226901,1657226901,-90.2035,38.8157,2758.44,false,128.1,269.54,-6.5,null,2895.6,"4102",false,0
bool OpenSkyConnection::ProcessFetchedData ()
{
    char buf[100];

    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) return true;
    
    // Only proceed in case HTTP response was OK
    if (httpResponse != HTTP_OK) {
        // Unauthorized?
        if (httpResponse == HTTP_UNAUTHORIZED) {
            SHOW_MSG(logERR, "OpenSky: Unauthorized! Verify username/password in settings.");
            SetValid(false,false);
            SetEnable(false);       // also disable to directly allow user/pwd change...and won't work on retry anyway
            return false;
        }
        
        // Ran out of requests?
        if (httpResponse == HTTP_TOO_MANY_REQU) {
            SHOW_MSG(logERR, "OpenSky: Used up request credit for today, try again on %s",
                     dataRefs.OpenSkyRetryAt.empty() ? "<?>" : dataRefs.OpenSkyRetryAt.c_str());
            SetValid(false,false);
            return false;
        }
        
        // Timeouts are so common recently with OpenSky that we no longer treat them as errors,
        // but we inform the user every once in a while
        if (httpResponse == HTTP_GATEWAY_TIMEOUT    &&
            httpResponse == HTTP_TIMEOUT)
        {
            static std::chrono::time_point<std::chrono::steady_clock> lastTimeoutWarn;
            auto tNow = std::chrono::steady_clock::now();
            if (tNow > lastTimeoutWarn + std::chrono::minutes(5)) {
                lastTimeoutWarn = tNow;
                SHOW_MSG(logWARN, "%s communication unreliable due to timeouts!", pszChName);
            }
        }
        else {                                  // anything else is serious
            IncErrCnt();
        }
        return false;
    }
    
    // now try to interpret it as JSON
    JSONRootPtr pRoot (netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // let's cycle the aircraft
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot.get());
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    
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
    RegisterMasterDataChn(this);                // Register myself as a master data channel
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
LTACMasterdataChannel(DR_CHANNEL_OPEN_SKY_AC_MASTERFILE, OPSKY_MD_NAME)
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
    RegisterMasterDataChn(this);            // Unregister myself as a master data channel
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

/// @brief Extract the hexId from an OpenSky ac database line, `0` if invalid
/// @details Line needs to start with a full 6-digit hex id in double quotes: "000001"
unsigned long GetHexId (const std::string& ln)
{
    // must have at least 8 characters, and double quotes at position 0 and 7:
    if (ln.size() < 8 ||
        ln[0] != '"' || ln[7] != '"')
        return 0UL;
    
    // Test the inside for all hex digits
    if (!std::all_of(ln.begin() + 1,
                     ln.begin() + 7,
                     [](const char& ch){ return std::isxdigit(ch); }))
        return 0UL;
    
    // convert text to number
    return std::strtoul(ln.c_str()+1, nullptr, 16);
}

/// Process looked up master data
/// @details Database file line is expected in `ln`
bool OpenSkyAcMasterFile::ProcessFetchedData ()
{
    try {
        // Sanity check: Less than 45 chars is impossible for a valid line (15 fields with at least 3 chars: "","","",...
        if (ln.size() < ACMFF_NUM_FIELDS * 3)
            return false;
        
        // Split the line into its fields.
        // Note that we split by the 3 characters ","  so that fields don't just depend on the comma
        // and most but not all double quotes are already removed along the way
        std::vector<std::string> v = str_fields(ln, "\",\"");
        if (v.size() < ACMFF_NUM_FIELDS) {
            LOG_MSG(logWARN, "A/c database file line has too few fields: %s", ln.c_str());
            AddIgnore();
            return false;
        }
        
        // The very first and very last double quotes have not yet been removed
        if (!v.front().empty() && v.front().front() == '"') v.front().erase(0,1);
        if (!v.back().empty() && v.back().back() == '"') v.back().erase(v.back().length()-1,1);
        
        // Sanity check: Hex id must match the current search request
        if (std::strtoul(v[ACMFF_hexId].c_str(), nullptr, 16) != currRequ.acKey.num) {
            LOG_MSG(logERR, "A/c id of fetched db line (%s) doesn't match requested id (%s)",
                    v[ACMFF_hexId].c_str(), currRequ.acKey.c_str());
            AddIgnore();
            return false;
        }
        
        // Fill readable static data from the database line
        LTFlightData::FDStaticData stat;
        stat.reg            = v[ACMFF_reg];
        stat.acTypeIcao     = v[ACMFF_designator];
        stat.man            = !v[ACMFF_man].empty() ?       v[ACMFF_man] :
                                                            v[ACMFF_manIcao];
        stat.mdl            = v[ACMFF_mdl];
        stat.catDescr       = v[ACMFF_catDescr];
        stat.op             = !v[ACMFF_operator].empty() ?  v[ACMFF_operator] :
                              !v[ACMFF_owner].empty() ?     v[ACMFF_owner] :
                                                            v[ACMFF_operatorCallsign];
        stat.opIcao         = v[ACMFF_opIcao];
        
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
            lnKey = GetHexId(ln);                       // get the a/c key from the line
        }
        // repeat while the line's key is smaller
        while (fAcDb.good() && (!lnKey || lnKey < currRequ.acKey.num));
        
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
    
    // as a last resort: we _know_ that the file for DEC-2023 was there
    return TryOpenDbFile(2023, 12);
}


// open/download the aircraft database file for the given month
/// @details After opening the file, loop over all lines
///          (~580,000) and save position information every
///          250 lines, so that we can search faster later
///          when looking up a/c keys.
bool OpenSkyAcMasterFile::TryOpenDbFile (int year, int month)
{
    // filename of what this is about
    char fileName[50] = {0};
    snprintf(fileName, sizeof(fileName), OPSKY_MD_DB_FILE,
             year, month);

    try {
        // Is the file available already?
        std::string filePath = dataRefs.GetLTPluginPath();
        filePath += PATH_RESOURCES;
        filePath += '/';
        filePath += fileName;

        // Just try to open and see what happens
        fAcDb.open(filePath);
        if (!fAcDb.is_open() || !fAcDb.good())
        {
            fAcDb.close();

            // file doesn't exist, try to download
            std::string url = OPSKY_MD_DB_URL;
            url += fileName;
            LOG_MSG(logDEBUG, "Try to download %s", url.c_str());
            if (!RemoteFileDownload(url,filePath)) {
                LOG_MSG(logDEBUG, "Download of %s unavailable", url.c_str());
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
        mapPos.clear();
        fAcDb.seekg(0);
        
        // --- Loop the file and save every 250th position ---
        // hexId, reg, manIcao, man, mdl, designator, serialNum, lineNum, icaoAircraftClass, operator, operatorCallsign, opIcao, opIata, owner, catDescr
        // "0000c4","N474EA","BOEING","Boeing","737-448 /SF","B734","24474","1742","L2J","","","","","",""
        // "00015f","-UNKNOWN-","TAI","General Dynamics","F-16","F16","","","L1J","Baf","BELGIAN AIRFORCE","BAF","","",""

        unsigned long prevHexId = 0;
        unsigned long lnNr = 0;
        while (fAcDb.good()) {
            std::ifstream::pos_type pos = fAcDb.tellg();        // current position _before_ reading the line
            safeGetline(fAcDb, ln);
            
            std::ifstream::pos_type posAfter = fAcDb.tellg();   // current position _after_ reading the line
            
            char s[100];
            snprintf(s, sizeof(s), "%lld - %lld", std::streamoff(pos), std::streamoff(posAfter));
            

            // Here, we are only interested in the very first field, the hexId
            unsigned long hexId = GetHexId(ln);
            if (hexId) {
                if (hexId <= prevHexId) {
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
        return true;
        
    } catch (const std::exception& e) {
        LOG_MSG(logERR, "Could not download/open a/c database file '%s': %s", fileName, e.what());
    } catch (...) {
        LOG_MSG(logERR, "Could not download/open a/c database file '%s'", fileName);
    }
    return false;
}

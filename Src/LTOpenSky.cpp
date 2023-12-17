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
    JSON_Value* pRoot = json_parse_string(netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // let's cycle the aircraft
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot);
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
    
    // cleanup JSON
    json_value_free (pRoot);
    
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


// virtual thread main function
void OpenSkyAcMasterdata::Main ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_OpSkyMaster", LC_ALL_MASK);
    
    while ( shallRun() ) {
        // LiveTraffic Top Level Exception Handling
        try {
            // (Try to) get the next request to be processed
            requ = FetchNextRequest();
            
            // if there is something to request, fetch the data and process it
            if (requ &&
                FetchAllData(positionTy()) && ProcessFetchedData()) {
                // reduce error count if processed successfully
                // as a chance to appear OK in the long run
                DecErrCnt();
            }
            
            // sleep for OPSKY_WAIT_BETWEEN or if woken up for termination
            // by condition variable trigger
            {
                std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
                FDThreadSynchCV.wait_for(lk, OPSKY_WAIT_BETWEEN,
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


// Returns the master data or route URL to query
std::string OpenSkyAcMasterdata::GetURL (const positionTy& /*pos*/)
{
    switch (requ.type) {
        case DATREQU_AC_MASTER:
            return std::string(OPSKY_MD_URL) + requ.acKey.key;
        case DATREQU_ROUTE:
            return std::string(OPSKY_ROUTE_URL) + requ.callSign;
        case DATREQU_NONE:
            return std::string();
    }
    return std::string();
}

// process each master data line read from OpenSky
bool OpenSkyAcMasterdata::ProcessFetchedData ()
{
    // If the requested data wasn't found we shall never try this request again
    if (httpResponse == HTTP_NOT_FOUND ||
        httpResponse == HTTP_BAD_REQUEST)
    {
        switch (requ.type) {
            case DATREQU_AC_MASTER:
                AddIgnoreAc(requ.acKey);
                break;
            case DATREQU_ROUTE:
                AddIgnoreCallSign(requ.callSign);
                break;
            case DATREQU_NONE:
                break;
        }
        // But this is syntactically a totally valid response
        return true;
    }
    
    // Any other result or no message is not OK
    if (httpResponse != HTTP_OK || !netDataPos) {
        IncErrCnt();
        return false;
    }
    
    // Try to interpret is as JSON and get the main JSON object
    JSON_Value* pRoot = json_parse_string(netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    JSON_Object* pObj = json_object(pRoot);
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    
    // Pass on the further processing depending on the request type
    bool bRet = false;
    switch (requ.type) {
        case DATREQU_AC_MASTER:
            bRet = ProcessMasterData(pObj);
            break;
        case DATREQU_ROUTE:
            bRet = ProcessRouteInfo(pObj);
            break;
        case DATREQU_NONE:
            break;
    }
    
    // cleanup JSON
    json_value_free (pRoot);
    return bRet;
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
    UpdateStaticData(requ.acKey, statDat);
    return true;
}


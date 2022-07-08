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
LTChannel(DR_CHANNEL_OPEN_SKY_ONLINE, OPSKY_NAME),
LTOnlineChannel(),
LTFlightDataChannel()
{
    // purely informational
    urlName  = OPSKY_CHECK_NAME;
    urlLink  = OPSKY_CHECK_URL;
    urlPopup = OPSKY_CHECK_POPUP;
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
bool OpenSkyConnection::ProcessFetchedData (mapLTFlightDataTy& fdMap)
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
        
        // There are a few typical responses that may happen when OpenSky
        // is just temporarily unresponsive. But in all _other_ cases
        // we increase the error counter.
        if (httpResponse != HTTP_BAD_GATEWAY        &&
            httpResponse != HTTP_NOT_AVAIL          &&
            httpResponse != HTTP_GATEWAY_TIMEOUT    &&
            httpResponse != HTTP_TIMEOUT)
            IncErrCnt();
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
            LTFlightData& fd = fdMap[fdKey];
            
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
                positionTy pos (jag_n_nan(pJAc, OPSKY_LAT),
                                jag_n_nan(pJAc, OPSKY_LON),
                                dataRefs.WeatherAltCorr_m(jag_n_nan(pJAc, OPSKY_BARO_ALT)),
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
            s += ", ";
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
LTChannel(DR_CHANNEL_OPEN_SKY_AC_MASTERDATA, OPSKY_MD_NAME),
LTOnlineChannel(),
LTACMasterdataChannel()
{
    // purely informational
    urlName  = OPSKY_MD_CHECK_NAME;
    urlLink  = OPSKY_MD_CHECK_URL;
    urlPopup = OPSKY_MD_CHECK_POPUP;
}

// OpenSkyAcMasterdata fetches two objects with two URL requests:
// 1. Master (or Meta) data based on transpIcao
// 2. Route information based on current call sign
// Both keys are passed in vecAcStatUpdate. Two requests are sent one after
// the other. The returning information is combined into one artifical JSON
// object:
//      { "MASTER": <1. response>, "ROUTE": <2. response> }
// to be interpreted by ProcessFetchedData later.
bool OpenSkyAcMasterdata::FetchAllData (const positionTy& /*pos*/)
{
    if ( !IsEnabled() )
        return false;
    
    // first of all copy all requested a/c to our private list,
    // the global one is refreshed before the next call.
    CopyGlobalRequestList();
    
    // cycle all a/c's that need master data
    int iNeedToWait = 0;                // will be >0 once we had _not_ waited once
    bool bChannelOK = true;
    positionTy pos;                     // no position needed, but we use the GND flag to tell the URL callback if we need master or route request
    acStatUpdateTy info;
    while (bChannelOK && !vecAc.empty() && !bFDMainStop &&
           // We might have many requests in the list and we must pause
           // between two of them. We check in the loop that we don't exceed the
           // next wakeup time for the global request/reply loop so that regular
           // tracking request lookup can happen in time.
           std::chrono::steady_clock::now() <= gNextWakeup - 2 * std::chrono::milliseconds(int(OPSKY_WAIT_BETWEEN * 1000.0)))
    {
        // fetch request from back of list and remove
        info = vecAc.back();
        vecAc.pop_back();
        // empty or not an ICAO code? -> skip
        if (info.acKey.eKeyType != LTFlightData::KEY_ICAO ||
            info.acKey.key.empty())
            continue;
        
//        LOG_MSG(logDEBUG, "Requesting for %s / %s, order = %d",
//                info.acKey.c_str(), info.callSign.c_str(), info.order);
        
        // beginning of a JSON object
        std::string data("{");
        
        // *** Fetch Masterdata ***
        pos.f.onGrnd = GND_ON;                          // flag for: master data
        
        // skip icao of which we know they will come back invalid
        if ( std::find(invIcaos.cbegin(),invIcaos.cend(),info.acKey.key) == invIcaos.cend() )
        {
            // set key (transpIcao) so that other functions (GetURL) can access it
            currKey = info.acKey.key;
            
            // Potentially need to wait a bit between two subsequent requests
            if (iNeedToWait++)
                std::this_thread::sleep_for(std::chrono::milliseconds(int(OPSKY_WAIT_BETWEEN * 1000.0)));

            // make use of LTOnlineChannel's capability of reading online data
            if (LTOnlineChannel::FetchAllData(pos)) {
                switch (httpResponse) {
                    case HTTP_OK:                       // save response
                        data += "\"" OPSKY_MD_GROUP "\": ";       // start the group MASTER
                        data += netData;                // add the reponse
                        bChannelOK = true;
                        break;
                    case HTTP_NOT_FOUND:                // doesn't know a/c, don't query again
                    case HTTP_BAD_REQUEST:              // uh uh...done something wrong, don't do that again
                        invIcaos.emplace_back(info.acKey.key);
                        bChannelOK = true;              // but technically a valid response
                        break;
                        // in all other cases (including 503 HTTP_NOT_AVAIL)
                        // we say it is a problem and we try probably again later
                    default:
                        bChannelOK = false;
                }
            } else {
                // technical problem with fetching HTTP data
                bChannelOK = false;
            }
        }
        
        // break out on problems
        if (!bChannelOK)
            break;
        
        // *** Fetch Flight Info ***
        pos.f.onGrnd = GND_OFF;                         // flag for: route info
        
        // call sign shall be alphanumeric but nothing else
        str_toupper(info.callSign);
        
        // requires call sign and shall not be known bad
        if (bChannelOK &&
            !info.callSign.empty() &&
            // shall be alphanumeric
            str_isalnum(info.callSign) &&
            // shall not be a known bad call sign
            std::find(invCallSigns.cbegin(),invCallSigns.cend(),info.callSign) == invCallSigns.cend())
        {
            // set key (call sign) so that other functions (GetURL) can access it
            currKey = info.callSign;
            
            // delay between 2 requests to not overload OpenSky
            if (iNeedToWait++)
                std::this_thread::sleep_for(std::chrono::milliseconds(int(OPSKY_WAIT_BETWEEN * 1000.0)));
            
            // make use of LTOnlineChannel's capability of reading online data
            if (LTOnlineChannel::FetchAllData(pos)) {
                switch (httpResponse) {
                    case HTTP_OK:                       // save response
                        if (data.length() > 1)          // concatenate both JSON groups
                            data += ", ";
                        data += "\"" OPSKY_ROUTE_GROUP "\": ";       // start the group ROUTE
                        data += netData;                // add the response
                        bChannelOK = true;
                        break;
                    case HTTP_NOT_FOUND:                // doesn't know callsign, don't query again
                    case HTTP_BAD_REQUEST:              // uh uh...done something wrong, don't do that again
                        invCallSigns.emplace_back(info.callSign);
                        bChannelOK = true;              // but technically a valid response
                        break;
                        // in all other cases (including 503 HTTP_NOT_AVAIL)
                        // we say it is a problem and we try probably again later
                    default:
                        bChannelOK = false;
                }
            } else {
                // technical problem with fetching HTTP data
                bChannelOK = false;
            }
        }
        
        // break out on problems
        if (!bChannelOK)
            break;
        
        // close the outer JSON group
        data += '}';
        
        // the data we found is saved for later processing
        if (data.length() > 2)
            listMd.emplace_back(std::move(data));
    }
    
    // done
    currKey.clear();
    
    // if no technical valid answer received handle error
    if ( !bChannelOK ) {
        // we need to do that last request again
        if (!info.empty())
            vecAc.push_back(std::move(info));
        
        IncErrCnt();
        return !listMd.empty();         // return `true` if there is data to process, otherwise we wouldn't process what had been received before the error
    }
    
    // success
    return true;
}

// returns the openSky a/c master data URL per a/c
std::string OpenSkyAcMasterdata::GetURL (const positionTy& pos)
{
    // FetchAllData tells us by pos' gnd flag if we are to query
    // meta data (ON) or route information (OFF)
    return std::string(pos.IsOnGnd() ? OPSKY_MD_URL : OPSKY_ROUTE_URL) + currKey;
}

// process each master data line read from OpenSky
bool OpenSkyAcMasterdata::ProcessFetchedData (mapLTFlightDataTy& /*fdMap*/)
{
    // loop all previously collected master data records
    while (!listMd.empty()) {
        // the current master data record, remove it from the list
        std::string ln = std::move(listMd.front());
        listMd.pop_front();
        
        // here we collect the master data
        LTFlightData::FDStaticData statDat;
        
        // each individual line should work as a JSON object
        JSON_Value* pRoot = json_parse_string(ln.c_str());
        if (!pRoot) {
            LOG_MSG(logERR,ERR_JSON_PARSE);
            if (IncErrCnt())
                continue;
            else
                return false;
        }
        JSON_Object* pMain = json_object(pRoot);
        if (!pMain) {
            LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT);
            if (IncErrCnt())
                continue;
            else
                return false;
        }
        
        // *** Meta Data ***
        // the key: transponder Icao code
        LTFlightData::FDKeyTy fdKey;

        // access the meta data field group
        JSON_Object* pJAc = json_object_get_object(pMain, OPSKY_MD_GROUP);
        if (pJAc)
        {
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
            else if (statDat.acTypeIcao == "GRND")
                statDat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        }
        
        // *** Route Information ***
        
        // access the route info field group
        JSON_Object* pJRoute = json_object_get_object(pMain, OPSKY_ROUTE_GROUP);
        if (pJRoute)
        {
            // fetch values from the online data
            // route is an array of typically 2 entries:
            //    "route":["EDDM","LIMC"]
            JSON_Array* pJRArr = json_object_get_array(pJRoute, OPSKY_ROUTE_ROUTE);
            if (pJRArr) {
                size_t cnt = json_array_get_count(pJRArr);
                // origin: first entry
                if (cnt > 0)
                    statDat.originAp = jag_s(pJRArr, 0);
                // destination: last entry
                if (cnt > 1)
                    statDat.destAp = jag_s(pJRArr, cnt-1);
            }
            
            // flight number: made up of IATA and actual number
            statDat.flight  = jog_s(pJRoute,OPSKY_ROUTE_OP_IATA);
            double flightNr = jog_n_nan(pJRoute,OPSKY_ROUTE_FLIGHT_NR);
            if (!std::isnan(flightNr))
                statDat.flight += std::to_string(lround(flightNr));
        }
        
        // update the a/c's master data
        if (!fdKey.empty())
            UpdateStaticData(fdKey, statDat);
    }
    
    // we've processed all data, return success
    return true;
}


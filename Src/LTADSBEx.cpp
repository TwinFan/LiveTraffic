/// @file       LTADSBEx.cpp
/// @brief      ADS-B Exchange: Requests and processes live tracking data
/// @see        https://www.adsbexchange.com/
/// @details    Implements ADSBExchangeConnection:\n
///             - Handles the API key\n
///             - Provides a proper REST-conform URL for both the original sevrer as well as for the Rapid API server.\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
///             \n
///             ADSBExchangeHistorical is an implementation for historic data that once could be downloaded
///             from ADSBEx, but is no longer available for the average user. This historic data code
///             is no longer maintained and probably defunct. It is no longer accessible through the
///             UI either and should probably be removed.
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

//
//MARK: ADS-B Exchange
//

ADSBExchangeConnection::ADSBExchangeConnection () :
LTChannel(DR_CHANNEL_ADSB_EXCHANGE_ONLINE, ADSBEX_NAME),
LTOnlineChannel(),
LTFlightDataChannel()
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
    if (keyTy == ADSBEX_KEY_RAPIDAPI)
        snprintf(url, sizeof(url), ADSBEX_RAPIDAPI_25_URL, pos.lat(), pos.lon());
    else
        snprintf(url, sizeof(url), ADSBEX_URL, pos.lat(), pos.lon(),
                 dataRefs.GetFdStdDistance_nm());
    return std::string(url);
}

// update shared flight data structures with received flight data
bool ADSBExchangeConnection::ProcessFetchedData (mapLTFlightDataTy& fdMap)
{
    // some things depend on the key type
    const char* sERR = keyTy == ADSBEX_KEY_EXCHANGE ? ADSBEX_ERR              : ADSBEX_RAPID_ERR;
    const char* sNOK = keyTy == ADSBEX_KEY_EXCHANGE ? ADSBEX_NO_API_KEY       : ADSBEX_NO_RAPIDAPI_KEY;
    
    // received an UNAUTHOIZRED response? Then the key is invalid!
    if (httpResponse == HTTP_UNAUTHORIZED || httpResponse == HTTP_FORBIDDEN) {
        SHOW_MSG(logERR, ERR_ADSBEX_KEY_FAILED);
        SetValid(false);
        return false;
    }

    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) return true;
    
    // now try to interpret it as JSON
    JSON_Value* pRoot = json_parse_string(netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot);
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    
    // test for ERRor response
    const std::string errTxt = jog_s(pObj, sERR);
    if (!errTxt.empty() &&
        errTxt != ADSBEX_SUCCESS) {
        if (begins_with<std::string>(errTxt, sNOK)) {
            SHOW_MSG(logERR, ERR_ADSBEX_KEY_FAILED);
            SetValid(false);
        } else {
            LOG_MSG(logERR, ERR_ADSBEX_OTHER, errTxt.c_str());
            IncErrCnt();
        }
        json_value_free (pRoot);
        return false;
    }
    
    // We need to calculate distance to current camera later on
    const positionTy viewPos = dataRefs.GetViewPos();
    
    // for determining an offset as compared to network time we need to know network time
    // Also used later to calcualte the position's timestamp
    double adsbxTime = jog_n_nan(pObj, ADSBEX_NOW)  / 1000.0;
    if (std::isnan(adsbxTime))
        adsbxTime = jog_n(pObj, ADSBEX_TIME)  / 1000.0;
    
    if (adsbxTime > JAN_FIRST_2019)
        // if reasonable add this to our time offset calculation
        dataRefs.ChTsOffsetAdd(adsbxTime);
    
    // Cut-off time: We ignore tracking data, which is older than our buffering time
    const double tsCutOff = (double) dataRefs.GetFdBufPeriod();
    
    // let's cycle the aircraft
    // fetch the aircraft array
    JSON_Array* pJAcList = json_object_get_array(pObj, ADSBEX_AIRCRAFT_ARR);
    // iterate all aircraft in the received flight data (can be 0 or even pJAcList == NULL!)
    for ( size_t i=0; pJAcList && (i < json_array_get_count(pJAcList)); i++ )
    {
        // get the aircraft
        JSON_Object* pJAc = json_array_get_object(pJAcList,i);
        if (!pJAc) {
            LOG_MSG(logERR,ERR_JSON_AC,i+1,ADSBEX_AIRCRAFT_ARR);
            if (IncErrCnt())
                continue;
            else {
                json_value_free (pRoot);
                return false;
            }
        }
        
        // try version 2 first
        int ver = 2;
        std::string hexKey = jog_s(pJAc, ADSBEX_V2_TRANSP_ICAO);
        if (hexKey.empty()) {
            // not found, try version 1
            ver = 1;
            hexKey = jog_s(pJAc, ADSBEX_V1_TRANSP_ICAO);
            if (hexKey.empty())
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

        // Process the details, depends on version detected
        try {
            if (ver == 2)
                ProcessV2(pJAc, fdKey, fdMap, tsCutOff, adsbxTime, viewPos);
            else if (ver == 1)
                ProcessV1(pJAc, fdKey, fdMap, tsCutOff, adsbxTime, viewPos);
        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        } catch(...) {
            LOG_MSG(logERR, "Exception while processing data for '%s'", hexKey.c_str());
        }
    }
    
    // cleanup JSON
    json_value_free (pRoot);
    
    // success
    return true;
}


// Process v2 data
void ADSBExchangeConnection::ProcessV2 (JSON_Object* pJAc,
                                        LTFlightData::FDKeyTy& fdKey,
                                        mapLTFlightDataTy& fdMap,
                                        const double tsCutOff,
                                        const double adsbxTime,
                                        const positionTy& viewPos)
{
    // ADS-B returns Java tics, that is milliseconds, we use seconds
    const double ageOfPos = jog_sn(pJAc, ADSBEX_V2_SEE_POS);
    // skip stale data
    if (ageOfPos >= tsCutOff)
        return;
    
    // Try getting best possible position information
    // Some fields can come back NAN if not defined
    positionTy pos (jog_n_nan(pJAc, ADSBEX_V2_LAT),
                    jog_n_nan(pJAc, ADSBEX_V2_LON),
                    jog_n_nan(pJAc, ADSBEX_V2_ALT_GEOM) * M_per_FT,
                    adsbxTime - ageOfPos,
                    jog_n_nan(pJAc, ADSBEX_V2_HEADING));
    
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
                pos.f.onGrnd = GND_OFF;         // we are definitely off ground
                // But we only need to _process_ baro alt if we don't yet have an altitude
                if (std::isnan(pos.alt_m()))
                {
                    // try converting baro alt from given QNH, otherwise we use our own weather
                    const double baro_alt = json_number(pAltBaro);
                    const double qnh = jog_n_nan(pJAc, ADSBEX_V2_NAV_QNH);
                    if (std::isnan(qnh))
                        pos.SetAltFt(dataRefs.WeatherAltCorr_ft(baro_alt));
                    else
                        pos.SetAltFt(dataRefs.WeatherAltCorr_ft(baro_alt, qnh));
                }
                break;
                
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
    const std::string reg = jog_s(pJAc, ADSBEX_V2_REG);
    std::string acTy = jog_s(pJAc, ADSBEX_V2_AC_TYPE_ICAO);
    const std::string cat = jog_s(pJAc, ADSBEX_V2_AC_CATEGORY);
    
    // Skip static objects
    if (dataRefs.GetHideStaticTwr()) {
        if (reg  == "TWR" ||
            acTy == "TWR" ||
            cat  == "C3")
            // skip
            return;
    }
    
    // Identify ground vehicles
    if (cat  == "C1"  || cat == "C2" ||
        reg  == "GND" ||
        acTy == "GND")
        acTy = dataRefs.GetDefaultCarIcaoType();
    else if (pos.IsOnGnd() &&
             acTy.empty() && reg.empty() && cat.empty())
        acTy = dataRefs.GetDefaultCarIcaoType();

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
    
    // -- fill static data --
    LTFlightData::FDStaticData stat;
    stat.reg =        reg;
    stat.acTypeIcao = acTy;
    stat.mil =        (jog_l(pJAc, ADSBEX_V2_FLAGS) & 0x01) == 0x01;
    stat.call =       jog_s(pJAc, ADSBEX_V2_FLIGHT);
    trim(stat.call);
    stat.catDescr = GetADSBEmitterCat(cat);
    stat.slug       = ADSBEX_SLUG_BASE;
    stat.slug      += fdKey.key;
    
    // -- dynamic data --
    LTFlightData::FDDynamicData dyn;
    
    // non-positional dynamic data
    dyn.radar.code =        jog_sl(pJAc, ADSBEX_V2_RADAR_CODE);
    dyn.gnd =               pos.IsOnGnd();
    dyn.heading =           pos.heading();
    dyn.spd =               jog_n_nan(pJAc, ADSBEX_V2_SPD);
    dyn.vsi =               jog_n_nan(pJAc, ADSBEX_V2_ALT_GEOM);
    if (std::isnan(dyn.vsi))
        dyn.vsi =           jog_n_nan(pJAc, ADSBEX_V2_ALT_BARO);
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

// Process v1 data
void ADSBExchangeConnection::ProcessV1 (JSON_Object* pJAc,
                                        LTFlightData::FDKeyTy& fdKey,
                                        mapLTFlightDataTy& fdMap,
                                        const double tsCutOff,
                                        const double /*adsbxTime*/,
                                        const positionTy& viewPos)
{
    // ADS-B returns Java tics, that is milliseconds, we use seconds
    const double posTime = jog_sn(pJAc, ADSBEX_V1_POS_TIME) / 1000.0;
    // skip stale data
    if (posTime <= tsCutOff)
        return;
    
    // ADSBEx, especially the RAPID API version, returns
    // aircraft regardless of distance. To avoid planes
    // created and immediately removed due to distanced settings
    // we continue only if pos is within wanted range
    positionTy pos (jog_sn_nan(pJAc, ADSBEX_V1_LAT),
                    jog_sn_nan(pJAc, ADSBEX_V1_LON),
                    NAN,
                    posTime);
    const double dist = pos.dist(viewPos);
    if (dist > dataRefs.GetFdStdDistance_m() )
        return;

    // Are we to skip static objects?
    if (dataRefs.GetHideStaticTwr()) {
        if (!strcmp(jog_s(pJAc, ADSBEX_V1_GND), "1") && // on the ground
            !*jog_s(pJAc, ADSBEX_V1_AC_TYPE_ICAO) &&    // no type
            !*jog_s(pJAc, ADSBEX_V1_HEADING) &&         // no `trak` heading, not even "0"
            !*jog_s(pJAc, ADSBEX_V1_CALL) &&            // no call sign
            !*jog_s(pJAc, ADSBEX_V1_REG) &&             // no tail number
            !strcmp(jog_s(pJAc, ADSBEX_V1_SPD), "0"))   // speed exactly "0"
            // skip
            return;
    }

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
    
    // -- fill static data --
    LTFlightData::FDStaticData stat;
    stat.reg =        jog_s(pJAc, ADSBEX_V1_REG);
    stat.country =    jog_s(pJAc, ADSBEX_V1_COUNTRY);
    stat.acTypeIcao = jog_s(pJAc, ADSBEX_V1_AC_TYPE_ICAO);
    stat.mil =        jog_sb(pJAc,ADSBEX_V1_MIL);
    stat.opIcao =     jog_s(pJAc, ADSBEX_V1_OP_ICAO);
    stat.call =       jog_s(pJAc, ADSBEX_V1_CALL);
    stat.originAp =   jog_s(pJAc, ADSBEX_V1_ORIGIN);
    stat.destAp =     jog_s(pJAc, ADSBEX_V1_DESTINATION);
    stat.slug       = ADSBEX_SLUG_BASE;
    stat.slug      += fdKey.key;
    
    // ADSBEx sends airport info like "LHR London Heathrow United Kingdom"
    // That's way to long...
    cut_off(stat.originAp, " ");
    cut_off(stat.destAp, " ");

    // -- dynamic data --
    LTFlightData::FDDynamicData dyn;
    
    // non-positional dynamic data
    dyn.radar.code =        jog_sl(pJAc, ADSBEX_V1_RADAR_CODE);
    dyn.gnd =               jog_sb(pJAc, ADSBEX_V1_GND);
    dyn.heading =           jog_sn_nan(pJAc, ADSBEX_V1_HEADING);
    dyn.spd =               jog_sn(pJAc, ADSBEX_V1_SPD);
    dyn.vsi =               jog_sn(pJAc, ADSBEX_V1_VSI);
    dyn.ts =                posTime;
    dyn.pChannel =          this;
    
    // altitude, if airborne; fetch barometric altitude here
    const double alt_ft = dyn.gnd ? NAN : jog_sn_nan(pJAc, ADSBEX_V1_ALT);

    // position: altitude, heading, ground status
    pos.SetAltFt(dataRefs.WeatherAltCorr_ft(alt_ft));
    pos.heading() = dyn.heading;
    pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;
    
    // -- Ground vehicle identification --
    // Sometimes we get "-GND", replace it with "ZZZC"
    if (stat.acTypeIcao == ADSBEX_V1_TYPE_GND)
        stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();

    // But often, ADSBEx doesn't send a clear indicator
    if (stat.acTypeIcao.empty() &&      // don't know a/c type yet
        stat.reg.empty() &&             // don't have tail number
        dyn.gnd &&                      // on the ground
        dyn.spd < 50.0)                 // reasonable speed
    {
        // we assume ground vehicle
        stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
    }

    // update the a/c's master data
    fd.UpdateData(std::move(stat), dist);
    
    // position is rather important, we check for validity
    if ( pos.isNormal(true) ) {
        fd.AddDynData(dyn, 0, 0, &pos);
    }
    else
        LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
}


// get status info, including remaining requests
std::string ADSBExchangeConnection::GetStatusText () const
{
    std::string s = LTChannel::GetStatusText();
    if (IsValid() && IsEnabled() && dataRefs.ADSBExRLimit > 0)
    {
        s += ", ";
        s += std::to_string(dataRefs.ADSBExRRemain);
        s += " / ";
        s += std::to_string(dataRefs.ADSBExRLimit);
        s += " RAPID API requests left";
    }
    return s;
}



// add/cleanup API key
// (this is actually called prior to each request, so quite often)
bool ADSBExchangeConnection::InitCurl ()
{
    // we require an API key
    const std::string theKey (dataRefs.GetADSBExAPIKey());
    keyTy = GetKeyType(theKey);
    if (!keyTy) {
        apiKey.clear();
        SHOW_MSG(logERR, ERR_ADSBEX_NO_KEY_DEF);
        SetValid(false);
        return false;
    }
    
    // Reset any RAPID API request count if talking to ADSBEx directly
    if (keyTy != ADSBEX_KEY_RAPIDAPI)
        dataRefs.ADSBExRLimit = dataRefs.ADSBExRRemain = 0;
    
    // let's do the standard CURL init first
    if (!LTOnlineChannel::InitCurl())
        return false;

    // maybe read headers
    curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, keyTy == ADSBEX_KEY_RAPIDAPI ? ReceiveHeader : NULL);

    // did the API key change?
    if (!slistKey || theKey != apiKey) {
        apiKey = theKey;
        if (slistKey) {
            curl_slist_free_all(slistKey);
            slistKey = NULL;
        }
        slistKey = MakeCurlSList(keyTy, apiKey);
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

// make list of HTTP header fields
struct curl_slist* ADSBExchangeConnection::MakeCurlSList (keyTypeE keyTy, const std::string theKey)
{
    switch (keyTy) {
        case ADSBEX_KEY_EXCHANGE:
            return curl_slist_append(NULL, (std::string(ADSBEX_API_AUTH)+theKey).c_str());
        case ADSBEX_KEY_RAPIDAPI:
        {
            struct curl_slist* slist = curl_slist_append(NULL, ADSBEX_RAPIDAPI_HOST);
            return curl_slist_append(slist, (std::string(ADSBEX_RAPIDAPI_KEY)+theKey).c_str());
        }
        default:
            return NULL;
    }
    
    
}

// read header and parse for request limit/remaining
size_t ADSBExchangeConnection::ReceiveHeader(char *buffer, size_t size, size_t nitems, void *)
{
    const size_t len = nitems * size;
    static size_t lenRLimit  = strlen(ADSBEX_RAPIDAPI_RLIMIT);
    static size_t lenRRemain = strlen(ADSBEX_RAPIDAPI_RREMAIN);
    char num[50];

    // Limit?
    if (len > lenRLimit &&
        memcmp(buffer, ADSBEX_RAPIDAPI_RLIMIT, lenRLimit) == 0)
    {
        const size_t copyCnt = std::min(len-lenRLimit,sizeof(num)-1);
        memcpy(num, buffer+lenRLimit, copyCnt);
        num[copyCnt]=0;                 // zero termination
        dataRefs.ADSBExRLimit = std::atol(num);
    }
    // Remining?
    else if (len > lenRRemain &&
             memcmp(buffer, ADSBEX_RAPIDAPI_RREMAIN, lenRRemain) == 0)
    {
        const size_t copyCnt = std::min(len-lenRRemain,sizeof(num)-1);
        memcpy(num, buffer+lenRRemain, copyCnt);
        num[copyCnt]=0;                 // zero termination
        dataRefs.ADSBExRRemain = std::atol(num);
    }

    // always say we processed everything, otherwise HTTP processing would stop!
    return len;
}

//
// MARK: Static Test for ADSBEx key
//

std::future<bool> futADSBExKeyValid;
bool bADSBExKeyTestRunning = false;

// Which type of key did the user enter?
ADSBExchangeConnection::keyTypeE ADSBExchangeConnection::GetKeyType (const std::string theKey)
{
    if (theKey.empty())
        return ADSBEX_KEY_NONE;
    // for the old-style key we just count hyphens...don't be tooooo exact
    else if (std::count(theKey.begin(), theKey.end(), '-') == 4)
        return ADSBEX_KEY_EXCHANGE;
    // all else is assume new style
    else
        return ADSBEX_KEY_RAPIDAPI;
}

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
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_TestADSBEx");

    bool bResult = false;
    char curl_errtxt[CURL_ERROR_SIZE];
    std::string readBuf;
    
    // differentiate based on key type
    keyTypeE testKeyTy = GetKeyType(newKey);
    if (!testKeyTy) return false;
    
    const char* sURL = testKeyTy == ADSBEX_KEY_EXCHANGE ? ADSBEX_VERIFY_KEY_URL   : ADSBEX_VERIFY_RAPIDAPI;
    const char* sERR = testKeyTy == ADSBEX_KEY_EXCHANGE ? ADSBEX_ERR              : ADSBEX_RAPID_ERR;
    const char* sNOK = testKeyTy == ADSBEX_KEY_EXCHANGE ? ADSBEX_NO_API_KEY       : ADSBEX_NO_RAPIDAPI_KEY;
    
    // initialize the CURL handle
    CURL *pCurl = curl_easy_init();
    if (!pCurl) {
        LOG_MSG(logERR,ERR_CURL_EASY_INIT);
        return false;
    }
    
    // prepare the handle with the right options
    readBuf.reserve(CURL_MAX_WRITE_SIZE);
    curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, dataRefs.GetNetwTimeout());
    curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, curl_errtxt);
    curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, testKeyTy == ADSBEX_KEY_RAPIDAPI ? ReceiveHeader : NULL);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, DoTestADSBExAPIKeyCB);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &readBuf);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    curl_easy_setopt(pCurl, CURLOPT_URL, sURL);
    
    // prepare the additional HTTP header required for API key
    struct curl_slist* slist = MakeCurlSList(testKeyTy, newKey);
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
        
        // Check HTTP return code
        switch (httpResponse) {
            case HTTP_OK:
                // check for msg/message saying "wrong key":
                if (readBuf.find(sERR) != std::string::npos &&
                    readBuf.find(sNOK) != std::string::npos)
                {
                    // definitely received an error response
                    SHOW_MSG(logERR, ERR_ADSBEX_KEY_FAILED);
                }
                // check what we received in the buffer: an "ac" array, or both 'total' and 'now'?
                else if (readBuf.find("\"" ADSBEX_AIRCRAFT_ARR "\"") != std::string::npos ||
                         (readBuf.find(ADSBEX_TOTAL) != std::string::npos &&
                          readBuf.find(ADSBEX_NOW) != std::string::npos))
                {
                    // looks like a valid response containing a/c info
                    bResult = true;
                    dataRefs.SetADSBExAPIKey(newKey);
                    dataRefs.SetChannelEnabled(DR_CHANNEL_ADSB_EXCHANGE_ONLINE, true);
                    // Reset any RAPID API request count if talking to ADSBEx directly
                    if (testKeyTy != ADSBEX_KEY_RAPIDAPI)
                        dataRefs.ADSBExRLimit = dataRefs.ADSBExRRemain = 0;
                    SHOW_MSG(logMSG, MSG_ADSBEX_KEY_SUCCESS);
                }
                else
                {
                    // somehow an unknown answer...
                    SHOW_MSG(logERR, ERR_ADSBEX_KEY_UNKNOWN);
                }
                break;
                
            case HTTP_UNAUTHORIZED:
            case HTTP_FORBIDDEN:
                SHOW_MSG(logERR, ERR_ADSBEX_KEY_FAILED);
                break;

            default:
                SHOW_MSG(logERR, ERR_ADSBEX_KEY_TECH, (int)httpResponse, ERR_HTTP_NOT_OK);
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

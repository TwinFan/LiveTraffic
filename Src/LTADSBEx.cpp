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
LTChannel(DR_CHANNEL_ADSB_EXCHANGE_ONLINE),
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
    if (!errTxt.empty()) {
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
    double adsbxTime = jog_n(pObj, ADSBEX_TIME)  / 1000.0;
    if (adsbxTime > JAN_FIRST_2019)
        // if reasonable add this to our time offset calculation
        dataRefs.ChTsOffsetAdd(adsbxTime);
    
    // Cut-off time: We ignore tracking data, which is "in the past" compared to simTime
    const double tsCutOff = dataRefs.GetSimTime();
    
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
        
        // the key: transponder Icao code
        LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO,
                                     jog_s(pJAc, ADSBEX_TRANSP_ICAO));
        
        // not matching a/c filter? -> skip it
        if (!acFilter.empty() && (fdKey != acFilter))
        {
            continue;
        }
        
        // ADS-B returns Java tics, that is milliseconds, we use seconds
        const double posTime = jog_sn(pJAc, ADSBEX_POS_TIME) / 1000.0;
        // skip stale data
        if (posTime <= tsCutOff)
            continue;
        
        // ADSBEx, especially the RAPID API version, returns
        // aircraft regardless of distance. To avoid planes
        // created and immediately removed due to distanced settings
        // we continue only if pos is within wanted range
        positionTy pos (jog_sn_nan(pJAc, ADSBEX_LAT),
                        jog_sn_nan(pJAc, ADSBEX_LON),
                        NAN,
                        posTime);
        if ( pos.dist(viewPos) > dataRefs.GetFdStdDistance_m() )
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
            
            // -- fill static data --
            LTFlightData::FDStaticData stat;
            stat.reg =        jog_s(pJAc, ADSBEX_REG);
            stat.country =    jog_s(pJAc, ADSBEX_COUNTRY);
            stat.acTypeIcao = jog_s(pJAc, ADSBEX_AC_TYPE_ICAO);
            stat.mil =        jog_sb(pJAc, ADSBEX_MIL);
            stat.trt          = transpTy(jog_sl(pJAc,ADSBEX_TRT));
            stat.opIcao =     jog_s(pJAc, ADSBEX_OP_ICAO);
            stat.call =       jog_s(pJAc, ADSBEX_CALL);

            // -- dynamic data --
            LTFlightData::FDDynamicData dyn;
            
            // non-positional dynamic data
            dyn.radar.code =        jog_sl(pJAc, ADSBEX_RADAR_CODE);
            dyn.gnd =               jog_sb(pJAc, ADSBEX_GND);
            dyn.heading =           jog_sn_nan(pJAc, ADSBEX_HEADING);
            dyn.spd =               jog_sn(pJAc, ADSBEX_SPD);
            dyn.vsi =               jog_sn(pJAc, ADSBEX_VSI);
            dyn.ts =                posTime;
            dyn.pChannel =          this;
            
            // altitude, if airborne; fetch barometric altitude here
            const double alt_ft = dyn.gnd ? NAN : jog_sn_nan(pJAc, ADSBEX_ALT);

            // position: altitude, heading, ground status
            pos.SetAltFt(dataRefs.WeatherAltCorr_ft(alt_ft));
            pos.heading() = dyn.heading;
            pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;
            
            // -- Ground vehicle identification --
            // ADSBEx doesn't send a clear indicator, but data analysis
            // suggests that EngType/Mount == 0 is a good indicator
            if (stat.acTypeIcao.empty() &&      // don't know a/c type yet
                dyn.gnd &&                      // on the ground
                dyn.spd < 50.0 &&               // reasonable speed
                stat.engType == 0 &&            // no engines
                stat.engMount == 0)
            {
                // we assume ground vehicle
                stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
            }

            // update the a/c's master data
            fd.UpdateData(std::move(stat));
            
            // position is rather important, we check for validity
            if ( pos.isNormal(true) ) {
                fd.AddDynData(dyn, 0, 0, &pos);
            }
            else
                LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());

        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        }
    }
    
    // cleanup JSON
    json_value_free (pRoot);
    
    // success
    return true;
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
                // check what we received in the buffer
                else if (readBuf.find(ADSBEX_TOTAL) != std::string::npos &&
                    readBuf.find(ADSBEX_TIME) != std::string::npos)
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

//
//MARK: ADS-B Exchange Historical Data
//
ADSBExchangeHistorical::ADSBExchangeHistorical (std::string base,
                                                std::string fallback ) :
LTChannel(DR_CHANNEL_ADSB_EXCHANGE_HISTORIC),
LTFileChannel(),
LTFlightDataChannel()
{
    // determine full path (might be local to XP System Path)
    pathBase = LTCalcFullPath(base);
    // must contain _something_
    if ( LTNumFilesInPath (pathBase) < 1) {
        // first path didn't work, try fallback
        if ( !fallback.empty() ) {
            SHOW_MSG(logWARN,ADSBEX_HIST_TRY_FALLBACK,pathBase.c_str());
            // determine full path (might be local to XP System Path)
            pathBase = LTCalcFullPath(fallback);
            // must contain _something_
            if ( LTNumFilesInPath (pathBase) < 1) {
                SetValid(false,false);
                SHOW_MSG(logERR,ADSBEX_HIST_FALLBACK_EMPTY,pathBase.c_str());
            } else {
                SetValid(true);
            }
        } else {
            // there is no fallback...so that's it
            SetValid(false,false);
            SHOW_MSG(logERR,ADSBEX_HIST_PATH_EMPTY,pathBase.c_str());
        }
    } else {
        // found something in the primary path, good!
        SetValid(true);
    }
}

// reads ADS-B data from historical files into the buffer 'fileData'.
// ADS-B provides one file per minute of the day (UTC)
// https://www.adsbexchange.com/data/
bool ADSBExchangeHistorical::FetchAllData (const positionTy& pos)
{
    // save last path verified to minimize I/O
    std::string lastCheckedPath;
    
    // the bounding box: only aircraft in this box are considered
    boundingBoxTy box (pos, dataRefs.GetFdStdDistance_m());
    
    // simulated "now" in full minutes UTC
    time_t now = stripSecs(dataRefs.GetSimTime());
    
    // first-time read?
    if ( !zuluLastRead )
        zuluLastRead = now;
    else {
        // make sure we don't read too much...
        // if for whatever reason we haven't had time to read files in the past 5 minutes
        zuluLastRead = std::max(zuluLastRead,
                                now - 5 * SEC_per_M);
    }
    
    // Loop over files until 1 minutes ahead of (current sim time + regular buffering)
    for (time_t until = now + (dataRefs.GetFdBufPeriod() + SEC_per_M);
         zuluLastRead <= until;
         zuluLastRead += SEC_per_M)      // increase by one minute per iteration
    {
        // put together path and file name
        char sz[50];
        struct tm tm_val;
        gmtime_s(&tm_val, &zuluLastRead);
        snprintf(sz,sizeof(sz),ADSBEX_HIST_DATE_PATH,
                 dataRefs.GetDirSeparator()[0],
                 tm_val.tm_year + 1900,
                 tm_val.tm_mon + 1,
                 tm_val.tm_mday);
        std::string pathDate = pathBase + sz;
        
        // check path, if not the same as last iteration
        if ((pathDate != lastCheckedPath) &&
            (LTNumFilesInPath(pathDate) < 1)) {
            SetValid(false,false);
            SHOW_MSG(logERR,ADSBEX_HIST_PATH_EMPTY,pathDate.c_str());
            return false;
        } else {
            lastCheckedPath = pathDate;     // path good, don't check again
        }
        
        // add hour-based file name
        snprintf(sz,sizeof(sz),ADSBEX_HIST_FILE_NAME,
                 dataRefs.GetDirSeparator()[0],
                 tm_val.tm_year + 1900,
                 tm_val.tm_mon + 1,
                 tm_val.tm_mday,
                 tm_val.tm_hour, tm_val.tm_min);
        pathDate += sz;
        
        // open the file
        std::ifstream f(pathDate);
        if ( !f ) {                         // couldn't open
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            SHOW_MSG(logERR,ADSBEX_HIST_FILE_ERR,pathDate.c_str(),sErr);
            IncErrCnt();
            return false;
        }
        LOG_MSG(logINFO,ADSBEX_HIST_READ_FILE,pathDate.c_str());
        
        // read the first line, which is expected to end with "acList":[
        char lnBuf[5000];                   // lines with positional data can get really long...
        f.getline(lnBuf,sizeof(lnBuf));
        if (!f || f.gcount() < ADSBEX_HIST_MIN_CHARS ||
            !strstr(lnBuf,ADSBEX_HIST_LN1_END))
        {
            // no significant number of chars read or end of line unexpected
            SHOW_MSG(logERR,ADSBEX_HIST_LN1_UNEXPECT,pathDate.c_str());
            IncErrCnt();
            return false;
        }
        
        // store a start-of-file indicator
        listFd.emplace_back(std::string(ADSBEX_HIST_START_FILE) + sz);
        
        // now loop over the positional lines
        int cntErr = 0;                     // count errors to bail out if file too bad
        
        // we make use of the apparent fact that the hist files
        // contain one aircraft per line. We decide here already if the line
        // is relevant to us (based on position falling into our bounding box)
        // as we don't want to run 22 MB through the JSON parser in memory
        for ( int i = 2; f.good() && !f.eof(); i++ ) {
            // read a fresh line from the filex
            lnBuf[0]=0;
            f.getline(lnBuf,sizeof(lnBuf));
            
            // just ignore the last line, this is closing line or even empty
            if ( f.eof() || strstr(lnBuf,ADSBEX_HIST_LAST_LN) == lnBuf)
                break;
            
            // otherwise it shoud contain reasonable info
            if ( !f ||
                f.gcount() < ADSBEX_HIST_MIN_CHARS ||
                !strchr (lnBuf,'{')         // should also contain '{'
                ) {
                // no significant number of chars read, looks invalid, skip
                SHOW_MSG(logWARN,ADSBEX_HIST_LN_ERROR,i,pathDate.c_str());
                if (++cntErr > ADSBEX_HIST_MAX_ERR_CNT) {
                    // this file is too bad...skip rest
                    IncErrCnt();
                    break;
                }
                continue;
            }
            
            // there are occasionally lines starting with the comma
            // (which is supposed to be at the end of the line)
            // remove that comma, otherwise the line is no valid JSON by itself
            if (lnBuf[0] != '{')
                memmove(lnBuf,strchr(lnBuf,'{'),strlen(strchr(lnBuf,'{'))+1);
            
            // there are two good places for positional info:
            // tags Lat/Long or the trail after tag Cos
            positionTy acPos;
            const char* pLat = strstr(lnBuf,ADSBEX_HIST_LAT);
            const char* pLong = strstr(lnBuf,ADSBEX_HIST_LONG);
            const char* pCos = strstr(lnBuf,ADSBEX_HIST_COS);
            if ( pLat && pLong ) {          // found Lat/Long tags
                pLat += strlen(ADSBEX_HIST_LAT);
                pLong += strlen(ADSBEX_HIST_LONG);
                acPos.lat() = atof(pLat);
                acPos.lon() = atof(pLong);
            } else if ( pCos ) {            // only found trails? (rare...)
                pCos += strlen(ADSBEX_HIST_COS);  // move to _after_ [
                // there follow: lat, lon, time, alt, lat, lon, time, alt...
                // we take the first lat/lon
                acPos.lat() = atof(pCos);
                // move on to lat - after the comma
                pCos = strchr(pCos,',');
                if ( !pCos ) {                  // no comma is _not_ valid,
                    SHOW_MSG(logWARN,ADSBEX_HIST_LN_ERROR,i,pathDate.c_str());
                    if (++cntErr > ADSBEX_HIST_MAX_ERR_CNT) {
                        // this file is too bad...skip rest
                        IncErrCnt();
                        break;
                    }
                    continue;
                }
                acPos.lon() = atof(++pCos);
            } else                          // no positional info...
                continue;                   // valid but useless for our purposes -> ignore line
            
            // if the position is within the bounding box then we save for later
#ifdef DEBUG
            std::string dbg (acPos.dbgTxt());
            dbg += " in ";
            dbg += box;
#endif
            if ( box.contains(acPos) )
                listFd.emplace_back(lnBuf);
        }
        
        // store an end-of-file indicator
        listFd.emplace_back(std::string(ADSBEX_HIST_END_FILE) + sz);
        
        // close the file
        f.close();
    }
    
    // Success
    return true;
}


bool ADSBExchangeHistorical::ProcessFetchedData (mapLTFlightDataTy& fdMap)
{
    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // data is expected in listFd, one line per flight data
    // iterate the lines
    for (std::list<std::string>::iterator lnIter = listFd.begin();
         lnIter != listFd.end();
         ++lnIter)
    {
        // must be a start-of-file indicator
        LOG_ASSERT(lnIter->find(ADSBEX_HIST_START_FILE) == 0);
        
        // *** Per a/c select just one best receiver, discard other lines ***
        // Reason is that despite any attempts of smoothening the flight path
        // when combining multiple receivers there still are spikes causing
        // zig zag routes or sudden up/down movement.
        // So instead of trying to combine the data from multiple lines
        // (=receivers) for one a/c we _select_ one of these lines and only
        // work with that one line.
        mapFDSelectionTy selMap;              // selected lines of current file
        
        // loop over flight data lines of current file
        for (++lnIter;
             lnIter != listFd.end() && lnIter->find(ADSBEX_HIST_END_FILE) == std::string::npos;
             ++lnIter)
        {
            // the line as read from the historic file
            std::string& ln = *lnIter;
            
            // each individual line should work as a JSON object
            JSON_Value* pRoot = json_parse_string(ln.c_str());
            if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
            JSON_Object* pJAc = json_object(pRoot);
            if (!pJAc) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
            
            // the key: transponder Icao code
            LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO,
                                         jog_s(pJAc, ADSBEX_TRANSP_ICAO));
            
            // not matching a/c filter? -> skip it
            if  (!acFilter.empty() && (fdKey != acFilter))
            {
                json_value_free (pRoot);
                continue;
            }
            
            // the receiver we are dealing with right now
            int rcvr = (int)jog_l(pJAc, ADSBEX_RCVR);
            
            // variables we need for quality indicator calculation
            int sig =               (int)jog_l(pJAc, ADSBEX_SIG);
            JSON_Array* pCosList =  json_object_get_array(pJAc, ADSBEX_COS);
            int cosCount =          int(json_array_get_count(pCosList)/4);
            
            // quality is made up of signal level, number of elements of the trail
            int qual = (sig + cosCount);
            
            // and we award the currently used receiver a 50% award: we value to
            // stay with the same receiver minute after minute (file-to-file)
            // as this is more likely to avoid spikes when connection this
            // minute's trail with last minute's trail
            

            
            mapLTFlightDataTy::iterator fdIter = fdMap.find(fdKey);
            if ( fdIter != fdMap.end() && fdIter->second.GetRcvr() == rcvr ) {
                qual *= 3;
                qual /= 2;
            }
            
            // did we find another line for this a/c earlier in this file?
            mapFDSelectionTy::iterator selIter = selMap.find(fdKey.key);
            
            // if so we have to compare qualities, the better one survives
            if ( selIter != selMap.end() )
            {
                // now...is quality better than the earlier line(s) in the file?
                if ( qual > selIter->second.quality ) {
                    // replace the content, _move_ the line here...we don't need copies
                    selIter->second.quality = qual;
                    selIter->second.ln = std::move(ln);
                }
            } else {
                // first time we see this a/c in this file -> add to map
                selMap.emplace(std::make_pair(fdKey.key, FDSelection {qual, std::move(ln)} ));
            }
            
            // done with interpreting this line
            json_value_free (pRoot);
            
        } // loop over lines of current files
        
        // now we only process the selected lines in order to actually
        // add flight data to our processing
        for ( mapFDSelectionTy::value_type selVal: selMap )
        {
            // each individual line should work as a JSON object
            JSON_Value* pRoot = json_parse_string(selVal.second.ln.c_str());
            if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
            JSON_Object* pJAc = json_object(pRoot);
            if (!pJAc) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
            
            try {
                // from here on access to fdMap guarded by a mutex
                // until FD object is inserted and updated
                std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
                
                // get the fd object from the map, key is the transpIcao
                // this fetches an existing or, if not existing, creates a new one
                LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO,
                                             selVal.first);
                LTFlightData& fd = fdMap[fdKey];
                
                // also get the data access lock once and for all
                // so following fetch/update calls only make quick recursive calls
                std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
                // now that we have the detail lock we can release the global one
                mapFdLock.unlock();

                // completely new? fill key fields
                if ( fd.empty() )
                    fd.SetKey(fdKey);
                
                // fill static
                {
                    LTFlightData::FDStaticData stat;
                    stat.reg =        jog_s(pJAc, ADSBEX_REG);
                    stat.country =    jog_s(pJAc, ADSBEX_COUNTRY);
                    stat.acTypeIcao = jog_s(pJAc, ADSBEX_AC_TYPE_ICAO);
                    stat.mil =        jog_sb(pJAc, ADSBEX_MIL);
                    stat.trt          = transpTy(jog_sl(pJAc,ADSBEX_TRT));
                    stat.opIcao =     jog_s(pJAc, ADSBEX_OP_ICAO);
                    stat.call =       jog_s(pJAc, ADSBEX_CALL);

                    // update the a/c's master data
                    fd.UpdateData(std::move(stat));
                }
                
                // dynamic data
                LTFlightData::FDDynamicData dyn;
                
                // ADS-B returns Java tics, that is milliseconds, we use seconds
                double posTime = jog_n(pJAc, ADSBEX_POS_TIME) / 1000.0;
                
                // non-positional dynamic data
                dyn.radar.code =        jog_sl(pJAc, ADSBEX_RADAR_CODE);
                dyn.gnd =               jog_sb(pJAc, ADSBEX_GND);
                dyn.heading =           jog_sn_nan(pJAc, ADSBEX_HEADING);
                dyn.spd =               jog_sn(pJAc, ADSBEX_SPD);
                dyn.vsi =               jog_sn(pJAc, ADSBEX_VSI);
                dyn.ts =                posTime;
                dyn.pChannel =          this;
                
                fd.AddDynData(dyn,
                              (int)jog_sl(pJAc, ADSBEX_RCVR),
                              (int)jog_sl(pJAc, ADSBEX_SIG));
                
                // altitude, if airborne
                const double alt_ft = dyn.gnd ? NAN : jog_sn_nan(pJAc, ADSBEX_ELEVATION);
                
                // position data, including short trails
                positionTy mainPos (jog_sn_nan(pJAc, ADSBEX_LAT),
                                    jog_sn_nan(pJAc, ADSBEX_LON),
                                    // ADSB data is feet, positionTy expects meter
                                    alt_ft * M_per_FT,
                                    posTime,
                                    dyn.heading);
                
                // position is kinda important...we continue only with valid data
                if ( mainPos.isNormal() )
                {
                    // we need a good take on the ground status of mainPos
                    // for later landing detection
                    mainPos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;
                    
                    // FIXME: Called from outside main thread,
                    //        can produce wrong terrain alt (2 cases here)
                    //        Probably: Just add positions and let updated
                    //                  AppendNewPos / CalcNewPos do the job of landing detection
                    fd.TryDeriveGrndStatus(mainPos);
                    
                    // Short Trails ("Cos" array), if available
                    bool bAddedTrails = false;
                    dequePositionTy trails;
                    JSON_Array* pCosList = json_object_get_array(pJAc, ADSBEX_COS);
                    
                    // short-cut: if we are in the air then skip adding trails
                    // they might be good on the ground...in the air the
                    // positions can be too inaccurate causing jumps in speed, vsi, heading etc...
                    if (mainPos.f.onGrnd == GND_OFF)
                        pCosList = NULL;
                    
                    // found trails and there are at least 2 quadrupels, i.e. really a "trail" not just a single pos?
                    if (json_array_get_count(pCosList) >= 8) {
                        if (json_array_get_count(pCosList) % 4 == 0)    // trails should be made of quadrupels
                            // iterate trail data in form of quadrupels (lat,lon,timestamp,alt):
                            for (size_t i=0; i< json_array_get_count(pCosList); i += 4) {
                                trails.emplace_back(json_array_get_number(pCosList, i),         // latitude
                                                    json_array_get_number(pCosList, i+1),       // longitude
                                                    json_array_get_number(pCosList, i+3) * M_per_FT,     // altitude (convert to meter)
                                                    json_array_get_number(pCosList, i+2) / 1000.0);      // timestamp (convert form ms to s)
                                // only keep new trail if it is a valid position
                                const positionTy& addedTrail = trails.back();
                                if ( !addedTrail.isNormal() ) {
                                    LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),addedTrail.dbgTxt().c_str());
                                    trails.pop_back();  // otherwise remove right away
                                }
                            }
                        else
                            LOG_MSG(logERR,ADSBEX_HIST_TRAIL_ERR,fdKey.c_str(),posTime);
                    }
                    
                    // if we did find enough trails work on them
                    if ( trails.size() >= 2 ) {
                        // ideally, the main position should have the exact same timestamp
                        // as the last trail item and a similar altitude (+/- 500m)
                        positionTy& lastTrail = trails.back();
#ifdef DEBUG
                        std::string dbgMain(mainPos.dbgTxt());
                        std::string dbgLast(lastTrail.dbgTxt());
#endif
                        if (mainPos.hasSimilarTS(lastTrail) &&
                            (std::abs(mainPos.alt_m() - lastTrail.alt_m()) < 500))
                        {
                            // reason we check this is: altitude data in the trails
                            // seems to always be a multiple of 500ft, which would mean
                            // that even during climb/decend the plane would do
                            // kinda "step climbs" of 500ft, which looks unrealistic.
                            // Altitude in the main position is exact though.
                            // We take that main altitude for the last trail quadrupel:
                            lastTrail.alt_m() = mainPos.alt_m();
                            
                            // Now we look into our already known positions for the
                            // last known position before mainPos
                            // and calculate a smooth climb/descend path between the two:
                            const dequePositionTy& posList = fd.GetPosDeque();
#ifdef DEBUG
                            std::string dbgPosList(positionDeque2String(posList));
#endif
                            dequePositionTy::const_iterator iBef =
                            positionDequeFindBefore(posList, mainPos.ts() - SIMILAR_TS_INTVL);
                            // if we find an earlier position we take that as reference,
                            // otherwise we fall back to the first trails element:
                            const positionTy& refPos = iBef == posList.end() ?
                            trails.front() : *iBef;
                            
                            // *** Landing Detection ***
                            // Issue: Altitude of trails is untrustworthy,
                            //        but reliable main positions are historically
                            //        available only every 60 seconds.
                            //        During Final/Landing that might be too long:
                            //        Imagine last position is 30m above ground,
                            //        but 60 seconds later only we can touch down? That's a looooong flare...
                            // So: We try determining this case and then
                            //     replace altitude with VSI-based calculation.
                            if (!refPos.IsOnGnd() && mainPos.IsOnGnd())
                            {
                                // aircraft model to use
                                const LTAircraft::FlightModel& mdl =
                                fd.hasAc() ? fd.GetAircraft()->mdl :
                                LTAircraft::FlightModel::FindFlightModel(fd.GetUnsafeStat().acTypeIcao);
                                
                                
                                // this is the Landing case
                                // we look for the VSI of the last two already
                                // known off-ground positions, one is refPos
                                const double vsiBef =
                                // is there an even earlier pos before refPos in the list?
                                iBef != posList.begin() ?
                                std::prev(iBef)->between(refPos).vsi : // take the vsi between these two
                                mdl.VSI_FINAL * Ms_per_FTm;            // no, assume reasonable vsi
                                
                                const double vsiDirectMain = refPos.vsi_m(mainPos);
                                
                                if (vsiBef < -(mdl.VSI_STABLE * Ms_per_FTm) && vsiBef < vsiDirectMain)
                                {
                                    // reasonable negative VSI, which is less (steeper) than direct way down to mainPos
                                    for(positionTy& posIter: trails) {
                                        // calc altitude based on vsiBef, as soon as we touch ground it will be normalized to terrain altitude
                                        posIter.alt_m() = refPos.alt_m() + vsiBef * (posIter.ts()-refPos.ts());
                                        posIter.f.onGrnd = GND_UNKNOWN;
                                        fd.TryDeriveGrndStatus(posIter);
                                        // only add pos if not off ground
                                        // (see above: airborne we don't want too many positions)
                                        if (posIter.f.onGrnd != GND_OFF)
                                            fd.AddNewPos(posIter);
                                    }
                                    
                                    // we added trails, don't add any more
                                    bAddedTrails = true;
                                }
                            }
                            
                            // didn't add anything yet?
                            // (this includes the standard non-landing case
                            //  and the landing case in which we couldn't determine VSI)
                            if (!bAddedTrails)
                            {
                                // this is the non-landing case
                                // adapt all altitudes to form a smooth path
                                // from refPos to mainPos
                                // and add the altered position to our known positions
                                const double altDiff = lastTrail.alt_m() - refPos.alt_m();
                                const double tsDiff  = lastTrail.ts()  - refPos.ts();
                                
                                // only valid if there is _any_ timestamp difference between first and last trail pos
                                // there _should_ be...but sometimes there simply isn't
                                if ( tsDiff > 0 ) {
                                    for(positionTy& posIter: trails) {
                                        posIter.alt_m() = refPos.alt_m() + altDiff * (posIter.ts()-refPos.ts()) / tsDiff;
                                        posIter.f.onGrnd = GND_UNKNOWN;
                                        fd.AddNewPos(posIter);
                                    }
                                    bAddedTrails = true;
                                }
                            }
#ifdef DEBUG
                            dbgPosList = positionDeque2String(posList);
#endif
                        }
                    }
                    
                    // if we didn't add the trails then just add the main position
                    if (!bAddedTrails) {
                        // no trails (or just one pos): we just add the main position
                        fd.AddNewPos(mainPos);
                    }
                }
                else {
                    LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),mainPos.dbgTxt().c_str());
                }
            } catch(const std::system_error& e) {
                LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
            }
            
            // cleanup JSON
            json_value_free (pRoot);
        } // for all _selected_ lines of an input file
        
    } // for all input files
    
    // list is processed
    listFd.clear();
    return true;
}


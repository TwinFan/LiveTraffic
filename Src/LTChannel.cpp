/// @file       LTChannel.cpp
/// @brief      Abstract base classes for any class reading tracking data from providers
/// @details    Network error handling .\n
///             Handles initializing and calling CURL library.\n
///             Global functions controlling regular requests to tracking data providers.
/// @author     Birger Hoppe
/// @copyright  (c) 2020 Birger Hoppe
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

#include <fstream>

// access to chrono literals like s for seconds
using namespace std::chrono_literals;

//
//MARK: Global
//

// set when first flight data is inserted
// used to calculate countdown while position buffer fills up
double initTimeBufFilled = 0;       // in 'simTime'

// global list of a/c for which static data is yet missing
// (reset with every network request cycle)
vecAcStatUpdateTy LTACMasterdataChannel::vecAcStatUpdate;
// Lock controlling multi-threaded access to `listAcSTatUpdate`
std::mutex LTACMasterdataChannel::vecAcStatMutex;

// Thread synch support (specifically for stopping them)
std::thread FDMainThread;               // the main thread (LTFlightDataSelectAc)
std::thread CalcPosThread;              // the thread for pos calc (TriggerCalcNewPos)
std::mutex  FDThreadSynchMutex;         // supports wake-up and stop synchronization
std::condition_variable FDThreadSynchCV;
volatile bool bFDMainStop = true;       // will be reset once the main thread starts
std::chrono::time_point<std::chrono::steady_clock> gNextWakeup;  ///< when to wake up next for network requests

// the global vector of all flight and master data connections
listPtrLTChannelTy    listFDC;

//
// MARK: Parson helpers
//

// tests for 'null', return ptr to value if wanted
bool jog_is_null (const JSON_Object *object, const char *name, JSON_Value** ppValue)
{
    JSON_Value* pJSONVal = json_object_get_value(object, name);
    if (ppValue)
        *ppValue = pJSONVal;
    return !pJSONVal || json_type(pJSONVal) == JSONNull;
}

bool jag_is_null (const JSON_Array *array,
                  size_t idx,
                  JSON_Value** ppValue)
{
    JSON_Value* pJSONVal = json_array_get_value (array, idx);
    if (ppValue)
        *ppValue = pJSONVal;
    return !pJSONVal || json_type(pJSONVal) == JSONNull;
}

// access to JSON string fields, with NULL replaced by ""
const char* jog_s (const JSON_Object *object, const char *name)
{
    const char* s = json_object_get_string ( object, name );
    return s ? s : "";
}

// access to JSON number fields, encapsulated as string, with NULL replaced by 0
double jog_sn (const JSON_Object *object, const char *name)
{
    const char* s = json_object_get_string ( object, name );
    return s ? strtod(s,NULL) : 0.0;
}

// access to JSON number with 'null' returned as 'NAN'
double jog_n_nan (const JSON_Object *object, const char *name)
{
    JSON_Value* pJSONVal = NULL;
    if (!jog_is_null(object, name, &pJSONVal))
        return json_value_get_number (pJSONVal);
    else
        return NAN;
}

double jog_sn_nan (const JSON_Object *object, const char *name)
{
    const char* s = json_object_get_string ( object, name );
    return s ? strtod(s,NULL) : NAN;
}


// access to JSON array string fields, with NULL replaced by ""
const char* jag_s (const JSON_Array *array, size_t idx)
{
    const char* s = json_array_get_string ( array, idx );
    return s ? s : "";
}

// access to JSON array number fields, encapsulated as string, with NULL replaced by 0
double jag_sn (const JSON_Array *array, size_t idx)
{
    const char* s = json_array_get_string ( array, idx );
    return s ? strtod(s,NULL) : 0.0;
}

// access to JSON array number field with 'null' returned as 'NAN'
double jag_n_nan (const JSON_Array *array, size_t idx)
{
    JSON_Value* pJSONVal = NULL;
    if (!jag_is_null(array, idx, &pJSONVal))
        return json_value_get_number (pJSONVal);
    else
        return NAN;
}

//
//MARK: LTChannel
//

void LTChannel::SetValid (bool _valid, bool bMsg)
{
    // (re)set to valid channel
    if (_valid) {
        if (!bValid && bMsg) {
            LOG_MSG(logINFO, INFO_CH_RESTART, ChName());
        }
        errCnt = 0;
        bValid = true;
    } else {
        // set invalid
        bValid = false;
        if (bMsg)
            SHOW_MSG(logFATAL,ERR_CH_INVALID,ChName());
    }
}

// increases error counter, returns if (still) valid
bool LTChannel::IncErrCnt()
{
    if (++errCnt > CH_MAC_ERR_CNT) {
        SetValid(false, false);
        SHOW_MSG(logFATAL,ERR_CH_MAX_ERR_INV,ChName());
    }
    return IsValid();
}

// decrease error counter
void LTChannel::DecErrCnt()
{
    if (errCnt > 0)
        --errCnt;
}


// enabled-status is maintained by global dataRef object
bool LTChannel::IsEnabled() const
{
    return IsValid() && dataRefs.IsChannelEnabled(channel);
}

void LTChannel::SetEnable (bool bEnable)
{
    // if we enable we also set the channel valid
    if (bEnable)
        SetValid(true);
    
    // then we actually enable
    dataRefs.SetChannelEnabled(channel,bEnable);
}

std::string LTChannel::GetStatusText () const
{
    // invalid (after errors)? Just disabled/off? Or active (but not a source of tracking data)?
    if (!IsValid())                         return "Invalid";
    if (!IsEnabled())                       return "Off";
    if (GetChType() != CHT_TRACKING_DATA)   return "Active";
    
    // An active source of tracking data...for how many aircraft?
    char buf[50];
    snprintf (buf, sizeof(buf), "Active, serving %d aircraft",
              GetNumAcServed());
    return std::string(buf);
}

//
//MARK: LTFlightDataChannel
//

// how many a/c do we feed when counted last?
int LTFlightDataChannel::GetNumAcServed () const
{
    // only try to re-count every second, and needs the mapFd lock
    if (timeLastAcCnt + 1.0f < dataRefs.GetMiscNetwTime()) {
        std::unique_lock<std::mutex> lockFd(mapFdMutex, std::try_to_lock);
        if (lockFd) {
            timeLastAcCnt = dataRefs.GetMiscNetwTime();
            numAcServed = 0;                // start counting flight data served by _this_ channel
            for (const mapLTFlightDataTy::value_type& p: mapFd) {
                const LTChannel* pCh = nullptr;
                if (p.second.GetCurrChannel(pCh) && pCh == this)
                    ++numAcServed;
            }
        }
    }

    // return whatever we know
    return numAcServed;
}

//
//MARK: LTACMasterdata
//

// fetches a/c master data and updates the respective static data in the FDMap
bool LTACMasterdataChannel::UpdateStaticData (const LTFlightData::FDKeyTy& keyAc,
                                              const LTFlightData::FDStaticData& dat)
{
    // Find and update respective flight data
    try {
        // from here on access to fdMap guarded by a mutex
        std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
        
        // get the fd object from the map, key is the transpIcao
        mapLTFlightDataTy::iterator fdIter = mapFd.find(keyAc);
        if (fdIter == mapFd.end())
            return false;                   // not found
        
        // do the actual update
        fdIter->second.UpdateData(dat, NAN, true);
        return true;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
    
    // must have caught an error
    return false;
}

// static function to add key/callSign to list of data,
// for which master data shall be requested by a master data channel
void LTACMasterdataChannel::RequestMasterData (const LTFlightData::FDKeyTy& keyAc,
                                               const std::string callSign,
                                               double distance)
{
    try {
        // multi-threaded access guarded by vecAcStatMutex
        std::lock_guard<std::mutex> lock (vecAcStatMutex);
        
        // just add the request to the request list, uniquely,
        // but update the distance as it serves as a sorting order
        const acStatUpdateTy acUpd (keyAc,callSign,std::isnan(distance) ? UINT_MAX : unsigned(distance));
        vecAcStatUpdateTy::iterator i = std::find(vecAcStatUpdate.begin(),vecAcStatUpdate.end(),acUpd);
        if (i == vecAcStatUpdate.end()) {
            vecAcStatUpdate.push_back(acUpd);
//            LOG_MSG(logDEBUG, "Request added for %s / %s, order = %d",
//                    acUpd.acKey.c_str(), acUpd.callSign.c_str(), acUpd.order);
        }
        else {
//            LOG_MSG(logDEBUG, "Request updated for %s / %s, order = %d (from %d)",
//                    acUpd.acKey.c_str(), acUpd.callSign.c_str(), acUpd.order, i->order);
            i->order = acUpd.order;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "listAcStatUpdate", e.what());
    }
}

void LTACMasterdataChannel::ClearMasterDataRequests ()
{
    try {
        // multi-threaded access guarded by vecAcStatMutex
        std::lock_guard<std::mutex> lock (vecAcStatMutex);

        // remove all processed entries as well as outdated entries,
        // for which the flight no longer exists in mapFd
        // (avoids leaking when no channel works on master data)
        vecAcStatUpdateTy::iterator i = vecAcStatUpdate.begin();
        while(i != vecAcStatUpdate.end()) {
            if (i->HasBeenProcessed() || (mapFd.count(i->acKey) == 0))
                i = vecAcStatUpdate.erase(i);
            else
                ++i;
        }

    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "listAcStatUpdate", e.what());
    }
}


// copy all requested a/c to our private list,
// the global one is refreshed before the next call.
void LTACMasterdataChannel::CopyGlobalRequestList ()
{
    try {
        // multi-threaded access guarded by vecAcStatMutex
        std::lock_guard<std::mutex> lock (vecAcStatMutex);
        
        // Copy global list into local, update order (prio) if already existing, and
        // mark the global record "processed" so it can be cleaned up
        for (acStatUpdateTy& info: vecAcStatUpdate) {
            vecAcStatUpdateTy::iterator i = std::find(vecAc.begin(),vecAc.end(),info);
            if (i == vecAc.end())
                vecAc.push_back(info);
            else
                i->order = info.order;
            info.SetProcessed();
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "listAcStatUpdate", e.what());
    }
    
    // Now sort the list descending by distance, so that the planes closest to us
    // are located at the list's end and are fetched first by pop_back:
    std::sort(vecAc.begin(), vecAc.end(),
              [](const acStatUpdateTy& a, const acStatUpdateTy& b)
              { return a.order > b.order; });
}

//
//MARK: LTOnlineChannel
//

// the one (hence static) output file for logging raw network data
std::ofstream LTOnlineChannel::outRaw;

LTOnlineChannel::LTOnlineChannel () :
pCurl(NULL),
nTimeout(dataRefs.GetNetwTimeout()),
netData((char*)malloc(CURL_MAX_WRITE_SIZE)),      // initial buffer allocation
netDataPos(0), netDataSize(CURL_MAX_WRITE_SIZE),
curl_errtxt{0}, httpResponse(HTTP_OK)
{
    // initialize a CURL handle (sets invalid if it fails)
    InitCurl();
}

LTOnlineChannel::~LTOnlineChannel ()
{
    // close the raw output file
    if (outRaw.is_open()) {
        outRaw.close();
        SHOW_MSG(logWARN, DBG_RAW_FD_STOP, PATH_DEBUG_RAW_FD);
    }

    CleanupCurl();
    if ( netData )
        free ( netData );
}

// basic setup of CURL
bool LTOnlineChannel::InitCurl ()
{
    // is already?
    if ( pCurl ) return true;
    
    // get curl handle
    pCurl = curl_easy_init();
    if ( !pCurl )
    {
        // something went wrong
        LOG_MSG(logFATAL,ERR_CURL_EASY_INIT);
        SetValid(false, false);
        return false;
    }
    
    // define the handle
    curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, nTimeout);
    curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, curl_errtxt);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, LTOnlineChannel::ReceiveData);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    
    // success
    return true;
}

void LTOnlineChannel::CleanupCurl()
{
    // cleanup the CURL handle
    if ( pCurl )
    {
        curl_easy_cleanup(pCurl);
        pCurl = NULL;
    }
}

// static CURL Write Callback, manages the receive buffer
size_t LTOnlineChannel::ReceiveData(const char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t realsize = size * nmemb;
    LTOnlineChannel& me =
    *reinterpret_cast<LTOnlineChannel*>(userdata);
    
    // ensure buffer is big enough for received data plus zero termination
    size_t requBufSize = me.netDataPos + realsize + 1;
    if ( requBufSize > me.netDataSize )
    {
        // we increase the buffer size in fixed increments (and never decrease its size!)
        while ( requBufSize > me.netDataSize ) me.netDataSize += CURL_MAX_WRITE_SIZE;
        me.netData = (char*)realloc(me.netData, me.netDataSize);
        if ( !me.netData )
        {LOG_MSG(logFATAL,ERR_MALLOC,me.netDataSize); me.SetValid(false); return 0;}
    }
    
    // save the received data, ensure zero-termination
    memmove(me.netData + me.netDataPos, ptr, realsize);
    me.netDataPos += realsize;
    me.netData[me.netDataPos] = 0;
    
    // we've taken care of everything
    return realsize;
}

// debug: log raw network data to a log file
void LTOnlineChannel::DebugLogRaw(const char *data, bool bHeader)
{
    // no logging? return (after closing the file if open)
    if (!dataRefs.GetDebugLogRawFD()) {
        if (outRaw.is_open()) {
            outRaw.close();
            SHOW_MSG(logWARN, DBG_RAW_FD_STOP, PATH_DEBUG_RAW_FD);
        }
        return;
    }
    
    // *** Logging enabled ***
    
    // As there are different threads (e.g. in LTRealTraffic), which send data,
    // we guard file writing with a lock, so that no line gets intermingled
    // with another thread's data:
    static std::mutex logRawMutex;
    std::lock_guard<std::mutex> lock(logRawMutex);
    
    // Need to open the file first?
    if (!outRaw.is_open()) {
        // open the file, append to it
        std::string sFileName (LTCalcFullPath(PATH_DEBUG_RAW_FD));
        outRaw.open (sFileName, std::ios_base::out | std::ios_base::app);
        if (!outRaw) {
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            // could not open output file: bail out, decativate logging
            SHOW_MSG(logERR, DBG_RAW_FD_ERR_OPEN_OUT,
                     sFileName.c_str(), sErr);
            dataRefs.SetDebugLogRawFD(false);
            return;
        }
        SHOW_MSG(logWARN, DBG_RAW_FD_START, PATH_DEBUG_RAW_FD);
    }
    
    // Receives modifiable copy of the data
    std::string dupData (data);
    
    // Overwrite client_secret
    // {"grant_type": "password","client_id": 1,"client_secret": "7HTOw2421WBZxrGCksPvez2BG6Yl918uUHAcEWRg","username":
    std::string::size_type pos = dupData.find("\"client_secret\":");
    if (pos != std::string::npos && dupData.size() >= pos + 60)
        dupData.replace(pos+18, 40, "...");
    
    // limit output in case a password or token is found
    pos = dupData.find("\"password\":");
    if (pos != std::string::npos) {
        // just truncate after the password tag so the actual password is gone
        dupData.erase(pos + 12);
        dupData += "...(truncated)...";
    }
    
    pos = dupData.find("\"access_token\":");
    if (pos != std::string::npos) {
        dupData.erase(pos + 26);
        dupData += "...(truncated)...";
    }
    
    // timestamp (numerical and human readable)
    const double now = GetSysTime();
    outRaw.precision(2);
    if (bHeader)
        outRaw
        << std::fixed << now << ' ' << ts2string(now,2)
        << " - SimTime "
        << dataRefs.GetSimTimeString()
        << " - "
        // Channel's name
        << ChName()
        << "\n";
    outRaw
    // the actual given data, stripped from general personal data
    << str_replPers(dupData)
    // newlines + flush
    << std::endl;
}

// fetch flight data from internet (takes time!)
bool LTOnlineChannel::FetchAllData (const positionTy& pos)
{
    CURLcode cc = CURLE_OK;

    // make sure CURL is initialized
    if ( !InitCurl() ) return false;
    
    // ask for the URL
    std::string url (GetURL(pos));
    
    // no url -> don't query
    if (url.empty())
        return false;
    
    // ask for a body of a POST request (to be put into requBody)
    ComputeBody(pos);
    
    // put together the REST request
    curl_easy_setopt(pCurl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(pCurl, CURLOPT_BUFFERSIZE, netDataSize );
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, nTimeout);

    // Set up the request
    curl_easy_setopt(pCurl, CURLOPT_NOBODY,  0);
    if (requBody.empty())
        // HTTPS GET
        curl_easy_setopt(pCurl, CURLOPT_HTTPGET, 1);
    else
        // HTTPS POST
        curl_easy_setopt(pCurl, CURLOPT_POSTFIELDS, requBody.data());
    
    // get fresh data via the internet
    // this will take a second or more...don't try in render loop ;)
    // it is assumed that this is called in a separate thread,
    // hence we can use the simple blocking curl_easy_ call
    netDataPos = 0;                 // fill buffer from beginning
    netData[0] = 0;
    DebugLogRaw(url.c_str());
    if (!requBody.empty())
        DebugLogRaw(requBody.c_str(), false);
    
    // perform the request and take its time
    std::chrono::time_point<std::chrono::steady_clock> tStart = std::chrono::steady_clock::now();
    httpResponse = 0;
    cc=curl_easy_perform(pCurl);
    std::chrono::time_point<std::chrono::steady_clock> tEnd = std::chrono::steady_clock::now();

    // Give it another try in case of revocation list issues
    if ( cc != CURLE_OK && IsRevocationError(curl_errtxt)) {
        // try not to query revoke list
        curl_easy_setopt(pCurl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
        LOG_MSG(logWARN, ERR_CURL_DISABLE_REV_QU, ChName());
        // and just give it another try
        tStart = std::chrono::steady_clock::now();
        cc = curl_easy_perform(pCurl);
        tEnd = std::chrono::steady_clock::now();
    }

    // if (still) error, then log error and bail out
    if (cc != CURLE_OK) {
        
        // In case of timeout increase the channel's timeout and don't count it as an error
        if (cc == CURLE_OPERATION_TIMEDOUT) {
            LOG_MSG(logWARN, ERR_CURL_PERFORM, ChName(), cc, curl_errtxt);
            nTimeout = std::min (nTimeout * 2,
                                 dataRefs.GetNetwTimeout());
            LOG_MSG(logWARN, "%s: Timeout increased to %ds", ChName(), nTimeout);
        } else {
            SHOW_MSG(logERR, ERR_CURL_PERFORM, ChName(), cc, curl_errtxt);
            IncErrCnt();
        }
        
        return false;
    }

    // check HTTP response code
    curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);
    switch (httpResponse) {
        case HTTP_OK:
        // Based on actual time take set a new network timeout as twice the response time
        {
            const std::chrono::seconds d = std::chrono::duration_cast<std::chrono::seconds>(tEnd - tStart);
            nTimeout = std::clamp<int> ((int)d.count() * 2,
                                        std::max(MIN_NETW_TIMEOUT,nTimeout/2),  // this means the in case of a reduction we reduce to now less than half the previous value
                                        dataRefs.GetNetwTimeout());
            // LOG_MSG(logWARN, "%s: Timeout set to %ds", ChName(), nTimeout);
            break;
        }
        case HTTP_NOT_FOUND:
            // not found is typically handled separately, so only debug-level
            LOG_MSG(logDEBUG,ERR_CURL_HTTP_RESP,ChName(),httpResponse, url.c_str());
            break;
            
        default:
            // all other responses are warnings
            LOG_MSG(logWARN,ERR_CURL_HTTP_RESP,ChName(),httpResponse, url.c_str());
    }
    
    // if requested log raw data received
    DebugLogRaw(netData);
    
    // success
    return true;
}

// Is the given network error text possibly caused by problems querying the revocation list?
bool LTOnlineChannel::IsRevocationError (const std::string& err)
{
    // we can check for the word "revocation", but in localized versions of
    // Windows this is translated! We have, so far, seen two different error codes.
    // So what we do is to look for all three things:
    for (const std::string s: ERR_CURL_REVOKE_MSG)
        if (err.find(s) != std::string::npos)
            return true;
    return false;
}

//
//MARK: LTFileChannel
//

LTFileChannel::LTFileChannel () :
zuluLastRead(0)
{}

//
//MARK: Init Functions
//
bool LTFlightDataInit()
{
    // global init libcurl
    CURLcode resCurl = curl_global_init (CURL_GLOBAL_ALL);
    if ( resCurl != CURLE_OK )
    {
        // something went wrong
        LOG_MSG(logFATAL,ERR_CURL_INIT,curl_easy_strerror(resCurl));
        return false;
    }
    
    // Success
    return true;
}

bool LTFlightDataEnable()
{
    // create list of flight and master data connections
    listFDC.clear();

    // load live feed readers (in order of priority)
    listFDC.emplace_back(new RealTrafficConnection(mapFd));
    listFDC.emplace_back(new OpenSkyConnection);
    listFDC.emplace_back(new ADSBExchangeConnection);
    listFDC.emplace_back(new OpenGliderConnection);
    listFDC.emplace_back(new FSCConnection);
    // load online master data connections
    listFDC.emplace_back(new OpenSkyAcMasterdata);
    // load other channels
    listFDC.emplace_back(new ForeFlightSender(mapFd));
    
    // Success only if there are still connections left
    return listFDC.size() > 0;
}

void LTFlightDataDisable()
{
    // remove all flight data connections
    listFDC.clear();
}

void LTFlightDataStop()
{
    // cleanup global CURL stuff
    curl_global_cleanup();
}

//
//MARK: Show/Select Aircraft / Thread Control
//
// this function is spawned as a separate thread in LTFlightDataShowAircraft
// and it runs in a loop until LTFlightDataHideAircraft stops it
void LTFlightDataSelectAc ()
{
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_Channels");

    while ( !bFDMainStop )
    {
        // basis for determining when to be called next
        gNextWakeup = std::chrono::steady_clock::now();
        
        // LiveTraffic Top Level Exception Handling
        try {
            // where are we right now?
            positionTy pos (dataRefs.GetViewPos());
            
            // If the camera position is valid we can request data around it
            if (pos.isNormal()) {

                // Next wakeup is "refresh interval" from _now_
                // (calls to network requests might take a long time,
                //  see wait in OpenSkyAcMasterdata::FetchAllData)
                gNextWakeup += std::chrono::seconds(dataRefs.GetFdRefreshIntvl());

                // cycle all flight data connections
                for ( ptrLTChannelTy& p: listFDC )
                {
                    // LiveTraffic Top Level Exception Handling
                    try {
                        // fetch all aicrafts
                        if ( p->IsEnabled() ) {
                            
                            // if enabled fetch data and process it
                            if ( p->FetchAllData(pos) && !bFDMainStop ) {
                                if (p->ProcessFetchedData(mapFd))
                                    // reduce error count if processed successfully
                                    // as a chance to appear OK in the long run
                                    p->DecErrCnt();
                            }
                        } else {
                            // if disabled...maybe do still some processing to connections
                            p->DoDisabledProcessing();
                        }
                    } catch (const std::exception& e) {
                        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
                        // in case of any exception disable this channel
                        p->SetValid(false, true);
                    } catch (...) {
                        // in case of any exception disable this channel
                        p->SetValid(false, true);
                    }
                    
                    // exit early if asked to do so
                    if ( bFDMainStop )
                        break;
                }

                // Clear away processed master data requests
                LTACMasterdataChannel::ClearMasterDataRequests();
            }
            else {
                // Camera position is yet invalid, retry in a second
                gNextWakeup += std::chrono::seconds(1);
            }
            
        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            // in case of any exception here completely re-init
            dataRefs.SetReInitAll(true);
        } catch (...) {
            // in case of any exception here completely re-init
            dataRefs.SetReInitAll(true);
        }
        
        // sleep for FD_REFRESH_INTVL or if woken up for termination
        // by condition variable trigger
        {
            std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
            FDThreadSynchCV.wait_until(lk, gNextWakeup,
                                       []{return bFDMainStop;});
            lk.unlock();
        }
    }
}

// called from main thread to start showing aircraft
bool LTFlightDataShowAircraft()
{
    // is there a main thread running already? -> just return
    if ( FDMainThread.joinable() ) return true;
    
    // Verify if there are any enabled, active tracking data channels.
    // If not bail out and inform the user.
    if (!LTFlightDataAnyTrackingChEnabled())
    {
        SHOW_MSG(logERR, ERR_CH_NONE_ACTIVE);
        return false;
    }
    
    // create a new thread that receives flight data / creates aircraft
    bFDMainStop = false;
    FDMainThread = std::thread ( LTFlightDataSelectAc );
    // and one for position calculation
    CalcPosThread = std::thread ( LTFlightData::CalcNextPosMain );
    
    // tell the user we do something in the background
    SHOW_MSG(logINFO,
             MSG_REQUESTING_LIVE_FD);
    
    // flag for: as soon as data arrives start buffer countdown
    initTimeBufFilled = -1;
    
    return true;
}

// called from main thread to stop showing aircraft
void LTFlightDataHideAircraft()
{
    // is there a main thread running? -> stop it and wait for it to return
    if ( FDMainThread.joinable() )
    {
        // stop the main thread
        bFDMainStop = true;                 // the message is: Stop!
        FDThreadSynchCV.notify_all();          // wake them up if just waiting for next refresh
        CalcPosThread.join();               // wait for threads to finish
        FDMainThread.join();
        
        CalcPosThread = std::thread();
        FDMainThread = std::thread();
    }
    
    // tell all connection to close
    for ( ptrLTChannelTy& p: listFDC ) {
        p->Close();
    }
    
    // Remove all flight data info including displayed aircraft
    try {
        // access guarded by a mutex
        std::lock_guard<std::mutex> lock (mapFdMutex);
        mapFd.clear();
        LOG_ASSERT ( dataRefs.GetNumAc() == 0 );
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
    
    // not showing any longer
    LOG_MSG(logINFO,INFO_AC_ALL_REMOVED);
}



// Is at least one tracking data channel enabled?
bool LTFlightDataAnyTrackingChEnabled ()
{
    return std::any_of(listFDC.cbegin(), listFDC.cend(),
                       [](const ptrLTChannelTy& pCh)
                       { return pCh->GetChType() == LTChannel::CHT_TRACKING_DATA &&
                                pCh->IsEnabled(); });
}
                         

// Is any channel invalid?
bool LTFlightDataAnyChInvalid ()
{
    return std::any_of(listFDC.cbegin(), listFDC.cend(),
                       [](const ptrLTChannelTy& pCh)
                       { return !pCh->IsValid(); });
}


// Restart all invalid channels (set valid)
void LTFlightDataRestartInvalidChs ()
{
    for (ptrLTChannelTy& pCh: listFDC)
        if (!pCh->IsValid())
            pCh->SetValid(true, true);
}


// Return channel object
LTChannel* LTFlightDataGetCh (dataRefsLT ch)
{
    listPtrLTChannelTy::iterator iter =
    std::find_if(listFDC.begin(), listFDC.end(),
                 [ch](const ptrLTChannelTy& pCh)
                 { return pCh->channel == ch; });
    if (iter == listFDC.end())
        return nullptr;
    else
        return iter->get();
}

//
//MARK: Aircraft Maintenance
//      (called from flight loop callback!)
//

void LTFlightDataAcMaintenance()
{
    int numAcBefore = dataRefs.GetNumAc();
    
    try {
        // access guarded by the fd mutex
        std::lock_guard<std::mutex> lock (mapFdMutex);
        double simTime = dataRefs.GetSimTime();
        
        // iterate all flight data and remove outdated aircraft along with their fd data
        // (although c++ doc says map iterators won't be affected by erase it actually crashes...
        //  so we do it the old-fashioned way and store a vector of to-be-deleted keys
        //  and do the actual delete in a second round)
        std::vector<mapLTFlightDataTy::key_type> vFdKeysToErase;
        for ( mapLTFlightDataTy::value_type& fdPair: mapFd )
        {
            // do the maintenance, remember a/c to be deleted
            if ( fdPair.second.AircraftMaintenance(simTime) )
                vFdKeysToErase.push_back(fdPair.first);
        }
        // now remove all outdated fd objects remembered for deletion
        for ( const mapLTFlightDataTy::key_type& key: vFdKeysToErase ) {
            mapFd.erase(key);
        }
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
    
    /*** UI messages about filling up the buffer ***/
    int numAcAfter = dataRefs.GetNumAc();
    
    // initially: we might see some a/c but don't have enough data yet
    if ( initTimeBufFilled < 0 ) {
        // did we see any aircraft yet?
        if ( mapFd.size() > 0 )
            // show messages for FD_BUF_PERIOD time
            initTimeBufFilled = dataRefs.GetSimTime() + dataRefs.GetFdBufPeriod();
    }
    
    // if buffer-fill countdown is (still) running, update the figures in UI
    if ( initTimeBufFilled > 0 ) {
        CreateMsgWindow(float(AC_MAINT_INTVL),
                        int(mapFd.size()), numAcAfter,
                        int(initTimeBufFilled - dataRefs.GetSimTime()));
        // buffer fill-up time's up
        if (dataRefs.GetSimTime() >= initTimeBufFilled) {
            initTimeBufFilled = 0;
            CreateMsgWindow(float(AC_MAINT_INTVL),
                            int(mapFd.size()), numAcAfter,
                            -1);            // clear the message
        }
    } else {
        // tell the user a change from or to zero aircraft (actually showing)
        if ( !numAcBefore && (numAcAfter > 0))
            CreateMsgWindow(WIN_TIME_DISPLAY, logINFO, MSG_NUM_AC_INIT, numAcAfter);
        if ( (numAcBefore > 0) && !numAcAfter)
            CreateMsgWindow(WIN_TIME_DISPLAY, logINFO, MSG_NUM_AC_ZERO);
    }
    
#ifdef DEBUG
    // if no planes are left we can reset our debug buffering period
    if (numAcAfter == 0)
        dataRefs.fdBufDebug = 0.0;
#endif
}


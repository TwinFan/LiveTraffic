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

// Thread synch support (specifically for stopping them)
std::thread CalcPosThread;              // the thread for pos calc (TriggerCalcNewPos)
std::mutex  FDThreadSynchMutex;         // supports wake-up and stop synchronization
std::condition_variable FDThreadSynchCV;
volatile bool bFDMainStop = true;       // will be reset once the main thread starts

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


// Destructor makes sure the thread is stopped
LTChannel::~LTChannel ()
{
    Stop(true);
}


// Start the channel, typically starts a separate thread
void LTChannel::Start ()
{
    if (!isRunning()) {
        eThrStatus = THR_STARTING;
        thr = std::thread(&LTChannel::_Main, this);
    }
}

// Stop the channel, wait for the thread to end
void LTChannel::Stop (bool bWaitJoin)
{
    if (isRunning()) {
        if (eThrStatus < THR_STOP)
            eThrStatus = THR_STOP;          // indicate to the thread that it has to end itself
        if (bWaitJoin) {
            thr.join();                     // wait for the thread to actually end
            thr = std::thread();
            eThrStatus = THR_NONE;
        }
    }
}


// all conditions met to continue the thread loop?
bool LTChannel::shallRun () const
{
    return
       !bFDMainStop                         // stop flag for all LT processing
    && eThrStatus <= THR_RUNNING            // thread is not signalled to stop
    && IsValid()                            // channel valid?
    && dataRefs.IsChannelEnabled(channel);  // channel enabled?
}


// Thread main function, just calls virtual Main()
void LTChannel::_Main()
{
    eThrStatus = THR_RUNNING;
    LOG_MSG(logDEBUG, "%s: Thread starts", pszChName);
    Main();
    LOG_MSG(logDEBUG, "%s: Thread ends", pszChName);
    eThrStatus = THR_ENDED;
}


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
    char buf[50];

    // invalid (after errors)? Just disabled/off?
    if (!IsValid())                         return "Invalid";
    if (!IsEnabled())                       return "Off";
    // Active, but currently running into errors?
    if (errCnt > 0) {
        snprintf (buf, sizeof(buf), "Active, but ERROR Count = %d", errCnt);
        return buf;
    }
    // Active, but not a channel for tracking data
    if (GetChType() != CHT_TRACKING_DATA)   return "Active";
    // An active source of tracking data...for how many aircraft?
    snprintf (buf, sizeof(buf), "Active, serving %d aircraft",
              GetNumAcServed());
    return buf;
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

// Lock controlling multi-threaded access to all the 3 sets
std::mutex LTACMasterdataChannel::mtxSets;
// global list of static data requests
setAcStatUpdateTy LTACMasterdataChannel::setAcStatUpdate;
// List of a/c to ignore, as we know we don't get data online
setFdKeyTy LTACMasterdataChannel::setIgnoreAc;
// List of call signs to ignore, as we know we don't get route info online
setStringTy LTACMasterdataChannel::setIgnoreCallSign;

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
        fdIter->second.UpdateData(dat, NAN, requ.type);
        return true;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
    
    // must have caught an error
    return false;
}

// Called regularly to keep the priority list updated
void LTACMasterdataChannel::MaintainMasterDataRequests ()
{
    try {
        // multi-threaded access guarded by vecAcStatMutex
        std::lock_guard<std::mutex> lock (mtxSets);
        
        // remove all entries for which the a/c no longer exist in mapFd
        for (setAcStatUpdateTy::iterator i = setAcStatUpdate.begin();
             i != setAcStatUpdate.end();)
        {
            if (mapFd.count(i->acKey) == 0)
                i = setAcStatUpdate.erase(i);
            else
                ++i;
        }

    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "listAcStatUpdate", e.what());
    }
}

// Add request to fetch data
bool LTACMasterdataChannel::RequestMasterData (const LTFlightData::FDKeyTy& keyAc,
                                               const std::string& callSign,
                                               double distance)
{
    // Sanity check
    if (keyAc.empty() || keyAc.eKeyType != LTFlightData::KEY_ICAO) return false;
    
    // Prepare the request object that is to be added later
    const unsigned long dist = std::isnan(distance) ? UINT_MAX : (unsigned long) (distance);
    const acStatUpdateTy acUpd (keyAc,str_toupper_c(callSign),dist);

    try {
        // multi-threaded access guarded by vecAcStatMutex
        std::lock_guard<std::mutex> lock (mtxSets);
        
        // Avoid entry from the ignore lists that won't succeed anyway
        switch (acUpd.type) {
            case DATREQU_NONE: break;
            case DATREQU_AC_MASTER:
                if (std::find(setIgnoreAc.begin(), setIgnoreAc.end(), acUpd.acKey) != setIgnoreAc.end())
                    return false;
                break;
            case DATREQU_ROUTE:
                if (std::find(setIgnoreCallSign.begin(), setIgnoreCallSign.end(), acUpd.callSign) != setIgnoreCallSign.end())
                    return false;
                break;
        }
        
        // Avoid double entry: Bail if the same kind of request exists already
        // This is different from the duplication check that std::set::insert does
        // because it uses operator== (ignoring `distance`) instead operator<
        if (std::find(setAcStatUpdate.begin(), setAcStatUpdate.end(), acUpd) != setAcStatUpdate.end())
            return false;
        
        // Add the new request to the set
        setAcStatUpdate.insert(acUpd);
        return true;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mtxSets", e.what());
    }
    return false;
}

// Fetch next master data request from our set (`acKey` will be empty if there is no waiting request)
acStatUpdateTy LTACMasterdataChannel::FetchNextRequest ()
{
    try {
        // multi-threaded access guarded by vecAcStatMutex
        std::lock_guard<std::mutex> lock (mtxSets);
        
        // If there is an element, then return a copy and remove it
        if (!setAcStatUpdate.empty()) {
            acStatUpdateTy req = *(setAcStatUpdate.begin());
            setAcStatUpdate.erase(setAcStatUpdate.begin());
            return req;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mtxSets", e.what());
    }
    // Empty or exception: Return an empty request record
    return acStatUpdateTy();
}

// Add an aircraft to the ignore list
void LTACMasterdataChannel::AddIgnoreAc (const LTFlightData::FDKeyTy& keyAc)
{
    try {
        // multi-threaded access guarded by vecAcStatMutex
        std::lock_guard<std::mutex> lock (mtxSets);
        // Add a/c key if not yet in the list
        setIgnoreAc.insert(keyAc);
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mtxSets", e.what());
    }

}

// Add a callsign to the ignore list
void LTACMasterdataChannel::AddIgnoreCallSign (const std::string& cs)
{
    try {
        // multi-threaded access guarded by vecAcStatMutex
        std::lock_guard<std::mutex> lock (mtxSets);
        // Add call sign if not yet in the list
        setIgnoreCallSign.insert(cs);
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mtxSets", e.what());
    }
}


//
//MARK: LTOnlineChannel
//

// the one (hence static) output file for logging raw network data
std::ofstream LTOnlineChannel::outRaw;

LTOnlineChannel::LTOnlineChannel (dataRefsLT ch, LTChannelType t, const char* chName) :
LTChannel(ch, t, chName),
pCurl(NULL),
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
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, dataRefs.GetNetwTimeoutMax());
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
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, dataRefs.GetNetwTimeoutMax());

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
            // But bother the user only with messages once every 3 minutes (this is due to OpenSky having been plagued with slow API responses running into timeouts)
            static float tLastMsg = 0.0f;                   // time last message was logged
            const float now = dataRefs.GetMiscNetwTime();
            if (tLastMsg + 180.0f < now) {
                LOG_MSG(logWARN, ERR_CURL_PERFORM, ChName(), cc, curl_errtxt);
                tLastMsg = now;
            }
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
            break;

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
    listFDC.emplace_back(new RealTrafficConnection());
    listFDC.emplace_back(new OpenSkyConnection);
    listFDC.emplace_back(new ADSBHubConnection());
    listFDC.emplace_back(new ADSBExchangeConnection);
    listFDC.emplace_back(new OpenGliderConnection);
    listFDC.emplace_back(new FSCConnection);
    // load online master data connections
    listFDC.emplace_back(new OpenSkyAcMasterdata);
    // load other channels
    listFDC.emplace_back(new ForeFlightSender());
    
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

/// Walks the list of channels and starts those channels' threads that need to run
void LTFlightDataStartJoinChannels()
{
    for (ptrLTChannelTy& p: listFDC) {
        if (!p) continue;                       // sanity check
        
        const bool bIsRunning = p->isRunning();
        
        // Join threads that have ended themselves (e.g. deactivated or gone invalid)
        if (bIsRunning && p->hasEnded())        // Channel has ended itself?
            p->Stop(true);                      // join the thread, so it can be reused if needed
        else if (!bIsRunning && p->shallRun())  // Channel is not running but should?
            p->Start();                         // -> start it!
    }
}

// called from main thread to start showing aircraft
bool LTFlightDataShowAircraft()
{
    // is there a calculation thread running already? -> just return
    if ( CalcPosThread.joinable() ) return true;
    
    // Verify if there are any enabled, active tracking data channels.
    // If not bail out and inform the user.
    if (!LTFlightDataAnyTrackingChEnabled())
    {
        SHOW_MSG(logERR, ERR_CH_NONE_ACTIVE);
        return false;
    }
    
    // create a new thread for position calculation
    bFDMainStop = false;
    CalcPosThread = std::thread ( LTFlightData::CalcNextPosMain );
    
    // tell the user we do something in the background
    SHOW_MSG(logINFO,
             MSG_REQUESTING_LIVE_FD);
    
    // flag for: as soon as data arrives start buffer countdown
    initTimeBufFilled = -1;
    
    // Start the required communication threads
    LTFlightDataStartJoinChannels();
    
    return true;
}

// called from main thread to stop showing aircraft
void LTFlightDataHideAircraft()
{
    // is there a calculation thread running?
    if ( CalcPosThread.joinable() )
    {
        // Stop all threads
        bFDMainStop = true;                 // the message is: Stop!
        FDThreadSynchCV.notify_all();       // wake them all up to exit
        
        // wait for all network threads
        for (ptrLTChannelTy& p: listFDC)
            if (p) p->Stop(true);
        // Wait for the calculation thread
        CalcPosThread.join();
        CalcPosThread = std::thread();
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
    // Verify all required channels are running (necessary in case users activates channels via UI)
    LTFlightDataStartJoinChannels();
    
    // Actual aircraft maintenance: call individual FD objects, remove outdated ones
    int numAcBefore = dataRefs.GetNumAc();
    
    try {
        // access guarded by the fd mutex
        std::lock_guard<std::mutex> lock (mapFdMutex);
        double simTime = dataRefs.GetSimTime();
        
        // iterate all flight data and remove outdated aircraft along with their fd data
        for ( auto i = mapFd.begin(); i != mapFd.end(); )
        {
            // access guarded by a mutex
            LTFlightData& fd = i->second;
            std::lock_guard<std::recursive_mutex> lockFd (fd.dataAccessMutex);
            // do the maintenance, remove aircraft if that's the verdict
            if ( fd.AircraftMaintenance(simTime) )
                i = mapFd.erase(i);
            else
                ++i;
        }

        // Clear away outdated master data requests
        LTACMasterdataChannel::MaintainMasterDataRequests();

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
        CreateMsgWindow(float(AC_MAINT_INTVL * 1.5),
                        int(mapFd.size()), numAcAfter,
                        int(initTimeBufFilled - dataRefs.GetSimTime()));
        // buffer fill-up time's up
        if (dataRefs.GetSimTime() >= initTimeBufFilled) {
            initTimeBufFilled = 0;
            CreateMsgWindow(float(AC_MAINT_INTVL * 1.5),
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


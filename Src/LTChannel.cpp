//
//  LTChannel.cpp
//  LiveTraffic

/*
 * Copyright (c) 2018, Birger Hoppe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "LiveTraffic.h"

#include <future>
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
listAcStatUpdateTy LTACMasterdataChannel::listAcStatUpdate;

// Thread synch support (specifically for stopping them)
std::thread FDMainThread;               // the main thread (LTFlightDataSelectAc)
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

LTChannel::~LTChannel ()
{}

void LTChannel::SetValid (bool _valid, bool bMsg)
{
    // (re)set to valid channel
    if (_valid) {
        errCnt = 0;
        bValid = true;
    } else {
        // set invalid, also means: disable
        bValid = false;
        SetEnable (false);
        if (bMsg)
            SHOW_MSG(logFATAL,ERR_CH_INVALID,ChName());
        
        // there is no other place in the code to actually re-validate a channel
        // so as surprising as it sounds: we do it right here:
        // That way the user has a chance by actively reenabling the channel
        // in the settings to try again.
        errCnt = 0;
        bValid = true;
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

//
//MARK: LTACMasterdata
//

// fetches a/c master data and updates the respective static data in the FDMap
bool LTACMasterdataChannel::UpdateStaticData (std::string keyAc,
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
        fdIter->second.UpdateData(dat);
        return true;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
    
    // must have caught an error
    return false;
}

// static function to add key/callSign to list of data,
// for which master data shall be requested by a master data channel
void LTACMasterdataChannel::RequestMasterData (const std::string transpIcao,
                                               const std::string callSign)
{
    // just add the request to the request list, uniquely
    push_back_unique<listAcStatUpdateTy>
    (listAcStatUpdate,
     acStatUpdateTy(transpIcao,callSign));
}

void LTACMasterdataChannel::ClearMasterDataRequests ()
{
    listAcStatUpdate.clear();
}


// copy all requested a/c to our private list,
// the global one is refreshed before the next call.
void LTACMasterdataChannel::CopyGlobalRequestList ()
{
    for (const acStatUpdateTy& info: listAcStatUpdate)
        push_back_unique<listAcStatUpdateTy>(listAc, info);
}

//
//MARK: LTOnlineChannel
//

// the one (hence static) output file for logging raw network data
std::ofstream LTOnlineChannel::outRaw;

LTOnlineChannel::LTOnlineChannel () :
pCurl(NULL),
netData((char*)malloc(CURL_MAX_WRITE_SIZE)),      // initial buffer allocation
netDataPos(0), netDataSize(CURL_MAX_WRITE_SIZE),
curl_errtxt{0}, httpResponse(HTTP_OK)
{
    // initialize a CURL handle
    SetValid(InitCurl());
}

LTOnlineChannel::~LTOnlineChannel ()
{
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
        SetValid(false);
        return false;
    }
    
    // define the handle
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
void LTOnlineChannel::DebugLogRaw(const char *data)
{
    // no logging? return (after closing the file if open)
    if (!dataRefs.GetDebugLogRawFD()) {
        if (outRaw.is_open()) {
            outRaw.close();
            SHOW_MSG(logWARN, DBG_RAW_FD_STOP, PATH_DEBUG_RAW_FD);
        }
        return;
    }
    
    // logging enabled. Need to open the file first?
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
    
    // timestamp (numerical and human readable)
    outRaw
    << std::fixed << dataRefs.GetSimTime()
    << " - "
    << dataRefs.GetSimTimeString()
    << " - "
    // Channel's name
    << ChName()
    << "\n"
    // the actual given data
    << data
    // 2 newlines + flush
    << "\n" << std::endl;
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
    
    // put together the REST request
    curl_easy_setopt(pCurl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(pCurl, CURLOPT_BUFFERSIZE, netDataSize );
    
    // get fresh data via the internet
    // this will take a second or more...don't try in render loop ;)
    // it is assumed that this is called in a separate thread,
    // hence we can use the simple blocking curl_easy_ call
    netDataPos = 0;                 // fill buffer from beginning
    netData[0] = 0;
    LOG_MSG(logDEBUG,DBG_SENDING_HTTP,ChName(),url.c_str());
    DebugLogRaw(url.c_str());
    if ( (cc=curl_easy_perform(pCurl)) != CURLE_OK )
    {
        // problem with querying revocation list?
        if (strstr(curl_errtxt, ERR_CURL_REVOKE_MSG)) {
            // try not to query revoke list
            curl_easy_setopt(pCurl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
            LOG_MSG(logWARN, ERR_CURL_DISABLE_REV_QU, ChName());
            // and just give it another try
            cc = curl_easy_perform(pCurl);
        }

        // if (still) error, then log error and bail out
        if (cc != CURLE_OK) {
            SHOW_MSG(logERR, ERR_CURL_PERFORM, ChName(), cc, curl_errtxt);
            IncErrCnt();
            return false;
        }
    }
    
    // check HTTP response code
    httpResponse = 0;
    curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);
    
    switch (httpResponse) {
        case HTTP_OK:
            // log number of bytes received
            LOG_MSG(logDEBUG,DBG_RECEIVED_BYTES,ChName(),(long)netDataPos);
            break;
            
        case HTTP_NOT_FOUND:
            // not found is typically handled separately, so only debug-level
            LOG_MSG(logDEBUG,ERR_CURL_HTTP_RESP,ChName(),httpResponse);
            break;
            
        default:
            // all other responses are warnings
            LOG_MSG(logWARN,ERR_CURL_HTTP_RESP,ChName(),httpResponse);
    }
    
    // if requested log raw data received
    DebugLogRaw(netData);
    
    // success
    return true;
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
    if ( dataRefs.GetUseHistData() ) {
        // load historic data readers
        listFDC.emplace_back(new ADSBExchangeHistorical);
        // TODO: master data readers for historic data, like reading CSV file
    } else {
        // load live feed readers (in order of priority)
        listFDC.emplace_back(new RealTrafficConnection(mapFd));
        listFDC.emplace_back(new OpenSkyConnection);
        listFDC.emplace_back(new ADSBExchangeConnection);
        // load online master data connections
        listFDC.emplace_back(new OpenSkyAcMasterdata);
    }
    
    // check for validity after construction, disable all invalid ones
    for ( ptrLTChannelTy& p: listFDC )
    {
        if (!p->IsValid())
            p->SetEnable(false);
    }
    
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
//MARK: Show/Select Aircrafts / Thread Control
//
// this function is spawned as a separate thread in LTFlightDataShowAircraft
// and it runs in a loop until LTFlightDataHideAircraft stops it
void LTFlightDataSelectAc ()
{
    while ( !bFDMainStop )
    {
        // determine when to be called next
        // (calls to network requests might take a long time,
        //  see wait in OpenSkyAcMasterdata::FetchAllData)
        auto nextWakeup = std::chrono::system_clock::now();
        nextWakeup += std::chrono::seconds(dataRefs.GetFdRefreshIntvl());
        
        // LiveTraffic Top Level Exception Handling
        try {
            // where are we right now?
            positionTy pos (dataRefs.GetViewPos());
            
            // reset list of a/c needing master data updates
            LTACMasterdataChannel::ClearMasterDataRequests();
            
            // cycle all flight data connections
            for ( ptrLTChannelTy& p: listFDC )
            {
                // LiveTraffic Top Level Exception Handling
                try {
                    // fetch all aicrafts
                    if ( p->IsEnabled() ) {
                        
                        if ( p->FetchAllData(pos) && !bFDMainStop ) {
                            if (p->ProcessFetchedData(mapFd))
                                // reduce error count if processed successfully
                                // as a chance to appear OK in the long run
                                p->DecErrCnt();
                        }
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
            FDThreadSynchCV.wait_until(lk, nextWakeup,
                                       []{return bFDMainStop;});
            lk.unlock();
        }
    }
}

// called from main thread to start showing aircrafts
bool LTFlightDataShowAircraft()
{
    // is there a main thread running already? -> just return
    if ( FDMainThread.joinable() ) return true;
    
    // Verify if there are any enabled, active tracking data channels.
    // If not bail out and inform the user.
    if (listFDC.empty() ||
        std::find_if(listFDC.cbegin(), listFDC.cend(),
                     [](const ptrLTChannelTy& pCh)
                     { return
                         pCh->GetChType() == LTChannel::CHT_TRACKING_DATA &&
                         pCh->IsEnabled(); }) == listFDC.cend())
    {
        SHOW_MSG(logERR, ERR_CH_NONE_ACTIVE);
        return false;
    }
    
    // create a new thread that receives flight data / creates aircrafts
    bFDMainStop = false;
    FDMainThread = std::thread ( LTFlightDataSelectAc );
    // and one for position calculation
    CalcPosThread = std::thread ( LTFlightData::CalcNextPosMain );
    
    // tell the user we do something in the background
    SHOW_MSG(logINFO,
             dataRefs.GetUseHistData() ? MSG_READING_HIST_FD :
             MSG_REQUESTING_LIVE_FD);
    
    // flag for: as soon as data arrives start buffer countdown
    initTimeBufFilled = -1;
    
    return true;
}

// called from main thread to stop showing aircrafts
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
    
    // Remove all flight data info including displayed aircrafts
    try {
        // access guarded by a mutex
        std::lock_guard<std::mutex> lock (mapFdMutex);
        mapFd.clear();
        LOG_ASSERT ( dataRefs.GetNumAircrafts() == 0 );
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
    
    // not showing any longer
    LOG_MSG(logINFO,INFO_AC_ALL_REMOVED);
}

//
//MARK: Aircraft Maintenance
//      (called from flight loop callback!)
//

void LTFlightDataAcMaintenance()
{
    int numAcBefore = dataRefs.GetNumAircrafts();
    
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
        for ( const mapLTFlightDataTy::key_type& key: vFdKeysToErase )
            mapFd.erase(key);
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
    
    /*** UI messages about filling up the buffer ***/
    int numAcAfter = dataRefs.GetNumAircrafts();
    
    // initially: we might see some a/c but don't have enough data yet
    if ( initTimeBufFilled < 0 ) {
        // did we see any aircraft yet?
        if ( mapFd.size() > 0 )
            // show messages for FD_BUF_PERIOD time
            initTimeBufFilled = dataRefs.GetSimTime() + dataRefs.GetFdBufPeriod();
    }
    
    // if buffer-fill countdown is (still) running, update the figures in UI
    if ( initTimeBufFilled > 0 ) {
        CreateMsgWindow(float(AC_MAINT_INTVL - .05), logMSG, MSG_BUF_FILL_COUNTDOWN,
                        int(mapFd.size()),
                        numAcAfter,
                        int(initTimeBufFilled - dataRefs.GetSimTime()));
        // buffer fill-up time's up
        if (dataRefs.GetSimTime() >= initTimeBufFilled)
            initTimeBufFilled = 0;
    } else {
        // tell the user a change from or to zero aircrafts (actually showing)
        if ( !numAcBefore && (numAcAfter > 0))
            CreateMsgWindow(WIN_TIME_DISPLAY, logMSG, MSG_NUM_AC_INIT, numAcAfter);
        if ( (numAcBefore > 0) && !numAcAfter)
            CreateMsgWindow(WIN_TIME_DISPLAY, logMSG, MSG_NUM_AC_ZERO);
    }
}


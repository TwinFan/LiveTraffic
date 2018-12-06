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

// list of a/c for which static data is yet missing
listStringTy listAcStatUpdate;

// Thread synch support (specifically for stopping them)
std::thread FDMainThread;               // the main thread (LTFlightDataSelectAc)
std::thread CalcPosThread;              // the thread for pos calc (TriggerCalcNewPos)
std::mutex  FDThreadSynchMutex;         // supports wake-up and stop synchronization
std::condition_variable FDThreadSynchCV;
volatile bool bFDMainStop = true;       // will be reset once the main thread starts

// the global vector of all flight and master data connections
listPtrLTChannelTy    listFDC;

//
//MARK: LTChannel
//

LTChannel::~LTChannel ()
{}

const char* LTChannel::ChId2String (dataRefsLT ch)
{
    switch (ch) {
        case DR_CHANNEL_ADSB_EXCHANGE_ONLINE:
            return ADSBEX_NAME;
        case DR_CHANNEL_ADSB_EXCHANGE_HISTORIC:
            return ADSBEX_HIST_NAME;
        case DR_CHANNEL_OPEN_SKY_ONLINE:
            return OPSKY_NAME;
        case DR_CHANNEL_OPEN_SKY_AC_MASTERDATA:
            return OPSKY_MD_NAME;
        default:
            return ERR_CH_UNKNOWN_NAME;
    }
}

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
        fdIter->second.UpdateData(dat, true);
        return true;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
    
    // must have caught an error
    return false;
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
        std::string sFileName (LTCalcFullPath(PATH_DEBUG_RAW_FD));
#ifdef APL
        // Mac: convert to Posix
        LTHFS2Posix(sFileName);
#endif
        // open the file, append to it
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
    if ( curl_easy_perform(pCurl) != CURLE_OK )
    {
        SHOW_MSG(logERR,ERR_CURL_PERFORM,ChName(),curl_errtxt);
        IncErrCnt();
        return false;
    }
    
    // check HTTP response code
    httpResponse = 0;
    curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);
    if (httpResponse != HTTP_OK)
        LOG_MSG(logWARN,ERR_CURL_HTTP_RESP,ChName(),httpResponse);
    
    // log number of bytes received
    LOG_MSG(logDEBUG,DBG_RECEIVED_BYTES,ChName(),(long)netDataPos);
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
//MARK: OpenSky
//

// put together the URL to fetch based on current view position
std::string OpenSkyConnection::GetURL (const positionTy& pos)
{
    boundingBoxTy box (pos, dataRefs.GetFdStdDistance_m());
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
bool OpenSkyConnection::ProcessFetchedData (mapLTFlightDataTy& fdMap)
{
    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) return true;
    
    // now try to interpret it as JSON
    JSON_Value* pRoot = json_parse_string(netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // let's cycle the aircrafts
    // first get the structre's main object, then its aircraft array
    JSON_Object* pObj = json_object(pRoot);
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
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
    // iterate all aircrafts in the received flight data (can be 0)
    else for ( int i=0; i < json_array_get_count(pJAcList); i++ )
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
        std::string transpIcao (jag_s(pJAc, OPSKY_TRANSP_ICAO) );
        str_toupper(transpIcao);
        
        // not matching a/c filter? -> skip it
        if ((!acFilter.empty() && (acFilter != transpIcao)) )
        {
            continue;
        }
        
        try {
            // from here on access to fdMap guarded by a mutex
            // until FD object is inserted and updated
            std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
            
            // get the fd object from the map, key is the transpIcao
            // this fetches an existing or, if not existing, creates a new one
            LTFlightData& fd = fdMap[transpIcao];
            
            // also get the data access lock once and for all
            // so following fetch/update calls only make quick recursive calls
            std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
            
            // completely new? fill key fields
            if ( fd.empty() )
                fd.SetKey(transpIcao);
            
            // fill static data
            {
                LTFlightData::FDStaticData stat;
                stat.country =    jag_s(pJAc, OPSKY_COUNTRY);
                stat.trt     =    trt_ADS_B_unknown;
                stat.call    =    jag_s(pJAc, OPSKY_CALL);
                while (!stat.call.empty() && stat.call.back() == ' ')      // trim trailing spaces
                    stat.call.pop_back();
                fd.UpdateData(std::move(stat), false);
                
                // openSky doesn't deliver a/c master data with the flight data stream
                // so fetch the master data afterwards
                if ( !fd.GetUnsafeStat().isInit() )
                    push_back_unique<listStringTy, listStringTy::value_type>(listAcStatUpdate,transpIcao);
            }
            
            // dynamic data
            {   // unconditional...block is only for limiting local variables
                LTFlightData::FDDynamicData dyn;
                
                // position time
                double posTime = jag_n(pJAc, OPSKY_POS_TIME);
                
                // non-positional dynamic data
                dyn.radar.code =  (long)jag_sn(pJAc, OPSKY_RADAR_CODE);
                dyn.gnd =               jag_b(pJAc, OPSKY_GND);
                dyn.heading =           jag_n(pJAc, OPSKY_HEADING);
                dyn.spd =               jag_n(pJAc, OPSKY_SPD);
                dyn.vsi =               jag_n(pJAc, OPSKY_VSI);
                dyn.ts =                posTime;
                dyn.pChannel =          this;
                
                // position
                positionTy pos (jag_n(pJAc, OPSKY_LAT),
                                jag_n(pJAc, OPSKY_LON),
                                jag_n(pJAc, OPSKY_ELEVATION),
                                posTime);
                pos.onGrnd = dyn.gnd ? positionTy::GND_ON : positionTy::GND_OFF;
                
                // position is rather important, we check for validity
                if ( pos.isNormal() )
                    fd.AddDynData(dyn, 0, 0, &pos);
                else
                    LOG_MSG(logWARN,ERR_POS_UNNORMAL,transpIcao.c_str(),pos.dbgTxt().c_str());
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

//
//MARK: ADS-B Exchange
//

// put together the URL to fetch based on current view position
std::string ADSBExchangeConnection::GetURL (const positionTy& pos)
{
    char url[128] = "";
    snprintf(url, sizeof(url), ADSBEX_URL_ALL, pos.lat(), pos.lon(),
             dataRefs.GetFdStdDistance());
    return std::string(url);
}

// update shared flight data structures with received flight data
bool ADSBExchangeConnection::ProcessFetchedData (mapLTFlightDataTy& fdMap)
{
    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) return true;
    
    // now try to interpret it as JSON
    JSON_Value* pRoot = json_parse_string(netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    
    // let's cycle the aircrafts
    // first get the structre's main object, then its aircraft array
    JSON_Object* pObj = json_object(pRoot);
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    JSON_Array* pJAcList = json_object_get_array(pObj, ADSBEX_AIRCRAFT_ARR);
    if (!pJAcList) { LOG_MSG(logERR,ERR_JSON_ACLIST,ADSBEX_AIRCRAFT_ARR); IncErrCnt(); return false; }
    
    // iterate all aircrafts in the received flight data (can be 0)
    for ( int i=0; i < json_array_get_count(pJAcList); i++ )
    {
        // get the aircraft
        JSON_Object* pJAc = json_array_get_object(pJAcList,i);
        if (!pJAc) {
            LOG_MSG(logERR,ERR_JSON_AC,i+1,ADSBEX_AIRCRAFT_ARR);
            if (IncErrCnt())
                continue;
            else
                return false;
        }
        
        // the key: transponder Icao code
        std::string transpIcao ( jog_s(pJAc, ADSBEX_TRANSP_ICAO) );
        str_toupper(transpIcao);
        
        // data already stale? -> skip it
        if ( jog_b(pJAc, ADSBEX_POS_STALE) ||
            // not matching a/c filter? -> skip it
            (!acFilter.empty() && (acFilter != transpIcao)) )
        {
            continue;
        }
        
        try {
            // from here on access to fdMap guarded by a mutex
            // until FD object is inserted and updated
            std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
            
            // get the fd object from the map, key is the transpIcao
            // this fetches an existing or, if not existing, creates a new one
            LTFlightData& fd = fdMap[transpIcao];
            
            // also get the data access lock once and for all
            // so following fetch/update calls only make quick recursive calls
            std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
            
            // completely new? fill key fields
            if ( fd.empty() )
                fd.SetKey(transpIcao);
            
            // fill static data
            {
                LTFlightData::FDStaticData stat;
                stat.reg =        jog_s(pJAc, ADSBEX_REG);
                stat.country =    jog_s(pJAc, ADSBEX_COUNTRY);
                stat.acTypeIcao = jog_s(pJAc, ADSBEX_AC_TYPE_ICAO);
                stat.man =        jog_s(pJAc, ADSBEX_MAN);
                stat.mdl =        jog_s(pJAc, ADSBEX_MDL);
                stat.year =  (int)jog_sn(pJAc, ADSBEX_YEAR);
                stat.mil =        jog_b(pJAc, ADSBEX_MIL);
                stat.trt          = transpTy(int(jog_n(pJAc,ADSBEX_TRT)));
                stat.op =         jog_s(pJAc, ADSBEX_OP);
                stat.opIcao =     jog_s(pJAc, ADSBEX_OP_ICAO);
                stat.call =       jog_s(pJAc, ADSBEX_CALL);
                // update the a/c's master data
                if ( stat.acTypeIcao.empty() )
                    LOG_MSG(logWARN,ERR_CH_INV_DATA,ChName(),transpIcao.c_str());
                fd.UpdateData(std::move(stat), true);
            }
            
            // dynamic data
            {   // unconditional...block is only for limiting local variables
                LTFlightData::FDDynamicData dyn;
                
                // ADS-B returns Java tics, that is milliseconds, we use seconds
                double posTime = jog_n(pJAc, ADSBEX_POS_TIME) / 1000.0;
                
                // non-positional dynamic data
                dyn.radar.code =  (long)jog_sn(pJAc, ADSBEX_RADAR_CODE);
                dyn.gnd =               jog_b(pJAc, ADSBEX_GND);
                dyn.heading =           jog_n(pJAc, ADSBEX_HEADING);
                dyn.inHg =              jog_n(pJAc, ADSBEX_IN_HG);
                dyn.brng =              jog_n(pJAc, ADSBEX_BRNG);
                dyn.dst =               jog_n(pJAc, ADSBEX_DST);
                dyn.spd =               jog_n(pJAc, ADSBEX_SPD);
                dyn.vsi =               jog_n(pJAc, ADSBEX_VSI);
                dyn.ts =                posTime;
                dyn.pChannel =          this;
                
                // position and its ground status
                positionTy pos (jog_n(pJAc, ADSBEX_LAT),
                                jog_n(pJAc, ADSBEX_LON),
                                // ADSB data is feet, positionTy expects meter
                                jog_n(pJAc, ADSBEX_ELEVATION) * M_per_FT,
                                posTime);
                pos.onGrnd = dyn.gnd ? positionTy::GND_ON : positionTy::GND_OFF;
                
                // position is rather important, we check for validity
                if ( pos.isNormal() )
                    fd.AddDynData(dyn,
                                  (int)jog_n(pJAc, ADSBEX_RCVR),
                                  (int)jog_n(pJAc, ADSBEX_SIG),
                                  &pos);
                else
                    LOG_MSG(logWARN,ERR_POS_UNNORMAL,transpIcao.c_str(),pos.dbgTxt().c_str());
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
    pathBase = LTCalcFullPath(base.c_str());
    // must contain _something_
    if ( LTNumFilesInPath (pathBase.c_str()) < 1) {
        // first path didn't work, try fallback
        if ( !fallback.empty() ) {
            SHOW_MSG(logWARN,ADSBEX_HIST_TRY_FALLBACK,pathBase.c_str());
            // determine full path (might be local to XP System Path)
            pathBase = LTCalcFullPath(fallback.c_str());
            // must contain _something_
            if ( LTNumFilesInPath (pathBase.c_str()) < 1) {
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
    
    // the bounding box: only aircrafts in this box are considered
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
            (LTNumFilesInPath(pathDate.c_str()) < 1)) {
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
#ifdef APL
        // Mac: convert to Posix
        LTHFS2Posix(pathDate);
#endif
        
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
            std::string transpIcao ( jog_s(pJAc, ADSBEX_TRANSP_ICAO) );
            str_toupper(transpIcao);
            
            // data already stale? -> skip it
            if ( jog_b(pJAc, ADSBEX_POS_STALE) ||
                // not matching a/c filter? -> skip it
                (!acFilter.empty() && (acFilter != transpIcao)) )
            {
                json_value_free (pRoot);
                continue;
            }
            
            // the receiver we are dealing with right now
            int rcvr = (int)jog_n(pJAc, ADSBEX_RCVR);
            
            // variables we need for quality indicator calculation
            int sig =               (int)jog_n(pJAc, ADSBEX_SIG);
            JSON_Array* pCosList =  json_object_get_array(pJAc, ADSBEX_COS);
            int cosCount =          int(json_array_get_count(pCosList)/4);
            
            // quality is made up of signal level, number of elements of the trail
            int qual = (sig + cosCount);
            
            // and we award the currently used receiver a 50% award: we value to
            // stay with the same receiver minute after minute (file-to-file)
            // as this is more likely to avoid spikes when connection this
            // minute's trail with last minute's trail
            mapLTFlightDataTy::iterator fdIter = fdMap.find(transpIcao);
            if ( fdIter != fdMap.end() && fdIter->second.GetRcvr() == rcvr ) {
                qual *= 3;
                qual /= 2;
            }
            
            // did we find another line for this a/c earlier in this file?
            mapFDSelectionTy::iterator selIter = selMap.find(transpIcao);
            
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
                selMap.emplace(std::make_pair(transpIcao, FDSelection {qual, std::move(ln)} ));
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
                std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
                
                // get the fd object from the map, key is the transpIcao
                // this fetches an existing or, if not existing, creates a new one
                const std::string& transpIcao = selVal.first;
                LTFlightData& fd = fdMap[transpIcao];
                
                // also get the data access lock once and for all
                // so following fetch/update calls only make quick recursive calls
                std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
                
                // completely new? fill key fields
                if ( fd.empty() )
                    fd.SetKey(transpIcao);
                
                // fill static
                {
                    LTFlightData::FDStaticData stat;
                    stat.reg =        jog_s(pJAc, ADSBEX_REG);
                    stat.country =    jog_s(pJAc, ADSBEX_COUNTRY);
                    stat.acTypeIcao = jog_s(pJAc, ADSBEX_AC_TYPE_ICAO);
                    stat.man =        jog_s(pJAc, ADSBEX_MAN);
                    stat.mdl =        jog_s(pJAc, ADSBEX_MDL);
                    stat.year =  (int)jog_sn(pJAc, ADSBEX_YEAR);
                    stat.mil =        jog_b(pJAc, ADSBEX_MIL);
                    stat.trt          = transpTy(int(jog_n(pJAc,ADSBEX_TRT)));
                    stat.op =         jog_s(pJAc, ADSBEX_OP);
                    stat.opIcao =     jog_s(pJAc, ADSBEX_OP_ICAO);
                    stat.call =       jog_s(pJAc, ADSBEX_CALL);
                    // update the a/c's master data
                    if ( stat.acTypeIcao.empty() )
                        LOG_MSG(logWARN,ERR_CH_INV_DATA,ChName(),transpIcao.c_str());
                    fd.UpdateData(std::move(stat), true);
                }
                
                // dynamic data
                LTFlightData::FDDynamicData dyn;
                
                // ADS-B returns Java tics, that is milliseconds, we use seconds
                double posTime = jog_n(pJAc, ADSBEX_POS_TIME) / 1000.0;
                
                // non-positional dynamic data
                dyn.radar.code =  (long)jog_sn(pJAc, ADSBEX_RADAR_CODE);
                dyn.gnd =               jog_b(pJAc, ADSBEX_GND);
                dyn.heading =           jog_n(pJAc, ADSBEX_HEADING);
                dyn.inHg =              jog_n(pJAc, ADSBEX_IN_HG);
                dyn.brng =              jog_n(pJAc, ADSBEX_BRNG);
                dyn.dst =               jog_n(pJAc, ADSBEX_DST);
                dyn.spd =               jog_n(pJAc, ADSBEX_SPD);
                dyn.vsi =               jog_n(pJAc, ADSBEX_VSI);
                dyn.ts =                posTime;
                dyn.pChannel =          this;
                
                fd.AddDynData(dyn,
                              (int)jog_n(pJAc, ADSBEX_RCVR),
                              (int)jog_n(pJAc, ADSBEX_SIG));
                
                // position data, including short trails
                positionTy mainPos (jog_n(pJAc, ADSBEX_LAT),
                                    jog_n(pJAc, ADSBEX_LON),
                                    // ADSB data is feet, positionTy expects meter
                                    jog_n(pJAc, ADSBEX_ELEVATION) * M_per_FT,
                                    posTime,
                                    dyn.heading);
                
                // position is kinda important...we continue only with valid data
                if ( mainPos.isNormal() )
                {
                    // we need a good take on the ground status of mainPos
                    // for later landing detection
                    mainPos.onGrnd = dyn.gnd ? positionTy::GND_ON : positionTy::GND_OFF;
                    fd.TryDeriveGrndStatus(mainPos);
                    
                    // Short Trails ("Cos" array), if available
                    bool bAddedTrails = false;
                    dequePositionTy trails;
                    JSON_Array* pCosList = json_object_get_array(pJAc, ADSBEX_COS);

                    // short-cut: if we are in the air then skip adding trails
                    // they might be good on the ground...in the air the
                    // positions can be too inaccurate causing jumps in speed, vsi, heading etc...
                    if (mainPos.onGrnd == positionTy::GND_OFF)
                        pCosList = NULL;

                    // found trails and there are at least 2 quadrupels, i.e. really a "trail" not just a single pos?
                    if (json_array_get_count(pCosList) >= 8) {
                        if (json_array_get_count(pCosList) % 4 == 0)    // trails should be made of quadrupels
                            // iterate trail data in form of quadrupels (lat,lon,timestamp,alt):
                            for (int i=0; i< json_array_get_count(pCosList); i += 4) {
                                const positionTy& addedTrail =
                                trails.emplace_back(json_array_get_number(pCosList, i),         // latitude
                                                    json_array_get_number(pCosList, i+1),       // longitude
                                                    json_array_get_number(pCosList, i+3) * M_per_FT,     // altitude (convert to meter)
                                                    json_array_get_number(pCosList, i+2) / 1000.0);      // timestamp (convert form ms to s)
                                // only keep new trail if it is a valid position
                                if ( !addedTrail.isNormal() ) {
                                    LOG_MSG(logWARN,ERR_POS_UNNORMAL,transpIcao.c_str(),addedTrail.dbgTxt().c_str());
                                    trails.pop_back();  // otherwise remove right away
                                }
                            }
                        else
                            LOG_MSG(logERR,ADSBEX_HIST_TRAIL_ERR,transpIcao.c_str(),posTime);
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
                            (abs(mainPos.alt_m() - lastTrail.alt_m()) < 500))
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
                            if (refPos.onGrnd == positionTy::GND_OFF &&
                                mainPos.onGrnd == positionTy::GND_ON)
                            {
                                // TODO: Determine mdl based on a/c type
                                //       So far it's just the default values
                                LTAircraft::FlightModel mdl;
                                
                                
                                // this is the Landing case
                                // we look for the VSI of the last two already
                                // known off-ground positions, one is refPos
                                const double vsiBef =
                                // is there an even earlier pos before refPos in the list?
                                iBef != posList.begin() ?
                                std::prev(iBef)->between(refPos).vsi : // take the vsi between these two
                                mdl.VSI_FINAL * Ms_per_FTm;            // no, assume reasonable vsi

                                const double vsiDirectMain = refPos.between(mainPos).vsi;

                                if (vsiBef < -(mdl.VSI_STABLE * Ms_per_FTm) && vsiBef < vsiDirectMain)
                                {
                                    // reasonable negative VSI, which is less (steeper) than direct way down to mainPos
                                    for(positionTy& posIter: trails) {
                                        // calc altitude based on vsiBef, as soon as we touch ground it will be normalized to terrain altitude
                                        posIter.alt_m() = refPos.alt_m() + vsiBef * (posIter.ts()-refPos.ts());
                                        posIter.onGrnd = positionTy::GND_UNKNOWN;
                                        fd.TryDeriveGrndStatus(posIter);
                                        // only add pos if not off ground
                                        // (see above: airborne we don't want too many positions)
                                        if (posIter.onGrnd != positionTy::GND_OFF)
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
                                        posIter.onGrnd = positionTy::GND_UNKNOWN;
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
                    LOG_MSG(logWARN,ERR_POS_UNNORMAL,transpIcao.c_str(),mainPos.dbgTxt().c_str());
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

//
//MARK: OpenSkyAcMasterdata
//

bool OpenSkyAcMasterdata::FetchAllData (const positionTy& /*pos*/)
{
    if ( !IsEnabled() )
        return false;
    
    // cycle all a/c's that need master data
    bool bChannelOK = true;
    for (const std::string& transpIcao: listAcStatUpdate) {
        // skip icao of which we know they will come back invalid
        if ( std::find(invIcaos.cbegin(),invIcaos.cend(),transpIcao) != invIcaos.cend() )
            continue;
        
        // set key so that other functions can access it
        currKey = transpIcao;
        
        // make use of LTOnlineChannel's capability of reading online data
        if (LTOnlineChannel::FetchAllData(positionTy())) {
            switch (httpResponse) {
                case HTTP_OK:                       // save response
                    listMd.emplace_back(netData);
                    bChannelOK = true;
                    break;
                case HTTP_NOT_FOUND:                // doesn't know a/c, don't query again
                    invIcaos.emplace_back(transpIcao);
                    bChannelOK = true;              // but technically a valid response
                    break;
            }
        } else {
            // technical problem with fetching HTTP data
            // FIXME: Remove temp LOG once found the originator
            LOG_MSG(logWARN, "FetchAllData somehow failed for %s, httpResponse=%ld",
                    transpIcao.c_str(), httpResponse);
            bChannelOK = false;
            break;
        }
    }
    
    // done
    currKey.clear();
    
    // if no technical valid answer received set invalid
    if ( !bChannelOK ) {
        // FIXME: Remove temp LOG once found the originator
        LOG_MSG(logWARN, "FetchAllData somehow failed");
        IncErrCnt();
        return false;
    }
    
    // success
    return true;
}

// returns the openSky a/c master data URL per a/c
std::string OpenSkyAcMasterdata::GetURL (const positionTy& /*pos*/)
{
	    return std::string(OPSKY_MD_URL) + currKey;
}

// process each master data line read from OpenSky
bool OpenSkyAcMasterdata::ProcessFetchedData (mapLTFlightDataTy& /*fdMap*/)
{
    // loop all previously collected master data records
    for ( const std::string& ln: listMd) {
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
        JSON_Object* pJAc = json_object(pRoot);
        if (!pJAc) {
            LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT);
            if (IncErrCnt())
                continue;
            else
                return false;
        }
        
        // fetch values from the online data
        std::string transpIcao ( jog_s(pJAc, OPSKY_MD_TRANSP_ICAO) );
        str_toupper(transpIcao);
        statDat.reg         = jog_s(pJAc, OPSKY_MD_REG);
        statDat.country     = jog_s(pJAc, OPSKY_MD_COUNTRY);
        statDat.acTypeIcao  = jog_s(pJAc, OPSKY_MD_AC_TYPE_ICAO);
        statDat.man         = jog_s(pJAc, OPSKY_MD_MAN);
        statDat.mdl         = jog_s(pJAc, OPSKY_MD_MDL);
        statDat.op          = jog_s(pJAc, OPSKY_MD_OP);
        statDat.opIcao      = jog_s(pJAc, OPSKY_MD_OP_ICAO);
        
        // update the a/c's master data
        if ( statDat.acTypeIcao.empty() )
            LOG_MSG(logWARN,ERR_CH_INV_DATA,ChName(),transpIcao.c_str());
        UpdateStaticData(transpIcao, statDat);
    }
    
    // we've processed all data, clear it and return success
    listMd.clear();
    return true;
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
    if ( dataRefs.GetUseHistData() ) {
        // load historic data readers
        listFDC.emplace_back(new ADSBExchangeHistorical);
        // TODO: master data readers for historic data, like reading CSV file
    } else {
        // load live feed readers
        listFDC.emplace_back(new ADSBExchangeConnection);
        listFDC.emplace_back(new OpenSkyConnection);
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
        // LiveTraffic Top Level Exception Handling
        try {
            // where are we right now?
            positionTy pos (dataRefs.GetViewPos());
            
            // reset list of a/c needing master data updates
            listAcStatUpdate.clear();
            
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
            FDThreadSynchCV.wait_for(lk,
                                     std::chrono::seconds(dataRefs.GetFdRefreshIntvl()),
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
        CreateMsgWindow(float(FLIGHT_LOOP_INTVL - .05), logMSG, MSG_BUF_FILL_COUNTDOWN,
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


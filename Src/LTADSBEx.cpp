//
//  LTADSBEx.xpp
//

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

// All includes are collected in one header
#include "LiveTraffic.h"

//
//MARK: ADS-B Exchange
//

// put together the URL to fetch based on current view position
std::string ADSBExchangeConnection::GetURL (const positionTy& pos)
{
    char url[128] = "";
    snprintf(url, sizeof(url), ADSBEX_URL_ALL, pos.lat(), pos.lon(),
             dataRefs.GetFdStdDistance_km());
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
    // first get the structre's main object
    JSON_Object* pObj = json_object(pRoot);
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    
    // for determining an offset as compared to network time we need to know network time
    double adsbxTime = jog_n(pObj, ADSBEX_TIME)  / 1000.0;
    if (adsbxTime > JAN_FIRST_2019)
        // if reasonable add this to our time offset calculation
        dataRefs.ChTsOffsetAdd(adsbxTime);
    
    // fetch the aircraft array
    JSON_Array* pJAcList = json_object_get_array(pObj, ADSBEX_AIRCRAFT_ARR);
    if (!pJAcList) { LOG_MSG(logERR,ERR_JSON_ACLIST,ADSBEX_AIRCRAFT_ARR); IncErrCnt(); return false; }
    
    // iterate all aircrafts in the received flight data (can be 0)
    for ( size_t i=0; i < json_array_get_count(pJAcList); i++ )
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
        LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO,
                                     jog_s(pJAc, ADSBEX_TRANSP_ICAO));
        
        // data already stale? -> skip it
        if ( jog_b(pJAc, ADSBEX_POS_STALE) ||
            // not matching a/c filter? -> skip it
            (!acFilter.empty() && (fdKey != acFilter)) )
        {
            continue;
        }
        
        try {
            // from here on access to fdMap guarded by a mutex
            // until FD object is inserted and updated
            std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
            
            // get the fd object from the map, key is the transpIcao
            // this fetches an existing or, if not existing, creates a new one
            LTFlightData& fd = fdMap[fdKey];
            
            // also get the data access lock once and for all
            // so following fetch/update calls only make quick recursive calls
            std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
            
            // completely new? fill key fields
            if ( fd.empty() )
                fd.SetKey(fdKey);
            
            // fill static data
            {
                LTFlightData::FDStaticData stat;
                stat.reg =        jog_s(pJAc, ADSBEX_REG);
                stat.country =    jog_s(pJAc, ADSBEX_COUNTRY);
                stat.acTypeIcao = jog_s(pJAc, ADSBEX_AC_TYPE_ICAO);
                stat.man =        jog_s(pJAc, ADSBEX_MAN);
                stat.mdl =        jog_s(pJAc, ADSBEX_MDL);
                stat.engType =    (int)jog_l(pJAc, ADSBEX_ENG_TYPE);
                stat.engMount =   (int)jog_l(pJAc, ADSBEX_ENG_MOUNT);
                stat.year =  (int)jog_sl(pJAc, ADSBEX_YEAR);
                stat.mil =        jog_b(pJAc, ADSBEX_MIL);
                stat.trt          = transpTy(jog_l(pJAc,ADSBEX_TRT));
                stat.op =         jog_s(pJAc, ADSBEX_OP);
                stat.opIcao =     jog_s(pJAc, ADSBEX_OP_ICAO);
                stat.call =       jog_s(pJAc, ADSBEX_CALL);
                
                // try getting origin/destination
                // FROM
                std::string s = jog_s(pJAc, ADSBEX_ORIGIN);
                if (s.length() == 4 ||          // extract 4 letter airport code from beginning
                    (s.length() > 4 && s[4] == ' '))
                    stat.originAp = s.substr(0,4);
                // TO
                s = jog_s(pJAc, ADSBEX_DESTINATION);
                if (s.length() == 4 ||          // extract 4 letter airport code from beginning
                    (s.length() > 4 && s[4] == ' '))
                    stat.destAp = s.substr(0,4);
                
                // update the a/c's master data
                fd.UpdateData(std::move(stat));
            }
            
            // dynamic data
            {   // unconditional...block is only for limiting local variables
                LTFlightData::FDDynamicData dyn;
                
                // ADS-B returns Java tics, that is milliseconds, we use seconds
                double posTime = jog_n(pJAc, ADSBEX_POS_TIME) / 1000.0;
                
                // non-positional dynamic data
                dyn.radar.code =        jog_sl(pJAc, ADSBEX_RADAR_CODE);
                dyn.gnd =               jog_b(pJAc, ADSBEX_GND);
                dyn.heading =           jog_n_nan(pJAc, ADSBEX_HEADING);
                dyn.inHg =              jog_n(pJAc, ADSBEX_IN_HG);
                dyn.brng =              jog_n(pJAc, ADSBEX_BRNG);
                dyn.dst =               jog_n(pJAc, ADSBEX_DST);
                dyn.spd =               jog_n(pJAc, ADSBEX_SPD);
                dyn.vsi =               jog_n(pJAc, ADSBEX_VSI);
                dyn.ts =                posTime;
                dyn.pChannel =          this;
                
                // position and its ground status
                positionTy pos (jog_n_nan(pJAc, ADSBEX_LAT),
                                jog_n_nan(pJAc, ADSBEX_LON),
                                // ADSB data is feet, positionTy expects meter
                                jog_n_nan(pJAc, ADSBEX_ELEVATION) * M_per_FT,
                                posTime);
                pos.onGrnd = dyn.gnd ? positionTy::GND_ON : positionTy::GND_OFF;
                
                // position is rather important, we check for validity
                if ( pos.isNormal(true) )
                    fd.AddDynData(dyn,
                                  (int)jog_l(pJAc, ADSBEX_RCVR),
                                  (int)jog_l(pJAc, ADSBEX_SIG),
                                  &pos);
                else
                    LOG_MSG(logINFO,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
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
            
            // data already stale? -> skip it
            if ( jog_b(pJAc, ADSBEX_POS_STALE) ||
                // not matching a/c filter? -> skip it
                (!acFilter.empty() && (fdKey != acFilter)) )
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
            mapFDSelectionTy::iterator selIter = selMap.find(fdKey.icao);
            
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
                selMap.emplace(std::make_pair(fdKey.icao, FDSelection {qual, std::move(ln)} ));
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
                LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO,
                                             selVal.first);
                LTFlightData& fd = fdMap[fdKey];
                
                // also get the data access lock once and for all
                // so following fetch/update calls only make quick recursive calls
                std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
                
                // completely new? fill key fields
                if ( fd.empty() )
                    fd.SetKey(fdKey);
                
                // fill static
                {
                    LTFlightData::FDStaticData stat;
                    stat.reg =        jog_s(pJAc, ADSBEX_REG);
                    stat.country =    jog_s(pJAc, ADSBEX_COUNTRY);
                    stat.acTypeIcao = jog_s(pJAc, ADSBEX_AC_TYPE_ICAO);
                    stat.man =        jog_s(pJAc, ADSBEX_MAN);
                    stat.mdl =        jog_s(pJAc, ADSBEX_MDL);
                    stat.engType =    (int)jog_l(pJAc, ADSBEX_ENG_TYPE);
                    stat.engMount =   (int)jog_l(pJAc, ADSBEX_ENG_MOUNT);
                    stat.year =  (int)jog_sl(pJAc, ADSBEX_YEAR);
                    stat.mil =        jog_b(pJAc, ADSBEX_MIL);
                    stat.trt          = transpTy(jog_l(pJAc,ADSBEX_TRT));
                    stat.op =         jog_s(pJAc, ADSBEX_OP);
                    stat.opIcao =     jog_s(pJAc, ADSBEX_OP_ICAO);
                    stat.call =       jog_s(pJAc, ADSBEX_CALL);
                    
                    // try getting origin/destination
                    // FROM
                    std::string s = jog_s(pJAc, ADSBEX_ORIGIN);
                    if (s.length() == 4 ||          // extract 4 letter airport code from beginning
                        (s.length() > 4 && s[4] == ' '))
                        stat.originAp = s.substr(0,4);
                    // TO
                    s = jog_s(pJAc, ADSBEX_DESTINATION);
                    if (s.length() == 4 ||          // extract 4 letter airport code from beginning
                        (s.length() > 4 && s[4] == ' '))
                        stat.destAp = s.substr(0,4);
                    
                    // update the a/c's master data
                    fd.UpdateData(std::move(stat));
                }
                
                // dynamic data
                LTFlightData::FDDynamicData dyn;
                
                // ADS-B returns Java tics, that is milliseconds, we use seconds
                double posTime = jog_n(pJAc, ADSBEX_POS_TIME) / 1000.0;
                
                // non-positional dynamic data
                dyn.radar.code =        jog_sl(pJAc, ADSBEX_RADAR_CODE);
                dyn.gnd =               jog_b(pJAc, ADSBEX_GND);
                dyn.heading =           jog_n_nan(pJAc, ADSBEX_HEADING);
                dyn.inHg =              jog_n(pJAc, ADSBEX_IN_HG);
                dyn.brng =              jog_n(pJAc, ADSBEX_BRNG);
                dyn.dst =               jog_n(pJAc, ADSBEX_DST);
                dyn.spd =               jog_n(pJAc, ADSBEX_SPD);
                dyn.vsi =               jog_n(pJAc, ADSBEX_VSI);
                dyn.ts =                posTime;
                dyn.pChannel =          this;
                
                fd.AddDynData(dyn,
                              (int)jog_l(pJAc, ADSBEX_RCVR),
                              (int)jog_l(pJAc, ADSBEX_SIG));
                
                // position data, including short trails
                positionTy mainPos (jog_n_nan(pJAc, ADSBEX_LAT),
                                    jog_n_nan(pJAc, ADSBEX_LON),
                                    // ADSB data is feet, positionTy expects meter
                                    jog_n_nan(pJAc, ADSBEX_ELEVATION) * M_per_FT,
                                    posTime,
                                    dyn.heading);
                
                // position is kinda important...we continue only with valid data
                if ( mainPos.isNormal() )
                {
                    // we need a good take on the ground status of mainPos
                    // for later landing detection
                    mainPos.onGrnd = dyn.gnd ? positionTy::GND_ON : positionTy::GND_OFF;
                    
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
                    if (mainPos.onGrnd == positionTy::GND_OFF)
                        pCosList = NULL;
                    
                    // found trails and there are at least 2 quadrupels, i.e. really a "trail" not just a single pos?
                    if (json_array_get_count(pCosList) >= 8) {
                        if (json_array_get_count(pCosList) % 4 == 0)    // trails should be made of quadrupels
                            // iterate trail data in form of quadrupels (lat,lon,timestamp,alt):
                            for (size_t i=0; i< json_array_get_count(pCosList); i += 4) {
                                const positionTy& addedTrail =
                                trails.emplace_back(json_array_get_number(pCosList, i),         // latitude
                                                    json_array_get_number(pCosList, i+1),       // longitude
                                                    json_array_get_number(pCosList, i+3) * M_per_FT,     // altitude (convert to meter)
                                                    json_array_get_number(pCosList, i+2) / 1000.0);      // timestamp (convert form ms to s)
                                // only keep new trail if it is a valid position
                                if ( !addedTrail.isNormal() ) {
                                    LOG_MSG(logINFO,ERR_POS_UNNORMAL,fdKey.c_str(),addedTrail.dbgTxt().c_str());
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
                    LOG_MSG(logINFO,ERR_POS_UNNORMAL,fdKey.c_str(),mainPos.dbgTxt().c_str());
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


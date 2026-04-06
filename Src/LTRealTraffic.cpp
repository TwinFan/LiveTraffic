/// @file       LTRealTraffic.cpp
/// @brief      RealTraffic: Receives and processes live tracking data
/// @see        https://rtweb.flyrealtraffic.com/
/// @details    Defines RealTrafficConnection in two different variants:\n
///             - Direct Connection:
///               - Expects RealTraffic license information
///               - Sends authentication, weather, and tracking data requests to RealTraffic servcers
///             - via RealTraffic app
///               - Sends current position to RealTraffic app\n
///               - Receives tracking data via UDP\n
///               - Interprets the response and passes the tracking data on to LTFlightData.\n
/// @author     Birger Hoppe
/// @copyright  (c) 2019-2024 Birger Hoppe
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

#if APL == 1 || LIN == 1
#include <unistd.h>
#include <fcntl.h>
#endif

// RealTraffic's atmospheric layers in meters
const std::vector<float> RT_ATMOS_LAYERS = {
    111, 323, 762, 988, 1457, 1948, 2465, 3011, 3589, 4205,
    4863, 5572, 6341, 7182, 8114, 9160, 10359, 11770, 13503, 15790
};


//
// MARK: RealTraffic Connection
//

// Set all relevant values
void RealTrafficConnection::WxTy::set(double qnh, long _tOff, bool bResetErr)
{
    QNH     = qnh;
    tOff    = _tOff;
    next    = std::chrono::steady_clock::now() + RT_DRCT_WX_WAIT;
    if (bResetErr)
        nErr    = 0;
}

// Constructor doesn't do much
RealTrafficConnection::RealTrafficConnection () :
LTFlightDataChannel(DR_CHANNEL_REAL_TRAFFIC_ONLINE, REALTRAFFIC_NAME)
{
    //purely information
    urlName  = RT_CHECK_NAME;
    urlLink  = RT_CHECK_URL;
    urlPopup = RT_CHECK_POPUP;
}

// Stop the UDP listener gracefully
void RealTrafficConnection::Stop (bool bWaitJoin)
{
    if (isRunning()) {
        if (eThrStatus < THR_STOP)
            eThrStatus = THR_STOP;          // indicate to the thread that it has to end itself

#if APL == 1 || LIN == 1
        // Mac/Lin: Try writing something to the self-pipe to stop gracefully
        if (udpPipe[1] == INVALID_SOCKET ||
            write(udpPipe[1], "STOP", 4) < 0)
        {
            // if the self-pipe didn't work:
#endif
            // close all connections, this will also break out of all
            // blocking calls for receiving message and hence terminate the threads
            udpTrafficData.Close();
            udpWeatherData.Close();
#if APL == 1 || LIN == 1
        }
#endif
    }

    // Parent class processing: Wait for the thread to join
    LTFlightDataChannel::Stop(bWaitJoin);
}


std::string RealTrafficConnection::GetStatusText () const
{
    char sIntvl[100];

    // Invalid or disabled/off?
    if (!IsValid() || !IsEnabled())
        return LTChannel::GetStatusText();

    // --- Direct Connection? ---
    // Use dataRefs setting instead of eConnType to reflect current configuration
    if (dataRefs.GetRTConnType() == RT_CONN_REQU_REPL) {
        // Only show request-specific status if thread is actually running
        std::string s = !isRunning() ? LTChannel::GetStatusText() :
            curr.eRequType == CurrTy::RT_REQU_AUTH          ? "Authenticating..." :
            curr.eRequType == CurrTy::RT_REQU_DEAUTH        ? "De-authenticating..." :
            curr.eRequType == CurrTy::RT_REQU_PARKED        ? "Fetching parked aircraft..." :
            curr.eRequType == CurrTy::RT_REQU_NEAREST_METAR ? "Fetching weather..." :
            curr.eRequType == CurrTy::RT_REQU_WEATHER       ? "Fetching weather..." :
            LTChannel::GetStatusText();
        if (isHistoric()) {                             // historic data?
            snprintf(sIntvl, sizeof(sIntvl), MSG_RT_ADJUST,
                     GetAdjustTSText().c_str());
            s += sIntvl;
        }
        if (lTotalFlights == 0) {                       // RealTraffic has no data at all???
            s += " | RealTraffic has no traffic at all! ";
            s += (isHistoric() ? "Maybe requested historic data too far in the past?" : "(full_count=0)");
        }
        return s;
    }
    
    // --- UDP/TCP connection ---
    // If we are waiting to establish a connection then we return RT-specific texts
    if (status == RT_STATUS_NONE)           return "Starting...";
    if (status == RT_STATUS_STARTING ||
        status == RT_STATUS_STOPPING)
        return GetStatusStr();
    
    // An active source of tracking data...for how many aircraft?
    std::string s = LTChannel::GetStatusText();
    // Add extended information specifically on RealTraffic connection status
    s += " | ";
    s += GetStatusStr();
    if (bWaitForBuffers)
        s += " | Waiting for buffered traffic";
    if (IsConnected() && lastReceivedTime > 0.0) {
        // add when the last msg was received
        snprintf(sIntvl,sizeof(sIntvl),MSG_RT_LAST_RCVD,
                 dataRefs.GetSimTime() - lastReceivedTime);
        s += sIntvl;
        // if receiving historic traffic say so
        if (tsAdjust > 1.0) {
            snprintf(sIntvl, sizeof(sIntvl), MSG_RT_ADJUST,
                     GetAdjustTSText().c_str());
            s += sIntvl;
        }
    }
    return s;
}

// also take care of status
void RealTrafficConnection::SetValid (bool _valid, bool bMsg)
{
    if (!_valid && status != RT_STATUS_NONE)
        SetStatus(RT_STATUS_STOPPING);
    
    LTOnlineChannel::SetValid(_valid, bMsg);
}

// virtual thread main function
void RealTrafficConnection::Main ()
{
    // Loop to facilitate a change between connection types
    while (shallRun()) {
        
        // -- Init --

        // Clear the list of historic time stamp differences
        dequeTS.clear();
        // Some more data resets to make sure we start over with the series of requests
        curr.eRequType = CurrTy::RT_REQU_AUTH;
        curr.sGUID.clear();
        rtWx.QNH = NAN;
        rtWx.nErr = 0;
        rtWx.ResetFirstTime();
        lTotalFlights = -1;

        // reset last known values
        lastReceivedTime = 0.0;
        lastKnownViewPos = positionTy();
        
        // Just distinguish between direct R/R and UDP connection
        switch (dataRefs.GetRTConnType())
        {
            case RT_CONN_REQU_REPL:
                if (dataRefs.GetRTLicense().empty()) {
                    SHOW_MSG(logERR, "Enter RealTraffic license in settings to use direct connection!");
                    SetValid(false,true);
                } else {
                    MainDirect();
                }
                break;
            case RT_CONN_APP:
                MainUDP();
                break;
        }
    }
}

//
// MARK: Direct connection via Request/Reply
//

// virtual thread main function
void RealTrafficConnection::MainDirect ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_RT_Direct", LC_ALL_MASK);
    // can right away read parked traffic if parked aircraft enabled and airport data is already available, otherwise we'll be triggered later when airport data has been processed
    bDoParkedTraffic = dataRefs.ShallKeepParkedAircraft() && LTAptAvailable();
    // If we could theoretically set weather we prepare the interpolation settings
    if (WeatherCanSet()) {
        rtWx.interp = LTWeather::ComputeInterpol(RT_ATMOS_LAYERS,
                                                 rtWx.w.atmosphere_alt_levels_m);
    }

    while ( shallRun() && dataRefs.GetRTConnType() == RT_CONN_REQU_REPL ) {
        // LiveTraffic Top Level Exception Handling
        try {
            // where are we right now?
            const positionTy pos (dataRefs.GetViewPos());

            // If the camera position is valid we can request data around it
            if (pos.isNormal()) {
                // Fetch data and process it
                curr.pos = pos;
                if (FetchAllData(pos) && ProcessFetchedData())
                    // reduce error count if processed successfully
                    // as a chance to appear OK in the long run
                    DecErrCnt();

                // Determine next action and wait time
                tNextWakeup = SetRequType(pos);
            }
            else {
                // Camera position is yet invalid, retry in a second
                tNextWakeup = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }

            // sleep for a bit or if woken up for termination
            // by condition variable trigger
            {
                std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
                FDThreadSynchCV.wait_until(lk, tNextWakeup,
                                           [this]{return !shallRun() || dataRefs.GetRTConnType() != RT_CONN_REQU_REPL;});
            }

        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
            IncErrCnt();
        } catch (...) {
            LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown type)");
            IncErrCnt();
        }
    }
    
    // Close the session with RealTraffic
    try {
        const positionTy pos (dataRefs.GetViewPos());
        SetRequType(pos);
        if (FetchAllData(pos) && ProcessFetchedData())
            // reduce error count if processed successfully
            // as a chance to appear OK in the long run
            DecErrCnt();
    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
        IncErrCnt();
    } catch (...) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown type)");
        IncErrCnt();
    }
    
    // Reset weather control (this assumes noone else can control weather and
    // would need to change once any other source in LiveTraffic can do so)
    // however only if actually stopping RT, but not in case of a switch between App and UI
    if (!shallRun())
        WeatherReset();
}

// Which request do we need next and when?
std::chrono::time_point<std::chrono::steady_clock> RealTrafficConnection::SetRequType (const positionTy& _pos)
{
    // Position as passed in
    curr.pos = _pos;
    
    // Time offset: in minutes compared to now
    curr.tOff = dataRefs.GetRTHistTimeOff();
    
    if (!shallRun()) {                                          // end the session?
        curr.eRequType = CurrTy::RT_REQU_DEAUTH;
        return std::chrono::steady_clock::now();
    }
    if (curr.sGUID.empty()) {                                   // have no GUID? Need authentication
        curr.eRequType = CurrTy::RT_REQU_AUTH;
        return std::chrono::steady_clock::now();
    }
    if (curr.eRequType == CurrTy::RT_REQU_NEAREST_METAR) {      // previous request was METAR location?
        curr.eRequType = CurrTy::RT_REQU_WEATHER;
        return tNextWeather;
    }
    const positionTy posUser = dataRefs.GetUsersPlanePos();     // Where is the user's plane just now=
    if (rtWx.nErr < RT_DRCT_MAX_WX_ERR &&                       // not yet seen too many weather request errors? _AND_
        (std::isnan(rtWx.QNH) ||                                // no Weather, or wrong time offset, or outdated, or moved too far away?
         std::labs(curr.tOff - rtWx.tOff) > 120 ||
         // too far? (we use half the max. METAR distance
         rtWx.pos.distRoughSqr(posUser) > (sqr(dataRefs.GetWeatherMaxMetarDist_m()/2.0)) ||
         // or turned by at least 45 degrees? (Heading into a different direction can mean to select a different METAR)
         std::abs(HeadingDiff(rtWx.pos.heading(), posUser.heading())) > 45.0))
    {
        curr.eRequType = CurrTy::RT_REQU_NEAREST_METAR;
        if (std::labs(curr.tOff - rtWx.tOff) > 120)             // if changing the timeoffset (request other historic data) then we must have new weather before proceeding
            rtWx.QNH = NAN;
        return tNextWeather;
    }
    if (rtWx.nErr < RT_DRCT_MAX_WX_ERR &&                       // not yet seen too many weather request errors? _AND_
        std::chrono::steady_clock::now() >= rtWx.next)          // just time for a weather update
    {
        curr.eRequType = CurrTy::RT_REQU_WEATHER;
        return tNextWeather;
    }
    if (bDoParkedTraffic && LTAptAvailable()) {                 // Do the parked traffic now, and only when airport details are available so we can place the aircraft correctly
        curr.eRequType = CurrTy::RT_REQU_PARKED;
        return tNextTraffic;
    }
    
    // in all other cases we ask for traffic data
    curr.eRequType = CurrTy::RT_REQU_TRAFFIC;
    return tNextTraffic;
}


// in direct mode return URL and set
std::string RealTrafficConnection::GetURL (const positionTy&)
{
    // Make sure we accept GZIPed encoding
    curl_errtxt[0] = '\0';
    CURLcode ret = curl_easy_setopt(pCurl, CURLOPT_ACCEPT_ENCODING, "gzip");
    if (ret != CURLE_OK) {
        LOG_MSG(logWARN, "Could not set to accept gzip encoding: %d - %s",
                ret, curl_errtxt);
    }
    
    // What kind of request do we need next?
    switch (curr.eRequType) {
        case CurrTy::RT_REQU_AUTH:
            return RT_AUTH_URL;
        case CurrTy::RT_REQU_DEAUTH:
            return RT_DEAUTH_URL;
        case CurrTy::RT_REQU_NEAREST_METAR:
            return RT_NEAREST_METAR_URL;
        case CurrTy::RT_REQU_WEATHER:
            return RT_WEATHER_URL;
        case CurrTy::RT_REQU_PARKED:
        case CurrTy::RT_REQU_TRAFFIC:
            return RT_TRAFFIC_URL;
    }
    return RT_TRAFFIC_URL;
}

// in direct mode puts together the POST request with the position data etc.
void RealTrafficConnection::ComputeBody (const positionTy&)
{
    char s[256] = "";
    
    // What kind of request will we need?
    switch (curr.eRequType) {
        case CurrTy::RT_REQU_AUTH:
            snprintf(s,sizeof(s),
                     stribeginwith(dataRefs.GetRTLicense(), "rt_") ?
                     RT_AUTH_TOKEN_POST : RT_AUTH_LIC_POST,
                     dataRefs.GetRTLicense().c_str(),
                     HTTP_USER_AGENT);
            break;
        case CurrTy::RT_REQU_DEAUTH:
            snprintf(s, sizeof(s), RT_DEAUTH_POST,
                     curr.sGUID.c_str());
            break;
        case CurrTy::RT_REQU_NEAREST_METAR:
            rtWx.pos = dataRefs.GetUsersPlanePos();
            snprintf(s, sizeof(s), RT_NEAREST_METAR_POST,
                     curr.sGUID.c_str(),
                     rtWx.pos.lat(), rtWx.pos.lon(),
                     curr.tOff);
            break;
        case CurrTy::RT_REQU_WEATHER:
            rtWx.pos = dataRefs.GetUsersPlanePos();
            snprintf(s, sizeof(s), RT_WEATHER_POST,
                     curr.sGUID.c_str(),
                     rtWx.pos.lat(), rtWx.pos.lon(), std::lround(rtWx.pos.alt_ft()),
                     rtWx.nearestMETAR.ICAO.c_str(),
                     curr.tOff);
            break;
        case CurrTy::RT_REQU_PARKED:
        {
            // we add 10% to the bounding box to have some data ready once the plane is close enough for display
            const boundingBoxTy box (curr.pos, double(dataRefs.GetFdStdDistance_m()) * 1.10);
            snprintf(s,sizeof(s),
                     RT_TRAFFIC_POST_PARKED,
                     curr.sGUID.c_str(),
                     box.top(),  box.bottom(),
                     box.left(), box.right(),
                     curr.tOff);
            break;
        }
        case CurrTy::RT_REQU_TRAFFIC:
        {
            // we add 10% to the bounding box to have some data ready once the plane is close enough for display
            const boundingBoxTy box (curr.pos, double(dataRefs.GetFdStdDistance_m()) * 1.10);
            
            // If we request traffic for the very first time or if user jumped far, then we ask for some buffer into the past for faster plane display
            if (!lastKnownViewPos.isNormal() ||
                lastKnownViewPos.distRoughSqr(curr.pos) > sqr(dataRefs.GetFdStdDistance_m()/2))
            {
                if (lastKnownViewPos.isNormal()) {
                    LOG_MSG(logDEBUG, "Moved far, by %.1fnm",
                            lastKnownViewPos.dist(curr.pos) / M_per_NM);
                }
                lastKnownViewPos = curr.pos;
                
                // Send buffered traffic request
                snprintf(s,sizeof(s), RT_TRAFFIC_POST_BUFFER,
                         curr.sGUID.c_str(),
                         box.top(),  box.bottom(),
                         box.left(), box.right(),
                         curr.tOff,
                         GetNumTrafficBuffers(),
                         RT_BUFFER_PERIOD);
            }
            // normal un-buffered request for traffic
            else {
                snprintf(s,sizeof(s),
                         RT_TRAFFIC_POST,
                         curr.sGUID.c_str(),
                         box.top(),  box.bottom(),
                         box.left(), box.right(),
                         curr.tOff);
            }
            break;
        }
    }
    
    requBody = s;
}

// in direct mode process the received data
bool RealTrafficConnection::ProcessFetchedData ()
{
    // No data!
    if (!netDataPos) {
        if (httpResponse != HTTP_OK)
            IncErrCnt();
        return false;
    }
    
    // Try to parse as JSON...even in case of errors we might be getting a body
    // Unique_ptr ensures it is freed before leaving the function
    JSONRootPtr pRoot (netData);
    if (!pRoot) { LOG_MSG(logERR,ERR_JSON_PARSE); IncErrCnt(); return false; }
    JSON_Object* pObj = json_object(pRoot.get());
    if (!pObj) { LOG_MSG(logERR,ERR_JSON_MAIN_OBJECT); IncErrCnt(); return false; }
    
    // Try the error fields first
    long rStatus = jog_l(pObj, "status");
    if (!rStatus) { LOG_MSG(logERR,"Response has no 'status'"); IncErrCnt(); return false; }
    
    std::string rMsg = jog_s(pObj, "message");
    
    // --- Error processing ---
    
    // For failed weather requests keep a separate counter
    if (curr.eRequType == CurrTy::RT_REQU_WEATHER && rStatus != HTTP_OK) {
        if (++rtWx.nErr >= RT_DRCT_MAX_WX_ERR) { // Too many WX errors?
            SHOW_MSG(logERR, "Too many errors trying to fetch RealTraffic weather, will continue without; planes may appear at slightly wrong altitude.");
        }
    }
    
    switch (rStatus) {
        case HTTP_OK:
            break;                                  // All good, just continue
            
        case HTTP_PAYMENT_REQU:
        case HTTP_NOT_FOUND:
            if (curr.eRequType == CurrTy::RT_REQU_AUTH) {
                SHOW_MSG(logERR, "RealTraffic license invalid: %s", rMsg.c_str());
                SetValid(false,true);               // set invalid, stop trying
                return false;
            } else {
                LOG_MSG(logWARN, "RealTraffic returned: %s", rMsg.c_str());
                IncErrCnt();
                tNextTraffic = tNextWeather = std::chrono::steady_clock::now() + RT_DRCT_ERR_WAIT;
                return false;
            }
            
        case HTTP_METH_NOT_ALLWD:                   // Send for "too many sessions" / "request rate violation"
            LOG_MSG(logERR, "RealTraffic: %s", rMsg.c_str());
            IncErrCnt();
            // documentation says "wait 10 seconds"
            tNextTraffic = tNextWeather = std::chrono::steady_clock::now() + RT_DRCT_ERR_RATE;
            curr.sGUID.clear();                     // force re-login
            return false;
            
        case HTTP_UNAUTHORIZED:                     // means our GUID expired
            LOG_MSG(logDEBUG, "Session expired");
            curr.sGUID.clear();                     // re-login immediately
            tNextTraffic = tNextWeather = std::chrono::steady_clock::now();
            return false;
            
        case HTTP_FORBIDDEN:
            LOG_MSG(logWARN, "RealTraffic forbidden: %s", rMsg.c_str());
            IncErrCnt();
            tNextTraffic = tNextWeather = std::chrono::steady_clock::now() + RT_DRCT_ERR_WAIT;
            return false;
            
        case HTTP_INTERNAL_ERR:
        default:
            LOG_MSG(logERR, "RealTraffic returned an error: %s", rMsg.c_str());
            IncErrCnt();
            tNextTraffic = tNextWeather = std::chrono::steady_clock::now() + RT_DRCT_ERR_WAIT;
            return false;
    }
    
    // All good, process the request
    
    // Wait till next request?
    long rrl = jog_l(pObj, "rrl");                  // Wait time till next request, separately for traffic and weather
    long wrrl = jog_l(pObj, "wrrl");
    switch (curr.eRequType) {
        case CurrTy::RT_REQU_AUTH:                  // in most cases we continue as quickly as possible
        case CurrTy::RT_REQU_DEAUTH:
        case CurrTy::RT_REQU_PARKED:
        case CurrTy::RT_REQU_WEATHER:
            break;
        case CurrTy::RT_REQU_NEAREST_METAR:         // after learning the META we continue quickly with the weather request
            wrrl = 50;
            break;
        case CurrTy::RT_REQU_TRAFFIC:               // By default we wait at least 8s, or more if RealTraffic instructs us so
            if (rrl < RT_DRCT_DEFAULT_WAIT)
                rrl = RT_DRCT_DEFAULT_WAIT;
            break;
    }
    if (rrl > 0)
        tNextTraffic = std::chrono::steady_clock::now() + std::chrono::milliseconds(rrl);
    if (wrrl > 0)
        tNextWeather = std::chrono::steady_clock::now() + std::chrono::milliseconds(wrrl);
    
    // --- Authorization ---
    if (curr.eRequType == CurrTy::RT_REQU_AUTH) {
        eLicType = RTLicTypeTy(jog_l(pObj, "type"));
        curr.sGUID = jog_s(pObj, "GUID");
        if (curr.sGUID.empty()) {
            LOG_MSG(logERR, "Did not actually receive a GUID:\n%s", netData);
            IncErrCnt();
            return false;
        }
        LOG_MSG(logDEBUG, "Authenticated: type=%d, GUID=%s",
                eLicType, curr.sGUID.c_str());
        return true;
    }
    
    // --- De-authentication (closing the session) ---
    if (curr.eRequType == CurrTy::RT_REQU_DEAUTH) {
        curr.sGUID.clear();
        LOG_MSG(logDEBUG, "De-authenticated");
        return true;
    }
    
    // --- Nearest METAR location ---
    if (curr.eRequType == CurrTy::RT_REQU_NEAREST_METAR) {
        ProcessNearestMETAR(json_object_get_array(pObj, "data"));
        return true;
    }
    
    // --- Weather ---
    if (curr.eRequType == CurrTy::RT_REQU_WEATHER) {
        // Weather is in the 'data' object
        return PreProcessWeather(json_object_get_object(pObj, "data"));
   }
    
    // --- Parked Aircraft ---
    if (curr.eRequType == CurrTy::RT_REQU_PARKED) {
        bDoParkedTraffic = false;                       // Repeat only when instructed
        return ProcessParkedAcBuffer(json_object_get_object(pObj, "data"));
    }

    // --- Traffic data ---
    
    // In `dataepoch` RealTraffic delivers the point in time when the data was valid.
    // That is relevant especially for historic data, when `dataepoch` is in the past.
    long epoch = jog_l(pObj, "endepoch");               // we're getting an 'endepoch' only for buffered data, but if so we need to process this one
    if (epoch < long(JAN_FIRST_2019))
        epoch = jog_l(pObj, "dataepoch");               // this is the value we typically will process
    if (epoch > long(JAN_FIRST_2019))
    {
        // "now" is the simulated time plus the buffering period
        const long simTime  = long(dataRefs.GetSimTime());
        const long bufTime  = long(dataRefs.GetFdBufPeriod());
        // As long as the timestamp is half the buffer time close to "now" we consider the data current, ie. non-historic
        if (epoch > simTime + bufTime/2) {
            if (tsAdjust > 0.0) {                       // is that a change from historic delivery?
                SHOW_MSG(logINFO, INFO_RT_REAL_TIME);
            }
            tsAdjust = 0.0;
        }
        // we have historic data
        else {
            long diff = simTime + bufTime - epoch;      // difference between "now"
            diff -= 10;                                 // rounding 10s above the minute down,
            diff += 60 - diff % 60;                     // everything else up to the next minute
            if (long(tsAdjust) != diff) {               // is this actually a change?
                tsAdjust = double(diff);
                SHOW_MSG(logINFO, INFO_RT_ADJUST_TS, GetAdjustTSText().c_str());
            }
        }
    }
    
    // If RealTraffic returns `full_count = 0` then something's wrong...
    // like data requested too far in the past
    lTotalFlights = jog_l(pObj, "full_count");
    if (lTotalFlights == 0) {
        static std::chrono::steady_clock::time_point prevWarn;
        const auto now = std::chrono::steady_clock::now();
        if (now - prevWarn > std::chrono::minutes(5)) {
            SHOW_MSG(logWARN, "RealTraffic has no traffic at all! %s",
                     curr.tOff > 0 ? "Maybe requested historic data too far in the past?" : "(full_count=0)");
            prevWarn = now;
        }
    }
    
    // The 'data' object holds the aircraft data in two different variants:
    // - directly, then it holds a set of objects, each being an aircraft (essentially a fake array)
    // - buffered, then it holds a set of objects, which in turn hold a set of aircraft objects
    const JSON_Object* pData = json_object_get_object(pObj, "data");
    if (!pData) {
        LOG_MSG(logERR, "Response is missing the 'data' object that would have the aircraft data!");
        IncErrCnt();
        return false;
    }
    // Has buffered data? Then we need to loop those buffers in order
    bool bRet = true;
    const JSON_Object* pAcBuf = json_object_get_object(pData, "buffer_0");
    if (pAcBuf) {
        // loop over all "buffer_x" until we find no more
        for (int i = 0; pAcBuf; ++i)
        {
            char bufName[20];
            snprintf(bufName, sizeof(bufName), "buffer_%d", i);
            pAcBuf = json_object_get_object(pData, bufName);
            if (pAcBuf && !ProcessTrafficBuffer(pAcBuf)) {
                if (i == 0) {                            // buffer_0 must exist
                    LOG_MSG(logWARN, "Couldn't process 'buffer_0'!");
                    IncErrCnt();
                    bRet = false;
                }
            }
        }
    }
    // no buffered data, just process the one set of data that is there
    else {
        bRet = ProcessTrafficBuffer(pData);
    }
    
    return bRet;
}

// in direct mode process an object with aircraft data, essentially a fake array
bool RealTrafficConnection::ProcessTrafficBuffer (const JSON_Object* pBuf)
{
    // Quick exit if no data
    if (!pBuf) return false;
    
    // any a/c filter defined for debugging purposes?
    const std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // Current camera position
    const positionTy posView = dataRefs.GetViewPos();
    
    // The data is delivered in many many values,
    // ie. each plane is just a JSON_Value, its name being the hexid,
    // its value being an array with the details.
    // That means we need to traverse all values, not knowing which are in there.
    // Most are planes, but some are just ordinary values like "status" or "rll"...
    // Really bad data structure design if you'd ask me...
    //   {
    //     "a1bc56": ["a1bc56",34.21124,-118.939533, ...],
    //     "a1ef41": ["a1ef41",34.263657,-118.863373, ...],
    //     ...
    //     "a26738": ["a26738",33.671356,-117.867284, ...],
    //     "full_count": 13501, "source": "MemoryDB", "rrl": 2000, "status": 200, "dataepoch": 1703885732
    //   }
    const size_t numVals = json_object_get_count(pBuf);
    for (size_t i = 0; i < numVals && shallRun(); ++i)
    {
        // Get the array 'behind' the i-th value,
        // will fail if it is no aircraft entry
        const JSON_Value* pVal = json_object_get_value_at(pBuf, i);
        if (!pVal) break;
        const JSON_Array* pJAc = json_value_get_array(pVal);
        if (!pJAc) continue;                  // probably not an aircraft line
        
        // Check for minimum number of fields
        if (json_array_get_count(pJAc) < RT_DRCT_NUM_FIELDS) {
            LOG_MSG(logWARN, "Received too few fields in a/c record %ld", (long)i);
            IncErrCnt();
            continue;
        }
        
        // the key: transponder Icao code
        bool b = jag_l(pJAc, RT_DRCT_ICAO_ID) != 0;   // is an ICAO id?
        LTFlightData::FDKeyTy fdKey (b ? LTFlightData::KEY_ICAO : LTFlightData::KEY_RT,
                                     jag_s(pJAc, RT_DRCT_HexId));
        // not matching a/c filter? -> skip it
        if ((!acFilter.empty() && (fdKey != acFilter)) )
            continue;

        // position time
        double posTime = jag_n(pJAc, RT_DRCT_TimeStamp);
        //  (needs adjustment in case we are receiving historical data)
        posTime += tsAdjust;
        
        // position
        positionTy pos (jag_n(pJAc, RT_DRCT_Lat),
                        jag_n(pJAc, RT_DRCT_Lon),
                        NAN,                            // we take care of altitude next
                        posTime);
        if (jag_l(pJAc, RT_DRCT_Gnd) != 0)              // on ground?
            pos.f.onGrnd = GND_ON;
        else {
            pos.f.onGrnd = GND_OFF;
            double d = jag_n_nan(pJAc, RT_DRCT_BaroAlt);    // prefer baro altitude
            if (!std::isnan(d)) {
                if (!std::isnan(rtWx.QNH))
                    d = BaroAltToGeoAlt_ft(d, rtWx.QNH);
                pos.SetAltFt(d);
            }
            else                                        // else try geo altitude
                pos.SetAltFt(jag_n(pJAc, RT_DRCT_GeoAlt));
        }
        // position is rather important, we check for validity
        // (we do allow alt=NAN if on ground)
        if ( !pos.isNormal(true) ) {
            LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
            continue;
        }
        
        // Static data
        LTFlightData::FDStaticData stat;
        stat.acTypeIcao         = jag_s(pJAc, RT_DRCT_AcType);
        stat.call               = jag_s(pJAc, RT_DRCT_CallSign);
        stat.reg                = jag_s(pJAc, RT_DRCT_Reg);
        stat.setOrigDest(         jag_s(pJAc, RT_DRCT_Origin),
                                  jag_s(pJAc, RT_DRCT_Dest)  );
        stat.flight             = jag_s(pJAc, RT_DRCT_FlightNum);
        
        std::string s           = jag_s(pJAc, RT_DRCT_Category);
        stat.catDescr           = GetADSBEmitterCat(s);
        
        // RealTraffic often sends ASW20 when it should be AS20, a glider
        if (stat.acTypeIcao == "ASW20") stat.acTypeIcao = "AS20";
        
        // Static objects are all equally marked with a/c type TWR
        if ((s == "C3" || s == "C4" || s == "C5") ||
            (stat.reg == STATIC_OBJECT_TYPE && stat.acTypeIcao == STATIC_OBJECT_TYPE))
        {
            stat.reg = stat.acTypeIcao = STATIC_OBJECT_TYPE;
        }

        // Vehicle?
        if (stat.acTypeIcao == "GRND" || stat.acTypeIcao == "GND" ||// some vehicles come with type 'GRND'...
            s == "C1" || s == "C2" ||                               // emergency/surface vehicle?
            (s.empty() && (pos.f.onGrnd == GND_ON) && stat.acTypeIcao.empty() && stat.reg.empty()))
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();

        // Dynamic data
        LTFlightData::FDDynamicData dyn;
        dyn.radar.code          = std::lround(jag_sn(pJAc, RT_DRCT_Squawk));
        dyn.gnd                 = pos.f.onGrnd == GND_ON;
        // Heading: try in this order: True Heading, Track, Magnetic heading
        pVal                    = jag_FindFirstNonNull(pJAc,
                                                       { RT_DRCT_HeadTrue,
                                                         RT_DRCT_Track,
                                                         RT_DRCT_HeadMag });
        pos.heading() = dyn.heading = pVal ? json_value_get_number(pVal) : NAN;
        // Speed: try in this order: ground speed, TAS, IAS, 0.0
        pVal                    = jag_FindFirstNonNull(pJAc,
                                                       { RT_DRCT_GndSpeed,
                                                         RT_DRCT_TAS,
                                                         RT_DRCT_IAS });
        dyn.spd = pVal ? json_value_get_number(pVal) : 0.0;
        // VSI: try in this order barometric and geometric vertical speed
        pVal                    = jag_FindFirstNonNull(pJAc,
                                                       { RT_DRCT_BaroVertRate,
                                                         RT_DRCT_GeoVertRate });
        dyn.vsi = pVal ? json_value_get_number(pVal) : 0.0;
        
        dyn.ts = pos.ts();
        dyn.pChannel = this;
        
        try {
            // from here on access to fdMap guarded by a mutex
            // until FD object is inserted and updated
            std::unique_lock<std::mutex> mapFdLock (mapFdMutex);

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
            
            // add the static data
            fd.UpdateData(std::move(stat), pos.dist(posView));

            // add the dynamic data
            fd.AddDynData(dyn, 0, 0, &pos);

        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
            IncErrCnt();
        }
    }
    
    return true;
}


// in direct mode process an object with parked aircraft data, essentially a fake array
bool RealTrafficConnection::ProcessParkedAcBuffer (const JSON_Object* pData)
{
    // Quick exit if no data
    if (!pData) return false;
    
    // any a/c filter defined for debugging purposes?
    const std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // Current camera position
    const positionTy posView = dataRefs.GetViewPos();
    
    // The data is delivered in many many values,
    // ie. each plane is just a JSON_Value, its name being the hexid,
    // its value being an array with the details.
    // That means we need to traverse all values.
    //   {
    //     "7c4920": [-33.936407, 151.169229, "EGLL_569", "A388", "VH-OQA", 1721590016.01, "QFA2"],
    //     "7c5325": [-33.936333, 151.170109, "EGLL_569", "A333", "VH-QPJ", 1721597924.81, "QFA128"],
    //     "7c765a": [-33.935635, 151.17746, "EGLL_320", "A320", "VH-XNW", 1721650004.0, "JST825"]
    //   }
    
    // Additionally, at busy places like EGLL we receive duplicates,
    // ie. different aircraft said to be parked at the same location.
    // To reduce duplicates, we pre-process the data and pick the latest of duplicates
    struct parkedAcData {
        double              lat, lon, ts;
        std::string         key, acType, reg, call;
    };
    // We organize the data by parking position to be able to identify duplicates
    std::map<std::string, parkedAcData> mapPData;
    
    const size_t numVals = json_object_get_count(pData);
    for (size_t i = 0; i < numVals && shallRun(); ++i)
    {
        // Aircraft key (hexId)
        std::string key = json_object_get_name(pData, i);
        
        // Get the array 'behind' the i-th value,
        // will fail if it is no aircraft entry
        const JSON_Value* pVal = json_object_get_value_at(pData, i);
        if (!pVal) break;
        const JSON_Array* pJAc = json_value_get_array(pVal);
        if (!pJAc) continue;                  // probably not an aircraft line
        
        // Check for minimum number of fields
        if (json_array_get_count(pJAc) < RT_PARK_NUM_FIELDS ) {
            LOG_MSG(logWARN, "Received too few fields in parked a/c record %ld", (long)i);
            IncErrCnt();
            continue;
        }
        
        // Get the parking position and timestamp first to check for duplicates
        std::string parkPos = jag_s(pJAc, RT_PARK_ParkPosName);
        double ts           = jag_n(pJAc, RT_PARK_LastTimeStamp);
        if (mapPData.count(parkPos) > 0) {
            // we know that parking position already!
            parkedAcData& dat = mapPData.at(parkPos);
            if (ts > dat.ts) {                  // but new data is newer -> replace
                dat = {
                    jag_n_nan(pJAc, RT_PARK_Lat),
                    jag_n_nan(pJAc, RT_PARK_Lon),
                    ts,
                    std::move(key),
                    jag_s(pJAc, RT_PARK_AcType),
                    jag_s(pJAc, RT_PARK_Reg),
                    jag_s(pJAc, RT_PARK_CallSign)
                };
            }
        } else {
            // We don't yet know that parking position, store data in new map record
            mapPData.emplace(std::move(parkPos),
                             parkedAcData {
                jag_n_nan(pJAc, RT_PARK_Lat),
                jag_n_nan(pJAc, RT_PARK_Lon),
                ts,
                std::move(key),
                jag_s(pJAc, RT_PARK_AcType),
                jag_s(pJAc, RT_PARK_Reg),
                jag_s(pJAc, RT_PARK_CallSign)
            });
        }
    }
    
    // Now mapPData is filled with parked aircraft that we really want to process
    for (auto& p: mapPData)
    {
        parkedAcData& dat = p.second;
        
        // Get the name of the i-th value, that is the hex id
        LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO, dat.key);

        // not matching a/c filter? -> skip it
        if ((!acFilter.empty() && (fdKey != acFilter)) )
            continue;
        
        // position
        positionTy pos (dat.lat, dat.lon);
        pos.heading() = 0.0;
        pos.f.onGrnd = GND_ON;                          // parked aircraft are by definition on the ground
        // see later how TS is used: we send 3 instances to make the a/c appear immediately
        pos.ts() = dataRefs.GetSimTime() - 0.5 * double(dataRefs.GetFdBufPeriod());

        // position is rather important, we check for validity
        // (we do allow alt=NAN if on ground)
        if ( !pos.isNormal(true) ) {
            LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
            continue;
        }
        
        // Static data
        LTFlightData::FDStaticData stat;
        stat.acTypeIcao         = std::move(dat.acType);
        stat.call               = std::move(dat.call);
        stat.reg                = std::move(dat.reg);
        
        // RealTraffic often sends ASW20 when it should be AS20, a glider
        if (stat.acTypeIcao == "ASW20") stat.acTypeIcao = "AS20";
        
        // Dynamic data
        LTFlightData::FDDynamicData dyn;
        dyn.radar.mode          = xpmpTransponderMode_Off;
        dyn.gnd                 = true;
        dyn.heading             = pos.heading();
        dyn.ts                  = pos.ts();
        dyn.spd                 = 0.0;
        dyn.vsi                 = 0.0;
        dyn.pChannel            = this;
        
        // Try to find a matching "startup position" to perfectly put the aircraft in place
        positionTy startupPos = LTAptFindStartupLoc(pos,
                                                    (double)dataRefs.GetFdSnapTaxiDist_m());
        if (startupPos.isNormal(true)) {
            pos.lat()       = startupPos.lat();
            pos.lon()       = startupPos.lon();
            pos.heading()   = startupPos.heading();
        }
        
        try {
            // from here on access to fdMap guarded by a mutex
            // until FD object is inserted and updated
            std::unique_lock<std::mutex> mapFdLock (mapFdMutex);

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
            
            // add the static data
            fd.UpdateData(std::move(stat), pos.dist(posView));

            // add the "dynamic" data
            // We send in the position 3 times in enough of a time distance for the plane to appear directly
            for (int k = 0; k < 4; ++k) {
                fd.AddDynData(dyn, 0, 0, &pos);
                pos.ts() = (dyn.ts += 0.5 * double(dataRefs.GetFdBufPeriod()));
            }

        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
            IncErrCnt();
        }
    }
    
    LOG_MSG(logINFO, "Received %d parked aircraft", int(numVals));
    
    return true;
}
//
// MARK: Direct Connection, Weather processing
//


// parse RT's NearestMETAR response array entry
bool RealTrafficConnection::NearestMETAR::Parse (const JSON_Object* pObj)
{
    if (!pObj) {
        LOG_MSG(logWARN, "Array entry 'data[]' empty!");
        return false;
    }
    
    ICAO    = jog_s(pObj, "ICAO");
    dist    = jog_n_nan(pObj, "Dist");
    brgTo   = jog_n_nan(pObj, "BrgTo");
    
    return isValid();
}


// in direct mode process NearestMETAR response, find a suitable METAR from the returned array
void RealTrafficConnection::ProcessNearestMETAR (const JSON_Array* pData)
{
    if (!pData) {
        LOG_MSG(logWARN, "JSON response is missing 'data' array!");
        return;
    }
    
    // Reset until we know better
    rtWx.nearestMETAR.clear();
    
    // Array must have at least one element
    const size_t cntMetars = json_array_get_count(pData);
    if (cntMetars < 1) {
        LOG_MSG(logWARN, "Received no nearest METARs from RealTraffic ('data' array empty)");
        return;
    }
    
    // The first METAR is the closest, so a priori best cadidate to be used
    NearestMETAR m(json_array_get_object(pData, 0));
    if (!m.isValid()) {
        LOG_MSG(logWARN, "Nearest METAR ('data[0]') isn't valid");
        return;
    }
    // even the nearest station is too far away for reliable weather?
    if (m.dist > dataRefs.GetWeatherMaxMetarDist_nm()) {
        LOG_MSG(logDEBUG, "Nearest METAR location too far away, using none");
        rtWx.nearestMETAR.clear();
        return;
    }
    
    // Check for better matching station in direction of flight,
    // but start with that one
    rtWx.nearestMETAR = m;
    
    // Factor in bearing of station in relation to current plane's flight path:
    // It's worth half the distance if it is straight ahead (0 deg difference)
    // and the full distance if it is in my back (180 deg difference),
    // so that position ahead of me appear "nearer"
    double bestDist = m.dist * (180.0 + std::abs(HeadingDiff(rtWx.pos.heading(), double(m.brgTo)))) / 360.0;
    
    for (size_t i = 1; i < cntMetars; ++i)
    {
        m.Parse(json_array_get_object(pData, i));               // parse the next METAR info record
        if (m.dist > dataRefs.GetWeatherMaxMetarDist_nm())      // stop processing once the stations are too far away
            break;
        // based on weighted distance, is this station "closer" than our current best?
        const double weightedDist = m.dist * (180.0 + std::abs(HeadingDiff(rtWx.pos.heading(), double(m.brgTo)))) / 360.0;
        if (weightedDist < bestDist) {
            bestDist = weightedDist;                            // then use it!
            rtWx.nearestMETAR = m;
        }
    }

    LOG_MSG(logDEBUG, "Using Nearest METAR location %s (%.1fnm, %.0fdeg)",
            rtWx.nearestMETAR.ICAO.c_str(),
            rtWx.nearestMETAR.dist, rtWx.nearestMETAR.brgTo);
}


//
// MARK: Weather Processing for both Direct and UDP connections
//


// weather: process QNH and error, returns if successful
bool RealTrafficConnection::PreProcessWeather(const JSON_Object* pData)
{
    if (!pData) {
        LOG_MSG(logWARN, "JSON response is missing 'data' object!");
        ++rtWx.nErr;
        return false;
    }
    
    // First, we are interested in just a single value: local Pressure
    double wxQNH = jog_n_nan(pData, "QNH");             // ideally QNH
    if (std::isnan(wxQNH))
        wxQNH = jog_n_nan(pData, "locWX.SLP");          // if not given then SLP
    
    // Error in locWX data?
    std::string s = jog_s(pData, "locWX.Error");        // sometimes errors are given in a specific field
    if (s.empty()) {                                    // and at other times there is something in the 'Info' field...not very consistent
        s = jog_s(pData, "locWX.Info");
        if (!s.empty() &&
            s != "TinyDelta" &&                         // if we request too often then Info is 'TinyDelta', and we let it sit in 's'
            s.substr(0,6) != "error:")                  // any error starts with "error:" and we let it sit in 's'
            s.clear();
    }
    
    // Any error, either explicitely or because local pressure is bogus?
    if (!s.empty() || std::isnan(wxQNH) || wxQNH < 800.0)
    {
        if (s == "File requested") {
            // Error "File requested" often occurs when requesting historic weather that isn't cached on the server, so we only issue debug-level message
            LOG_MSG(logDEBUG, "Weather details being fetched at RealTraffic, will try again in 60s");
        } else {
            // Anything else is unexpected
            if (!s.empty()) {
                LOG_MSG(logERR, "Requesting RealTraffic weather returned error '%s':\n%s",
                        s.c_str(), netData);
            } else {
                LOG_MSG(logERR, "RealTraffic returned no or invalid local pressure %.1f:\n%s",
                        wxQNH, netData);
            }
        }
        // one more error
        ++rtWx.nErr;
        // If we don't yet have any pressure...
        if (std::isnan(rtWx.QNH)) {
            // Too many WX errors? We give up and just use standard pressure
            if (rtWx.nErr >= RT_DRCT_MAX_WX_ERR) {
                SHOW_MSG(logERR, "Too many errors trying to fetch RealTraffic weather, will continue without; planes may appear at slightly wrong altitude.");
                rtWx.set(HPA_STANDARD, curr.tOff, false);
                rtWx.pos = positionTy();
            } else {
                // We will request weather directly again, but need to wait 60s for it
                tNextWeather = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            }
        }
        return false;
    }
    
    // If we have METAR info pass that on, too
    s = jog_s(pData, "ICAO");
    std::string metar = jog_s(pData, "METAR");
    
    if (s.empty() || s == "UNKN") {         // ignore no/unknown METAR
        s.clear();
        metar.clear();
    }
    
    // If this is live data, not historic, then we can use it instead of separately querying METAR
    if (!isHistoric()) {
        rtWx.w.qnh_pas = dataRefs.SetWeather((float)wxQNH, s, metar);
    }
    // historic data
    else {
        // Try reading QNH from METAR
        rtWx.w.qnh_pas = WeatherQNHfromMETAR(metar);
    }
    
    // Successfully received local pressure information
    rtWx.set(std::isnan(rtWx.w.qnh_pas) ? wxQNH : double(rtWx.w.qnh_pas), curr.tOff);   // Save new QNH
    LOG_MSG(logDEBUG, "Received RealTraffic Weather with QNH = %.1f", rtWx.QNH);
    
    // If requested to set X-Plane's weather based on detailed weather data
    if (dataRefs.GetWeatherControl() == WC_REAL_TRAFFIC) {
        ProcessWeather (pData);
        if (std::isnan(rtWx.w.qnh_pas))
            rtWx.w.qnh_pas = float(rtWx.QNH);
    }
    
    return true;
}


// in direct mode process detailed weather information
void RealTrafficConnection::ProcessWeather(const JSON_Object* pData)
{
    rtWx.w = LTWeather();                                       // reset all values
    if (!pData) {
        LOG_MSG(logWARN, "JSON response is missing 'data' object!");
        return;
    }
    const JSON_Object* pLocWX = json_object_get_object(pData, "locWX");
    if (!pLocWX) {
        LOG_MSG(logWARN, "JSON response is missing 'data.locWX' object!");
        return;
    }
    
    // --- Process detailed weather data ---
    const JSON_Array* pDPs      = json_object_get_array(pLocWX, "DPs");
    const JSON_Array* pTEMPs    = json_object_get_array(pLocWX, "TEMPs");
    const JSON_Array* pWDIRs    = json_object_get_array(pLocWX, "WDIRs");
    const JSON_Array* pWSPDs    = json_object_get_array(pLocWX, "WSPDs");
    const JSON_Array* pDZDTs    = json_object_get_array(pLocWX, "DZDTs");
    std::vector<float> aTEMPs;
    
    if (!pDPs || !pTEMPs || !pWDIRs || !pWSPDs || !pDZDTs) {
        LOG_MSG(logWARN, "JSON response is missing one of the following arrays in data.locWX: DPs, TEMPs, WDIRs, WSPDs, DZDTs");
    }
    
    rtWx.w.pos                          = rtWx.pos;
    
    // METAR
    rtWx.w.metar                        = jog_s(pData, "METAR");
    rtWx.w.metarFieldIcao               = jog_s(pData, "ICAO");
    rtWx.w.posMetarField = positionTy();                // field's location is to be determined later in main thread

    rtWx.w.visibility_reported_sm       = float(jog_n_nan(pLocWX, "SVis") / M_per_SM);
    rtWx.w.sealevel_pressure_pas        = float(jog_n_nan(pLocWX, "SLP") * 100.0);
    if (pTEMPs && json_array_get_count(pTEMPs) >= 1)                    // use temperature of lowest level, adjusted per temperature lapse rate
        rtWx.w.sealevel_temperature_c   = float(jag_n_nan(pTEMPs, 0)) - RT_ATMOS_LAYERS.front() * float(TEMP_LAPS_R);
    rtWx.w.rain_percent                 = std::min(float(jog_n_nan(pLocWX, "PRR")) / 9.0f, 1.0f);   // RT sends mm/h and says ">7.5 is heavy", XP wants a 0..1 scale
    
    // Wind
    rtWx.w.wind_altitude_msl_m          = rtWx.w.atmosphere_alt_levels_m;       // we just use the standard atmospheric layers of XP, need to interpolate anyway as RT sends more layers
    if (pWSPDs) {
        rtWx.w.Interpolate(rtWx.interp, jag_f_vector(pWSPDs), rtWx.w.wind_speed_msc);
        std::for_each(rtWx.w.wind_speed_msc.begin(), rtWx.w.wind_speed_msc.end(),
                      [](float& f){ f *= float(MSC_per_KMH); });                // convert from km/h to m/s
    }
    if (pWDIRs)
        rtWx.w.InterpolateDir(rtWx.interp, jag_f_vector(pWDIRs), rtWx.w.wind_direction_degt);
    if (pDZDTs) {
        rtWx.w.Interpolate(rtWx.interp, jag_f_vector(pDZDTs), rtWx.w.turbulence);
        std::for_each(rtWx.w.turbulence.begin(), rtWx.w.turbulence.end(),
                      [](float& f){ f = std::clamp<float>(f * 0.5f, 0.0f, 1.0f); });         // convert from RT's scale (">2 severe") to XP's of 0.0..1.0
    }
   
    // Temperature
    if (pDPs)
        rtWx.w.Interpolate(rtWx.interp, jag_f_vector(pDPs), rtWx.w.dewpoint_deg_c);
    rtWx.w.temperature_altitude_msl_m   = rtWx.w.atmosphere_alt_levels_m;
    if (pTEMPs)
        rtWx.w.Interpolate(rtWx.interp, aTEMPs = jag_f_vector(pTEMPs), rtWx.w.temperatures_aloft_deg_c);

    // Cloud layers
    ProcessCloudLayer(json_object_get_object(pLocWX, "LLC"), 0);
    ProcessCloudLayer(json_object_get_object(pLocWX, "MLC"), 1);
    ProcessCloudLayer(json_object_get_object(pLocWX, "HLC"), 2);
    
    // Troposhere
    rtWx.w.tropo_alt_m = float(jog_n_nan(pLocWX, "TPP"));
    if (!aTEMPs.empty() && !std::isnan(rtWx.w.tropo_alt_m))
        rtWx.w.tropo_temp_c = interpolate(RT_ATMOS_LAYERS, aTEMPs, rtWx.w.tropo_alt_m);
    
    // Waves
    rtWx.w.wave_dir = float(jog_n_nan(pLocWX, "SWDIR"));                        // we just use surface wind direction directly
    if (std::isnan(rtWx.w.wave_dir))
        rtWx.w.wave_dir = rtWx.w.wind_direction_degt.front();
    float SWSPD = float(jog_n_nan(pLocWX, "SWSPD"));                            // we determine the wave amplitude based on surface wind speed [km/h]
    if (!std::isnan(SWSPD)) {
        // We take wave amplitudes from https://www.wpc.ncep.noaa.gov/html/beaufort.shtml
        SWSPD *= (float)NM_per_KM;                                              // convert to knots
        if      (SWSPD <  1) rtWx.w.wave_amplitude =  0.0f;
        else if (SWSPD <  4) rtWx.w.wave_amplitude =  0.1f;
        else if (SWSPD <  7) rtWx.w.wave_amplitude =  0.25f;
        else if (SWSPD < 11) rtWx.w.wave_amplitude =  0.8f;
        else if (SWSPD < 17) rtWx.w.wave_amplitude =  1.25f;
        else if (SWSPD < 22) rtWx.w.wave_amplitude =  2.25f;
        else if (SWSPD < 28) rtWx.w.wave_amplitude =  3.5f;
        else if (SWSPD < 34) rtWx.w.wave_amplitude =  4.75f;
        else if (SWSPD < 41) rtWx.w.wave_amplitude =  6.5f;
        else if (SWSPD < 48) rtWx.w.wave_amplitude =  8.5f;
        else if (SWSPD < 56) rtWx.w.wave_amplitude = 10.75f;
        else if (SWSPD < 64) rtWx.w.wave_amplitude = 13.75f;
        else                 rtWx.w.wave_amplitude = 17.0f;
    }
    
    // Rwy_Friction
    if (rtWx.w.metar.empty())                   // ideally set later based on METAR
        // Rain causes wet status [0..7]
        rtWx.w.runway_friction = (int)std::lround(rtWx.w.rain_percent * 7.f);

    // Have the weather set (force immediate update on first weather data)
    rtWx.w.update_immediately = rtWx.IsFirstTime();
    WeatherSet(rtWx.w);
}

// Process one cloud layer
void RealTrafficConnection::ProcessCloudLayer(const JSON_Object* pCL, size_t i)
{
    if (!pCL) return;
    const float cover = (float) jog_n_nan(pCL, "cover");
    const float base  = (float) jog_n_nan(pCL, "base");
    const float tops  = (float) jog_n_nan(pCL, "tops");
    const float type  = (float) jog_n_nan(pCL, "type");
    const float conf  = (float) jog_n_nan(pCL, "confidence");
    if (std::isnan(cover) || std::isnan(base) || std::isnan(tops) || std::isnan(type) || std::isnan(conf))
        return;
    
    rtWx.w.cloud_type[i]             = type;
    rtWx.w.cloud_coverage_percent[i] = cover/100.0f;
    rtWx.w.cloud_base_msl_m[i]       = base;
    rtWx.w.cloud_tops_msl_m[i]       = tops;
}



//
// MARK: UDP/TCP via App
//

// virtual thread main function
void RealTrafficConnection::MainUDP ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_RT_App", LC_ALL_MASK);

    // Top-level exception handling
    try {
        // set startup status
        SetStatus(RT_STATUS_STARTING);
        
        // When starting up we definitely want to request buffered traffic first, so make sure we don't process live traffic
        bWaitForBuffers = true;

        // If we could theoretically set weather we prepare the interpolation settings
        if (WeatherCanSet()) {
            rtWx.interp = LTWeather::ComputeInterpol(RT_ATMOS_LAYERS,
                                                     rtWx.w.atmosphere_alt_levels_m);
        }

        // --- UDP Listeners ---

        // Open the UDP port for traffic data (port 49005 by default)
        udpTrafficData.Open (RT_LOCALHOST,
                             DataRefs::GetCfgInt(DR_CFG_RT_TRAFFIC_PORT),
                             RT_NET_BUF_SIZE);
        int maxSock = (int)udpTrafficData.getSocket() + 1;

        // Open the UDP port for weather data (port 49004 by default)
        udpWeatherData.Open (RT_LOCALHOST,
                             DataRefs::GetCfgInt(DR_CFG_RT_WEATHER_PORT),
                             RT_NET_BUF_SIZE);
        maxSock = std::max(maxSock, (int)udpWeatherData.getSocket() + 1);

#if APL == 1 || LIN == 1
        // the self-pipe to shut down the UDP socket gracefully
        if (pipe(udpPipe) < 0)
            throw XPMP2::NetRuntimeError("Couldn't create pipe");
        fcntl(udpPipe[0], F_SETFL, O_NONBLOCK);
        maxSock = std::max(maxSock, udpPipe[0]+1);
#endif

        // Start the TCP listening thread, that waits for an incoming TCP connection from the RealTraffic app
        StartTcpConnection();

        // Next time we should send a position update
        std::chrono::time_point<std::chrono::steady_clock> tNextPos;
        // count position send transmissions...to be able to every once in a while send a time request along with it
        int nCountPosSent = -1;                             // -1 is a flag to send the initial traffic request
        
        LOG_MSG(logINFO, "RealTraffic Application: Listening for traffic on UDP port %d, weather on UDP port %d",
                DataRefs::GetCfgInt(DR_CFG_RT_TRAFFIC_PORT),
                DataRefs::GetCfgInt(DR_CFG_RT_WEATHER_PORT));

        // --- Main Loop ---

        while (shallRun() && udpTrafficData.isOpen() && IsConnecting() && dataRefs.GetRTConnType() == RT_CONN_APP)
        {
            // wait for a UDP datagram on either socket (traffic, weather)
            fd_set sRead;
            FD_ZERO(&sRead);
            FD_SET(udpTrafficData.getSocket(), &sRead);     // check traffic socket
            FD_SET(udpWeatherData.getSocket(), &sRead);     // check weather socket
#if APL == 1 || LIN == 1
            FD_SET(udpPipe[0], &sRead);
#endif
            // We specify a short timeout (200ms) to ensure we can send position updates at 5 Hz
            struct timeval timeout = {
                 dataRefs.GetRTSendPosFrequ() / 1000,           // seconds
                (dataRefs.GetRTSendPosFrequ() % 1000) * 1000    // microseconds
            };
            int retval = select(maxSock, &sRead, NULL, NULL, &timeout);

            // short-cut if we are to shut down or connection type changed
            if (!shallRun() || dataRefs.GetRTConnType() != RT_CONN_APP) break;

            // select call failed???
            if(retval == -1)
                throw XPMP2::NetRuntimeError("'select' failed");

            // select successful - traffic data
            if (retval > 0 && FD_ISSET(udpTrafficData.getSocket(), &sRead))
            {
                // read UDP datagram
                long rcvdBytes = udpTrafficData.recv();

                // received something?
                if (rcvdBytes > 0)
                {
                    // yea, we received something!
                    SetStatusUdp(true, false);

                    // have it processed
                    ProcessRecvedTrafficData(udpTrafficData.getBuf());
                }
                else
                    retval = -1;
            }

            // select successful - weather data
            if (retval > 0 && FD_ISSET(udpWeatherData.getSocket(), &sRead))
            {
                // read UDP datagram
                const long rcvdBytes = udpWeatherData.recv();

                // received something?
                if (rcvdBytes > 0)
                {
                    // have it processed
                    ProcessRecvedWeatherData(udpWeatherData.getBuf());
                }
            }
            // handling of errors, both from select and from recv
            if (retval < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                // not just a normal timeout?
                char sErr[SERR_LEN];
                strerror_s(sErr, sizeof(sErr), errno);
                LOG_MSG(logERR, ERR_UDP_RCVR_RCVR, ChName(),
                        sErr);
                // increase error count...bail out if too bad
                if (!IncErrCnt()) {
                    SetStatusUdp(false, true);
                    break;
                }
            }

            // --- Maintenance Activities ---

            // If we are connected via TCP to RealTraffic
            if (tcpPosSender.IsConnected()) {
                
                // Send time and position at 5 Hz (every 200ms) to keep RT App updated
                if (std::chrono::steady_clock::now() > tNextPos) {
                    
                    SendXPSimTime(nCountPosSent <= 0);  // force every once in a while
                    SendUsersPlanePos();
                    
                    // "fast move" detection
                    const positionTy viewPos = dataRefs.GetViewPos();
                    if (!lastKnownViewPos.isNormal() ||
                        lastKnownViewPos.distRoughSqr(viewPos) > sqr(dataRefs.GetFdStdDistance_m()/2))
                    {
                        if (lastKnownViewPos.isNormal()) {
                            LOG_MSG(logDEBUG, "Moved far, by %.1fnm",
                                    lastKnownViewPos.dist(viewPos) / M_per_NM);
                        }
                        RequestBufferTraffic(viewPos, dataRefs.GetFdStdDistance_m());
                        lastKnownViewPos = viewPos;
                        bWaitForBuffers = true;
                    }
                        
                    // next time in about 200ms
                    tNextPos = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(dataRefs.GetRTSendPosFrequ());
                    // count a times until we send the timing again
                    if (++nCountPosSent >= RT_CNT_SEND_TIMING)
                        nCountPosSent = 0;
                }
            }
            // Not connected by TCP, are we still listening and waiting?
            else if (eTcpThrStatus != THR_RUNNING) {
                // Not running...so make sure it restarts for us to have a chance to get a connection
                StopTcpConnection();
                StartTcpConnection();
            }

            // cleanup map of last datagrams
            CleanupMapDatagrams();
            // map is empty? That only happens if we don't receive data continuously
            if (mapDatagrams.empty())
                // set UDP status to unavailable, but keep listener running
                SetStatusUdp(false, false);
        }

    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
        IncErrCnt();
    } catch (...) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown type)");
        IncErrCnt();
    }

    // Let's make absolutely sure that any connection is really closed
    // once we return from this thread
    if (udpTrafficData.isOpen())
        udpTrafficData.Close();
    if (udpWeatherData.isOpen())
        udpWeatherData.Close();
#if APL == 1 || LIN == 1
    // close the self-pipe sockets
    for (SOCKET &s: udpPipe) {
        if (s != INVALID_SOCKET) close(s);
        s = INVALID_SOCKET;
    }
#endif

    // Make sure the TCP listener is down
    StopTcpConnection();

    // stopped
    SetStatus(RT_STATUS_NONE);

    // Reset weather control (this assumes noone else can control weather and
    // would need to change once any other source in LiveTraffic can do so)
    // however only if actually stopping RT, but not in case of a switch between App and UI
    if (!shallRun())
        WeatherReset();
}

// sets the status and updates global text to show elsewhere
void RealTrafficConnection::SetStatus(rtStatusTy s)
{
    // consistent status decision
    std::lock_guard<std::recursive_mutex> lock(rtMutex);

    status = s;
    LOG_MSG(logINFO, MSG_RT_STATUS,
            s == RT_STATUS_NONE ? "Stopped" : GetStatusStr().c_str());
}

void RealTrafficConnection::SetStatusTcp(bool bEnable, bool _bStopTcp)
{
    static bool bInCall = false;
    
    // avoid recursiv calls from error handlers
    if (bInCall)
        return;
    bInCall = true;
    
    // consistent status decision
    std::lock_guard<std::recursive_mutex> lock(rtMutex);

    if (bEnable) switch (status) {
        case RT_STATUS_NONE:
        case RT_STATUS_STARTING:
            SetStatus(RT_STATUS_CONNECTED_TO);
            break;
        case RT_STATUS_CONNECTED_PASSIVELY:
            SetStatus(RT_STATUS_CONNECTED_FULL);
            break;
        case RT_STATUS_CONNECTED_TO:
        case RT_STATUS_CONNECTED_FULL:
        case RT_STATUS_STOPPING:
            // no change
            break;
    } else {
        // Disable - also disconnect, otherwise restart wouldn't work
        if (_bStopTcp)
            StopTcpConnection();
        
        // set status
        switch (status) {
        case RT_STATUS_NONE:
        case RT_STATUS_STARTING:
        case RT_STATUS_CONNECTED_PASSIVELY:
        case RT_STATUS_STOPPING:
            // no change
            break;
        case RT_STATUS_CONNECTED_TO:
            SetStatus(RT_STATUS_STARTING);
            break;
        case RT_STATUS_CONNECTED_FULL:
            SetStatus(RT_STATUS_CONNECTED_PASSIVELY);
            break;

        }
        
    }
    
    bInCall = false;

}

void RealTrafficConnection::SetStatusUdp(bool bEnable, bool _bStopUdp)
{
    static bool bInCall = false;
    
    // avoid recursiv calls from error handlers
    if (bInCall)
        return;
    bInCall = true;
    
    // consistent status decision
    std::lock_guard<std::recursive_mutex> lock(rtMutex);
    
    if (bEnable) switch (status) {
        case RT_STATUS_NONE:
        case RT_STATUS_STARTING:
            SetStatus(RT_STATUS_CONNECTED_PASSIVELY);
            break;
        case RT_STATUS_CONNECTED_TO:
            SetStatus(RT_STATUS_CONNECTED_FULL);
            break;
        case RT_STATUS_CONNECTED_PASSIVELY:
        case RT_STATUS_CONNECTED_FULL:
        case RT_STATUS_STOPPING:
            // no change
            break;
    } else {
        // Disable - also disconnect, otherwise restart wouldn't work
        if (_bStopUdp)
            eThrStatus = THR_STOP;
        
        // set status
        switch (status) {
        case RT_STATUS_NONE:
        case RT_STATUS_STARTING:
        case RT_STATUS_CONNECTED_TO:
        case RT_STATUS_STOPPING:
            // no change
            break;
        case RT_STATUS_CONNECTED_PASSIVELY:
            SetStatus(RT_STATUS_STARTING);
            break;
        case RT_STATUS_CONNECTED_FULL:
            SetStatus(RT_STATUS_CONNECTED_TO);
            break;
        }
    }
    
    bInCall = false;
}
    
std::string RealTrafficConnection::GetStatusStr() const
{
    switch (status) {
        case RT_STATUS_NONE:                return "";
        case RT_STATUS_STARTING:            return "Waiting for RealTraffic...";
        case RT_STATUS_CONNECTED_PASSIVELY: return "Connected passively";
        case RT_STATUS_CONNECTED_TO:        return "Connected, waiting...";
        case RT_STATUS_CONNECTED_FULL:      return "Fully connected";
        case RT_STATUS_STOPPING:            return "Stopping...";
    }
    return "";
}

//
// MARK: TCP Connection
//

// main function of TCP listening thread, lives only until TCP connection established
void RealTrafficConnection::tcpConnection ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_RT_TCP", LC_ALL_MASK);
    eTcpThrStatus = THR_RUNNING;
    
    // port to use is configurable
    int tcpPort = DataRefs::GetCfgInt(DR_CFG_RT_LISTEN_PORT);
    
    try {
        tcpPosSender.Open (RT_LOCALHOST, tcpPort, RT_NET_BUF_SIZE);
        LOG_MSG(logDEBUG, "RealTraffic: Listening on port %d for TCP connection by RealTraffic App", tcpPort);
        if (tcpPosSender.listenAccept()) {
            // so we did accept a connection!
            LOG_MSG(logDEBUG, "RealTraffic: Accepted TCP connection from RealTraffic App");
            SetStatusTcp(true, false);
        }
        else
        {
            // short-cut if we are to shut down (return from 'select' due to closed socket)
            if (eTcpThrStatus < THR_STOP) {
                // not forced to shut down...report other problem
                SHOW_MSG(logERR,ERR_RT_CANTLISTEN);
                SetStatusTcp(false, true);
            }
        }
    }
    catch (std::runtime_error& e) {
        LOG_MSG(logERR, ERR_TCP_LISTENACCEPT, ChName(),
                RT_LOCALHOST, std::to_string(tcpPort).c_str(),
                e.what());
        // invalidate the channel
        SetStatusTcp(false, true);
        SetValid(false, true);
    }
    
    // We make sure that, once leaving this thread, there is no
    // open listener (there might be a connected socket, though)
#if IBM
    if (eTcpThrStatus < THR_STOP)   // already closed if stop flag set, avoid rare crashes if called in parallel
#endif
        tcpPosSender.CloseListenerOnly();
    eTcpThrStatus = THR_ENDED;
}


// start the TCP listening thread
void RealTrafficConnection::StartTcpConnection ()
{
    if (!thrTcpServer.joinable()) {
        eTcpThrStatus = THR_STARTING;
        thrTcpServer = std::thread (&RealTrafficConnection::tcpConnection, this);
    }
}

// stop the TCP listening thread
void RealTrafficConnection::StopTcpConnection ()
{
    // close all connections, this will also break out of all
    // blocking calls for receiving message and hence terminate the threads
    eTcpThrStatus = THR_STOP;
    tcpPosSender.Close();
    
    // wait for threads to finish (if I'm not myself this thread...)
    if (std::this_thread::get_id() != thrTcpServer.get_id()) {
        if (thrTcpServer.joinable())
            thrTcpServer.join();
        thrTcpServer = std::thread();
        eTcpThrStatus = THR_NONE;
    }
}


// Send and log a message to RealTraffic
void RealTrafficConnection::SendMsg (const char* msg)
{
    if (!tcpPosSender.IsConnected())
    { LOG_MSG(logWARN,ERR_SOCK_NOTCONNECTED,ChName()); return; }
        
    // send the string
    if (!tcpPosSender.send(msg)) {
        LOG_MSG(logERR,ERR_SOCK_SEND_FAILED,ChName());
        SetStatusTcp(false, true);
    }
    DebugLogRaw(msg, HTTP_FLAG_SENDING);
}

// Send a timestamp to RealTraffic
/// @details Format is Qs123=1674984782616, where the long number is the UTC epoch milliseconds of the simulator time.
void RealTrafficConnection::SendTime (long long ts)
{
    // format the string to send and send it out
    char s[50];
    snprintf(s, sizeof(s), "Qs123=%lld\n", ts);
    SendMsg(s);
}

// Send XP's current simulated time to RealTraffic, adapted to "today or earlier"
void RealTrafficConnection::SendXPSimTime (bool bForce)
{
    const long currOff = dataRefs.GetRTHistTimeOff();
    if (bForce || currOff != curr.tOff) {               // did the historic time offset setting change?
        curr.tOff = currOff;
        // Which time stamp to send?
        const long long ts = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
        // reduce by (potential) historic time request, convert to milliseconds
                             - (long long)curr.tOff * 60000LL;
        SendTime(ts);
    }
}


// send position to RealTraffic so that RT knows which area
// we are interested and to give us local weather
// Example:
// “Qs121=6747;289;5.449771266137578;37988724;501908;0.6564195830703577;-2.1443275933742236”
void RealTrafficConnection::SendPos (const positionTy& pos, double speed_m)
{
    if (!pos.isFullyValid())
    { LOG_MSG(logWARN,ERR_SOCK_INV_POS,ChName()); return; }

    // format the string to send
    char s[200];
    snprintf(s,sizeof(s),
             "Qs121=%ld;%ld;%.15f;%ld;%ld;%.15f;%.15f\n",
             lround(deg2rad(pos.pitch()) * 100000.0),   // pitch
             lround(deg2rad(-pos.roll()) * 100000.0),   // bank/roll (inverted)
             deg2rad(pos.heading()),                    // heading
             lround(pos.alt_ft() * 1000.0),             // altitude (ft * 1000)
             lround(speed_m * KT_per_M_per_S * 1000.0), // ground speed (knots * 1000)
             deg2rad(pos.lat()),                        // latitude
             deg2rad(pos.lon())                         // longitude
    );
    
    // Send the message
    SendMsg(s);
}

// send the position of the user's plane
void RealTrafficConnection::SendUsersPlanePos()
{
    double groundSpeed_m = 0.0;
    rtWx.pos = curr.pos = dataRefs.GetUsersPlanePos(nullptr, nullptr, nullptr, &groundSpeed_m);
    SendPos(curr.pos, groundSpeed_m);
}


// Request data for buffering from the App via the TCP channel
// Qs999=sendbuffer=count,time,[bottom,left,top,right]\n
void RealTrafficConnection::RequestBufferTraffic (const positionTy& pos,
                                                  double radius_m)
{
    const boundingBoxTy box (pos, radius_m);
    // send the string to request buffer traffic
    char s[100];
    snprintf(s, sizeof(s), "Qs999=sendbuffer=%d,%d,%.2f,%.2f,%.2f,%.2f\n",
             GetNumTrafficBuffers(),
             RT_BUFFER_PERIOD,
             box.bottom(), box.left(),
             box.top(),    box.right());
    SendMsg(s);
    LOG_MSG(logINFO, "Requested buffered traffic %ldnm around %s",
            std::lround(radius_m / M_per_NM),
            std::string(pos).c_str());
}


// MARK: Traffic
// Process received traffic data.
// We keep this a bit flexible to be able to work with different formats
bool RealTrafficConnection::ProcessRecvedTrafficData (const char* traffic)
{
    // sanity check: not empty
    if (!traffic || !traffic[0])
        return false;
    
    // Raw data logging
    DebugLogRaw(traffic, HTTP_FLAG_UDP);
    lastReceivedTime = dataRefs.GetSimTime();
    
    // split the datagram up into its parts, keeping empty positions empty
    std::vector<std::string> tfc = str_tokenize(traffic, ",()", false);
    
    // not enough fields found for any message?
    if (tfc.size() < RT_MIN_TFC_FIELDS)
    {
        // RealTraffic sends various markers that we mostly don't need...we just ignore them
        if (tfc.size() == 1) {
            if (tfc[0] == "RTBUF_EOT") {        // but when done with buffers process normal traffic again
                LOG_MSG(logINFO, "RTBUF_EOT Finished receiving buffered traffic");
                bWaitForBuffers = false;
            }
            return true;
        }
        // Otherwise it's worth a warning because it's unexpected
        LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic);
        return false;
    }
    
    // *** Duplicaton Check ***
    
    // comes in all 3 formats at position 1 and in decimal form
    const unsigned long numId = std::stoul(tfc[RT_AITFC_HEXID]);
    
    // ignore aircraft, which don't want to be tracked
    if (numId == 0)
        return true;            // ignore silently
    
    // RealTraffic sends bursts of data often, but that doesn't necessarily
    // mean that anything really moved. Data could be stale.
    // So here we just completely ignore data which looks exactly like the previous datagram
    if (IsDatagramDuplicate(numId, traffic))
        return true;            // ignore silently

    // key is most likely an Icao transponder code, but could also be a Realtraffic internal id
    LTFlightData::FDKeyTy fdKey (numId <= MAX_TRANSP_ICAO ? LTFlightData::KEY_ICAO : LTFlightData::KEY_RT,
                                 numId);
    
    // not matching a/c filter? -> skip it
    const std::string acFilter ( dataRefs.GetDebugAcFilter() );
    if ((!acFilter.empty() && (fdKey != acFilter)))
        return true;            // silently

    // *** Replace 'null' ***
    std::for_each(tfc.begin(), tfc.end(),
                  [](std::string& s){ if (s == "null") s.clear(); });
    
    // *** Process different formats ****
    
    // buffered traffic?
    int nBuf = 0;
    if (std::strncmp(tfc[RT_RTTFC_REC_TYPE].c_str(), "RTBUF=", 6) == 0) {
        nBuf = std::atoi(tfc[RT_RTTFC_REC_TYPE].c_str()+6) + 1;
        if (bWaitForBuffers) {
            LOG_MSG(logDEBUG, "RTBUF Received first buffered traffic");
            bWaitForBuffers = false;        // buffered traffic has arrived, we wait no longer
        }
    }
    else {
        // live traffic...but if we are waiting for buffered then we skip it silently
        if (bWaitForBuffers)
            return true;
    }
    
    // There are 3 formats we are _really_ interested in: RTTFC, AITFC, and XTRAFFICPSX
    // Check for them and their correct number of fields
    if (tfc[RT_RTTFC_REC_TYPE] == RT_TRAFFIC_RTTFC ||                   // regular traffic
        (nBuf && tfc.size() >= RT_RTTFC_MIN_TFC_FIELDS))                // buffered traffic with many fields
    {
        if (tfc.size() < RT_RTTFC_MIN_TFC_FIELDS)
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }

        return ProcessRTTFC(fdKey, tfc, nBuf);
    }
    // Buffered traffic comes with few fields and is typically processed here as AITFC format
    else if (tfc[RT_AITFC_REC_TYPE] == RT_TRAFFIC_AITFC || nBuf) {
        if (tfc.size() < RT_AITFC_NUM_FIELDS_MIN)
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }

        return ProcessAITFC(fdKey, tfc, nBuf);
    }
    else if (tfc[RT_AITFC_REC_TYPE] == RT_TRAFFIC_XTRAFFICPSX) {
        if (tfc.size() < RT_XTRAFFICPSX_NUM_FIELDS)
        { LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic); return false; }

        return ProcessAITFC(fdKey, tfc, false);
    }
    else {
        // other format than AITFC or XTRAFFICPSX
        LOG_MSG(logWARN, ERR_RT_DISCARDED_MSG, traffic);
        return false;
    }
}
    

/// Helper to return first element larger than zero from the data array
double firstPositive (const std::vector<std::string>& tfc,
                      std::initializer_list<size_t> li)
{
    for (size_t i: li) {
        const double d = std::stod(tfc[i]);
        if (d > 0.0)
            return d;
    }
    return 0.0;
}


// Process a RTTFC  type message
/// @details RTTraffic format (port 49005), introduced in v9 of RealTraffic
///            RTTFC,hexid, lat, lon, baro_alt, baro_rate, gnd, track, gsp,
///            cs_icao, ac_type, ac_tailno, from_iata, to_iata, timestamp,
///            source, cs_iata, msg_type, alt_geom, IAS, TAS, Mach, track_rate,
///            roll, mag_heading, true_heading, geom_rate, emergency, category,
///            nav_qnh, nav_altitude_mcp, nav_altitude_fms, nav_heading,
///            nav_modes, seen, rssi, winddir, windspd, OAT, TAT,
///            isICAOhex,augmentation_status,authentication
/// @details Example:
///            RTTFC,11234042,-33.9107,152.9902,26400,1248,0,90.12,490.00,
///            AAL72,B789, N835AN,SYD,LAX,1645144774.2,X2,AA72,adsb_icao,
///            27575,320,474,0.780, 0.0,0.0,78.93,92.27,1280,none,A5,1012.8,
///            35008,-1,71.02, autopilot|vnav|lnav|tcas,0.0,-21.9,223,24,
///            -30,0,1,170124
bool RealTrafficConnection::ProcessRTTFC (LTFlightData::FDKeyTy& fdKey,
                                          const std::vector<std::string>& tfc,
                                          int nBuffer)
{
    // *** position time ***
    double posTime = std::stod(tfc[RT_RTTFC_TIMESTAMP]);
    AdjustTimestamp(posTime, nBuffer);

    // *** Process received data ***

    // *** position ***
    // RealTraffic always provides data 100km around current position
    // Let's check if the data falls into our configured range and discard it if not
    positionTy pos (std::stod(tfc[RT_RTTFC_LAT]),
                    std::stod(tfc[RT_RTTFC_LON]),
                    0,              // we take care of altitude later
                    posTime);
    
    // position is rather important, we check for validity
    // (we do allow alt=NAN if on ground)
    if ( !pos.isNormal(true) ) {
        LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
        return false;
    }
    
    // RealTraffic always sends data of 100km or so around current pos
    // Filter data that the user didn't want based on settings
    const positionTy viewPos = dataRefs.GetViewPos();
    const double dist = pos.dist(viewPos);
    if (dist > dataRefs.GetFdStdDistance_m() )
        return true;            // silently
    
    try {
        // from here on access to fdMap guarded by a mutex
        // until FD object is inserted and updated
        std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
        
        // There's a flag telling us if a key is an ICAO code
        if (tfc[RT_RTTFC_ISICAOHEX] != "1")
            fdKey.eKeyType = LTFlightData::KEY_RT;
        
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
        
        // -- fill static data --
        LTFlightData::FDStaticData stat;
        
        stat.acTypeIcao     = tfc[RT_RTTFC_AC_TYPE];
        stat.call           = tfc[RT_RTTFC_CS_ICAO];
        stat.reg            = tfc[RT_RTTFC_AC_TAILNO];
        stat.setOrigDest(tfc[RT_RTTFC_FROM_IATA], tfc[RT_RTTFC_TO_IATA]);

        const std::string& sCat = tfc[RT_RTTFC_CATEGORY];
        stat.catDescr       = GetADSBEmitterCat(sCat);
        
        // Static objects are all equally marked with a/c type TWR
        if ((sCat == "C3" || sCat == "C4" || sCat == "C5") ||
            (stat.reg == STATIC_OBJECT_TYPE && stat.acTypeIcao == STATIC_OBJECT_TYPE))
        {
            stat.reg = stat.acTypeIcao = STATIC_OBJECT_TYPE;
        }

        // -- dynamic data --
        LTFlightData::FDDynamicData dyn;
        
        // non-positional dynamic data
        dyn.gnd         = tfc[RT_RTTFC_AIRBORNE] == "0";
        dyn.heading     = firstPositive(tfc, {RT_RTTFC_TRUE_HEADING, RT_RTTFC_TRACK, RT_RTTFC_MAG_HEADING});
        dyn.spd         = std::stod(tfc[RT_RTTFC_GSP]);
        dyn.vsi         = firstPositive(tfc, {RT_RTTFC_GEOM_RATE, RT_RTTFC_BARO_RATE});
        dyn.ts          = posTime;
        dyn.pChannel    = this;
        
        // Altitude
        if (dyn.gnd)
            pos.alt_m() = NAN;          // ground altitude to be determined in scenery
        else {
            // Since RealTraffic v10, it delivers "corrected" altitude in the Baremtric Alt field, we prefer this value and don't need to apply pressure correction
            double alt = std::stod(tfc[RT_RTTFC_ALT_BARO]);
            if (alt > 0.0)
                pos.SetAltFt(alt);
            // Otherwise we try using geometric altitude
            else
                if ((alt = std::stod(tfc[RT_RTTFC_ALT_GEOM])) > 0.0)
                    pos.SetAltFt(alt);
        }
        // don't forget gnd-flag in position
        pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;

        // Vehicle?
        if (stat.acTypeIcao == "GRND" || stat.acTypeIcao == "GND")  // some vehicles come with type 'GRND'...
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        else if (sCat.length() == 2 && sCat[0] == 'C' && (sCat[1] == '1' || sCat[1] == '2'))
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        else if (sCat.empty() && dyn.gnd && stat.acTypeIcao.empty() && stat.reg.empty())
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();

        // add the static data
        fd.UpdateData(std::move(stat), dist);

        // add the dynamic data
        fd.AddDynData(dyn, 0, 0, &pos);

    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        return false;
    }

    // success
    return true;
}


// Process a AITFC or XTRAFFICPSX type message
/// @details AITraffic format (port 49003), which has more fields:
///            AITFC,531917901,40.9145,-73.7625,1975,64,1,218,140,DAL9936,BCS1,N101DU,BOS,LGA
///          and the Foreflight format (broadcasted on port 49002):
///            XTRAFFICPSX,531917901,40.9145,-73.7625,1975,64,1,218,140,DAL9936(BCS1)
///
bool RealTrafficConnection::ProcessAITFC (LTFlightData::FDKeyTy& fdKey,
                                          const std::vector<std::string>& tfc,
                                          int nBuffer)
{
    // *** position time ***
    // There are 2 possibilities:
    // 1. As of v7.0.55 RealTraffic can send a timestamp (when configured
    //    to use the "LiveTraffic" as Simulator in use, I assume)
    // 2. Before that or with other settings there is no timestamp
    //    so we assume 'now'
    
    double posTime;
    // Timestamp included?
    if (tfc.size() > RT_AITFC_TIMESTAMP)
    {
        // use that delivered timestamp and (potentially) adjust it if it is in the past
        posTime = std::stod(tfc[RT_AITFC_TIMESTAMP]);
        AdjustTimestamp(posTime, nBuffer);
    }
    else
    {
        // No Timestamp provided: assume 'now'
        using namespace std::chrono;
        posTime =
        // system time in microseconds
        double(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count())
        // divided by 1000000 to create seconds with fractionals
        / 1000000.0;
    }

    // *** Process received data ***

    // *** position ***
    // RealTraffic always provides data 100km around current position
    // Let's check if the data falls into our configured range and discard it if not
    positionTy pos (std::stod(tfc[RT_AITFC_LAT]),
                    std::stod(tfc[RT_AITFC_LON]),
                    0,              // we take care of altitude later
                    posTime);
    
    // position is rather important, we check for validity
    // (we do allow alt=NAN if on ground)
    if ( !pos.isNormal(true) ) {
        LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
        return false;
    }
    
    // RealTraffic always sends data of 100km or so around current pos
    // Filter data that the user didn't want based on settings
    const positionTy viewPos = dataRefs.GetViewPos();
    const double dist = pos.dist(viewPos);
    if (dist > dataRefs.GetFdStdDistance_m() )
        return true;            // silently
    
    try {
        // from here on access to fdMap guarded by a mutex
        // until FD object is inserted and updated
        std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
        
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
        
        // -- fill static data --
        LTFlightData::FDStaticData stat;
        
        stat.acTypeIcao     = tfc[RT_AITFC_TYPE];
        stat.call           = tfc[RT_AITFC_CS];
        
        if (tfc.size() > RT_AITFC_TO) {
            stat.reg            = tfc[RT_AITFC_TAIL];
            stat.setOrigDest(tfc[RT_AITFC_FROM], tfc[RT_AITFC_TO]);
        }
        
        // For static objects we also set `reg` to TWR for consistency
        if (stat.acTypeIcao == STATIC_OBJECT_TYPE) {
            stat.reg = STATIC_OBJECT_TYPE;
            stat.catDescr = GetADSBEmitterCat("C3");
        }

        // -- dynamic data --
        LTFlightData::FDDynamicData dyn;
        
        // non-positional dynamic data
        dyn.gnd =               tfc[RT_AITFC_AIRBORNE] == "0";
        dyn.spd =               std::stoi(tfc[RT_AITFC_SPD]);
        dyn.heading =           std::stoi(tfc[RT_AITFC_HDG]);
        dyn.vsi =               std::stoi(tfc[RT_AITFC_VS]);
        dyn.ts =                posTime;
        dyn.pChannel =          this;
        
        // *** gnd detection hack ***
        // RealTraffic keeps the airborne flag always 1,
        // even with traffic which definitely sits on the gnd.
        // Also, reported altitude never seems to become negative,
        // though this would be required in high pressure weather
        // at airports roughly at sea level.
        // And altitude is rounded to 250ft which means that close
        // to the ground it could be rounded down to 0!
        //
        // If "0" is reported we need to assume "on gnd" and bypass
        // the pressure correction.
        // If at the same time VSI is reported significantly (> +/- 100)
        // then we assume plane is already/still flying, but as we
        // don't know exact altitude we just skip this record.
        if (tfc[RT_AITFC_ALT]         == "0") {
            // skip this dynamic record in case VSI is too large
            if (std::abs(dyn.vsi) > RT_VSI_AIRBORNE)
                return true;
            // have proper gnd altitude calculated
            pos.alt_m() = NAN;
            dyn.gnd = true;
        } else {
            // probably not on gnd, so take care of altitude
            // altitude comes without local pressure applied
            pos.SetAltFt(BaroAltToGeoAlt_ft(std::stod(tfc[RT_AITFC_ALT]), dataRefs.GetPressureHPA()));
        }
        
        // don't forget gnd-flag in position
        pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;

        // -- Ground vehicle identification --
        // is really difficult with RealTraffic as we only have very few information:
        if (stat.acTypeIcao.empty() &&      // don't know a/c type yet
            dyn.gnd &&                      // on the ground
            dyn.spd < 50.0 &&               // reasonable speed
            stat.reg.empty() &&             // no tail number
            stat.dest().empty())            // no destination airport
        {
            // we assume ground vehicle
            stat.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
        }

        // add the static data
        fd.UpdateData(std::move(stat), dist);

        // add the dynamic data
        fd.AddDynData(dyn, 0, 0, &pos);

    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        return false;
    }

    // success
    return true;
}


// Determine timestamp adjustment necessary in case of historic data
void RealTrafficConnection::AdjustTimestamp (double& ts, int nBuffer)
{
    // If this is a buffered request then it is a timestamp further in the past, adjust for that
    if (nBuffer) {
        // buffer number starts with 1 for the oldest buffer all the way up to the GetNumTrafficBuffers
        // for the last buffer before current time
        nBuffer = (GetNumTrafficBuffers() - nBuffer + 1) * RT_BUFFER_PERIOD;
    }
    
    // the assumed 'now' is simTime + buffering period
    // minus the buffering time if we are buffering
    const double now = dataRefs.GetSimTime() + dataRefs.GetFdBufPeriod();
    
    // *** Keep the rolling list of timestamps diffs, max length: 11 ***
    dequeTS.push_back(now - (ts + double(nBuffer)));
    while (dequeTS.size() > 11)
        dequeTS.pop_front();
    
    // *** Determine Median of timestamp differences ***
    double medianTs;
    if (dequeTS.size() >= 3)
    {
        // To find the (lower) Median while at the same time preserve the deque in its order,
        // we do a partial sort into another array
        std::vector<double> v((dequeTS.size()+1)/2);
        std::partial_sort_copy(dequeTS.cbegin(), dequeTS.cend(),
                               v.begin(), v.end());
        medianTs = v.back();
    }
    // with less than 3 sample we just pick the last
    else {
        medianTs = dequeTS.back();
    }
    
    // *** Need to change the timestamp adjustment?
    // Priority has to change back to zero if we are within the buffering period
    const int bufPeriod = dataRefs.GetFdBufPeriod();
    if (medianTs < bufPeriod) {
        if (tsAdjust > 0.0) {
            tsAdjust = 0.0;
            SHOW_MSG(logINFO, INFO_RT_REAL_TIME);
        }
    }
    // ...if that median is more than half the buffering period away from current adjustment
    else if (std::abs(medianTs - tsAdjust) > bufPeriod/2)
    {
        // new adjustment is that median, rounded to 10 seconds
        tsAdjust = std::round(medianTs / 10.0) * 10.0;
        SHOW_MSG(logINFO, INFO_RT_ADJUST_TS, GetAdjustTSText().c_str());
    }
    
    // Remember for other calculations, e.g. weather, here in minutes
    curr.tOff = std::lround(tsAdjust / 60.0);

    // Adjust the passed-in timestamp by the determined adjustment
    ts += tsAdjust;
}


// Return a string describing the current timestamp adjustment
std::string RealTrafficConnection::GetAdjustTSText () const
{
    char timeTxt[100];
    if (tsAdjust < 300.0)               // less than 5 minutes: tell seconds
        snprintf(timeTxt, sizeof(timeTxt), "%.0fs ago", tsAdjust);
    else if (tsAdjust < 86400.0)        // less than 1 day
        snprintf(timeTxt, sizeof(timeTxt), "%ld:%02ldh ago",
                 long(tsAdjust/3600.0),         // hours
                 long(tsAdjust/60.0) % 60);     // minutes
    else {
        // More than a day ago, compute full UTC time the data is from
        struct tm tm;
        time_t t;
        time(&t);
        t -= time_t(tsAdjust);
        gmtime_s(&tm, &t);
        
        snprintf(timeTxt, sizeof(timeTxt), "%ldd %ld:%02ldh ago (%4d-%02d-%02d %02d:%02d UTC)",
                 long(tsAdjust/86400),          // days
                 long(tsAdjust/3600.0) % 24,    // hours
                 long(tsAdjust/60.0) % 60,      // minutes
                 // UTC timestamp
                 tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min);
    }
    return std::string(timeTxt);
}


// Is it a duplicate? (if not datagram is copied into a map)
bool RealTrafficConnection::IsDatagramDuplicate (unsigned long numId,
                                                 const char* datagram)
{
    // access is guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(rtMutex);
    
    // is the plane, identified by numId unkown?
    auto it = mapDatagrams.find(numId);
    if (it == mapDatagrams.end()) {
        // add the datagram the first time for this plane
        mapDatagrams.emplace(std::piecewise_construct,
                             std::forward_as_tuple(numId),
                             std::forward_as_tuple(dataRefs.GetSimTime(),datagram));
        // no duplicate
        return false;
    }
    
    // plane known...is the data identical? -> duplicate
    RTUDPDatagramTy& d = it->second;
    if (d.datagram == datagram)
        return true;
        
    // plane known, but data different, replace data in map
    d.posTime = dataRefs.GetSimTime();
    d.datagram = datagram;
    
    // no duplicate
    return false;
}

// remove outdated entries from mapDatagrams
void RealTrafficConnection::CleanupMapDatagrams()
{
    // access is guarded by a lock
    std::lock_guard<std::recursive_mutex> lock(rtMutex);

    // cut-off time is current sim time minus outdated interval,
    // or in other words: Remove all data that had no updates for
    // the outdated period, planes will vanish soon anyway
    const double cutOff = dataRefs.GetSimTime() - dataRefs.GetAcOutdatedIntvl();

    for (auto it = mapDatagrams.begin(); it != mapDatagrams.end(); ) {
        if (it->second.posTime < cutOff)
            it = mapDatagrams.erase(it);
        else
            ++it;
    }
}

// Process UDP weather JSON from RT Application
bool RealTrafficConnection::ProcessRecvedWeatherData (const char* weather)
{
    // Silently ignore too frequent weather data
    if (std::chrono::steady_clock::now() < rtWx.next) {
        // LOG_MSG(logDEBUG, "RealTraffic: Ignoring too frequent weather updates");
        return false;
    }
    
    // sanity check: not empty
    if (!weather || !weather[0])
        return false;

    // Raw data logging
    DebugLogRaw(weather, HTTP_FLAG_UDP);

    // Parse JSON, unique_ptr ensures it is freed before leaving the function
    JSONRootPtr pRoot (weather);
    const JSON_Object* pMain = json_object(pRoot.get());
    if (!pMain) {
        LOG_MSG(logERR, "RealTraffic: Could not parse weather JSON: %s", weather);
        IncErrCnt();
        return false;
    }

    // The weather JSON can have different formats depending on how RT App sends it
    // Check for 'data' object (similar to direct API response) or use root directly
    const JSON_Object* pData = json_object_get_object(pMain, "data");
    return PreProcessWeather (pData ? pData : pMain);
}

/// @file       LTOpenGlider.cpp
/// @brief      Open Glider Network: Requests and processes live tracking data
/// @see        http://wiki.glidernet.org/
/// @see        https://github.com/glidernet/ogn-live#backend
/// @see        http://live.glidernet.org/
/// @details    Defines OpenGliderConnection:\n
///             - Direct TCP connection to aprs.glidernet.org:14580 (preferred)
///               - connects to the server
///               - sends a dummy login for read-only access
///               - listens to incoming tracking data
///             - Request/Reply Interface (alternatively)
///               - Provides a proper REST-conform URL\n
///               - Interprets the response and passes the tracking data on to LTFlightData.\n
/// @details    Also downloads and performs searches in the aircraft list
/// @see        http://ddb.glidernet.org/download/
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

#if APL == 1 || LIN == 1
#include <unistd.h>         // for self-pipe functionality
#include <fcntl.h>
#endif

/// Reference to the global map of flight data
extern mapLTFlightDataTy mapFd;

//
// MARK: Open Glider Network
//

// log messages
#define OGN_AC_LIST_DOWNLOAD        "Downloading a/c list from ddb.glidernet.org"
#define ERR_OGN_XLM_END_MISSING     "OGN response malformed, end of XML element missing: %s"
#define ERR_OGN_WRONG_NUM_FIELDS    "OGN response contains wrong number of fields: %s"

#define ERR_OGN_ACL_FILE_OPEN_W     "Could not open '%s' for writing: %s"
#define ERR_OGN_ACL_FILE_OPEN_R     "Could not open '%s' for reading: %s"
#define INFO_OGN_AC_LIST_DOWNLOADED "Aircraft list downloaded from ddb.glidernet.org"

#define ERR_OGN_TCP_CONNECTED       "Connected to OGN Server %s"
#define ERR_OGN_TCP_ERROR           "OGN Server returned error: %s"
#define WARN_OGN_NOT_MATCHED        "A message could be tracking data, but didn't match: %s"

// Request Reply
constexpr const char* OGN_MARKER_BEGIN = "<m a=\""; ///< beginning of a marker in the XML response
constexpr const char* OGN_MARKER_END   = "\"/>";    ///< end of a marker in the XML response
constexpr size_t OGN_MARKER_BEGIN_LEN = 6;          ///< strlen(OGN_MARKER_BEGIN)

// Direct TCP connection
constexpr const char* OGN_TCP_SERVER    = "aprs.glidernet.org";
constexpr int         OGN_TCP_PORT      = 14580;
constexpr size_t      OGN_TCP_BUF_SIZE  = 1024;
constexpr const char* OGN_TCP_LOGIN     = "user LiveTrffc pass -1 vers " LIVE_TRAFFIC " %.2f filter r/%.3f/%.3f/%u -p/oimqstunw\r\n";
constexpr const char* OGN_TCP_LOGIN_GOOD= "# logresp LiveTrffc unverified, server ";

// Constructor
OpenGliderConnection::OpenGliderConnection () :
LTChannel(DR_CHANNEL_OPEN_GLIDER_NET),
LTOnlineChannel(),
LTFlightDataChannel()
{
    // purely informational
    urlName  = OPGLIDER_CHECK_NAME;
    urlLink  = OPGLIDER_CHECK_URL;
    urlPopup = OPGLIDER_CHECK_POPUP;
}

// Destructor closes the a/c list file
OpenGliderConnection::~OpenGliderConnection ()
{
    Cleanup();
}

// All the cleanup we usually need
void OpenGliderConnection::Cleanup ()
{
    TCPClose();
    if (ifAcList.is_open())
        ifAcList.close();
}

// put together the URL to fetch based on current view position
std::string OpenGliderConnection::GetURL (const positionTy& pos)
{
    // We only return a URL if we are to use the request/reply procedure
    if (DataRefs::GetCfgInt(DR_CFG_OGN_USE_REQUREPL)) {
        boundingBoxTy box (pos, dataRefs.GetFdStdDistance_m());
        char url[128] = "";
        snprintf(url, sizeof(url),
                 OPGLIDER_URL,
                 box.nw.lat(),              // lamax
                 box.se.lat(),              // lamin
                 box.se.lon(),              // lomax
                 box.nw.lon());             // lomin
        return std::string(url);
    } else {
        // otherwise we are to use the direct TCP connection
        TCPStartUpdate(pos, dataRefs.GetFdStdDistance_km());
        return std::string();
    }
}

/// @details Returned data is XML style looking like this:
///          @code{.xml}
///             <markers>
///             <style class="darkreader darkreader--fallback">html, body, body :not(iframe) { background-color: #181a1b !important; border-color: #776e62 !important; color: #e8e6e3 !important; }</style>
///             <m a="50.882481,11.649430,DRF,D-HDSO,416,20:45:52,140,293,169,-0.8,1,EDBJ,DD0C07,db9d47d1"/>
///             <m a="53.550369,10.158180,_0e,a07f1e0e,108,20:47:57,15,0,0,-0.3,1,EDDHEast,0,a07f1e0e"/>
///             <m a="49.052521,9.494550,DRF,D-HDSG,1013,20:47:51,21,125,230,0.4,3,Voelklesh,3DDC66,c2b8cf7"/>
///             </markers>
///          @endcode
///          We are not doing full XML parsing, but just search for <m a="
///          and process everything till we find "/>
bool OpenGliderConnection::ProcessFetchedData (mapLTFlightDataTy& fdMap)
{
    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) return true;
    
    // We need current Zulu time to interpret the timestamp in the data
    const std::time_t tNow = std::time(nullptr);
    
    // Search for all markers in the response
    for (const char* sPos = strstr(netData, OGN_MARKER_BEGIN);
         sPos != nullptr;
         sPos = strstr(sPos, OGN_MARKER_BEGIN))
    {
        // increase sPos to actual begining of definition
        sPos += OGN_MARKER_BEGIN_LEN;
        
        // find the end of the marker definition
        const char* sEnd = strstr(sPos, OGN_MARKER_END);
        if (!sEnd) {
            LOG_MSG(logERR, ERR_OGN_XLM_END_MISSING, sPos);
            break;
        }
        
        // then this is the marker definition to work on
        const std::string sMarker (sPos, sEnd-sPos);
        std::vector<std::string> tok = str_tokenize(sMarker, ",", false);
        if (tok.size() != GNF_COUNT) {
            LOG_MSG(logERR, ERR_OGN_WRONG_NUM_FIELDS, sMarker.c_str());
            break;
        }
        
        // We sliently skip all static objects
        if (std::stoi(tok[GNF_FLARM_ACFT_TYPE]) == FAT_STATIC_OBJ)
            continue;
        
        // We also skip records, which are outdated by the time they arrive
        const long age_s = std::abs(std::stol(tok[GNF_AGE_S]));
        if (age_s >= dataRefs.GetAcOutdatedIntvl())
            continue;

        // They key: if no 6-digit FLARM device id is available then we use the
        //           OGN id, which is assigned daily, but good enough to track a flight
        LTFlightData::FDKeyTy fdKey;
        LTFlightData::FDStaticData stat;
        if (tok[GNF_FLARM_DEVICE_ID].size() != 6) {
            // use the OGN Registration
            fdKey.SetKey(LTFlightData::KEY_OGN, tok[GNF_OGN_REG_ID]);
        } else {
            // otherwise we look up the 6-digit key in the a/c list to learn more details about the type
            LTFlightData::FDKeyType keyType = LookupAcList(std::stoul(tok[GNF_FLARM_DEVICE_ID], nullptr, 16), stat);
            if (keyType != LTFlightData::KEY_UNKNOWN)       // found in the a/c list!
                // also look up a good ICAO a/c type by the model text
                stat.acTypeIcao = ModelIcaoType::getIcaoType(str_toupper_c(stat.mdl));
            else
                // not found in a/c list: Assume the key is FLARM
                keyType = LTFlightData::KEY_FLARM;
            fdKey.SetKey(keyType, tok[GNF_FLARM_DEVICE_ID]);
        }
        
        // key not matching a/c filter? -> skip it
        if ((!acFilter.empty() && (fdKey != acFilter)) )
            continue;
        
        try {
            // from here on access to fdMap guarded by a mutex
            // until FD object is inserted and updated
            std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
            
            // get the fd object from the map
            // this fetches an existing or, if not existing, creates a new one
            LTFlightData& fd = fdMap[fdKey];
            
            // also get the data access lock once and for all
            // so following fetch/update calls only make quick recursive calls
            std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
            
            // completely new? fill key fields and define static data
            // (for OGN we only define static data initially,
            //  it has no changing elements, and ICAO a/c type derivation
            //  has a random element (if more than one ICAO type is defined))
            if ( fd.empty() ) {
                fd.SetKey(fdKey);
            
                // Call Sign: We use the CN, don't have a proper call sign,
                // makes it easier to match a/c to what live.glidernet.org shows
                stat.call = tok[GNF_CN];
                // We assume that GNF_REG holds a (more or less)
                // proper reg in case it is not just the generated OGN_REG_ID
                if (stat.reg.empty()) {
                    if (tok[GNF_REG] != tok[GNF_OGN_REG_ID])
                        stat.reg = tok[GNF_REG];
                    else
                        // otherwise (again) the CN
                        stat.reg = tok[GNF_CN];
                }
                // Aircraft type converted from Flarm AcftType
                const FlarmAircraftTy acTy = (FlarmAircraftTy)clamp<int>(std::stoi(tok[GNF_FLARM_ACFT_TYPE]),
                                                                         FAT_UNKNOWN, FAT_STATIC_OBJ);
                stat.catDescr   = OGNGetAcTypeName(acTy);
                
                // If we still have no accurate ICAO type then we need to fall back to some configured defaults
                if (stat.acTypeIcao.empty()) {
                    stat.acTypeIcao = OGNGetIcaoAcType(acTy);
                    stat.mdl        = stat.catDescr;
                }

                fd.UpdateData(std::move(stat));
            }
            
            // dynamic data
            {   // unconditional...block is only for limiting local variables
                LTFlightData::FDDynamicData dyn;
                
                // position time: zulu time is given in the data, but it is even easier
                //                when using the age, which is always given relative to the query time
                dyn.ts = double(tNow - age_s);
                
                // non-positional dynamic data
                dyn.gnd =               false;      // there is no GND indicator in OGN data
                dyn.heading =           std::stod(tok[GNF_TRK]);
                dyn.spd =               std::stod(tok[GNF_SPEED_KM_H]) * NM_per_KM;
                dyn.vsi =               std::stod(tok[GNF_VERT_M_S]);
                dyn.pChannel =          this;
                
                // position
                positionTy pos (std::stod(tok[GNF_LAT]),
                                std::stod(tok[GNF_LON]),
                                std::stod(tok[GNF_ALT_M]),
// no weather correction, OGN delivers geo altitude?   dataRefs.WeatherAltCorr_m(std::stod(tok[GNF_ALT_M])),
                                dyn.ts,
                                dyn.heading);
                pos.f.onGrnd = GND_UNKNOWN;         // there is no GND indicator in OGN data
                
                // position is rather important, we check for validity
                // (we do allow alt=NAN if on ground)
                if ( pos.isNormal(true) )
                    fd.AddDynData(dyn, 0, 0, &pos);
                else
                    LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
            }
        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
        }
    }
    
    // success
    return true;
}


/// Main function for TCP connection, expected to be started in a thread
void OpenGliderConnection::TCPMain (const positionTy& pos, unsigned dist_km)
{
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_OGN_TCP");
    
    try {
        // open a TCP connection to glidernet.org
        tcpRcvr.Connect(OGN_TCP_SERVER, OGN_TCP_PORT, OGN_TCP_BUF_SIZE);
        int maxSock = tcpRcvr.getSocket() + 1;
#if APL == 1 || LIN == 1
        // the self-pipe to shut down the TCP socket gracefully
        if (pipe(tcpPipe) < 0)
            throw NetRuntimeError("Couldn't create pipe");
        fcntl(tcpPipe[0], F_SETFL, O_NONBLOCK);
        maxSock = std::max(maxSock, tcpPipe[0]+1);
#endif
        
        // Login
        if (!TCPDoLogin(pos, dist_km)) {
            SetValid(false, true);
            bStopTcp = true;
        }
        
        // *** Main Loop ***
        while (!bStopTcp && tcpRcvr.isOpen())
        {
            // wait for a UDP datagram on either socket (traffic, weather)
            fd_set sRead;
            FD_ZERO(&sRead);
            FD_SET(tcpRcvr.getSocket(), &sRead);     // check our socket
#if APL == 1 || LIN == 1
            FD_SET(tcpPipe[0], &sRead);
#endif
            int retval = select(maxSock, &sRead, NULL, NULL, NULL);
            
            // short-cut if we are to shut down (return from 'select' due to closed socket)
            if (bStopTcp)
                break;

            // select call failed???
            if(retval == -1)
                throw NetRuntimeError("'select' failed");

            // select successful - traffic data
            if (retval > 0 && FD_ISSET(tcpRcvr.getSocket(), &sRead))
            {
                // read UDP datagram
                long rcvdBytes = tcpRcvr.recv();
                
                // received something?
                if (rcvdBytes > 0)
                {
                    // have it processed
                    if (!TCPProcessData(tcpRcvr.getBuf())) {
                        SetValid(false, true);
                        break;
                    }
                }
                else
                    retval = -1;
            }
            
            // short-cut if we are to shut down
            if (bStopTcp)
                break;
            
            // handling of errors, both from select and from recv
            if (retval < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                // not just a normal timeout?
                char sErr[SERR_LEN];
                strerror_s(sErr, sizeof(sErr), errno);
                LOG_MSG(logERR, ERR_UDP_RCVR_RCVR, ChName(),
                        sErr);
                // increase error count...bail out if too bad
                if (!IncErrCnt()) {
                    SetValid(false, true);
                    break;
                }
            }
        }
        
    }
    catch (std::runtime_error& e) {
        LOG_MSG(logERR, ERR_TCP_LISTENACCEPT, ChName(),
                OGN_TCP_SERVER, std::to_string(OGN_TCP_PORT).c_str(),
                e.what());
        // invalidate the channel
        SetValid(false, true);
    }
    
    // make sure the socket is closed
    tcpRcvr.Close();
}

// Send the login
bool OpenGliderConnection::TCPDoLogin (const positionTy& pos, unsigned dist_km)
{
    std::string logTxt;
    
    // Prepare login string like "user LiveTrffc pass -1 vers LiveTraffic 2.20 filter r/43.3/-80.2/50 -p/oimqstunw"
    char sLogin[120];
    snprintf(sLogin, sizeof(sLogin), OGN_TCP_LOGIN, VERSION_NR,
             pos.lat(), pos.lon(), dist_km);
    return tcpRcvr.send(sLogin);
}

/// Process received data
bool OpenGliderConnection::TCPProcessData (const char* buffer)
{
    // save the data to our processing buffer
    DebugLogRaw(buffer);
    tcpData += buffer;
    if (tcpData.empty())            // weird...but not an error if there's nothing to process
        return true;
    
    // process the input line by line, expected a line to be ended by \r\n
    // (If CR/LF is yet missing then the received data is yet incomplete and will be completed with the next received data)
    for (size_t lnEnd = tcpData.find("\r\n");
         lnEnd != std::string::npos;
         tcpData.erase(0,lnEnd+2), lnEnd = tcpData.find("\r\n"))
    {
        if (!TCPProcessLine(tcpData.substr(0,lnEnd)))
            return false;
    }
    return true;
}

/// @brief Process one line of received data
/// @see https://github.com/svoop/ogn_client-ruby/wiki/SenderBeacon
bool OpenGliderConnection::TCPProcessLine (const std::string& ln)
{
    // Sanity check
    if (ln.empty()) return true;
    
    // Special processing for lines beginning with a hash mark
    if (ln[0] == '#')
    {
        // Test for login error
        if (ln.find("Invalid") != std::string::npos) {
            LOG_MSG(logERR, ERR_OGN_TCP_ERROR, ln.c_str());
            return false;
        }
        
        // Test for successful login
        if (ln.find(OGN_TCP_LOGIN_GOOD) != std::string::npos)
            LOG_MSG(logINFO, ERR_OGN_TCP_CONNECTED, str_last_word(ln).c_str());
        
        // Otherwise ignore all lines starting with '#' as comments
        return true;
    }
    
    // Try to match the line with an expected pattern
    static std::regex re (":/(\\d\\d)(\\d\\d)(\\d\\d)h"     // timestamp, 3 matches: h, min, sec
                          "(\\d\\d)(\\d\\d.\\d\\d)(N|S)"    // latitude, 3 matches: degree, minutes incl. decimals, N or S
                          "(?:/|\\\\)"                      // display symbol, not stored
                          "(\\d\\d\\d)(\\d\\d.\\d\\d)(E|W)" // longitude, 3 matches: degree, minutes incl. decimals, E or W
                          "."                               // display symbol
                          "(\\d\\d\\d)/(\\d\\d\\d)"         // heading/speed, 2 matches (optional, "000/000" indicates no data)
                          "/A=(\\d{6}) "                    // altitude in feet, 1 match
                          "!W(\\d)(\\d)! "                  // position precision enhancement, 2 matches: latitude, longitude
                          "id([0-9A-Z]{2})([0-9A-Z]{6,8}) " // sender details and address, 2 matches
                          "([-+]\\d\\d\\d)fpm "            // vertical speed, 1 match
                          );
    // Indexes for the above matches
    enum mIdx {
        M_ALL = 0,
        M_TS_H, M_TS_MIN, M_TS_S,
        M_LAT_DEG, M_LAT_MIN, M_LAT_NS,
        M_LON_DEG, M_LON_MIN, M_LON_EW,
        M_HEAD, M_SPEED,
        M_ALT,
        M_LAT_PREC, M_LON_PREC,
        M_SEND_DETAILS, M_SEND_ID,
        M_VSI
    };
    std::smatch m;
    std::regex_search(ln, m, re);
    
    // We expect 17 matches. Size is one more because element 0 is the complete matched string:
    if (m.size() != 18) {
        // didn't match. But if we think this _could_ be a valid message then we should warn, maybe there's still a flaw in the regex above
        if (ln.find("! id") != std::string::npos) {
            LOG_MSG(logWARN, WARN_OGN_NOT_MATCHED, ln.c_str());
        }
        // but otherwise no issue...there are some message in the stream that we just don't need
        return true;
    }
    
    // Matches:        0                                                        1    2    3    4    5       6   7     8       9   10    11    12       13  14  15   16       17
    // :/215957h5000.42N\00839.32En000/000/A=000502 !W38! id3ED0075F -019fpm  | 21 | 59 | 57 | 50 | 00.42 | N | 008 | 39.32 | E | 000 | 000 | 000502 | 3 | 8 | 3E | D0075F | -019 |
    
    // We silently skip all static objects and those who do not want to be tracked
    APRSSenderDetailsTy senderDetails;
    senderDetails.u = (uint8_t)std::stoul(m.str(M_SEND_DETAILS), nullptr, 16);
    if (senderDetails.b.bNoTracking || senderDetails.b.bStealthMode ||
        senderDetails.b.acTy == FAT_STATIC_OBJ)
        return true;
    
    // Timestamp - skip too old records
    time_t ts = mktime_utc(std::stoi(m.str(M_TS_H)),
                           std::stoi(m.str(M_TS_MIN)),
                           std::stoi(m.str(M_TS_S)));
    if (time(NULL) - ts > dataRefs.GetAcOutdatedIntvl())
        return true;
    
    // They key: if no 6-digit FLARM device id is available then we use the
    //           OGN id, which is assigned daily, but good enough to track a flight
    LTFlightData::FDKeyTy fdKey;
    LTFlightData::FDStaticData stat;
    if (m.str(M_SEND_ID).size() != 6) {
        // use the OGN Registration
        fdKey.SetKey(LTFlightData::KEY_OGN, m.str(M_SEND_ID));
    } else {
        // otherwise we look up the 6-digit key in the a/c list to learn more details about the a/c
        LTFlightData::FDKeyType keyType = LookupAcList(std::stoul(m.str(M_SEND_ID), nullptr, 16), stat);
        if (keyType != LTFlightData::KEY_UNKNOWN)       // found in the a/c list!
            // also look up a good ICAO a/c type by the model text
            stat.acTypeIcao = ModelIcaoType::getIcaoType(str_toupper_c(stat.mdl));
        else {
            // use the type specified in the message
            keyType =
            senderDetails.b.addrTy == APRS_ADDR_ICAO  ? LTFlightData::KEY_ICAO :
            senderDetails.b.addrTy == APRS_ADDR_FLARM ? LTFlightData::KEY_FLARM : LTFlightData::KEY_OGN;
        }
        fdKey.SetKey(keyType, m.str(M_SEND_ID));
    }
    
    // key not matching a/c filter? -> skip it
    std::string s ( dataRefs.GetDebugAcFilter() );
    if ((!s.empty() && (fdKey != s)) )
        return true;
    
    try {
        // from here on access to fdMap guarded by a mutex
        // until FD object is inserted and updated
        std::lock_guard<std::mutex> mapFdLock (mapFdMutex);
        
        // get the fd object from the map
        // this fetches an existing or, if not existing, creates a new one
        LTFlightData& fd = mapFd[fdKey];
        
        // also get the data access lock once and for all
        // so following fetch/update calls only make quick recursive calls
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        
        // completely new? fill key fields and define static data
        // (for OGN we only define static data initially,
        //  it has no changing elements, and ICAO a/c type derivation
        //  has a random element (if more than one ICAO type is defined))
        if ( fd.empty() ) {
            fd.SetKey(fdKey);
        
            // Aircraft type converted from Flarm AcftType
            stat.catDescr   = OGNGetAcTypeName(senderDetails.b.acTy);
            
            // If we still have no accurate ICAO type then we need to fall back to some configured defaults
            if (stat.acTypeIcao.empty()) {
                stat.acTypeIcao = OGNGetIcaoAcType(senderDetails.b.acTy);
                stat.mdl        = stat.catDescr;
            }

            fd.UpdateData(std::move(stat));
        }
        
        // dynamic data
        {   // unconditional...block is only for limiting local variables
            LTFlightData::FDDynamicData dyn;
            
            // position time: zulu time is given in the data, but it is even easier
            //                when using the age, which is always given relative to the query time
            dyn.ts = double(ts);
            
            // non-positional dynamic data
            dyn.gnd =               false;      // there is no GND indicator in OGN data
            dyn.heading =           std::stod(m.str(M_HEAD));
            dyn.spd =               std::stod(m.str(M_SPEED));
            dyn.vsi =               std::stod(m.str(M_VSI));
            dyn.pChannel =          this;
            
            // position
            double lat = std::stod(m.str(M_LAT_DEG));       // degree
            s = m.str(M_LAT_MIN);                           // minutes
            s += m.str(M_LAT_PREC);                         // plus additional precision digit
            lat += std::stod(s) / 60.0;                     // added to degrees
            if (m.str(M_LAT_NS)[0] == 'S')                  // negative in the southern hemisphere
                lat = -lat;
            double lon = std::stod(m.str(M_LON_DEG));       // degree
            s = m.str(M_LON_MIN);                           // minutes
            s += m.str(M_LON_PREC);                         // plus additional precision digit
            lon += std::stod(s) / 60.0;                     // added to degrees
            if (m.str(M_LON_EW)[0] == 'W')                  // negative in the western hemisphere
                lon = -lon;
            positionTy pos (lat, lon,
                            std::stod(m.str(M_ALT)) * M_per_FT,
// no weather correction, OGN delivers geo altitude?   dataRefs.WeatherAltCorr_m(std::stod(tok[GNF_ALT_M])),
                            dyn.ts,
                            dyn.heading);
            pos.f.onGrnd = GND_UNKNOWN;         // there is no GND indicator in OGN data
            
            // position is rather important, we check for validity
            // (we do allow alt=NAN if on ground)
            if ( pos.isNormal(true) )
                fd.AddDynData(dyn, 0, 0, &pos);
            else
                LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,fdKey.c_str(),pos.dbgTxt().c_str());
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }

    return true;
}

// Start or restart a new thread for connecting to aprs.glidernet.org
void OpenGliderConnection::TCPStartUpdate (const positionTy& pos, unsigned dist_km)
{
    // Is there already a TCP thread running?
    if (thrTcp.joinable()) {
        // If the position has not moved too much (20% of dist_km), then we leave the connection as is
        if (tcpPos.dist(pos) / M_per_KM < 0.2 * double(dist_km))
            return;
        // Otherwise we stop the current thread first (will block till stopped)
        TCPClose();
    }
    
    // Stat the TCP connection thread
    bStopTcp = false;
    tcpPos = pos;
    thrTcp = std::thread(&OpenGliderConnection::TCPMain, this, pos, dist_km);
}


// Closes the TCP connection
void OpenGliderConnection::TCPClose ()
{
    if (thrTcp.joinable()) {
        bStopTcp = true;
#if APL == 1 || LIN == 1
        // Mac/Lin: Try writing something to the self-pipe to stop gracefully
        if (tcpPipe[1] == INVALID_SOCKET ||
            write(tcpPipe[1], "STOP", 4) < 0)
        {
            // if the self-pipe didn't work:
#endif
            // close the connection, this will also break out of all
            // blocking calls for receiving message and hence terminate the threads
            tcpRcvr.Close();
#if APL == 1 || LIN == 1
        }
#endif
        
        // wait for thread to finish if I'm not this thread myself
        if (std::this_thread::get_id() != thrTcp.get_id()) {
            if (thrTcp.joinable())
                thrTcp.join();
            thrTcp = std::thread();
        }
    }
}



// Tries reading aircraft information from the OGN a/c list
LTFlightData::FDKeyType OpenGliderConnection::LookupAcList (unsigned long uDevId, LTFlightData::FDStaticData& stat)
{
    // If needed open the file
    if (!ifAcList.is_open()) {
        // open the output file in binary mode
        const std::string sFileName = dataRefs.GetLTPluginPath() + OGN_AC_LIST_FILE;
        ifAcList.open (sFileName, std::ios::binary | std::ios::in);
        if (!ifAcList) {
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            LOG_MSG(logERR, ERR_OGN_ACL_FILE_OPEN_R, sFileName.c_str(), sErr);
            return LTFlightData::KEY_UNKNOWN;
        }
    }
    
    // look up data in the sorted file
    OGNcalcAcFileRecTy rec;
    if (!FileRecLookup (ifAcList, numRecAcList,
                        uDevId, minKeyAcList, maxKeyAcList,
                        &rec, sizeof(rec)))
        return LTFlightData::KEY_UNKNOWN;
    
    // copy some information into the stat structure
    if (*rec.mdl != ' ') { stat.mdl.assign(rec.mdl,sizeof(rec.mdl)); rtrim(stat.mdl); }
    if (*rec.reg != ' ') { stat.reg.assign(rec.reg,sizeof(rec.reg)); rtrim(stat.reg); }
    if (*rec.cn  != ' ') { stat.call.assign(rec.cn,sizeof(rec.cn));  rtrim(stat.call); }
    return
    rec.devType == 'F' ? LTFlightData::KEY_FLARM    :
    rec.devType == 'I' ? LTFlightData::KEY_ICAO     :
    rec.devType == 'O' ? LTFlightData::KEY_OGN      : LTFlightData::KEY_UNKNOWN;
}

//
// MARK: OGN Aircraft list file
//

// process one line of input
static void OGNAcListOneLine (OGNCbHandoverTy& ho, std::string::size_type posEndLn)
{
    // divide the line into its tokens separate by comma
    std::vector<std::string> tok = str_tokenize(ho.readBuf.substr(0,posEndLn), ",", false);
    
    // remove whatever we process now
    ho.readBuf.erase(0,posEndLn+1);
    
    // safety measure: no tokens identified?
    if (tok.empty())
        return;
    
    // is this the very first line telling the field positions?
    if (tok[0][0] == '#') {
        tok[0].erase(0,1);              // remove the #
        for (int i = 0; i < (int)tok.size(); i++) {
            if (tok[i] == "DEVICE_ID") ho.devIdIdx = i;
            else if (tok[i] == "DEVICE_TYPE") ho.devTypeIdx = i;
            else if (tok[i] == "AIRCRAFT_MODEL") ho.mdlIdx = i;
            else if (tok[i] == "REGISTRATION") ho.regIdx = i;
            else if (tok[i] == "CN") ho.cnIdx = i;
        }
        ho.maxIdx = std::max({ho.devIdIdx, ho.mdlIdx, ho.regIdx, ho.cnIdx});
        return;
    }
    
    // regular line must have at least as many fields as we process at maximum
    if (tok.size() <= (size_t)ho.maxIdx)
        return;
    
    // Remove surrounding '
    for (int i = 0; i <= ho.maxIdx; i++) {
        if (tok[i].front() == '\'') tok[i].erase(0,1);
        if (tok[i].back()  == '\'') tok[i].pop_back();
    }
    
    // prepare a new record to be added to the output file
    OGNcalcAcFileRecTy rec;
    rec.devId = std::stoul(tok[ho.devIdIdx], nullptr, 16);
    rec.devType = tok[ho.devTypeIdx][0];
    tok[ho.mdlIdx].     copy(rec.mdl,       sizeof(rec.mdl));
    tok[ho.regIdx].     copy(rec.reg,       sizeof(rec.reg));
    tok[ho.cnIdx].      copy(rec.cn,        sizeof(rec.cn));
    ho.f.write(reinterpret_cast<char*>(&rec), sizeof(rec));
}

/// CURL callback just adding up data
static size_t OGNAcListNetwCB(char *ptr, size_t, size_t nmemb, void* userdata)
{
    // copy buffer to our std::string
    OGNCbHandoverTy& ho = *reinterpret_cast<OGNCbHandoverTy*>(userdata);
    ho.readBuf.append(ptr, nmemb);
    
    // So, apparently we receive data. Latest now open the file to write to
    // (we do that late because we truncate the file and only want to do that
    //  when we are about sure that we can fill it up again)
    if (!ho.f.is_open()) {
        // open the output file in binary mode
        const std::string sFileName = dataRefs.GetLTPluginPath() + OGN_AC_LIST_FILE;
        ho.f.open(sFileName, std::ios::binary | std::ios::trunc);
        if (!ho.f) {
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            LOG_MSG(logERR, ERR_OGN_ACL_FILE_OPEN_W,
                    sFileName.c_str(), sErr);
            // this will make the transfer stop with return code CURLE_WRITE_ERROR
            return 0;
        }
    }
    
    // Now process lines from the beginning of the buffer
    for (std::string::size_type pos = ho.readBuf.find('\n');
         pos != std::string::npos;
         pos = ho.readBuf.find('\n'))
    {
        OGNAcListOneLine(ho, pos);
    }
    
    // all bytes read from the network are consumed
    return nmemb;
}

/// @brief Download OGN Aircraft list, to be called asynchronously (thread)
/// @see http://ddb.glidernet.org/download/
static bool OGNAcListDoDownload ()
{
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_OGNAcList");
    
    bool bRet = false;
    try {
        char curl_errtxt[CURL_ERROR_SIZE];
        OGNCbHandoverTy ho;             // hand-over structure to callback
        
        // initialize the CURL handle
        CURL *pCurl = curl_easy_init();
        if (!pCurl) {
            LOG_MSG(logERR,ERR_CURL_EASY_INIT);
            return false;
        }

        // prepare the handle with the right options
        ho.readBuf.reserve(CURL_MAX_WRITE_SIZE);
        curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, dataRefs.GetNetwTimeout());
        curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, curl_errtxt);
        curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, OGNAcListNetwCB);
        curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &ho);
        curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
        curl_easy_setopt(pCurl, CURLOPT_URL, OGN_AC_LIST_URL);

        // perform the HTTP get request
        CURLcode cc = CURLE_OK;
        if ((cc = curl_easy_perform(pCurl)) != CURLE_OK)
        {
            // problem with querying revocation list?
            if (LTOnlineChannel::IsRevocationError(curl_errtxt)) {
                // try not to query revoke list
                curl_easy_setopt(pCurl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
                LOG_MSG(logWARN, ERR_CURL_DISABLE_REV_QU, LT_DOWNLOAD_CH);
                // and just give it another try
                cc = curl_easy_perform(pCurl);
            }

            // if (still) error, then log error
            if (cc != CURLE_OK)
                LOG_MSG(logERR, ERR_CURL_PERFORM, OGN_AC_LIST_DOWNLOAD, cc, curl_errtxt);
        }

        if (cc == CURLE_OK)
        {
            // CURL was OK, now check HTTP response code
            long httpResponse = 0;
            curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);

            // not HTTP_OK?
            if (httpResponse != HTTP_OK) {
                LOG_MSG(logERR, ERR_CURL_PERFORM, OGN_AC_LIST_DOWNLOAD, (int)httpResponse, ERR_HTTP_NOT_OK);
            }
            else {
                // Success
                bRet = true;
                LOG_MSG(logINFO, INFO_OGN_AC_LIST_DOWNLOADED);
            }
        }
        
        // close the file properly
        if (ho.f.is_open())
            ho.f.close();

        // cleanup CURL handle
        curl_easy_cleanup(pCurl);
    }
    catch (const std::exception& e) {
        LOG_MSG(logERR, "Fetching OGN a/c list failed with exception %s", e.what());
    }
    catch (...) {
        LOG_MSG(logERR, "Fetching OGN a/c list failed with exception");
    }
    
    // done
    return bRet;
}

//
// MARK: Global Functions
//

/// Update a/c list every 12h at most
constexpr time_t OGN_AC_LIST_REFRESH = 12*60*60;

/// Is currently an async operation running to download a/c list?
static std::future<bool> futAcListDownload;

// Return a descriptive text per flam a/c type
const char* OGNGetAcTypeName (FlarmAircraftTy _acTy)
{
    switch (_acTy) {
        case FAT_UNKNOWN:       return "unknown";
        case FAT_GLIDER:        return "Glider / Motor-Glider";
        case FAT_TOW_PLANE:     return "Tow / Tug Plane";
        case FAT_HELI_ROTOR:    return "Helicopter, Rotorcraft";
        case FAT_PARACHUTE:     return "Parachute";
        case FAT_DROP_PLANE:    return "Drop Plane for parachutes";
        case FAT_HANG_GLIDER:   return "Hangglider";
        case FAT_PARA_GLIDER:   return "Paraglider";
        case FAT_POWERED_AC:    return "Powered Aircraft";
        case FAT_JET_AC:        return "Jet Aircraft";
        case FAT_UFO:           return "Flying Saucer, UFO";
        case FAT_BALLOON:       return "Balloon";
        case FAT_AIRSHIP:       return "Airship";
        case FAT_UAV:           return "Unmanned Aerial Vehicle";
        case FAT_STATIC_OBJ:    return "Static object";
    }
    return "unknown";
}

// Return a matching ICAO type code per flarm a/c type
const std::string& OGNGetIcaoAcType (FlarmAircraftTy _acTy)
{
    const std::vector<std::string>& icaoTypes = dataRefs.aFlarmToIcaoAcTy[_acTy];
    if (icaoTypes.empty()) return dataRefs.GetDefaultAcIcaoType();
    if (icaoTypes.size() == 1) return icaoTypes.front();
    // more than one type defined, take a random pick
    const size_t i = randoml(0, long(icaoTypes.size())-1);
    assert(0 <= i && i < icaoTypes.size());
    return icaoTypes[i];
}

// Fill defaults for Flarm aircraft types where not existing
void OGNFillDefaultFlarmAcTypes ()
{
    // Defaults for the FLARM aircraft types. It's of GLID, simply because
    // there are currently no good CSL models out there for paragliders, balloons, ships...
    const std::array<const char*, FAT_UAV+1> DEFAULT_FLARM_ACTY = {
        "GLID",     // FAT_UNKNOWN     = 0,        ///< unknown
        "GLID",     // FAT_GLIDER      = 1,        ///< Glider / Sailplane / Motor-Glider
        "DR40",     // FAT_TOW_PLANE   = 2,        ///< Tow / Tug Plane (usually a L1P type of plane, on OGN the RObin DR-400 is the most often seen L1P plane so I picked this model as a default)
        "EC35",     // FAT_HELI_ROTOR  = 3,        ///< Helicopter, Rotorcraft
        "GLID",     // FAT_PARACHUTE   = 4,        ///< Parachute
        "C208",     // FAT_DROP_PLANE  = 5,        ///< Drop Plane for parachutes (not rarely a L2T type of plane)
        "GLID",     // FAT_HANG_GLIDER = 6,        ///< Hangglider
        "GLID",     // FAT_PARA_GLIDER = 7,        ///< Paraglider
        "C172",     // FAT_POWERED_AC  = 8,        ///< Powered Aircraft
        "C510",     // FAT_JET_AC      = 9,        ///< Jet Aircraft
        "MG29",     // FAT_UFO         = 10,       ///< Flying Saucer, UFO (well, yea...specification says so...not sure how the aliens can get hold of a FLARM sender before reaching earth, though...and _if_ they are interested in being tracked at all)
        "GLID",     // FAT_BALLOON     = 11,       ///< Balloon
        "GLID",     // FAT_AIRSHIP     = 12,       ///< Airship
        "GLID",     // FAT_UAV         = 13,       ///< unmanned aerial vehicle
    };

    // Test all definitions in dataRefs, and if empty then add the above default as single option
    for (size_t i = (size_t)FAT_UNKNOWN; i <= (size_t)FAT_UAV; i++)
        if (dataRefs.aFlarmToIcaoAcTy[i].empty())
            dataRefs.aFlarmToIcaoAcTy[i].push_back(DEFAULT_FLARM_ACTY[i]);
}


// Fetch the aircraft list from OGN
void OGNDownloadAcList ()
{
    // a download still underway?
    if (futAcListDownload.valid() &&
        futAcListDownload.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            // then stop here
            return;
    
    // Don't need to download more often than every 12 hours
    if (GetFileModTime(dataRefs.GetLTPluginPath() + OGN_AC_LIST_FILE) + OGN_AC_LIST_REFRESH > time(NULL))
        return;

    // start another thread to download the a/c list
    futAcListDownload = std::async(std::launch::async, OGNAcListDoDownload);
}

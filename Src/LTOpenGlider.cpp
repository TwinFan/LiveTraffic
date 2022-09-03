/// @file       LTOpenGlider.cpp
/// @brief      Open Glider Network: Requests and processes live tracking data
/// @see        http://live.glidernet.org/
/// @details    Defines OpenGliderConnection:
///             - Direct TCP connection to aprs.glidernet.org:14580 (preferred)
///               - connects to the server
///               - sends a dummy login for read-only access
///               - listens to incoming tracking data
///
/// @see        http://wiki.glidernet.org/wiki:subscribe-to-ogn-data
///
/// @details    Alternatively, and as a fallback if APRS fails:
///             - Request/Reply Interface
///               - Provides a proper REST-conform URL
///               - Interprets the response and passes the tracking data on to LTFlightData.
///
/// @see        https://github.com/glidernet/ogn-live#backend
///
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
// MARK: OGNAnonymousIdMapTy
//

/// The maximum "number" that can be expressed in out way of deriving a 4-char call sign for generated ids, which is using 0-9A-Z in a 36-based number system
constexpr unsigned long OGN_MAX_ANON_CALL_SIGN = 36^4;
/// First anonymous id when we "generate" one ("real" IDs are hex 6 digit long, so we start with 7 digits)
constexpr unsigned long OGN_FIRST_ANONYM_ID = 0x01000000;
/// Next anonymous id when we "generate" one ("real" IDs are hex 6 digit long, so we start with 7 digits)
static unsigned long gOGNAnonId = OGN_FIRST_ANONYM_ID;

// assigns the next anonymous id and generates also a call sign
void OGNAnonymousIdMapTy::GenerateNextId ()
{
    anonymId = ++gOGNAnonId;       // assign the next anonymous id
    // The call sign will start with a question mark to say "it is anonymous and generated"
    // and then we just convert the anonymId to a 4 digit 36-based number,
    // ie. a mix of digits 0-9 and characters A-Z
    anonymCall = "?____";
    unsigned long uCallSign = (anonymId - OGN_FIRST_ANONYM_ID) % OGN_MAX_ANON_CALL_SIGN;
    for (size_t i = 4; i >= 1; --i) {
        char digit = uCallSign % 36;
        anonymCall[i] = digit < 10 ? '0' + digit :
                        digit < 36 ? 'A' + (digit-10) : '?';
        uCallSign /= 36;
    }
}

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

#define ERR_OGN_APRS_CONNECTED      "Connected to OGN APRS Server %s"
#define ERR_OGN_APRS_ERROR          "OGN APRS Server returned error: %s"
#define WARN_OGN_APRS_TIMEOUT       "%s: Timeout waiting for any message"
#define WARN_OGN_APRS_NOT_MATCHED   "An APRS message could be tracking data, but didn't match: %s"

// Request Reply
constexpr const char* OGN_MARKER_BEGIN = "<m a=\""; ///< beginning of a marker in the XML response
constexpr const char* OGN_MARKER_END   = "\"/>";    ///< end of a marker in the XML response
constexpr size_t OGN_MARKER_BEGIN_LEN = 6;          ///< strlen(OGN_MARKER_BEGIN)

// Direct APRS connection
constexpr const char* OGN_APRS_SERVER       = "aprs.glidernet.org";
constexpr int         OGN_APRS_PORT         = 14580;
constexpr size_t      OGN_APRS_BUF_SIZE     = 4096;
constexpr int         OGN_APRS_TIMEOUT_S    = 60;           ///< there's a keep alive _from_ APRS every 20s, so if we don't receive anything in 60s there's something wrong
constexpr float       OGN_APRS_SEND_KEEPALV = 10 * 60.0f;   ///< [s] how often shall _we_ send a keep alive _to_ APRS?
constexpr const char* OGN_APRS_LOGIN        = "user LiveTrffc pass -1 vers " LIVE_TRAFFIC " %s filter r/%.3f/%.3f/%u -p/oimqstunw\r\n";
constexpr const char* OGN_APRS_KEEP_ALIVE   = "# " LIVE_TRAFFIC " %s still alive at %sZ\r\n";
constexpr const char* OGN_APRS_LOGIN_GOOD   = "# logresp LiveTrffc unverified, server ";

// Constructor
OpenGliderConnection::OpenGliderConnection () :
LTChannel(DR_CHANNEL_OPEN_GLIDER_NET, OPGLIDER_NAME),
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
    APRSClose();
    if (ifAcList.is_open())
        ifAcList.close();
    aprsLastData = NAN;
    bFailoverToHttp = false;
}

// put together the URL to fetch based on current view position
std::string OpenGliderConnection::GetURL (const positionTy& pos)
{
    // We only return a URL if we are to use the request/reply procedure
    if (bFailoverToHttp || DataRefs::GetCfgInt(DR_CFG_OGN_USE_REQUREPL)) {
        APRSClose();                         // make sure the ARPS connection is off, will return quickly if not even running
        // Bounding box the size of the configured distance...plus 10% so we have data in hand once the plane comes close enough
        boundingBoxTy box (pos, double(dataRefs.GetFdStdDistance_m()) * 1.10 );
        char url[128] = "";
        snprintf(url, sizeof(url),
                 OPGLIDER_URL,
                 box.nw.lat(),              // lamax
                 box.se.lat(),              // lamin
                 box.se.lon(),              // lomax
                 box.nw.lon());             // lomin
        return std::string(url);
    } else {
        // otherwise (and by default) we are to use the direct APRS connection
        APRSStartUpdate(pos, (unsigned)dataRefs.GetFdStdDistance_km());
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
///          We are not doing full XML parsing, but just search for `<m a=""`
///          and process everything till we find `""/>`
bool OpenGliderConnection::ProcessFetchedData (mapLTFlightDataTy& fdMap)
{
    char buf[100];

    // any a/c filter defined for debugging purposes?
    std::string acFilter ( dataRefs.GetDebugAcFilter() );
    
    // data is expected to be in netData string
    // short-cut if there is nothing
    if ( !netDataPos ) return true;
    
    // We need current Zulu time to interpret the timestamp in the data
    const std::time_t tNow = std::time(nullptr) + std::lround(dataRefs.GetChTsOffset());
    
    // We need to calculate distance to current camera later on
    const positionTy viewPos = dataRefs.GetViewPos();

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
        const std::string sMarker (sPos, std::string::size_type(sEnd-sPos));
        std::vector<std::string> tok = str_tokenize(sMarker, ",", false);
        if (tok.size() != GNF_COUNT) {
            LOG_MSG(logERR, ERR_OGN_WRONG_NUM_FIELDS, sMarker.c_str());
            break;
        }
        
        // We sliently skip all static objects
        if (dataRefs.GetHideStaticTwr() &&
            std::stoi(tok[GNF_FLARM_ACFT_TYPE]) == FAT_STATIC_OBJ)
            continue;
        
        // We also skip records, which are outdated by the time they arrive
        const long age_s = std::abs(std::stol(tok[GNF_AGE_S]));
        if (age_s >= dataRefs.GetFdBufPeriod())
            continue;

        // Look up the device in the DDB / Aircraft list.
        // This also checks if the device wants to be tracked and sets the key accordingly
        LTFlightData::FDKeyTy fdKey;
        LTFlightData::FDStaticData stat;
        if (!LookupAcList(tok[GNF_FLARM_DEVICE_ID], fdKey, stat))
            continue;                   // device doesn't want to be tracked -> ignore!
        
        // key not matching a/c filter? -> skip it
        if ((!acFilter.empty() && (fdKey != acFilter)) )
            continue;
        
        try {
            // from here on access to fdMap guarded by a mutex
            // until FD object is inserted and updated
            std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
            
            // get the fd object from the map
            // this fetches an existing or, if not existing, creates a new one
            LTFlightData& fd = fdMap[fdKey];
            
            // also get the data access lock once and for all
            // so following fetch/update calls only make quick recursive calls
            std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
            // now that we have the detail lock we can release the global one
            mapFdLock.unlock();

            // completely new? fill key fields and define static data
            // (for OGN some data we only define initially,
            //  ICAO a/c type derivation has a random element
            //  (if more than one ICAO type is defined))
            if ( fd.empty() ) {
                fd.SetKey(fdKey);
            
                // Aircraft type converted from Flarm AcftType
                const FlarmAircraftTy acTy = (FlarmAircraftTy)std::clamp<int>(std::stoi(tok[GNF_FLARM_ACFT_TYPE]),
                                                                              FAT_UNKNOWN, FAT_STATIC_OBJ);
                stat.catDescr   = OGNGetAcTypeName(acTy);
                
                // If we still have no accurate ICAO type then we need to fall back to some configured defaults
                if (stat.acTypeIcao.empty())
                    stat.acTypeIcao = OGNGetIcaoAcType(acTy);
                if (stat.mdl.empty())
                    stat.mdl        = stat.catDescr;
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
                
                // Update the slug with current position
                snprintf(buf, sizeof(buf), OPGLIDER_CHECK_URL,
                         pos.lat(), pos.lon());
                stat.slug = buf;
                // Update static data
                fd.UpdateData(std::move(stat), pos.dist(viewPos));
                
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


// return a human-readable staus
std::string OpenGliderConnection::GetStatusText () const
{
    // Let's start with the standard text
    std::string s (LTChannel::GetStatusText());
    // if we have a latest APRS timestamp then we add age of last APRS message
    if (!std::isnan(aprsLastData)) {
        char buf[50];
        snprintf(buf, sizeof(buf), ", last message %.1fs ago",
                 dataRefs.GetMiscNetwTime() - aprsLastData);
        s += buf;
    }
    return s;
}


// Main function for APRS connection, expected to be started in a thread
void OpenGliderConnection::APRSMain (const positionTy& pos, unsigned dist_km)
{
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_OGN_APRS");
    
    try {
        // open a TCP connection to APRS.glidernet.org
        aprsLastData = NAN;
        tcpAprs.Connect(OGN_APRS_SERVER, OGN_APRS_PORT, OGN_APRS_BUF_SIZE, unsigned(OGN_APRS_TIMEOUT_S * 1000));
        int maxSock = (int)tcpAprs.getSocket() + 1;
#if APL == 1 || LIN == 1
        // the self-pipe to shut down the TCP socket gracefully
        if (pipe(aprsPipe) < 0)
            throw NetRuntimeError("Couldn't create pipe");
        fcntl(aprsPipe[0], F_SETFL, O_NONBLOCK);
        maxSock = std::max(maxSock, aprsPipe[0]+1);
#endif
        
        // Login
        if (!APRSDoLogin(pos, dist_km)) {
            SetValid(false, true);
            bStopAprs = true;
        }
        
        // *** Main Loop ***
        struct timeval timeout = { OGN_APRS_TIMEOUT_S, 0 };
        while (!bStopAprs && tcpAprs.isOpen())
        {
            // wait for some signal on either socket (APRS or self-pipe)
            fd_set sRead;
            FD_ZERO(&sRead);
            FD_SET(tcpAprs.getSocket(), &sRead);     // check our socket
#if APL == 1 || LIN == 1
            FD_SET(aprsPipe[0], &sRead);              // check the self-pipe
#endif
            int retval = select(maxSock, &sRead, NULL, NULL, &timeout);
            
            // short-cut if we are to shut down (return from 'select' due to closed socket)
            if (bStopAprs)
                break;

            // select call failed???
            if (retval == -1)
                throw NetRuntimeError("'select' failed");
            else if (retval == 0)
                throw NetRuntimeError("'select' ran into a timeout");

            // select successful - traffic data
            if (retval > 0 && FD_ISSET(tcpAprs.getSocket(), &sRead))
            {
                // read APRS message
                long rcvdBytes = tcpAprs.recv();
                
                // received something?
                if (rcvdBytes > 0)
                {
                    // have it processed
                    if (!APRSProcessData(tcpAprs.getBuf()))
                        throw NetRuntimeError("APRSProcessData failed");
                }
                else
                    throw NetRuntimeError("Read no data from TCP socket ");
            }

            // Send a keep-alive every 15 minutes
            if (CheckEverySoOften(aprsLastKeepAlive, OGN_APRS_SEND_KEEPALV))
                APRSSendKeepAlive();
        }
        
    }
    catch (std::runtime_error& e) {
        LOG_MSG(logERR, ERR_TCP_LISTENACCEPT, ChName(),
                OGN_APRS_SERVER, std::to_string(OGN_APRS_PORT).c_str(),
                e.what());
        // Stop this connection attempt
        bStopAprs = true;
        if (GetErrCnt() == CH_MAC_ERR_CNT) {         // too bad already?
            bFailoverToHttp = true; // we fail over to the HTTP way of things
            SetValid(true);         // this resets the error count
        } else
            IncErrCnt();
    }
    
#if APL == 1 || LIN == 1
    // close the self-pipe sockets
    for (SOCKET &s: aprsPipe) {
        if (s != INVALID_SOCKET) close(s);
        s = INVALID_SOCKET;
    }
#endif
    
    // make sure the socket is closed
    tcpAprs.Close();
}

// Send the login
bool OpenGliderConnection::APRSDoLogin (const positionTy& pos, unsigned dist_km)
{
    // Prepare login string like "user LiveTrffc pass -1 vers LiveTraffic 2.20 filter r/43.3/-80.2/50 -p/oimqstunw"
    char sLogin[120];
    snprintf(sLogin, sizeof(sLogin), OGN_APRS_LOGIN, LT_VERSION,
             pos.lat(), pos.lon(), dist_km);
    aprsLastKeepAlive = dataRefs.GetMiscNetwTime();     // also counts as a message sent _to_ APRS
    DebugLogRaw(sLogin);
    return tcpAprs.send(sLogin);
}

// Send a simple keep-alive message to APRS
/// @details Documentation is not exactly unambigious if sending a keep-alive is needed.
///          But I experienced connection drops after exactly 30 minutes without keep-alive.
///          So we do send one.
bool OpenGliderConnection::APRSSendKeepAlive()
{
    // Prepare login string like "user LiveTrffc pass -1 vers LiveTraffic 2.20 filter r/43.3/-80.2/50 -p/oimqstunw"
    char sKeepAlive[120];
    snprintf(sKeepAlive, sizeof(sKeepAlive), OGN_APRS_KEEP_ALIVE, LT_VERSION,
             ts2string(time(nullptr)).c_str());
    LOG_MSG(logDEBUG, "OGN: Sending keep alive: %s", sKeepAlive);
    DebugLogRaw(sKeepAlive);
    return tcpAprs.send(sKeepAlive);
}

// Process received data
bool OpenGliderConnection::APRSProcessData (const char* buffer)
{
    // save the data to our processing buffer
    DebugLogRaw(buffer);
    aprsData += buffer;
    if (aprsData.empty())            // weird...but not an error if there's nothing to process
        return true;
    
    // So we just received something
    aprsLastData = dataRefs.GetMiscNetwTime();
    DecErrCnt();

    // process the input line by line, expected a line to be ended by \r\n
    // (If CR/LF is yet missing then the received data is yet incomplete and will be completed with the next received data)
    for (size_t lnEnd = aprsData.find("\r\n");
         lnEnd != std::string::npos;
         aprsData.erase(0,lnEnd+2), lnEnd = aprsData.find("\r\n"))
    {
        if (!APRSProcessLine(aprsData.substr(0,lnEnd)))
            return false;
    }
    return true;
}

/// @brief Process one line of received data
/// @see https://github.com/svoop/ogn_client-ruby/wiki/SenderBeacon
bool OpenGliderConnection::APRSProcessLine (const std::string& ln)
{
    char buf[100];
    
    // Sanity check
    if (ln.empty()) return true;
    
    // Special processing for lines beginning with a hash mark
    if (ln[0] == '#')
    {
        // Test for login error
        if (ln.find("Invalid") != std::string::npos) {
            LOG_MSG(logERR, ERR_OGN_APRS_ERROR, ln.c_str());
            return false;
        }
        
        // Test for successful login
        if (ln.find(OGN_APRS_LOGIN_GOOD) != std::string::npos)
            LOG_MSG(logINFO, ERR_OGN_APRS_CONNECTED, str_last_word(ln).c_str());
        
        // Test for server time to feed into our system clock deviation calculation
        if (dataRefs.ChTsAcceptMore()) {
            static std::regex reTm ("\\d+ \\w{3} \\d{4} (\\d{1,2}):(\\d{2}):(\\d{2}) GMT");
            std::smatch mTm;
            std::regex_search(ln, mTm, reTm);
            if (!mTm.empty()) {
                const time_t serverT = mktime_utc(std::stoi(mTm.str(1)),
                                                  std::stoi(mTm.str(2)),
                                                  std::stoi(mTm.str(3)));
                dataRefs.ChTsOffsetAdd(double(serverT));
            }
        }
        
        // Otherwise ignore all lines starting with '#' as comments
        return true;
    }
    
    // Try to match the line with an expected pattern
    static std::regex re (":/(\\d\\d)(\\d\\d)(\\d\\d)[hz]"  // timestamp, 3 matches: h, min, sec
                          "(\\d\\d)(\\d\\d.\\d\\d)(N|S)"    // latitude, 3 matches: degree, minutes incl. decimals, N or S
                          "(?:/|\\\\)"                      // display symbol, not stored
                          "(\\d\\d\\d)(\\d\\d.\\d\\d)(E|W)" // longitude, 3 matches: degree, minutes incl. decimals, E or W
                          "."                               // display symbol
                          "(\\d\\d\\d)/(\\d\\d\\d)"         // heading/speed, 2 matches (optional, "000/000" indicates no data)
                          "/A=(\\d{6}) "                    // altitude in feet, 1 match
                          "!W(\\d)(\\d)! "                  // position precision enhancement, 2 matches: latitude, longitude
                          "id([0-9A-Z]{2})([0-9A-Z]{6,8}) " // sender details and address, 2 matches
                          "(?:([-+]\\d+)fpm)?"              // vertical speed (optional), 1 match
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
    
    // We expect 16 matches, 17 if fpm is given. Size is one more because element 0 is the complete matched string:
    if (m.size() < 17) {
        // didn't match. But if we think this _could_ be a valid message then we should warn, maybe there's still a flaw in the regex above
        if (ln.find("! id") != std::string::npos) {
            LOG_MSG(logWARN, WARN_OGN_APRS_NOT_MATCHED, ln.c_str());
        }
        // but otherwise no issue...there are some message in the stream that we just don't need
        return true;
    }
    
    // Matches:        0                                                        1    2    3    4    5       6   7     8       9   10    11    12       13  14  15   16       17
    // :/215957h5000.42N\00839.32En000/000/A=000502 !W38! id3ED0075F -019fpm  | 21 | 59 | 57 | 50 | 00.42 | N | 008 | 39.32 | E | 000 | 000 | 000502 | 3 | 8 | 3E | D0075F | -019 |
    
    // We silently skip all static objects and those who do not want to be tracked
    uint8_t senderDetails   = (uint8_t)std::stoul(m.str(M_SEND_DETAILS), nullptr, 16);
    FlarmAircraftTy acTy    = FlarmAircraftTy((senderDetails & 0b00111100) >> 2);
    if (senderDetails & 0b11000000)             // "No tracking" or "stealth mode" set?
        return true;                            // -> ignore
    
    if (dataRefs.GetHideStaticTwr() &&          // Shall hide static objects and it is
        acTy == FAT_STATIC_OBJ)                 // Static object?
        return true;                            // -> ignore
    
    // Timestamp - skip too old records
    time_t ts = mktime_utc(std::stoi(m.str(M_TS_H)),
                           std::stoi(m.str(M_TS_MIN)),
                           std::stoi(m.str(M_TS_S)));
    if (ts < time_t(dataRefs.GetSimTime()))
        return true;
    
    // Look up the device in the DDB / Aircraft list.
    // This also checks if the device wants to be tracked and sets the key accordingly
    LTFlightData::FDKeyTy fdKey;
    LTFlightData::FDStaticData stat;
    if (!LookupAcList(m.str(M_SEND_ID), fdKey, stat))
        return true;                            // device doesn't want to be tracked -> ignore silently!

    // key not matching a/c filter? -> skip it
    std::string s ( dataRefs.GetDebugAcFilter() );
    if ((!s.empty() && (fdKey != s)) )
        return true;
    
    try {
        // from here on access to fdMap guarded by a mutex
        // until FD object is inserted and updated
        std::unique_lock<std::mutex> mapFdLock (mapFdMutex);
        
        // get the fd object from the map
        // this fetches an existing or, if not existing, creates a new one
        LTFlightData& fd = mapFd[fdKey];
        
        // also get the data access lock once and for all
        // so following fetch/update calls only make quick recursive calls
        std::lock_guard<std::recursive_mutex> fdLock (fd.dataAccessMutex);
        // now that we have the detail lock we can release the global one
        mapFdLock.unlock();

        // completely new? fill key fields and define static data
        // (for OGN we only define static data initially,
        //  it has no changing elements, and ICAO a/c type derivation
        //  has a random element (if more than one ICAO type is defined))
        if ( fd.empty() ) {
            fd.SetKey(fdKey);
        
            // Aircraft type converted from Flarm AcftType
            stat.catDescr   = OGNGetAcTypeName(acTy);
            
            // If we still have no accurate ICAO type then we need to fall back to some configured defaults
            if (stat.acTypeIcao.empty())
                stat.acTypeIcao = OGNGetIcaoAcType(acTy);
            if (stat.mdl.empty())
                stat.mdl        = stat.catDescr;
        }
        
        // dynamic data
        {   // unconditional...block is only for limiting local variables
            LTFlightData::FDDynamicData dyn;
            
            // position time: zulu time is given in the data
            dyn.ts = double(ts);
            
            // non-positional dynamic data
            dyn.gnd =               false;      // there is no GND indicator in OGN data
            dyn.heading =           std::stod(m.str(M_HEAD));
            dyn.spd =               std::stod(m.str(M_SPEED));
            if (m.size() > M_VSI && !m.str(M_VSI).empty())
                dyn.vsi =           std::stod(m.str(M_VSI));
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
            
            // Update the slug with current position
            snprintf(buf, sizeof(buf), OPGLIDER_CHECK_URL,
                     pos.lat(), pos.lon());
            stat.slug = buf;
            // Send static data if requested
            fd.UpdateData(std::move(stat),
                          pos.dist(dataRefs.GetViewPos()));
            
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
void OpenGliderConnection::APRSStartUpdate (const positionTy& pos, unsigned dist_km)
{
    // Is there already an APRS thread running?
    if (thrAprs.joinable()) {
        // If we are actively running and the position has not moved too much
        // (20% of dist_km), then we leave the connection as is
        if (!bStopAprs && (aprsPos.dist(pos) / M_per_KM < 0.2 * double(dist_km)))
            return;
        // Otherwise we stop the current thread first (will block till stopped)
        APRSClose();
    }
    
    // Start the APRS connection thread
    bStopAprs = false;
    aprsPos = pos;
    thrAprs = std::thread(&OpenGliderConnection::APRSMain, this, pos, dist_km);
}


// Closes the APRS connection
void OpenGliderConnection::APRSClose ()
{
    if (thrAprs.joinable()) {
        if (!bStopAprs) {
            bStopAprs = true;
#if APL == 1 || LIN == 1
            // Mac/Lin: Try writing something to the self-pipe to stop gracefully
            if (aprsPipe[1] == INVALID_SOCKET ||
                write(aprsPipe[1], "STOP", 4) < 0)
            {
                // if the self-pipe didn't work:
                // close the connection, this will also break out of all
                // blocking calls for receiving message and hence terminate the threads
                tcpAprs.Close();
            }
#else
            tcpAprs.Close();
#endif
        }
        
        // wait for thread to finish if I'm not this thread myself
        if (std::this_thread::get_id() != thrAprs.get_id()) {
            if (thrAprs.joinable())
                thrAprs.join();
            thrAprs = std::thread();
        }
        // reset last data timestamp
        aprsLastData = NAN;
    }
}


// Tries reading aircraft information from the OGN a/c list
bool OpenGliderConnection::LookupAcList (const std::string& sDevId,
                                         LTFlightData::FDKeyTy& key,
                                         LTFlightData::FDStaticData& stat)
{
    // device id converted to binary number
    unsigned long uDevId = std::stoul(sDevId, nullptr, 16);
    
    // If needed open the file
    if (!ifAcList.is_open()) {
        // open the output file in binary mode
        const std::string sFileName = dataRefs.GetLTPluginPath() + OGN_AC_LIST_FILE;
        ifAcList.open (sFileName, std::ios::binary | std::ios::in);
        if (!ifAcList) {
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            LOG_MSG(logERR, ERR_OGN_ACL_FILE_OPEN_R, sFileName.c_str(), sErr);
        }
    }
    
    // look up data in the sorted file
    OGN_DDB_RecTy rec;
    if (ifAcList.is_open() &&
        FileRecLookup (ifAcList, numRecAcList,
                       uDevId, minKeyAcList, maxKeyAcList,
                       &rec, sizeof(rec)))
    {
        // copy some information into the stat structure
        if (*rec.mdl != ' ') { stat.mdl.assign(rec.mdl,sizeof(rec.mdl)); rtrim(stat.mdl); }
        if (*rec.reg != ' ') { stat.reg.assign(rec.reg,sizeof(rec.reg)); rtrim(stat.reg); }
        if (*rec.cn  != ' ') { stat.call.assign(rec.cn,sizeof(rec.cn));  rtrim(stat.call); }
        
        // based on the model information look up an ICAO a/c type
        if (!stat.mdl.empty())
            stat.acTypeIcao = ModelIcaoType::getIcaoType(stat.mdl);
    } else {
        // clear the record again (potentially used as buffer during lookup)
        // This will also CLEAR the TRACKED and IDENTIFIED flags
        // as required for a device not found in the DDB:
        rec = OGN_DDB_RecTy();
        rec.devType = 'O';          // treat it as an OGN id from the outset
        rec.SetTracked();           // tracking a not-in-DDB device is OK
    }
    
    // If the device doesn't want to be tracked we bail
    if (!rec.IsTracked())
        return false;
    
    // *** Aircraft key type / device
    
    // If the device doesn't want to be identified -> map to generated anonymous id
    if (!rec.IsIdentified()) {
        // clear any potentially identifying information
        stat.reg.clear();
        stat.call.clear();

        // The key into the map consists of the device id and the device type
        std::string idKey (sDevId);             // device id string
        idKey += '_';                           // _
        idKey += rec.devType;                   // append device type (F or I or even O) to increase uniqueness - just in case...
        // look up or _create_ the mapping record to the generated anonymous id
        OGNAnonymousIdMapTy& anon = mapAnonymousId[idKey];
        // take over the mapped anonymous id
        key.SetKey(LTFlightData::KEY_OGN, anon.anonymId);
        stat.call = anon.anonymCall;
    } else {
        // device is allowed to be identified
        key.SetKey(rec.devType == 'F' ? LTFlightData::KEY_FLARM    :
                   rec.devType == 'I' ? LTFlightData::KEY_ICAO     : LTFlightData::KEY_OGN,
                   uDevId);
    }
    
    // is allowed to be tracked, ie. is visible
    return true;
}

//
// MARK: OGN Aircraft list file (DDB)
//

// process one line of input
static void OGNAcListOneLine (OGNCbHandoverTy& ho, std::string::size_type posEndLn)
{
    // divide the line into its tokens separate by comma
    std::string ln = ho.readBuf.substr(0,posEndLn);
    ho.readBuf.erase(0,posEndLn+1);         // remove from the buffer whatever we process now
    rtrim(ln, "\r\n");                      // remove CR/LF from the line
    std::vector<std::string> tok = str_tokenize(ln, ",", false);
    
    // safety measure: no tokens identified?
    if (tok.empty())
        return;
    
    // is this the very first line telling the field positions?
    if (tok[0][0] == '#') {
        tok[0].erase(0,1);              // remove the #
        // walk the tokens and remember the column indexes per field
        for (unsigned i = 0; i < (unsigned)tok.size(); i++) {
            if (tok[i] == "DEVICE_ID") ho.devIdIdx = i;
            else if (tok[i] == "DEVICE_TYPE") ho.devTypeIdx = i;
            else if (tok[i] == "AIRCRAFT_MODEL") ho.mdlIdx = i;
            else if (tok[i] == "REGISTRATION") ho.regIdx = i;
            else if (tok[i] == "CN") ho.cnIdx = i;
            else if (tok[i] == "TRACKED") ho.trackedIdx = i;
            else if (tok[i] == "IDENTIFIED") ho.identifiedIdx = i;
        }
        ho.maxIdx = std::max({
            ho.devIdIdx, ho.mdlIdx, ho.regIdx, ho.cnIdx,
            ho.trackedIdx, ho.identifiedIdx });
        return;
    }
    
    // regular line must have at least as many fields as we process at maximum
    if (tok.size() <= (size_t)ho.maxIdx)
        return;
    
    // Remove surrounding '
    for (unsigned i = 0; i <= ho.maxIdx; i++) {
        if (tok[i].front() == '\'') tok[i].erase(0,1);
        if (tok[i].back()  == '\'') tok[i].pop_back();
    }
    
    // prepare a new record to be added to the output file
    OGN_DDB_RecTy rec;
    rec.devId = std::stoul(tok[ho.devIdIdx], nullptr, 16);
    rec.devType = tok[ho.devTypeIdx][0];
    tok[ho.mdlIdx].     copy(rec.mdl,       sizeof(rec.mdl));
    tok[ho.regIdx].     copy(rec.reg,       sizeof(rec.reg));
    tok[ho.cnIdx].      copy(rec.cn,        sizeof(rec.cn));
    if (tok[ho.trackedIdx] == "Y")    rec.SetTracked();
    if (tok[ho.identifiedIdx] == "Y") rec.SetIdentified();
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

// Return a descriptive text per FLARM a/c type
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

// Return a matching ICAO type code per FLARM a/c type
const std::string& OGNGetIcaoAcType (FlarmAircraftTy _acTy)
{
    try {
        const std::vector<std::string>& icaoTypes = dataRefs.aFlarmToIcaoAcTy.at(_acTy);
        if (icaoTypes.empty()) return dataRefs.GetDefaultAcIcaoType();
        if (icaoTypes.size() == 1) return icaoTypes.front();
        // more than one type defined, take a random pick
        const size_t i = (size_t)randoml(0, long(icaoTypes.size()) - 1);
        assert(0 <= i && i < icaoTypes.size());
        return icaoTypes[i];
    }
    catch (...) {
        return dataRefs.GetDefaultAcIcaoType();
    }
}

// Fill defaults for FLARM aircraft types were not existing
void OGNFillDefaultFlarmAcTypes ()
{
    // Defaults for the FLARM aircraft types. It's often GLID, simply because
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

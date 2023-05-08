/// @file       LTADSBHub.cpp
/// @brief      ADSBHub: Processes live tracking data
/// @see        https://www.adsbhub.org/howtogetdata.php
/// @details    Defines ADSBHubConnection:
///             - Direct TCP connection to data.adsbhub.org:5002
///               - connects to the server
///               - listens to incoming tracking data
/// @author     Birger Hoppe
/// @copyright  (c) 2023 Birger Hoppe
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

//
// MARK: ADSBHub Definitions
//

#define ADSBHUB_NAME                "ADSBHub"               ///< Human-readable Name of connection
#define ADSBHUB_HOST                "data.adsbhub.org"      ///< Host to connect to
constexpr int ADSBHUB_PORT          = 5002;                 ///< Port to connect to
constexpr size_t ADSBHUB_BUF_SIZE   = 8192;                 ///< Buffer size to use
constexpr int    ADSBHUB_TIMEOUT_S  = 60;                   ///< ADSBHub sends something every 5-6s, so if we don't receive anything in 60s there's something wrong

//
// MARK: ADSBHub Connection
//

// Constructor
ADSBHubConnection::ADSBHubConnection (mapLTFlightDataTy& _fdMap) :
LTChannel(DR_CHANNEL_ADSB_HUB, ADSBHUB_NAME),
LTOnlineChannel(),
LTFlightDataChannel(),
fdMap(_fdMap)
{
    // purely informational
    urlName  = ADSBHUB_CHECK_NAME;
    urlLink  = ADSBHUB_CHECK_URL;
    urlPopup = ADSBHUB_CHECK_POPUP;
}

// Destructor closes the a/c list file
ADSBHubConnection::~ADSBHubConnection ()
{
    Close();
}

// return a human-readable staus
std::string ADSBHubConnection::GetStatusText () const
{
    // Let's start with the standard text
    std::string s (LTChannel::GetStatusText());
    // add format of data being received
    if (eFormat == FMT_NULL_DATA) {
        s += ", had received no data, verify ADSBHub 'Data Access' configuration!";
        if (!sPublicIPv4addr.empty()) {
            s += " Your public IP address appears to be ";
            s += sPublicIPv4addr;
        }
    }
    else if (thrStream.joinable()) {
        switch (eFormat) {
            case FMT_SBS:       s += ", receiving SBS format"; break;
            case FMT_ComprVRS:  s += ", receiving Compressed VRS format"; break;
            default:            s += ", connecting...";
        }
    }
    return s;
}


/// Makes sure the TCP thread is running
bool ADSBHubConnection::FetchAllData(const positionTy&)
{
    StreamStart();
    return false;               // didn't receive data to be processed in the outer loop
}


// Main function for stream connection, expected to be started in a thread
void ADSBHubConnection::StreamMain ()
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_ADSBHUB", LC_ALL_MASK);

    try {
        // open a TCP connection to APRS.glidernet.org
        tcpStream.Connect(ADSBHUB_HOST, ADSBHUB_PORT, ADSBHUB_BUF_SIZE, unsigned(ADSBHUB_TIMEOUT_S * 1000));
        int maxSock = (int)tcpStream.getSocket() + 1;
#if APL == 1 || LIN == 1
        // the self-pipe to shut down the TCP socket gracefully
        if (pipe(streamPipe) < 0)
            throw NetRuntimeError("Couldn't create pipe");
        fcntl(streamPipe[0], F_SETFL, O_NONBLOCK);
        maxSock = std::max(maxSock, streamPipe[0]+1);
#endif
        
        // *** Main Loop ***
        struct timeval timeout = { ADSBHUB_TIMEOUT_S, 0 };
        while (!bStopThr && tcpStream.isOpen())
        {
            // wait for some signal on either socket (Stream or self-pipe)
            fd_set sRead;
            FD_ZERO(&sRead);
            FD_SET(tcpStream.getSocket(), &sRead);     // check our socket
#if APL == 1 || LIN == 1
            FD_SET(streamPipe[0], &sRead);              // check the self-pipe
#endif
            int retval = select(maxSock, &sRead, NULL, NULL, &timeout);
            
            // short-cut if we are to shut down (return from 'select' due to closed socket)
            if (bStopThr)
                break;

            // select call failed???
            if (retval == -1)
                throw NetRuntimeError("'select' failed");
            else if (retval == 0)
                throw NetRuntimeError("'select' ran into a timeout");

            // select successful - traffic data
            if (retval > 0 && FD_ISSET(tcpStream.getSocket(), &sRead))
            {
                // read Stream data
                long rcvdBytes = tcpStream.recv();
                
                // received something?
                if (rcvdBytes > 0)
                {
                    // if not yet decided: decided on received message format (very first frame only)
                    if (!eFormat && rcvdBytes >= 3) {
                        const char* p = tcpStream.getBuf();
                        if (std::isalpha(p[0]) && std::isalpha(p[1]) &&
                            (std::isalpha(p[2]) || p[2] == ','))
                            eFormat = FMT_SBS;
                        else
                            eFormat = FMT_ComprVRS;
                    }
                    
                    // have it processed
                    switch (eFormat) {
                        case FMT_SBS:
                            if (!StreamProcessDataSBS(size_t(rcvdBytes), tcpStream.getBuf()))
                                throw NetRuntimeError("StreamProcessDataSBS failed");
                            break;
                        case FMT_ComprVRS:
                            if (!StreamProcessDataVRS(size_t(rcvdBytes), (const uint8_t*)tcpStream.getBuf()))
                                throw NetRuntimeError("StreamProcessDataVRS failed");
                            break;
                        case FMT_NULL_DATA:
                        case FMT_UNKNOWN:
                            throw NetRuntimeError("Format yet unknown, received too few data");
                    }
                }
                else {
                    sPublicIPv4addr = GetPublicIPv4();
                    eFormat = FMT_NULL_DATA;
                    SHOW_MSG(logERR, "Received no data from ADSBHub. Verify 'Read Access' configuration at ADSBHub!%s",
                             sPublicIPv4addr.empty() ? "" : (std::string("\nYour public IP address appears to be ") + sPublicIPv4addr).c_str());
                    throw NetRuntimeError("Read no data from TCP socket (IP address correctly set up in ADSBHub settings?)");
                }
            }
        }
    }
    catch (std::runtime_error& e) {
        LOG_MSG(logERR, ERR_TCP_LISTENACCEPT, ChName(),
                ADSBHUB_HOST, std::to_string(ADSBHUB_PORT).c_str(),
                e.what());
        // Stop this connection attempt
        bStopThr = true;
        // Set channel to invalid
        SetValid(false,true);
    }
    
#if APL == 1 || LIN == 1
    // close the self-pipe sockets
    for (SOCKET &s: streamPipe) {
        if (s != INVALID_SOCKET) close(s);
        s = INVALID_SOCKET;
    }
#endif
    
    // make sure the socket is closed
    tcpStream.Close();
}

// Process received SBS data
bool ADSBHubConnection::StreamProcessDataSBS (size_t num, const char* buffer)
{
    // reading pointer into buffer: beginning and end of a line
    const char* pStart = buffer;            // beginning of a line
    const char* pEnd = (const char*)std::memchr(pStart, '\n', num);
    
    // Loop for all complete lines found
    while (pEnd)
    {
        const size_t len = size_t(pEnd-pStart +1);
        
        // Any left overs from previous message?
        if (!lnLeftOver.empty()) {
            lnLeftOver.append(pStart, len);
            if (!StreamProcessDataSBSLine(lnLeftOver.c_str(), lnLeftOver.c_str() + lnLeftOver.length() -1) &&
                !IncErrCnt())
            {
                lnLeftOver.clear();     // too many errors, bail
                return false;
            }
            lnLeftOver.clear();         // left overs now processed
        }
        // Process full line directly from received data
        else {
            if (!StreamProcessDataSBSLine(pStart, pEnd) &&
                !IncErrCnt())
                return false;           // too many errors, bail
        }
        // Advance to next line in the data
        num -= len;                     // have processed `len` chars
        pStart = pEnd + 1;              // next line starts _after_ the CR
        pEnd = (const char*)std::memchr(pStart, '\n', num);
    }
    
    // Any left overs? Save for later
    if (num > 0)
        lnLeftOver.append(pStart, num);
    else
        // If the entire network message _exactly_ ended with a full line then assume we're done completely and also process that last plane data
        ProcessPlaneData();
    
    // Success
    DecErrCnt();                        // reduce error counter with each fully processed message
    return true;
}

/// Read a 'token' from the given string
std::string readToken (const char* &pStart, const char* pEnd, char sep = ',')
{
    // Safety check
    if (pStart > pEnd) return std::string();
    
    const char* pBegin = pStart;
    for (; pStart != pEnd && *pStart != sep; ++pStart);
    // eat separator
    ++pStart;
    return std::string(pBegin, size_t(pStart - pBegin -1));
}


#define TEST_END(s) if (pStart > pEnd) { LOG_MSG(logERR, "Line too short, " s ); return false; }

#define TEST_LEN(l,s) if ((pStart + l-1) > pEnd) { LOG_MSG(logERR, "Line too short for next value " s ); return false; }

/// @brief Process a single line of SBS data
/// @see http://woodair.net/sbs/article/barebones42_socket_data.htm
/// @details ADSBHub only sends `MSG` type, so we only prepare processing that.
///          Also, we don't care about the `MSG` transmission type...we just process
///          any field we actually receive and skip over any empty field.
///          ADSBHub sends infos per plane in ascending order by ADS-B Hex id,
///          typically in 3 `MSG` lines of transmission types 1, 3, 4. But we don't
///          fully rely on that. We just read data per plane until the hex id
///          changes.
bool ADSBHubConnection::StreamProcessDataSBSLine (const char* pStart, const char* pEnd)
{
    // Line must start with 'MSG', otherwise we ignore
    std::string token = readToken(pStart, pEnd);
    if (token != "MSG") {
        LOG_MSG(logDEBUG, "Ignoring line of type '%s'", token.c_str());
        return false;
    }
    
    // Ignore 3 fields (transmission type, session ID, aircraft id)
    readToken(pStart, pEnd);
    readToken(pStart, pEnd);
    readToken(pStart, pEnd);
    TEST_END("not even read an ADS-B hex id yet");
    
    // Read the ADS-B hex id
    token = readToken(pStart, pEnd);
    TEST_END("just only read an ADS-B hex id");
    if (token.empty()) {
        LOG_MSG(logDEBUG, "ADS-B hex id was empty");
        return false;
    }
    LTFlightData::FDKeyTy key (LTFlightData::KEY_ICAO, token);
    
    // Change of plane? Then process previous data first before continuing
    if (fdKey != key) {
        ProcessPlaneData();
        // New plane
        fdKey = key;
    }
    
    // Skip over FlightID and date/time message created
    readToken(pStart, pEnd);
    readToken(pStart, pEnd);
    readToken(pStart, pEnd);
    TEST_END("not yet read date/time of message created");
    
    // Date/time message logged
    std::tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_isdst = -1;               // re-lookup DST info
    tm.tm_year = std::stoi(readToken(pStart, pEnd, '/')) - 1900;
    tm.tm_mon  = std::stoi(readToken(pStart, pEnd, '/')) - 1;
    tm.tm_mday = std::stoi(readToken(pStart, pEnd));
    tm.tm_hour = std::stoi(readToken(pStart, pEnd, ':'));
    tm.tm_min  = std::stoi(readToken(pStart, pEnd, ':'));
    tm.tm_sec  = std::stoi(readToken(pStart, pEnd, '.'));
    int ms     = std::stoi(readToken(pStart, pEnd));
    TEST_END("not yet read actual data");
    time_t ts = mktime_utc(tm);
    pos.ts() = dyn.ts = double(ts) + double(ms) / 1000.0;
    
    // Call sign
    if (!(token = readToken(pStart, pEnd)).empty())
        stat.call = token;
    
    // Altitude, supposingly "HAE" = Height above ellipsoid
    if (!(token = readToken(pStart, pEnd)).empty())
        pos.SetAltFt(std::stod(token));

    // Ground Speed
    if (!(token = readToken(pStart, pEnd)).empty())
        dyn.spd = std::stod(token);
    
    // Track (it is not heading...but best we have, we don't get heading)
    if (!(token = readToken(pStart, pEnd)).empty())
        pos.heading() = dyn.heading = std::stod(token);
    
    // Latitude, Longitude
    if (!(token = readToken(pStart, pEnd)).empty())
        pos.lat() = std::stod(token);
    if (!(token = readToken(pStart, pEnd)).empty())
        pos.lon() = std::stod(token);

    // VSI
    if (!(token = readToken(pStart, pEnd)).empty())
        dyn.vsi = std::stod(token);

    // Squawk
    if (!(token = readToken(pStart, pEnd)).empty())
        dyn.radar.code = std::stol(token);

    // Skip over Alert, Emergency, SPI flags
    readToken(pStart, pEnd);
    readToken(pStart, pEnd);
    readToken(pStart, pEnd);

    // Ground flag
    if (!(token = readToken(pStart, pEnd)).empty()) {
        dyn.gnd = token == "1";
        pos.f.onGrnd = dyn.gnd ? GND_ON : GND_OFF;
    }
    
    return true;
}

// Process received Compressed VRS data
bool ADSBHubConnection::StreamProcessDataVRS (size_t num, const uint8_t* buffer)
{
    // Start / length of line
    const uint8_t* pStart = buffer;
    size_t len = 0;
    
    // Something left over from previous message?
    if (!lnLeftOver.empty()) {
        // add missing bytes from this current message
        len = size_t(lnLeftOver[0]);
        assert(len > lnLeftOver.length());
        const size_t lenMissing = len - lnLeftOver.length();
        
        // Current message doesn't have all the remainder??? (shouldn't happen...)
        if (lenMissing > num) {
            // add everything to the left-over-buffer...but then return and wait for next message
            lnLeftOver.append((const char*)buffer, num);
            return true;
        }
        
        // rest of line available now in current message
        lnLeftOver.append((const char*)buffer, lenMissing);
        if (!StreamProcessDataVRSLine((const uint8_t*)lnLeftOver.data()) && !IncErrCnt())
        {
            lnLeftOver.clear();             // too many errors -> bail
            return false;
        }
        lnLeftOver.clear();                 // we've processed the remainder just now
        
        // eat the remainder that we had just processed
        num -= lenMissing;
        pStart += lenMissing;
    }
    
    // Process all (complete) lines of C-VRS data
    len = size_t(pStart[0]);
    while (len <= num)
    {
        // Process the current line
        if (!StreamProcessDataVRSLine(pStart) && !IncErrCnt())
        {
            return false;                   // too many errors, bail
        }
        
        // advance to next line
        pStart += len;
        num -= len;
        if (num <= 0)                       // processed complete message -> break out
            break;
        len = size_t(pStart[0]);
    }
    
    // Anything left to remember for next turn?
    if (num > 0)
        lnLeftOver.append((const char*)pStart, num);
    else
        // If the entire network message _exactly_ ended with a full line then assume we're done completely and also process that last plane data
        ProcessPlaneData();

    // Success
    return true;
}

std::string VRSString (const uint8_t* &pStart)
{
    size_t sLen = size_t(pStart[0]);
    ++pStart;                           // eat the length byte
    const char* pS = (const char*)pStart;
    pStart += sLen;                     // eat the actual string
    return std::string(pS, sLen);
}

long VRSFloatInt (const uint8_t* &pStart)
{
    long l = long((unsigned long)(pStart[0]) << 16 |
                  (unsigned long)(pStart[1]) <<  8 |
                  (unsigned long)(pStart[2]));
    pStart += 3;                        // eat the number
    // test for negative value
    if (l & 0x800000) {
        l &= 0x7fffffL;                 // remove the negative flag
        l *= -1L;                       // make value negative
    }
    return l;
}

int VRSFloatShort (const uint8_t* &pStart)
{
    // this happens to just be a little endian signed 16 bit value with negative values as 1 complement
    int16_t i = *((const int16_t*)pStart);
    pStart += 2;
    return i;
}

float VRSFloat (const uint8_t* &pStart)
{
    float f = NAN;
    static_assert (sizeof(f) == 4);
    memcpy(&f, pStart, 4);              // a float is just a standard IEEE 32bit float
    pStart += 4;
    return f;
}

unsigned VRSShort (const uint8_t* &pStart)
{
    unsigned u = unsigned(pStart[1]) << 8 |
                 unsigned(pStart[0]);
    pStart += 2;
    return u;
}

/// @brief Process a single line of C-VRS data
/// @see https://www.virtualradarserver.co.uk/Documentation/Formats/CompressedVrs.aspx
bool ADSBHubConnection::StreamProcessDataVRSLine (const uint8_t* pStart)
{
    // end of line, based on fact that line length is stored in first byte
    const size_t len = size_t(pStart[0]);
    if (len < 10) {
        LOG_MSG(logDEBUG, "Ignoring too short a message of length %d", int(len));
        return false;
    }
    const uint8_t* const pEnd = pStart + len;
    
    // Skip over length, checksum, transmission type:
    pStart += 4;
    
    // next 3 bytes is the ADS-B hex id
    LTFlightData::FDKeyTy key (LTFlightData::KEY_ICAO,
                               (unsigned long)(pStart[0]) << 16 |
                               (unsigned long)(pStart[1]) <<  8 |
                               (unsigned long)(pStart[2]));
    pStart += 3;

    // Change of plane? Then process previous data first before continuing
    if (fdKey != key) {
        ProcessPlaneData();
        // New plane
        fdKey = key;
    }
    
    // remember list of fields and list of flags
    uint8_t fields = pStart[0];
    uint8_t flags  = pStart[1];
    pStart += 2;
    
    // --- process fields ---
    TEST_END("no fields processed")
    
    // Callsign
    if (fields & 0x01) {
        TEST_LEN(size_t(pStart[0]), "Call Sign");
        stat.call = VRSString(pStart);
    }

    // Altitude
    if (fields & 0x02) {
        TEST_LEN(3, "Altitude");
        pos.SetAltFt((double)VRSFloatInt(pStart));
    }
    
    // Ground Speed
    if (fields & 0x04) {
        TEST_LEN(2, "Altitude");
        dyn.spd = (double)VRSFloatShort(pStart);
    }
    
    // Track * 10.0
    if (fields & 0x08) {
        TEST_LEN(2, "Track");
        pos.heading() = dyn.heading = (double)VRSFloatShort(pStart) / 10.0;
    }
    
    // Latitude
    if (fields & 0x10) {
        TEST_LEN(4, "Latitude");
        pos.lat() = (double)VRSFloat(pStart);
    }
    
    // Longitude
    if (fields & 0x20) {
        TEST_LEN(4, "Longitude");
        pos.lon() = (double)VRSFloat(pStart);
    }
    
    // Vertical Speed
    if (fields & 0x40) {
        TEST_LEN(2, "VSI");
        dyn.vsi = (double)VRSFloatShort(pStart);
    }
    
    // Squawk
    if (fields & 0x80) {
        TEST_LEN(2, "Squawk");
        dyn.radar.code = (long)VRSShort(pStart);
    }

    // --- Flags ---
    if (flags) {
        TEST_LEN(1, "Flags");
        // Only one we are interested in is 'On ground'
        if (flags & 0x08) {
            pos.f.onGrnd =
                (pStart[0] & 0x08) ? GND_ON : GND_OFF;
        }
        ++pStart;
    }
    
    return true;
}

/// Add the collected data for a plane to LiveTraffic's FlightData and reset the internal buffers
void ADSBHubConnection::ProcessPlaneData ()
{
    // if no timestamp then assume "3s ago"
    if (std::isnan(pos.ts()))
        pos.ts() = GetSysTime() - 3.0;

    // Data collected?
    if (!fdKey.empty() && pos.isNormal(true))
    {
        // ADSBHub always sends _all_ data,
        // Filter data that the user doesn't want based on settings
        const positionTy viewPos = dataRefs.GetViewPos();
        const double dist = pos.dist(viewPos);
        if (dist < dataRefs.GetFdStdDistance_m() )
        {
            try {
                // from here on access to fdMap guarded by a mutex
                // until FD object is inserted and updated
                std::unique_lock<std::mutex> mapFdLock (mapFdMutex);

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
                
                // add the static data
                fd.UpdateData(stat, dist);

                // add the dynamic data
                dyn.pChannel = this;
                fd.AddDynData(dyn, 0, 0, &pos);

            } catch(const std::system_error& e) {
                LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
            }
        }
    }

    // Clear processed data
    fdKey.clear();
    stat = LTFlightData::FDStaticData();
    dyn = LTFlightData::FDDynamicData();
    pos = positionTy();
}


// Start or restart a new thread for connecting to ADSBHub
void ADSBHubConnection::StreamStart ()
{
    // If there is no TCP thread running we start one
    if (!thrStream.joinable()) {
        LOG_MSG(logDEBUG, "Starting ADSBHub thread...");
        eFormat = FMT_UNKNOWN;
        bStopThr = false;
        thrStream = std::thread(&ADSBHubConnection::StreamMain, this);
    }
    // else: Maybe the thread ended itself?
    else if (bStopThr) {
        thrStream.join();
        thrStream = std::thread();
        bStopThr = false;
    }
}

/// Closes the stream TCP connection
void ADSBHubConnection::StreamClose ()
{
    // Stop the separate TCP stream, thereby closing any TCP connection
    if (thrStream.joinable()) {
        if (!bStopThr) {
            LOG_MSG(logDEBUG, "Stopping ADSBHub thread...");
            bStopThr = true;
#if APL == 1 || LIN == 1
            // Mac/Lin: Try writing something to the self-pipe to stop gracefully
            if (streamPipe[1] == INVALID_SOCKET ||
                write(streamPipe[1], "STOP", 4) < 0)
            {
                // if the self-pipe didn't work:
                // close the connection, this will also break out of all
                // blocking calls for receiving message and hence terminate the threads
                tcpStream.Close();
            }
#else
            tcpStream.Close();
#endif
        }
        
        // wait for thread to finish if I'm not this thread myself
        if (std::this_thread::get_id() != thrStream.get_id()) {
            if (thrStream.joinable())
                thrStream.join();
            thrStream = std::thread();
            bStopThr = false;
        }
    }
}


size_t CBReadToStringBuf(char *ptr, size_t, size_t nmemb, void* userdata)
{
    // copy buffer to our std::string
    std::string& readBuf = *reinterpret_cast<std::string*>(userdata);
    readBuf.append(ptr, nmemb);
    return nmemb;
}

// Query https://api.ipify.org/ to get own public IP address
std::string GetPublicIPv4 ()
{
    char curl_errtxt[CURL_ERROR_SIZE];
    std::string readBuf;
    
    // initialize the CURL handle
    CURL *pCurl = curl_easy_init();
    if (!pCurl) {
        LOG_MSG(logERR,ERR_CURL_EASY_INIT);
        return "";
    }

    // Setup buffer and CURL handle
    readBuf.reserve(CURL_MAX_WRITE_SIZE);
    curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, 5);
    curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, curl_errtxt);
    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, CBReadToStringBuf);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &readBuf);
    curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    curl_easy_setopt(pCurl, CURLOPT_URL, "https://api.ipify.org/");
    curl_easy_setopt(pCurl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);

    // Perform the CURL call to read from the URL
    const CURLcode cc = curl_easy_perform(pCurl);
    if (cc == CURLE_OK) {
        // CURL was OK, now check HTTP response code
        long httpResponse = 0;
        curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);
        // Not HTTP_OK?
        if (httpResponse != HTTP_OK) {
            LOG_MSG(logERR, "HTPP response was not OK but %d", (int)httpResponse);
            readBuf.clear();
        }

    } else {
        LOG_MSG(logERR, "CURL call failed (%d)", (int)cc);
        readBuf.clear();
    }
    
    // cleanup CURL handle
    curl_easy_cleanup(pCurl);
    
    // return what we've read
    return readBuf;
}

/// @file       LTADSBHub.h
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

#ifndef LTADSBHub_h
#define LTADSBHub_h

#include "LTChannel.h"

//
//MARK: ADSBHub Constants
//

#define ADSBHUB_CHECK_NAME      "ADSBHub Coverage"
#define ADSBHUB_CHECK_URL       "https://www.adsbhub.org/coverage.php"
#define ADSBHUB_CHECK_POPUP     "Check ADSBHub's coverage"

//
// MARK: ADSBHubConnection
//

/// Connection to ADSBHub via TCP stream
class ADSBHubConnection : public LTFlightDataChannel
{
protected:
    XPMP2::TCPConnection tcpStream; ///< TCP connection to data.adsbhub.org:5002
#if APL == 1 || LIN == 1
    /// the self-pipe to shut down the TCP thread gracefully
    SOCKET streamPipe[2] = { INVALID_SOCKET, INVALID_SOCKET };
#endif
    
    /// Own public IP4 address, only filled if FMT_NULL_DATA
    std::string sPublicIPv4addr;
    
    /// ADSBHub format that we receive
    enum FormatTy : int {
        FMT_UNKNOWN = 0,            ///< (yet) unknown format
        FMT_SBS,                    ///< SBS format (CSV text-based)
        FMT_ComprVRS,               ///< Compressed VRS (binary)
        FMT_NULL_DATA,              ///< Received null data, indicative of wrong configuration
    } eFormat = FMT_UNKNOWN;        ///< ADSBHub format that we receive
    
    /// Time of last received data
    std::chrono::time_point<std::chrono::steady_clock> lastData;
    
    /// Incomplete line/data left over from previous message
    std::string lnLeftOver;
    
    // Plane data currently being collected
    LTFlightData::FDKeyTy fdKey;    ///< ADS-B hex id currently being processed
    LTFlightData::FDStaticData stat;///< static data of plane currently being processed
    LTFlightData::FDDynamicData dyn;///< dynamic data of plane currently being processed
    positionTy pos;                 ///< position of plane currently being processed
    
public:
    /// Constructor
    ADSBHubConnection ();
    /// No URL involved
    std::string GetURL (const positionTy&) override { return ""; }
    /// @brief Processes the fetched data
    bool ProcessFetchedData () override { return true; };
    std::string GetStatusText () const override;  ///< return a human-readable staus
    bool FetchAllData(const positionTy&) override { return false; }
    void Stop (bool bWaitJoin) override;        ///< Stop the TCP stream gracefully

    // ADSBHub Stream connection
protected:
    void Main () override;          ///< virtual thread main function

    /// Process received SBS data
    bool StreamProcessDataSBS (size_t num, const char* buffer);
    /// Process a single line of SBS data
    bool StreamProcessDataSBSLine (const char* pStart, const char* pEnd);
    /// Process received VRS data
    bool StreamProcessDataVRS (size_t num, const uint8_t* buffer);
    /// Process a single line of C-VRS data
    bool StreamProcessDataVRSLine (const uint8_t* pStart);

    /// Add the collected data for a plane to LiveTraffic's FlightData and reset the internal buffers
    void ProcessPlaneData ();
};

/// @brief Query https://api.ipify.org/ to get own public IP address
/// @note Blocking call! Should be quick...but don't call too often
std::string GetPublicIPv4 ();

#endif /* LTADSBHub_h */

//
//  LTADSBEx.h
//  LiveTraffic
//

/*
 * Copyright (c) 2019, Birger Hoppe
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

#ifndef LTADSBEx_h
#define LTADSBEx_h

#include "LTChannel.h"

//MARK: ADS-B Exchange Constants
#define ADSBEX_NAME             "ADSB Exchange Live Online"
#define ADSBEX_URL_ALL          "https://public-api.adsbexchange.com/VirtualRadar/AircraftList.json?lat=%f&lng=%f&fDstU=%d"
#define ADSBEX_URL_AC           "https://public-api.adsbexchange.com/VirtualRadar/AircraftList.json?fIcoQ=%s"
#define ADSBEX_TIME             "stm"
#define ADSBEX_AIRCRAFT_ARR     "acList"
#define ADSBEX_TRANSP_ICAO      "Icao"          // Key data
#define ADSBEX_TRT              "Trt"
#define ADSBEX_RCVR             "Rcvr"
#define ADSBEX_SIG              "Sig"
#define ADSBEX_RADAR_CODE       "Sqk"           // Dynamic data
#define ADSBEX_CALL             "Call"
#define ADSBEX_C_MSG            "CMsgs"
#define ADSBEX_LAT              "Lat"
#define ADSBEX_LON              "Long"
#define ADSBEX_ELEVATION        "GAlt"
#define ADSBEX_HEADING          "Trak"
#define ADSBEX_GND              "Gnd"
#define ADSBEX_IN_HG            "InHg"
#define ADSBEX_POS_TIME         "PosTime"
#define ADSBEX_POS_STALE        "PosStale"
#define ADSBEX_BRNG             "Brng"
#define ADSBEX_DST              "Dst"
#define ADSBEX_SPD              "Spd"
#define ADSBEX_VSI              "Vsi"
#define ADSBEX_REG              "Reg"
#define ADSBEX_COUNTRY          "Cou"
#define ADSBEX_AC_TYPE_ICAO     "Type"
#define ADSBEX_MAN              "Man"
#define ADSBEX_MDL              "Mdl"
#define ADSBEX_YEAR             "Year"
#define ADSBEX_MIL              "Mil"
#define ADSBEX_OP               "Op"
#define ADSBEX_OP_ICAO          "OpIcao"
#define ADSBEX_COS              "Cos"               // array of short trails
#define ADSBEX_ENG_TYPE         "EngType"
#define ADSBEX_ENG_MOUNT        "EngMount"
#define ADSBEX_ORIGIN           "From"
#define ADSBEX_DESTINATION      "To"

//
//MARK: ADS-B Exchange
//
class ADSBExchangeConnection : public LTOnlineChannel, LTFlightDataChannel
{
public:
    ADSBExchangeConnection () :
    LTChannel(DR_CHANNEL_ADSB_EXCHANGE_ONLINE),
    LTOnlineChannel(),
    LTFlightDataChannel()  {}
    virtual std::string GetURL (const positionTy& pos);
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
    virtual bool IsLiveFeed() const { return true; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual const char* ChName() const { return ADSBEX_NAME; }
    virtual bool FetchAllData(const positionTy& pos) { return LTOnlineChannel::FetchAllData(pos); }
};


//MARK: ADS-B Exchange (Historic) Constants
#define ADSBEX_HIST_NAME        "ADSB Exchange Historic File"
constexpr int ADSBEX_HIST_MIN_CHARS   = 20;             // minimum nr chars per line to be a 'reasonable' line
constexpr int ADSBEX_HIST_MAX_ERR_CNT = 5;              // after that many errorneous line we stop reading
#define ADSBEX_HIST_PATH        "Custom Data/ADSB"  // TODO: Move to options: relative to XP main
#define ADSBEX_HIST_PATH_2      "Custom Data/ADSB2" // TODO: Move to options: fallback, if first one doesn't work
#define ADSBEX_HIST_DATE_PATH   "%c%04d-%02d-%02d"
#define ADSBEX_HIST_FILE_NAME   "%c%04d-%02d-%02d-%02d%02dZ.json"
#define ADSBEX_HIST_PATH_EMPTY  "Historic Data Path doesn't exist or folder empty at %s"
#define ADSBEX_HIST_TRY_FALLBACK "Trying fallback as primary Historic Data Path doesn't exist or folder empty at %s"
#define ADSBEX_HIST_FALLBACK_EMPTY  "Also fallback Historic Data Path doesn't exist or folder empty at %s"
#define ADSBEX_HIST_FILE_ERR    "Could not open historic file '%s': %s"
#define ADSBEX_HIST_READ_FILE   "Reading from historic file %s"
#define ADSBEX_HIST_LN1_END     "\"acList\":["      // end of first line
#define ADSBEX_HIST_LAT         "\"Lat\":"          // latitude tag
#define ADSBEX_HIST_LONG        "\"Long\":"         // longitude tag
#define ADSBEX_HIST_COS         "\"Cos\":["         // start of short trails
#define ADSBEX_HIST_LAST_LN     "]"                 // begin of last line
#define ADSBEX_HIST_LN1_UNEXPECT "First line doesn't look like hist file: %s"
#define ADSBEX_HIST_LN_ERROR    "Error reading line %d of hist file %s"
#define ADSBEX_HIST_TRAIL_ERR   "Trail data not quadrupels (%s @ %f)"
#define ADSBEX_HIST_START_FILE  "START OF FILE "
#define ADSBEX_HIST_END_FILE    "END OF FILE "

//
//MARK: ADS-B Exchange Historical Data
//
class ADSBExchangeHistorical : public LTFileChannel, LTFlightDataChannel
{
    // helper type to select best receiver per a/c from multiple in one file
    struct FDSelection
    {
        int quality;                // quality value
        std::string ln;             // line of flight data from file
    };
    
    typedef std::map<std::string, FDSelection> mapFDSelectionTy;
    
public:
    ADSBExchangeHistorical (std::string base = ADSBEX_HIST_PATH,
                            std::string fallback = ADSBEX_HIST_PATH_2);
    virtual bool FetchAllData (const positionTy& pos);
    virtual bool IsLiveFeed() const { return false; }
    virtual LTChannelType GetChType() const { return CHT_TRACKING_DATA; }
    virtual const char* ChName() const { return ADSBEX_HIST_NAME; }
    virtual bool ProcessFetchedData (mapLTFlightDataTy& fdMap);
};


#endif /* LTADSBEx_h */

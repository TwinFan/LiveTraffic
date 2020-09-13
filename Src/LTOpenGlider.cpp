/// @file       LTOpenSky.cpp
/// @brief      OpenSky Network: Requests and processes live tracking and aircraft master data
/// @see        https://opensky-network.org/
/// @details    Implements OpenSkyConnection and OpenSkyAcMasterdata:\n
///             - Provides a proper REST-conform URL\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
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

//
// MARK: Open Glider Network
//

// log messages
#define ERR_OGN_XLM_END_MISSING     "OGN response malformed, end of XML element missing: %s"
#define ERR_OGN_WRONG_NUM_FIELDS    "OGN response contains wrong number of fields: %s"

constexpr const char* OGN_MARKER_BEGIN = "<m a=\""; ///< beginning of a marker in the XML response
constexpr const char* OGN_MARKER_END   = "\"/>";    ///< end of a marker in the XML response
constexpr size_t OGN_MARKER_BEGIN_LEN = 6;          ///< strlen(OGN_MARKER_BEGIN)

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

// put together the URL to fetch based on current view position
std::string OpenGliderConnection::GetURL (const positionTy& pos)
{
    boundingBoxTy box (pos, dataRefs.GetFdStdDistance_m());
    char url[128] = "";
    snprintf(url, sizeof(url),
             OPGLIDER_URL,
             box.nw.lat(),              // lamax
             box.se.lat(),              // lamin
             box.se.lon(),              // lomax
             box.nw.lon());             // lomin
    return std::string(url);
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
        std::vector<std::string> tok = str_tokenize(sMarker, ",");
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
        
        // the key: flarm id (which is persistent, but not always included),
        //          or alternatively the OGN id (which is assigned daily, so good enough to track a flight)
        LTFlightData::FDKeyTy fdKey (tok[GNF_FLARM_DEVICE_ID].size() == 6 ? LTFlightData::KEY_FLARM  : LTFlightData::KEY_OGN,
                                     tok[GNF_FLARM_DEVICE_ID].size() == 6 ? tok[GNF_FLARM_DEVICE_ID] : tok[GNF_OGN_REG_ID]);
        
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
            
            // completely new? fill key fields
            if ( fd.empty() )
                fd.SetKey(fdKey);
            
            // fill static data
            {
                LTFlightData::FDStaticData stat;
                
                // Call Sign: We use the CN, don't have a proper call sign,
                // makes it easier to match a/c to what live.glidernet.org shows
                stat.call = tok[GNF_CN];
                // We assume that GNF_REG holds a (more or less)
                // proper reg in case it is not just the generated OGN_REG_ID
                if (tok[GNF_REG] != tok[GNF_OGN_REG_ID])
                    stat.reg = tok[GNF_REG];
                else
                    // otherwise (again) the CN
                    stat.reg = tok[GNF_CN];
                // Aircraft type converted from Flarm AcftType
                FlarmAircraftTy acTy = (FlarmAircraftTy)clamp<int>(std::stoi(tok[GNF_FLARM_ACFT_TYPE]),
                                                                   FAT_UNKNOWN, FAT_STATIC_OBJ);
                stat.acTypeIcao = OGNGetIcaoAcType(acTy);
                stat.catDescr   = OGNGetAcTypeName(acTy);
                stat.man        = "-";
                stat.mdl        = stat.catDescr;

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

//
// MARK: Global Functions
//

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
std::string OGNGetIcaoAcType (FlarmAircraftTy _acTy)
{
    // TODO: Make this user-configurable
    switch (_acTy) {
        case FAT_UNKNOWN:
        case FAT_GLIDER:        return "GLID";
        case FAT_TOW_PLANE:     return "PA25";
        case FAT_HELI_ROTOR:    return "EC35";
        case FAT_PARACHUTE:     return "ULAC";
        case FAT_DROP_PLANE:    return "C208";
        case FAT_HANG_GLIDER:
        case FAT_PARA_GLIDER:   return "ULAC";
        case FAT_POWERED_AC:    return "C172";
        case FAT_JET_AC:        return "C510";
        case FAT_UFO:
        case FAT_BALLOON:       return "BALL";
        case FAT_AIRSHIP:       return "SHIP";
        case FAT_UAV:           return "ULAC";
        case FAT_STATIC_OBJ:    return dataRefs.GetDefaultCarIcaoType();
    }
    return dataRefs.GetDefaultAcIcaoType();
}

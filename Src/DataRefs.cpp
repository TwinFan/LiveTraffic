/// @file       DataRefs.cpp
/// @brief      Access to global data like dataRefs, `doc8643` and `model_typecode` files.
/// @details    Implements classes Doc8643, DataRefs, and read access to `model_typecode` file.\n
///             There is exactly one instance of DataRefs, which is the global variable `dataRefs`,
///             in which all globally relevant values are stored, beyond just XP's dataRefs:\n
///             - LiveTraffic's configuration options including reading/writing of the config file\n
///             - readable callbacks for other plugins' access to LiveTraffic's data
///             - LTAPI interface
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

#include "LiveTraffic.h"

#include <fstream>
#include <errno.h>

//
//MARK: external references
//

// return color into a RGB array as XP likes it
void conv_color (int inCol, float outColor[4])
{
    outColor[0] = float((inCol & 0xFF0000) >> 16) / 255.0f;    // red
    outColor[1] = float((inCol & 0x00FF00) >>  8) / 255.0f;    // green
    outColor[2] = float((inCol & 0x0000FF)      ) / 255.0f;    // blue
    outColor[3] = 1.0f;
}



//
// MARK: Doc8643
//

// global map, which stores the content of the doc8643 file
std::map<std::string, Doc8643> mapDoc8643;
const Doc8643 DOC8643_EMPTY;    // objet returned if Doc8643::get fails

// constructor setting all elements
Doc8643::Doc8643 (std::string&& _manufacturer,
                  std::string&& _model,
                  std::string&& _typeDesignator,
                  std::string&& _classification,
                  std::string&& _wtc) :
manufacturer    (std::move(_manufacturer)),
model           (std::move(_model)),
typeDesignator  (std::move(_typeDesignator)),
classification  (std::move(_classification)),
wtc             (std::move(_wtc))
{}

// return the string for FlightModel matching
Doc8643::operator std::string() const
{
    return wtc + ';' + classification + ';' + typeDesignator + ';' +
    model + ';' + manufacturer;
}

//
// Static functions
//

// reads the Doc8643 file into mapDoc8643
bool Doc8643::ReadDoc8643File ()
{
    // clear the map, just in case
    mapDoc8643.clear();
    
    // Put together path to Doc8643.txt
    std::string path (LTCalcFullPluginPath(PATH_DOC8643_TXT));
    
    // open the file for reading
    std::ifstream fIn (path);
    if (!fIn) {
        // if there is no config file output a warning (we can use defaults)
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                 path.c_str(), sErr);
        return false;
    }
    
    // regular expression to extract individual values, separated by TABs
    enum { DOC_MANU=1, DOC_MODEL, DOC_TYPE, DOC_CLASS, DOC_WTC, DOC_EXPECTED };
    const std::regex re("^([^\\t]+)\\t"                   // manufacturer
                        "([^\\t]+)\\t"                    // model
                        "([[:alnum:]]{2,4})\\t"           // type designator
                        "(-|[AGHLST][C1-8][EJPRT])\\t"    // classification
                        "(-|[HLM]|L/M)");                 // wtc

    // loop over lines of the file
    std::string text;
    int errCnt = 0;
    for (int ln=1; fIn && errCnt <= ERR_CFG_FILE_MAXWARN; ln++) {
        // read entire line
        safeGetline(fIn, text);
        if (text.empty())           // skip empty lines silently
            continue;
        
        // apply the regex to extract values
        std::smatch m;
        std::regex_search(text, m, re);
        
        // add to map (if matched)
        if (m.size() == DOC_EXPECTED) {
            mapDoc8643.emplace(m[DOC_TYPE],
                               Doc8643(m[DOC_MANU],
                                       m[DOC_MODEL],
                                       m[DOC_TYPE],
                                       m[DOC_CLASS],
                                       m[DOC_WTC]));
        } else if (fIn) {
            // I/O was good, but line didn't match
            SHOW_MSG(logWARN, ERR_CFG_LINE_READ,
                     path.c_str(), ln, text.c_str());
            errCnt++;
        } else if (!fIn && !fIn.eof()) {
            // I/O error
			char sErr[SERR_LEN];
			strerror_s(sErr, sizeof(sErr), errno);
			SHOW_MSG(logWARN, ERR_CFG_LINE_READ,
                     path.c_str(), ln, sErr);
            errCnt++;
        }
    }
    
    // close file
    fIn.close();
    
    // too many warnings?
    if (errCnt > ERR_CFG_FILE_MAXWARN) {
        SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                 path.c_str(), ERR_CFG_FILE_TOOMANY);
        return false;
    }
    
    // looks like success
    return true;
}

// return the matching Doc8643 object from the global map
const Doc8643& Doc8643::get (const std::string& _type)
try
{
    return mapDoc8643.at(_type);
}
catch (...)
{
    return DOC8643_EMPTY;
}

//
// MARK: ModelIcaoType
//

namespace ModelIcaoType
{
    /// Map, which stores the content of the model_typecode.txt file:
    /// human-readable model text maps to ICAO type cide
    std::map<std::string, std::string> mapModelIcaoType;
    
    /// global empty string returned if nothing is found in map
    const std::string gEmptyString;

    // Read the `model_typecode.txt` file
    bool ReadFile ()
    {
        // clear the map
        mapModelIcaoType.clear();
        
        // Put together path to model_typecode.txt
        std::string path (LTCalcFullPluginPath(PATH_MODEL_TYPECODE_TXT));
        
        // open the file for reading
        std::ifstream fIn (path);
        if (!fIn) {
            // if there is no config file output a warning, but otherwise no worries
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                     path.c_str(), sErr);
            return false;
        }
        
        // loop over lines of the file
        std::string text;
        int errCnt = 0;
        for (int ln=1; fIn && errCnt <= ERR_CFG_FILE_MAXWARN; ln++) {
            // read entire line
            safeGetline(fIn, text);
            if (text.empty())           // skip empty lines silently
                continue;
            
            // ignore the first line, if it is just the column header
            if (ln == 1 && text == "MODEL|TYPECODE")
                continue;
            
            // Elements model and type code are separate by pipe.
            // Find the last pipe in line (just in case there happens to a pipe in the model name, which is the first field)
            const std::string::size_type pos = text.rfind('|');
            // not found a pipe?
            if (pos == std::string::npos ||
                // pipe is first or last character? not good either as either string would be empty then
                pos == 0 || pos == text.size()-1) {
                // I/O was good, but line has wrong format
                SHOW_MSG(logWARN, ERR_CFG_LINE_READ,
                         path.c_str(), ln, text.c_str());
                errCnt++;
            }
            
            // everything to the left is mode text
            std::string mdl = text.substr(0,pos);
            str_toupper(trim(mdl));
            // everything to the right is type code, which should exist
            // (but if not we use it anyway...)
            std::string type = text.substr(pos+1);
            str_toupper(trim(type));
#if TEST_MODEL_TYPE_CODES
            // quite many invalid codes are in the file, which originates
            // in the OpenSky aircraft database...we don't warn about all of them
            if (Doc8643::get(type) == DOC8643_EMPTY) {
                LOG_MSG(logWARN, ERR_CFG_TYPE_INVALID,
                        path.c_str(), ln,
                        type.c_str());
            }
#endif
            
            // and just move it to the map:
            mapModelIcaoType.emplace(std::move(mdl),
                                     std::move(type));
        }

        // close file
        fIn.close();
        
        // too many warnings?
        if (errCnt > ERR_CFG_FILE_MAXWARN) {
            SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                     path.c_str(), ERR_CFG_FILE_TOOMANY);
            return false;
        }
        
        // looks like success
        return true;
    }
    
    // Lookup ICAO type designator for human-readable model text, empty if nothing found
    const std::string& getIcaoType (const std::string& _model)
    {
        try {
            // lookup a value on the map, throws std::out_of_range if nothing found
            return mapModelIcaoType.at(_model);
        }
        catch (...) {
            // caught exception -> nothing found in map, return something empty
            return gEmptyString;
        }
    }

}

//MARK: X-Plane Datarefs
const char* DATA_REFS_XP[] = {
    "sim/network/misc/network_time_sec",        // float	n	seconds	The current elapsed time synched across the network (used as timestamp in Log.txt)
    "sim/time/local_time_sec",
    "sim/time/local_date_days",
    "sim/time/use_system_time",
    "sim/time/zulu_time_sec",
    "sim/flightmodel/position/lat_ref",         // float    n    degrees    The latitude of the point 0,0,0 in OpenGL coordinates
    "sim/flightmodel/position/lon_ref",         // float    n    degrees    The longitude of the point 0,0,0 in OpenGL coordinates"
    "sim/graphics/view/view_is_external",
    "sim/graphics/view/view_type",
    "sim/graphics/view/using_modern_driver",    // boolean: Vulkan/Metal in use? (since XP11.50)
    "sim/weather/barometer_sealevel_inhg",      // float  y    29.92    +- ....        The barometric pressure at sea level.
    "sim/weather/use_real_weather_bool",        // int    y    0,1    Whether a real weather file is in use."
    "sim/flightmodel/position/latitude",
    "sim/flightmodel/position/longitude",
    "sim/flightmodel/position/elevation",
    "sim/flightmodel/position/true_theta",
    "sim/flightmodel/position/true_phi",
    "sim/flightmodel/position/true_psi",
    "sim/flightmodel/position/hpath",
    "sim/flightmodel/position/true_airspeed",
    "sim/flightmodel/failures/onground_any",
    "sim/graphics/VR/enabled",
    "sim/graphics/view/pilots_head_x",
    "sim/graphics/view/pilots_head_y",
    "sim/graphics/view/pilots_head_z",
    "sim/graphics/view/pilots_head_psi",
    "sim/graphics/view/pilots_head_the",
    "sim/graphics/view/pilots_head_phi",
};

static_assert(sizeof(DATA_REFS_XP) / sizeof(DATA_REFS_XP[0]) == CNT_DATAREFS_XP,
    "dataRefsXP and DATA_REFS_XP[] differ in number of elements");

//MARK: X-Plane Command Refs
const char* CMD_REFS_XP[] = {
    "sim/general/left",
    "sim/general/right",
    "sim/general/left_fast",
    "sim/general/right_fast",
    "sim/general/forward",
    "sim/general/backward",
    "sim/general/forward_fast",
    "sim/general/backward_fast",
    "sim/general/hat_switch_left",
    "sim/general/hat_switch_right",
    "sim/general/hat_switch_up",
    "sim/general/hat_switch_down",
    "sim/general/hat_switch_up_left",
    "sim/general/hat_switch_up_right",
    "sim/general/hat_switch_down_left",
    "sim/general/hat_switch_down_right",
    "sim/general/up",
    "sim/general/down",
    "sim/general/up_fast",
    "sim/general/down_fast",
    "sim/general/rot_left",
    "sim/general/rot_right",
    "sim/general/rot_left_fast",
    "sim/general/rot_right_fast",
    "sim/general/rot_up",
    "sim/general/rot_down",
    "sim/general/rot_up_fast",
    "sim/general/rot_down_fast",
    "sim/general/zoom_in",
    "sim/general/zoom_out",
    "sim/general/zoom_in_fast",
    "sim/general/zoom_out_fast",
};

static_assert(sizeof(CMD_REFS_XP) / sizeof(CMD_REFS_XP[0]) == CNT_CMDREFS_XP,
    "cmdRefsXP and CMD_REFS_XP[] differ in number of elements");

//
//MARK: DataRefs::dataRefDefinitionT
//

// constant used in dataRefDefinitionT::refCon but denoting to query the address of the respective variable
void* GET_VAR = reinterpret_cast<void*>(INT_MIN);

// list of all datRef definitions offered by LiveTraffic:
DataRefs::dataRefDefinitionT DATA_REFS_LT[CNT_DATAREFS_LT] = {
    // a/c information
    {"livetraffic/ac/key",                          DataRefs::LTGetAcInfoI, DataRefs::LTSetAcKey,   (void*)DR_AC_KEY, false },
    {"livetraffic/ac/num",                          DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_NUM, false },
    {"livetraffic/ac/on_gnd",                       DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_ON_GND, false },
    {"livetraffic/ac/phase",                        DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_PHASE, false },
    {"livetraffic/ac/lat",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_LAT, false },
    {"livetraffic/ac/lon",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_LON, false },
    {"livetraffic/ac/alt",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_ALT, false },
    {"livetraffic/ac/heading",                      DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_HEADING, false },
    {"livetraffic/ac/roll",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_ROLL, false },
    {"livetraffic/ac/pitch",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_PITCH, false },
    {"livetraffic/ac/speed",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_SPEED, false },
    {"livetraffic/ac/vsi",                          DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_VSI, false },
    {"livetraffic/ac/terrain_alt",                  DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_TERRAIN_ALT, false },
    {"livetraffic/ac/height",                       DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_HEIGHT, false },
    {"livetraffic/ac/flaps",                        DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_FLAPS, false },
    {"livetraffic/ac/gear",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_GEAR, false },
    {"livetraffic/ac/lights/beacon",                DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_BEACON, false },
    {"livetraffic/ac/lights/strobe",                DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_STROBE, false },
    {"livetraffic/ac/lights/nav",                   DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_NAV, false },
    {"livetraffic/ac/lights/landing",               DataRefs::LTGetAcInfoI, NULL,                   (void*)DR_AC_LIGHTS_LANDING, false },
    {"livetraffic/ac/bearing",                      DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_BEARING, false },
    {"livetraffic/ac/dist",                         DataRefs::LTGetAcInfoF, NULL,                   (void*)DR_AC_DIST, false },
    
    {"livetraffic/bulk/quick",                      DataRefs::LTGetBulkAc,  NULL,                   (void*)DR_AC_BULK_QUICK, false },
    {"livetraffic/bulk/expensive",                  DataRefs::LTGetBulkAc,  NULL,                   (void*)DR_AC_BULK_EXPENSIVE, false },

    {"livetraffic/sim/date",                        DataRefs::LTGetSimDateTime, DataRefs::LTSetSimDateTime, (void*)1, false },
    {"livetraffic/sim/time",                        DataRefs::LTGetSimDateTime, DataRefs::LTSetSimDateTime, (void*)2, false },

    {"livetraffic/ver/nr",                          GetLTVerNum,  NULL, NULL, false },
    {"livetraffic/ver/date",                        GetLTVerDate, NULL, NULL, false },

    // configuration options
    {"livetraffic/cfg/aircrafts_displayed",         DataRefs::LTGetInt, DataRefs::LTSetAircraftDisplayed, GET_VAR, false },
    {"livetraffic/cfg/auto_start",                  DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/ai_on_request",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/ai_controlled",               DataRefs::HaveAIUnderControl, NULL,             NULL,    false },
    {"livetraffic/cfg/labels",                      DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_shown",                 DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_col_dyn",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/label_color",                 DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/log_level",                   DataRefs::LTGetInt, DataRefs::LTSetLogLevel,    GET_VAR, true },
    {"livetraffic/cfg/msg_area_level",              DataRefs::LTGetInt, DataRefs::LTSetLogLevel,    GET_VAR, true },
    {"livetraffic/cfg/use_historic_data",           DataRefs::LTGetInt, DataRefs::LTSetUseHistData, GET_VAR, false },
    {"livetraffic/cfg/max_num_ac",                  DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/fd_std_distance",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/fd_snap_taxi_dist",           DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/fd_refresh_intvl",            DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/fd_buf_period",               DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/ac_outdated_intvl",           DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/network_timeout",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/lnd_lights_taxi",             DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/hide_below_agl",              DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/hide_taxiing",                DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/cfg/last_check_new_ver",          DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },

    // debug options
    {"livetraffic/dbg/ac_filter",                   DataRefs::LTGetInt, DataRefs::LTSetDebugAcFilter, GET_VAR, false },
    {"livetraffic/dbg/ac_pos",                      DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/dbg/log_raw_fd",                  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/dbg/model_matching",              DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    
    // channel configuration options
    {"livetraffic/channel/real_traffic/listen_port",DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/real_traffic/traffic_port",DataRefs::LTGetInt,DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/real_traffic/weather_port",DataRefs::LTGetInt,DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/fore_flight/send_port",   DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },
    {"livetraffic/channel/fore_flight/user_plane",  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/fore_flight/traffic",     DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/fore_flight/interval",    DataRefs::LTGetInt, DataRefs::LTSetCfgValue,    GET_VAR, true },

    // channels, in ascending order of priority
    {"livetraffic/channel/futuredatachn/online",    DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/channel/fore_flight/sender",      DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/open_glider/online",      DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/adsb_exchange/online",    DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/adsb_exchange/historic",  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, false },
    {"livetraffic/channel/open_sky/online",         DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/open_sky/ac_masterdata",  DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
    {"livetraffic/channel/real_traffic/online",     DataRefs::LTGetInt, DataRefs::LTSetBool,        GET_VAR, true },
};

// returns the actual address of the variable within DataRefs, which stores the value of interest as per dataRef definition
// (called in case dataRefDefinitionT::refCon == GET_VAR)
void* DataRefs::getVarAddr (dataRefsLT dr)
{
    switch (dr) {
        // configuration options
        case DR_CFG_AIRCRAFT_DISPLAYED:    return &bShowingAircraft;
        case DR_CFG_AUTO_START:             return &bAutoStart;
        case DR_CFG_AI_ON_REQUEST:          return &bAIonRequest;
        case DR_CFG_LABELS:                 return &labelCfg;
        case DR_CFG_LABEL_SHOWN:            return &labelShown;
        case DR_CFG_LABEL_COL_DYN:          return &bLabelColDynamic;
        case DR_CFG_LABEL_COLOR:            return &labelColor;
        case DR_CFG_LOG_LEVEL:              return &iLogLevel;
        case DR_CFG_MSG_AREA_LEVEL:         return &iMsgAreaLevel;
        case DR_CFG_USE_HISTORIC_DATA:      return &bUseHistoricData;
        case DR_CFG_MAX_NUM_AC:             return &maxNumAc;
        case DR_CFG_FD_STD_DISTANCE:        return &fdStdDistance;
        case DR_CFG_FD_SNAP_TAXI_DIST:      return &fdSnapTaxiDist;
        case DR_CFG_FD_REFRESH_INTVL:       return &fdRefreshIntvl;
        case DR_CFG_FD_BUF_PERIOD:          return &fdBufPeriod;
        case DR_CFG_AC_OUTDATED_INTVL:      return &acOutdatedIntvl;
        case DR_CFG_NETW_TIMEOUT:           return &netwTimeout;
        case DR_CFG_LND_LIGHTS_TAXI:        return &bLndLightsTaxi;
        case DR_CFG_HIDE_BELOW_AGL:         return &hideBelowAGL;
        case DR_CFG_HIDE_TAXIING:           return &hideTaxiing;
        case DR_CFG_LAST_CHECK_NEW_VER:     return &lastCheckNewVer;

        // debug options
        case DR_DBG_AC_FILTER:              return &uDebugAcFilter;
        case DR_DBG_AC_POS:                 return &bDebugAcPos;
        case DR_DBG_LOG_RAW_FD:             return &bDebugLogRawFd;
        case DR_DBG_MODEL_MATCHING:         return &bDebugModelMatching;
            
        // channel configuration options
        case DR_CFG_RT_LISTEN_PORT:         return &rtListenPort;
        case DR_CFG_RT_TRAFFIC_PORT:        return &rtTrafficPort;
        case DR_CFG_RT_WEATHER_PORT:        return &rtWeatherPort;
        case DR_CFG_FF_SEND_PORT:           return &ffSendPort;
        case DR_CFG_FF_SEND_USER_PLANE:     return &bffUserPlane;
        case DR_CFG_FF_SEND_TRAFFIC:        return &bffTraffic;
        case DR_CFG_FF_SEND_TRAFFIC_INTVL:  return &ffSendTrfcIntvl;

        default:
            // flight channels
            if (DR_CHANNEL_FIRST <= dr && dr <= DR_CHANNEL_LAST)
                return &bChannel[dr-DR_CHANNEL_FIRST];
            
            // else: must not happen
            LOG_ASSERT(NULL);
            return NULL;
    }
}

//MARK: LiveTraffic Command Refs
struct cmdRefDescrTy {
    const char* cmdName;
    const char* cmdDescr;
} CMD_REFS_LT[] = {
    {"LiveTraffic/Aircraft_Info_Wnd/Open",              "Opens an aircraft information window"},
    {"LiveTraffic/Aircraft_Info_Wnd/Open_Popped_Out",   "Opens a popped out aircraft information window (separate OS-level window)"},
    {"LiveTraffic/Aircraft_Info_Wnd/Hide_Show",         "Hides/Shows all aircraft information windows, but does not close"},
    {"LiveTraffic/Aircraft_Info_Wnd/Close_All",         "Closes all aircraft information windows"},
    {"LiveTraffic/Aircrafts/Display",                   "Starts/Stops display of live aircraft"},
    {"LiveTraffic/Aircrafts/TCAS_Control",              "TCAS Control: Tries to take control over AI aircraft"},
    {"LiveTraffic/Aircrafts/Toggle_Labels",             "Toggle display of labels in current view"},
};

static_assert(sizeof(CMD_REFS_LT) / sizeof(CMD_REFS_LT[0]) == CNT_CMDREFS_LT,
              "cmdRefsLT and CMD_REFS_LT[] differ in number of elements");

// returns offset to UTC in seconds
// https://stackoverflow.com/questions/13804095/get-the-time-zone-gmt-offset-in-c
int timeOffsetUTC()
{
	static int cachedOffset = INT_MIN;

	if (cachedOffset > INT_MIN)
		return cachedOffset;
	else {
		time_t gmt, rawtime = time(NULL);
		struct tm gbuf;
		gmtime_s(&gbuf, &rawtime);

        // Request that mktime() looksup dst in timezone database
		gbuf.tm_isdst = -1;
		gmt = mktime(&gbuf);

		return cachedOffset = (int)difftime(rawtime, gmt);
	}
}

//MARK: Constructor - just plain variable init, no API calls
DataRefs::DataRefs ( logLevelTy initLogLevel ) :
iLogLevel (initLogLevel)
#ifdef DEBUG
,bDebugAcPos (true)
#endif
{
    // override log level in Beta and DEBUG cases
    // (config file is read later, that may reduce the level again)
#ifdef DEBUG
    iLogLevel = logDEBUG;
#else
    if ( LT_BETA_VER_LIMIT )
        iLogLevel = logDEBUG;
#endif
    
    // disable all channels
    for ( int& i: bChannel )
        i = false;

    // enable OpenSky and ADSBEx by default
    SetChannelEnabled(DR_CHANNEL_OPEN_SKY_ONLINE, true);
    SetChannelEnabled(DR_CHANNEL_OPEN_SKY_AC_MASTERDATA, true);

    // Clear the dataRefs arrays
    memset ( adrXP, 0, sizeof(adrXP));
    memset ( adrLT, 0, sizeof(adrLT));
    
    // X-Plane only knows about day in the year, but not the actual year
    // so we assume something:
    //   if LocalDateDays < today's day of year --> current year
    //   if LocalDateDays >= today's day of year --> previous year
    {
        std::tm tm;
        time_t now = time(nullptr);
        localtime_s(&tm, &now);

        dataRefs.iTodaysDayOfYear = tm.tm_yday;
        
        // also compute start of this and last year for sim-time computations
        tm.tm_sec = tm.tm_min = tm.tm_hour = 0;     // 00:00:00h
        tm.tm_mday = 1;                             // 1st of
        tm.tm_mon = 0;                              // January
        tm.tm_isdst = 0;                            // no DST
        tStartThisYear = mktime(&tm);
        
        // now adjust for timezone: current value is midnight as per local time
        // but for our calculations we need midnight UTC
        tStartThisYear += timeOffsetUTC();
        
        // previous year
        tm.tm_year--;
        tStartPrevYear = mktime(&tm);
        tStartPrevYear += timeOffsetUTC();
    }
}

// Find and register dataRefs
bool DataRefs::Init ()
{
    // initialize XP compatibility proxy functions
    if (!XPC_Init())
        return false;
    
    // XP System Path
    char aszPath[512];
    XPLMGetSystemPath ( aszPath );
    XPSystemPath = aszPath;
    
    // Directory Separator provided by XP
    DirSeparator = XPLMGetDirectorySeparator();
    
    // my own plugin path
    pluginID = XPLMGetMyID();
    aszPath[0] = 0;
    XPLMGetPluginInfo(pluginID, NULL, aszPath, NULL, NULL);
    LTPluginPath = aszPath;
    LOG_ASSERT(!LTPluginPath.empty());
    
    // LTPluginPath is now something like "...:Resources:plugins:LiveTraffic:64:mac.xpl"
    // we now reduce the path to the beginning of the plugin:
    // remove the file name
    std::string::size_type pos = LTPluginPath.rfind(DirSeparator);
    LOG_ASSERT(pos != std::string::npos);
    LTPluginPath.erase(pos);
    // remove the 64 subdirectory, but leave the separator at the end
    pos = LTPluginPath.rfind(DirSeparator);
    LOG_ASSERT(pos != std::string::npos);
    LTPluginPath.erase(pos+1);
    
    // Fetch all XP-provided data refs and verify if OK
    for ( int i=0; i < CNT_DATAREFS_XP; i++ )
    {
        if ( (adrXP[i] = XPLMFindDataRef (DATA_REFS_XP[i])) == NULL )
        {
            // for XP10 compatibility we accept if we don't find a few,
            // all else stays an error
            if (i != DR_VR_ENABLED &&
                i != DR_PILOTS_HEAD_ROLL &&
                i != DR_MODERN_DRIVER) {
                LOG_MSG(logFATAL,ERR_DATAREF_FIND,DATA_REFS_XP[i]);
                return false;
            }
        }
    }

    // Fetch all XP-provided cmd refs and verify if OK
    for (int i = 0; i < CNT_CMDREFS_XP; i++)
    {
        if ((cmdXP[i] = XPLMFindCommand(CMD_REFS_XP[i])) == NULL)
        {
            // Not finding a command is not fatal.
            // Just be aware that it could come back zero when using.
            LOG_MSG(logWARN, ERR_DATAREF_FIND, CMD_REFS_XP[i]);
        }
    }

    // register all LiveTraffic-provided dataRefs and commands
    if (!RegisterDataAccessors() || !RegisterCommands())
        return false;

    // read Doc8643 file (which we could live without)
    Doc8643::ReadDoc8643File();
    
    // read model_typecode file (which we could live without)
    ModelIcaoType::ReadFile();
    
    // read configuration file if any
    if (!LoadConfigFile())
        return false;
    
    // *** CSL path defaults ***
    // We'll try making this fool-proof but expert-changeable:
    // There are two paths under LiveTraffic that in all normal
    // installations should be supported, especially on initial setup.
    // Experts, however, may want to remove them and keep their CSLs elsewhere
    // or just deactivate the standard directories.
    // So the logic is:
    // 1. Underlying directory _does_ exist and is not empty
    // 2. Entry in vCSLPath does _not_ yet exist
    // then: add an activated entry
    for (std::string stdCSL: { PATH_RESOURCES_CSL, PATH_RESOURCES_SCSL }) {
        // 1. Underlying directory _does_ exist and is not empty
        std::string path (LTCalcFullPluginPath(stdCSL));
        if (LTNumFilesInPath(path) > 0) {
            // 2. Entry in vCSLPath does _not_ yet exist
            CSLPathCfgTy cfg (true, LTRemoveXPSystemPath(path));
            if (std::find(vCSLPaths.cbegin(), vCSLPaths.cend(), cfg) == vCSLPaths.cend()) {
                // insert at beginning
                vCSLPaths.emplace(vCSLPaths.cbegin(), std::move(cfg));
            }
        }
    }
    
    return true;
}

// Unregister (destructor would be too late for reasonable API calls)
void DataRefs::Stop ()
{
    // unregister our dataRefs
    for (XPLMDataRef& dr: adrLT) {
        XPLMUnregisterDataAccessor(dr);
        dr = NULL;
    }
}

// call XPLMRegisterDataAccessor
bool DataRefs::RegisterDataAccessors ()
{
    bool bRet = true;
    // loop over all data ref definitions
    for (int i=0; i < CNT_DATAREFS_LT; i++)
    {
        dataRefsLT eDataRef = dataRefsLT(i);
        dataRefDefinitionT& def = DATA_REFS_LT[i];
        
        // look up _and update_ refCon first if required
        // (can look up variable addresses only when object is known but not at compile time in definition of DATA_REFS_LT)
        if (def.getRefCon() == GET_VAR)
            def.setRefCon(getVarAddr(eDataRef));
        
        // register data accessor
        if ( (adrLT[i] =
              XPLMRegisterDataAccessor(def.getDataName(),       // inDataName
                                       def.getDataType(),       // inDataType
                                       def.isWriteable(),       // inIsWritable
                                       def.getDatai_f(),        // int
                                       def.setDatai_f(),
                                       def.getDataf_f(),        // float
                                       def.setDataf_f(),
                                       NULL,NULL,               // double
                                       NULL,NULL,               // int array
                                       NULL,NULL,               // float array
                                       def.getDatab_f(),        // data (read only)
                                       NULL,
                                       def.getRefCon(),         // read refCon
                                       def.getRefCon()          // write refCon
                                       )) == NULL )
        { LOG_MSG(logERR,ERR_DATAREF_ACCESSOR,def.getDataName()); bRet = false; }
    }
    return bRet;
}

// call XPLMRegisterDataAccessor
bool DataRefs::RegisterCommands()
{
    bool bRet = true;
    // loop over all data ref definitions
    for (int i=0; i < CNT_CMDREFS_LT; i++)
    {
        // register command
        if ( (cmdLT[i] =
              XPLMCreateCommand(CMD_REFS_LT[i].cmdName,
                                CMD_REFS_LT[i].cmdDescr)) == NULL )
        { LOG_MSG(logERR,ERR_CREATE_COMMAND,CMD_REFS_LT[i].cmdName); bRet = false; }
    }
    return bRet;
}


// Did the reference point to the local coordinate system change since last call to this function?
/// @note Will always return `true` on first call, intentionally.
bool DataRefs::DidLocalRefPointChange ()
{
    const float nowLatRef = XPLMGetDataf(adrXP[DR_LAT_REF]);
    const float nowLonRef = XPLMGetDataf(adrXP[DR_LON_REF]);
    
    // Is this a change compared to what we know?
    if (std::isnan(lstLonRef) ||          // never asked before?
        !dequal(lstLatRef, nowLatRef) ||  // changed compared to last call?
        !dequal(lstLonRef, nowLonRef))
    {
        // Update our known value of lat/lon reference
        lstLatRef = nowLatRef;
        lstLonRef = nowLonRef;
        
        // ref point changed
        return true;
    }
 
    // no change
    return false;
}

// return user's plane pos
positionTy DataRefs::GetUsersPlanePos(double& trueAirspeed_m, double& track ) const
{
    positionTy pos
    (
     XPLMGetDatad(adrXP[DR_PLANE_LAT]),
     XPLMGetDatad(adrXP[DR_PLANE_LON]),
     XPLMGetDatad(adrXP[DR_PLANE_ELEV]),
     GetSimTime(),
     XPLMGetDataf(adrXP[DR_PLANE_HEADING]),
     XPLMGetDataf(adrXP[DR_PLANE_PITCH]),
     XPLMGetDataf(adrXP[DR_PLANE_ROLL]),
     XPLMGetDatai(adrXP[DR_PLANE_ONGRND]) ? GND_ON : GND_OFF
    );
    
    // make invalid pos invalid
    if (pos.lat() < -75 || pos.lat() > 75)
        pos.lat() = NAN;
    
    // also fetch true airspeed and track
    trueAirspeed_m =    XPLMGetDataf(adrXP[DR_PLANE_TRUE_AIRSPEED]);
    track =             XPLMGetDataf(adrXP[DR_PLANE_TRACK]);

    return pos;
}

// return pilot's head position from 6 dataRefs combined
void DataRefs::GetPilotsHeadPos(XPLMCameraPosition_t& headPos) const
{
    headPos.x = XPLMGetDataf(adrXP[DR_PILOTS_HEAD_X]);
    headPos.y = XPLMGetDataf(adrXP[DR_PILOTS_HEAD_Y]);
    headPos.z = XPLMGetDataf(adrXP[DR_PILOTS_HEAD_Z]);
    headPos.heading = XPLMGetDataf(adrXP[DR_PILOTS_HEAD_HEADING]);
    headPos.pitch   = XPLMGetDataf(adrXP[DR_PILOTS_HEAD_PITCH]);
    headPos.roll    = adrXP[DR_PILOTS_HEAD_ROLL] ? XPLMGetDataf(adrXP[DR_PILOTS_HEAD_ROLL]) : 0.0f;
    headPos.zoom    = 1.0f;
}

//
//MARK: Generic Callbacks
//
// Generic get callbacks: just return the value pointed to...
int     DataRefs::LTGetInt(void* p)     { return *reinterpret_cast<int*>(p); }
float   DataRefs::LTGetFloat(void* p)   { return *reinterpret_cast<float*>(p); }

void    DataRefs::LTSetBool(void* p, int i)
{
    *reinterpret_cast<int*>(p) = i != 0;
    
    // also enable OpenSky Master data if OpenSky tracking data is now enabled
    if (((p == &dataRefs.bChannel[DR_CHANNEL_OPEN_SKY_ONLINE - DR_CHANNEL_FIRST]) && i) ||
        // override OpenSky Master if OpenSky tracking active
        ((p == &dataRefs.bChannel[DR_CHANNEL_OPEN_SKY_AC_MASTERDATA - DR_CHANNEL_FIRST]) &&
         dataRefs.IsChannelEnabled(DR_CHANNEL_OPEN_SKY_ONLINE)) )
        dataRefs.SetChannelEnabled(DR_CHANNEL_OPEN_SKY_AC_MASTERDATA, true);
}

//
// MARK: Bulk dataRef
//

/// @brief Bulk data access to transfer a lot of a/c info to LTAPI
/// @param inRefcon DR_AC_BULK_QUICK or DR_AC_BULK_EXPENSIVE
/// @param[out] outData Points to buffer provided by caller, can be NULL to "negotiate" struct size
/// @param inStartPos array position to start with, multiple of agreed struct size
/// @param inNumBytes Number of byte to copy, multiple of agreed struct size / or caller's struct size during "negotiation"
/// @return number of bytes stored
/// @note This dataRef is "rogue" as per XPLMGetDatab API documentation:
///       It has other semantics than X-Plane's API describes.
///       When passing in NULL for `outData`, then `inNumAc` has to be filled
///       with caller's structur size and LiveTraffic returns LiveTraffic's
///       struct size.
///       LTAPI will for its convenience always use its size, makes it easier
///       to allocate array memory there.
///       LiveTraffic will only fill as much as requested and use the
///       passed in size to advance the array pointer.
///       This way version differences between LiveTraffic and LTAPI can
///       be overcome and still an array of a/c info can be transferred.
int DataRefs::LTGetBulkAc (void* inRefcon, void * outData,
                           int inStartPos, int inNumBytes)
{
    // quick or expensive one?
    dataRefsLT dr = (dataRefsLT)reinterpret_cast<long long>(inRefcon);
    LOG_ASSERT(dr == DR_AC_BULK_QUICK || dr == DR_AC_BULK_EXPENSIVE);

    // "Negotiation": In case of version differences between calling app
    // and LiveTraffic we need to be conscious of the callers struct size.
    static int size_quick = 0, size_expensive = 0;
    if (!outData)
    {
        if (dr == DR_AC_BULK_QUICK) {
            size_quick = inNumBytes;
            return (int)sizeof(LTAPIAircraft::LTAPIBulkData);
        } else {
            size_expensive = inNumBytes;
            return (int)sizeof(LTAPIAircraft::LTAPIBulkInfoTexts);
        }
    }
    
    // Validations before starting normal operations
    // Size must have been negotiated first
    int size = dr == DR_AC_BULK_QUICK ? size_quick : size_expensive;
    if (!size) return 0;

    // Positions / copy size must both be multiples of agreed size
    // (This is also a safeguard against attempts to access the bulk
    //  dataRefs by apps that don't implement proper array semantics and
    //  "negotiation", read: Don't use LTAPI.)
    if ((inStartPos % size != 0) ||
        (inNumBytes % size != 0))
        return 0;
    
    // Normal operation: loop over requested a/c
    const int startAc = 1 + inStartPos / size;      // first a/c index (1-based)
    const int endAc = startAc + (inNumBytes / size);// last+1 a/c index (passed-the-end)
    char* pOut = (char*)outData;                    // point to current output position
    int iAc = startAc;                              // current a/c index (1-based)
    for (mapLTFlightDataTy::iterator fdIter = mapFdAcByIdx(iAc);
         fdIter != mapFd.end() && iAc < endAc;
         // iterator to next FlighData _with_ aircraft; advance output pointer
         fdIter = mapFdNextWithAc(fdIter), iAc++, pOut += size)
    {
        // copy data of the current aircraft
        const LTAircraft& ac = *fdIter->second.GetAircraft();
        if (dr == DR_AC_BULK_QUICK)
            ac.CopyBulkData ((LTAPIAircraft::LTAPIBulkData*)pOut, size);
        else
            ac.CopyBulkData((LTAPIAircraft::LTAPIBulkInfoTexts*)pOut, size);
    }
    
    // how many bytes copied?
    return (iAc - startAc) * size;
}


//
//MARK: Aircraft Information
//

// finds the a/c ptr based on the key (transpIcao)
bool DataRefs::FetchPAc ()
{
    // if there is no a/c defined yet, BUT there is a debug a/c defined
    // then we use that debug a/c
    if ( keyAc.empty() )
        keyAc = GetDebugAcFilter();
    
    // short-cut if there is still no key
    if ( keyAc.empty() )
        return false;
    
    // find that key's element
    LTFlightData::FDKeyTy fdKey (LTFlightData::KEY_ICAO, keyAc);
    mapLTFlightDataTy::const_iterator fdIter = mapFd.find(fdKey);
    if (fdIter != mapFd.end()) {
        // found, save ptr to a/c
        pAc = fdIter->second.GetAircraft();
        // that pointer might be NULL if a/c has not yet been created!
        return pAc != nullptr;
    }

    // not found, clear all ptr/keys
    pAc = nullptr;
    keyAc.clear();
    return false;
}


// Set a key to define which a/c we return data for
// can be an array index or a transpIcao
void DataRefs::LTSetAcKey(void*, int key)
{
    // sanity check
    if ( key < 0 || (unsigned)key > MAX_TRANSP_ICAO )
        return;
    
    // default: nothing found
    dataRefs.pAc = nullptr;
    dataRefs.keyAc.clear();
    
    // 0 means reset
    if (key == 0) {
        return;
    }
    // key can be either index or the decimal representation of an transpIcao
    // for any number below number of a/c displayed we assume: index
    else if ( key <= dataRefs.cntAc )
    {
        // let's find the i-th aircraft
        mapLTFlightDataTy::iterator fdIter = mapFdAcByIdx(key);
        if (fdIter != mapFd.end()) {
            dataRefs.keyAc = fdIter->second.key();
            dataRefs.pAc = fdIter->second.GetAircraft();
            return;
        }
    }
    // so we deal with a transpIcao code
    else
    {
        // the key into mapFd is a 6-digit hex string
        char keyHex[10];
        snprintf ( keyHex, sizeof(keyHex), "%06X", (unsigned int)key );
        
        // now try to find that key and set the pAc ptr
        dataRefs.keyAc = keyHex;
        dataRefs.FetchPAc();
    }
}

// return info 
int DataRefs::LTGetAcInfoI(void* p)
{
    // don't need an a/c pointer for this one:
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_NUM: return dataRefs.cntAc;
    }

    // verify a/c ptr is available
    if ( !dataRefs.pAc && !dataRefs.FetchPAc() )
        return 0;

    // return a/c info
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_KEY: return (int)dataRefs.pAc->fd.key().num;
        case DR_AC_ON_GND: return dataRefs.pAc->IsOnGrnd();
        case DR_AC_PHASE: return dataRefs.pAc->GetFlightPhase();
        case DR_AC_LIGHTS_BEACON: return dataRefs.pAc->surfaces.lights.bcnLights;
        case DR_AC_LIGHTS_STROBE: return dataRefs.pAc->surfaces.lights.strbLights;
        case DR_AC_LIGHTS_NAV: return dataRefs.pAc->surfaces.lights.navLights;
        case DR_AC_LIGHTS_LANDING: return dataRefs.pAc->surfaces.lights.landLights;
        default:
            LOG_ASSERT(false);              // not allowed...we should handle all value types!
            return 0;
    }
}

float DataRefs::LTGetAcInfoF(void* p)
{
    if ( !dataRefs.pAc && !dataRefs.FetchPAc() )
        return 0.0;
    
    switch ( reinterpret_cast<long long>(p) ) {
        case DR_AC_LAT:         return (float)dataRefs.pAc->GetPPos().lat();
        case DR_AC_LON:         return (float)dataRefs.pAc->GetPPos().lon();
        case DR_AC_ALT:         return (float)dataRefs.pAc->GetAlt_ft();
        case DR_AC_HEADING:     return (float)dataRefs.pAc->GetHeading();
        case DR_AC_ROLL:        return (float)dataRefs.pAc->GetRoll();
        case DR_AC_PITCH:       return (float)dataRefs.pAc->GetPitch();
        case DR_AC_SPEED:       return (float)dataRefs.pAc->GetSpeed_kt();
        case DR_AC_VSI:         return (float)dataRefs.pAc->GetVSI_ft();
        case DR_AC_TERRAIN_ALT: return (float)dataRefs.pAc->GetTerrainAlt_ft();
        case DR_AC_HEIGHT:      return (float)dataRefs.pAc->GetPHeight_ft();
        case DR_AC_FLAPS:       return (float)dataRefs.pAc->GetFlapsPos();
        case DR_AC_GEAR:        return (float)dataRefs.pAc->GetGearPos();
        case DR_AC_BEARING:     return (float)dataRefs.pAc->GetVecView().angle;
        case DR_AC_DIST:        return (float)dataRefs.pAc->GetVecView().dist;
        default:
            LOG_ASSERT(false);              // not allowed...we should handle all value types!
            return 0.0;
    }
}

//
//MARK: Config Options
//

// simulated time (seconds since Unix epoch, including fractionals)
double DataRefs::GetSimTime() const
{
    // using historic data means: we take the date configured in X-Plane's date&time settings
    if ( bUseHistoricData )
    {
        // cache parts of the calculation as the difficult part can only
        // change at the full hour
        static time_t cacheStartOfZuluDay = -1;
        static int lastCalcZHour = -1;
        static int lastLocalDateDays = -1;
        
        // current zulu time of day
        double z  = GetZuluTimeSec();
        // X-Plane's local date, expressed in days since January 1st
        int localDateDays = GetLocalDateDays();

        // if the zulu hour or the date changed since last full calc then the full calc
        // might change, so redo it once and cache the result
        if (int(z/SEC_per_H) != lastCalcZHour ||
            localDateDays != lastLocalDateDays)
        {
            // challenge: Xp doesn't provide "ZuluDateDays". The UTC day might
            // not be the same as the local day we get with GetLocalDateDays.
            // So the approach is as follows: In reality, the time diff between
            // local and zulu can't be more than 12 hours.
            // So if the diff between local and zulu time appears greater than 12 hours
            // we have to adjust the date by one day, which can happen into the past as well as
            // into the future:
            // l = local time
            // z = zulu time
            // 0 = local midnight
            // d = z - l
            //
            // 1 -----0--l---z-----  l < z,   0 <  d <= 12
            // 2 -----0--z---l-----  z < l, -12 <= d <  0
            // 3 --z--0---l--------  z > l,   d > 12,  z-day less    than l-day
            // 4 --l--0---z--------  l > z,   d < -12, z-day greater than l-day
            double l = GetLocalTimeSec();
            double d  = z - l;        // time doesn't move between the two calls within the same drawing frame so the diff is actually a multiple of hours (or at least minutes), but no fractional seconds
            
            // we only need to adapt d if abs(d) is greater than 12 hours
            if ( d > 12 * SEC_per_H )
                localDateDays--;
            else if ( d < -12 * SEC_per_H )
                localDateDays++;
            
            // calculate the right zulu day
            cacheStartOfZuluDay =
                // cater for year-wrap-around as X-Plane doesn't configure the year
                (( localDateDays <= iTodaysDayOfYear ) ? tStartThisYear : tStartPrevYear) +
                // add seconds for each completed day of that year
                localDateDays * SEC_per_D;
            
            // the zulu hour/date we did the calculation for
            lastCalcZHour = int(z / SEC_per_H);
            lastLocalDateDays = localDateDays;
        }

        // add current zulu time to start of zulu day
        return cacheStartOfZuluDay + z;
    }
    else
    {
        // we use current system time (no matter what X-Plane simulates),
        // but lagging behind by the buffering time
        using namespace std::chrono;
        return
            // system time in microseconds
            double(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count())
            // divided by 1000000 to create seconds with fractionals
            / 1000000.0
            // minus the buffering time
            - GetFdBufPeriod()
            // plus the offset compared to network (this corrects for wrong system clock time as compared to reality)
            + GetChTsOffset();
    }
    
}

// current sim time as human readable string,
// including 10th of seconds
std::string DataRefs::GetSimTimeString() const
{
    const double simTime = dataRefs.GetSimTime();
    char s[100];
    snprintf(s, sizeof(s), "%s.%dZ",
             ts2string(time_t(simTime)).c_str(),
             int(std::fmod(simTime, 1.0f)*10.0f) );
    return std::string(s);
}

// livetraffic/sim/date and .../time
void DataRefs::LTSetSimDateTime(void* p, int i)
{
    long bDateTime = (long)reinterpret_cast<long long>(p);
    
    // as we are setting a specific date/time we switch XP to "don't use system time"
    dataRefs.SetUseSystemTime(false);
    
    // setting date?
    if ( bDateTime == 1) {
        // range check: if month/day only add _any_ year...doesn't matter (much) for day-of-year calc
        if ( 0101 <= i && i <= 1231 )
            i += 20180000;
        
        // range check: 19000101 <= i <= 29991231
        if ( i < 19000101 || i > 29991231 ) return;
        
        // calculate days since beginning of year: mktime fills that field
        std::tm tm;
        memset (&tm, 0, sizeof(tm));
        tm.tm_year = i / 10000 - 1900;
        tm.tm_mon  = (i % 10000) / 100 - 1;
        tm.tm_mday = i % 100;
        tm.tm_hour = 12;            // pretty safe re local time zone...not absolutely, though
        mktime(&tm);
        
        // set the data ref for local_date_days to adjust X-Planes date immediately
        dataRefs.SetLocalDateDays(tm.tm_yday);
    } else {
        // setting time, range check: 000000 <= i <= 235959
        if ( i < 0 || i > 235959 ) return;
        
        // seconds since midnight
        int sec = i / 10000 * SEC_per_H +           // hour
                  (i % 10000) / 100 * SEC_per_M +   // minute
                  i % 100;
        dataRefs.SetZuluTimeSec((float)sec);
    }
    
    // finally, if we are not already using historic data switch to use it
    //          and force reloading all data
    dataRefs.SetUseHistData(true, true);
}

int DataRefs::LTGetSimDateTime(void* p)
{
    long bDateTime = (long)reinterpret_cast<long long>(p);
    
    // current simulated time, converted to structure
    time_t simTime = (time_t)dataRefs.GetSimTime();
    std::tm tm;
    gmtime_s(&tm, &simTime);

    // asked for date? Return date as human readable number yyyymmdd:
    if ( bDateTime == 1 ) {
        return
            (tm.tm_year + 1900) * 10000 +           // year
            (tm.tm_mon + 1)     *   100 +           // month
            (tm.tm_mday);                           // day of month
    } else {
        // asked for time, return time as human readable number hhmmss:
        return
            tm.tm_hour          * 10000 +           // hour
            tm.tm_min           *   100 +           // minute
            tm.tm_sec;                              // second
    }
}

// Enable/Disable display of aircraft
void DataRefs::LTSetAircraftDisplayed(void*, int i)
{ dataRefs.SetAircraftDisplayed (i); }

void DataRefs::SetAircraftDisplayed ( int bEnable )
{
    // Do what we are asked to do
    if ( bEnable )
    {
        bShowingAircraft = LTMainShowAircraft();
    }
    else
    {
        LTMainHideAircraft();
        bShowingAircraft = 0;
    }
    
    // update menu item's checkmark
    MenuUpdateAllItemStatus();
}

int DataRefs::ToggleAircraftDisplayed ()
{
    // set the switch to the other value
    SetAircraftDisplayed ( !bShowingAircraft );
    // return the new status
    return bShowingAircraft;
}

// Log Level
void DataRefs::LTSetLogLevel(void* p, int i)
{
    LOG_ASSERT (p == &dataRefs.iLogLevel || p == &dataRefs.iMsgAreaLevel);
    if ( p == &dataRefs.iMsgAreaLevel )
        dataRefs.SetMsgAreaLevel(i);
    else
        dataRefs.SetLogLevel(i);
}

void DataRefs::SetLogLevel ( int i )
{
    if ( i <= logDEBUG )       iLogLevel = logDEBUG;
    else if ( i >= logFATAL )  iLogLevel = logFATAL ;
    else                       iLogLevel = logLevelTy(i);
}

void DataRefs::SetMsgAreaLevel ( int i )
{
    if ( i <= logDEBUG )       iMsgAreaLevel = logDEBUG;
    else if ( i >= logFATAL )  iMsgAreaLevel = logFATAL ;
    else                       iMsgAreaLevel = logLevelTy(i);
}

// switch usage of historic data
void DataRefs::LTSetUseHistData(void*, int useHistData)
{
    dataRefs.SetUseHistData(useHistData != 0, false);
}

// switch usage of historic data, return success
bool DataRefs::SetUseHistData (bool bUseHistData, bool bForceReload)
{
    // short-cut if no actual change...cause changing it is expensive
    if ( !bForceReload && dataRefs.bUseHistoricData == (int)bUseHistData )
        return true;
    
    // change to historical data but running with system time?
    if ( bUseHistData && dataRefs.GetUseSystemTime() )
    {
        SHOW_MSG(logERR, MSG_HIST_WITH_SYS_TIME);
        return false;
    }
    
    // if we change this setting while running
    // we 'simulate' a re-initialization
    if (pluginState >= STATE_ENABLED) {
        // remove all existing aircraft
        bool bShowAc = dataRefs.AreAircraftDisplayed();
        dataRefs.SetAircraftDisplayed(false);

        // disable myself / stop all connections
        LTMainDisable();
        
        // Now set the new setting
        dataRefs.bUseHistoricData = bUseHistData;
        
        // create the connections to flight data
        if ( LTMainEnable() ) {
            // display aircraft (if that was the case previously)
            dataRefs.SetAircraftDisplayed(bShowAc);
            return true;
        }
        else {
            return false;
        }
    }
    else {
        // not yet running, i.e. init phase: just set the value
        dataRefs.bUseHistoricData = bUseHistData;
        return true;
    }
}

// set a config value and validate (all of) them
void DataRefs::LTSetCfgValue (void* p, int val)
{ dataRefs.SetCfgValue(p, val); }

bool DataRefs::SetCfgValue (void* p, int val)
{
    // If fdSnapTaxiDist changes we might want to enable/disable airport reading
    int oldFdSnapTaxiDist = fdSnapTaxiDist;
    
    // we don't exactly know which parameter p points to...
    // ...we just set it, validate all of them, and reset in case validation fails
    int oldVal = *reinterpret_cast<int*>(p);
    *reinterpret_cast<int*>(p) = val;
    
    // any configuration value invalid?
    if (labelColor      < 0                 || labelColor       > 0xFFFFFF ||
#ifdef DEBUG
        maxNumAc        < 1                 || maxNumAc         > 100   ||
#else
        maxNumAc        < 5                 || maxNumAc         > 100   ||
#endif
        fdStdDistance   < 5                 || fdStdDistance    > 100   ||
        fdRefreshIntvl  < 10                || fdRefreshIntvl   > 5*60  ||
        fdBufPeriod     < fdRefreshIntvl    || fdBufPeriod      > 5*60  ||
        acOutdatedIntvl < 2*fdRefreshIntvl  || acOutdatedIntvl  > 5*60  ||
        fdSnapTaxiDist  < 0                 || fdSnapTaxiDist   > 50    ||
        netwTimeout     < 15                ||
        hideBelowAGL    < 0                 || hideBelowAGL     > MDL_ALT_MAX ||
        rtListenPort    < 1024              || rtListenPort     > 65535 ||
        rtTrafficPort   < 1024              || rtTrafficPort    > 65535 ||
        rtWeatherPort   < 1024              || rtWeatherPort    > 65535 ||
        ffSendPort      < 1024              || ffSendPort       > 65535
        )
    {
        // undo change
        *reinterpret_cast<int*>(p) = oldVal;
        return false;
    }
    
    // Special handling for fdSnapTaxiDist:
    if (oldFdSnapTaxiDist != fdSnapTaxiDist)        // snap taxi dist did change
    {
        // switched from on to off?
        if (oldFdSnapTaxiDist > 0 && fdSnapTaxiDist == 0)
            LTAptDisable();
        // switched from off to on?
        else if (oldFdSnapTaxiDist == 0 && fdSnapTaxiDist > 0)
            LTAptEnable();
    }
    
    // success
    return true;
}

// generic config access (not as fast as specific access, but good for rare access)
bool  DataRefs::GetCfgBool  (dataRefsLT dr)
{
    return GetCfgInt(dr) != 0;
}

int   DataRefs::GetCfgInt   (dataRefsLT dr)
{
    assert(0 <= dr && dr < CNT_DATAREFS_LT);
    return DATA_REFS_LT[dr].getDatai();
}

float DataRefs::GetCfgFloat (dataRefsLT dr)
{
    assert(0 <= dr && dr < CNT_DATAREFS_LT);
    return DATA_REFS_LT[dr].getDataf();
}


// more than 24h passed since last version check?
bool DataRefs::NeedNewVerCheck () const
{
    if (!lastCheckNewVer)
        return true;
    
    // 'now' in hours since the epoch
    int nowH = (int)std::chrono::duration_cast<std::chrono::hours>
    (std::chrono::system_clock::now().time_since_epoch()).count();
    
    return nowH - lastCheckNewVer > LT_NEW_VER_CHECK_TIME;
}

// saves the fact that we just checked for a new version
void DataRefs::SetLastCheckedNewVerNow ()
{
    lastCheckNewVer = (int)
    std::chrono::duration_cast<std::chrono::hours>
    (std::chrono::system_clock::now().time_since_epoch()).count();
}

// return color into a RGB array as XP likes it
void DataRefs::GetLabelColor (float outColor[4]) const
{
    conv_color(labelColor, outColor);
}

//
//MARK: Debug Options
//

// return current a/c filter
std::string DataRefs::GetDebugAcFilter() const
{
    char key[7];
    if ( !uDebugAcFilter ) return std::string();
    
    // convert to hex representation
    snprintf(key,sizeof(key),"%06X",uDebugAcFilter);
    return key;
}

// sets A/C filter
void DataRefs::LTSetDebugAcFilter( void* /*inRefcon*/, int i )
{
    bool bWasFilterDefined = dataRefs.uDebugAcFilter != 0;
    
    // match hex range of transpIcao codes
    if ( 0x000000 <= i && (unsigned)i <= MAX_TRANSP_ICAO ) {
        dataRefs.uDebugAcFilter = unsigned(i);
        
        // also set the key for the a/c info datarefs
        if (i > 0x000000) {
            LTSetAcKey(reinterpret_cast<void*>(long(DR_AC_KEY)), i);
            LOG_MSG(logWARN,DBG_FILTER_AC,
                    dataRefs.GetDebugAcFilter().c_str());
        } else if (bWasFilterDefined) {
            LOG_MSG(logWARN,DBG_FILTER_AC_REMOVED);
        }
    }
}

//
// MARK: DataRefs::dataRefDefinitionT
//

// get the actual current value (by calling the getData?_f function)
int DataRefs::dataRefDefinitionT::getDatai () const
{
    if (dataType != xplmType_Int || ifRead == NULL)
        return 0;

    LOG_ASSERT(refCon != GET_VAR);
    return (ifRead)(refCon);
}

float DataRefs::dataRefDefinitionT::getDataf () const
{
    if (dataType != xplmType_Float || ffRead == NULL)
        return NAN;

    LOG_ASSERT(refCon != GET_VAR);
    return (ffRead)(refCon);
}


// compiles string for storage in config file
std::string DataRefs::dataRefDefinitionT::GetConfigString() const
{
    // short-cut: not for saving in config file
    if (!bCfgFile)
        return std::string();
    
    // put together dataRef's dataName and
    // add the current value, which might be int or float
    switch (dataType) {
        case xplmType_Int:
            return dataName + ' ' + std::to_string(getDatai());
        case xplmType_Float:
            return dataName + ' ' + std::to_string(getDataf());
        default:
            // else: must not happen
            LOG_ASSERT(NULL);
            return std::string();
    }
}


// write values to the dataRef
void DataRefs::dataRefDefinitionT::setData (int i)
{
    if (dataType == xplmType_Int && ifWrite != NULL) {
        LOG_ASSERT(refCon != GET_VAR);
		(ifWrite) (refCon, i);
    }
}

void DataRefs::dataRefDefinitionT::setData (float f)
{
    if (dataType == xplmType_Float && ffWrite != NULL) {
        LOG_ASSERT(refCon != GET_VAR);
		(ffWrite) (refCon, f);
    }
}

// assumes the string is a number, which will be converted to the
// appropriate type and then passed on to the other setData functions
void DataRefs::dataRefDefinitionT::setData (const std::string& s)
{
    try {
        switch (dataType) {
            case xplmType_Int:
                setData (std::stoi(s));
                break;
            case xplmType_Float:
                setData (std::stof(s));
                break;
            default:
                // else: must not happen
                LOG_ASSERT(NULL);
        }
    }
    catch (const std::invalid_argument& e) {
        LOG_MSG(logWARN,ERR_CFG_FILE_VALUE,dataName.c_str(),s.c_str(),e.what());
    }
    catch (const std::out_of_range& e) {
        LOG_MSG(logWARN,ERR_CFG_FILE_VALUE,dataName.c_str(),s.c_str(),e.what());
    }
}

//MARK: Increse/decrease number of a/c

// increase number of a/c, update menu item with that number
int DataRefs::IncNumAc()
{
    return ++cntAc;
}

// decreses number of aircraft
// which by itself is simplistic, but as the just removed a/c
// _could_ be the one we are monitoring in our dataRefs (we don't know)
// we better invalidate the pAc ptr and force the dataRef to find the a/c again
int DataRefs::DecNumAc()
{
    pAc=nullptr;
    return --cntAc;
}


//MARK: Config File

bool DataRefs::LoadConfigFile()
{
    // which conversion to do with the (older) version of the config file?
    enum cfgFileConvE { CFG_NO_CONV=0, CFG_KM_2_NM } conv = CFG_NO_CONV;
    
    // open a config file
    std::string sFileName (LTCalcFullPath(PATH_CONFIG_FILE));
    std::ifstream fIn (sFileName);
    if (!fIn) {
        // if there is no config file just return...that's no problem, we use defaults
        if (errno == ENOENT)
            return true;
        
        // something else happened
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
		LOG_MSG(logERR, ERR_CFG_FILE_OPEN_IN,
                sFileName.c_str(), sErr);
        return false;
    }
    
    // *** VERSION ***
    // first line is supposed to be the version - and we know of exactly one:
    // read entire line
    std::vector<std::string> ln;
    std::string lnBuf;
    if (!safeGetline(fIn, lnBuf) ||                     // read a line
        (ln = str_tokenize(lnBuf, " ")).size() != 2 ||  // split into two words
        ln[0] != LIVE_TRAFFIC)                          // 1. is LiveTraffic
    {
        LOG_MSG(logERR, ERR_CFG_FILE_VER, sFileName.c_str(), lnBuf.c_str());
        return false;
    }
    
    // 2. is version / test for older version for which a conversion is to be done?
    if (ln[1] == LT_CFG_VERSION)            conv = CFG_NO_CONV;
    else if (ln[1] == LT_CFG_VER_NM_CONV)   conv = CFG_KM_2_NM;
    else {
        SHOW_MSG(logERR, ERR_CFG_FILE_VER, sFileName.c_str(), lnBuf.c_str());
        return false;
    }
    
    // *** DataRefs ***
    // then follow the config entries
    // supposingly they are just 'dataRef <space> value',
    // but to prevent misuse we certainly validate that we support
    // those dataRefs.
    // The file could end with a [CSLPaths] section, too.
    int errCnt = 0;
    while (fIn && errCnt <= ERR_CFG_FILE_MAXWARN) {
        // read line and break into tokens, delimited by spaces
        safeGetline(fIn, lnBuf);
        ln = str_tokenize(lnBuf, " ");
        
        // break out of loop if reading [CSLPaths]
        if (ln.size() == 1 && ln[0] == CFG_CSL_SECTION)
            break;

        // otherwise should be 2 tokens
        if (ln.size() != 2) {
            // wrong number of words in that line
            LOG_MSG(logWARN,ERR_CFG_FILE_WORDS, sFileName.c_str(), lnBuf.c_str());
            errCnt++;
            continue;
        }
        
        // did read a name and a value?
        const std::string& sDataRef = ln[0];
        std::string& sVal     = ln[1];
        if (!sDataRef.empty() && !sVal.empty()) {
            // verify that we know that name
            dataRefDefinitionT* i = std::find_if(std::begin(DATA_REFS_LT),
                                                 std::end(DATA_REFS_LT),
                                                 [&](const DataRefs::dataRefDefinitionT& def)
                                                 { return def.getDataNameStr() == sDataRef; } );
            if ( i != nullptr && i != std::cend(DATA_REFS_LT) &&
                 i->isCfgFile())        // and it is a configurable one
            {
                // conversion of older config file formats
                switch (conv) {
                    case CFG_NO_CONV: break;
                    case CFG_KM_2_NM:           // distance values converted from km to nm
                        if (*i == DATA_REFS_LT[DR_CFG_FD_STD_DISTANCE])
                        {
                            // distances are int values, so we have to convert, then round to int:
                            sVal = std::to_string(std::lround(std::stoi(sVal) *
                                                  double(M_per_KM) / double(M_per_NM)));
                        }
                        break;
                }
                
                // *** valid config entry, now process it ***
                i->setData(sVal);
            }
            // *** Strings ***
            else if (sDataRef == CFG_DEFAULT_AC_TYPE)
                dataRefs.SetDefaultAcIcaoType(sVal);
            else if (sDataRef == CFG_DEFAULT_CAR_TYPE)
                dataRefs.SetDefaultCarIcaoType(sVal);
            else if (sDataRef == CFG_ADSBEX_API_KEY)
                dataRefs.SetADSBExAPIKey(sVal);
            else
            {
                // unknown config entry, ignore
                LOG_MSG(logWARN,ERR_CFG_FILE_IGNORE,
                        sDataRef.c_str(), sFileName.c_str());
                errCnt++;
            }
        }
        
    }
    
    // *** [CSLPaths] ***
    // maybe there's a [CSLPath] section?
    if (fIn && errCnt <= ERR_CFG_FILE_MAXWARN &&
        ln.size() == 1 && ln[0] == CFG_CSL_SECTION)
    {
        
        // loop until EOF
        while (fIn && errCnt <= ERR_CFG_FILE_MAXWARN)
        {
            // read line and break into tokens, delimited by spaces
            safeGetline(fIn, lnBuf);

            // skip empty lines without warning
            if (lnBuf.empty())
                continue;
            
            // line has to start with 0 or 1 and | to separate "enabled?" from path
            ln = str_tokenize(lnBuf, "|");
            if (ln.size() != 2 ||
                ln[0].size() != 1 ||
                (ln[0][0] != '0' && ln[0][0] != '1'))
            {
                LOG_MSG(logWARN, ERR_CFG_CSL_INVALID, sFileName.c_str(), lnBuf.c_str());
                errCnt++;
                continue;
            }
            
            // enabled?
            bool bEnabled = ln[0][0] == '1';
            
            // add the path to the list (unvalidated!) if no duplicate
            if (std::find(vCSLPaths.begin(), vCSLPaths.end(), ln[1]) == vCSLPaths.end())
                vCSLPaths.emplace_back(bEnabled, std::move(ln[1]));
        }
    }

    // problem was not just eof?
    if (!fIn && !fIn.eof()) {
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        LOG_MSG(logERR, ERR_CFG_FILE_READ,
                sFileName.c_str(), sErr);
        return false;
    }
    
    // close file
    fIn.close();

    // too many warnings?
    if (errCnt > ERR_CFG_FILE_MAXWARN) {
        LOG_MSG(logERR, ERR_CFG_FILE_READ,
                sFileName.c_str(), ERR_CFG_FILE_TOOMANY);
        return false;
    }
    
    // looks like success
    return true;
}

bool DataRefs::SaveConfigFile()
{
    // open an output config file
    std::string sFileName (LTCalcFullPath(PATH_CONFIG_FILE));
    std::ofstream fOut (sFileName, std::ios_base::out | std::ios_base::trunc);
    if (!fOut) {
		char sErr[SERR_LEN];
		strerror_s(sErr, sizeof(sErr), errno);
		SHOW_MSG(logERR, ERR_CFG_FILE_OPEN_OUT,
                 sFileName.c_str(), sErr);
        return false;
    }
    
    // *** VERSION ***
    // save application and version first...maybe we need to know it in
    // future versions for conversion efforts - who knows?
    fOut << LIVE_TRAFFIC << ' ' << LT_CFG_VERSION << '\n';
    
    // *** DataRefs ***
    // loop over our LiveTraffic values and store those meant to be stored
    for (const DataRefs::dataRefDefinitionT& def: DATA_REFS_LT)
        if (def.isCfgFile())                   // only for values which are to be saved
            fOut << def.GetConfigString() << '\n';
    
    // *** Strings ***
    fOut << CFG_DEFAULT_AC_TYPE << ' ' << dataRefs.GetDefaultAcIcaoType() << '\n';
    fOut << CFG_DEFAULT_CAR_TYPE << ' ' << dataRefs.GetDefaultCarIcaoType() << '\n';
    if (!dataRefs.GetADSBExAPIKey().empty())
        fOut << CFG_ADSBEX_API_KEY << ' ' << dataRefs.GetADSBExAPIKey() << '\n';

    // *** [CSLPatchs] ***
    // add section of CSL paths to the end
    if (!vCSLPaths.empty()) {
        fOut << CFG_CSL_SECTION << '\n';
        for (const DataRefs::CSLPathCfgTy& cslPath: vCSLPaths)
            if (!cslPath.empty())
                fOut << (cslPath.enabled() ? "1|" : "0|") <<
                LTRemoveXPSystemPath(cslPath.path) << '\n';
    }
    
    // some error checking towards the end
    if (!fOut) {
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_WRITE,
                 sFileName.c_str(), sErr);
        fOut.close();
        return false;
    }

    // flush (which we explicitely did not do for each line for performance reasons) and close
    fOut.flush();
    fOut.close();
        
    return true;
}

// Save a new/changed CSL path
void DataRefs::SaveCSLPath(int idx, const CSLPathCfgTy path)
{
    // make sure the array of paths is large enough
    while (size_t(idx) >= vCSLPaths.size())
        vCSLPaths.push_back({});
    
    // then store the actual path
    vCSLPaths[idx] = path;
}

// Load a CSL package interactively
bool DataRefs::LoadCSLPackage(int idx)
{
    if (size_t(idx) < vCSLPaths.size()) {
        // enabled, path could be relative to X-Plane
        const std::string path = LTCalcFullPath(vCSLPaths[idx].path);
        
        // doesn't exist? has no files?
        if (LTNumFilesInPath(path) < 1) {
            SHOW_MSG(logERR, ERR_CFG_CSL_EMPTY, path.c_str());
        }
        else
        {
            // try loading the package
            const char* cszResult = XPMPLoadCSLPackage(path.c_str());
            
            // Addition of CSL package failed?
            if ( cszResult[0] ) {
                SHOW_MSG(logERR,ERR_XPMP_ADD_CSL, cszResult);
            } else {
                SHOW_MSG(logMSG,MSG_CSL_PACKAGE_LOADED, vCSLPaths[idx].path.c_str());
                return true;
            }
        }
    }
    
    // didn't work for some reason
    return false;
}


// sets the default a/c icao type after validation with Doc8643
bool DataRefs::SetDefaultAcIcaoType(const std::string type)
{
    if (Doc8643::get(type) != DOC8643_EMPTY) {
        sDefaultAcIcaoType = type;
        XPMPSetDefaultPlaneICAO(type.c_str());  // inform libxplanemp
        LOG_MSG(logINFO,CFG_DEFAULT_AC_TYP_INFO,sDefaultAcIcaoType.c_str());
        return true;
    }

    // invalid type
    SHOW_MSG(logWARN,ERR_CFG_AC_DEFAULT,type.c_str(),
             sDefaultAcIcaoType.c_str());
    return false;
}

// sets default car type. this is a fake value, so no validation agains Doc8643
// but still needs to be 1 through 4 characters long
bool DataRefs::SetDefaultCarIcaoType(const std::string type)
{
    if (1 <= type.length() && type.length() <= 4) {
        sDefaultCarIcaoType = type;
        XPMPSetDefaultPlaneICAO(nullptr, type.c_str());  // inform libxplanemp
        LOG_MSG(logINFO,CFG_DEFAULT_CAR_TYP_INFO,sDefaultCarIcaoType.c_str());
        return true;
    }
    
    // invalid
    LOG_MSG(logWARN,ERR_CFG_CAR_DEFAULT,type.c_str(),
            sDefaultCarIcaoType.c_str());
    return false;
}

// how many channels are enabled?
int DataRefs::CntChannelEnabled () const
{
    return (int)std::count(std::begin(bChannel),
                           std::end(bChannel),
                           1);
}

// add another offset to the offset calculation (network time vs. system clock)
// This is just a simple average calculation, not caring too much about rounding issues
void DataRefs::ChTsOffsetAdd (double aNetTS)
{
    // after some calls we keep our offset stable (each chn has the chance twice)
    if (cntAc > 0 ||                // and no change any longer if displaying a/c!
        chTsOffsetCnt >= CntChannelEnabled() * 2)
        return;
    
    // for TS to become an offset we need to remove current system time;
    // yes...since we received that timestamp time has passed...but this is all
    // not about milliseconds...if it is plus/minus 5s we are good enough!
    using namespace std::chrono;
    aNetTS -=
        // system time in microseconds
        double(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count())
        // divided by 1000000 to create seconds with fractionals
        / 1000000.0;
    
    // now for the average
    chTsOffset *= chTsOffsetCnt;
    chTsOffset += aNetTS;
    chTsOffset /= ++chTsOffsetCnt;
}

//MARK: Processed values (static functions)

// return the camera's position in world coordinates
positionTy DataRefs::GetViewPos()
{
    XPLMCameraPosition_t camPos = {NAN, NAN, NAN, 0.0f, 0.0f, 0.0f, 0.0f};
    // get the dataref values for current view pos, which are in local coordinates
    XPLMReadCameraPosition(&camPos);
    // convert to world coordinates and return them
    double lat, lon, alt;
    XPLMLocalToWorld(camPos.x, camPos.y, camPos.z,
                     &lat, &lon, &alt);
    
    return positionTy(lat, lon, alt,
                      dataRefs.GetSimTime(),
                      camPos.heading,
                      camPos.pitch,
                      camPos.roll);
}

// return the direction the camera is looking to
double DataRefs::GetViewHeading()
{
    XPLMCameraPosition_t camPos = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    // get the dataref values for current view pos, which are in local coordinates
    XPLMReadCameraPosition(&camPos);
    return camPos.heading;
}

// in current situation, shall we draw labels?
bool DataRefs::ShallDrawLabels() const
{
    // user doesn't want labels in VR but is in VR mode? -> no labels
    if (!labelShown.bVR && IsVREnabled())
        return false;
    
    // now depends on internal or external view
    return IsViewExternal() ? labelShown.bExternal : labelShown.bInternal;
}

// in current situation, toggle label drawing
bool DataRefs::ToggleLabelDraw()
{
    // Situation = VR?
    if (IsVREnabled())
        return (labelShown.bVR = !labelShown.bVR);
    // Situation = External View?
    if (IsViewExternal())
        return (labelShown.bExternal = !labelShown.bExternal);
    // Situation = Internal View
    else
        return (labelShown.bInternal = !labelShown.bInternal);
}

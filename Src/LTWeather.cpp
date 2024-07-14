/// @file       LTWeather.cpp
/// @brief      Set X-Plane weather / Fetch real weather information from AWC
///
/// @details    Set X-Plane weather based on weather information received from channels like RealTraffic
/// @note       Available with X-Plane 12 only
/// @see        https://developer.x-plane.com/article/weather-datarefs-in-x-plane-12/
///
/// @details    Fetch METAR information from Aviation Weather,
///             mainly to learn about QNH for altitude correction
/// @see        https://aviationweather.gov/data/api/#/Dataserver/dataserverMetars
/// @see        Example request: Latest weather somewhere around EDDL, limited to the fields we are interested in:
///             https://aviationweather.gov//cgi-bin/data/dataserver.php?requestType=retrieve&dataSource=metars&format=xml&boundingBox=45.5,0.5,54.5,9,5&hoursBeforeNow=2&mostRecent=true&fields=raw_text,station_id,latitude,longitude,altim_in_hg
/// @details    Example response:
///                 @code{.xml}
///                     <response xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XML-Schema-instance" version="1.2" xsi:noNamespaceSchemaLocation="http://aviationweather.gov/adds/schema/metar1_2.xsd">
///                     <request_index>71114711</request_index>
///                     <data_source name="metars"/>
///                     <request type="retrieve"/>
///                     <errors/>
///                     <warnings/>
///                     <time_taken_ms>249</time_taken_ms>
///                     <data num_results="1">
///                     <METAR>
///                     <raw_text>KL18 222035Z AUTO 23009G16KT 10SM CLR A2990 RMK AO2</raw_text>
///                     <station_id>KL18</station_id>
///                     <latitude>33.35</latitude>
///                     <longitude>-117.25</longitude>
///                     <altim_in_hg>29.899607</altim_in_hg>
///                     </METAR>
///                     </data>
///                     </response>
///                 @endcode
///
///             Example empty response (no weather reports found):
///                 @code{.xml}
///                 <response xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XML-Schema-instance" version="1.2" xsi:noNamespaceSchemaLocation="http://aviationweather.gov/adds/schema/metar1_2.xsd">
///                 <request_index>60222216</request_index>
///                 <data_source name="metars"/>
///                 <request type="retrieve"/>
///                 <errors/>
///                 <warnings/>
///                 <time_taken_ms>7</time_taken_ms>
///                 <data num_results="0"/>
///                 </response>
///                 @endcode
///
///             Example error response:
///                 @code{.xml}
///                 <response xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XML-Schema-instance" version="1.2" xsi:noNamespaceSchemaLocation="http://aviationweather.gov/adds/schema/metar1_2.xsd">
///                 <request_index>59450188</request_index>
///                 <data_source name="metars"/>
///                 <request type="retrieve"/>
///                 <errors>
///                 <error>Query must be constrained by time</error>
///                 </errors>
///                 <warnings/>
///                 <time_taken_ms>0</time_taken_ms>
///                 </response>
///                 @endcode
///
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2024 Birger Hoppe
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

//
// MARK: Set X-Plane Weather
//

/// Represents access to dataRefs
struct XDR {
    XPLMDataRef             dr = NULL;      ///< handle to the dataRef
    bool find (const char* sDR)             ///< find the dataRef, return if found
    { return (dr = XPLMFindDataRef(sDR)) != NULL; }
    bool good () const { return dr != NULL; }
};

/// Represents a float dataRef
struct XDR_float : public XDR {
    float get () const { return XPLMGetDataf(dr); }     ///< read the float value
    operator float () const { return get(); }           ///< read the float value
    void set (float v) { if (std::isfinite(v)) XPLMSetDataf(dr, v); }   ///< set the float value
    XDR_float& operator = (float v) { set(v); return *this; }
};

/// Represents a float array dataRef
template<std::size_t N>
struct XDR_farr : public XDR {
    void get (std::array<float,N>& v) const { XPLMGetDatavf(dr,v.data(),0,N ); }  ///< get data from X-Plane into local storage
    void set (std::array<float,N> & v)                  ///< write data to X-Plane (if element 0 is finite, ie. not NAN)
    { if (std::isfinite(v[0])) XPLMSetDatavf(dr,v.data(),0,N ); }
};

/// Represents an int dataRef
struct XDR_int : public XDR {
    int get () const { return XPLMGetDatai(dr); }       ///< read the int value
    operator int () const { return get(); }             ///< read the int value
    void set (int v) { XPLMSetDatai(dr, v); }           ///< set the int value
    XDR_int& operator = (int v) { set(v); return *this; }
};

    
// Weather Region dataRefs
XDR_float    wdr_visibility_reported_sm;    ///< float      y    statute_miles  = 0. The reported visibility (e.g. what the METAR/weather window says).
XDR_float    wdr_sealevel_pressure_pas;     ///< float      y    pascals        Pressure at sea level, current planet
XDR_float    wdr_sealevel_temperature_c;    ///< float      y    degreesC       The temperature at sea level.
XDR_float    wdr_qnh_base_elevation;        ///< float      y    float          Base elevation for QNH. Takes into account local physical variations from a spheroid.
XDR_float    wdr_qnh_pas;                   ///< float      y    float          Base elevation for QNH. Takes into account local physical variations from a spheroid.
XDR_float    wdr_rain_percent;              ///< float      y    ratio          [0.0 - 1.0] The percentage of rain falling.
XDR_int      wdr_change_mode;               ///< int        y    enum           How the weather is changing. 0 = Rapidly Improving, 1 = Improving, 2 = Gradually Improving, 3 = Static, 4 = Gradually Deteriorating, 5 = Deteriorating, 6 = Rapidly Deteriorating, 7 = Using Real Weather
XDR_int      wdr_weather_source;            ///< int        n    enum           What system is currently controlling the weather. 0 = Preset, 1 = Real Weather, 2 = Controlpad, 3 = Plugin.
XDR_int      wdr_update_immediately;        ///< int        y    bool           If this is true, any weather region changes EXCEPT CLOUDS will take place immediately instead of at the next update interval (currently 60 seconds).
XDR_farr<13> wdr_atmosphere_alt_levels_m;   ///< float[13]  n    meters         The altitudes for the thirteen atmospheric layers returned in other sim/weather/region datarefs.
XDR_farr<13> wdr_wind_altitude_msl_m;       ///< float[13]  y    meters         >= 0. The center altitude of this layer of wind in MSL meters.
XDR_farr<13> wdr_wind_speed_msc;            ///< float[13]  y    kts            >= 0. The wind speed in knots.
XDR_farr<13> wdr_wind_direction_degt;       ///< float[13]  y    degrees        [0 - 360] The direction the wind is blowing from in degrees from true north clockwise.
XDR_farr<13> wdr_shear_speed_msc;           ///< float[13]  y    kts            >= 0. The gain from the shear in knots.
XDR_farr<13> wdr_shear_direction_degt;      ///< float[13]  y    degrees        [0 - 360]. The direction for a wind shear, per above.
XDR_farr<13> wdr_turbulence;                ///< float[13]  y    float          [0 - 10] A turbulence factor, 0-10, the unit is just a scale.
XDR_farr<13> wdr_dewpoint_deg_c;            ///< float[13]  y    degreesC       The dew point at specified levels in the atmosphere.
XDR_farr<13> wdr_temperature_altitude_msl_m;///< float[13]  y    meters         >= 0. Altitudes used for the temperatures_aloft_deg_c array.
XDR_farr<13> wdr_temperatures_aloft_deg_c;  ///< float[13]  y    degreesC       Temperature at pressure altitudes given in sim/weather/region/atmosphere_alt_levels. If the surface is at a higher elevation, the ISA difference at wherever the surface is is assumed to extend all the way down to sea level.
XDR_farr<3>  wdr_cloud_type;                ///< float[3]   y    float          Blended cloud types per layer. 0 = Cirrus, 1 = Stratus, 2 = Cumulus, 3 = Cumulo-nimbus. Intermediate values are to be expected.
XDR_farr<3>  wdr_cloud_coverage_percent;    ///< float[3]   y    float          Cloud coverage per layer, range 0 - 1.
XDR_farr<3>  wdr_cloud_base_msl_m;          ///< float[3]   y    meters         MSL >= 0. The base altitude for this cloud layer.
XDR_farr<3>  wdr_cloud_tops_msl_m;          ///< float[3]   y    meters         >= 0. The tops for this cloud layer.
XDR_float    wdr_tropo_temp_c;              ///< float      y    degreesC       Temperature at the troposphere
XDR_float    wdr_tropo_alt_m;               ///< float      y    meters         Altitude of the troposphere
XDR_float    wdr_thermal_rate_ms;           ///< float      y    m/s            >= 0 The climb rate for thermals.
XDR_float    wdr_wave_amplitude;            ///< float      y    meters         Amplitude of waves in the water (height of waves)
XDR_float    wdr_wave_length;               ///< float      n    meters         Length of a single wave in the water - not writable starting in v12
XDR_float    wdr_wave_speed;                ///< float      n    m/s            Speed of water waves - not writable starting in v12
XDR_float    wdr_wave_dir;                  ///< float      y    degrees        Direction of waves.
XDR_float    wdr_runway_friction;           ///< float      y    enum           The friction constant for runways (how wet they are).  Dry = 0, wet(1-3), puddly(4-6), snowy(7-9), icy(10-12), snowy/icy(13-15)
XDR_float    wdr_variability_pct;           ///< float      y    ratio          How randomly variable the weather is over distance. Range 0 - 1.
XDR_int      wdr_weather_preset;            ///< int        y    enum           Read the UI weather preset that is closest to the current conditions, or set an UI preset. Clear(0), VFR Few(1), VFR Scattered(2), VFR Broken(3), VFR Marginal(4), IFR Non-precision(5), IFR Precision(6), Convective(7), Large-cell Storms(8)

bool WeatherInitDataRefs ()
{
    return true
    && wdr_visibility_reported_sm     .find("sim/weather/region/visibility_reported_sm")
    && wdr_sealevel_pressure_pas      .find("sim/weather/region/sealevel_pressure_pas")
    && wdr_sealevel_temperature_c     .find("sim/weather/region/sealevel_temperature_c")
    && wdr_qnh_base_elevation         .find("sim/weather/region/qnh_base_elevation")
    && wdr_qnh_pas                    .find("sim/weather/region/qnh_pas")
    && wdr_rain_percent               .find("sim/weather/region/rain_percent")
    && wdr_change_mode                .find("sim/weather/region/change_mode")
    && wdr_weather_source             .find("sim/weather/region/weather_source")
    && wdr_update_immediately         .find("sim/weather/region/update_immediately")
    && wdr_atmosphere_alt_levels_m    .find("sim/weather/region/atmosphere_alt_levels_m")
    && wdr_wind_altitude_msl_m        .find("sim/weather/region/wind_altitude_msl_m")
    && wdr_wind_speed_msc             .find("sim/weather/region/wind_speed_msc")
    && wdr_wind_direction_degt        .find("sim/weather/region/wind_direction_degt")
    && wdr_shear_speed_msc            .find("sim/weather/region/shear_speed_msc")
    && wdr_shear_direction_degt       .find("sim/weather/region/shear_direction_degt")
    && wdr_turbulence                 .find("sim/weather/region/turbulence")
    && wdr_dewpoint_deg_c             .find("sim/weather/region/dewpoint_deg_c")
    && wdr_temperature_altitude_msl_m .find("sim/weather/region/temperature_altitude_msl_m")
    && wdr_temperatures_aloft_deg_c   .find("sim/weather/region/temperatures_aloft_deg_c")
    && wdr_cloud_type                 .find("sim/weather/region/cloud_type")
    && wdr_cloud_coverage_percent     .find("sim/weather/region/cloud_coverage_percent")
    && wdr_cloud_base_msl_m           .find("sim/weather/region/cloud_base_msl_m")
    && wdr_cloud_tops_msl_m           .find("sim/weather/region/cloud_tops_msl_m")
    && wdr_tropo_temp_c               .find("sim/weather/region/tropo_temp_c")
    && wdr_tropo_alt_m                .find("sim/weather/region/tropo_alt_m")
    && wdr_thermal_rate_ms            .find("sim/weather/region/thermal_rate_ms")
    && wdr_wave_amplitude             .find("sim/weather/region/wave_amplitude")
    && wdr_wave_length                .find("sim/weather/region/wave_length")
    && wdr_wave_speed                 .find("sim/weather/region/wave_speed")
    && wdr_wave_dir                   .find("sim/weather/region/wave_dir")
    && wdr_runway_friction            .find("sim/weather/region/runway_friction")
    && wdr_variability_pct            .find("sim/weather/region/variability_pct")
    && wdr_weather_preset             .find("sim/weather/region/weather_preset")
    ;
}

// last position for which weather was set (to check if next one is "far" awar and deserves to be updated immedlately
positionTy LTWeather::lastPos;

// Constructor sets all arrays to all `NAN`
LTWeather::LTWeather()
{
    wdr_atmosphere_alt_levels_m.get(atmosphere_alt_levels_m);
    wind_altitude_msl_m.fill(NAN);
    wind_speed_msc.fill(NAN);
    wind_direction_degt.fill(NAN);
    shear_speed_msc.fill(NAN);
    shear_direction_degt.fill(NAN);
    turbulence.fill(NAN);
    dewpoint_deg_c.fill(NAN);
    temperature_altitude_msl_m.fill(NAN);
    temperatures_aloft_deg_c.fill(NAN);
    cloud_type.fill(NAN);
    cloud_coverage_percent.fill(NAN);
    cloud_base_msl_m.fill(NAN);
    cloud_tops_msl_m.fill(NAN);
}

// Set the given weather in X-Plane
void LTWeather::Set (bool bForceImmediately)
{
    // Set weather with immediate effect if first time, or if position changed dramatically
    const bool bImmediately = bForceImmediately || !lastPos.isNormal() || lastPos.dist(dataRefs.GetViewPos()) > WEATHER_MAX_DIST;
    if (bImmediately) {
        SHOW_MSG(logINFO, "LiveTraffic is setting X-Plane's weather");
    }
    wdr_update_immediately.set(bImmediately);
    lastPos = dataRefs.GetViewPos();

    wdr_change_mode.set(3);                         // 3 - Static (this also switches off XP's real weather)

    wdr_visibility_reported_sm.set(visibility_reported_sm);
    wdr_sealevel_pressure_pas.set(sealevel_pressure_pas);
    wdr_sealevel_temperature_c.set(sealevel_temperature_c);
    wdr_qnh_base_elevation.set(qnh_base_elevation);
    wdr_qnh_pas.set(qnh_pas);
    wdr_rain_percent.set(rain_percent);
    wdr_wind_altitude_msl_m.set(wind_altitude_msl_m);
    wdr_wind_speed_msc.set(wind_speed_msc);
    wdr_wind_direction_degt.set(wind_direction_degt);
    wdr_shear_speed_msc.set(shear_speed_msc);
    wdr_shear_direction_degt.set(shear_direction_degt);
    wdr_turbulence.set(turbulence);
    wdr_dewpoint_deg_c.set(dewpoint_deg_c);
    wdr_temperature_altitude_msl_m.set(temperature_altitude_msl_m);
    wdr_temperatures_aloft_deg_c.set(temperatures_aloft_deg_c);
    wdr_cloud_type.set(cloud_type);
    wdr_cloud_coverage_percent.set(cloud_coverage_percent);
    wdr_cloud_base_msl_m.set(cloud_base_msl_m);
    wdr_cloud_tops_msl_m.set(cloud_tops_msl_m);
    wdr_tropo_temp_c.set(tropo_temp_c);
    wdr_tropo_alt_m.set(tropo_alt_m);
    wdr_thermal_rate_ms.set(thermal_rate_ms);
    wdr_wave_amplitude.set(wave_amplitude);
    wdr_wave_dir.set(wave_dir);
    wdr_runway_friction.set(runway_friction);
    wdr_variability_pct.set(variability_pct);

    if (dataRefs.ShallLogWeather())
        Log(bImmediately ? "Set Weather immediately" : "Set Weather");
}

// Read weather from X-Plane
void LTWeather::Get (const std::string& logMsg)
{
    visibility_reported_sm      = wdr_visibility_reported_sm.get();
    sealevel_pressure_pas       = wdr_sealevel_pressure_pas.get();
    sealevel_temperature_c      = wdr_sealevel_temperature_c.get();
    qnh_base_elevation          = wdr_qnh_base_elevation.get();
    qnh_pas                     = wdr_qnh_pas.get();
    rain_percent                = wdr_rain_percent.get();
    wdr_atmosphere_alt_levels_m.get(atmosphere_alt_levels_m);
    wdr_wind_altitude_msl_m.get(wind_altitude_msl_m);
    wdr_wind_speed_msc.get(wind_speed_msc);
    wdr_wind_direction_degt.get(wind_direction_degt);
    wdr_shear_speed_msc.get(shear_speed_msc);
    wdr_shear_direction_degt.get(shear_direction_degt);
    wdr_turbulence.get(turbulence);
    wdr_dewpoint_deg_c.get(dewpoint_deg_c);
    wdr_temperature_altitude_msl_m.get(temperature_altitude_msl_m);
    wdr_temperatures_aloft_deg_c.get(temperatures_aloft_deg_c);
    wdr_cloud_type.get(cloud_type);
    wdr_cloud_coverage_percent.get(cloud_coverage_percent);
    wdr_cloud_base_msl_m.get(cloud_base_msl_m);
    wdr_cloud_tops_msl_m.get(cloud_tops_msl_m);
    tropo_temp_c                = wdr_tropo_temp_c.get();
    tropo_alt_m                 = wdr_tropo_alt_m.get();
    thermal_rate_ms             = wdr_thermal_rate_ms.get();
    wave_amplitude              = wdr_wave_amplitude.get();
    wave_dir                    = wdr_wave_dir.get();
    runway_friction             = wdr_runway_friction.get();
    variability_pct             = wdr_variability_pct.get();
    
    if (!logMsg.empty())
        Log(logMsg);
    else if (dataRefs.ShallLogWeather())
        Log("Got Weather");
}

// Log values to Log.txt
void LTWeather::Log (const std::string& msg) const
{
    std::ostringstream lOut;
    lOut << std::fixed << std::setprecision(1);
    lOut <<  "lastPos:     " << std::string(lastPos)     << "\n";
    lOut <<  "vis:         " << visibility_reported_sm   << "sm, ";
    lOut <<  "sea_pressure: "<< sealevel_pressure_pas    << "pas, ";
    lOut <<  "sea_temp: "    << sealevel_temperature_c   << "C, ";
    lOut <<  "qnh_base_elev: "<<qnh_base_elevation       << "?, ";
    lOut <<  "qnh_pas:"      << qnh_pas                  << ", ";
    lOut <<  "rain: "        << rain_percent             << "%,\n";

#define LOG_WARR(label, var, unit)                          \
lOut << label;                                              \
for (const float& f: var) lOut << std::setw(8) << f << " "; \
lOut << unit "\n";
    
    LOG_WARR("wind_alt:    ",   wind_altitude_msl_m,        "m");
    LOG_WARR("wind_speed:  ",   wind_speed_msc,             "kts");
    LOG_WARR("wind_dir:    ",   wind_direction_degt,        "deg");
    LOG_WARR("shear_speed: ",   shear_speed_msc,            "kts");
    LOG_WARR("shear_dir:   ",   shear_direction_degt,       "deg");
    LOG_WARR("turbulence:  ",   turbulence,                 "");
    LOG_WARR("deqpoint:    ",   dewpoint_deg_c,             "C");
    LOG_WARR("temp_alt:    ",   temperature_altitude_msl_m, "m");
    LOG_WARR("temp:        ",   temperatures_aloft_deg_c,   "C");
    LOG_WARR("cloud_type:  ",   cloud_type,                 "0=Ci, 1=St, 2=Cu, 3=Cb");
    LOG_WARR("cloud_cover: ",   cloud_coverage_percent,     "%");
    LOG_WARR("cloud_base:  ",   cloud_base_msl_m,           "m");
    LOG_WARR("cloud_tops:  ",   cloud_tops_msl_m,           "m");
    lOut << std::setw(0);

    lOut <<  "tropo_temp:  " << tropo_temp_c             << "c, ";
    lOut <<  "tropo_alt: "   << tropo_alt_m              << "m, ";
    lOut <<  "thermal_rate: "<< thermal_rate_ms          << "m/s, ";
    lOut <<  "wave_amp: "    << wave_amplitude           << "m, ";
    lOut <<  "wave_dir: "    << wave_dir                 << "deg, ";
    lOut <<  "rwy_fric: "    << runway_friction          << ", ";
    lOut <<  "variability: " << variability_pct          << "%\n";

    LOG_MSG(logDEBUG, "%s\n%s", msg.c_str(), lOut.str().c_str());
}

// Compute interpolation settings to fill one array from a differently sized one
std::array<LTWeather::InterpolSet,13> LTWeather::ComputeInterpol (const std::vector<float>& from,
                                                                  const std::array<float,13>& to)
{
    std::array<LTWeather::InterpolSet,13> ret;
    for (size_t i = 0; i < to.size(); ++i)                          // for each element of the destination
    {
        const float f = to[i];
        if (f <= from.front()) {                                    // Smaller than even lowest `from` value->use first value only
            ret[i].i = 0;
            ret[i].w = 1.0f;
        }
        else if (f >= from.back()) {                                // Larger than even largest `from` value
            ret[i].i = from.size()-2;                               // Use only last value
            ret[i].w = 0.0f;                                        // (Start with 2nd last index, but with weight 0, that means, use the next index, the last one, with weight 1)
        }
        else {
            // Search for f in `from`, find where it would fit inbetween
            // (as border cases are covered above must find something)
            const auto iter = std::adjacent_find(from.begin(), from.end(),
                                                 [f](const float a, const float b)
                                                 { return a <= f && f <= b; });
            if(iter != from.end()) {
                ret[i].i = (size_t)std::distance(from.begin(), iter);
                ret[i].w = 1.0f - (f - *iter)/(*(iter+1) - *iter);
            }
        }
    }
    
    return ret;
}

/// Fill values from a differently sized input vector based on interpolation
void LTWeather::Interpolate (const std::array<InterpolSet,13>& aInterpol,
                             const std::vector<float>& from,
                             std::array<float,13>& to)
{
    for (size_t i = 0; i < aInterpol.size(); ++i) {
        const InterpolSet& is = aInterpol[i];
        if (is.i+1 < from.size()) {
            to[i] = from[is.i] * is.w + from[is.i+1] * (1-is.w);
        }
        else to[i] = NAN;
    }
}


//
// MARK: Weather Network Request handling
//

/// The request URL, parameters are in this order: radius, longitude, latitude
const char* WEATHER_URL="https://aviationweather.gov/cgi-bin/data/dataserver.php?requestType=retrieve&dataSource=metars&format=xml&hoursBeforeNow=2&mostRecent=true&boundingBox=%.2f,%.2f,%.2f,%.2f&fields=raw_text,station_id,latitude,longitude,altim_in_hg";

/// Weather search radius (increment) to use if the initial weather request came back empty
constexpr float ADD_WEATHER_RADIUS_NM = 100.0f;
/// How often to add up ADD_WEATHER_RADIUS_NM before giving up?
constexpr long  MAX_WEATHER_RADIUS_FACTOR = 5;

/// suppress further error message as we had enough already?
bool gbSuppressWeatherErrMsg = false;

// Error messages
#define ERR_WEATHER_ERROR       "Weather request returned with error: %s"
#define INFO_NO_NEAR_WEATHER    "Found no nearby weather in a %.fnm radius"
#define ERR_NO_WEATHER          "Found no weather in a %.fnm radius, giving up"
#define INFO_FOUND_WEATHER_AGAIN "Successfully updated weather again from %s"

/// return the value between two xml tags
std::string GetXMLValue (const std::string& _r, const std::string& _tag,
                         std::string::size_type& pos)
{
    // find the tag
    std::string::size_type p = _r.find(_tag, pos);
    if (p == std::string::npos)         // didn't find it
        return "";
    
    // find the beginning of the _next_ tag (we don't validate any further)
    const std::string::size_type startPos = p + _tag.size();
    pos = _r.find('<', startPos);       // where the end tag begins
    if (pos != std::string::npos)
        return _r.substr(startPos, pos-startPos);
    else {
        pos = 0;                        // we overwrite pos with npos...reset to buffer's beginning for next search
        return "";
    }
}

/// @brief Process the response from aviationweather.com
/// @details Response is in XML format. (JSON is not available.)
///          We aren't doing a full XML parse here but rely on the
///          fairly static structure:
///          We straight away search for:
///            `<error>` Indicates just that and stops interpretation.\n
///            `<station_id>`, `<raw_text>`, `<latitude>`, `<longitude>`,
///            and`<altim_in_hg>` are the values we are interested in.
bool WeatherProcessResponse (const std::string& _r)
{
    float lat = NAN;
    float lon = NAN;
    float hPa = NAN;
    std::string stationId;
    std::string METAR;
    
    // Any error?
    std::string::size_type pos = 0;
    std::string val = GetXMLValue(_r, "<error>", pos);
    if (!val.empty()) {
        LOG_MSG(logERR, ERR_WEATHER_ERROR, val.c_str());
        return false;
    }
    
    // find the pressure
    val = GetXMLValue(_r, "<altim_in_hg>", pos);
    if (!val.empty()) {
        hPa = std::stof(val) * (float)HPA_per_INCH;
        
        // We fetch the other fields in order of appearance, but need to start once again from the beginning of the buffer
        pos = 0;
        // Try fetching METAR and station_id
        METAR = GetXMLValue(_r, "<raw_text>", pos);
        stationId = GetXMLValue(_r, "<station_id>", pos);

        // then let's see if we also find the weather station's location
        val = GetXMLValue(_r, "<latitude>", pos);
        if (!val.empty())
            lat = std::stof(val);
        val = GetXMLValue(_r, "<longitude>", pos);
        if (!val.empty())
            lon = std::stof(val);
        
        // tell ourselves what we found
        dataRefs.SetWeather(hPa, lat, lon, stationId, METAR);

        // found again weather after we had started to suppress messages?
        if (gbSuppressWeatherErrMsg) {
            // say hooray and report again
            LOG_MSG(logINFO, INFO_FOUND_WEATHER_AGAIN, stationId.c_str());
            gbSuppressWeatherErrMsg = false;
        }

        return true;
    }

    // didn't find weather!
    return false;
}

/// CURL callback just adding up data
size_t WeatherFetchCB(char *ptr, size_t, size_t nmemb, void* userdata)
{
    // copy buffer to our std::string
    std::string& readBuf = *reinterpret_cast<std::string*>(userdata);
    readBuf.append(ptr, nmemb);
    
    // all consumed
    return nmemb;
}

// check on X-Plane.org what version's available there
// This function would block. Idea is to call it in a thread like with std::async
bool WeatherFetch (float _lat, float _lon, float _radius_nm)
{
    // This is a communication thread's main function, set thread's name and C locale
    ThreadSettings TS ("LT_Weather", LC_ALL_MASK);

    bool bRet = false;
    try {
        char curl_errtxt[CURL_ERROR_SIZE];
        char url[512];
        std::string readBuf;
        
        // initialize the CURL handle
        CURL *pCurl = curl_easy_init();
        if (!pCurl) {
            LOG_MSG(logERR,ERR_CURL_EASY_INIT);
            return false;
        }

        // Loop in case we need to re-do a request with larger radius
        bool bRepeat = false;
        do {
            bRepeat = false;

            // put together the URL, convert nautical to statute miles
            const boundingBoxTy box (positionTy(_lat, _lon), _radius_nm * M_per_NM);
            const positionTy minPos = box.sw();
            const positionTy maxPos = box.ne();
            snprintf(url, sizeof(url), WEATHER_URL,
                     minPos.lat(), minPos.lon(),
                     maxPos.lat(), maxPos.lon());

            // prepare the handle with the right options
            readBuf.reserve(CURL_MAX_WRITE_SIZE);
            curl_easy_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
            curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, dataRefs.GetNetwTimeoutMax());
            curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, curl_errtxt);
            curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, WeatherFetchCB);
            curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &readBuf);
            curl_easy_setopt(pCurl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
            curl_easy_setopt(pCurl, CURLOPT_URL, url);

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
                    LOG_MSG(logERR, ERR_CURL_PERFORM, "Weather download", cc, curl_errtxt);
            }

            if (cc == CURLE_OK)
            {
                // CURL was OK, now check HTTP response code
                long httpResponse = 0;
                curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &httpResponse);

                // not HTTP_OK?
                if (httpResponse != HTTP_OK) {
                    LOG_MSG(logERR, ERR_CURL_PERFORM, "Weather download", (int)httpResponse, ERR_HTTP_NOT_OK);
                }
                else {
                    // Success: Process data
                    bRet = WeatherProcessResponse(readBuf);
                    // Not found weather yet?
                    if (!bRet) {
                        // How often did we apply ADD_WEATHER_RADIUS_NM already?
                        const long nRadiusFactor = std::lround(_radius_nm/ADD_WEATHER_RADIUS_NM);
                        if (nRadiusFactor < MAX_WEATHER_RADIUS_FACTOR) {
                            if (!gbSuppressWeatherErrMsg)
                                LOG_MSG(logINFO, INFO_NO_NEAR_WEATHER, _radius_nm);
                            _radius_nm = (nRadiusFactor+1) * ADD_WEATHER_RADIUS_NM;
                            bRepeat = true;
                        } else if (!gbSuppressWeatherErrMsg) {
                            LOG_MSG(logERR, ERR_NO_WEATHER, _radius_nm);
                            gbSuppressWeatherErrMsg = true;
                        }
                    }
                }
            }
        } while (bRepeat);
        
        // cleanup CURL handle
        curl_easy_cleanup(pCurl);
    }
    catch (const std::exception& e) {
        LOG_MSG(logERR, "Fetching weather failed with exception %s", e.what());
    }
    catch (...) {
        LOG_MSG(logERR, "Fetching weather failed with exception");
    }
    
    // done
    return bRet;
}


//
// MARK: Global functions
//

static bool bCanSetWeather = false;

// Initialize Weather module, dataRefs
bool WeatherInit ()
{
    bCanSetWeather = WeatherInitDataRefs();
    if (!bCanSetWeather) {
        LOG_MSG(logWARN, "Could not find all Weather dataRefs, cannot set X-Plane's weather (X-Plane < v12?)");
    }
#if DEBUG
    // TODO: Undo: in debug version now activate weather logging
    else {
        dataRefs.SetDebugLogWeather(true);
    }
#endif
    return bCanSetWeather;
}

// Shutdown Weather module
void WeatherStop ()
{
    
}

// Can we set weather? (X-Plane 12 forward only)
bool CanSetWeather ()
{
    return bCanSetWeather;
}


/// Is currently an async operation running to fetch METAR?
static std::future<bool> futWeather;

// Asynchronously, fetch fresh weather information
bool WeatherUpdate (const positionTy& pos, float radius_nm)
{
    // does only make sense in a certain latitude range
    // (During XP startup irregular values >80 show up)
    if (pos.lat() >= 80.0)
        return false;
    
    // a request still underway?
    if (futWeather.valid() &&
        futWeather.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            // then stop here
            return false;

    // start another thread with the weather request
    futWeather = std::async(std::launch::async,
                            WeatherFetch, (float)pos.lat(), (float)pos.lon(), radius_nm);
    return true;
}

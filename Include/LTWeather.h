/// @file       LTWeather.h
/// @brief      Set X-Plane weather / Fetch real weather information from AWC
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

#ifndef LTWeather_h
#define LTWeather_h

class LTWeather;

/// Initialize Weather module, dataRefs
bool WeatherInit ();
/// Shutdown Weather module
void WeatherStop ();

/// Can we, technically, set weather? (X-Plane 12 forward only)
bool WeatherCanSet ();
/// Are we controlling weather?
bool WeatherInControl ();
/// Is X-Plane set to use real weather?
bool WeatherIsXPRealWeather ();
/// Have X-Plane use its real weather
void WeatherSetXPRealWeather ();

/// Thread-safely store weather information to be set in X-Plane in the main thread later
void WeatherSet (const LTWeather& w);
/// Thread-safely store weather information to be set in X-Plane in the main thread later
void WeatherSet (const std::string& metar, const std::string& metarIcao);
/// @brief Set weather constantly to this METAR
/// @details Defines a weather solely based on the METAR, sets it,
///          then turns _off_ any further weather generation, so it stays constant.
/// @note Must be called from main thread
void WeatherSetConstant (const std::string& metar);
/// Actually update X-Plane's weather if there is anything to do (called from main thread)
void WeatherUpdate ();
/// Reset weather settings to what they were before X-Plane took over
void WeatherReset ();

/// Log current weather
void WeatherLogCurrent (const std::string& msg);

/// Current METAR in use for weather generation
const std::string& WeatherGetMETAR ();
/// Return a human readable string on the weather source, is "LiveTraffic" if WeatherInControl()
std::string WeatherGetSource ();

/// Extract QNH or SLP from METAR, NAN if not found any info, which is rather unlikely
float WeatherQNHfromMETAR (const std::string& metar);

//
// MARK: Set X-Plane Weather
//

/// Distance when next weather is set to update immediately instead of gradually
constexpr double WEATHER_MAX_DIST_M = 50 * M_per_NM;
/// Standard thickness of a METAR cloud layer [m]
constexpr float WEATHER_METAR_CLOUD_HEIGHT_M = 500;
/// Minimum thickness of a METAR cloud layer [m]
constexpr float WEATHER_MIN_CLOUD_HEIGHT_M = 100;
/// Thickness of a METAR Cumulo-nimbus cloud layer [m]
constexpr float WEATHER_METAR_CB_CLOUD_HEIGHT_M = 5000;

/// @brief Weather data to be set in X-Plane
/// @details A value of `NAN` means: don't set.
class LTWeather
{
public:
    /// Interpolation settings: indexes and weights to take over values from a differently sized float array
    struct InterpolSet {
        size_t i = 0;                               ///< lower index, other is i+1
        float w = 1.0f;                             ///< weight on lower index' value, other weight is 1.0f-w
    };
    
    positionTy pos;                                 ///< position the weather refers to, effectively the camera view pos, including its altitude
    
    float    visibility_reported_sm = NAN;          ///< float      y    statute_miles  = 0. The reported visibility (e.g. what the METAR/weather window says).
    float    sealevel_pressure_pas = NAN;           ///< float      y    pascals        Pressure at sea level, current planet
    float    sealevel_temperature_c = NAN;          ///< float      y    degreesC       The temperature at sea level.
    float    qnh_base_elevation = NAN;              ///< float      y    float          Base elevation for QNH. Takes into account local physical variations from a spheroid.
    float    qnh_pas = NAN;                         ///< float      y    float          Base elevation for QNH. Takes into account local physical variations from a spheroid.
    float    rain_percent = NAN;                    ///< float      y    ratio          [0.0 - 1.0] The percentage of rain falling.
    std::array<float,13> atmosphere_alt_levels_m;   ///< float[13]  n    meters         The altitudes for the thirteen atmospheric layers returned in other sim/weather/region datarefs.
    std::array<float,13> wind_altitude_msl_m;       ///< float[13]  y    meters         >= 0. The center altitude of this layer of wind in MSL meters.
    std::array<float,13> wind_speed_msc;            ///< float[13]  y    kts            >= 0. The wind speed in knots.
    std::array<float,13> wind_direction_degt;       ///< float[13]  y    degrees        [0 - 360] The direction the wind is blowing from in degrees from true north clockwise.
    std::array<float,13> shear_speed_msc;           ///< float[13]  y    kts            >= 0. The gain from the shear in knots.
    std::array<float,13> shear_direction_degt;      ///< float[13]  y    degrees        [0 - 360]. The direction for a wind shear, per above.
    std::array<float,13> turbulence;                ///< float[13]  y    float          [0.0 - 1.0] A turbulence factor, 0-10, the unit is just a scale.
    std::array<float,13> dewpoint_deg_c;            ///< float[13]  y    degreesC       The dew point at specified levels in the atmosphere.
    std::array<float,13> temperature_altitude_msl_m;///< float[13]  y    meters         >= 0. Altitudes used for the temperatures_aloft_deg_c array.
    std::array<float,13> temperatures_aloft_deg_c;  ///< float[13]  y    degreesC       Temperature at pressure altitudes given in sim/weather/region/atmosphere_alt_levels. If the surface is at a higher elevation, the ISA difference at wherever the surface is is assumed to extend all the way down to sea level.
    std::array<float,3>  cloud_type;                ///< float[3]   y    float          Blended cloud types per layer. 0 = Cirrus, 1 = Stratus, 2 = Cumulus, 3 = Cumulo-nimbus. Intermediate values are to be expected.
    std::array<float,3>  cloud_coverage_percent;    ///< float[3]   y    float          Cloud coverage per layer, range 0 - 1.
    std::array<float,3>  cloud_base_msl_m;          ///< float[3]   y    meters         MSL >= 0. The base altitude for this cloud layer.
    std::array<float,3>  cloud_tops_msl_m;          ///< float[3]   y    meters         >= 0. The tops for this cloud layer.
    float    tropo_temp_c = NAN;                    ///< float      y    degreesC       Temperature at the troposphere
    float    tropo_alt_m = NAN;                     ///< float      y    meters         Altitude of the troposphere
    float    thermal_rate_ms = NAN;                 ///< float      y    m/s            >= 0 The climb rate for thermals.
    float    wave_amplitude = NAN;                  ///< float      y    meters         Amplitude of waves in the water (height of waves)
    float    wave_dir = NAN;                        ///< float      y    degrees        Direction of waves.
    int      runway_friction = -1;                  ///< int        y    enum           The friction constant for runways (how wet they are).  Dry = 0, wet(1-3), puddly(4-6), snowy(7-9), icy(10-12), snowy/icy(13-15)
    float    variability_pct = 0;                   ///< float      y    ratio          How randomly variable the weather is over distance. Range 0 - 1.
    bool     update_immediately = false;            ///< int        y    bool           If this is true, any weather region changes EXCEPT CLOUDS will take place immediately instead of at the next update interval (currently 60 seconds).
    int      change_mode = -1;                      ///< int        y    enum           How the weather is changing. 0 = Rapidly Improving, 1 = Improving, 2 = Gradually Improving, 3 = Static, 4 = Gradually Deteriorating, 5 = Deteriorating, 6 = Rapidly Deteriorating, 7 = Using Real Weather
    int      weather_source = -1;                   ///< int        n    enum           What system is currently controlling the weather. 0 = Preset, 1 = Real Weather, 2 = Controlpad, 3 = Plugin.
    int      weather_preset = -1;                   ///< int        y    enum           Read the UI weather preset that is closest to the current conditions, or set an UI preset. Clear(0), VFR Few(1), VFR Scattered(2), VFR Broken(3), VFR Marginal(4), IFR Non-precision(5), IFR Precision(6), Convective(7), Large-cell Storms(8)

    // METAR
    std::string metar;                              ///< METAR, if filled combine METAR data into weather generation
    std::string metarFieldIcao;                     ///< METAR field's ICAO code
    positionTy posMetarField;                       ///< position of the field the METAR refers to

public:
    LTWeather();                                    ///< Constructor sets all arrays to all `NAN`

    /// Clear all METAR-related fields
    void ClearMETAR ();
    
    /// @brief Compute interpolation settings to fill one array from a differently sized one
    /// @details: Both arrays are supposed to be sorted ascending.
    ///           They hold e.g. altimeter values of weather layers.
    ///           The result is how to interpolate values from one layer to the other
    static std::array<InterpolSet,13> ComputeInterpol (const std::vector<float>& from,
                                                       const std::array<float,13>& to);
    /// Fill values from a differently sized input vector based on interpolation
    static void Interpolate (const std::array<InterpolSet,13>& aInterpol,
                             const std::vector<float>& from,
                             std::array<float,13>& to);
    /// @brief Fill directions/headings from a differently sized input vector based on interpolation
    /// @details Headings need to be interpolated separately as the average of 359 and 001 is 000 rather than 180...
    static void InterpolateDir (const std::array<InterpolSet,13>& aInterpol,
                                const std::vector<float>& from,
                                std::array<float,13>& to);
    
    /// Get interpolated value for a given altitude
    static float GetInterpolated (const std::array<float,13>& levels_m,
                                  const std::array<float,13>& vals,
                                  float alt_m);
    
    /// Fill value equally up to given altitude
    static void FillUp (const std::array<float,13>& levels_m,
                        std::array<float,13>& to,
                        float alt_m,
                        float val,
                        bool bInterpolateNext);
    /// Fill value equally to the given minimum up to given altitude (ie. don't overwrite values that are already larger)
    static void FillUpMin (const std::array<float,13>& levels_m,
                           std::array<float,13>& to,
                           float alt_m,
                           float valMin,
                           bool bInterpolateNext);

protected:
    void Set () const;                              ///< Set the given weather in X-Plane
    void Get (const std::string& logMsg = "");      ///< Read weather from X-Plane, if `logMsg` non-empty then log immediately (mith `logMsg` appearing on top)
    void Log (const std::string& msg) const;        ///< Log values to Log.txt

    bool IncorporateMETAR ();                       ///< add information from the METAR into the data (run from XP's main thread, so can use XP SDK, just before LTWeather::Set())
    
// Some global functions require access
friend void WeatherSet (const LTWeather& w);
friend void WeatherSet (const std::string& metar, const std::string& metarIcao);
friend void WeatherSetConstant (const std::string& metar);
friend void WeatherDoSet (bool bTakeControl);
friend void WeatherUpdate ();
friend void WeatherReset ();
friend void WeatherLogCurrent (const std::string& msg);
};

//
// MARK: Fetch METAR
//

/// Asynchronously, fetch fresh weather information
bool WeatherFetchUpdate (const positionTy& pos, float radius_nm);

#endif /* LTWeather_h */

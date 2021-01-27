/// @file       CoordCalc.h
/// @brief      Arithmetics with geographic coordinations and altitudes
/// @details    Basic calculations like distance, angle between vectors, point plus vector.\n
///             Definitions for classes positionTy, vectorTy, and boundingBoxTy.
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

#ifndef CoordCalc_h
#define CoordCalc_h

#include "XPLMScenery.h"
#include <valarray>
#include <deque>

// positions and angles are in degrees
// distances and altitude are in meters

//
// MARK: Mathematical helper functions
//
/// Square, ie. a^2
template <class T>
inline T sqr (T a) { return a*a; }

/// Pythagoras square, ie. a^2 + b^2
template <class T>
inline T pyth2 (T a, T b) { return sqr(a) + sqr(b); }

//
//MARK: Degree/Radian conversion
//      (as per stackoverflow post, adapted)
//

/// Converts degree [-180..+360] to radians [-π..+π]
constexpr inline double deg2rad (const double deg)
{ return ((deg <= 180.0 ? deg : deg-360.0) * PI / 180.0); }

/// Converts radians [-π...+π] to degree [-180..180]
constexpr inline double rad2deg (const double rad)
{ return (rad * 180 / PI); }

/// Converts radians [-π...+2π] to degree [0..360]
constexpr inline double rad2deg360 (const double rad)
{ return ((rad >= 0.0 ? rad : rad+PI+PI) * 180.0 / PI); }

// angle flown, given speed and vsi (both in m/s)
inline double vsi2deg (const double speed, const double vsi)
{ return rad2deg(std::atan2(vsi,speed)); }

//
//MARK: Functions on coordinates
//

struct positionTy;
struct vectorTy;

/// A simple two-dimensional point
struct ptTy {
    double x, y;
    ptTy () : x(NAN), y(NAN) {}
    ptTy (double _x, double _y) : x(_x), y(_y) {}
    ptTy operator + (const ptTy& _o) const { return ptTy ( x+_o.x, y+_o.y); }   ///< scalar sum
    ptTy operator - (const ptTy& _o) const { return ptTy ( x-_o.x, y-_o.y); }   ///< scalar difference
    bool operator== (const ptTy& _o) const;                                     ///< equality based on dequal() (ie. 'nearly' equal)
    bool operator!= (const ptTy& _o) const { return !operator==(_o); }          ///< unequality bases on `not equal`
    bool isValid() const { return !std::isnan(x) && !std::isnan(y); }           ///< valid if both `x` and `y` are not `NAN`
    void clear() { x = y = NAN; }                                               ///< set both `x` and `y` to `NAN`
    ptTy mirrorAt (const ptTy& _o) const                                        ///< return a point of `this` mirrored at `_o`
    { return ptTy (2*_o.x - x, 2*_o.y - y); }
    
    std::string dbgTxt () const;                                                ///< returns a string "y, x" for the point/position
};
inline ptTy operator * (double d, ptTy pt) { return ptTy ( d * pt.x, d * pt.y); }   ///< scalar multiplication
inline ptTy operator / (ptTy pt, double d) { return ptTy ( pt.x / d, pt.y / d); }   ///< scalar division

/// Vector of points
typedef std::vector<ptTy> vecPtTyT;

/// angle between two locations given in plain lat/lon
double CoordAngle (double lat1, double lon1, double lat2, double lon2);
/// distance between two locations given in plain lat/lon [meter]
double CoordDistance (double lat1, double lon1, double lat2, double lon2);
// angle between two coordinates
double CoordAngle (const positionTy& pos1, const positionTy& pos2 );
//distance between two coordinates
double CoordDistance (const positionTy& pos1, const positionTy& pos2);
// vector from one position to the other (combines both functions above)
vectorTy CoordVectorBetween (const positionTy& from, const positionTy& to );
// destination point given a starting point and a vetor
positionTy CoordPlusVector (const positionTy& pos, const vectorTy& vec);

// returns terrain altitude at given position
// returns NaN in case of failure
double YProbe_at_m (const positionTy& posAt, XPLMProbeRef& probeRef);

//
// MARK: Estimated Functions on coordinates
//

/// @brief Length of one degree latitude
/// @see https://en.wikipedia.org/wiki/Geographic_coordinate_system#Length_of_a_degree
constexpr double LAT_DEG_IN_MTR = 111132.95;

/// @brief Length of a degree longitude
/// @see https://en.wikipedia.org/wiki/Geographic_coordinate_system#Length_of_a_degree
inline double LonDegInMtr (double lat) { return LAT_DEG_IN_MTR * std::cos(deg2rad(lat)); }

/// Convert vertical distance into degree latitude
constexpr inline double Dist2Lat (double dist_m) { return dist_m / LAT_DEG_IN_MTR; }
/// Convert vertical distance into degree longitude
inline double Dist2Lon (double dist_m, double lat) { return dist_m / LonDegInMtr(lat); }
/// Convert degree latitude into vertical distance
constexpr inline double Lat2Dist (double latDiff) { return latDiff * LAT_DEG_IN_MTR; }
/// Convert degree longitude into vertical distance
inline double Lon2Dist (double lonDiff, double lat) { return lonDiff * LonDegInMtr(lat); }

/// @brief An _estimated_ **square** of the distance between 2 points given by lat/lon
/// @details Makes use simple formulas to convert lat/lon differences into meters
///          So this is not exact but quick and good enough for many purposes.
///          On short distances of less than 10m, difference to CoordDistance() is a few millimeters.
/// @return Square of distance (estimated) in meter
double DistLatLonSqr (double lat1, double lon1,
                      double lat2, double lon2);

/// @brief An _estimated_ distance between 2 points given by lat/lon
/// @details Makes use simple formulas to convert lat/lon differences into meters
///          So this is not exact but quick and good enough for many purposes.
///          On short distances of less than 10m, difference to CoordDistance() is a few millimeters.
/// @return Distance (estimated) in meter
inline double DistLatLon (double lat1, double lon1,
                          double lat2, double lon2)
{ return std::sqrt(DistLatLonSqr(lat1,lon1,lat2,lon2)); }


//
// MARK: Functions on 2D points, typically in meters
//

/// @brief Simple square of distance just by Pythagoras
inline double DistPythSqr (double x1, double y1,
                           double x2, double y2)
{ return pyth2(x2-x1, y2-y1); }

/// Return structure for DistPointToLineSqr()
struct distToLineTy {
    double      dist2 = NAN;        ///< main result: square distance of point to the line
    double      len2 = NAN;         ///< square of length of line between ln_x/y1 and ln_x/y2
    double      leg1_len2 = NAN;    ///< square length of leg from point 1 to base (base is point on the line with shortest distance to point)
    double      leg2_len2 = NAN;    ///< square length of leg from point 2 to base (base is point on the line with shortest distance to point)
    
    /// Is the base outside the endpoints of the line?
    bool IsBaseOutsideLine () const
    { return leg1_len2 > len2 || leg2_len2 > len2; }
    /// How much is the base outside the (nearer) endpoint? (squared)
    double DistSqrOfBaseBeyondLine () const
    { return std::max(leg1_len2,leg2_len2) - len2; }
    /// Resulting distance, considering also distance of base outside line as distance
    double DistSqrPlusOuts () const
    { return dist2 + (IsBaseOutsideLine() ? DistSqrOfBaseBeyondLine () : 0.0); }
};

/// @brief Square of distance between a location and a line defined by two points.
/// @note Function makes no assuptions about the coordinate system,
///       only that x and y are orthogonal. It uses good ole plain Pythagoras.
///       Ie., if x/y are in local coordinates, result is in meter.
///       if they are in geometric coordinates, result cannot be converted to an actual length,
///       but can still be used in relative comparions.
/// @note All results are square values. Functions avoids taking square roots for performance reasons.
/// @param pt_x Point's x coordinate
/// @param pt_y Point's y coordinate
/// @param ln_x1 Line: First endpoint's x coordinate
/// @param ln_y1 Line: First endpoint's y coordinate
/// @param ln_x2 Line: Second endpoint's x coordinate
/// @param ln_y2 Line: Second endpoint's y coordinate
/// @param[out] outResults Structure holding the results, see ::distToLineTy
void DistPointToLineSqr (double pt_x, double pt_y,
                         double ln_x1, double ln_y1,
                         double ln_x2, double ln_y2,
                         distToLineTy& outResults);

/// @brief Based on results from DistPointToLineSqr() computes locaton of base point (projection) on line
/// @param ln_x1 Line: First endpoint's x coordinate (same as passed in to DistPointToLineSqr())
/// @param ln_y1 Line: First endpoint's y coordinate (same as passed in to DistPointToLineSqr())
/// @param ln_x2 Line: Second endpoint's x coordinate (same as passed in to DistPointToLineSqr())
/// @param ln_y2 Line: Second endpoint's y coordinate (same as passed in to DistPointToLineSqr())
/// @param res Result returned by DistPointToLineSqr()
/// @param[out] x X coordinate of base point on the line
/// @param[out] y Y coordinate of base point on the line
void DistResultToBaseLoc (double ln_x1, double ln_y1,
                          double ln_x2, double ln_y2,
                          const distToLineTy& res,
                          double &x, double &y);

/// @brief Intersection point of two lines through given points
/// @see https://en.wikipedia.org/wiki/Line%E2%80%93line_intersection#Given_two_points_on_each_line
ptTy CoordIntersect (const ptTy& a, const ptTy& b, const ptTy& c, const ptTy& d,
                     double* pT = nullptr,
                     double* pU = nullptr);

/// @brief Calculate a point on a quadratic Bezier curve
/// @see https://en.wikipedia.org/wiki/B%C3%A9zier_curve#Quadratic_B%C3%A9zier_curves
/// @param t Range [0..1] defines which point on the curve to be returned, 0 = p0, 1 = p2
/// @param p0 Start point of curve, reached with t=0.0
/// @param p1 Control point of curve, usually not actually reached at any value of t
/// @param p2 End point of curve, reached with t=1.0
/// @param[out] pAngle If defined, receives the angle of the curve at `t` in degrees
ptTy Bezier (double t, const ptTy& p0, const ptTy& p1, const ptTy& p2,
             double* pAngle = nullptr);

/// @brief Calculate a point on a cubic Bezier curve
/// @see https://en.wikipedia.org/wiki/B%C3%A9zier_curve#Cubic_B%C3%A9zier_curves
/// @param t Range [0..1] defines which point on the curve to be returned, 0 = p0, 1 = p3
/// @param p0 Start point of curve, reached with t=0.0
/// @param p1 1st control point of curve, usually not actually reached at any value of t
/// @param p2 2nd control point of curve, usually not actually reached at any value of t
/// @param p3 End point of curve, reached with t=1.0
/// @param[out] pAngle If defined, receives the angle of the curve at `t` in degrees
ptTy Bezier (double t, const ptTy& p0, const ptTy& p1, const ptTy& p2, const ptTy& p3,
             double* pAngle = nullptr);

//
// MARK: Global enums
//

/// Flight phase
enum flightPhaseE : unsigned char {
    FPH_UNKNOWN     = 0,            ///< used for initializations
    FPH_TAXI        = 10,           ///< Taxiing
    FPH_TAKE_OFF    = 20,           ///< Group of status for take-off:
    FPH_TO_ROLL,                    ///< Take-off roll
    FPH_ROTATE,                     ///< Rotating
    FPH_LIFT_OFF,                   ///< Lift-off, until "gear-up" height
    FPH_INITIAL_CLIMB,              ///< Initial climb, until "flaps-up" height
    FPH_CLIMB       = 30,           ///< Regular climbout
    FPH_CRUISE      = 40,           ///< Cruising, no altitude change
    FPH_DESCEND     = 50,           ///< Descend, more then 100ft/min descend
    FPH_APPROACH    = 60,           ///< Approach, below "flaps-down" height
    FPH_FINAL,                      ///< Final, below "gear-down" height
    FPH_LANDING     = 70,           ///< Group of status for landing:
    FPH_FLARE,                      ///< Flare, when reaching "flare        " height
    FPH_TOUCH_DOWN,                 ///< The one cycle when plane touches down, don't rely on catching it...it's really one cycle only
    FPH_ROLL_OUT,                   ///< Roll-out after touch-down until reaching taxi speed or stopping
    FPH_STOPPED_ON_RWY              ///< Stopped on runway because ran out of tracking data, plane will disappear soon
};

/// Is this a flight phase requiring a runway?
inline bool isRwyPhase (flightPhaseE fph)
{ return fph == FPH_TAKE_OFF || fph == FPH_TO_ROLL || fph == FPH_ROTATE ||
         fph == FPH_TOUCH_DOWN || fph == FPH_ROLL_OUT; }

/// Ground status
enum onGrndE    : unsigned char {
    GND_UNKNOWN=0,                  ///< ground status yet unknown
    GND_OFF,                        ///< off the ground, airborne
    GND_ON                          ///< on the ground
};

/// Coordinates are in which kind of coordinate system?
enum coordUnitE : unsigned char {
    UNIT_WORLD=0,                   ///< world coordinates (latitude, longitude, altitude)
    UNIT_LOCAL                      ///< local GL coordinates (x, y, z)
};

/// Angles are in degree or radians?
enum angleUnitE : unsigned char {
    UNIT_DEG=0,                     ///< angles are in degree
    UNIT_RAD                        ///< angles are in radians
};

/// Position is on taxiway, runway, startup location?
enum specialPosE : unsigned char {
    SPOS_NONE=0,                    ///< no special position
    SPOS_STARTUP,                   ///< at startup location (gate, ramp, tie-down...)
    SPOS_TAXI,                      ///< snapped to taxiway
    SPOS_RWY,                       ///< snapped to runway
};

/// Return a 3 char-string for the special position enums
inline const char* SpecialPosE2String (specialPosE sp)
{
    return
    sp == SPOS_STARTUP ? "SUP" :
    sp == SPOS_TAXI    ? "TXI" :
    sp == SPOS_RWY     ? "RWY" : "   ";
}


//
//MARK: Data Structures
//

// MARK: std::valarray validation in support of analysis for issue TwinFan/LiveTraffic#207
#ifdef _MSC_VER
#define ASSERT_VALARRAY(va) LOG_ASSERT(&(va.operator[](0)) != nullptr)
#else
#define ASSERT_VALARRAY(v)
#endif

// a vector
struct vectorTy {
    double  angle;                      // degrees
    double  dist;                       // meters
    double  vsi;                        // m/s
    double  speed;                      // m/s
    
    vectorTy () : angle(NAN), dist(NAN), vsi(NAN), speed(NAN) {}
    vectorTy ( double dAngle, double dDist, double dVsi=NAN, double dSpeed=NAN ) :
    angle(dAngle), dist(dDist), vsi(dVsi), speed(dSpeed) {}
    
    /// Valid vector, ie. at least angle and distance defined?
    bool isValid () const { return !std::isnan(angle) && !std::isnan(dist); }

    // standard string for any output purposes
    operator std::string() const;
    
    // convert to nautical units
    inline double speed_kn () const { return speed * KT_per_M_per_S; }
    inline double vsi_ft () const { return vsi / Ms_per_FTm; }
};

constexpr size_t EDGE_UNKNOWN = ULONG_MAX;      ///< position's taxiway edge is unknown, not even tried to find one
constexpr size_t EDGE_UNAVAIL = EDGE_UNKNOWN-1; ///< tried finding a taxiway, but was unsuccessful

// a position: latitude (Z), longitude (X), altitude (Y), timestamp
struct positionTy {
    enum positionTyE { LAT=0, LON, ALT, TS, HEADING, PITCH, ROLL };
    std::valarray<double> v;
    
    int mergeCount = 1;      /// for posList use only: when merging positions this counts how many flight data objects made up this position
        
    /// collection of defining flags
    struct posFlagsTy {
        flightPhaseE flightPhase : 7;   ///< start of some special flight phase?
        bool         bHeadFixed  : 1;   ///< heading fixed, not to be recalculated?
        onGrndE      onGrnd      : 2;   ///< on ground or not or not known?
        coordUnitE   unitCoord   : 1;   ///< world or local coordinates?
        angleUnitE   unitAngle   : 1;   ///< heading in degree or radians?
        specialPosE  specialPos  : 2;   ///< position is somehow special`
        bool         bCutCorner  : 1;   ///< is this an (inserted) position, that can be cut short? (-> use quadratic Bezier instead of cubic)
    } f;
    
    /// The taxiway network's edge this pos is on, index into Apt::vecTaxiEdges
    size_t edgeIdx = EDGE_UNKNOWN;
public:
    positionTy () : v{NAN,NAN,NAN,NAN,NAN,NAN,NAN},
    f{FPH_UNKNOWN,false,GND_UNKNOWN,UNIT_WORLD,UNIT_DEG,SPOS_NONE,false}
    {}
    positionTy (double dLat, double dLon, double dAlt_m=NAN,
                double dTS=NAN, double dHead=NAN, double dPitch=NAN, double dRoll=NAN,
                onGrndE grnd=GND_UNKNOWN, coordUnitE uCoord=UNIT_WORLD, angleUnitE uAngle=UNIT_DEG,
                flightPhaseE fPhase = FPH_UNKNOWN) :
        v{dLat, dLon, dAlt_m, dTS, dHead, dPitch, dRoll},
        f{fPhase,false,grnd,uCoord,uAngle,SPOS_NONE,false}
    {}
    positionTy ( const XPLMProbeInfo_t& probe ) :
        positionTy ( probe.locationZ, probe.locationX, probe.locationY ) { f.unitCoord=UNIT_LOCAL; }
    positionTy ( const ptTy& _pt) :
        positionTy ( _pt.y, _pt.x ) {}
    
    // merge with the given position
    positionTy& operator |= (const positionTy& pos);
    
    // typecase to ptTy
    operator ptTy() const { return ptTy(lon(),lat()); }
    // standard string for any output purposes
    static const char* GrndE2String (onGrndE grnd);
    std::string dbgTxt() const;
    operator std::string() const;
    
    // timestamp-based comparison
    inline bool hasSimilarTS (const positionTy& p) const { return std::abs(ts()-p.ts()) <= SIMILAR_TS_INTVL; }
    inline bool canBeMergedWith (const positionTy& p) const { return hasSimilarTS(p); }
    inline int cmp (const positionTy& p)        const { return ts() < p.ts() ? -1 : (ts() > p.ts() ? 1 : 0); }
    inline bool operator<< (const positionTy& p) const { return ts() < p.ts() - SIMILAR_TS_INTVL; }
    inline bool operator<  (const positionTy& p) const { return ts() < p.ts(); }
    inline bool operator<= (const positionTy& p) const { return ts() <= p.ts() + SIMILAR_TS_INTVL; }
    inline bool operator>= (const positionTy& p) const { return ts() >= p.ts() - SIMILAR_TS_INTVL; }
    inline bool operator>  (const positionTy& p) const { return ts() > p.ts(); }
    inline bool operator>> (const positionTy& p) const { return ts() > p.ts() + SIMILAR_TS_INTVL; }

    // normalizes to -90/+90 lat, -180/+180 lon, 360° heading, return *this
    positionTy& normalize();
    // is a good valid position?
    bool isNormal (bool bAllowNanAltIfGnd = false) const;
    // is fully valid? (isNormal + heading, pitch, roll)?
    bool isFullyValid() const;
    /// Has a valid edge in the taxiway network of some airport?
    bool HasTaxiEdge () const { return edgeIdx < EDGE_UNAVAIL; }
    /// Has position been post-processed by some optimization (like snap to taxiway)?
    bool IsPostProcessed () const { return
        f.bHeadFixed || f.bCutCorner || f.specialPos != SPOS_NONE ||
        f.flightPhase != FPH_UNKNOWN || edgeIdx != EDGE_UNKNOWN;
    }
    
    // rad/deg conversion (only affects lat and lon)
    positionTy  deg2rad() const;
    positionTy& deg2rad();
    positionTy  rad2deg() const;
    positionTy& rad2deg();
    
    // named element access
    inline double lat()     const { return v[LAT]; }
    inline double lon()     const { return v[LON]; }
    inline double alt_m()   const { return v[ALT]; }                    // in meter
    inline double alt_ft()  const { return alt_m()/M_per_FT; }   // in feet
    inline double ts()      const { ASSERT_VALARRAY(v); return v[TS]; }
    inline double heading() const { return v[HEADING]; }
    inline double pitch()   const { return v[PITCH]; }
    inline double roll()    const { return v[ROLL]; }

    inline bool   IsOnGnd() const { return f.onGrnd == GND_ON; }

    inline double& lat()        { return v[LAT]; }
    inline double& lon()        { return v[LON]; }
    inline double& alt_m()      { return v[ALT]; }
    inline double& ts()         { return v[TS]; }
    inline double& heading()    { return v[HEADING]; }
    inline double& pitch()      { return v[PITCH]; }
    inline double& roll()       { return v[ROLL]; }
    
    inline void SetAltFt (double ft) { alt_m() = ft * M_per_FT; }

    // named element access using local coordinate names
    // latitude and Z go north/south
    // longitude and X go east/west
    // altitude and Y go up/down
    inline double Z() const { return v[LAT]; }
    inline double X() const { return v[LON]; }
    inline double Y() const { return v[ALT]; }
    inline double& Z() { return v[LAT]; }
    inline double& X() { return v[LON]; }
    inline double& Y() { return v[ALT]; }

    // short-cuts to coord functions
    inline double angle (const positionTy& pos2 ) const       { return CoordAngle ( *this, pos2); }
    inline double dist (const positionTy& pos2 ) const        { return CoordDistance ( *this, pos2); }
    inline vectorTy between (const positionTy& pos2 ) const   { return CoordVectorBetween ( *this, pos2); }
    inline positionTy destPos (const vectorTy& vec ) const    { return CoordPlusVector ( *this, vec); }
    inline positionTy operator+ (const vectorTy& vec ) const  { return destPos (vec); }
    inline double vsi_m (const positionTy& posTo) const       { return (posTo.alt_m()-alt_m()) / (posTo.ts()-ts()); }   // [m/s]
    inline double vsi_ft (const positionTy& posTo) const      { return vsi_m(posTo) * SEC_per_M/M_per_FT; }             // [ft/min]
    inline double speed_m (const positionTy& posTo) const     { return dist(posTo) / (posTo.ts()-ts()); }               // [m/s]
    inline double speed_kt (const positionTy& posTo) const    { return speed_m(posTo) * KT_per_M_per_S; }               // [kn]

    // move myself by a certain distance in a certain direction (normalized)
    // also changes altitude applying vec.vsi
    positionTy& operator += (const vectorTy& vec );
    
    // convert between World and Local OpenGL coordinates
    positionTy& LocalToWorld ();
    positionTy& WorldToLocal ();
};

typedef std::deque<positionTy> dequePositionTy;

// stringify all elements of a list for debugging purposes
std::string positionDeque2String (const dequePositionTy& l,
                                  const positionTy* posAfterLast = nullptr);

// find youngest position with timestamp less than parameter ts
dequePositionTy::const_iterator positionDequeFindBefore (const dequePositionTy& l, double ts);

// find two positions around given timestamp ts (before <= ts < after)
// pBefore and pAfter can come back NULL!
void positionDequeFindAdjacentTS (double ts, dequePositionTy& l,
                                  positionTy*& pBefore, positionTy*& pAfter);

// return the average of two headings, shorter side, normalized to [0;360)
double HeadingAvg (double h1, double h2, double f1=1, double f2=1);

/// @brief Difference between two headings
/// @returns number of degrees to turn from h1 to reach h2
/// -180 <= HeadingDiff <= 180
double HeadingDiff (double h1, double h2);

/// Normaize a heading to the value range [0..360)
double HeadingNormalize (double h);

/// Return an abbreviation for a heading, like N, SW
std::string HeadingText (double h);

// a bounding box has a north/west and a south/east corner
// we use positionTy for convenience...alt is usually not used here
struct boundingBoxTy {
    positionTy nw, se;
    
    boundingBoxTy () : nw(), se() {}
    boundingBoxTy (const positionTy& inNw, const positionTy& inSe ) :
    nw(inNw), se(inSe) {}
    
    /// @brief computes a bounding box based on a central position and a box width/height
    /// @param center Center position
    /// @param width Width of box in meters
    /// @param height Height of box in meters (defaults to `width`)
    boundingBoxTy (const positionTy& center, double width, double height = NAN );
    
    /// Enlarge the box by the given x/y values in meters on each side (`y` defaults to `x`)
    void enlarge_m (double x, double y = NAN);
    /// Increases the bounding box to include the given position
    void enlarge_pos (double lat, double lon);
    /// Increases the bounding box to include the given position
    void enlarge (const positionTy& lPos);
    /// Increases the bounding box to include the given position(s)
    void enlarge (std::initializer_list<positionTy> lPos) {
       for (const positionTy& pos: lPos)
            enlarge(pos);
    }
    
    /// Center point of bounding box
    positionTy center () const;

    // standard string for any output purposes
    operator std::string() const;
    
    // is position within bounding box?
    bool contains (const positionTy& pos ) const;
    bool operator & (const positionTy& pos ) const { return contains(pos); }
    
    /// Do both boxes overlap?
    bool overlap (const boundingBoxTy& o) const;
    /// Do both boxes overlap?
    bool operator & (const boundingBoxTy& o) const { return overlap(o); }
};

#endif /* CoordCalc_h */

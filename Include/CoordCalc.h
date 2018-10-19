//
//  CoordCalc.h
//  LiveTraffic
/*
 * Found on stackoverflow, code originally by iammilind, improved by 4566976
 * original: https://stackoverflow.com/questions/32096968
 * improved version: https://ideone.com/9yuONO
 *
 * I've adapted to my structures and coding style/naming
 * added a function (CoordVectorBetween) and this header file.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef CoordCalc_h
#define CoordCalc_h

#include "XPLMScenery.h"
#include <valarray>
#include <deque>

// positions and angles are in degrees
// distances and altitude are in meters

//
//MARK: Degree/Radian conversion
//      (as per stackoverflow post, adapted)
//
inline double deg2rad (const double deg)
{ return (deg * PI / 180); }

inline double rad2deg (const double rad)
{ return (rad * 180 / PI); }

// angle flown, given speed and vsi (both in m/s)
inline double vsi2deg (const double speed, const double vsi)
{ return rad2deg(std::atan2(vsi,speed)); }

//
//MARK: Functions on coordinates
//

struct positionTy;
struct vectorTy;

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
//MARK: Data Structures
//

// a vector
struct vectorTy {
    double  angle;                      // degrees
    double  dist;                       // meters
    double  vsi;                        // m/s
    double  speed;                      // m/s
    
    vectorTy () : angle(NAN), dist(NAN), vsi(NAN), speed(NAN) {}
    vectorTy ( double dAngle, double dDist, double dVsi=NAN, double dSpeed=NAN ) :
    angle(dAngle), dist(dDist), vsi(dVsi), speed(dSpeed) {}

    // standard string for any output purposes (returns static buffer!)
    operator std::string() const;
    
    // convert to nautical units
    inline double speed_kn () const { return speed * KT_per_M_per_S; }
    inline double vsi_ft () const { return vsi / Ms_per_FTm; }
};

// a position: latitude (Z), longitude (X), altitude (Y), timestamp
struct positionTy {
    enum positionTyE { LAT=0, LON, ALT, TS, HEADING, PITCH, ROLL };
    std::valarray<double> v;
    
    int mergeCount;      // for posList use only: when merging positions this counts how many flight data objects made up this position
    
    enum onGrndE    { GND_UNKNOWN=0, GND_LEAVING, GND_OFF, GND_APPROACHING, GND_ON } onGrnd;
    enum coordUnitE { UNIT_WORLD, UNIT_LOCAL } unitCoord;
    enum angleUnitE { UNIT_DEG, UNIT_RAD } unitAngle;
public:
    positionTy () : v{NAN,NAN,NAN,NAN,NAN,NAN,NAN}, mergeCount(1),
                    onGrnd(GND_UNKNOWN), unitCoord(UNIT_WORLD), unitAngle(UNIT_DEG) {}
    positionTy (double dLat, double dLon, double dAlt_m=NAN,
                double dTS=NAN, double dHead=NAN, double dPitch=NAN, double dRoll=NAN,
                onGrndE grnd=GND_UNKNOWN, coordUnitE uCoord=UNIT_WORLD, angleUnitE uAngle=UNIT_DEG) :
        v{dLat, dLon, dAlt_m, dTS, dHead, dPitch, dRoll}, mergeCount(1),
        onGrnd(grnd), unitCoord(uCoord), unitAngle(uAngle) {}
    positionTy(const XPMPPlanePosition_t& x) :
        positionTy (x.lat, x.lon, x.elevation * M_per_FT,
                    NAN, x.heading, x.pitch, x.roll) {}
    positionTy ( const XPLMProbeInfo_t& probe ) :
        positionTy ( probe.locationZ, probe.locationX, probe.locationY ) { unitCoord=UNIT_LOCAL; }
    
    // merge with the given position
    positionTy& operator |= (const positionTy& pos);
    
    // typecast to what XPMP API needs
    operator XPMPPlanePosition_t() const;
    // standard string for any output purposes (returns static buffer!)
    static const char* GrndE2String (onGrndE grnd);
    operator std::string() const;
    
    // timestamp-based comparison
    inline bool hasSimilarTS (const positionTy& p) const { return abs(ts()-p.ts()) <= SIMILAR_TS_INTVL; }
    inline bool canBeMergedWith (const positionTy& p) const { return hasSimilarTS(p) && onGrnd == p.onGrnd; }
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
    bool isNormal() const;
    // is fully valid? (isNormal + heading, pitch, roll)?
    bool isFullyValid() const;
    
    // rad/deg conversion (only affects lat and lon)
    positionTy  deg2rad() const;
    positionTy& deg2rad();
    positionTy  rad2deg() const;
    positionTy& rad2deg();
    
    // named element access
    inline double lat()     const { return v[LAT]; }
    inline double lon()     const { return v[LON]; }
    inline double alt_m()   const { return v[ALT]; }                    // in meter
    inline double alt_ft()  const { return round(alt_m()/M_per_FT); }   // in feet
    inline double ts()      const { return v[TS]; }
    inline double heading() const { return v[HEADING]; }
    inline double pitch()   const { return v[PITCH]; }
    inline double roll()    const { return v[ROLL]; }

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
 //   positionTy deltaPos (const vectorTy& vec ) const;
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
    /* FIXME: Remove
    // return an on-ground-status guess based on a from-status and a to-status
    static onGrndE DeriveGrndStatus (onGrndE from, onGrndE to);
     */
};

typedef std::deque<positionTy> dequePositionTy;

// stringify all elements of a list for debugging purposes
std::string positionDeque2String (const dequePositionTy& l);

// find youngest position with timestamp less than parameter ts
dequePositionTy::const_iterator positionDequeFindBefore (const dequePositionTy& l, double ts);

// find two positions around given timestamp ts (before <= ts < after)
// pBefore and pAfter can come back NULL!
void positionDequeFindAdjacentTS (double ts, dequePositionTy& l,
                                  positionTy*& pBefore, positionTy*& pAfter);

// return the average of two headings, shorter side, normalized to [0;360)
double AvgHeading (double h1, double h2, double f1=1, double f2=1);

// a bounding box has a north/west and a south/east corner
// we use positionTy for convenience...alt is usually not used here
struct boundingBoxTy {
    positionTy nw, se;
    
    boundingBoxTy () : nw(), se() {}
    boundingBoxTy (const positionTy& inNw, const positionTy& inSe ) :
    nw(inNw), se(inSe) {}
    // computes a bounding box based on a central position and a box width/
    // height (height defaults to width)
    boundingBoxTy (const positionTy& center, double width, double height=-1 );
    
    // standard string for any output purposes (returns static buffer!)
    operator std::string() const;
    
    // is position within bounding box?
    bool contains (const positionTy& pos ) const;
    inline bool operator & (const positionTy& pos ) { return contains(pos); }
};

#endif /* CoordCalc_h */

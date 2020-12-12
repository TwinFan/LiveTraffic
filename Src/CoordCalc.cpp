/// @file       CoordCalc.cpp
/// @brief      Arithmetics with geographic coordinations and altitudes
/// @details    Basic calculations like distance, angle between vectors, point plus vector.\n
///             Implementations for classes positionTy, vectorTy, and boundingBoxTy.
/// @see        Based on code found on stackoverflow, originally by iammilind, improved by 4566976\n
///             original: https://stackoverflow.com/questions/32096968\n
///             improved version: https://ideone.com/9yuONO\n
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

#include<iostream>
#include<iomanip>
#include<cmath>

//
// MARK: ptTy
//

bool ptTy::operator== (const ptTy& _o) const
{
    return dequal(x, _o.x) && dequal(y, _o.y);
}


std::string ptTy::dbgTxt () const
{
    char buf[100];
    snprintf(buf, sizeof(buf), "%7.5f, %7.5f", y, x);
    return std::string(buf);
}


//
// MARK: Coordinate Calc
//      (as per stackoverflow post, adapted)
//
double CoordAngle (double lat1, double lon1, double lat2, double lon2)
{
    lat1 *= PI; lat1 /= 180.0;              // in-place degree-to-rad conversion
    lon1 *= PI; lon1 /= 180.0;
    lat2 *= PI; lat2 /= 180.0;
    lon2 *= PI; lon2 /= 180.0;
    const double longitudeDifference = lon2 - lon1;
    
    using namespace std;
    const double x = (cos(lat1) * sin(lat2)) -
                     (sin(lat1) * cos(lat2) * cos(longitudeDifference));
    const double y = sin(longitudeDifference) * cos(lat2);
    
    return rad2deg360(atan2(y, x));
}

double CoordDistance (double lat1, double lon1, double lat2, double lon2)
{
    lat1 *= PI; lat1 /= 180.0;              // in-place degree-to-rad conversion
    lon1 *= PI; lon1 /= 180.0;
    lat2 *= PI; lat2 /= 180.0;
    lon2 *= PI; lon2 /= 180.0;

    using namespace std;
    const double x = sin((lat2 - lat1) / 2);
    const double y = sin((lon2 - lon1) / 2);
    return EARTH_D_M * asin(sqrt((x * x) + (cos(lat1) * cos(lat2) * y * y)));
}

double CoordAngle (const positionTy& p1, const positionTy& p2 )
{
    return CoordAngle (p1.lat(), p1.lon(), p2.lat(), p2.lon());
}

double CoordDistance (const positionTy& p1, const positionTy& p2)
{
    return CoordDistance (p1.lat(), p1.lon(), p2.lat(), p2.lon());
}

vectorTy CoordVectorBetween (const positionTy& from, const positionTy& to )
{
    double d_ts = to.ts() - from.ts();
    double dist = CoordDistance (from, to);
    return vectorTy (CoordAngle (from, to),         // angle
                     dist,                          // dist
                     dequal(d_ts,0) ? INFINITY :    // vsi
                        (to.alt_m()-from.alt_m())/d_ts,
                     dequal(d_ts,0) ? INFINITY :    // spped
                        dist/d_ts);
}

// moves the position
// calculates new altitude by applying speed and vsi
positionTy CoordPlusVector (const positionTy& p, const vectorTy& vec)
{
    const positionTy pos(p.deg2rad());
    const double vec_angle = deg2rad(vec.angle);
    const double vec_dist = vec.dist * 2 / EARTH_D_M;
    
    using namespace std;
    positionTy ret(pos);        // init with pos=p to save other values
    ret.mergeCount = 1;         // only reset merge count
    
    // altitude changes by: vsi * flight-time
    // timestamp changes by:      flight-time
    //                            flight-time is: dist / speed
    if ( !std::isnan(vec.speed) && vec.speed >= 0 ) {
        if ( !std::isnan(vec.vsi) )
            ret.alt_m() += vec.vsi * (vec.dist / vec.speed);
        ret.ts()    +=            vec.dist / vec.speed;
    }
    
    // lat/lon now to be recalculated:
    ret.lat() = asin((sin(pos.lat()) * cos(vec_dist))
                     + (cos(pos.lat()) * sin(vec_dist) * cos(vec_angle)));
    ret.lon() = pos.lon() + atan2((sin(vec_angle) * sin(vec_dist) * cos(pos.lat())),
                                  cos(vec_dist) - (sin(pos.lat()) * sin(ret.lat())));
    
    return ret.rad2deg();
}

//An estimated square of the distance between 2 points given by lat/lon
double DistLatLonSqr (double lat1, double lon1,
                      double lat2, double lon2)
{
    const double dx = (lon2-lon1) * LonDegInMtr((lat1+lat2)/2);
    const double dz = (lat2-lat1) * LAT_DEG_IN_MTR;
    return sqr(dx) + sqr(dz);
}

// Square of distance between a location and a line defined by two points.
void DistPointToLineSqr (double pt_x, double pt_y,
                         double ln_x1, double ln_y1,
                         double ln_x2, double ln_y2,
                         distToLineTy& outResults)
{
    // known input:
    const double A2 = DistPythSqr(pt_x, pt_y, ln_x2, ln_y2);    // distance point to second line end
    const double B2 = DistPythSqr(pt_x, pt_y, ln_x1, ln_y1);    // distance point to first line end
    outResults.len2 = DistPythSqr(ln_x1, ln_y1, ln_x2, ln_y2);  // line length
#define C2 (outResults.len2)
    
    // output calculation
    outResults.dist2 = A2 - (sqr(B2-C2-A2)/(4*C2));             // distance point to line's base
#define Z2 (outResults.dist2)

    outResults.leg1_len2 = B2 - Z2;                              // distance from first line endpoint to base
    outResults.leg2_len2 = A2 - Z2;                              // distance from second line endpoint to base
}

// Based on results from DistPointToLineSqr() computes locaton of base point on line
void DistResultToBaseLoc (double ln_x1, double ln_y1,
                          double ln_x2, double ln_y2,
                          const distToLineTy& res,
                          double &x, double &y)
{
    // ratio where base is between pts 1 and 2
    double r = std::sqrt(res.leg1_len2) / std::sqrt(res.len2);
    
    // If base point is outside line end points on point 1's side:
    if (res.leg2_len2 > res.len2)
        r *= -1.0;                          // make factor negative
    
    // compute actual coordinates as linear factors based on point 1:
    const double dx = ln_x2 - ln_x1;        // delta of x coordinates
    const double dy = ln_y2 - ln_y1;        // delta of y coordinates
    x = ln_x1 + r * dx;
    y = ln_y1 + r * dy;
}


// Intersection point of two lines through given points
/// @details `1` = `a`...`4` = `d`
ptTy CoordIntersect (const ptTy& a, const ptTy& b, const ptTy& c, const ptTy& d,
                     double* pT, double* pU)
{
    const double divisor =
    (a.x - b.x)*(c.y - d.y) - (a.y - b.y)*(c.x - d.x);
    
    // Are we to calculate t and u, too?
    if (pT)
        *pT = ((a.x - c.x)*(c.y - d.y) - (a.y - c.y)*(c.x - d.x))/divisor;
    if (pU)
        *pU = ((a.x - b.x)*(a.y - c.y) - (a.y - b.y)*(a.x - c.x))/divisor;
    
    // return the intersection point
    const double f1 = (a.x*b.y - a.y*b.x);
    const double f2 = (c.x*d.y - c.y*d.x);
    return (f1 * (c - d) - f2 * (a - b)) / divisor;
}


// Calculate a point on a quadratic Bezier curve
ptTy Bezier (double t, const ptTy& p0, const ptTy& p1, const ptTy& p2,
             double* pAngle)
{
    const double oneMt  = 1-t;
    
    // Angle wanted?
    if (pAngle) {
        // B'(t) = 2(1-t)(p1-p0)+2t(p2-p1)
        ptTy dtB = 2 * oneMt * (p1-p0) + 2 * t * (p2-p1);
        *pAngle = rad2deg360(std::atan2(dtB.x, dtB.y));
    }
    
    // We calculate the value directly, ie. without De-Casteljau
    // B(t) = (1-t)^2 p0 + 2(1+t)t p1 + t^2 p2
    return
    (oneMt*oneMt)   * p0 +              // (1-t)^2 p0 +
    (2 * oneMt * t) * p1 +              // 2(1+t)t p1 +
    (t*t)           * p2;               // t^2     p2
}

// Calculate a point on a cubic Bezier curve
ptTy Bezier (double t, const ptTy& p0, const ptTy& p1, const ptTy& p2, const ptTy& p3,
             double* pAngle)
{
    const double oneMt  = 1-t;
    const double oneMt2 = oneMt * oneMt;
    const double t2     = t * t;

    // Angle wanted?
    if (pAngle) {
        // B'(t) = 3(1-t)^2 (p1-p0) + 6(1-t)t(p2-p1) + 3t^2(p3-p2)
        ptTy dtB = 3*oneMt2*(p1-p0) + 6*(oneMt)*t*(p2-p1) + 3*t2*(p3-p2);
        *pAngle = rad2deg360(std::atan2(dtB.x, dtB.y));
    }
    
    // We calculate the value directly, ie. without De-Casteljau
    // B(t) = (1-t)^3 p0 + 3(1-t)^2t p1 + 3(1-t)t^2 p2 + t^3 p3
    return
    (oneMt2 * oneMt)  * p0 +            // (1-t)^3   p0 +
    (3 * oneMt2 * t)  * p1 +            // 3(1-t)^2t p1 +
    (3 * oneMt  * t2) * p2 +            // 3(1-t)t^2 p2 +
    (t2 * t)          * p3;             // t^3       p3
}


// returns terrain altitude at given position
// returns NaN in case of failure
double YProbe_at_m (const positionTy& posAt, XPLMProbeRef& probeRef)
{
    // first call, don't have handle?
    if (!probeRef)
        probeRef = XPLMCreateProbe(xplm_ProbeY);
    LOG_ASSERT(probeRef!=NULL);
    
    // works with local coordinates
    positionTy pos (posAt);
    // next call requires a valid altitude, even if it is just the altitude we want to figure out...
    if (std::isnan(pos.alt_m()))
        pos.alt_m() = 0;
    pos.WorldToLocal();
    
    // let the probe drop...
    XPLMProbeInfo_t probeInfo { sizeof(probeInfo), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    XPLMProbeResult res = XPLMProbeTerrainXYZ(probeRef,
                                              (float)pos.X(),
                                              (float)pos.Y(),
                                              (float)pos.Z(),
                                              &probeInfo);
    if (res != xplm_ProbeHitTerrain)
    {
        LOG_MSG(logDEBUG,ERR_Y_PROBE,int(res),posAt.dbgTxt().c_str());
        return 0.0;                 // assume water
    }
    
    // convert to World coordinates and save terrain altitude [in ft]
    pos = positionTy(probeInfo);
    pos.LocalToWorld();
    return pos.alt_m();             // THIS is terrain altitude beneath posAt
}

//
//MARK: vectorTy
//

vectorTy::operator std::string() const
{
    char buf[50];
    snprintf(buf, sizeof(buf), "<h %3.0f, %5.0fm @ %3.0fkt, %4.0fft/m>",
             angle, dist,
             speed_kn(),
             vsi_ft());
    return std::string(buf);
}

//
//MARK: positionTy
//

// "merges" with the given position, i.e. creates kind of an "average" position
positionTy& positionTy::operator |= (const positionTy& pos)
{
    LOG_ASSERT(f.unitCoord == pos.f.unitCoord && f.unitAngle == pos.f.unitAngle);
    // heading needs special treatment
    // (also removes nan value if one of the headings is nan)
    const double h = HeadingAvg(heading(), pos.heading(), mergeCount, pos.mergeCount);
    // take into account how many other objects made up the current pos! ("* count")

    // Special handling for possible NAN values: If NAN on either side then the other wins
    double _alt = std::isnan(alt_m()) ? pos.alt_m() : alt_m();
    double _ptc = std::isnan(pitch()) ? pos.pitch() : pitch();
    double _rol = std::isnan(roll())  ? pos.roll()  : roll();

	// previous implementation:    v = (v * mergeCount + pos.v) / (mergeCount+1);
	for (double &d : v) d *= mergeCount;						// (v * mergeCount           (VS doesn't compile v.apply with lambda function)
	v += pos.v;													//                 + pos.v)
	for (double &d : v) d /= (mergeCount + 1);					//                          / (mergeCount+1)

    heading() = h;
    
    // Handle NAN cases
    if (std::isnan(alt_m())) alt_m() = _alt;
    if (std::isnan(pitch())) pitch() = _ptc;
    if (std::isnan(roll()))  roll()  = _rol;

    mergeCount++;               // one more position object making up this position
    
    // any special flight phase? shall survive
    // (if both pos have special flight phases then ours survives)
    if (!f.flightPhase)
        f.flightPhase = pos.f.flightPhase;
    
    // ground status: if different, then the new one is likely off ground,
    //                but we have it determined soon
    if (f.onGrnd != pos.f.onGrnd)
        f.onGrnd = GND_UNKNOWN;       // IsOnGnd() will return false for this!
    
    // Special Pos and other location flags need to be re-evaluated
    f.bHeadFixed = false;
    f.specialPos = SPOS_NONE;
    f.bCutCorner = false;
    edgeIdx      = EDGE_UNKNOWN;
    
    return normalize();
}

const char* positionTy::GrndE2String (onGrndE grnd)
{
    switch (grnd) {
        case GND_OFF:           return "GND_OFF";
        case GND_ON:            return "GND_ON";
        default:                return "GND_UNKNOWN";
    }
}

std::string positionTy::dbgTxt () const
{
    char buf[200];
    snprintf(buf, sizeof(buf), "%.1f: (%7.5f, %7.5f) %7.1fft %8.8s %3.3s %2.2s %-13.13s %4.*zu {h %3.0f%c, p %3.0f, r %3.0f}",
             ts(),
             lat(), lon(),
             alt_ft(),
             GrndE2String(f.onGrnd),
             SpecialPosE2String(f.specialPos),
             f.bCutCorner ? "CT" : "  ",
             f.flightPhase ? (LTAircraft::FlightPhase2String(f.flightPhase)).c_str() : "",
             HasTaxiEdge() ? 1 : 0,
             HasTaxiEdge() ? edgeIdx : 0,
             heading(),
             (f.bHeadFixed ? '*' : ' '),
             pitch(), roll());
    return std::string(buf);
}

positionTy::operator std::string () const
{
    char buf[100];
    snprintf(buf, sizeof(buf), "%6.4f %c / %6.4f %c",
             std::abs(lat()), lat() < 0 ? 'S' : 'N',
             std::abs(lon()), lon() < 0 ? 'W' : 'E');
    return std::string(buf);
}

// normalizes to -90/+90 lat, -180/+180 lon, 360° heading, return *this
positionTy& positionTy::normalize()
{
    LOG_ASSERT(f.unitAngle==UNIT_DEG && f.unitCoord==UNIT_WORLD);
    
    // latitude: works for -180 <= lat <= 180
    LOG_ASSERT (lat() <= 180);
    LOG_ASSERT (lat() >= -180);
    if ( lat() >  90 ) lat() = 180-lat();      // crossed north pole
    if ( lat() < -90 ) lat() = 180+lat();      // crossed south pole
    
    // longitude: works for -360 <= lon <= 360
    LOG_ASSERT (lon() <= 360);
    LOG_ASSERT (lon() >= -360);
    if ( lon() >  180 ) lon() = lon()-360;      // crossed 180° meridian east-bound
    if ( lon() < -180 ) lon() = lon()+360;      // crossed 180° meridian west-bound
    
    // heading
    if (heading() >= 360.0 || heading() <= -360.0)  // normalize to -360 < head < 360
        heading() = std::fmod(heading(), 360.0);
    if (heading() < 0.0)                            // normalize to 0 <= head < 360
        heading() += 360.0;
    
    // return myself
    return *this;
}

// is a good valid position?
bool positionTy::isNormal (bool bAllowNanAltIfGnd) const
{
    LOG_ASSERT(f.unitAngle==UNIT_DEG && f.unitCoord==UNIT_WORLD);
    return
        // should be actual numbers
        ( !std::isnan(lat()) && !std::isnan(lon()) && !std::isnan(ts())) &&
        // should normal latitudes/longitudes
        (  -90 <= lat() && lat() <=    90)  &&
        ( -180 <= lon() && lon() <=   180) &&
        // altitude can be Null - but only if on ground and specifically allowed by parameter
        ( (IsOnGnd() && bAllowNanAltIfGnd) ||
        // altitude: a 'little' below MSL might be possible (dead sea),
        //           no more than 60.000ft...we are talking planes, not rockets ;)
          (!std::isnan(alt_m()) && MDL_ALT_MIN <= alt_ft() && alt_ft() <= MDL_ALT_MAX) );
}

// is fully valid? (isNormal + heading, pitch, roll)?
bool positionTy::isFullyValid() const
{
    return
    !std::isnan(heading()) && !std::isnan(pitch()) && !std::isnan(roll()) &&
    isNormal();
}


// degree/radian conversion (only affects lat/lon, other values passed through / unaffected)
positionTy positionTy::deg2rad() const
{
    positionTy ret(*this);                  // copy position
    if (f.unitAngle == UNIT_DEG) {          // if DEG convert to RAD
        ret.lat() = ::deg2rad(lat());
        ret.lon() = ::deg2rad(lon());
        ret.f.unitAngle = UNIT_RAD;
    }
    return ret;
}

positionTy& positionTy::deg2rad()
{
    if (f.unitAngle == UNIT_DEG) {          // if DEG convert to RAD
        lat() = ::deg2rad(lat());
        lon() = ::deg2rad(lon());
        f.unitAngle = UNIT_RAD;
    }
    return *this;
}

positionTy  positionTy::rad2deg() const
{
    positionTy ret(*this);                  // copy position
    if (f.unitAngle == UNIT_RAD) {          // if DEG convert to RAD
        ret.lat() = ::rad2deg(lat());
        ret.lon() = ::rad2deg(lon());
        ret.f.unitAngle = UNIT_DEG;
    }
    return ret;
}

positionTy& positionTy::rad2deg()
{
    if (f.unitAngle == UNIT_RAD) {          // if DEG convert to RAD
        lat() = ::rad2deg(lat());
        lon() = ::rad2deg(lon());
        f.unitAngle = UNIT_DEG;
    }
    return *this;
}

// move myself by a certain distance in a certain direction (but don't touch alt)
positionTy& positionTy::operator += (const vectorTy& vec )
{
    // overwrite myself with new position
    *this = destPos(vec);
    return normalize();                 // normalize and return myself
}

// convert between World and Local OpenGL coordinates
positionTy& positionTy::LocalToWorld()
{
    if (f.unitCoord == UNIT_LOCAL) {
        XPLMLocalToWorld(X(), Y(), Z(),
                         &lat(), &lon(), &alt_m());
        f.unitCoord = UNIT_WORLD;
    }
    return *this;
}

positionTy& positionTy::WorldToLocal()
{
    if (f.unitCoord == UNIT_WORLD) {
        XPLMWorldToLocal(lat(), lon(), alt_m(),
                         &X(), &Y(), &Z());
        f.unitCoord = UNIT_LOCAL;
    }
    return *this;
}

//
//MARK: dequePositionTy
//

// stringify all elements of a list
std::string positionDeque2String (const dequePositionTy& _l,
                                  const positionTy* posAfterLast)
{
    std::string ret;
    
    if (_l.empty())
        ret = "<empty>\n";
    else {
        // copy for better thread safety
        const dequePositionTy l(_l);
        for (dequePositionTy::const_iterator iter = l.cbegin();
            iter != l.cend();
            ++iter)
        {
            ret += iter->dbgTxt();              // add position info
            if (std::next(iter) != l.cend())    // there is a next position
            {
                ret += ' ';                     // add vector to next position
                ret += iter->between(*std::next(iter));
            } else if (posAfterLast) {          // a pos after last is given for final vector?
                const vectorTy v = iter->between(*posAfterLast);
                if (v.dist > 0.00001) {         // and that pos is not about the same as current
                    ret += ' ';                 // add vector to that position
                    ret += v;
                }
            }
            ret += '\n';
        }
    }
    return ret;
}

// find youngest position with timestamp less than parameter ts
// assumes sorted list!
dequePositionTy::const_iterator positionDequeFindBefore (const dequePositionTy& l, double ts)
{
    dequePositionTy::const_iterator ret = l.cend();
    for (dequePositionTy::const_iterator iter = l.cbegin();
         iter != l.cend() && iter->ts() < ts;   // as long as valid and timestamp lower
         ++iter )
        ret = iter;                             // remember iterator value
    return ret;
}

// find two positions around given timestamp ts
// pBefore and pAfter can come back NULL!
void positionDequeFindAdjacentTS (double ts, dequePositionTy& l,
                                  positionTy*& pBefore, positionTy*& pAfter)
{
    // init
    pBefore = pAfter = nullptr;
    
    // loop
    for (positionTy& p: l) {
        if (p.ts() <= ts)
            pBefore = &p;           // while less than timestamp keep pBefore updated
        else {
            pAfter = &p;            // now found (first) position greater then ts
            return;                 // short-cut...ts in l would only further increase
        }
    }
}

// return the average of two headings, shorter side, normalized to [0;360)
// f1/f2 are linear factors, defaulting to 1
double HeadingAvg (double head1, double head2, double f1, double f2)
{
    // if either value is nan return the other (returns nan if both are nan)
    if (std::isnan(head1)) return head2;
    if (std::isnan(head2)) return head1;
    
    // if 0° North lies between head1 and head2 then simple
    // average doesn't work
    if ( std::abs(head2-head1) > 180 ) {
        // add 360° to the lesser value...then average works
        if ( head1 < head2 )
            head1 += 360;
        else
            head2 += 360;
        LOG_ASSERT ( std::abs(head2-head1) <= 180 );
    }
    
    // return average of the two, normalized to 360°
    return fmod((f1*head1+f2*head2)/(f1+f2), 360);
}

// return the smaller difference between two headings
double HeadingDiff (double head1, double head2)
{
    // if either value is nan return nan
    if (std::isnan(head1) || std::isnan(head2)) return NAN;
    
    // if 0° North lies between head1 and head2 then simple
    // diff doesn't work
    if ( std::abs(head2-head1) > 180 ) {
        // add 360° to the lesser value...then diff works
        if ( head1 < head2 )
            head1 += 360;
        else
            head2 += 360;
        LOG_ASSERT ( std::abs(head2-head1) <= 180 );
    }
    
    return head2 - head1;
}

// Normaize a heading to the value range [0..360)
double HeadingNormalize (double h)
{
    // Rarely will ever more than one statement be executed:
    while (h < 0.0)    h += 360.0;          // make sure it's non-negative
    while (h >= 360.0) h -= 360.0;          // make sure it's less than 360
    return h;
}

// Return an abbreviation for a heading, like N, SW
std::string HeadingText (double h)
{
    constexpr double CARDINAL_HALF_SEGMENT = 360.0 / 16.0 / 2.0;
    h = HeadingNormalize(h);
    double card = 0.0;
    for (const char* sCard: { "N", "NNE", "NE", "ENE",
                              "E", "ESE", "SE", "SSE",
                              "S", "SSW", "SW", "WSW",
                              "W", "WNW", "NW", "NNW" })
    {
        if (h <= card + CARDINAL_HALF_SEGMENT)
            return sCard;
        card += CARDINAL_HALF_SEGMENT * 2.0;
    }
    return "N";
}


//
//MARK: Bounding Box
//

// computes a box from a central position plus width/height
boundingBoxTy::boundingBoxTy (const positionTy& center, double width, double height ) :
nw(center), se(center)          // corners initialized with center position
{
    // now enlarge in all directions
    enlarge_m(width/2.0,height/2.0);
}
    
// Enlarge the box by the given width/height in meters (height defaults to width)
void boundingBoxTy::enlarge_m (double x, double y)
{
    // height defaults to width
    if (std::isnan(y)) y = x;
    
    // we move 45 degrees from the center point to the nw and se corners,
    // use good ole pythagoras, probably not _exact_ but good enough here
    const double d = sqrt(x*x + y*y);
    
    // let's move the corners out:
    nw += vectorTy ( 315, d, NAN, NAN );
    se += vectorTy ( 135, d, NAN, NAN );
}

// Increases the bounding box to include the given position
void boundingBoxTy::enlarge_pos (double lat, double lon)
{
    // in the special case that the bounding box isn't initialized
    // we make it the size of this point:
    if (std::isnan(nw.lon())) {
        nw = se = positionTy(lat,lon);
        return;
    }
    
    // Latitude is easy as it must be between -90 and 90 degrees
    if (lat < se.lat())
        se.lat() = lat;
    else if (lat > nw.lat())
        nw.lat() = lat;
    
    // Longitude is more complex, the bounding box can be enlarged
    // both to the east or to the west to include lPos.
    // Which way to go? We go the way with the shorter added angle
    const double diffW = HeadingDiff(nw.lon(), lon);
    const double diffE = HeadingDiff(se.lon(), lon);
    
    // There are 2 special cases:
    // 1. The longitude is already included in the bounding box if diffW points east _and_ diffE points west,
    // 2. The bounding box is a single point
    if (dequal(diffW, diffE)) {
        if (diffW < 0.0)                    // extend west-ward
            nw.lon() = lon;
        else                                // else east-ward
            se.lon() = lon;
    }
    // in all other cases we change the edge which requires least change:
    else if (diffW <= 0.0 || diffE >= 0.0)
    {
        if (fabs(diffW) < fabs(diffE))
            nw.lon() = lon;
        else
            se.lon() = lon;
    }
}

// Increases the bounding box to include the given position
void boundingBoxTy::enlarge (const positionTy& lPos)
{
    // in the special case that the bounding box isn't initialized
    // we make it the size of this point:
    if (std::isnan(nw.lon())) {
        nw = se = lPos;
        return;
    }
 
    enlarge_pos(lPos.lat(), lPos.lon());
}

// Center point of bounding box
positionTy boundingBoxTy::center () const
{
    positionTy c;
    c.lat() = (nw.lat() + se.lat()) / 2.0;
    c.lon() = nw.lon() + HeadingDiff(nw.lon(), se.lon()) / 2.0;
    c.alt_m() = (nw.alt_m() + se.alt_m()) / 2.0;
    return c;
}

// standard string for any output purposes
boundingBoxTy::operator std::string() const
{
    char buf[100];
    snprintf(buf, sizeof(buf), "[(%7.3f, %7.3f) - (%7.3f, %7.3f)]",
             nw.lat(), nw.lon(),
             se.lat(), se.lon());
    return std::string(buf);
}



// checks if a given position lies within the bounding box
bool boundingBoxTy::contains (const positionTy& pos ) const
{
    // Can't handle boxes crossing the poles, sorry (isn't covered in X-Plane anyway)
    // So we assume nw latitude is greater (more north) than sw latidue
    LOG_ASSERT(nw.lat() >= se.lat());
    
    // Standard case: west longitude is less than east longitude
    if ( nw.lon() < se.lon() )
    {
        // nw must be north and west of pos / se must south and east of pos
        return  nw.lat() >= pos.lat() && pos.lat() >= se.lat() &&
                nw.lon() <= pos.lon() && pos.lon() <= se.lon();
    }
    // else: bounding box crosses 180° meridian
    else
    {
        // all negative longitudes are wrapped around the global (add 360°)
        // means: all longitudes are now between 0° and 360°
        positionTy nw2(nw), se2(se), pos2(pos);
        if (nw2.lon() < 0) nw2.lon() += 360;
        if (se2.lon() < 0) se2.lon() += 360;
        if (pos2.lon() < 0) pos2.lon() += 360;
        
        // still, w-lon could be greter than e-lon, which means that more
        // than half the earth circumfence is part of the bounding box
        if ( nw2.lon() < se2.lon())
        {
            // standard case
            return  nw2.lat() >= pos2.lat() && pos2.lat() >= se2.lat() &&
                    nw2.lon() <= pos2.lon() && pos2.lon() <= se2.lon();
        }
        else
        {
            // big box case
            return  nw2.lat() >= pos2.lat() && pos2.lat() >= se2.lat() &&
                    nw2.lon() >= pos2.lon() && pos2.lon() >= se2.lon();

        }
    }
}

// Do both boxes overlap?
/// @details The idea is to test 4 points:\n
///          Is our nw or se corner contained in o?\n
///          Is ne or sw corner of o contained in us?
bool boundingBoxTy::overlap (const boundingBoxTy& o) const
{
    // Easy cases first
    return (o.contains(nw) ||
            o.contains(se) ||
            contains(positionTy(o.nw.lat(), o.se.lon())) ||
            contains(positionTy(o.se.lat(), o.nw.lon())));
}

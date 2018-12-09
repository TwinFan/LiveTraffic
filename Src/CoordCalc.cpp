//
//  CoordCalc.cpp
//  LiveTraffic
/*
 * Based on code found on stackoverflow, originally by iammilind, improved by 4566976
 * original: https://stackoverflow.com/questions/32096968
 * improved version: https://ideone.com/9yuONO
 *
 * I (Birger Hoppe) have adapted it to my structures and coding style/naming
 * and added several functions of positionTy struct.
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

#include "LiveTraffic.h"

#include<iostream>
#include<iomanip>
#include<cmath>

 //
//MARK: Coordinate Calc
//      (as per stackoverflow post, adapted)
//
double CoordAngle (const positionTy& p1, const positionTy& p2 )
{
    const positionTy pos1(p1.deg2rad()), pos2(p2.deg2rad());
    const double longitudeDifference = pos2.lon() - pos1.lon();
    
    using namespace std;
    const double x = (cos(pos1.lat()) * sin(pos2.lat())) -
                     (sin(pos1.lat()) * cos(pos2.lat()) * cos(longitudeDifference));
    const double y = sin(longitudeDifference) * cos(pos2.lat());
    
    const double degree = rad2deg(atan2(y, x));
    return (degree >= 0)? degree : (degree + 360);
}

double CoordDistance (const positionTy& p1, const positionTy& p2)
{
    const positionTy pos1(p1.deg2rad()), pos2(p2.deg2rad());
    
    using namespace std;
    const double x = sin((pos2.lat() - pos1.lat()) / 2);
    const double y = sin((pos2.lon() - pos1.lon()) / 2);
    return EARTH_D_M * asin(sqrt((x * x) + (cos(pos1.lat()) * cos(pos2.lat()) * y * y)));
}

vectorTy CoordVectorBetween (const positionTy& from, const positionTy& to )
{
    double d_ts = to.ts() - from.ts();
    double dist = CoordDistance (from, to);
    return vectorTy (CoordAngle (from, to),         // angle
                     dist,                          // dist
                     d_ts == 0 ? INFINITY :         // vsi
                        (to.alt_m()-from.alt_m())/d_ts,
                     d_ts == 0 ? INFINITY :         // spped
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
    if ( !isnan(vec.speed) && vec.speed != 0 ) {
        if ( !isnan(vec.vsi) )
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
    pos.WorldToLocal();
    
    // let the probe drop...
    XPLMProbeInfo_t probeInfo { sizeof(probeInfo), 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    XPLMProbeResult res = XPLMProbeTerrainXYZ(probeRef,
                                              (float)pos.X(),
                                              (float)pos.Y(),
                                              (float)pos.Z(),
                                              &probeInfo);
    if (res != xplm_ProbeHitTerrain)
    {LOG_MSG(logERR,ERR_Y_PROBE,int(res),posAt.dbgTxt().c_str()); return NAN;}
    
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
    // heading needs special treatment
    const double h = AvgHeading(heading(), pos.heading(), mergeCount, pos.mergeCount);
    // take into account how many other objects made up the current pos! ("* count")

	// TODO: Test new implementation
	// previous implementation:    v = (v * mergeCount + pos.v) / (mergeCount+1);
	for (double &d : v) d *= mergeCount;						// (v * mergeCount           (VS doesn't compile v.apply with lambda function)
	v += pos.v;													//                 + pos.v)
	for (double &d : v) d /= (mergeCount + 1);					//                          / (mergeCount+1)
	// v = v.apply([=](double d)->double {return d * mergeCount; });		
	//v = v.apply([=](double d)->double {return d * (mergeCount+1); });	

    heading() = h;
    
    mergeCount++;               // one more position object making up this position
    return normalize();
}

// conversion to XPMP's type is quite straight-forward
inline double nanToZero (double d)
{ return isnan(d) ? 0.0 : d; }

positionTy::operator XPMPPlanePosition_t() const
{
    return XPMPPlanePosition_t {
        sizeof(XPMPPlanePosition_t),
        lat(),lon(),alt_ft(),
        float(nanToZero(pitch())),
        float(nanToZero(roll())),
        float(nanToZero(heading())),
        ""              // can't compute label here!
    };
}

const char* positionTy::GrndE2String (onGrndE grnd)
{
    switch (grnd) {
        case GND_LEAVING:       return "GND_LEAVING";
        case GND_OFF:           return "GND_OFF    ";
        case GND_APPROACHING:   return "GND_APPRCH ";
        case GND_ON:            return "GND_ON     ";
        default:                return "GND_UNKNOWN";
    }
}

std::string positionTy::dbgTxt () const
{
    char buf[100];
    snprintf(buf, sizeof(buf), "(%7.4f, %7.4f) %5.0ff %s {h %3.0f, p %3.0f, r %3.0f} [%.1f]",
             lat(), lon(),
             alt_ft(),
             GrndE2String(onGrnd),
             heading(), pitch(), roll(),
             ts());
    return std::string(buf);
}

positionTy::operator std::string () const
{
    char buf[100];
    snprintf(buf, sizeof(buf), "%7.4f %c / %7.4f %c",
             std::abs(lat()), lat() < 0 ? 'S' : 'N',
             std::abs(lon()), lon() < 0 ? 'W' : 'E');
    return std::string(buf);
}

// normalizes to -90/+90 lat, -180/+180 lon, 360° heading, return *this
positionTy& positionTy::normalize()
{
    LOG_ASSERT(unitAngle==UNIT_DEG && unitCoord==UNIT_WORLD);
    
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
    while ( heading() >= 360 ) heading() -= 360;
    while ( heading() <    0 ) heading() += 360;
    
    // return myself
    return *this;
}

// is a good valid position?
bool positionTy::isNormal() const
{
    LOG_ASSERT(unitAngle==UNIT_DEG && unitCoord==UNIT_WORLD);
    return
        // should be actual numbers
        ( !isnan(lat()) && !isnan(lon()) && !isnan(alt_m()) && !isnan(ts())) &&
        // should normal latitudes/longitudes
        (  -90 <= lat() && lat() <=    90)  &&
        ( -180 <= lon() && lon() <=   180) &&
        // altitude: a 'little' below MSL might be possible (dead sea),
        //           no more than 60.000ft...we are talking planes, not rockets ;)
        ( MDL_ALT_MIN <= alt_ft() && alt_ft() <= MDL_ALT_MAX);
}

// is fully valid? (isNormal + heading, pitch, roll)?
bool positionTy::isFullyValid() const
{
    return
    !isnan(heading()) && !isnan(pitch()) && !isnan(roll()) &&
    isNormal();
}


// degree/radian conversion (only affects lat/lon, other values passed through / unaffected)
positionTy positionTy::deg2rad() const
{
    positionTy ret(*this);                  // copy position
    if (unitAngle == UNIT_DEG) {            // if DEG convert to RAD
        ret.lat() = ::deg2rad(lat());
        ret.lon() = ::deg2rad(lon());
        ret.unitAngle = UNIT_RAD;
    }
    return ret;
}

positionTy& positionTy::deg2rad()
{
    if (unitAngle == UNIT_DEG) {            // if DEG convert to RAD
        lat() = ::deg2rad(lat());
        lon() = ::deg2rad(lon());
        unitAngle = UNIT_RAD;
    }
    return *this;
}

positionTy  positionTy::rad2deg() const
{
    positionTy ret(*this);                  // copy position
    if (unitAngle == UNIT_RAD) {            // if DEG convert to RAD
        ret.lat() = ::rad2deg(lat());
        ret.lon() = ::rad2deg(lon());
        ret.unitAngle = UNIT_DEG;
    }
    return ret;
}

positionTy& positionTy::rad2deg()
{
    if (unitAngle == UNIT_RAD) {            // if DEG convert to RAD
        lat() = ::rad2deg(lat());
        lon() = ::rad2deg(lon());
        unitAngle = UNIT_DEG;
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
    if ( unitCoord == UNIT_LOCAL ) {
        XPLMLocalToWorld(X(), Y(), Z(),
                         &lat(), &lon(), &alt_m());
        unitCoord = UNIT_WORLD;
    }
    return *this;
}

positionTy& positionTy::WorldToLocal()
{
    if ( unitCoord == UNIT_WORLD ) {
        XPLMWorldToLocal(lat(), lon(), alt_m(),
                         &X(), &Y(), &Z());
        unitCoord = UNIT_LOCAL;
    }
    return *this;
}
/* FIXME: Remove
// return an on-ground-status guess based on a from-status and a to-status
positionTy::onGrndE positionTy::DeriveGrndStatus (positionTy::onGrndE from,
                                                   positionTy::onGrndE to)
{
    static const onGrndE gndMatrix[GND_ON+1][GND_ON+1] = {
    // from: GND_UNKNOWN, GND_LEAVING, GND_OFF,       GND_APPROACHING, GND_ON              to:
        { GND_UNKNOWN, GND_UNKNOWN, GND_UNKNOWN,     GND_UNKNOWN,     GND_UNKNOWN },    // GND_UNKNOWN
        { GND_UNKNOWN, GND_LEAVING, GND_UNKNOWN,     GND_UNKNOWN,     GND_LEAVING },    // GND_LEAVING
        { GND_UNKNOWN, GND_LEAVING, GND_OFF ,        GND_UNKNOWN,     GND_LEAVING },    // GND_OFF
        { GND_UNKNOWN, GND_UNKNOWN, GND_APPROACHING, GND_APPROACHING, GND_UNKNOWN },    // GND_APPROACHING
        { GND_UNKNOWN, GND_UNKNOWN, GND_APPROACHING, GND_APPROACHING, GND_ON      },    // GND_ON
    };
    LOG_ASSERT(gndMatrix[to][from] != GND_UNKNOWN);
    return gndMatrix[to][from];
}
*/
//
//MARK: dequePositionTy
//

// stringify all elements of a list
std::string positionDeque2String (const dequePositionTy& _l)
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
double AvgHeading (double head1, double head2, double f1, double f2)
{
    // if 0° North lies between head1 and head2 then simple
    // average doesn't work
    if ( abs(head2-head1) > 180 ) {
        // add 360° to the lesser value...then average works
        if ( head1 < head2 )
            head1 += 360;
        else
            head2 += 360;
        LOG_ASSERT ( abs(head2-head1) <= 180 );
    }
    
    // return average of the two, normalized to 360°
    return fmod((f1*head1+f2*head2)/(f1+f2), 360);
}


//
//MARK: Bounding Box
//

// computes a box from a central position plus width/height
boundingBoxTy::boundingBoxTy (const positionTy& center, double width, double height ) :
nw(center), se(center)          // corners initialized with center position
{
    // height defaults to width
    if ( height < 0 ) height = width;
    
    // we move 45 degrees from the center point to the nw and se corners,
    // use good ole pythagoras, probably not _exact_ but good enough here
    width /= 2;             // we move only half the given distances as we start in the center
    height /= 2;
    // now for pythagoras:
    double d = sqrt(width*width + height*height);
    
    // let's move the corners out:
    nw += vectorTy ( 315, d, NAN, NAN );
    se += vectorTy ( 135, d, NAN, NAN );
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

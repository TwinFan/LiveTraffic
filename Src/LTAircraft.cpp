/// @file       LTAircraft.cpp
/// @brief      LTAircraft represents an individual tracked aircraft drawn into X-Plane's sky
/// @details    Implements helper classes MovingParam, AccelParam for flght parameters
///             that change in a controlled way (like flaps, roll, speed).\n
///             LTAircraft::FlightModel provides configuration values controlling flight modelling.\n
///             LTAircraft calculates the current position and configuration of the aircraft
///             in every flighloop cycle while being called from libxplanemp.
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2026 Birger Hoppe
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

//
//MARK: Globals
//

// previous and current cycle info
struct cycleInfo {
    int     num;                // cycle numer (which is kind of an id)
    double  simTime;            // simulated time when the cycle started
    double  diffTime;           // the simulated time difference to previous cycle
};

cycleInfo prevCycle = { -1, -1, 0 };
cycleInfo currCycle = { -1, -1, 0 };

#ifdef DEBUG
/// Is the selected aircraft currently being calculated by a callback to LTAircraft::GetPlanePosition?
static bool gSelAcCalc = false;

/// Here we keep track of last 20 frame lengths to be able to compute an average
typedef std::array<double,20> FrameLenArrTy;
static FrameLenArrTy gFrameLen;
static FrameLenArrTy::iterator gFrameLenIter;
#endif

/// This is called every frame before updating aircraft positions,
/// but only while there are aircraft to show.
// cycle the cycle...that is move the old current values to previous
// and fetch new current values
// returns true if new cycle looks valid, false indicates: re-init all a/c!
bool NextCycle (int newCycle)
{
    if ( currCycle.num >= 0 )    // not the very very first cycle?
        prevCycle = currCycle;
    else
    {
        prevCycle.num = newCycle-1;
        prevCycle.simTime  = dataRefs.GetSimTime() - 0.1;
#ifdef DEBUG
        // initialize frame lengths with 30 FPS
        gFrameLen.fill(1/30.0);
        gFrameLenIter = gFrameLen.begin();
#endif
    }
    currCycle.num = newCycle;
    currCycle.simTime  = dataRefs.GetSimTime();
    
    // the time that has passed since the last cycle
    currCycle.diffTime  = currCycle.simTime - prevCycle.simTime;

#ifdef DEBUG
    // When debugging we want to step through the data and don't mind if
    // we de-synch with reality, so if we notice too long a wait between
    // frames we assume some debugging delay and instead increase buffering time
    if (dataRefs.GetNumAc() > 0)
    {
        // Only re-init on a *substantial* backward jump (> 1 s). Sub-second
        // backward steps caused by frame stutters or brief pause/unpause
        // cycles are absorbed silently — the per-frame interpolators read
        // simTime directly, so a small reversal is just a tiny stutter on
        // the next render, not a state-corrupting event. See
        // TIME_NONLINEAR_BACKWARD_S in Constants.h for the rationale.
        if (currCycle.diffTime < TIME_NONLINEAR_BACKWARD_S) {
            // jumped backward...that has nothing to do with debugging
            dataRefs.SetReInitAll(true);
            SHOW_MSG(logWARN, ERR_TIME_NONLINEAR, currCycle.diffTime);
            return false;
        }
        // test for forward movement of time, which here we assume is due to debugging
        else if (currCycle.diffTime > 1.0) {
            const double avgFrameLen = std::accumulate(gFrameLen.cbegin(), gFrameLen.cend(), 0.0) / gFrameLen.size();
            dataRefs.fdBufDebug += currCycle.diffTime;      // increase buffering period by wait time
            dataRefs.fdBufDebug -= avgFrameLen;             // minus an average frame length
            
            // And fix this cycle
            dataRefs.UpdateSimTime();                       // recalc simTime based on fdBufDebug
            currCycle.simTime  = dataRefs.GetSimTime();
            currCycle.diffTime = currCycle.simTime - prevCycle.simTime; // should be about avgFrameLen now
        }
        // otherwise all good, store our frame time
        else {
            *gFrameLenIter = currCycle.diffTime;
            if ((++gFrameLenIter) == gFrameLen.end())
                gFrameLenIter = gFrameLen.begin();
        }
    }
#else
    // time should move forward (positive difference) and not too much either
    // If time moved to far between two calls then we better start over
    // (but no problem if no a/c yet displayed anyway)
    // Backward step: ignore sub-second blips (frame stutter, brief
    // pause/unpause, autosave). Only a substantial reversal triggers
    // re-init. Forward step: any jump beyond the buffer period means
    // our buffered data is stale and re-init is the correct response.
    // See TIME_NONLINEAR_BACKWARD_S in Constants.h for the rationale
    // behind the asymmetric thresholds.
    if (dataRefs.GetNumAc() > 0 &&
        (currCycle.diffTime < TIME_NONLINEAR_BACKWARD_S ||
         currCycle.diffTime > dataRefs.GetFdBufPeriod()) ) {
        // too much time passed...we start over and reinit all aircraft
        dataRefs.SetReInitAll(true);
        SHOW_MSG(logWARN, ERR_TIME_NONLINEAR, currCycle.diffTime);
        return false;
    }
#endif
    
    // All regular updates are collected here
    LTRegularUpdates();
    
    // tell multiplayer lib if we want to see labels
    // (these are very quick calls only setting a variable)
    // as the user can change between views any frame
    // Tell XPMP if we need labels
    if (dataRefs.ShallDrawLabels())
        XPMPEnableAircraftLabels();
    else
        XPMPDisableAircraftLabels();
    
    return true;
}

/// @brief Calculates tire rpm based on aircraft speed
/// @param kn Aircraft speed in knots
/// @return Tire revolutions per minute
inline double TireRpm (double kn)
{
    return
    // speed in meters per minute
    (kn / KT_per_M_per_S * SEC_per_M)
    // divided by tire's (constant) circumfence
    / MDL_TIRE_CF_M;
}

/// @brief Rotation: Computes turn in degrees based on rpm and timeframe
/// @param rpm Rotation speed in revolutions per minute
/// @param s Timeframe to consider in seconds
/// @return Turn amount in degrees
inline float RpmToDegree (float rpm, double s)
{
    return
    // revolutions per second
    rpm/60.0f
    // multiplied by seconds gives revolutions
    * float(s)
    // multiplied by 360 degrees per revolution gives degrees
    * 360.0f;
}

//
// MARK: MovingParam
//
MovingParam::MovingParam(double _dur,
                         double _max, double _min,
                         bool _wrap_around) :
defMin(_min), defMax(_max), defDist(_max - _min),
defDuration(_dur),
bWrapAround(_wrap_around),
valFrom(NAN), valTo(NAN), valDist(NAN),
timeFrom(NAN), timeTo(NAN),
bIncrease(true),
val(_min)
{
    LOG_ASSERT(defMax > defMin);
}

void MovingParam::SetVal(double _val)
{
    // just set the target value, no moving
    val = std::clamp<double>(_val, defMin, defMax);
    valFrom = valTo = valDist = timeFrom = timeTo = NAN;
}

// are we in motion? (i.e. moving from val to target?)
bool MovingParam::inMotion () const
{
    // (will return false if valFrom/to are NAN)
    return timeFrom <= currCycle.simTime && currCycle.simTime <= timeTo;
}

// is a move programmed or already in motion?
bool MovingParam::isProgrammed () const
{
    return !std::isnan(timeFrom) && !std::isnan(timeTo) &&
           currCycle.simTime <= timeTo;
}

// start a move to the given target in the specified time frame
void MovingParam::moveTo ( double tval, double _startTS )
{
    // Normalize between limits
    tval = std::clamp<double>(tval, defMin, defMax);
    
    // current value equals target already
    if (dequal(tval, val))
        SetVal(tval);                     // just set the target value bit-euqal, no moving
    // we shall move to a (new) given target:
    // calc required duration by using defining parameters
    else if ( !dequal(valTo, tval) ) {
        // set origin and desired target value
        valFrom = val;
        valTo = tval;
        valDist = valTo - valFrom;
        bIncrease = valDist > 0;

        // full travel from defMin to defMax takes defDuration
        // So: How much time shall we use? = Which share of the full duration do we need?
        // And: When will we be done?
        // timeTo = std::abs(valDist/defDist) * defDuration + timeFrom;
        timeFrom = std::isnan(_startTS) ? currCycle.simTime : _startTS;
        timeTo = fma(std::abs(valDist/defDist), defDuration, timeFrom);
    }
}

// pre-program a move, which is to finish by the given time
void MovingParam::moveToBy (double _from, bool _increase, double _to,
                            double _startTS, double _by_ts,
                            bool _startEarly)
{
    // Normalize between limits
    _to = std::clamp<double>(_to, defMin, defMax);

    // current value equals target already
    if (dequal(_to, val)) {
        SetVal(_to);                // just set the target value bit-euqal, no moving
    }
    // we shall move to a (new) given target:
    // calc required duration by using defining parameters
    else if ( !dequal(valTo, _to) ) {
        // default values
        if (std::isnan(_from))       _from = std::clamp<double>(val, defMin, defMax);
        if (std::isnan(_startTS))    _startTS = currCycle.simTime;

        // cleanup funny ts configurations
        if (_by_ts <= currCycle.simTime) {      // supposed to be done already?
            SetVal(_to);                        // just set the target value
            return;
        }
        if (_startTS >= _by_ts)                 // start later than end?
            _startTS = currCycle.simTime;       // -> start now
            
        // set origin and desired target value
        valFrom = _from;
        valTo = _to;
        bIncrease = _increase;
        
        // distance depends if we are going to wrap around on the way
        // standard (direct, no wrap-around) case first
        if (( _increase && _from < _to) ||
            (!_increase && _from > _to)) {
            valDist = _to - _from;
        } else {
            // wrap around cases
            LOG_ASSERT(bWrapAround);
            if (_increase)
                valDist = _to - _from + defDist; // (defMax - _from) + (_to - defMin)
            else
                valDist = _to - _from - defDist; // -((_from - defMin) + (defMax - _to))
        }
        
        // full travel from defMin to defMax takes defDuration
        // So: How much time shall we use? = Which share of the full duration do we need?
        double timeDist = std::abs(valDist/defDist * defDuration);
        
        // Do we have that much time? If not...just be quicker than configured ;)
        LOG_ASSERT(_by_ts > _startTS);
        if (timeDist > _by_ts - _startTS)
            timeDist = _by_ts - _startTS;
        
        // start now? or start late?
        if (_startEarly) {
            timeFrom = _startTS;
            timeTo = timeFrom + timeDist;
            val = valFrom;                      // start now
        } else {
            timeTo = _by_ts;
            timeFrom = timeTo - timeDist;
        }
    }
}

// pre-program a quick move the shorter way (using wrap around if necessary)
void MovingParam::moveQuickestToBy (double _from, double _to,
                                    double _startTS, double _by_ts,
                                    bool _startEarly)
{
    // default values
    if (std::isnan(_from))
        _from = val;
    
    // is the shorter way if we increase or decrease?
    if ( !bWrapAround || std::abs(_to-_from) <= defDist/2 ) {       // direct way is the only possible (no wrap-around) or it is the shorter way
        moveToBy (_from, _from <= _to, _to, _startTS, _by_ts, _startEarly);
    } else {                                    // wrap around
        moveToBy (_from, _to < _from, _to, _startTS, _by_ts, _startEarly);
    }
}


double MovingParam::get()
{
    // target time passed? -> We're done
    if ( currCycle.simTime >= timeTo ) {
        SetVal(valTo);
    } else if (inMotion()) {
        // we are actually moving. How much have we moved based on time?
        const double timeDist = timeTo - timeFrom;
        const double timePassed = currCycle.simTime - timeFrom;
        // val = (timePassed/timeDist) * valDist + valFrom;
        val = fma(timePassed/timeDist, valDist, valFrom);
        
        // normalize in case of wrap-around
        if ( bWrapAround ) {
            while ( val > defMax )
                val -= defDist;
            while ( val < defMin )
                val += defDist;
        }
    }

    // return current value
    return val;
}


double MovingParam::percDone () const
{
    if (!inMotion() || std::isnan(valDist) || between(valDist, -0.0001, 0.0001))
        return 1.0;
    else
        return std::abs((val - valFrom) / valDist);
}


//
//MARK: AccelParam
//
AccelParam::AccelParam() :
startSpeed(NAN), targetSpeed(NAN), acceleration(NAN), targetDeltaDist(NAN),
startTime(NAN), accelStartTime(NAN), targetTime(NAN),
currSpeed_m_s(NAN), currSpeed_kt(NAN)
{}

// set current speed, but blank out the rest
void AccelParam::SetSpeed (double speed)
{
    currSpeed_m_s = speed;
    currSpeed_kt = speed * KT_per_M_per_S;
    startSpeed = targetSpeed = acceleration = NAN;
    targetDeltaDist = NAN;
    startTime = accelStartTime = targetTime = NAN;
}

// starts an acceleration with given parameters
void AccelParam::StartAccel(double _startSpeed, double _targetSpeed,
                            double _accel, double _startTime)
{
    // Sanity check
    if (!(_accel > 0 ? _targetSpeed > _startSpeed : _targetSpeed < _startSpeed))
        return;
    
    // reset
    SetSpeed(_startSpeed);
    
    // set values
    startSpeed = _startSpeed;
    targetSpeed = _targetSpeed;
    acceleration = _accel;
    startTime = accelStartTime = std::isnan(_startTime) ? currCycle.simTime : _startTime;

    // pre-calculate the target delta distance, needed for ratio-calculation
    targetTime = startTime + (targetSpeed - startSpeed)/acceleration;
    targetDeltaDist = getDeltaDist(targetTime);
}

// determine speed/acceleration/deceleration
// Complication: The entire design idea is that we know positions
//       at defined timestamp. We have to be there exactly at the defined time.
//       We don't know the exact speed in that point, and even if so we
//       wouldn't know the speed change over the course of the vector
//       leading up to the target.
//       Moreover, we 'only' work with constant acceleration, i.e. the
//       speed changes linear from a start to an end. This way, despite
//       changing speed all the time we travel the exact same distance
//       as we would if we would keep the speed constant at the average
//       between start and target speed.
// Idea: We do know the average speed for a vector (distance divided by time).
//       For the position connecting two vectors (our 'to' position)
//       we just assume a target speed identical to the average speed
//       between the current vector's and the next vector's speed.
//       To make that work with our constant acceleration the start speed
//       needs to be on the opposite site of the current vectors average
//       speed to ensure we travel the right distance.
//       That still means that we will change speeds momentarily when
//       reaching/switching a position. But that sudden change should
//       on average be a lot lower than if we would just change from
//       one vector's average speed to the next.
//       To further improve target speed accuracy we take the _weighted_
//       average of vectors' speeds with a higher weight on the _shorter_
//       vector to smooth out acceleration. (Acceleration/decelaration
//       would be higher on shorter vectors in case of uniform distribution.)
void AccelParam::StartSpeedControl(double _startSpeed, double _targetSpeed,
                                   double _deltaDist,
                                   double _startTime, double _targetTime,
                                   const LTAircraft* pAc)
{
    // Sanity checks
    if (_targetTime <= currCycle.simTime || _startTime >= _targetTime)
        return;

    const double deltaTime = _targetTime - _startTime;
    const double avgSpeed = _deltaDist / deltaTime;
    
    // Sanity checks: ∆distance and ∆t should not be too close to zero
    if (dequal(_deltaDist, 0) || dequal(deltaTime, 0)) {
        SetSpeed(_targetSpeed);
        return;
    }

    // If current speed and target speed are on opposite sides of
    // average speed then we can continue with current speed
    // up to a point from which we accelerate/decelerate to 'to'-position
    // (formula documentation see Notes.txt)
    // tx =  2/∆v * (vi * ∆t + ∆v/2 * tt - d)
    
    // decelerate (currently above avg, target below avg: keep current speed
    // until it is time [tx] to deceleration)
    if (_startSpeed > avgSpeed && avgSpeed > _targetSpeed)
    {
        // is startSpeed too fast? (too far on the other side of average?)
        if (_startSpeed > avgSpeed + (avgSpeed - _targetSpeed))
            // too fast, so we reduce startSpart to mirror targetSpeed on the other side of average speed
            _startSpeed = avgSpeed + (avgSpeed - _targetSpeed);
    }
    // accelerate (currently below avg, target above)
    else if (_startSpeed < avgSpeed && avgSpeed < _targetSpeed)
    {
        // is startSpeed too slow? (too far on the other side of average?)
        if (_startSpeed < avgSpeed - (_targetSpeed - avgSpeed))
            // too slow, increase to mirror targetSPeed
            _startSpeed = avgSpeed - (_targetSpeed - avgSpeed);
        
    }
    // no way of complying to input parameters under our conditions:
    // set constant speed and return
    else {
        // output debug info on request
        if (dataRefs.GetDebugAcPos(pAc->key())) {
            LOG_MSG(logDEBUG,"CONSTANT SPEED due impossible speeds (start=%.1f,avg=%.1f=%.1fm/%.1fs,target=%.1f) for %s",
                    _startSpeed,
                    avgSpeed, _deltaDist, deltaTime,
                    _targetSpeed,
                    std::string(*pAc).c_str());
        }
        SetSpeed(avgSpeed);
        return;
    }
    
    // continue with dynamic speed:
    // calc the time when to start speed change
    const double deltaSpeed = _targetSpeed - _startSpeed;
    const double tx = 2/deltaSpeed * (_startSpeed * deltaTime +
                         deltaSpeed/2 * _targetTime -
                         _deltaDist);
    const double accel = deltaSpeed / (_targetTime - tx);
    
    // set object's values
    if (tx <= _targetTime) {            // expected...but in rare edge cases it seems possible to be violated
        SetSpeed(_startSpeed);          // reset, then set to calculated values:
        startSpeed = _startSpeed;
        targetSpeed = _targetSpeed;
        acceleration = accel;
        targetDeltaDist = _deltaDist;
        startTime = _startTime;
        accelStartTime = std::max(tx, _startTime);
        targetTime = _targetTime;
    //    if (dataRefs.GetDebugAcPos(pAc->key())) {
    //        LOG_MSG(logDEBUG,"%s: start=%.1f, in %.1fs: accel=%.1f,target=%.1f) for %s",
    //                acceleration >= 0.0 ? "ACCELERATION" : "DECELERATION",
    //                startSpeed,
    //                accelStartTime - startTime,
    //                acceleration,
    //                targetSpeed,
    //                std::string(*pAc).c_str());
    //    }
    } else {
        LOG_MSG(logERR, "tx <= _targetTime violated for %s (tx = %.1f | %.1f, %.1f, %.1f, %.1f, %.1f)",
                pAc->key().c_str(), tx,
                _startSpeed, _targetSpeed, _deltaDist, _startTime, _targetTime);
        SetSpeed(avgSpeed);
        return;
    }
}

// *** Acceleration formula ***
// The idea is: With a given acceleration (speed increases by that amount per second - or decreases if negative)
// we need speed-diff/acceleration seconds to increase start speed to target speed.
// At the beginning (∆t=0) speed is start speed, at the end (∆t=speed-diff/acceleration) speed is target speed.
// => 𝑣(∆t) = startSpeed + acceleration × ∆t
double AccelParam::updateSpeed ( double ts )
{
    // shortcut for constant speed
    if (!isChanging())
        return currSpeed_m_s;
    
    // by default use current sim time
    if (std::isnan(ts))
        ts = currCycle.simTime;
    
    // before acceleration time it's always start speed
    if (ts < accelStartTime)
        currSpeed_m_s = startSpeed;
    // after targetTime it's always targetSpeed
    else if (ts >= targetTime)
        currSpeed_m_s = targetSpeed;
    // inbetween the speed changes constantly over time:
    // 𝑣(∆t) =  acceleration × ∆t + startSpeed
    else
        currSpeed_m_s = fma(acceleration, ts-accelStartTime, startSpeed);
    
    // convert to knots
    currSpeed_kt = currSpeed_m_s * KT_per_M_per_S;
    
    // return m/s value
    return currSpeed_m_s;
}

// The integral over 𝑣 provides us with the distance:
// ∫𝑣(∆t) = 𝑑(∆t) = startSpeed × ∆t + ½ acceleration × ∆t²
double AccelParam::getDeltaDist(double ts) const
{
    // by default use current sim time
    if (std::isnan(ts))
        ts = currCycle.simTime;
    LOG_ASSERT(ts >= startTime);
    
    // shortcut for constant speed: 𝑑(∆t) = startSpeed × ∆t
    if (std::isnan(accelStartTime))
        return startSpeed * (ts - startTime);
    
    // before acceleration time there's a constant speed phase (at max until accelStartTime)
    double dist = startSpeed * (std::min(accelStartTime, ts) - startTime);

    // if ts is beyond accelStartTime
    if (ts > accelStartTime) {
        // distance travelled while accelerating after passing accelStartTime, max until targetTime
        // 𝑑(∆t) = startSpeed × ∆t + ½ acceleration × ∆t²
        //       = ∆t × (½ acceleration × ∆t + startSpeed)
        const double deltaTS = std::min(ts,targetTime) - accelStartTime;
        dist += deltaTS * fma(acceleration/2, deltaTS, startSpeed);
    }
    
    // if ts beyond target time: add distance with constant target speed
    if (ts > targetTime)
        dist += (ts - targetTime) * targetSpeed;
    
    // return that distance
    return dist;
}

// ratio is a value from 0.0 to 1.0 indicating which share of the
// distance to travel till targetDist has passed
// Useful in CalcPPos to determine PPos between 'from' and 'to'
double AccelParam::getRatio (double deltaTS ) const
{
    return getDeltaDist(deltaTS) / targetDeltaDist;
}

//
// MARK: Bezier Curves
//

// Define a quadratic Bezier Curve based on the given flight data positions
void BezierCurve::Define (const positionTy& _start,
                          const positionTy& _mid,
                          const positionTy& _end)
{
    // init
    start   = _start;
    ptCtrl  = _mid;
    end     = _end;
    
#ifdef DEBUG
    if (gSelAcCalc)
        LOG_MSG(logDEBUG, "Quadratic Bezier defined:\n%s",
                dbgTxt().c_str());
#endif

    // Convert all coordinates to meter
    ConvertToMeter();
}

// Define a quadratic Bezier Curve based on the given flight data positions, with the mid point being the intersection of the vectors
bool BezierCurve::Define (const positionTy& _start,
                          const positionTy& _end)
{
    // Find the mid point as the intersection of the vectors
    // defined by the two positions and their headings
    start = _start;                     // A = start = (0|0)
    const positionTy posB = _start.destPos(vectorTy(_start.heading(), 1000.0));
    ptTy b (posB.lon(), posB.lat());    // B = A + vector along A-heading
    ConvertToMeter(b);
    
    // End point and its vector
    ptTy c (_end.lon(), _end.lat());    // C = _end
    const positionTy posD = _end.destPos(vectorTy(_end.heading(), 1000.0));
    ptTy d (posD.lon(), posD.lat());    // D = _end + vector along C-heading
    ConvertToMeter(c);
    ConvertToMeter(d);

    // find the intersection
    ptTy mid = CoordIntersect(ptTy(0,0), b, c, d);
    ConvertToGeographic(mid);
    
    // This intersection serves our Bezier curve purposes only if a few conditions
    // are met:
    Clear();                            // reset, just in case we bail
    // 1. Must be in start-heading direction relative to start
    if (std::abs(HeadingDiff(_start.angle(mid), _start.heading())) > 15.0)
        return false;
    // 2. Must be in reverse end-heading direction relative to end
    if (std::abs(HeadingDiff(_end.angle(mid), _end.heading())) < 165.0)
        return false;
    // 3. Each leg (distance from _start/_end to mid) should be longer than, say,
    //    twice the direct distance _start/_end
    const double dist = _start.dist(_end);
    const double startDist = _start.dist(mid);
    const double endDist = _end.dist(mid);
    if (startDist > 2.0*dist || endDist > 2.0*dist)
        return false;
    // Not too short legs either, otherwise progress along the line is too non-linear
    if (startDist < 0.25*dist || endDist < 0.25*dist)
        return false;
    
    // Define the Bezier curve
    Define(_start, mid, _end);
    return true;
}

// Convert the geographic coordinates to meters, with `start` being the origin (0|0) point
/// @details The `start` point serves as origin and is - by definition - (0|0).
///          The content of `start` will not be overwritten, it is necessary for reverse conversion.
///          The other points are overwritten with the distance - in meters - to `start`.
void BezierCurve::ConvertToMeter ()
{
    end.lat() = Lat2Dist(end.lat() - start.lat());
    end.lon() = Lon2Dist(end.lon() - start.lon(), start.lat());
    ConvertToMeter(ptCtrl);
}

/// Convert the given geographic coordinates to meters
void BezierCurve::ConvertToMeter (ptTy& pt) const
{
    if (pt.isValid()) {
        pt.y = Lat2Dist(pt.y - start.lat());
        pt.x = Lon2Dist(pt.x - start.lon(), start.lat());
    }
}

// Convert the given position back to geographic coordinates
void BezierCurve::ConvertToGeographic (ptTy& pt) const
{
    if (pt.isValid()) {
        pt.y = start.lat() + Dist2Lat(pt.y);
        pt.x = start.lon() + Dist2Lon(pt.x, start.lat());
    }
}

// Clear the definition, so that BezierCurve::isDefined() will return `false`
void BezierCurve::Clear ()
{
    start.ts() = NAN;
    end.ts() = NAN;
    ptCtrl.clear();
}

// Return the position as per given timestamp, if the timestamp is between `start` and `end`
bool BezierCurve::GetPos (positionTy& pos, double _calcTs)
{
    // not defined or not in the time range of this curve?
    if (!isTsInbetween(_calcTs))
        return false;
    
    // Calculate f between [0.0..1.0]
    const double myF = (_calcTs - start.ts()) / (end.ts() - start.ts());
    if (myF < 0.0 || myF > 1.0)
        return false;
    
    // The position to return
    double angle = NAN;
    ptTy p = Bezier(myF, {0.0,0.0}, ptCtrl, end, &angle);
    LOG_ASSERT(p.isValid());
    
    // Convert the result back into geographic coordinated
    ConvertToGeographic(p);
/*
#ifdef DEBUG
    if (gSelAcCalc) {
 //       if (std::abs(pos.heading()-angle) > 1.5)
            LOG_MSG(logDEBUG, "_calcTs=%.1f, myF=%.4f, p={%s}, head=%.1f -> %.1f",
                    _calcTs, myF, p.dbgTxt().c_str(), pos.heading(), angle);
    }
#endif
*/
    // Update pos
    pos.lat() = p.y;
    pos.lon() = p.x;
    pos.alt_m() = start.alt_m() * (1-myF) + end.alt_m() * myF;
    pos.heading() = angle;
    
    // We've updated the position
    return true;
}


// Debug text output
std::string BezierCurve::dbgTxt() const
{
    if (isDefined()) {
        char s[250];
        snprintf(s, sizeof(s), "(%.5f / %.5f / %.1f @ %.1f) {%.5f %.5f} (%.5f / %.5f / %.1f @ %.1f)",
                 start.lat(), start.lon(), start.heading(), start.ts(),
                 ptCtrl.y, ptCtrl.x,
                 end.lat(), end.lon(), end.heading(), end.ts());
        return s;
    } else {
        return "<undefined>";
    }
}


//
//MARK: LTAircraft::FlightModel
//

// Calculate max possible heading change based on turn speed (max return: 180.0)
double LTAircraft::FlightModel::maxHeadChange (bool bOnGnd, double time_s) const
{
    // max return value is 180°, so if time passed in is larger
    // than half the turn time, then we max out:
    const double turnTime = bOnGnd ? TAXI_TURN_TIME : MIN_FLIGHT_TURN_TIME;
    if (time_s >= turnTime/2)
        return 180.0;
    else
        // Otherwise we could have turned less only
        return (time_s/turnTime) * 360.0;
}

// Is this modelling a glider?
bool LTAircraft::FlightModel::isGlider () const
{
    // The flight model [Glider] has to be mapped to gliders...
    return modelName == "Glider";
}

// list of flight models as read from FlightModel.prf file
std::list<LTAircraft::FlightModel> listFlightModels;

// ordered list of matches (regex|model), read from FlightModel.prf, section [Map]
typedef std::pair<std::regex,const LTAircraft::FlightModel&> regexFM;
std::list<regexFM> listFMRegex;

/// List of regular expressions matching call signs of ground vehicles
std::list<std::regex> listCarRegex;

// global constant for a default model
const LTAircraft::FlightModel MDL_DEFAULT;

// sub-functions for reading flight model file
// process a line in a Flight Model section by splitting values using a RegEx
// and then assign the value to the fm object
bool fm_processModelLine (const char* fileName, int ln,
                          std::string& text, LTAircraft::FlightModel& fm)
{
    // There is one special case: LABEL_COLOR
    if (begins_with<std::string>(text, MDL_LABEL_COLOR))
    {
        // separate LABEL_COLOR from actual value
        const std::vector<std::string> t = str_tokenize(text, " ");
        if (t.size() != 2) {
            LOG_MSG(logWARN, ERR_CFG_FORMAT, fileName, ln, text.c_str());
            return false;
        }
        // convert the value first from hex to a number, then to a float array
        conv_color(std::stoi(t[1], nullptr, 16), fm.LABEL_COLOR);
        return true;
    }
    
    // split into name and value (keeping integer and digits separate to avoid different decimal interpretation later on)
    static std::regex re ("(\\w+)\\s+(-?\\d+)(\\.\\d+)?");
    std::smatch m;
    std::regex_search(text, m, re);
    
    // two or three matches expected
    if (m.size() < 3 || m.size() > 4) {
        LOG_MSG(logWARN, ERR_CFG_FORMAT, fileName, ln, text.c_str());
        return false;
    }
    
    // name and value
    std::string name (m[1]);
    double val = std::atof(m[2].str().c_str());
    
    // Are there some decimal places given?
    // (This handling makes us independend of atof() interpreting any localized decimal point.
    //  See https://github.com/TwinFan/LiveTraffic/issues/156 )
    if (m.size() == 4 && m[3].str().size() >= 2) {
        // copy everything after the dot
        std::string decimals = m[3].str().substr(1);
        const double divisor = std::pow(10, decimals.length());
        const double dec = std::atof(decimals.c_str()) / divisor;
        val += dec;
    }
    
    // some very very basic bounds checking
    if (val < -10000 || val > 60000) {
        LOG_MSG(logWARN, ERR_CFG_VAL_INVALID, fileName, ln, text.c_str());
        return false;
    }
                           
    // now find correct member variable and assign value
#define FM_ASSIGN(nameOfVal) if (name == #nameOfVal) fm.nameOfVal = val
#define FM_ASSIGN_MIN(nameOfVal,minVal) if (name == #nameOfVal) fm.nameOfVal = std::max(val,minVal)

    FM_ASSIGN_MIN(GEAR_DURATION,1.0);       // avoid zero - this is a moving parameter
    else FM_ASSIGN_MIN(GEAR_DEFLECTION,0.1);// avoid zero - this is a moving parameter
    else FM_ASSIGN_MIN(FLAPS_DURATION,1.0); // avoid zero - this is a moving parameter
    else FM_ASSIGN(VSI_STABLE);
    else FM_ASSIGN(ROTATE_TIME);
    else FM_ASSIGN(VSI_FINAL);
    else FM_ASSIGN(VSI_INIT_CLIMB);
    else FM_ASSIGN(SPEED_INIT_CLIMB);
    else FM_ASSIGN(VSI_MAX);
    else FM_ASSIGN(AGL_GEAR_DOWN);
    else FM_ASSIGN(AGL_GEAR_UP);
    else FM_ASSIGN(AGL_FLARE);
    else FM_ASSIGN(MAX_TAXI_SPEED);
    else FM_ASSIGN(MIN_REVERS_SPEED);
    else FM_ASSIGN_MIN(TAXI_TURN_TIME,1.0); // avoid zero - this becomes a divisor
    else FM_ASSIGN(FLIGHT_TURN_TIME);
    else FM_ASSIGN_MIN(MIN_FLIGHT_TURN_TIME,1.0); // avoid zero - this becomes a divisor
    else FM_ASSIGN_MIN(ROLL_MAX_BANK,1.0);  // avoid zero - this is a moving parameter
    else FM_ASSIGN_MIN(ROLL_RATE, 1.0);     // avoid zero - this becomes a divisor
    else FM_ASSIGN(MIN_FLIGHT_SPEED);
    else FM_ASSIGN(FLAPS_UP_SPEED);
    else FM_ASSIGN(FLAPS_DOWN_SPEED);
    else FM_ASSIGN(MAX_FLIGHT_SPEED);
    else FM_ASSIGN(CRUISE_HEIGHT);
    else FM_ASSIGN(ROLL_OUT_DECEL);
    else FM_ASSIGN(PITCH_MIN);
    else FM_ASSIGN(PITCH_MIN_VSI);
    else FM_ASSIGN(PITCH_MAX);
    else FM_ASSIGN(PITCH_MAX_VSI);
    else FM_ASSIGN(PITCH_FLAP_ADD);
    else FM_ASSIGN(PITCH_FLARE);
    else FM_ASSIGN_MIN(PITCH_RATE, 1.0);    // avoid zero - this becomes a divisor
    else FM_ASSIGN(PROP_RPM_MAX);
    else FM_ASSIGN(LIGHT_LL_ALT);
    else FM_ASSIGN(EXT_CAMERA_LON_OFS);
    else FM_ASSIGN(EXT_CAMERA_LAT_OFS);
    else FM_ASSIGN(EXT_CAMERA_VERT_OFS);
    else {
        LOG_MSG(logWARN, ERR_FM_UNKNOWN_NAME, fileName, ln, text.c_str());
        return false;
    }
    
    return true;
}

// process a line in the [Map] section by splitting values using a RegEx
// and then assign the value to the fm object
bool fm_processMapLine (const char* fileName, int ln,
                        std::string& text)
{
    // split into name and value
    static std::regex re ("(\\w+)\\s+(.+)$");
    std::smatch m;
    std::regex_search(text, m, re);
    
    // two matches expected
    if (m.size() != 3) {
        LOG_MSG(logWARN, ERR_CFG_FORMAT, fileName, ln, text.c_str());
        return false;
    }
    
    // name must refer to existing section
    std::string sectionName (m[1]);
    const LTAircraft::FlightModel* pFm = LTAircraft::FlightModel::GetFlightModel(sectionName);
    if (!pFm) {
        LOG_MSG(logWARN, ERR_FM_UNKNOWN_SECTION, fileName, ln, text.c_str());
        return false;
    }
    
    // check for valid regular expression
    std::regex sectionRe;
    try {
        sectionRe.assign (m[2].str());
    } catch (const std::regex_error& e) {
        LOG_MSG(logWARN, ERR_FM_REGEX, e.what(), fileName, ln, text.c_str());
        return false;
    }
    
    // add a list entry
    listFMRegex.emplace_back(std::move(sectionRe), *pFm);
    return true;
}

/// Processes a line in the [GroundVehicles] section by just storing the regEx
bool fm_processCarLine (const char* fileName, int ln,
                        const std::string& text)
{
    // check for valid regular expression
    std::regex carRe;
    try {
        carRe.assign (text);
    } catch (const std::regex_error& e) {
        LOG_MSG(logWARN, ERR_FM_REGEX, e.what(), fileName, ln, text.c_str());
        return false;
    }
    
    // add a list entry
    listCarRegex.emplace_back(std::move(carRe));
    return true;
}


// read and process the FlightNodel.prf file
bool LTAircraft::FlightModel::ReadFlightModelFile ()
{
    const std::string ws(WHITESPACE);
    
    // open the Flight Model file
    std::string sFileName (LTCalcFullPluginPath(PATH_FLIGHT_MODELS));
    std::ifstream fIn (sFileName);
    if (!fIn) {
        // if there is no FlightModel file just return
        // that's no real problem, we can use defaults, but unexpected
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logWARN, ERR_CFG_FILE_OPEN_IN,
                 sFileName.c_str(), sErr);
        return false;
    }
    
    // first line is supposed to be the version - and we know of exactly one:
    std::vector<std::string> lnVer;
    std::string text;
    if (!safeGetline(fIn, text) ||                          // read a line
        (lnVer = str_tokenize(text, " ")).size() != 2 ||    // split into two words
        lnVer[0] != LIVE_TRAFFIC ||                         // 1. is LiveTraffic
        lnVer[1] != LT_FM_VERSION)                          // 2. is the version
    {
        // tell user, but then continue
        SHOW_MSG(logERR, ERR_CFG_FILE_VER_UNEXP, sFileName.c_str(), text.c_str(), LT_FM_VERSION);
    }
    
    // then follow sections and their entries
    /// state signifies what kind of section we are currently reading
    enum fmFileStateTy {
        FM_NO_SECTION = 0,              ///< before the first flight model section
        FM_MODEL_SECTION,               ///< processing a flight model section
        FM_MAP,                         ///< Processing the [Map] section
        FM_CAR,                         ///< Processing the [GroundVehicles] section
        FM_IGNORE,                      ///< Found a section after [Map], which we just ignore
    } fmState = FM_NO_SECTION;
    FlightModel fm;
    int errCnt = 0;
    for (int ln=1; fIn && errCnt <= ERR_CFG_FILE_MAXWARN; ln++) {
        // read entire line
        safeGetline(fIn, text);
        
        // remove trailing comments starting with '#'
        size_t pos = text.find('#');
        if (pos != std::string::npos)
            text.erase(pos);
    
        // remove leading and trailing white space
        trim(text);

        // the above assume that there is at least one non-WHITESPACE character
        // now handle the case that the entire string is all WHITESPACES:
        // As long as the last char is one of the whitespaces keep removing it
        while (!text.empty() && ws.find_first_of(text.back()) != std::string::npos)
            text.pop_back();
        
        // ignore empty lines
        if (text.empty())
            continue;
        
        // *** start a(nother) [section] ***
        if (text.front() == '[' && text.back() == ']') {
            // remove brackets (first and last character)
            text.erase(0,1);
            text.erase(text.length()-1);
            
            // finish previous model section first
            // (as [Map] has to be after FlightModel sections
            // this will safely be executed for all FlightModel sections)
            if (fm && fmState == FM_MODEL_SECTION) {
                // i.e. add the defined model to the list
                push_back_unique(listFlightModels, fm);
            }

            // identify map section?
            if (text == FM_MAP_SECTION) {
                fmState = FM_MAP;
            // identify cars section?
            } else if (text == FM_CAR_SECTION) {
                fmState = FM_CAR;
            // are we beyond all flightModel sections already?
            } else if (fmState > FM_MODEL_SECTION) {
                // There must not be unknown section any longer!
                LOG_MSG(logWARN, ERR_CFG_FORMAT, sFileName.c_str(), ln,
                        ERR_FM_NOT_AFTER_MAP);
                errCnt++;
                fmState = FM_IGNORE;
            } else {
                // no, so it must be a new model section
                fmState = FM_MODEL_SECTION;
                
                // init model with default values
                fm = FlightModel();
                fm.modelName = text;

                // is the new section a child of another section?
                std::string::size_type posSep = text.find(FM_PARENT_SEPARATOR);
                if (posSep != std::string::npos) {
                    // parent's name and model
                    const std::string parent (text.substr(posSep+1,
                                                          text.length()-posSep-1));
                    const FlightModel* pParentFM = GetFlightModel(parent);
                    
                    // init model from parent
                    if (pParentFM)
                        fm = *pParentFM;
                    else
                        LOG_MSG(logWARN, ERR_FM_UNKNOWN_PARENT, sFileName.c_str(), ln,
                                text.c_str());
                    
                    // change name to new section's name (without ":parent")
                    fm.modelName = text.substr(0,posSep);
                }
            }
        }
        // *** otherwise process section content ***
        else switch (fmState) {
                // content line before a section started is ignored
            case FM_NO_SECTION:
                LOG_MSG(logWARN, ERR_CFG_FORMAT, sFileName.c_str(), ln,
                        ERR_FM_NOT_BEFORE_SEC);
                errCnt++;
                break;
                
                // process a flight model section
            case FM_MODEL_SECTION:
                if (!fm_processModelLine(sFileName.c_str(), ln, text, fm))
                    errCnt++;
                break;
                
                // process the map of flight models to regEx patterns
            case FM_MAP:
                if (!fm_processMapLine(sFileName.c_str(), ln, text))
                    errCnt++;
                break;
                
            case FM_CAR:
                if (!fm_processCarLine(sFileName.c_str(), ln, text))
                    errCnt++;
                break;
                
            case FM_IGNORE:
                break;
        }
        
    }
    
    // problem was not just eof?
    if (!fIn && !fIn.eof()) {
        char sErr[SERR_LEN];
        strerror_s(sErr, sizeof(sErr), errno);
        SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                 sFileName.c_str(), sErr);
        return false;
    }
    
    // close file
    fIn.close();
    
    // too many warnings?
    if (errCnt > ERR_CFG_FILE_MAXWARN) {
        SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                 sFileName.c_str(), ERR_CFG_FILE_TOOMANY);
        return false;
    }
    
    // looks like success
    return true;
}

/// Returns the best possible a/c type to use based on given LTFlightData
const std::string& DetermineIcaoType (const LTFlightData& fd,
                                      bool& bDefaulted)
{
    bDefaulted = false;
    if (fd.hasAc())                                 // 1. choice: aircraft
        return fd.GetAircraft()->acIcaoType;
    
    const LTFlightData::FDStaticData& stat = fd.GetUnsafeStat();
    if (!stat.acTypeIcao.empty())                   // 2. choice: FD's static data
        return stat.acTypeIcao;
                                                    // 3. choice: Derived from model text
    const std::string& s = ModelIcaoType::getIcaoType(stat.mdl);
    if (!s.empty())
        return s;
    
    bDefaulted = true;                              // 4. choice: configured default type
    return dataRefs.GetDefaultAcIcaoType();
}

// Returns a model based on pAc's type, fd.statData's type or by trying to derive a model from statData.mdlName
const LTAircraft::FlightModel& LTAircraft::FlightModel::FindFlightModel
        (LTFlightData& fd, bool bForceSearch, const std::string** pIcaoType)
{
    // Do we have cached data available that allows to skip all the searching?
    if (!bForceSearch) {
        if (fd.hasAc() && fd.GetAircraft()->pMdl)
            return *(fd.GetAircraft()->pMdl);
        if (fd.pMdl)
            return *reinterpret_cast<const LTAircraft::FlightModel*>(fd.pMdl);
    }
    
    // 1. find an aircraft ICAO type based on input
    bool bDefaulted;
    const std::string& acTypeIcao = DetermineIcaoType(fd, bDefaulted);
    if (pIcaoType)
        *pIcaoType = bDefaulted ? nullptr : &acTypeIcao;
    
    // 2. find aircraft type specification in the Doc8643
    const Doc8643& acType = Doc8643::get(acTypeIcao);
    const std::string acSpec (acType);      // the string to match
    
    // 3. walk through the Flight Model map list and try each regEx pattern
    for (const auto& mapIt: listFMRegex) {
        std::smatch m;
        std::regex_search(acSpec, m, mapIt.first);
        if (m.size() > 0) {                 // matches?
            fd.pMdl = &(mapIt.second);      // save and...
            return mapIt.second;            // return that flight model
        }
    }
    
    // no match: return default
    LOG_MSG(logWARN, ERR_FM_NOT_FOUND,
            acTypeIcao.c_str(), acSpec.c_str());
    return MDL_DEFAULT;
}

// return a ptr to a flight model based on its model or [section] name
// returns nullptr if not found
const LTAircraft::FlightModel* LTAircraft::FlightModel::GetFlightModel
        (const std::string& modelName)
{
    // search through list of flight models, match by modelName
    auto fmIt = std::find_if(listFlightModels.cbegin(),
                             listFlightModels.cend(),
                             [&modelName](const LTAircraft::FlightModel& fm)
                             { return fm.modelName == modelName; });
    return fmIt == listFlightModels.cend() ? nullptr : &*fmIt;
}

// Tests if the given call sign matches typical call signs of ground vehicles
bool LTAircraft::FlightModel::MatchesCar (const std::string& _callSign)
{
    // Walk the car regEx list and try each pattern
    for (const std::regex& re: listCarRegex) {
        std::smatch m;
        std::regex_search(_callSign, m, re);
        if (m.size() > 0)                   // matches?
            return true;
    }
    // no match found
    return false;
}

//
//MARK: LTAircraft::FlightPhase
//

std::string LTAircraft::FlightPhase2String (flightPhaseE phase)
{
    switch (phase) {
        case FPH_UNKNOWN:           return "Unknown";
        case FPH_PARKED:            return "Parked";
        case FPH_TAXI:              return "Taxi";
        case FPH_TAKE_OFF:          return "Take Off";
        case FPH_TO_ROLL:           return "Take Off Roll";
        case FPH_ROTATE:            return "Rotate";
        case FPH_LIFT_OFF:          return "Lift Off";
        case FPH_INITIAL_CLIMB:     return "Initial Climb";
        case FPH_CLIMB:             return "Climb";
        case FPH_CRUISE:            return "Cruise";
        case FPH_DESCEND:           return "Descend";
        case FPH_APPROACH:          return "Approach";
        case FPH_FINAL:             return "Final";
        case FPH_LANDING:           return "Landing";
        case FPH_FLARE:             return "Flare";
        case FPH_TOUCH_DOWN:        return "Touch Down";
        case FPH_ROLL_OUT:          return "Roll Out";
        case FPH_STOPPED_ON_RWY:    return "Stopped";
    }
    // must not get here...then we missed a value in the above switch
    LOG_ASSERT(phase!=FPH_UNKNOWN);
    return "?";
}

//
//MARK: LTAircraft Init/Destroy
//

constexpr float ACI_NEAR_AIRPRT_PERIOD =180.0f; ///< How often update the nearest airport? [s]

// Constructor: create an aircraft from Flight Data
LTAircraft::LTAircraft(LTFlightData& inFd) :
// Base class -> this registers with XPMP API for actual display in XP!
// repeated calls to WaitForSafeCopyStat look inefficient, but if the lock is held by the calling function already then these are quick recursive calls
// Using registration as livery indicator, allows for different liveries per actual airframe
// Debug options to set fixed type/op/livery take precedence
XPMP2::Aircraft(str_first_non_empty({dataRefs.cslFixAcIcaoType, inFd.WaitForSafeCopyStat().acTypeIcao}).c_str(),
                str_first_non_empty({dataRefs.cslFixOpIcao,     inFd.WaitForSafeCopyStat().airlineCode()}).c_str(),
                str_first_non_empty({dataRefs.cslFixLivery,     inFd.WaitForSafeCopyStat().reg}).c_str(),
                inFd.key().num < MAX_MODE_S_ID ? (XPMPPlaneID)inFd.key().num : 0),      // OGN Ids can be larger than MAX_MODE_S_ID, in that case let XPMP2 assign a synthetic id
// class members
fd(inFd),
pMdl(&FlightModel::FindFlightModel(inFd, true)),      // find matching flight model
pDoc8643(&Doc8643::get(acIcaoType)),
tsLastCalcRequested(0),
phase(FPH_UNKNOWN),
rotateTs(NAN),
vsi(0.0),
bArtificalPos(false),
heading(pMdl->TAXI_TURN_TIME, 360, 0, true),
corrAngle(pMdl->FLIGHT_TURN_TIME / 2.0, 90, -90, false),
gear(pMdl->GEAR_DURATION),
flaps(pMdl->FLAPS_DURATION),
pitch((pMdl->PITCH_MAX-pMdl->PITCH_MIN)/pMdl->PITCH_RATE, pMdl->PITCH_MAX, pMdl->PITCH_MIN),
reversers(MDL_REVERSERS_TIME),
spoilers(MDL_SPOILERS_TIME),
tireRpm(MDL_TIRE_SLOW_TIME, MDL_TIRE_MAX_RPM),
gearDeflection(MDL_GEAR_DEFL_TIME, pMdl->GEAR_DEFLECTION),
probeNextTs(0), terrainAlt_m(0.0)
{
    // for some calcs we need correct timestamps _before_ first draw already
    // so make sure the currCycle struct is up-to-date
    int cycle = XPLMGetCycleNumber();
    if ( cycle != currCycle.num )            // new cycle!
        NextCycle(cycle);

    try {
        // access guarded by a mutex (will be a recursive call: creator/caller should be holding the lock already)
        std::lock_guard<std::recursive_mutex> lock (fd.dataAccessMutex);
        
        // get copy of dynamice and static data for constructor purposes
        LTFlightData::FDDynamicData dynCopy (fd.WaitForSafeCopyDyn());
        LTFlightData::FDStaticData statCopy (fd.WaitForSafeCopyStat());
        bStaticObject = statCopy.isStaticObject();
        
        // init
        aiPrio = 0;
        bClampToGround = false;
        
        // positional data / radar: just copy from fd for a start
        acRadar = dynCopy.radar;

        // standard label
        LabelUpdate();

        // standard internal label (e.g. for logging) is transpIcao + ac type + another id if available
        CalcLabelInternal(statCopy);
        
        // init moving params where necessary.
        // The pitch is initialised to the static ground attitude
        // `GND_PITCH_DEG` whenever the aircraft starts its life on the
        // ground (parked / taxiing) so the first rendered frame already
        // looks correct — otherwise the MovingParam would briefly target
        // 0° and produce a visible nose-bob before the ground-attitude
        // override (in `CalcAcPos`) kicks in. Aircraft created in flight
        // continue to start at neutral 0°.
        pitch.SetVal(IsOnGrnd() ? GND_PITCH_DEG : 0);
        corrAngle.SetVal(0);
        
        // calculate our first position, must also succeed
        if (!CalcPPos())
            LOG_MSG(logERR,ERR_AC_CALC_PPOS,fd.key().c_str());
        
        // if we start on the ground then have the gear out already
        if (IsOnGrnd()) {
            gear.SetVal(gear.defMax);
            gearDeflection.SetVal((gearDeflection.defMin+gearDeflection.defMax)/2);
        }
        
        // tell the world we've added something
        dataRefs.IncNumAc();
        LOG_MSG(logDEBUG,INFO_AC_ADDED,
                labelInternal.c_str(),
                statCopy.opIcao.c_str(),
                GetModelName().c_str(),
                pMdl->modelName.c_str(),
                vecView.angle, vecView.dist/M_per_NM,
                dynCopy.pChannel ? dynCopy.pChannel->ChName() : "?");
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// Destructor
LTAircraft::~LTAircraft()
{
    // make sure external view doesn't use this aircraft any longer
    if (IsInCameraView())
        ToggleCameraView();
    
    // Decrease number of visible aircraft and log a message about that fact
    dataRefs.DecNumAc();
    LOG_MSG(logDEBUG,INFO_AC_REMOVED,labelInternal.c_str());
}

void LTAircraft::CalcLabelInternal (const LTFlightData::FDStaticData& statDat)
{
    std::string s (statDat.acId(""));
    labelInternal = fd.key().GetKeyTypeText();
    labelInternal += ' ';
    labelInternal += key();
    labelInternal += " (";
    labelInternal += acIcaoType;
    if (!s.empty()) {
        labelInternal += ' ';
        labelInternal += s;
    }
    labelInternal += ')';
}


/// LTAircraft stringify for debugging output purposes
LTAircraft::operator std::string() const
{
    char buf[500];
    snprintf(buf,sizeof(buf),"a/c %s\n%s  <- turn\n%s Y: %.1fft %.0fkn %.0fft/m Phase: %02d %s\nposList:\n",
             labelInternal.c_str(),
             turn.dbgTxt().c_str(),
             ppos.dbgTxt().c_str(), GetTerrainAlt_ft(),
             GetSpeed_kt(),
             GetVSI_ft(),
             phase, FlightPhase2String(phase).c_str());
    
    // We'll add out position list soon. To be able to also add a vector
    // to the first pos in the flight data we look for that
    positionTy firstFdPos;
    const positionTy* pFirstFdPos = nullptr;
    const dequePositionTy& fdPosDeque = fd.GetPosDeque();
    if (!fdPosDeque.empty()) {
        firstFdPos = fdPosDeque.front();    // copy for a bit better thread safety
        pFirstFdPos = &firstFdPos;
    }
    
    return std::string(buf) + positionDeque2String(posList, pFirstFdPos);
}

// Update the aircraft's label
// We do all logic and string handling here so we can just copy chars later in the callback
void LTAircraft::LabelUpdate()
{
    label = fd.ComposeLabel();
    
    // color depends on setting and maybe model
    if (dataRefs.IsLabelColorDynamic())
        memmove(colLabel, pMdl->LABEL_COLOR, sizeof(colLabel));
    else
        dataRefs.GetLabelColor(colLabel);
}

// Return a value for dataRef .../tcas/target/flight_id
std::string LTAircraft::GetFlightId() const
{
    LTFlightData::FDStaticData stat = fd.WaitForSafeCopyStat();
    return stat.acId(key());
}

//
// MARK: LTAircraft Calculate present position
//

// position heading to (usually posList.back(), but ppos if ppos > posList.back())
const positionTy& LTAircraft::GetToPos(double* pHeading) const
{
    // posList contains more than just the _current_ to-position, but even a future one (temporary state)
    if ( posList.size() >= 3 ) {
        if (pHeading)
            *pHeading = posList[posList.size()-2].angle(posList.back());
        return posList.back();
    }
    
    // posList contains the _current_ to-position...and maybe we even passed it already
    // but heading is same in both cases
    if (pHeading)
        *pHeading = GetTrack();
    if ( posList.size() >= 2 && ppos < posList.back() )
        return posList.back();
    else
        return ppos;
}

// are we in desperate need of new positions?
bool LTAircraft::OutOfPositions() const
{
    // we are running artificially already (roll-out phase to full stop), or
    // don't have two positions, or
    // the last positions timestamp has passed
    return
    bArtificalPos ||
    (posList.size() < 2) ||
    (currCycle.simTime >= posList.back().ts());
}


/// Finds a near airport and outputs a human-readable position like "3.1nm N of EDDL"
std::string LTAircraft::RelativePositionText ()
{
    // find/update the nearest airport when needed or
    if (std::isnan(nearestAirportPos.lat()) ||
        CheckEverySoOften(lastNearestAirportCheck, ACI_NEAR_AIRPRT_PERIOD))
    {
        lastNearestAirportCheck = dataRefs.GetMiscNetwTime();
    
        // Find the nearest airport
        nearestAirportPos.lat() = NAN;
        nearestAirportPos.lon() = NAN;
        nearestAirport = GetNearestAirportId(GetPPos(), &nearestAirportPos);
        if (std::isnan(nearestAirportPos.lat())) {   // no airport found?
            return std::string(GetPPos());
        }
    }
    
    // determine bearing from airport to position
    vectorTy vecRel = nearestAirportPos.between(GetPPos());
    
    // put together a nice string
    char out[100];
    snprintf(out, sizeof(out), "%.1fnm %s of %s",
             vecRel.dist / M_per_NM, HeadingText(vecRel.angle).c_str(), nearestAirport.c_str());
    return std::string(out);
}


// GetFlightPhaseString() plus rwy id in case of approach
std::string LTAircraft::GetFlightPhaseRwyString() const
{
    if (phase < FPH_APPROACH || fd.GetRwyId().empty())
        return GetFlightPhaseString();
    else
        return GetFlightPhaseString() + ' ' + fd.GetRwyId();
}


// is the aircraft on a rwy (on ground and at least on pos on rwy)
bool LTAircraft::IsOnRwy() const
{
    return IsOnGrnd() &&
    posList.size() >= 2 &&
    (posList.front().f.specialPos == SPOS_RWY ||
     posList[1].f.specialPos == SPOS_RWY);
}


// Lift produced for wake system, typically mass * 9.81, but blends in during rotate and blends out while landing
float LTAircraft::GetLift() const
{
    // Are we in any phase in which we phase lift in/out?
    if (pitch.inMotion() &&
        (phase == FPH_TAKE_OFF || phase == FPH_ROTATE ||        // take-off
         phase == FPH_TOUCH_DOWN || phase == FPH_ROLL_OUT))     // landing
    {
        // Transitioning (either rolling out or rotating)
        return GetMass() * XPMP2::G_EARTH * (float)pitch.percDone();
    }
    
    // No transition: Then it's all (airborne) or nothing (ground)
    return IsOnGrnd() ? 0.0f : GetMass() * XPMP2::G_EARTH;
}

// The basic idea is: We are given a 'from'-position and a 'to'-position,
// both including a timestamp. The 'from'-timestamp is in the past,
// the 'to'-timestamp is in the future (as compared to simulated LT time,
// which is lagging behind real time by a buffer [defaults to 60 seconds]).
// The present position is basically inbetween 'from' and 'to',
// moving linear with time as to reach 'to' when reaching the 'to'-timestamp
// in simulated LT time.
// All aspects of flight position and attitude (pitch, roll, heading)
// are deducted from that movement, also see CalcFlightModel
bool LTAircraft::CalcPPos()
{
    // new positions to work with?
    bool bPosSwitch = phase == FPH_UNKNOWN;
    
    // *** some checks on our positional information ***

    // are there sufficient position information for a calculation?
    // we need at least two: 'from' and 'to'
    while ( posList.size() < 2 ) {
        // try fetching new data, if succeeded repeated evaluation
        switch ( fd.TryFetchNewPos(posList, rotateTs) ){
            case LTFlightData::TRY_NO_DATA:
                // no new data available...tell fd (maybe again) that we urgendtly need some!
                fd.TriggerCalcNewPos(tsLastCalcRequested = currCycle.simTime);
                LOG_MSG(logERR,ERR_AC_NO_POS,fd.key().c_str());
                [[fallthrough]];
            case LTFlightData::TRY_TECH_ERROR:
            case LTFlightData::TRY_NO_LOCK:
                // clear positional information
                ppos = positionTy();            // clear ppos
                return false;                   // can't calc pos, can't fly
                
            case LTFlightData::TRY_SUCCESS:
                // received new positions!
                bArtificalPos = false;
                // should usually mean to have 2 positions now,
                // but while-loop will make sure
        }
    }

    // check if we are running out of positions soon. If so we ask for more
    LOG_ASSERT_FD(fd, posList.size() >= 2);

    // If we aren't just yet creating the object (in that case fd.pAc is not yet set), then:
    // 1s before reaching last know position we trigger pos calculation (max every 1,0s)
    if (fd.hasAc()) {
        const positionTy& lastPos = posList.back();
        if ((lastPos.ts() <= currCycle.simTime + 2 * TIME_REQU_POS) &&
            (tsLastCalcRequested + 2 * TIME_REQU_POS <= currCycle.simTime))
        {
            fd.TriggerCalcNewPos(std::max(currCycle.simTime, lastPos.ts()));
            tsLastCalcRequested = currCycle.simTime;
        }

        // 0,5s before reaching last known position we try adding new positions
        if (lastPos.ts() <= currCycle.simTime + TIME_REQU_POS) {
            if (fd.TryFetchNewPos(posList, rotateTs) == LTFlightData::TRY_SUCCESS) {
                // we got new position(s)!
                bArtificalPos = false;
            }
        }
    }
    
    // Finally: Time to switch to next position?
    // (Must have reach/passed posDeque[1] and there must be a third position,
    //  which can now serve as 'to')
    while ( posList[1].ts() <= currCycle.simTime && posList.size() >= 3 ) {
        // Preserve the slot we are about to discard. The Catmull-Rom
        // spline that renders ground position + heading needs the
        // *previous* `from` as its P0 control point — the slot that
        // gave the curve its incoming tangent at P1. Without this
        // save the spline at the start of each new leg would treat
        // the leg as the head of the deque and lose its smoothness
        // at the join.
        posPrev = posList.front();

        // By just removing the first element (current 'from') from the deqeue
        // we make posDeque[2] the next 'to'
        posList.pop_front();

        // Snapshot the spline's exit-tangent control point (P3) for the
        // new leg. After the pop, the new leg is from = posDeque[0],
        // to = posDeque[1], and the slot one beyond `to` is posDeque[2]
        // (if it exists). We freeze that value into `posNext` and use
        // it for the entire leg — see the LTAircraft::posNext docstring
        // for why we must NOT read posDeque[2] live each frame. If the
        // deque does not yet extend that far we leave posNext invalid
        // (lat()=NaN) and the spline evaluator falls back to
        // duplicating P2 for a stable, slightly-tighter exit.
        if (posList.size() >= 3)
            posNext = posList[2];
        else
            posNext = positionTy();         // lat()/lon() default to NaN

        // Invalidate the arc-length LUT. The control points for the new
        // segment are not yet bound to a numeric value here (we want to
        // build the LUT only if the spline branch is actually entered —
        // chord-skip and air legs would not use it). The spline branch
        // checks `splineLut.valid` and rebuilds on the first render
        // frame of any segment that needs it.
        splineLut.valid = false;
        // Now: If running point-to-point, ie. _not_ cutting corners with
        // Bezier curves, then to absolutely ensure we continue seamlessly from current
        // ppos we set posDeque[0] ('from') to ppos. Should be close anyway in normal
        // situations. (It's not if the simulation was halted while feeding live
        // data, then posList got completely outdated and ppos might jump beyond the entire list.)
        if ( ppos < posList[1]) {
            // Save some flags needed for later calculations
            ppos.f.specialPos = posList.front().f.specialPos;
            ppos.f.bCutCorner = posList.front().f.bCutCorner;
            ppos.edgeIdx      = posList.front().edgeIdx;
            // Then overwrite posDeque[0] if not currently turning using a Bezier
            if (!turn.isTsInbetween(currCycle.simTime))
                posList.front() = ppos;
        }
        // flag: switched positions
        bPosSwitch = true;
    }

    // *** fixed to/from positions ***
    LOG_ASSERT_FD(fd, posList.size() >= 2);
    
    // we are now certain to have at least 2 position and we are flying
    // from the first to the second
    positionTy& from  = posList[0];
    positionTy& to    = posList[1];
    const double duration = to.ts() - from.ts();
    const double prevHead = phase == FPH_UNKNOWN ? from.heading()    : ppos.heading();  // previous heading (needed for roll calculation)
#ifdef DEBUG
    std::string debFrom ( from.dbgTxt() );
    std::string debTo   ( to.dbgTxt() );
    std::string debVec  ( from.between(to) );
#endif
    LOG_ASSERT_FD(fd,duration > 0);

    // *** position switch ***
    
    // some things only change when we work with new positions compared to last frame
    if ( bPosSwitch ) {
        // *** vector we will be flying now from 'from' to 'to':
        from.normalize();
        to.normalize();
        vec = from.between(to);
        LOG_ASSERT_FD(fd,!std::isnan(vec.speed) && !std::isnan(vec.vsi));
        
        // vertical speed between the two points [ft/m] is constant:
        vsi = vec.vsi_ft();
        
        // first time inits
        if (phase == FPH_UNKNOWN) {
            // check if starting on the ground
            bOnGrnd = from.IsOnGnd();
            
            // avg of the current vector
            speed.SetSpeed(vec.speed);
            
            // point to some reasonable heading
            heading.SetVal(ppos.heading() = from.heading());
        }
        
        // *** ground status starts with that one of 'from'
        ppos.f.onGrnd = from.f.onGrnd;
        
        // *** Pitch ***
        
        // Fairly simplistic...in the range -2 to +18 depending linearly on vsi only
        double toPitch =
        (from.IsOnGnd() && to.IsOnGnd()) ? 0 :
        vsi < pMdl->PITCH_MIN_VSI ? pMdl->PITCH_MIN :
        vsi > pMdl->PITCH_MAX_VSI ? pMdl->PITCH_MAX :
        (pMdl->PITCH_MIN +
         (vsi-pMdl->PITCH_MIN_VSI)/(pMdl->PITCH_MAX_VSI-pMdl->PITCH_MIN_VSI)*
         (pMdl->PITCH_MAX-pMdl->PITCH_MIN));
        
        // another approach for climbing phases:
        // a/c usually flies more or less with nose pointing upward toward climb,
        // nearly no drag, so try calculating the flight path angle and use
        // it also as pitch
        if ( vsi > pMdl->VSI_STABLE ) {
            toPitch = std::clamp<double>(vsi2deg(vec.speed, vec.vsi),
                                         pMdl->PITCH_MIN, pMdl->PITCH_MAX);
        }
        
        // add some degrees in case flaps will be set
        if (!bOnGrnd && vec.speed_kn() < std::max(pMdl->FLAPS_DOWN_SPEED,pMdl->FLAPS_UP_SPEED)) {
            toPitch += pMdl->PITCH_FLAP_ADD;
            if (toPitch > pMdl->PITCH_MAX)
                toPitch = pMdl->PITCH_MAX;
        }
        
        // set destination pitch and move there in a controlled way
        to.pitch() = toPitch;
        if (phase != FPH_ROTATE)            // while rotating don't interfere
            pitch.moveQuickestToBy(NAN, toPitch, NAN, to.ts(), true);
        
        // *** speed/acceleration/decelaration
        
        // Only skip acceleration control in case of short final, i.e. if currently off ground
        // but next pos is on the ground, because in that case the
        // next vector is the vector for roll-out on the ground
        // with significantly reduced speed due to breaking, that speed
        // is undesirable at touch-down; for short final we assume constant speed
        bNeedSpeed = from.IsOnGnd() || !to.IsOnGnd();
        if (!bNeedSpeed)
            speed.SetSpeed(vec.speed);

        // Not already controlled by a cut-corner Bezier curve
        if (!turn.isTsInbetween(currCycle.simTime)) {
            // Clear an outdated turn
            turn.Clear();
        
            // *** Heading ***
            
            // Try a Bezier curve first, if that doesn't work...
            //
            // On the ground we use the lower `GND_BEZIER_MIN_HEAD_DIFF`
            // threshold so that 1–2° taxi turns also get curve-tangent
            // heading and are visually rendered as a smooth arc rather than
            // as a heading walked via the linear MovingParam fallback. In
            // the air we keep the original 2.5° threshold so en-route course
            // corrections don't constantly enter/exit Bezier mode.
            const double minHeadDiff = IsOnGrnd() ? GND_BEZIER_MIN_HEAD_DIFF
                                                  : BEZIER_MIN_HEAD_DIFF;
            // At high ground speed (landing rollout, takeoff roll, fast taxi)
            // we deliberately skip Bezier and use straight-line interpolation
            // instead. The Bezier's end-tangent comes from `to.heading()` which
            // is the next slot's reported heading — when that next slot is on
            // a turn-off taxiway and the current slot is on the runway, the
            // Bezier arcs the path across the runway corner and the rendered
            // aircraft visually slides off the runway with its nose pointing
            // away from its direction of motion. Linear interpolation makes
            // the renderer walk heading toward `vec.angle` (the motion vector
            // — see the moveQuickestToBy call below) which is what an aircraft
            // physically does on the ground at speed: nose along the track.
            // See `GND_TRACK_HEADING_MIN_KT` in Constants.h for the rationale.
            //
            // We use the leg's AVERAGE speed (`vec.speed_kn()` = dist/dt) and
            // NOT the current rendered speed. The rendered speed at leg-setup
            // is the speed the aircraft is *coming into* the leg — so for a
            // taxi-to-runway-entry leg where the aircraft taxis in slowly and
            // exits at runway-roll speed (e.g., gs 5 kn → 12 kn over 47 m in
            // 7.84 s, leg-average ~12 kn), the rendered start speed is 5 kn
            // and would fail this threshold even though the leg is the very
            // transition we want to handle straight-line. Using leg-average
            // catches all legs whose endpoint speed crosses the threshold,
            // which is what aligns the rendered nose with the runway from
            // the moment the aircraft starts accelerating onto it.
            const bool bGndFast = IsOnGrnd() &&
                                  !std::isnan(vec.speed) &&
                                  vec.speed_kn() >= GND_TRACK_HEADING_MIN_KT;
            if (to.f.bCutCorner ||                                      // next position is to use a cut-corner curve?
                vec.dist <= SIMILAR_POS_DIST ||                         // no reasonable leg distance and turn amount?
                std::abs(HeadingDiff(ppos.heading(), to.heading())) < minHeadDiff ||
                bGndFast ||                                             // high-speed ground motion: never Bezier
                !turn.Define(ppos, to))                                 // or defining the Bezier failed for some other reason?
            {
                // ...start the turn from the initial heading to the vector heading
                heading.defDuration = IsOnGrnd() ? pMdl->TAXI_TURN_TIME : pMdl->FLIGHT_TURN_TIME;
                heading.moveQuickestToBy(ppos.heading(),
                                         vec.dist > SIMILAR_POS_DIST ?  // if vector long enough:
                                         vec.angle :                    // turn to vector heading
                                         HeadingAvg(from.heading(),to.heading()),   // otherwise only turn to avg between from- and target-heading
                                         NAN, from.ts()+duration/2,     // by half the vector flight time
                                         true);                         // start immediately
            }
        }
        
        // *** Correction Angle for crosswind ***
        CalcCorrAngle();
        
        // output debug info on request
        if (dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_AC_SWITCH_POS,std::string(*this).c_str());
        }
    } // if ( bPosSwitch )
    
    // Further computations make only sense if 'to' is still in the future
    // (there seem to be case when this is not the case, and if only because the user pauses or changes time)
    if ((bNeedSpeed || bNeedCCBezier) && to.ts() < currCycle.simTime) {
        if (bNeedSpeed) speed.SetSpeed(vec.speed);
        bNeedSpeed = bNeedCCBezier = false;
    }
    
    // Need next position for speed or Bezier determination?
    // (We don't need a next position if the next is "STOPPED", because then we know the target speed is zero.)
    positionTy nextPos;
    vectorTy nextVec;
    if ((bNeedSpeed || bNeedCCBezier) && to.f.flightPhase != FPH_STOPPED_ON_RWY)
    {
        // Do we happen to have a next vector already in posList?
        if (posList.size() >= 3) {
            nextPos = posList[2];
            nextVec = to.between(nextPos);
        }
        else {
            // need to check with LTFlightData's posDeque:
            switch ( fd.TryGetNextPos(to.ts()+1.0, nextPos) ) {
                case LTFlightData::TRY_SUCCESS:
                    // got the next position!
                    // But it's from flight data's queue, so potentially doesn't yet have an altitude, we need one now
                    if (nextPos.IsOnGnd() && std::isnan(nextPos.alt_m()))
                        nextPos.alt_m() = fd.YProbe_at_m(nextPos);
                    // Compute vector to it
                    nextVec = to.between(nextPos);
                    break;
                case LTFlightData::TRY_NO_LOCK:
                    // try again next frame (bNeedSpeed || bNeedBezier stays true)
                    break;
                case LTFlightData::TRY_NO_DATA:
                case LTFlightData::TRY_TECH_ERROR:
                    // no data or errors...well...then we just fly straight
                    if (bNeedSpeed) speed.SetSpeed(vec.speed);
                    bNeedSpeed = bNeedCCBezier = false;
                    break;
            }
        }
    }
    
    // *** acceleration / deceleration ***
    if (bNeedSpeed && (nextVec.isValid() || to.f.flightPhase == FPH_STOPPED_ON_RWY))
    {
        // Target speed: Weighted average of current and next vector
        const double toSpeed =
            to.f.flightPhase == FPH_STOPPED_ON_RWY ? 0.0 :              // if we are to STOP, then target speed is zero
            (vec.speed * nextVec.dist + nextVec.speed * vec.dist) /     // otherwise we consider this and the next leg
            (vec.dist + nextVec.dist);
        
        // initiate speed control (if speed valid, could be NAN if both distances ae zero)
        if (!std::isnan(toSpeed)) {
            speed.StartSpeedControl(speed.m_s(),
                                    toSpeed,
                                    vec.dist,
                                    from.ts(), to.ts(),
                                    this);
        }
        // don't need to calc speed again
        bNeedSpeed = false;
    }
    
    // Update correction angle
    corrAngle.get();
    
    // *** Cut Corner Bezier Curve ***
    // We define them only if both legs are long enough.
    // (Very short legs indicate a plane standing still waiting,
    // we don't want such a plane to turn at all.)
    if (bNeedCCBezier && nextVec.isValid())
    {
        bNeedCCBezier = false;

        // Legs long enough?
        if (vec.dist > SIMILAR_POS_DIST && nextVec.dist > SIMILAR_POS_DIST) {
            positionTy _end = to;
            _end.mergeCount = nextPos.mergeCount = 1;
            _end |= nextPos;                        // effectively calculates mid-point between to and nextPos, taking care of proper heading, too
            turn.Define(ppos,                       // start is right here and now
                        to,                         // mid-point is end of current leg
                        _end);                      // end-point is the mid between to and nextPos
        }
    }

    // *** The Factor ***
    
    // How far have we traveled (in time) between from and to?
    double f = NAN;
    if (speed.isChanging()) {
        // accelerating/decelerating: f follows a 2. degree polynomial
        f = speed.getRatio();
        speed.updateSpeed();
    } else {
        // standard case: we move steadily from 'from' to 'to', f is linear
        f = (currCycle.simTime - from.ts()) / duration;
    }
    
    // *** Artifical stop ***
    
    // limit on-the-ground activities, i.e. slow down to a stop if we don't know better
    // (this also applies to artificial roll-out phase)
    if (f > 1.0 &&
        (phase == FPH_TAXI || phase >= FPH_TOUCH_DOWN) &&
        speed.m_s() > 0.5 &&
        !bArtificalPos)
    {
        // init deceleration down to zero
        speed.StartAccel(speed.m_s(),
                         0,
                         pMdl->ROLL_OUT_DECEL);
        
        // the vector to the stopping point
        vectorTy vecStop(ppos.heading(),            // keep current heading
                         speed.getTargetDeltaDist());// distance needed to stop
        
        // add ppos and the stop point (ppos + above vector) to the list of positions
        // (they will be activated with the next frame only)
        posList.emplace_back(ppos);
        posList.emplace_back(ppos.destPos(vecStop));
        positionTy& stopPoint = posList.back();
        stopPoint.ts() = speed.getTargetTime();
        stopPoint.f.flightPhase = FPH_STOPPED_ON_RWY;
        bArtificalPos = true;                   // flag: we are working with an artifical position now
        turn.Clear();                           // (and certainly not with a Bezier curve)
        if (dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_INVENTED_STOP_POS,stopPoint.dbgTxt().c_str());
        }
    }
    
    // Try getting our current position from the Bezier curve
    const double _calcTs = from.ts() * (1-f) + to.ts() * f;
    if (turn.GetPos(ppos, _calcTs)) {
        // sync the changing heading between Bezier curve and MovingParam
        heading.SetVal(ppos.heading());
    }
    // No Bezier curve currently active:
    else if (from.IsOnGnd() && to.IsOnGnd()) {
        // ------------------------------------------------------------------
        // Ground rendering — centripetal Catmull-Rom spline.
        //
        // While both endpoints of the current leg are on the ground we
        // interpolate position and heading along a smooth curve fit through
        // four control points: P0 = the previous `from` (preserved in
        // `posPrev` when the position switch popped it from the deque),
        // P1 = current from, P2 = current to, and P3 = the slot AFTER `to`
        // if one is available in `posList`. P0 / P3 set the entry / exit
        // tangents so adjacent legs join with C¹ continuity.
        //
        // P2 (the look-ahead endpoint) is lightly pre-smoothed against its
        // neighbours before the curve is fit (see GND_SPLINE_SMOOTH_WEIGHT
        // and the control-point smoothing block below), which turns the
        // otherwise strictly-interpolating spline into an approximating
        // one — the rendered path no longer threads exactly through every
        // noisy feed sample. P1 is left raw so the curve still returns
        // `from` exactly at u=0, which the segment-switch continuity
        // mechanism depends on.
        //
        // Heading is the tangent direction at the spline parameter — by
        // construction the rendered nose points the way the rendered
        // position is moving, which is the property that eliminates the
        // "sideways through a turn" symptom that linear-chord
        // interpolation produces.
        //
        // Math is performed in a local meters frame centred on P1 (see
        // `CatmullRomEvalCentripetal`). We convert the returned local
        // (x, y) back to lat/lon using `Dist2Lat` / `Dist2Lon` on the
        // same origin. Altitude and pitch stay on the linear path:
        // altitude is irrelevant on the ground (clamped to terrainAlt
        // later by the bOnGrnd branch), and pitch is hard-set to
        // GND_PITCH_DEG by the same later block.
        //
        // The Bezier curve `turn` is *not* used while the spline is in
        // effect — turn.GetPos returned false above, otherwise we
        // wouldn't be in this branch. The spline supersedes Bezier for
        // ground rendering because Bezier's end-tangents come from
        // slot.heading() (subject to EHS lag and channel-side filter
        // ambiguity) while the spline's tangents come from positions
        // only, so the spline is robust to a stale or missing feed
        // heading at the segment endpoints.
        // ------------------------------------------------------------------

        // Compute the chord length from→to in the local meters frame
        // first. If it is below GND_SPLINE_MIN_CHORD_M we are looking
        // at a "no useful motion" leg — parked aircraft jitter or a
        // near-stationary creep dominated by feed noise — and the
        // spline tangent through near-coincident control points would
        // produce a noise-driven heading that overrides the (already
        // correctly frozen) slot heading. Skip the spline in that
        // regime: linear position interp + preserve from.heading().
        const double dyMtr = Lat2Dist(to.lat() - from.lat());
        const double dxMtr = Lon2Dist(to.lon() - from.lon(), from.lat());
        const double chordMtr = std::sqrt(dxMtr * dxMtr + dyMtr * dyMtr);

        // Leg-average ground speed (chord / duration). Used purely to
        // pick the interpolation method below — see GND_SPLINE_MAX_KT.
        // `duration` is guaranteed positive by the LOG_ASSERT_FD above.
        const double legSpeedKt = (chordMtr / duration) * KT_per_M_per_S;

        if (chordMtr < GND_SPLINE_MIN_CHORD_M) {
            // Stationary / sub-noise motion — pure linear interp and
            // pass the slot heading through. CalcHeading has frozen
            // from.heading() to lastGoodHeading_ in this regime, so
            // both `from.heading()` and `to.heading()` should agree
            // and equal the parked heading. We blend them defensively
            // via shortest-path so a stray 1° slot mismatch does not
            // unwrap into a 359° backward swing.
            ppos.lat()    = from.lat() + Dist2Lat(dyMtr * f);
            ppos.lon()    = from.lon() + Dist2Lon(dxMtr * f, from.lat());
            ppos.alt_m()  = from.alt_m() * (1 - f) + to.alt_m() * f;
            ppos.pitch()  = from.pitch() * (1 - f) + to.pitch() * f;
            const double h0 = from.heading();
            const double hd = HeadingDiff(h0, to.heading());
            ppos.heading() = HeadingNormalize(h0 + hd * f);
            heading.SetVal(ppos.heading());
        }
        else if (legSpeedKt > GND_SPLINE_MAX_KT) {
            // High-speed straight-line runway motion — takeoff roll or
            // landing rollout. The spline exists to handle TURNS, which
            // do not happen at this speed; here it would only add
            // fragility (outlier amplification, arc-length-LUT vs
            // acceleration-profile interaction, leg-to-leg curve-shape
            // changes on sparse irregular feed data). Plain linear
            // interpolation renders the straight centreline track
            // exactly and returns `from` precisely at f=0, keeping the
            // segment-switch continuity seamless. See GND_SPLINE_MAX_KT
            // in Constants.h for the full rationale.
            ppos.lat()    = from.lat() + Dist2Lat(dyMtr * f);
            ppos.lon()    = from.lon() + Dist2Lon(dxMtr * f, from.lat());
            ppos.alt_m()  = from.alt_m() * (1 - f) + to.alt_m() * f;
            ppos.pitch()  = from.pitch() * (1 - f) + to.pitch() * f;

            // Heading from the chord bearing — the direct line of
            // travel between the two feed positions, which on a runway
            // IS the aircraft heading. Far-apart high-speed samples
            // make this rock-solid: ~3 m of feed noise on a 200 m+
            // chord is well under 1° of heading error. atan2(east,
            // north) gives the compass-convention bearing, matching
            // the convention used everywhere else in this file.
            double hdg = std::atan2(dxMtr, dyMtr) * 180.0 / PI;
            if (hdg < 0.0)
                hdg += 360.0;
            ppos.heading() = hdg;
            heading.SetVal(hdg);
        }
        else {
            // Choose control points. P0 comes from posPrev (cached at
            // the previous segment-switch). P3 comes from posNext
            // (cached at THIS segment's switch — see posNext docstring
            // in LTAircraft.h for why we MUST NOT read posDeque[2] live
            // here). When either snapshot is unavailable (insufficient
            // deque depth at switch time) we duplicate the adjacent
            // endpoint, which produces a zero entry/exit tangent and
            // degenerates the spline to a quadratic-like segment at
            // the boundary — safe, no overshoot.
            const bool haveP0 = !std::isnan(posPrev.lat());
            const bool haveP3 = !std::isnan(posNext.lat());
            const positionTy& P0 = (haveP0 ? posPrev : from);
            const positionTy& P3 = (haveP3 ? posNext : to);

            // Control-point smoothing — LOOK-AHEAD ENDPOINT (P2) ONLY.
            //
            // A centripetal Catmull-Rom spline interpolates: the curve
            // passes exactly through P1 and P2, so a noisy feed sample at
            // either endpoint becomes a noisy rendered position. We turn
            // it into an approximating spline by pre-smoothing P2 with a
            // 3-tap binomial kernel against its neighbours:
            //   P2' = w·from + (1−2w)·to + w·P3   (w = GND_SPLINE_SMOOTH_WEIGHT)
            //
            // P1 is deliberately NOT smoothed. The position-switch loop
            // above does `posList.front() = ppos` (line ~1631) to make a
            // new leg continue seamlessly from wherever the renderer
            // currently is — a mechanism that only works if the renderer
            // returns posDeque[0] (== `from`) EXACTLY at the start of the
            // leg. The unmodified Catmull-Rom evaluator does exactly that
            // (it interpolates P1 at u=0). If we smoothed P1 the evaluator
            // would instead return P1' ≠ from at u=0, so every segment
            // switch the rendered position would snap from `ppos` to P1' —
            // the visible ~6 s forward/backward jump.
            //
            // Smoothing only P2 still removes the jitter completely:
            // every feed sample is reached as the *smoothed* P2' endpoint
            // of its leg, and is then carried into the next leg as `from`
            // via the ppos overwrite. So the rendered path threads the
            // smoothed points {P2'_k} — the raw noisy samples are never
            // visited — while u=0 still yields `from` exactly, keeping the
            // segment joins seamless.
            //
            // P2 is only smoothed when posNext is a *real* slot; when it
            // fell back to a duplicated `to` the kernel would just bias
            // the endpoint toward the segment interior, so we keep `to`
            // raw in that case. See GND_SPLINE_SMOOTH_WEIGHT in
            // Constants.h for the corner-cutting trade-off.
            const double w = GND_SPLINE_SMOOTH_WEIGHT;
            positionTy P2s = to;                // copy ts/flags/alt/heading
            if (haveP3 && w > 0.0) {
                P2s.lat() = w * from.lat() + (1.0 - 2.0 * w) * to.lat() + w * P3.lat();
                P2s.lon() = w * from.lon() + (1.0 - 2.0 * w) * to.lon() + w * P3.lon();
            }

            // Arc-length reparameterisation. The spline's native u is
            // centripetal-knot space, NOT arc length, so feeding `f`
            // directly to the evaluator would make the rendered position
            // accelerate and decelerate within the segment (the user-
            // observed "slow down / speed back up" pulsation). Instead
            // we (re)build the arc-length LUT on the first frame of the
            // segment and look up the u that corresponds to having
            // traversed `f * totalArc` along the curve. Result: the
            // rendered position advances at constant arc-length-per-
            // time across the leg, with the visible speed equal to
            // totalArc / duration. The LUT is built from the SAME
            // control points (raw `from`, smoothed P2s) used for eval.
            if (!splineLut.valid)
                splineLut.Build(P0, from, P2s, P3);
            const double uArc = splineLut.UFromArcFraction(f);

            const CatmullRomResult cr =
                CatmullRomEvalCentripetal(P0, from, P2s, P3, uArc);

            // Convert spline result (local meters from P1) back to
            // geographic coordinates. P1 is the raw `from` (NOT smoothed,
            // see above), so the local-frame origin is from.lat/lon.
            ppos.lat() = from.lat() + Dist2Lat(cr.yMtr);
            ppos.lon() = from.lon() + Dist2Lon(cr.xMtr, from.lat());

            // Altitude and pitch on the linear path — these are not
            // part of the horizontal-plane spline. On the ground
            // altitude will be clamped to terrainAlt_m below anyway;
            // pitch is forced to GND_PITCH_DEG by the same block.
            ppos.alt_m() = from.alt_m() * (1 - f) + to.alt_m() * f;
            ppos.pitch() = from.pitch() * (1 - f) + to.pitch() * f;

            // Heading.
            //
            // Normally the spline tangent is the heading: the rendered
            // nose points along the rendered direction of motion, which
            // is what eliminates the "sideways through a turn" symptom.
            //
            // EXCEPTION: if EITHER end of the leg carries `bHeadFixed`,
            // an upstream stage has deliberately set a heading that must
            // NOT be overridden — currently that means a pushback leg,
            // where CalcHeading set the slot heading to the held nose
            // direction so the nose stays pointed away from the
            // (backward) direction of travel. The spline tangent here
            // points along that backward motion, so using it would
            // render the aircraft tail-first the wrong way. Instead we
            // interpolate the slot headings across the leg (shortest-
            // path), preserving the intended nose direction while still
            // drawing the smooth spline *position*.
            //
            // Why both ends, not just `from`: at the PB_NONE→PB_ACTIVE
            // transition the previous leg's `to` (now `from` here) came
            // from the parked era and has bHeadFixed=false. Pinning
            // bHeadFixed retroactively onto the predecessor slot is not
            // always possible — when posDeque has been drained during a
            // long stationary period, CalcHeading uses pAc->GetToPos()
            // as a virtual predecessor and that slot is not writable
            // from CalcHeading. Honouring `to.f.bHeadFixed` here covers
            // that case from the destination side: as long as the slot
            // we are transitioning *into* has its heading fixed (PB
            // override), interpolate instead of tangent.
            if (from.f.bHeadFixed || to.f.bHeadFixed) {
                const double h0 = from.heading();
                const double hd = HeadingDiff(h0, to.heading());
                ppos.heading() = HeadingNormalize(h0 + hd * f);
                heading.SetVal(ppos.heading());
            } else {
                // Sync the MovingParam so any downstream code that reads
                // `heading.get()` sees the spline-derived value as the
                // current state.
                ppos.heading() = cr.headingDeg;
                heading.SetVal(cr.headingDeg);
            }
        }
    }
    else {
        // Air or air↔ground transition.
        //
        // lat/lon/pitch use the same linear interpolation as before —
        // the user-visible feature for those is positional, not slope
        // smoothness, and the existing ground-rendering Catmull-Rom
        // already handles the curve quality where it matters (taxi,
        // rollout, slow turns).
        //
        // Altitude, however, goes through `LookupAltAtTs(simTime)` so
        // the rendered climb/descend profile is a C¹-continuous
        // Hermite/Catmull-Rom spline across all of `posList`. The
        // previous `from.alt_m() * (1-f) + to.alt_m() * f` was only
        // C⁰ across leg boundaries (slope was a step at every slot
        // transition), and the visible symptom was a sequence of
        // "kinks" in the rendered VSI whenever the active leg changed
        // — most clearly during the 10 s liftoff blend, where the
        // aircraft might cross several leg boundaries.
        //
        // Using the spline always (not only inside the blend) keeps
        // the blend's seam at t = T seamless: the blend formula
        // ends with ppos.alt_m() = LookupAltAtTs(simTime), and the
        // post-blend rendering uses the very same value.
        ppos.lat()   = from.lat()   * (1 - f) + to.lat() * f;
        ppos.lon()   = from.lon()   * (1 - f) + to.lon() * f;
        ppos.alt_m() = LookupAltAtTs(currCycle.simTime);
        ppos.pitch() = from.pitch() * (1 - f) + to.pitch() * f;
        // we handle roll later separately

        // Get heading from moving param
        ppos.heading() = heading.get();
    }

    // ----------------------------------------------------------------------
    // Per-frame heading rate limit (ground only).
    //
    // Even after `LTFlightData::CalcHeading` filtered out stationary jitter
    // and applied a hysteresis dead-band on the deque side, the *target*
    // heading that arrives here can still jump abruptly when, e.g., a new
    // position slot becomes the active `to` and changes the heading
    // MovingParam's destination. Without rate-limiting, that jump would be
    // rendered as a single-frame snap-rotation — visually wrong for an
    // aircraft on the ground. We therefore clamp the per-frame change to
    // `GND_HEADING_MAX_RATE_DPS * dt`. Anything larger walks toward the
    // target at the maximum allowed rate; the rendered nose then never
    // moves faster than `GND_HEADING_MAX_RATE_DPS` (see `Constants.h`).
    //
    // We use `HeadingDiff` so that the clamp picks the signed shortest
    // path across the 360°/0° wrap. The MovingParam is re-synced to the
    // clamped value so it does not race ahead in subsequent frames.
    //
    // Airborne aircraft skip this clamp: in the air, the heading
    // MovingParam is already smoothed via `defDuration` (TAXI_TURN_TIME
    // vs FLIGHT_TURN_TIME, see the half-way preparations below) and an
    // additional clamp here would make en-route course changes lag.
    // ----------------------------------------------------------------------
    if (IsOnGrnd() &&
        !std::isnan(prevHead) &&
        !std::isnan(ppos.heading()))
    {
        const double maxStep_deg = GND_HEADING_MAX_RATE_DPS * currCycle.diffTime;
        const double delta_deg   = HeadingDiff(prevHead, ppos.heading());
        if (std::abs(delta_deg) > maxStep_deg) {
            ppos.heading() = HeadingNormalize(
                prevHead + std::copysign(maxStep_deg, delta_deg));
            heading.SetVal(ppos.heading());
        }
    }

    // calculate timestamp can be a bit off, especially when acceleration is in progress,
    // overwrite with current value as of now
    ppos.ts() = currCycle.simTime;

    // if we are runnig beyond 'to' we might become invalid (especially too low, too high)
    // catch that case...likely the a/c is to be removed due to outdated data
    // soon anyway, we just speed up things a bit here
    if (!ppos.isNormal()) {
        // set the plane invalid and bail out with message
        SetInvalid();
        LOG_MSG(logWARN, ERR_POS_UNNORMAL, labelInternal.c_str(),
                dataRefs.GetDebugAcPos(key()) ?
                std::string(*this).c_str() : ppos.dbgTxt().c_str());
        return false;
    }
    
    // *** Half-way through preparations ***
    if (f >= 0.5 && f < 1.0)
    {
        // Cut Corner: half-way through prepare a quadratic curve to cut the corner...if needed
        if (to.f.bCutCorner &&                             // only for cut-corner positions
            !bNeedCCBezier &&                              // flag not already set?
            !turn.isTsBeforeEnd(currCycle.simTime) &&      // Bezier not already defined?
            vec.dist > SIMILAR_POS_DIST &&                 // reasonable leg distance and turn amount?
            std::abs(HeadingDiff(ppos.heading(), to.heading())) >= BEZIER_MIN_HEAD_DIFF)
        {
            // set the flag to fetch the next leg. All the rest is done above
            bNeedCCBezier = true;
        }
        // otherwise prepare turning heading to final heading (if not done already).
        //
        // On the ground at high leg-average speed (rollout, takeoff,
        // fast taxi) we deliberately do NOT retarget heading to
        // `to.heading()` here. The linear path set up at the start of
        // the leg already aimed heading at `vec.angle` (the motion
        // direction), which is the visually correct nose direction
        // during high-speed ground travel. Retargeting to the next
        // slot's reported heading would restart the same problem the
        // Bezier-skip above is trying to avoid: rendered nose pointing
        // away from the direction of motion. Once leg-average speed
        // drops below `GND_TRACK_HEADING_MIN_KT`, this branch is
        // allowed to fire and the aircraft can begin converging on
        // the slot's reported orientation for the upcoming turn-off,
        // gate manoeuvre, or other slow-speed activity.
        //
        // The condition uses `vec.speed_kn()` (leg-average) to match
        // the bGndFast check at leg-setup above. Using the current
        // rendered `GetSpeed_kt()` here would let the retarget fire
        // during the acceleration phase of a takeoff leg before the
        // rendered speed has caught up to the leg average, undoing
        // the Bezier-skip choice for the very legs that need it most.
        else if (!dequal(heading.toVal(), to.heading()) &&
                 !(IsOnGrnd() &&
                   !std::isnan(vec.speed) &&
                   vec.speed_kn() >= GND_TRACK_HEADING_MIN_KT))
        {
            heading.defDuration = IsOnGrnd() ? pMdl->TAXI_TURN_TIME : pMdl->FLIGHT_TURN_TIME;
            heading.moveQuickestToBy(ppos.heading(), to.heading(), // target heading
                                     NAN, to.ts(),      // by target timestamp
                                     false);            // start as late as possible
        }
    }

    // *** Attitude ***
    
    // Calculate roll based on heading change
    CalcRoll(prevHead);

    // current pitch
    ppos.pitch() = pitch.get();
    
#ifdef DEBUG
    std::string debPpos ( ppos.dbgTxt() );
#endif
    LOG_ASSERT_FD(fd,ppos.isFullyValid());

    // *** Height and Flight Model ***
    // Now we know our new position, determine height above ground
    YProbe();

    // Calculate other a/c parameters
    CalcFlightModel (from, to);
    
    if ( bOnGrnd )
    {
        // safety measure:
        // on the ground we are...on the ground, not moving vertically
        ppos.alt_m() = terrainAlt_m;
        vsi = 0;
        // but tires are rotating
        tireRpm.SetVal(std::min(TireRpm(GetSpeed_kt()),
                                tireRpm.defMax));

        // ------------------------------------------------------------------
        // Hard-set ground attitude every frame to defeat feed-driven jitter.
        //
        // Why this exists: data feeds and the position-interpolation code
        // path can produce small drifts in pitch and roll while an aircraft
        // is sitting on (or rolling along) the ground. Real aircraft are
        // mechanically held in a fixed attitude by their landing gear —
        // they do not bank while taxiing and their pitch is determined by
        // gear geometry rather than dynamic flight forces. So we forcibly
        // clamp pitch and roll to the constants `GND_PITCH_DEG` /
        // `GND_ROLL_DEG` defined in `Constants.h`, overriding whatever the
        // interpolation/flight-model code produced earlier in this frame.
        //
        // Exceptions: phases where the nose is genuinely moving relative
        // to the ground — rotation for take-off (`FPH_ROTATE`), lift-off
        // itself (`FPH_LIFT_OFF`), the flare before touchdown
        // (`FPH_FLARE`), the single-cycle touchdown event
        // (`FPH_TOUCH_DOWN`), and the roll-out that immediately follows
        // touchdown (`FPH_ROLL_OUT`). In all of these the flight-model
        // code is actively driving the `pitch` MovingParam through a
        // planned transition — `pitch.moveTo(ROTATE_PITCH_MAX_DEG)` on
        // rotate, VSI-derived target on lift-off,
        // `pitch.moveTo(PITCH_FLARE)` on flare,
        // `pitch.moveTo(GND_PITCH_DEG)` on touchdown to walk the nose
        // down smoothly during roll-out. Overriding pitch during any of
        // these phases would visibly snap the nose. In particular:
        //   - Without the `FPH_ROLL_OUT` exception, the de-rotation
        //     animation gets clobbered one frame after touchdown
        //     (touchdown is documented as a single-frame event) and
        //     the aircraft appears to slam its nose-wheel down.
        //   - Without the `FPH_LIFT_OFF` exception, an aircraft whose
        //     phase advances ROTATE → LIFT_OFF *while still bOnGrnd*
        //     (VSI crossed `VSI_STABLE` before the aircraft physically
        //     left the runway — common on takeoff rolls where the
        //     altitude is barometric and the smoothed value crosses
        //     `MDL_CLOSE_TO_GND` a frame or two before the deque
        //     bracket itself leaves the ground) gets its rotation pitch
        //     forcibly reset to `GND_PITCH_DEG = 2°` for as many frames
        //     as it takes for bOnGrnd to flip false. Visible as: nose
        //     pitches up, briefly flips level on the runway, then
        //     pitches up again once airborne. Reported on AAL2449.
        // Roll is forced flat in all phases — ground aircraft never
        // bank, so no exception is needed there.
        if (phase != FPH_ROTATE &&
            phase != FPH_LIFT_OFF &&
            phase != FPH_FLARE  &&
            phase != FPH_TOUCH_DOWN &&
            phase != FPH_ROLL_OUT)
        {
            ppos.pitch() = GND_PITCH_DEG;
            ppos.roll()  = GND_ROLL_DEG;
        } else {
            // Even in the dynamic-pitch phases, roll should still be
            // forced flat — there is no scenario where a wheeled
            // aircraft banks during rotation/flare/touchdown/roll-out.
            ppos.roll() = GND_ROLL_DEG;
        }
    } else {
        // not on the ground
        // just lifted off? then recalc vsi
        if (phase == FPH_LIFT_OFF && dequal(vsi, 0)) {
            vsi = ppos.vsi_ft(to);
        }

        // ------------------------------------------------------------------
        // Smooth altitude blend during the first LIFTOFF_BLEND_TIME_S
        // seconds after the on-ground → airborne transition.
        //
        // CalcFlightModel records `liftoffBlendStartTs` on the frame that
        // bOnGrnd flips from true to false. Up until that moment the
        // altitude was clamped to `terrainAlt_m` by the `if (bOnGrnd)`
        // branch above; on the very next frame the clamp goes away and
        // ppos.alt_m takes on its raw linearly-interpolated value
        // between the last on-ground slot (at terrain level) and the
        // next airborne slot (which may be 100-500 ft above the runway,
        // depending on how far apart the feed samples are in time).
        // Without intervention the aircraft visibly teleports up to
        // that interpolated altitude in a single frame — the "jumps
        // into the air on rotation" symptom.
        //
        // We lerp from the frozen terrain altitude at liftoff
        // (`liftoffStartAlt_m`) to the live spline-smoothed raw
        // altitude (already computed into `ppos.alt_m()` via
        // `LookupAltAtTs(simTime)` in the air-branch above) using
        // Ken Perlin's quintic smootherstep:
        //
        //     blend(t) = 6t⁵ − 15t⁴ + 10t³
        //
        // Smootherstep is C²-continuous at both endpoints (value AND
        // slope AND curvature are zero at t=0 and at t=1 with respect
        // to the blend's shape), so neither the runway departure nor
        // the post-blend handover introduces a kink from the blend
        // function itself.
        //
        // Because the live `ppos.alt_m()` going in is now also a
        // smooth Hermite/Catmull-Rom spline across `posList` (rather
        // than the per-leg-linear interp it used to be), the only
        // remaining source of "kinks" — slope discontinuities at
        // deque slot boundaries — is gone. The combined visual is
        // a single smooth runway-to-climbout arc.
        //
        // At t = T (sinceLiftoff = LIFTOFF_BLEND_TIME_S), blend = 1
        // and ppos.alt_m() = LookupAltAtTs(simTime). The very next
        // frame (sinceLiftoff > T) bails out of the blend and the
        // air-branch above continues to write the same Hermite
        // value, so the seam is mathematically exact.
        // ------------------------------------------------------------------
        if (!std::isnan(liftoffBlendStartTs) &&
            !std::isnan(liftoffStartAlt_m))
        {
            const double sinceLiftoff =
                currCycle.simTime - liftoffBlendStartTs;
            if (sinceLiftoff < LIFTOFF_BLEND_TIME_S)
            {
                const double t = sinceLiftoff / LIFTOFF_BLEND_TIME_S;
                const double blend =
                    t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
                // Clamp the (LookupAltAtTs − liftoffStartAlt) delta to
                // be non-negative. If the regression momentarily dips
                // below the frozen liftoff terrain (which can happen
                // when past-ground samples briefly out-weight the
                // future airborne samples during the early blend
                // window), the blend output must not render the
                // aircraft below the runway. Combined with the
                // `bOnGrnd` lock above this guarantees the aircraft
                // stays at or above terrain throughout the blend.
                const double diff = std::max(0.0,
                    ppos.alt_m() - liftoffStartAlt_m);
                ppos.alt_m() = liftoffStartAlt_m + diff * blend;
            } else {
                // Blend complete — release the frozen start altitude
                // and continue rendering with the spline-smoothed
                // raw alt that the air-branch above already produces.
                liftoffBlendStartTs = NAN;
                liftoffStartAlt_m   = NAN;
            }
        }
    }
    
    // save this position for (next) camera view position
    CalcCameraViewPos();
    
    // success
    return true;
}

// From ppos and altitudes we derive other a/c parameters like gear, flaps etc.
// Ultimately, we calculate flight phases based on flight model assumptions
void LTAircraft::CalcFlightModel (const positionTy& /*from*/, const positionTy& to)
{
    // *** calculate decision parameters ***
    
    // previous status
    bool bOnGrndPrev = bOnGrnd;
    flightPhaseE bFPhPrev = phase;
    
    // present height (AGL in ft)
    double PHeight = GetPHeight_ft();
    
    // Are we on the ground or not?
    
    // First: Are we _supposed_ to be on the ground, because we are now and will stay so?
    //        (avoids intermidate lift offs just because the runway has holes)
    if (ppos.IsOnGnd() && to.IsOnGnd()) {
        bOnGrnd = true;
    } else {
        // else: we could also be airborne,
        // so assume 'on the ground' if 'very' close to it, otherwise airborne
        bOnGrnd = PHeight <= MDL_CLOSE_TO_GND;
        // Liftoff-blend ground lock.
        //
        // Once the blend has started, the flight model has *committed*
        // to the aircraft being airborne; the visible aircraft is being
        // ramped up from terrain along the blend curve. If a transient
        // dip in the smoothed altitude (returned by `LookupAltAtTs`)
        // briefly takes the value back below `MDL_CLOSE_TO_GND` above
        // terrain, the `else` branch below would clamp `ppos.alt` to
        // terrain ("slam to ground"), the phase would regress to
        // `FPH_TO_ROLL`/`FPH_TAXI`, and the next time the regression
        // value rises again the lift-off transition fires anew —
        // `liftoffBlendStartTs` resets and the blend restarts from
        // scratch. Visibly: aircraft climbs to ~Nft, slams to the
        // runway, continues T/O roll, rotates again, climbs again.
        // That sequence was exactly what users reported on KLM99L /
        // IBE07TV and similar departures with sparse early-climb data.
        //
        // The dip is mostly an artefact of the smoothing window
        // rebalancing as past slots' Gaussian weights decay or the
        // next future slot's slope contribution shifts — physically
        // the aircraft has not landed. We therefore lock `bOnGrnd`
        // to false for the duration of the active blend.
        if (bOnGrnd && !std::isnan(liftoffBlendStartTs) &&
            currCycle.simTime - liftoffBlendStartTs < LIFTOFF_BLEND_TIME_S)
        {
            bOnGrnd = false;
        }
        ppos.f.onGrnd = bOnGrnd ? GND_ON : GND_OFF;
    }
    
    // Vertical Direction
    enum { V_Sinking=-1, V_Stable=0, V_Climbing=1 } VertDir = V_Stable;
    if ( vsi < -pMdl->VSI_STABLE ) VertDir = V_Sinking;
    else if ( vsi > pMdl->VSI_STABLE ) VertDir = V_Climbing;
    
    // if we _are_ on the ground then height is zero and there's no VSI
    if (bOnGrnd) {
        PHeight = 0;
        VertDir = V_Stable;
    }
    
    // *** decide the flight phase ***
    
    // Parked?
    if (bOnGrnd &&                                      //     must be on ground
        speed.m_s() < 0.5 &&                            // AND very slow
        !IsGroundVehicle() &&                           // AND NOT a car
        (ppos.f.specialPos == SPOS_STARTUP ||           // AND (   current pos is STARTUP)
         (posList.size() >= 2 &&                        //      OR (to AND from pos are STARTUP)
          (posList.front().f.specialPos == SPOS_STARTUP &&
           posList[1].f.specialPos == SPOS_STARTUP))))
    {
        phase = FPH_PARKED;
    }
    // ELSE on the ground with low speed
    else if ( bOnGrnd && speed.kt() <= pMdl->MAX_TAXI_SPEED )
    {
        // if not artifically reducing speed (roll-out)
        if (!bArtificalPos)
            phase = FPH_TAXI;
        // so we are rolling out artifically...have we stopped?
        else if (speed.isZero())
            phase = FPH_STOPPED_ON_RWY;
    }
    
    // on the ground with high speed or on a runway
    if ( bOnGrnd && (speed.kt() > pMdl->MAX_TAXI_SPEED || IsOnRwy()))
    {
        if ( bFPhPrev <= FPH_LIFT_OFF )     // before take off
            phase = FPH_TO_ROLL;
        else if (speed.isZero())            // stopped on rwy
            phase = FPH_STOPPED_ON_RWY;
        else                                // else: rolling out
            phase = FPH_ROLL_OUT;
    }
    
    // Determine FPH_ROTATE by use of rotate timestamp
    // (set in LTFlightData::CalcNextPos)
    if ( phase < FPH_ROTATE &&
         rotateTs <= currCycle.simTime && currCycle.simTime <= rotateTs + 2 * pMdl->ROTATE_TIME ) {
        phase = FPH_ROTATE;
    }

    // Diagnostic: log every bOnGrnd transition during the climb-out.
    // Single line per transition, per aircraft — low volume in the log
    // but enough to reconstruct any takeoff sequence and rule out (or
    // catch) repeat lift-off/slam-to-ground events.
    if (bOnGrndPrev != bOnGrnd && bFPhPrev != FPH_UNKNOWN) {
        const bool blendActive =
            !std::isnan(liftoffBlendStartTs) &&
            (currCycle.simTime - liftoffBlendStartTs) < LIFTOFF_BLEND_TIME_S;
        LOG_MSG(logDEBUG,
                "ALT_DIAG %s simT=%.1f bOnGrnd %d->%d ppos.alt=%.1fft "
                "PHeight=%.1fft terrain=%.1fft blendActive=%d sinceLO=%.2fs phase=%s",
                key().c_str(), currCycle.simTime,
                int(bOnGrndPrev), int(bOnGrnd),
                ppos.alt_ft(), PHeight, GetTerrainAlt_ft(),
                int(blendActive),
                std::isnan(liftoffBlendStartTs)
                    ? -1.0
                    : currCycle.simTime - liftoffBlendStartTs,
                FlightPhase2String(phase).c_str());
    }

    // last frame: on ground, this frame: not on ground -> we just lifted off
    if ( bOnGrndPrev && !bOnGrnd && bFPhPrev != FPH_UNKNOWN ) {
        phase = FPH_LIFT_OFF;
        // Record the wall-clock sim time so CalcPPos can blend the
        // altitude smoothly upward from terrain over the next
        // LIFTOFF_BLEND_TIME_S seconds. Without this, the rendered
        // altitude would jump from terrain level (clamped while on
        // ground) to the raw interpolated airborne value on this very
        // frame — the aircraft visually teleports upward. See the
        // blend application near `if (bOnGrnd) ... else { ... }` in
        // CalcPPos and the rationale in Constants.h.
        liftoffBlendStartTs = currCycle.simTime;
        // Freeze the start altitude (terrain at this moment). Using a
        // frozen value rather than reading `terrainAlt_m` every frame
        // means the blend curve does not bob as the YProbe samples
        // slightly different terrain elevations while the aircraft
        // moves horizontally during the 10 s blend.
        liftoffStartAlt_m   = terrainAlt_m;
    }
    
    // climbing but not even reached gear-up altitude
    if (VertDir == V_Climbing &&
        PHeight < pMdl->AGL_GEAR_UP) {
        phase = FPH_LIFT_OFF;
    }
    
    // climbing through gear-up altitude
    if (VertDir == V_Climbing &&
        PHeight >= pMdl->AGL_GEAR_UP) {
        phase = FPH_INITIAL_CLIMB;
    }
    
    // climbing through flaps toggle speed
    if (VertDir == V_Climbing &&
        PHeight >= pMdl->AGL_GEAR_UP &&
        speed.kt() >= pMdl->FLAPS_UP_SPEED) {
        phase = FPH_CLIMB;
    }
    
    // cruise when leveling off with a certain height
    // (this means that if leveling off below cruise alt while e.g. in CLIMB phase
    //  we keep CLIMB phase)
    if (VertDir == V_Stable &&
        PHeight >= pMdl->CRUISE_HEIGHT) {
        phase = FPH_CRUISE;
    }
    
    // sinking, but still above flaps toggle height
    if (VertDir == V_Sinking &&
        speed.kt() > pMdl->FLAPS_DOWN_SPEED) {
        phase = FPH_DESCEND;
    }
    
    // sinking through flaps toggle speed
    if (VertDir == V_Sinking &&
        speed.kt() <= pMdl->FLAPS_DOWN_SPEED) {
        phase = FPH_APPROACH;
    }
    
    // sinking through gear-down height
    if (VertDir == V_Sinking &&
        speed.kt() <= pMdl->FLAPS_DOWN_SPEED &&
        PHeight <= pMdl->AGL_GEAR_DOWN) {
        phase = FPH_FINAL;
    }
    
    // sinking through flare height
    if (VertDir == V_Sinking &&
        speed.kt() <= pMdl->FLAPS_DOWN_SPEED &&
        PHeight <= pMdl->AGL_FLARE) {
        phase = FPH_FLARE;
    }
    
    // last frame: not on ground, this frame: on ground -> we just touched down
    if ( !bOnGrndPrev && bOnGrnd && bFPhPrev != FPH_UNKNOWN ) {
        phase = FPH_TOUCH_DOWN;
    }

    // *** Cars will not actually fly, so we only allow for a limited set of status
    if (fd.GetUnsafeStat().isGrndVehicle() &&
        phase != FPH_TAXI &&
        phase != FPH_STOPPED_ON_RWY)
    {
        phase = FPH_TAXI;
    }
    
    // *** take action based on flight phase (change) ***
    
    // must not be FPH_UNKNOWN any longer
    if (phase == FPH_UNKNOWN)
        // situation is most likely: stable flight below cruise altitude
        phase = FPH_CRUISE;

// entered (or positively skipped over) a phase
#define ENTERED(ph) (bFPhPrev < ph && phase >= ph)
    
    // note: the initial phase during object creation is FPH_UNKNOWN,
    //       which is less than any actual flight phase.
    //       That means: The first time this function is evaluated for a
    //       new a/c object it 'executes' all flight phase changes
    //       until the current calculated phase.
    //       Hence, we can actually rely on properly set actions from
    //       previous phases, even if we start seeing the a/c in mid-flight.
    
    // Phase Parking *** Model Init ***
    if (ENTERED(FPH_PARKED) || phase == FPH_PARKED) {
        // this will only be executed when coming from FPH_UNKOWN,
        // i.e during the first call to the function per a/c object
        // -> can be used for flight model initialization
        // some assumption to begin with...
        SetThrustRatio(0.0f);                           // engines off
        SetLightsLanding(false);                        // most lights off
        SetLightsTaxi(false);
        SetLightsBeacon(false);
        SetLightsStrobe(false);
        SetLightsNav(true);                             // only keep NAV lights on
        
        gear.down();
        gearDeflection.half();
        flaps.up();
    }
    
    // Phase Taxi
    if (ENTERED(FPH_TAXI)) {
        SetThrustRatio(0.1f);
        SetLightsLanding(dataRefs.GetLndLightsTaxi());
        SetLightsTaxi(true);
        SetLightsBeacon(true);
        SetLightsStrobe(false);
    }
    
    // Phase Take Off
    if (ENTERED(FPH_TAKE_OFF)) {
        SetLightsStrobe(true);
        SetLightsLanding(true);
        SetThrustRatio(1.0f);       // a bit late...but anyway ;)
        flaps.half();
    }
    
    // Rotating
    if (ENTERED(FPH_ROTATE)) {
        // (as we don't do any counter-measure in the next ENTERED-statements
        //  we can lift the nose only if we are exatly AT rotate phase)
        if (phase == FPH_ROTATE) {
            // Cap the rotate-phase pitch target at `ROTATE_PITCH_MAX_DEG`
            // (10°) instead of `pMdl->PITCH_MAX` (15°). On the runway
            // the latter exceeds the tail-strike geometry of most
            // narrow-bodies. Once the aircraft transitions to
            // FPH_LIFT_OFF the in-air pitch logic in
            // `LTFlightData::CalcNextPos` (line ~1700) takes over and
            // walks pitch toward the VSI-derived target, clamped to
            // `pMdl->PITCH_MAX` — so steep climbs can still reach the
            // full 15°, just not while the gear is still on the runway.
            pitch.moveTo(ROTATE_PITCH_MAX_DEG);
            gearDeflection.min();               // and start easing up on the wheels
        }
    }
    
    // Lift off
    if (ENTERED(FPH_LIFT_OFF)) {
        // Tires are rotating but shall stop in max 5s
        if (gear.isDown())
            tireRpm.min();                              // "move" to 0
        gearDeflection.min();
        CalcCorrAngle();                        // might need to correct for cross wind
    }
    
    // entered Initial Climb
    if (ENTERED(FPH_INITIAL_CLIMB)) {
        gear.up();
        rotateTs = NAN;             // 'eat' the rotate timestamp, so we don't rotate any longer
    }
    
    // entered climb (from below) or climbing (catches go-around)
    if (ENTERED(FPH_CLIMB) || phase == FPH_CLIMB) {
        SetLightsTaxi(false);
        SetThrustRatio(0.8f);
        gear.up();
        flaps.up();
    }
    
    // cruise
    if (ENTERED(FPH_CRUISE)) {
        SetThrustRatio(0.6f);
        flaps.up();
    }

    // descend
    if (ENTERED(FPH_DESCEND)) {
        SetThrustRatio(0.1f);
        flaps.up();
    }
    
    // approach
    if (ENTERED(FPH_APPROACH)) {
        SetThrustRatio(0.2f);
        flaps.half();
    }
    
    // final
    if (ENTERED(FPH_FINAL)) {
        SetLightsTaxi(true);
        SetLightsLanding(true);
        SetThrustRatio(0.3f);
        flaps.down();
        gear.down();
        gearDeflection.min();
    }
    
    // flare
    if (ENTERED(FPH_FLARE)) {
        pitch.moveTo(pMdl->PITCH_FLARE);      // flare!
        corrAngle.moveTo(0.0);                // de-crab
    }
    
    // touch-down
    if (ENTERED(FPH_TOUCH_DOWN)) {
        gearDeflection.max();           // start main gear deflection
        spoilers.max();                 // start deploying spoilers
        ppos.f.onGrnd = GND_ON;
        // DEFERRED nose-down: do NOT call `pitch.moveTo(GND_PITCH_DEG)`
        // here. Record the touchdown timestamp instead; the frame-level
        // check further down in this function will fire the moveTo only
        // after `TOUCHDOWN_HOLD_PITCH_S` seconds have elapsed, modelling
        // the aerobrake during which real airliners keep the nose
        // pitched up at `PITCH_FLARE` after the main gear is on the
        // runway. Until that delayed moveTo fires, the MovingParam's
        // last commanded target remains `PITCH_FLARE` (set on
        // `FPH_FLARE` entry) and the ground-attitude override in
        // `CalcAcPos` is bypassed for both `FPH_TOUCH_DOWN` and
        // `FPH_ROLL_OUT`, so the pitch stays at flare value during the
        // hold.
        touchdownTs = currCycle.simTime;
    }

    // Deferred nose-down after touchdown (TOUCHDOWN_HOLD_PITCH_S hold).
    // Fires once, then clears `touchdownTs` so subsequent frames do
    // nothing. If for any reason the aircraft is destroyed mid-hold,
    // the timestamp dies with it. If the aircraft re-touchdowns
    // (e.g. porpoising) before we fire, the ENTERED(FPH_TOUCH_DOWN)
    // block above simply re-stamps the timestamp, restarting the hold.
    if (!std::isnan(touchdownTs) &&
        currCycle.simTime >= touchdownTs + TOUCHDOWN_HOLD_PITCH_S)
    {
        pitch.moveTo(GND_PITCH_DEG);
        touchdownTs = NAN;
    }
    
    // roll-out
    if (ENTERED(FPH_ROLL_OUT)) {
        SetThrustRatio(-0.9f);          // reversers
        reversers.max();                 // start opening reversers
    }
    
    // if deflected all the way down: start returning to on-the-ground normal
    if (gearDeflection.isDown())
        gearDeflection.half();

    if (phase >= FPH_ROLL_OUT || phase == FPH_TAXI) {
        // stop reversers below 80kn
        if (GetSpeed_kt() < pMdl->MIN_REVERS_SPEED) {
            SetThrustRatio(0.1f);
            reversers.min();
        }
    }
    
    // *** landing light ***
    // is there a landing-light-altitude in the flight model?
    if (pMdl->LIGHT_LL_ALT > 0) {
        // OK to turn OFF?
        if (ppos.alt_ft() > pMdl->LIGHT_LL_ALT) {
            if ((phase < FPH_TAKE_OFF) || (FPH_CLIMB <= phase && phase < FPH_FINAL))
                SetLightsLanding(false);
        } else {
            // need to turn/stay on below LIGHT_LL_ALT
            if (phase >= FPH_TAKE_OFF)
                SetLightsLanding(true);
        }
    }
    
    // *** safety measures ***
    
    // Need gear on the ground
    if ( bOnGrnd ) {
        gear.down();
    }
    
    // taxiing (includings rolling off the runway after landing (cycle phase back to beginning))
    if ( phase == FPH_TAXI || phase == FPH_STOPPED_ON_RWY ) {
        flaps.up();
        spoilers.min();
        SetThrustRatio(0.1f);
        reversers.min();
        SetLightsTaxi(true);
        SetLightsLanding(dataRefs.GetLndLightsTaxi());
        SetLightsStrobe(false);
    }
    
    // *** Log ***
    
    ppos.f.flightPhase = phase;
    
    // if requested log a phase change
    if ( bFPhPrev != phase && dataRefs.GetDebugAcPos(key()) )
        LOG_MSG(logDEBUG,DBG_AC_FLIGHT_PHASE,
                int(bFPhPrev),FlightPhase2String(bFPhPrev).c_str(),
                int(phase),FlightPhase2String(phase).c_str()
                );
}


// determine roll, based on a previous and a current heading
/// @details We assume that max bank angle (`pMdl->ROLL_MAX_BANK`) is applied for
///          the fastest possible turn (pMdl->MIN_FLIGHT_TURN_TIME).
///          If we are turning more slowly then we apply less bank angle.
void LTAircraft::CalcRoll (double _prevHeading)
{
    // How much of a turn did we do since last frame?
    const double partOfCircle = HeadingDiff(_prevHeading, ppos.heading()) / 360.0;
    const double timeFullCircle = currCycle.diffTime / partOfCircle;  // at current turn rate (if small then we turn _very_ fast!)

    // On the ground we should actually better be levelled, but we turn the nose wheel.
    // Note: this assignment is "early" — the final ground-attitude clamp in
    // `CalcAcPos` (the `bOnGrnd` block) will re-assert `GND_ROLL_DEG` after
    // `CalcFlightModel` has run, so anything we write here is just a sane
    // intermediate. We still set it explicitly so log output / debug dumps
    // in between these two points show the correct value.
    if (IsOnGrnd()) {
        // except...if we are a stopped glider ;-)
        if (GetSpeed_m_s() < 0.2 && pMdl->isGlider())
            ppos.roll() = MDL_GLIDER_STOP_ROLL;
        else
            ppos.roll() = GND_ROLL_DEG;
        
        // Nose wheel steering: Hm...we would need to know a lot about the plane's
        // geometry to do that exactly right...so we just guess: 30° for a standard turn:
        SetNoseWheelAngle(std::isnan(timeFullCircle) ? 0.0f :
                          30.0f * float(pMdl->TAXI_TURN_TIME / timeFullCircle));
        return;
    }
    
    // In the air we make sure nose wheel looks straight
    SetNoseWheelAngle(0.0f);
    
    // For the roll we assume that max bank angle is applied for the tightest turn.
    // If we are turning more slowly then we apply less bank angle.
    const double newRoll = (std::isnan(timeFullCircle) ? ppos.roll() :
                            std::abs(timeFullCircle) < pMdl->MIN_FLIGHT_TURN_TIME ? std::copysign(pMdl->ROLL_MAX_BANK,timeFullCircle) :
                            pMdl->ROLL_MAX_BANK * pMdl->MIN_FLIGHT_TURN_TIME / timeFullCircle);
    // safeguard against to harsh roll rates:
    if (std::abs(ppos.roll()-newRoll) > currCycle.diffTime * pMdl->ROLL_RATE) {
        if (newRoll < ppos.roll()) ppos.roll() -= currCycle.diffTime * pMdl->ROLL_RATE;
        else                       ppos.roll() += currCycle.diffTime * pMdl->ROLL_RATE;
    }
    else
        ppos.roll() = newRoll;
}


// determine correction angle
/// @details Uses a simple rule of thumb, good enough for the purpose
///          and saves on computing power:
/// @see https://www.pilotundflugzeug.de/artikel/2005-05-29/Faustformel
///      comment by 'defvh'
/// @details "Windeinfallswinkel mal Windgeschwindigkeit geteilt durch IAS.
///           Beispiel: RWY 27, Wind aus 220 Grad mit 12 kts und IAS 100.
///           Also Vorhalten links mit 6 Grad."
void LTAircraft::CalcCorrAngle ()
{
    // correction only applies to flying before flare
    if (!IsOnGrnd() && phase != FPH_FLARE) {
        const vectorTy vecWind = dataRefs.GetSimWind(ppos.alt_m());
        double headDiff = HeadingDiff(vec.angle, vecWind.angle);
        if (headDiff > 90.0)                // if wind comes from behind only consider cross-wind component
            headDiff = 180.0 - headDiff;    // (so that if it comes straight from back (180deg) it would result in 0 correction
        else if (headDiff < -90.0)
            headDiff = -180.0 - headDiff;
        const double corr = headDiff * vecWind.speed / vec.speed;
        corrAngle.moveTo(corr);
    } else {
        // on the ground and while flare: no correction, or actually de-crab
        corrAngle.moveTo(0.0);
    }
}

// Smooth altitude (m, MSL) at an arbitrary timestamp, fitted as a
// Gaussian-weighted local linear regression. The samples come from a
// per-aircraft archive (`pastAltSamples_`) that mirrors every slot
// we have ever seen in `fd.posDeque`, augmented with the future
// slots currently in `fd.posDeque`. The archive does NOT pop when
// `fd.posDeque` does, so popped slots remain in the regression
// (with their Gaussian weight smoothly decaying toward zero as
// `targetTs` advances past them). This is the *only* shape of input
// that produces visually smooth output — see the rationale on
// `pastAltSamples_` in the header for the full diagnosis.
double LTAircraft::LookupAltAtTs (double targetTs) const
{
    // Take the flight-data lock for the duration of the regression.
    // dataAccessMutex is recursive, so if a caller already holds it
    // the inner lock is a no-op. The fit is a single linear pass
    // over the archive, so the critical section is short.
    std::lock_guard<std::recursive_mutex> lock(fd.dataAccessMutex);
    const dequePositionTy& fullDeque = fd.GetPosDeque();

    // Append any slots we have not yet archived. Both `pastAltSamples_`
    // and `fullDeque` are individually sorted by ts; the boundary
    // condition is that everything in `pastAltSamples_` came from
    // earlier observations of `fullDeque` and is therefore older than
    // or equal to the deque's current contents. We append every slot
    // whose ts is strictly greater than the last archived ts.
    for (const auto& p : fullDeque) {
        if (pastAltSamples_.empty() ||
            p.ts() > pastAltSamples_.back().ts())
        {
            pastAltSamples_.push_back(p);
        }
    }
    // Prune samples well outside the Gaussian tail. We keep ±30 s
    // around `targetTs`: with σ = 5 s the weight at ±30 s = ±6 σ is
    // exp(-18) ≈ 1.5e-8, indistinguishable from zero for double-
    // precision arithmetic, so pruned samples could not measurably
    // affect the regression output.
    constexpr double PRUNE_HORIZON_S = 30.0;
    while (!pastAltSamples_.empty() &&
           pastAltSamples_.front().ts() < targetTs - PRUNE_HORIZON_S)
    {
        pastAltSamples_.pop_front();
    }
    // Also prune samples in the *future* beyond the same horizon —
    // they are buffered for later rendering and should not affect
    // the regression at the current `targetTs`. (In normal operation
    // `fullDeque`'s future extent is much smaller than 30 s, so this
    // is mostly a safety net.)
    while (!pastAltSamples_.empty() &&
           pastAltSamples_.back().ts() > targetTs + PRUNE_HORIZON_S)
    {
        pastAltSamples_.pop_back();
    }

    if (pastAltSamples_.empty()) {
        // Nothing to regress against. Fall back to the freshest
        // available value: deque front if any, else terrain.
        if (!fullDeque.empty())
            return fullDeque.front().alt_m();
        return terrainAlt_m;
    }
    if (pastAltSamples_.size() == 1)
        return pastAltSamples_.front().alt_m();

    // -----------------------------------------------------------------
    // Smoothing strategy: Gaussian-weighted local linear regression.
    //
    // Why an *approximating* fit rather than an *interpolating* spline:
    // ADS-B reports altitude in 25 ft quantization steps, and feed
    // slots arrive at irregular cadence (1–5 s between samples).
    // The per-segment slope is therefore *jagged by construction* —
    // a single steady climb at 2500 fpm shows up as alternating
    // 2000-fpm and 3400-fpm legs whenever the quantization boundary
    // straddles a sample interval. An interpolating spline (PCHIP or
    // otherwise) must pass through every data point, so the
    // quantization-induced slope variation is faithfully reproduced
    // as visible "kinks" in the rendered climb every 4–6 s. That was
    // the user's "100 ft up every few seconds" symptom.
    //
    // Local linear regression instead fits a *trend line* through the
    // window of nearby samples and renders the aircraft along that
    // trend. The 25 ft quantization noise is averaged out; the
    // rendered altitude moves at the *mean* climb rate of the window
    // rather than the instantaneous (quantized) per-leg slope.
    //
    // Weights are Gaussian in (targetTs − ts), σ = 3 s:
    //   - As targetTs advances and slots fall outside ±2σ, their
    //     weight drops smoothly toward zero — no discontinuity when
    //     a slot leaves the effective window. This is the key
    //     reason for the Gaussian weighting: a hard-edged window
    //     would introduce a step every time a sample left it.
    //   - σ = 3 s sets the smoothing scale: a few seconds wide enough
    //     to span typical sample intervals and average out
    //     quantization noise, but narrow enough that the rendered
    //     altitude tracks genuine climb-rate changes with under-1 s
    //     lag (e.g. when transitioning from initial climb to cruise
    //     climb at the gear-up altitude).
    //
    // Gaussian-weighted local linear regression in (ts, alt) over the
    // archived sample list. The fitted line evaluated at `targetTs` is
    // the rendered altitude.
    //
    // Why the archive (`pastAltSamples_`) and not `fullDeque` directly:
    //   `fullDeque` keeps at most ONE past slot at a time (the loop in
    //   LTFlightData::CalcNextPos pops slots as soon as `posDeque[1]`
    //   slides into the past). That single past sample has near-unit
    //   Gaussian weight at `targetTs`. The frame it pops, the weight-
    //   ed mean recomputes WITHOUT it in a single step, and for a
    //   climbing aircraft (where the just-popped slot was the lowest-
    //   altitude one) the mean visibly jumps UP — that is exactly the
    //   "instant 100 ft up" symptom the user reported despite the
    //   smoothing. The archive does not pop, so the same sample
    //   remains in the regression with its Gaussian weight smoothly
    //   decaying to zero over many frames as `targetTs` advances past
    //   it. No more discrete jumps at the deque-pop boundary.
    //
    // Why σ = 5 s: ADS-B altitude quantization (25 ft) plus typical
    // 1–5 s slot cadence means individual per-leg slopes can fluctuate
    // wildly even on a steady climb. σ = 5 s spans ~10–15 effective
    // samples in the ±2σ band — enough to average out the per-leg
    // jitter — while keeping the lag relative to real altitude
    // changes under ~1 s (visible climb-rate transitions still come
    // through promptly).
    //
    // Why we work in *relative* time (t − targetTs) rather than raw
    // epoch seconds: timestamps are ~1.78e9, so t·t is ~3.16e19. The
    // textbook variance formula Σw·t² − sumW·tMean² then subtracts
    // two numbers of order 3e19 to get a result of order 10², and a
    // 64-bit double has only ~16 significant digits — most of the
    // result is lost to catastrophic cancellation. Working in
    // (t − targetTs), all values stay in the ±30 s range, the
    // cancellation goes away, and the slope/intercept are computed
    // accurately. After the shift the query point lives at t_rel=0,
    // so the regression's prediction at `targetTs` is simply
    //   aMean − slope · tMean_rel.
    constexpr double SMOOTH_SIGMA_S = 5.0;
    const double invTwoSigmaSq = 1.0 / (2.0 * SMOOTH_SIGMA_S * SMOOTH_SIGMA_S);

    double sumW   = 0.0;
    double sumWT  = 0.0;   // Σ w · (t − targetTs)
    double sumWA  = 0.0;
    double sumWTT = 0.0;   // Σ w · (t − targetTs)²
    double sumWTA = 0.0;   // Σ w · (t − targetTs) · a
    for (const auto& p : pastAltSamples_) {
        const double t_rel = p.ts() - targetTs;
        const double a     = p.alt_m();
        const double w     = std::exp(-t_rel * t_rel * invTwoSigmaSq);
        sumW   += w;
        sumWT  += w * t_rel;
        sumWA  += w * a;
        sumWTT += w * t_rel * t_rel;
        sumWTA += w * t_rel * a;
    }
    // Degenerate case: zero combined weight (all samples extremely
    // far from targetTs even after pruning). Fall back to the nearest
    // archived sample's altitude.
    if (sumW <= 0.0) {
        const positionTy* pBest = &pastAltSamples_.front();
        double dtBest = std::abs(pBest->ts() - targetTs);
        for (const auto& p : pastAltSamples_) {
            const double dt = std::abs(p.ts() - targetTs);
            if (dt < dtBest) { dtBest = dt; pBest = &p; }
        }
        return pBest->alt_m();
    }

    const double tMean_rel = sumWT / sumW;
    const double aMean     = sumWA / sumW;
    const double varT  = sumWTT - sumW * tMean_rel * tMean_rel;
    const double slope = (varT > 0.0)
        ? ((sumWTA - sumW * tMean_rel * aMean) / varT)
        : 0.0;
    // Predict at t_rel = 0 (= targetTs).
    return aMean - slope * tMean_rel;
}


// determines terrain altitude via XPLM's Y Probe
bool LTAircraft::YProbe ()
{
    // short-cut if not yet due
    // (we do probes only every so often, more often close to the ground,
    //  but less often high up in the air,
    //  and every frame if were in camera view on the ground)
    if ( !(IsInCameraView() && IsOnGrnd()) && currCycle.simTime < probeNextTs )
        return true;
    
    // This is terrain altitude right beneath us in [ft]
    terrainAlt_m = fd.YProbe_at_m(ppos);
    
    if (currCycle.simTime >= probeNextTs)
    {
        // lastly determine when to do a probe next, more often if closer to the ground
        static_assert(sizeof(PROBE_HEIGHT_LIM) == sizeof(PROBE_DELAY));
        for ( size_t i=0; i < sizeof(PROBE_HEIGHT_LIM)/sizeof(PROBE_HEIGHT_LIM[0]); i++)
        {
            if ( GetPHeight_ft() >= PROBE_HEIGHT_LIM[i] ) {
                probeNextTs = currCycle.simTime + PROBE_DELAY[i];
                break;
            }
        }
        LOG_ASSERT_FD(fd,probeNextTs > currCycle.simTime);
        
        // *** unrelated to YProbe...just makes use of the "calc every so often" mechanism
        
        // calc current bearing and distance for pure informational purpose ***
        vecView = dataRefs.GetViewPos().between(ppos);
        // update AI slotting priority
        CalcAIPrio();
        // update the a/c label with fresh values
        LabelUpdate();
        // are we visible?
        CalcVisible();
    }
    
    // Success
    return true;
}

// return a string indicating the use of nav/beacon/strobe/landing lights
std::string LTAircraft::GetLightsStr() const
{
    char buf[30];
    snprintf(buf, sizeof(buf), "%s/%s/%s/%s/%s",
             GetLightsNav() ? "nav" : "---",
             GetLightsBeacon() ? "bcn" : "---",
             GetLightsStrobe() ? "strb" : "----",
             GetLightsTaxi() ? "taxi" : "----",
             GetLightsLanding() ? "land" : "----"
            );
    return std::string(buf);
}

// copies a/c info out into the bulk structure for LTAPI usage
/// @param pOut points to output data area
/// @param size Structure size to be copied. This can be less than sizeof(LTAPIBulkData) once new version are out.
/// @note This function is comparably quick, includes important location info,
///       but misses textual information, see other CopyBulkData() for that.
void LTAircraft::CopyBulkData (LTAPIAircraft::LTAPIBulkData* pOut,
                               size_t size) const
{
    // If size isn't enough for original structure we bail:
    if (size < LTAPIBulkData_v120)
        return;

    // fill all values one by one
    // identification
    pOut->keyNum = fd.key().num;
    // position, attitude
    pOut->lat_f = (float)GetPPos().lat();
    pOut->lon_f = (float)GetPPos().lon();
    pOut->alt_ft_f = (float)GetPPos().alt_ft();
    pOut->heading = (float)GetHeading();
    pOut->track = (float)GetTrack();
    pOut->roll = (float)GetRoll();
    pOut->pitch = (float)GetPitch();
    pOut->speed_kt = (float)GetSpeed_kt();
    pOut->vsi_ft = (float)GetVSI_ft();
    pOut->terrainAlt_ft = (float)GetTerrainAlt_ft();
    pOut->height_ft = (float)GetPHeight_ft();
    // configuration
    pOut->flaps = (float)GetFlapsPos();
    pOut->gear = (float)GetGearPos();
    pOut->reversers = (float)GetReverserPos();
    // simulation
    pOut->bearing = (float)GetVecView().angle;
    pOut->dist_nm = (float)GetVecView().dist / M_per_NM;
    pOut->bits.phase = LTAPIAircraft::LTFlightPhase(GetFlightPhase());
    pOut->bits.onGnd = IsOnGrnd();
    pOut->bits.taxi = GetLightsTaxi();
    pOut->bits.land = GetLightsLanding();
    pOut->bits.bcn  = GetLightsBeacon();
    pOut->bits.strb = GetLightsStrobe();
    pOut->bits.nav  = GetLightsNav();
    pOut->bits.hidden = !IsVisible();
    pOut->bits.camera = IsInCameraView();
    pOut->bits.multiIdx = tcasTargetIdx;
    pOut->bits.trspMode = unsigned(acRadar.mode);       // v4.4.0 addition
    pOut->bits.filler2 = 0;
    pOut->bits.filler3 = 0;
    
    // v1.22 additions
    if (size >= LTAPIBulkData_v122) {
        pOut->lat = GetPPos().lat();
        pOut->lon = GetPPos().lon();
        pOut->alt_ft = GetPPos().alt_ft();
    }
    
    // v4.4.0 additions
    pOut->x     = double(drawInfo.x);           // It is double because X-Plane is moving towards double already...we'll change LiveTraffic probably sometime soon, too
    pOut->y     = double(drawInfo.y);
    pOut->z     = double(drawInfo.z);
    pOut->v_x   = v_x;
    pOut->v_y   = v_y;
    pOut->v_z   = v_z;
}
    
// copies text information out into the bulk structure for LTAPI usage
/// @param pOut points to output data area
/// @param size Structure size to be copied. This can be less than sizeof(LTAPIBulkData) once new version are out.
/// @warning This function is comparably expensive, needs 2 locks for flight data
void LTAircraft::CopyBulkData (LTAPIAircraft::LTAPIBulkInfoTexts* pOut,
                               size_t size) const
{
    // If size isn't enough for original structure we bail:
    if (size < LTAPIBulkInfoTexts_v120)
        return;
    
    // Fill the output buffer one by one
    const LTFlightData::FDStaticData stat = fd.WaitForSafeCopyStat();
    const LTFlightData::FDDynamicData dyn = fd.WaitForSafeCopyDyn();
    pOut->keyNum = fd.key().num;
    STRCPY_ATMOST(pOut->registration,   stat.reg);
    // aircraft model/operator
    STRCPY_ATMOST(pOut->modelIcao,      stat.acTypeIcao);
    STRCPY_ATMOST(pOut->acClass,        pDoc8643->classification);
    STRCPY_ATMOST(pOut->wtc,            pDoc8643->wtc);
    STRCPY_ATMOST(pOut->opIcao,         stat.opIcao);
    STRCPY_ATMOST(pOut->man,            stat.man);
    STRCPY_ATMOST(pOut->model,          stat.mdl);
    STRCPY_ATMOST(pOut->catDescr,       stat.catDescr);
    STRCPY_ATMOST(pOut->op,             stat.op);
    // flight data
    STRCPY_ATMOST(pOut->callSign,       stat.call);
    STRCPY_ATMOST(pOut->squawk,         dyn.GetSquawk());
    STRCPY_ATMOST(pOut->flightNumber,   stat.flight);
    STRCPY_ATMOST(pOut->origin,         stat.origin());
    STRCPY_ATMOST(pOut->destination,    stat.dest());
    STRCPY_ATMOST(pOut->trackedBy,      dyn.pChannel ? dyn.pChannel->ChName() : "-");
    
    // v2.40 additions (cslModel field extended to 40 chars)
    if (size >= LTAPIBulkInfoTexts_v240) {
        STRCPY_ATMOST(pOut->cslModel, GetModelName());
    }
    // v1.22 additions (cslModel field had 24 chars only)
    else if (size >= LTAPIBulkInfoTexts_v122) {
        strncpy_s(pOut->cslModel, 24, GetModelName().c_str(), 23);
    }
}

//
// MARK: Visibility
//

// defines visibility, overrides auto visibility
void LTAircraft::SetVisible (bool b)
{
    bAutoVisible = false;
    bSetVisible = b;
    if (b != IsVisible())       // is new visibility a change?
    {
        XPMP2::Aircraft::SetVisible(b);
        LOG_MSG(logINFO, IsVisible() ? INFO_AC_SHOWN : INFO_AC_HIDDEN,
                labelInternal.c_str());
    }
}

// defines auto visibility, returns if (now) visible
bool LTAircraft::SetAutoVisible (bool b)
{
    bAutoVisible = b;
    return CalcVisible();
}

// calculates if the a/c is visible
bool LTAircraft::CalcVisible ()
{
    // possible change...save old value for comparison
    bool bPrevVisible = IsVisible();
    
    // Hide in replay mode?
    if (dataRefs.GetHidePausedReplay() && (dataRefs.IsReplayMode() || dataRefs.IsSimPaused()))
        XPMP2::Aircraft::SetVisible(false);
    // automatic is off -> take over manually given state
    else if (!dataRefs.IsAutoHidingActive() || !bAutoVisible)
        XPMP2::Aircraft::SetVisible(bSetVisible);
    // hide static objects and we are one?
    else if (dataRefs.GetHideStaticTwr() && bStaticObject)
        XPMP2::Aircraft::SetVisible(false);
    // hide while taxiing...and we are taxiing?
    else if (dataRefs.GetHideTaxiing() &&
        (phase == FPH_TAXI || phase == FPH_STOPPED_ON_RWY))
        XPMP2::Aircraft::SetVisible(false);
    // hide while parking at a startup position?
    else if (dataRefs.GetHideParking() &&
             ppos.f.specialPos == SPOS_STARTUP)
        XPMP2::Aircraft::SetVisible(false);
    // hide below certain height...and we are below that?
    else if (dataRefs.GetHideBelowAGL() > 0 &&
             GetPHeight_ft() < dataRefs.GetHideBelowAGL())
        XPMP2::Aircraft::SetVisible(false);
    // hide if close to user's aircraft?
    else {
        const int hideDist = dataRefs.GetHideNearby(IsOnGrnd());
        if (hideDist > 0) {
            // We need the distance to the user's aircraft
            const positionTy userPos = dataRefs.GetUsersPlanePos();
            const double dist = ppos.dist(userPos);
            XPMP2::Aircraft::SetVisible(dist > double(hideDist));
        }
        else
            // otherwise we are visible
            XPMP2::Aircraft::SetVisible(true);
    }
    
    // inform about a change
    if (bPrevVisible != IsVisible())
        LOG_MSG(logDEBUG, IsVisible() ? INFO_AC_SHOWN_AUTO : INFO_AC_HIDDEN_AUTO,
                labelInternal.c_str());

    // return new visibility
    return IsVisible();
}

/// Determines AI priority based on bearing to user's plane and ground status
/// 1. Planes in the 30° sector in front of user's plane
/// 2. Planes in the 90° sector in front of user's plane
/// 3. All else
/// If user is flying then airborne planes have in total higher prio than
/// taxiing planes.
/// @warning Should only be called "every so often" but not every drawing frame
void LTAircraft::CalcAIPrio ()
{
    // If this is the plane, which is currently in camera view,
    // then we want to see it in map apps as well:
    if (IsInCameraView()) {
        aiPrio = 0;
        return;
    }
    
    // user's plane's position and bearing from user's plane to this aircraft
    double userTrack = 0.0;
    positionTy posUser = dataRefs.GetUsersPlanePos(nullptr, &userTrack);
    if (posUser.IsOnGnd())              // if on the ground
        userTrack = posUser.heading();      // heading is more reliable
    const double bearing = posUser.angle(ppos);
    const double diff = std::abs(HeadingDiff(userTrack, bearing));
    
    // 1. Planes in the 30° sector in front of user's plane
    if (diff < 30)
        aiPrio = 0;
    // 2. Planes in the 90° sector in front of user's plane
    else if (diff < 90)
        aiPrio = 1;
    // 3. All else (default)
    else
        aiPrio = 2;
    
    // Ground consideration only if user's plane is flying but this a/c not
    if (!posUser.IsOnGnd() && IsOnGrnd())
        aiPrio += 3;
}

//
// MARK: External Camera View
//

LTAircraft* LTAircraft::pExtViewAc = nullptr;
positionTy  LTAircraft::posExt;
XPViewTypes LTAircraft::prevView = VIEW_UNKNOWN;
XPLMCameraPosition_t  LTAircraft::extOffs;

// start an outside camery view
void LTAircraft::ToggleCameraView()
{
    // reset camera offset
    extOffs.x = extOffs.y = extOffs.z = extOffs.heading = extOffs.roll = 0.0f;
    extOffs.zoom = 1.0f;
    extOffs.pitch = MDL_EXT_CAMERA_PITCH;

    // starting a new external view?
    if (!pExtViewAc) {
        pExtViewAc = this;                          // remember ourself as the aircraft to show
        if (!dataRefs.ShallUseExternalCamera()) {
            CalcCameraViewPos();                    // calc first position

            // we shall ensure to set an external view first,
            // so that sound and 2D stuff is handled correctly
            if (!dataRefs.IsViewExternal()) {
                prevView = dataRefs.GetViewType();
                dataRefs.SetViewType(VIEW_FREE_CAM);
            }
            else
                prevView = VIEW_UNKNOWN;

            XPLMControlCamera(xplm_ControlCameraUntilViewChanges, CameraCB, nullptr);
            CameraRegisterCommands(true);
        }
    }
    else if (pExtViewAc == this) {      // me again? -> switch off
        pExtViewAc = nullptr;
        if (!dataRefs.ShallUseExternalCamera()) {
            CameraRegisterCommands(false);
            XPLMDontControlCamera();

            // if a previous view is known we make sure we go back there
            if (prevView) {
                dataRefs.SetViewType(prevView);
                prevView = VIEW_UNKNOWN;
            }
        }
    }
    else {                              // view another plane
        pExtViewAc = this;
        CalcCameraViewPos();
    }
    
    // Inform 3rd party camera plugins
    dataRefs.SetCameraAc(pExtViewAc);
}

// calculate the correct external camera position
void LTAircraft::CalcCameraViewPos()
{
    if (IsInCameraView() && !dataRefs.ShallUseExternalCamera()) {
        posExt = ppos;

        // move position back along the longitudinal axes
        posExt += vectorTy(ppos.heading(), pMdl->EXT_CAMERA_LON_OFS + extOffs.x);
        // move position a bit to the side
        posExt += vectorTy(ppos.heading() + 90, pMdl->EXT_CAMERA_LAT_OFS + extOffs.z);
        // and move a bit up
        posExt.alt_m() += pMdl->EXT_CAMERA_VERT_OFS + extOffs.y;

        // convert to local
        posExt.WorldToLocal();
    }
}


// callback for external camera view
int LTAircraft::CameraCB (XPLMCameraPosition_t* outCameraPosition,
                          int                   inIsLosingControl,
                          void *                /*inRefcon*/)
{
    // Loosing control? So be it...
    if (!pExtViewAc || inIsLosingControl || !outCameraPosition)
    {
        CameraRegisterCommands(false);
        pExtViewAc = nullptr;
        dataRefs.SetCameraAc(nullptr);
        return 0;
    }

    // we have camera control, position has been calculated already in CalcPPos,
    // take it from posExt, fill output structure, apply movement by commands and pilot's head
    // factor of 10 means: If head moves 1m then view moves 10m...just imagine how big a wide body airliner is...
    outCameraPosition->x =        (float)posExt.X();
    outCameraPosition->y =        (float)posExt.Y();
    outCameraPosition->z =        (float)posExt.Z();
    outCameraPosition->heading  = (float)posExt.heading() + extOffs.heading;
    outCameraPosition->pitch =                              extOffs.pitch;
    outCameraPosition->roll =                               extOffs.roll;
    outCameraPosition->zoom =                               extOffs.zoom;
    
    // Reset the counter that counts flight loop calls w/o camera control.
    // The "loosing control" part above works great if X-Plane itself takes over camera control,
    // but reportedly not if a 3rd party plugin takes over, so we count ourselves.
    dataRefs.CntCameraCallback();
    
    return 1;
}


// command handling during camera view for camera movement
void LTAircraft::CameraRegisterCommands(bool bRegister)
{
    // first time init?
    for (int i = CR_GENERAL_LEFT; i <= CR_GENERAL_ZOOM_OUT_FAST; i++) {
        if (dataRefs.cmdXP[i]) {
            if (bRegister)
                XPLMRegisterCommandHandler(dataRefs.cmdXP[i], CameraCommandsCB, 0, (void*)(long long)i);
            else
                XPLMUnregisterCommandHandler(dataRefs.cmdXP[i], CameraCommandsCB, 0, (void*)(long long)i);
        }
    }
}

int LTAircraft::CameraCommandsCB(
    XPLMCommandRef      ,
    XPLMCommandPhase    inPhase,
    void *              inRefcon)
{
    // safety check: release commands if we aren't in camera view
    if (!pExtViewAc) {
        CameraRegisterCommands(false);
        return 1;
    }

    // we only process Begin and Continue, but not End
    if (inPhase == xplm_CommandEnd)
        return 0;

    // process the command by adjusting the offset of the camera:
    cmdRefsXP cmd = (cmdRefsXP)reinterpret_cast<long long>(inRefcon);

    // for the "corner" hat switch commands we simply process 2 of them
    cmdRefsXP cmd2 = CR_NO_COMMAND;
    switch (cmd) {
    case CR_GENERAL_HAT_SWITCH_UP_LEFT:
    case CR_GENERAL_HAT_SWITCH_DOWN_LEFT:   cmd2 = CR_GENERAL_LEFT; break;
    case CR_GENERAL_HAT_SWITCH_UP_RIGHT:
    case CR_GENERAL_HAT_SWITCH_DOWN_RIGHT:  cmd2 = CR_GENERAL_RIGHT; break;
    default: break;
    }

    // for the move commands (on the plane, i.e. left/right, forward/backward)
    // the orientation of the camera with respect to the aircraft is important:
    // - standard view from back looking forward means: left key is left motion, and forward key is forward motion
    // - looking from the side on plane's starboard means: right key is forward movement and forward key is left movement
    // with all calcs remember: extOffs.x/z are relative to plane's axis (and _not_ to X-Plane's coordinate system)
    float sinOfs = 0.0f;            // sinus of heading difference (which is precicely what extOffs.heading is)
    float cosOfs = 1.0f;
    if (CR_GENERAL_LEFT <= cmd && cmd <= CR_GENERAL_HAT_SWITCH_DOWN_RIGHT) {
        float extOffsHeadRad = (float)deg2rad(extOffs.heading);
        sinOfs = std::sin(extOffsHeadRad);
        cosOfs = std::cos(extOffsHeadRad);
    }

    while (cmd != CR_NO_COMMAND) {
        switch (cmd) {
            // movement on the plane
            // x = longitudinal axis, z = lateral axis
        case CR_GENERAL_HAT_SWITCH_LEFT:
        case CR_GENERAL_LEFT:           extOffs.z -= MDL_EXT_STEP_MOVE * cosOfs; extOffs.x += MDL_EXT_STEP_MOVE * sinOfs; break;
        case CR_GENERAL_HAT_SWITCH_RIGHT:
        case CR_GENERAL_RIGHT:          extOffs.z += MDL_EXT_STEP_MOVE * cosOfs; extOffs.x -= MDL_EXT_STEP_MOVE * sinOfs; break;
        case CR_GENERAL_HAT_SWITCH_UP_LEFT:
        case CR_GENERAL_HAT_SWITCH_UP_RIGHT:
        case CR_GENERAL_HAT_SWITCH_UP:
        case CR_GENERAL_FORWARD:        extOffs.z += MDL_EXT_STEP_MOVE * sinOfs; extOffs.x += MDL_EXT_STEP_MOVE * cosOfs; break;
        case CR_GENERAL_HAT_SWITCH_DOWN_LEFT:
        case CR_GENERAL_HAT_SWITCH_DOWN_RIGHT:
        case CR_GENERAL_HAT_SWITCH_DOWN:
        case CR_GENERAL_BACKWARD:       extOffs.z -= MDL_EXT_STEP_MOVE * sinOfs; extOffs.x -= MDL_EXT_STEP_MOVE * cosOfs; break;

        case CR_GENERAL_LEFT_FAST:      extOffs.z -= MDL_EXT_FAST_MOVE * cosOfs; extOffs.x += MDL_EXT_FAST_MOVE * sinOfs; break;
        case CR_GENERAL_RIGHT_FAST:     extOffs.z += MDL_EXT_FAST_MOVE * cosOfs; extOffs.x -= MDL_EXT_FAST_MOVE * sinOfs; break;
        case CR_GENERAL_FORWARD_FAST:   extOffs.z += MDL_EXT_FAST_MOVE * sinOfs; extOffs.x += MDL_EXT_FAST_MOVE * cosOfs; break;
        case CR_GENERAL_BACKWARD_FAST:  extOffs.z -= MDL_EXT_FAST_MOVE * sinOfs; extOffs.x -= MDL_EXT_FAST_MOVE * cosOfs; break;
            // up/down
        case CR_GENERAL_UP:             extOffs.y += MDL_EXT_STEP_MOVE; break;
        case CR_GENERAL_DOWN:           extOffs.y -= MDL_EXT_STEP_MOVE; break;
        case CR_GENERAL_UP_FAST:        extOffs.y += MDL_EXT_FAST_MOVE; break;
        case CR_GENERAL_DOWN_FAST:      extOffs.y -= MDL_EXT_FAST_MOVE; break;
            // heading change
        case CR_GENERAL_ROT_LEFT:       extOffs.heading -= MDL_EXT_STEP_DEG; break;
        case CR_GENERAL_ROT_RIGHT:      extOffs.heading += MDL_EXT_STEP_DEG; break;
        case CR_GENERAL_ROT_LEFT_FAST:  extOffs.heading -= MDL_EXT_FAST_DEG; break;
        case CR_GENERAL_ROT_RIGHT_FAST: extOffs.heading += MDL_EXT_FAST_DEG; break;
            // tilt/pitch
        case CR_GENERAL_ROT_UP:         extOffs.pitch += MDL_EXT_STEP_DEG; break;
        case CR_GENERAL_ROT_DOWN:       extOffs.pitch -= MDL_EXT_STEP_DEG; break;
        case CR_GENERAL_ROT_UP_FAST:    extOffs.pitch += MDL_EXT_FAST_DEG; break;
        case CR_GENERAL_ROT_DOWN_FAST:  extOffs.pitch -= MDL_EXT_FAST_DEG; break;
            // zoom
        case CR_GENERAL_ZOOM_IN:        extOffs.zoom *= MDL_EXT_STEP_FACTOR; break;
        case CR_GENERAL_ZOOM_OUT:       extOffs.zoom /= MDL_EXT_STEP_FACTOR; break;
        case CR_GENERAL_ZOOM_IN_FAST:   extOffs.zoom *= MDL_EXT_FAST_FACTOR; break;
        case CR_GENERAL_ZOOM_OUT_FAST:  extOffs.zoom /= MDL_EXT_FAST_FACTOR; break;

            // should not happen, but if so pass on to X-Plane
        default: return 1;
        }

        // if necessary process the 2nd command
        cmd = cmd2;
        cmd2 = CR_NO_COMMAND;
    }

    // normalize heading to +/- 180
    while (extOffs.heading < -180)
        extOffs.heading += 360;
    while (extOffs.heading > 180)
        extOffs.heading -= 360;

    // normaize pitch to +/- 180
    while (extOffs.pitch < -180)
        extOffs.pitch += 360;
    while (extOffs.pitch > 180)
        extOffs.pitch -= 360;

    // we handled it fine
    return 0;
}


//
//MARK: XPMP Aircraft Updates (callbacks)
//
//NOTE: These callbacks are entry points into LiveTraffic code.
//      They are especially guarded against exceptions. Once an exception
//      is caught the LTAircraft object is declared invalid and never
//      used again. It will be cleared by AircraftMaintenance.
//
void LTAircraft::UpdatePosition (float, int cycle)
{
    try {
        // We (LT) don't get called anywhere else once per frame.
        // XPMP API calls directly for aircraft positions.
        // So we need to figure out our way if we are called the first time of a cycle
        if ( cycle != currCycle.num )            // new cycle!
            NextCycle(cycle);
        
        // object invalid (due to exceptions most likely), don't use anymore, don't call LT functions
        if (!IsValid() ||
            dataRefs.IsReInitAll())
            return;

#ifdef DEBUG
        gSelAcCalc = fd.bIsSelected = bIsSelected = (key() == dataRefs.GetSelectedAcKey());
#endif
        
        
        // *** Position ***
        if (!CalcPPos())
            return;
        
        // If needed update the chosen CSL model
        if (ShallUpdateModel())
            ChangeModel();
        
        // Set Position
        SetLocation(ppos.lat(), ppos.lon(), ppos.alt_ft());
        drawInfo.pitch   = float(nanToZero(GetPitch()));
        drawInfo.roll    = float(nanToZero(GetRoll()));
        drawInfo.heading = float(nanToZero(GetHeading()));
        
        // *** Configuration ***
        
        SetGearRatio((float)gear.get());                // gear
        SetFlapRatio((float)flaps.get());               // flaps, and slats the same
        SetSlatRatio(GetFlapRatio());
        SetSpoilerRatio((float)spoilers.get());         // spoilers, and speed brakes the same
        SetSpeedbrakeRatio(GetSpoilerRatio());
        SetReversDeployRatio((float)reversers.get());   // opening reversers
        SetThrustReversRatio((float)reversers.get());

        // for engine / prop rotation we derive a value based on flight model
        if (GetFlightPhase() == FPH_PARKED)
            SetEngineRotRpm(0.0f);
        else if (pDoc8643->hasRotor())
            SetEngineRotRpm(float(pMdl->PROP_RPM_MAX));
        else
            SetEngineRotRpm(float(pMdl->PROP_RPM_MAX/2 + GetThrustRatio() * pMdl->PROP_RPM_MAX/2));
        SetPropRotRpm(GetEngineRotRpm());
        
        // Make props and rotors move based on rotation speed and time passed since last cycle
        SetEngineRotAngle(std::fmod(GetEngineRotAngle() + RpmToDegree(GetEngineRotRpm(), currCycle.diffTime),
                                    360.0f));
        SetPropRotAngle(GetEngineRotAngle());
        
        // Gear deflection - has an effect during touch-down only
        SetTireDeflection((float)gearDeflection.get());
        
        // Tire rotation similarly
        SetTireRotRpm((float)tireRpm.get());
        SetTireRotAngle(GetTireRotAngle() + RpmToDegree(GetTireRotRpm(), currCycle.diffTime));
        while (GetTireRotAngle() >= 360.0f)
            SetTireRotAngle(GetTireRotAngle() - 360.0f);

        // 'moment' of touch down?
        // (We use the reversers deploy time for this...that's 2s)
        SetTouchDown(reversers.isIncrease() && reversers.inMotion());
        
        // *** Radar ***
        
        // for radar 'calculation' we need some dynData
        // but radar doesn't change often...just only check every 100th cycle
        if ( currCycle.num % 100 <= 1 )
        {
            // fetch new data if available
            LTFlightData::FDDynamicData dynCopy;
            if ( fd.TryGetSafeCopy(dynCopy) )
            {
                // copy fresh radar data
                acRadar               = dynCopy.radar;
            }
        }
        
        // -> deactivate TCAS in some cases like parked or taxiing outside runway
        // (will be re-activated by the above code every 100th cycle)
        if (phase == FPH_PARKED)                        // off while parked
            acRadar.mode = xpmpTransponderMode_Off;
        // reduce to standby during taxiing if settings say so
        else if (phase == FPH_TAXI && dataRefs.IsAINotOnGnd() && !IsOnRwy())
            acRadar.mode = xpmpTransponderMode_Standby;
        // In case any of the S modes is being used, dynamically change the mode based on situation
        if (acRadar.mode >= xpmpTransponderMode_ModeS_Gnd) {
            if (IsOnGrnd()) {
                if (IsOnRwy())
                    acRadar.mode = xpmpTransponderMode_ModeS_TAOnly;
                else
                    acRadar.mode = xpmpTransponderMode_ModeS_Gnd;
            }
            else if (GetPHeight_ft() > 1000)
                acRadar.mode = xpmpTransponderMode_ModeS_TARA;
            else
                acRadar.mode = xpmpTransponderMode_ModeS_TAOnly;
        }

        // *** Informational Texts ***

        // Is there new data to send?
        if (ShallSendNewInfoData())
        {
            // fetch new data if available
            LTFlightData::FDStaticData statCopy;
            if (fd.TryGetSafeCopy(statCopy))
            {
                // copy data over to libxplanemp
                STRCPY_S     (acInfoTexts.tailNum,      statCopy.reg.c_str());
                STRCPY_S     (acInfoTexts.icaoAcType,   statCopy.acTypeIcao.c_str());
                STRCPY_ATMOST(acInfoTexts.manufacturer, statCopy.man.c_str());
                STRCPY_ATMOST(acInfoTexts.model,        statCopy.mdl.c_str());
                STRCPY_S     (acInfoTexts.icaoAirline,  statCopy.opIcao.c_str());
                STRCPY_ATMOST(acInfoTexts.airline,      statCopy.op.c_str());
                STRCPY_S     (acInfoTexts.flightNum,    statCopy.flight.c_str());
                STRCPY_S     (acInfoTexts.aptFrom,      statCopy.origin().c_str());
                STRCPY_S     (acInfoTexts.aptTo,        statCopy.dest().c_str());
            }
        }
        
        // Done, ie. in success case we return here
        return;
        
    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
    } catch (...) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, "(unknown)");
    }

    // for any kind of exception: don't use this object any more!
    SetInvalid();
}

// change the model (e.g. when model-defining static data changed)
void LTAircraft::ChangeModel ()
{
    // Try to fetch the static data
    LTFlightData::FDStaticData statData;
    if (!fd.TryGetSafeCopy(statData))
        return;
    
    // Save previous model name to identify an actual change
    const std::string oldIcaotype(acIcaoType);
    const std::string oldModelName(GetModelName());
    XPMP2::Aircraft::ChangeModel(str_first_non_empty({dataRefs.cslFixAcIcaoType, statData.acTypeIcao}),
                                 str_first_non_empty({dataRefs.cslFixOpIcao,     statData.airlineCode()}),
                                 str_first_non_empty({dataRefs.cslFixLivery,     statData.reg}));
    CalcLabelInternal(statData);

    // if there was an actual change inform the log
    if (oldModelName != GetModelName() ||
        oldIcaotype  != acIcaoType) {
        // also update the flight model to be used
        pMdl = &FlightModel::FindFlightModel(fd, true);
        pDoc8643 = &Doc8643::get(acIcaoType);
        bStaticObject = statData.isStaticObject();
        LOG_MSG(logINFO,INFO_AC_MDL_CHANGED,
                labelInternal.c_str(),
                statData.opIcao.c_str(),
                GetModelName().c_str(), pMdl->modelName.c_str());
    }
    
    // reset the flag that we needed to change the model
    bChangeModel = false;
}

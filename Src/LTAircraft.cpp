/// @file       LTAircraft.cpp
/// @brief      LTAircraft represents an individual tracked aircraft drawn into X-Plane's sky
/// @details    Implements helper classes MovingParam, AccelParam for flght parameters
///             that change in a controlled way (like flaps, roll, speed).\n
///             LTAircraft::FlightModel provides configuration values controlling flight modelling.\n
///             LTAircraft calculates the current position and configuration of the aircraft
///             in every flighloop cycle while being called from libxplanemp.
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

//
//MARK: Globals
//

// previous and current cycle info
struct cycleInfo {
    int     num;                // cycle numer (which is kind of an id)
    float   elapsedTime;        // X-Plane's elapsed time
    double  simTime;            // simulated time when the cycle started
    double  diffTime;           // the simulated time difference to previous cycle
};

cycleInfo prevCycle = { -1, -1, -1, 0 };
cycleInfo currCycle = { -1, -1, -1, 0 };

/// Position of user's plane, updated irregularly but often enough
positionTy posUsersPlane;

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
        prevCycle.elapsedTime = XPLMGetElapsedTime() - 0.1f;
        prevCycle.simTime  = dataRefs.GetSimTime() - 0.1;
    }
    currCycle.num = newCycle;
    currCycle.elapsedTime = XPLMGetElapsedTime();
    currCycle.simTime  = dataRefs.GetSimTime();
    
    // the time that has passed since the last cycle
    currCycle.diffTime  = currCycle.simTime - prevCycle.simTime;
    
    // tell multiplayer lib if we want to see labels
    // (these are very quick calls only setting a variable)
    // as the user can change between views any frame
    // Tell XPMP if we need labels
    if (dataRefs.ShallDrawLabels())
        XPMPEnableAircraftLabels();
    else
        XPMPDisableAircraftLabels();
    
    // time should move forward (positive difference) and not too much either
    // If time moved to far between two calls then we better start over
    // (but no problem if no a/c yet displayed anyway)
    if (dataRefs.GetNumAc() > 0 &&
        (currCycle.diffTime < 0 || currCycle.diffTime > dataRefs.GetFdBufPeriod()) ) {
        // too much time passed...we start over and reinit all aircraft
        dataRefs.SetReInitAll(true);
        SHOW_MSG(logWARN, ERR_TIME_NONLINEAR, currCycle.diffTime);
        return false;
    }
    
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
inline double RpmToDegree (double rpm, double s)
{
    return
    // revolutions per second
    rpm/60.0
    // multiplied by seconds gives revolutions
    * s
    // multiplied by 360 degrees per revolution gives degrees
    * 360.0;
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
    LOG_ASSERT(defMin <= _val && _val <= defMax);
    val = _val;                     // just set the target value, no moving
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
    LOG_ASSERT((defMin <= tval) && (tval <= defMax));
    
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
        // timeTo = fabs(valDist/defDist) * defDuration + timeFrom;
        timeFrom = std::isnan(_startTS) ? currCycle.simTime : _startTS;
        timeTo = fma(fabs(valDist/defDist), defDuration, timeFrom);
    }
}

// pre-program a move, which is to finish by the given time
void MovingParam::moveToBy (double _from, bool _increase, double _to,
                            double _startTS, double _by_ts,
                            bool _startEarly)
{
    LOG_ASSERT((defMin <= _to) && (_to <= defMax));

    // current value equals target already
    if (dequal(_to, val)) {
        SetVal(_to);                // just set the target value bit-euqal, no moving
    }
    // we shall move to a (new) given target:
    // calc required duration by using defining parameters
    else if ( !dequal(valTo, _to) ) {
        // default values
        if (std::isnan(_from))       _from = val;
        if (std::isnan(_startTS))    _startTS = currCycle.simTime;
        LOG_ASSERT((defMin <= _from) && (_from <= defMax));

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
        double timeDist = fabs(valDist/defDist * defDuration);
        
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
    if ( !bWrapAround || fabs(_to-_from) <= defDist/2 ) {       // direct way is the only possible (no wrap-around) or it is the shorter way
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
    LOG_ASSERT(_accel > 0 ? _targetSpeed > _startSpeed : _targetSpeed < _startSpeed);
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
                                   double _startTime, double _targetTime)
{
    LOG_ASSERT(_targetTime > currCycle.simTime);
    LOG_ASSERT(_startTime < _targetTime);
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
    LOG_ASSERT(tx <= _targetTime);
    SetSpeed(_startSpeed);              // reset
    startSpeed = _startSpeed;
    targetSpeed = _targetSpeed;
    acceleration = accel;
    targetDeltaDist = _deltaDist;
    startTime = _startTime;
    accelStartTime = std::max(tx, _startTime);
    targetTime = _targetTime;
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
//MARK: LTAircraft::FlightModel
//

// list of flight models as read from FlightModel.prf file
std::list<LTAircraft::FlightModel> listFlightModels;

// ordered list of matches (regex|model), read from FlightModel.prf, section [Map]
typedef std::pair<std::regex,const LTAircraft::FlightModel&> regexFM;
std::list<regexFM> listFMRegex;

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
    else FM_ASSIGN(AGL_GEAR_DOWN);
    else FM_ASSIGN(AGL_GEAR_UP);
    else FM_ASSIGN(AGL_FLARE);
    else FM_ASSIGN(MAX_TAXI_SPEED);
    else FM_ASSIGN(MIN_REVERS_SPEED);
    else FM_ASSIGN(TAXI_TURN_TIME);
    else FM_ASSIGN(FLIGHT_TURN_TIME);
    else FM_ASSIGN_MIN(ROLL_MAX_BANK,1.0);  // avoid zero - this is a moving parameter
    else FM_ASSIGN_MIN(ROLL_RATE, 1.0);     // avoid zero - this becomes a divisor
    else FM_ASSIGN(FLAPS_UP_SPEED);
    else FM_ASSIGN(FLAPS_DOWN_SPEED);
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
    else if (name == "LIGHT_PATTERN") {
        if ((int)val < 0 || (int)val > 2) {
            LOG_MSG(logWARN, ERR_CFG_VAL_INVALID, fileName, ln, text.c_str());
            return false;
        }
        fm.LIGHT_PATTERN = (int)val;
    }
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
    // state signifies what kind of section we are currently reading
    enum fmFileStateTy { FM_NO_SECTION, FM_MODEL_SECTION, FM_MAP } fmState = FM_NO_SECTION;
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
            // (as [Map] has to be last this will safely be executed for all FlightModel sections)
            if (fm && fmState == FM_MODEL_SECTION) {
                // i.e. add the defined model to the list
                push_back_unique(listFlightModels, fm);
            }
            else if (fmState == FM_MAP) {
                // there should be no section after [Map],
                // ignore remainder of file
                LOG_MSG(logWARN, ERR_CFG_FORMAT, sFileName.c_str(), ln,
                        ERR_FM_NOT_AFTER_MAP);
                errCnt++;
                break;
            }

            // identify map section?
            if (text == FM_MAP_SECTION) {
                fmState = FM_MAP;
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

// based on an aircraft type find a matching flight model
const LTAircraft::FlightModel& LTAircraft::FlightModel::FindFlightModel
        (const std::string acTypeIcao)
{
    // 1. find aircraft type specification in the Doc8643
    const Doc8643& acType = Doc8643::get(acTypeIcao);
    const std::string acSpec (acType);      // the string to match
    
    // 2. walk through the Flight Model map list and try each regEx pattern
    for (auto mapIt: listFMRegex) {
        std::smatch m;
        std::regex_search(acSpec, m, mapIt.first);
        if (m.size() > 0)                   // matches?
            return mapIt.second;            // return that flight model
    }
    
    // no match: return default
    LOG_MSG(logWARN, ERR_FM_NOT_FOUND,
            acTypeIcao.c_str(), acSpec.c_str());
    return MDL_DEFAULT;
}

// return a ptr to a flight model based on its model or [section] name
// returns nullptr if not found
const LTAircraft::FlightModel* LTAircraft::FlightModel::GetFlightModel
        (const std::string modelName)
{
    // search through list of flight models, match by modelName
    auto fmIt = std::find_if(listFlightModels.cbegin(),
                             listFlightModels.cend(),
                             [&modelName](const LTAircraft::FlightModel& fm)
                             { return fm.modelName == modelName; });
    return fmIt == listFlightModels.cend() ? nullptr : &*fmIt;
}

//
//MARK: LTAircraft::FlightPhase
//

std::string LTAircraft::FlightPhase2String (FlightPhase phase)
{
    switch (phase) {
        case FPH_UNKNOWN:           return "Unknown";
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

// Constructor: create an aircraft from Flight Data
LTAircraft::LTAircraft(LTFlightData& inFd) :
// Base class -> this registers with XPMP API for actual display in XP!
// repeated calls to WaitForSafeCopyStat look inefficient, but if the lock is held by the calling function already then these are quick recursive calls
// Using registration as livery indicator, allows for different liveries per actual airframe
// Debug options to set fixed type/op/livery take precedence
XPCAircraft(str_first_non_empty({dataRefs.cslFixAcIcaoType, inFd.WaitForSafeCopyStat().acTypeIcao}).c_str(),
            str_first_non_empty({dataRefs.cslFixOpIcao,     inFd.WaitForSafeCopyStat().airlineCode()}).c_str(),
            str_first_non_empty({dataRefs.cslFixLivery,     inFd.WaitForSafeCopyStat().reg}).c_str()),
// class members
fd(inFd),
mdl(FlightModel::FindFlightModel(inFd.WaitForSafeCopyStat().acTypeIcao)),   // find matching flight model
doc8643(Doc8643::get(inFd.WaitForSafeCopyStat().acTypeIcao)),
szLabelAc{ 0 },
tsLastCalcRequested(0),
phase(FPH_UNKNOWN),
rotateTs(NAN),
vsi(0.0),
bOnGrnd(false), bArtificalPos(false), bNeedNextVec(false),
gear(mdl.GEAR_DURATION),
flaps(mdl.FLAPS_DURATION),
heading(mdl.TAXI_TURN_TIME, 360, 0, true),
roll(2*mdl.ROLL_MAX_BANK / mdl.ROLL_RATE, mdl.ROLL_MAX_BANK, -mdl.ROLL_MAX_BANK, false),
pitch((mdl.PITCH_MAX-mdl.PITCH_MIN)/mdl.PITCH_RATE, mdl.PITCH_MAX, mdl.PITCH_MIN),
reversers(MDL_REVERSERS_TIME),
spoilers(MDL_SPOILERS_TIME),
tireRpm(MDL_TIRE_SLOW_TIME, MDL_TIRE_MAX_RPM),
gearDeflection(MDL_GEAR_DEFL_TIME, mdl.GEAR_DEFLECTION),
probeRef(NULL), probeNextTs(0), terrainAlt(0),
bValid(true)
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
        
        // positional data / radar: just copy from fd for a start
        radar = dynCopy.radar;

        // standard label
        LabelUpdate();

        // standard internal label (e.g. for logging) is transpIcao + ac type + another id if available
        CalcLabelInternal(statCopy);
        
        // init surfaces
        surfaces =
        {
            0,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            { 0 },
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false
        };
        surfaces.size = sizeof(surfaces);
        
        // init moving params where necessary
        pitch.SetVal(0);
        roll.SetVal(0);
        
        // calculate our first position, must also succeed
        if (!CalcPPos())
            LOG_MSG(logERR,ERR_AC_CALC_PPOS,fd.key().c_str());
        
        // if we start on the ground then have the gear out already
        if (IsOnGrnd())
            gear.SetVal(gear.defMax);
        
        // tell the world we've added something
        dataRefs.IncNumAc();
        LOG_MSG(logINFO,INFO_AC_ADDED,
                labelInternal.c_str(),
                statCopy.opIcao.c_str(),
                GetModelName().c_str(),
                mdl.modelName.c_str(),
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
    
    // Release probe handle
    if (probeRef)
        XPLMDestroyProbe(probeRef);
    
    // Decrease number of visible aircraft and log a message about that fact
    dataRefs.DecNumAc();
    LOG_MSG(logINFO,INFO_AC_REMOVED,labelInternal.c_str());
}

void LTAircraft::CalcLabelInternal (const LTFlightData::FDStaticData& statDat)
{
    std::string s (statDat.acId(""));
    labelInternal = key() + " (" + statDat.acTypeIcao;
    if (!s.empty()) {
        labelInternal += ' ';
        labelInternal += s;
    }
    labelInternal += ')';
}


// MARK: LTAircraft stringify for debugging output purposes
LTAircraft::operator std::string() const
{
    char buf[500];
    snprintf(buf,sizeof(buf),"a/c %s ppos:\n%s Y: %.0ff %.0fkn %.0fft/m Phase: %02d %s\nposList:\n",
             labelInternal.c_str(),
             ppos.dbgTxt().c_str(), terrainAlt,
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
    strcpy_s(
        szLabelAc,
        sizeof(szLabelAc),
        strAtMost(fd.ComposeLabel(), sizeof(szLabelAc) - 1).c_str());
}

//
// MARK: LTAircraft Calculate present position
//

// position heading to (usually posList[1], but ppos if ppos > posList[1])
const positionTy& LTAircraft::GetToPos() const
{
    if ( posList.size() >= 2 )
        return ppos < posList[1] ? posList[1] : ppos;
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
    (currCycle.simTime >= std::prev(posList.cend())->ts());
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

    // 1s before reaching last know position we trigger pos calculation (max every 1,0s)
    const positionTy& lastPos = posList.back();
    if ((lastPos.ts() <= currCycle.simTime + 2*TIME_REQU_POS) &&
        (tsLastCalcRequested + 2*TIME_REQU_POS <= currCycle.simTime))
    {
        fd.TriggerCalcNewPos(std::max(currCycle.simTime,lastPos.ts()));
        tsLastCalcRequested=currCycle.simTime;
    }

    // 0,5s before reaching last known position we try adding new positions
    if ( lastPos.ts() <= currCycle.simTime + TIME_REQU_POS ) {
        if ( fd.TryFetchNewPos(posList, rotateTs) == LTFlightData::TRY_SUCCESS) {
            // we got new position(s)!
            bArtificalPos = false;
        }
    }
    
    // Finally: Time to switch to next position?
    // (Must have reach/passed posList[1] and there must be a third position,
    //  which can now serve as 'to')
    while ( posList[1].ts() <= currCycle.simTime && posList.size() >= 3 ) {
        // By just removing the first element (current 'from') from the deqeue
        // we make posList[2] the next 'to'
        posList.pop_front();
        // Now: To absolutely ensure we continue seamlessly from current
        // ppos we set posList[0] ('from') to ppos. Should be close anyway in normal
        // situations. (It's not if the simulation was halted while feeding live
        // data, then posList got completely outdated and ppos might jump beyond the entire list.)
        if ( ppos < posList[1])
            posList[0] = ppos;
        // flag: switched positions
        bPosSwitch = true;
    }

    // *** fixed to/from positions ***
    LOG_ASSERT_FD(fd, posList.size() >= 2);
    
    // we are now certain to have at least 2 position and we are flying
    // from the first to the second
    positionTy& from  = posList[0];
    positionTy& to    = posList[1];
    double duration = to.ts() - from.ts();
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
        ppos.onGrnd = from.onGrnd;
        
        // *** heading: make sure it is less than 360 (well...just normaize the entire positions)
        from.normalize();
        to.normalize();
        
        // start the turn from the initial heading to the vector heading
        heading.defDuration = IsOnGrnd() ? mdl.TAXI_TURN_TIME : mdl.FLIGHT_TURN_TIME;
        heading.moveQuickestToBy(NAN,
                                 vec.dist > SIMILAR_POS_DIST ?  // if vector long enough:
                                 vec.angle :                    // turn to vector heading
                                 HeadingAvg(from.heading(),to.heading()),   // otherwise only turn to avg between from- and target-heading
                                 NAN, (from.ts()+to.ts())/2,    // by half the vector flight time
                                 true);                         // start immediately
        
        // *** roll ***
        // roll: we should currently have a bank angle, which should be
        //       returned back to level flight by the time the turn ends
        if (!IsOnGrnd() && phase != FPH_FLARE && heading.isProgrammed())
            roll.moveQuickestToBy(NAN, 0.0,
                                  NAN, heading.toTS(), false);
        else
            // on the ground or no heading change: keep wings level
            roll.moveTo(0.0);
        
        // *** Pitch ***
        
        // Fairly simplistic...in the range -2 to +18 depending linearly on vsi only
        double toPitch =
        (from.IsOnGnd() && to.IsOnGnd()) ? 0 :
        vsi < mdl.PITCH_MIN_VSI ? mdl.PITCH_MIN :
        vsi > mdl.PITCH_MAX_VSI ? mdl.PITCH_MAX :
        (mdl.PITCH_MIN +
         (vsi-mdl.PITCH_MIN_VSI)/(mdl.PITCH_MAX_VSI-mdl.PITCH_MIN_VSI)*
         (mdl.PITCH_MAX-mdl.PITCH_MIN));
        
        // another approach for climbing phases:
        // a/c usually flies more or less with nose pointing upward toward climb,
        // nearly no drag, so try calculating the flight path angle and use
        // it also as pitch
        if ( vsi > mdl.VSI_STABLE ) {
            toPitch = vsi2deg(vec.speed, vec.vsi);
            if (toPitch < mdl.PITCH_MIN)
                toPitch = mdl.PITCH_MIN;
            if (toPitch > mdl.PITCH_MAX)
                toPitch = mdl.PITCH_MAX;
        }
        
        // add some degrees in case flaps will be set
        if (!bOnGrnd && vec.speed_kn() < std::max(mdl.FLAPS_DOWN_SPEED,mdl.FLAPS_UP_SPEED)) {
            toPitch += mdl.PITCH_FLAP_ADD;
            if (toPitch > mdl.PITCH_MAX)
                toPitch = mdl.PITCH_MAX;
        }
        
        // set destination pitch and move there in a controlled way
        to.pitch() = toPitch;
        if (phase != FPH_ROTATE)            // while rotating don't interfere
            pitch.moveQuickestToBy(NAN, toPitch, NAN, to.ts(), true);
        
        // *** speed/acceleration/decelaration
        
        // Only skip this in case of short final, i.e. if currently off ground
        // but next pos is on the ground, because in that case the
        // next vector is the vector for roll-out on the ground
        // with significantly reduced speed due to breaking, that speed
        // is undesirable at touch-down; for short final we assume constant speed
        bNeedNextVec = false;
        if (!from.IsOnGnd() && to.IsOnGnd()) {
            // short final, i.e. last vector before touch down
            
            // keep constant speed:
            // avg ground speed between the two points [kt]
            speed.SetSpeed(vec.speed);
        } else {
            // will be calculated soon outside "if (bPosSwitch)"
            // (need provision for case we don't get the data access lock right now)
            bNeedNextVec = true;
        }
        
        // output debug info on request
        if (dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_AC_SWITCH_POS,std::string(*this).c_str());
        }
    }
    
    // *** acceleration / decelartion ***
    
    // Need next vector for speed determination?
    if ( bNeedNextVec ) {
        double toSpeed = NAN;
        vectorTy nextVec;
        
        // makes only sense if 'to' is still in the future
        // (there seem to be case when this is not the case, and if only because the user pauses or changes time)
        if (to.ts() > currCycle.simTime) {
            switch ( fd.TryGetVec(to.ts()+1, nextVec) ) {
                case LTFlightData::TRY_SUCCESS:
                    // got the vector!
                    // Target speed: Weighted average of current and next vector
                    toSpeed =
                    (vec.speed * nextVec.dist + nextVec.speed * vec.dist) /
                    (vec.dist + nextVec.dist);
                    break;
                case LTFlightData::TRY_NO_LOCK:
                    // try again next frame (bNeedNextVec stays true)
                    break;
                case LTFlightData::TRY_NO_DATA:
                    // No data...but if we are running artifical roll-out then
                    // we just decelerate to stop
                    if (bArtificalPos && phase >= FPH_TOUCH_DOWN) {
                        toSpeed = 0;
                        break;
                    }
                    // else (not artifical roll-out) fall-through
                    [[fallthrough]];
                case LTFlightData::TRY_TECH_ERROR:
                    // no data or errors...well...then we just fly constant speed
                    bNeedNextVec = false;
                    break;
            }
        } else {
            // 'to' is no longer future...then we just fly constant speed
            bNeedNextVec = false;
        }
        
        // if we did come up with a target speed then start speed control
        if (!std::isnan(toSpeed)) {
            // initiate speed control
            speed.StartSpeedControl(speed.m_s(),
                                    toSpeed,
                                    vec.dist,
                                    from.ts(), to.ts());
            // don't need to calc speed again
            bNeedNextVec = false;
        }
        // else if no next attempt: just fly constant avg speed then
        else if (!bNeedNextVec) {
            speed.SetSpeed(vec.speed);
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
        speed.m_s() > 0.0 &&
        !bArtificalPos)
    {
        // init deceleration down to zero
        speed.StartAccel(speed.m_s(),
                         0,
                         mdl.ROLL_OUT_DECEL);
        
        // the vector to the stopping point
        vectorTy vecStop(ppos.heading(),            // keep current heading
                         speed.getTargetDeltaDist());// distance needed to stop
        
        // add ppos and the stop point (ppos + above vector) to the list of positions
        // (they will be activated with the next frame only)
        posList.emplace_back(ppos);
        positionTy& stopPoint = posList.emplace_back(ppos.destPos(vecStop));
        stopPoint.ts() = speed.getTargetTime();
        bArtificalPos = true;                   // flag: we are working with an artifical position now
        if (dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_INVENTED_STOP_POS,stopPoint.dbgTxt().c_str());
        }
    }
    
    // Now we apply the factor so that with time we move from 'from' to 'to'.
    // Note that this calculation also works if we passed 'to' already
    // (due to no newer 'to' available): we just keep going the same way.
    // Here now valarray comes in handy as we can write the calculation
    // with simple vector notation:
    ppos.v = from.v * (1-f) + to.v * f;
    // (this also computes values for heading, pitch, roll, which is a historic
    //  relict. We later decided to use MovingParam for those values.)
    
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
    
    // *** Attitude ***/

    // half-way through prepare turning to end heading
    if ( f > 0.5 && f < 1.0 && !dequal(heading.toVal(), to.heading()) ) {
        heading.defDuration = IsOnGrnd() ? mdl.TAXI_TURN_TIME : mdl.FLIGHT_TURN_TIME;
        heading.moveQuickestToBy(NAN, to.heading(), // target heading
                                 NAN, to.ts(),      // by target timestamp
                                 false);            // start as late as possible
        // roll: start to roll when turn starts
        if (!IsOnGrnd() && phase != FPH_FLARE && heading.isProgrammed())
            roll.moveTo(heading.isIncrease() ? roll.defMax : roll.defMin,
                        heading.fromTS());
    }

    // if we ran out of positions we might have passed the final to-pos with a bank angle
    // return that bank angle to 0
    if (f > 1.0)
        roll.moveTo(0.0);

    // current heading
    ppos.heading() = heading.get();
    // current pitch
    ppos.pitch() = pitch.get();
    // current roll (bank angle)
    ppos.roll() = roll.get();
    
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
        ppos.SetAltFt(terrainAlt);
        vsi = 0;
        // but tires are rotating
        tireRpm.SetVal(std::min(TireRpm(GetSpeed_kt()),
                                tireRpm.defMax));
    } else {
        // not on the ground
        // just lifted off? then recalc vsi
        if (phase == FPH_LIFT_OFF && dequal(vsi, 0)) {
            vsi = ppos.vsi_ft(to);
        }
    }
    
    // are we visible?
    CalcVisible();
    
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
    FlightPhase bFPhPrev = phase;
    
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
        ppos.onGrnd = bOnGrnd ? positionTy::GND_ON : positionTy::GND_OFF;
    }
    
    // Vertical Direction
    enum { V_Sinking=-1, V_Stable=0, V_Climbing=1 } VertDir = V_Stable;
    if ( vsi < -mdl.VSI_STABLE ) VertDir = V_Sinking;
    else if ( vsi > mdl.VSI_STABLE ) VertDir = V_Climbing;
    
    // if we _are_ on the ground then height is zero and there's no VSI
    if (bOnGrnd) {
        PHeight = 0;
        VertDir = V_Stable;
    }
    
    // *** decide the flight phase ***
    
    // on the ground with low speed
    if ( bOnGrnd && speed.kt() <= mdl.MAX_TAXI_SPEED )
    {
        // if not artifically reducing speed (roll-out)
        if (!bArtificalPos)
            phase = FPH_TAXI;
        // so we are rolling out artifically...have we stopped?
        else if (speed.isZero())
            phase = FPH_STOPPED_ON_RWY;
    }
    
    // on the ground with high speed
    if ( bOnGrnd && speed.kt() > mdl.MAX_TAXI_SPEED ) {
        if ( bFPhPrev <= FPH_LIFT_OFF )
            phase = FPH_TO_ROLL;
        else
            phase = FPH_ROLL_OUT;
    }
    
    // Determine FPH_ROTATE by use of rotate timestamp
    // (set in LTFlightData::CalcNextPos)
    if ( phase < FPH_ROTATE &&
         rotateTs <= currCycle.simTime && currCycle.simTime <= rotateTs + 2 * mdl.ROTATE_TIME ) {
        phase = FPH_ROTATE;
    }

    // last frame: on ground, this frame: not on ground -> we just lifted off
    if ( bOnGrndPrev && !bOnGrnd && bFPhPrev != FPH_UNKNOWN ) {
        phase = FPH_LIFT_OFF;
    }
    
    // climbing but not even reached gear-up altitude
    if (VertDir == V_Climbing &&
        PHeight < mdl.AGL_GEAR_UP) {
        phase = FPH_LIFT_OFF;
    }
    
    // climbing through gear-up altitude
    if (VertDir == V_Climbing &&
        PHeight >= mdl.AGL_GEAR_UP) {
        phase = FPH_INITIAL_CLIMB;
    }
    
    // climbing through flaps toggle speed
    if (VertDir == V_Climbing &&
        PHeight >= mdl.AGL_GEAR_UP &&
        speed.kt() >= mdl.FLAPS_UP_SPEED) {
        phase = FPH_CLIMB;
    }
    
    // cruise when leveling off with a certain height
    // (this means that if leveling off below cruise alt while e.g. in CLIMB phase
    //  we keep CLIMB phase)
    if (VertDir == V_Stable &&
        PHeight >= mdl.CRUISE_HEIGHT) {
        phase = FPH_CRUISE;
    }
    
    // sinking, but still above flaps toggle height
    if (VertDir == V_Sinking &&
        speed.kt() > mdl.FLAPS_DOWN_SPEED) {
        phase = FPH_DESCEND;
    }
    
    // sinking through flaps toggle speed
    if (VertDir == V_Sinking &&
        speed.kt() <= mdl.FLAPS_DOWN_SPEED) {
        phase = FPH_APPROACH;
    }
    
    // sinking through gear-down height
    if (VertDir == V_Sinking &&
        speed.kt() <= mdl.FLAPS_DOWN_SPEED &&
        PHeight <= mdl.AGL_GEAR_DOWN) {
        phase = FPH_FINAL;
    }
    
    // sinking through flare height
    if (VertDir == V_Sinking &&
        speed.kt() <= mdl.FLAPS_DOWN_SPEED &&
        PHeight <= mdl.AGL_FLARE) {
        phase = FPH_FLARE;
    }
    
    // last frame: not on ground, this frame: on ground -> we just touched down
    if ( !bOnGrndPrev && bOnGrnd && bFPhPrev != FPH_UNKNOWN ) {
        phase = FPH_TOUCH_DOWN;
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
    
    // Phase Taxi *** Model Init ***
    if (ENTERED(FPH_TAXI)) {
        // this will only be executed when coming from FPH_UNKOWN,
        // i.e during the first call to the function per a/c object
        // -> can be used for flight model initialization
        // some assumption to begin with...
        surfaces.thrust            = 0.1f;
        surfaces.lights.timeOffset = (unsigned int)rand();
        surfaces.lights.landLights = dataRefs.GetLndLightsTaxi() ? 1 : 0;
        surfaces.lights.taxiLights = 1;
        surfaces.lights.bcnLights  = 1;
        surfaces.lights.strbLights = 0;
        surfaces.lights.navLights  = 1;
        surfaces.lights.flashPattern = mdl.LIGHT_PATTERN;
        
        gear.down();
        flaps.up();
    }
    
    // Phase Take Off
    if (ENTERED(FPH_TAKE_OFF)) {
        surfaces.lights.strbLights = 1;
        surfaces.lights.landLights = 1;
        surfaces.thrust = 1.0;          // a bit late...but anyway ;)
        flaps.half();
    }
    
    // Rotating
    if (ENTERED(FPH_ROTATE)) {
        // (as we don't do any counter-measure in the next ENTERED-statements
        //  we can lift the nose only if we are exatly AT rotate phase)
        if (phase == FPH_ROTATE)
            pitch.max();
    }
    
    // Lift off
    if (ENTERED(FPH_LIFT_OFF)) {
        // Tires are rotating but shall stop in max 5s
        if (gear.isDown())
            tireRpm.min();                              // "move" to 0
    }
    
    // entered Initial Climb
    if (ENTERED(FPH_INITIAL_CLIMB)) {
        gear.up();
        rotateTs = NAN;             // 'eat' the rotate timestamp, so we don't rotate any longer
    }
    
    // entered climb (from below) or climbing (catches go-around)
    if (ENTERED(FPH_CLIMB) || phase == FPH_CLIMB) {
        surfaces.lights.taxiLights = 0;
        surfaces.thrust = 0.8f;
        gear.up();
        flaps.up();
    }
    
    // cruise
    if (ENTERED(FPH_CRUISE)) {
        surfaces.thrust = 0.6f;
        flaps.up();
    }

    // descend
    if (ENTERED(FPH_DESCEND)) {
        surfaces.thrust = 0.1f;
        flaps.up();
    }
    
    // approach
    if (ENTERED(FPH_APPROACH)) {
        surfaces.thrust = 0.2f;
        flaps.half();
    }
    
    // final
    if (ENTERED(FPH_FINAL)) {
        surfaces.lights.taxiLights = 1;
        surfaces.lights.landLights = 1;
        surfaces.thrust = 0.3f;
        flaps.down();
        gear.down();
    }
    
    // flare
    if (ENTERED(FPH_FLARE)) {
        pitch.moveTo(mdl.PITCH_FLARE);      // flare!
        roll.moveTo(0);
    }
    
    // touch-down
    if (ENTERED(FPH_TOUCH_DOWN)) {
        gearDeflection.max();           // start main gear deflection
        spoilers.max();                 // start deploying spoilers
        ppos.onGrnd = positionTy::GND_ON;
        pitch.moveTo(0);
    }
    
    // roll-out
    if (ENTERED(FPH_ROLL_OUT)) {
        surfaces.thrust = -0.9f;         // reversers
        reversers.max();                 // start opening reversers
    }
    
    // if deflected all the way down: start returning to normal
    if (gearDeflection.isDown())
        gearDeflection.min();

    if (phase >= FPH_ROLL_OUT || phase == FPH_TAXI) {
        // stop reversers below 80kn
        if (GetSpeed_kt() < mdl.MIN_REVERS_SPEED) {
            surfaces.thrust = 0.1f;
            reversers.min();
        }
    }
    
    // *** landing light ***
    // is there a landing-light-altitude in the flight model?
    if (mdl.LIGHT_LL_ALT > 0) {
        // OK to turn OFF?
        if (ppos.alt_ft() > mdl.LIGHT_LL_ALT) {
            if ((phase < FPH_TAKE_OFF) || (FPH_CLIMB <= phase && phase < FPH_FINAL))
                surfaces.lights.landLights = 0;
        } else {
            // need to turn/stay on below LIGHT_LL_ALT
            if (phase >= FPH_TAKE_OFF)
                surfaces.lights.landLights = 1;
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
        surfaces.thrust = 0.1f;
        reversers.min();
        surfaces.lights.taxiLights = 1;
        surfaces.lights.landLights = dataRefs.GetLndLightsTaxi() ? 1 : 0;
        surfaces.lights.strbLights = 0;
    }
    
    // *** Log ***
    
    // if requested log a phase change
    if ( bFPhPrev != phase && dataRefs.GetDebugAcPos(key()) )
        LOG_MSG(logDEBUG,DBG_AC_FLIGHT_PHASE,
                int(bFPhPrev),FlightPhase2String(bFPhPrev).c_str(),
                int(phase),FlightPhase2String(phase).c_str()
                );
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
    terrainAlt = YProbe_at_m(ppos, probeRef) / M_per_FT;
    
    if (currCycle.simTime >= probeNextTs)
    {
        // lastly determine when to do a probe next, more often if closer to the ground
        static_assert(sizeof(PROBE_HEIGHT_LIM) == sizeof(PROBE_DELAY));
        for ( size_t i=0; i < sizeof(PROBE_HEIGHT_LIM)/sizeof(PROBE_HEIGHT_LIM[0]); i++)
        {
            if ( ppos.alt_ft() - terrainAlt >= PROBE_HEIGHT_LIM[i] ) {
                probeNextTs = currCycle.simTime + PROBE_DELAY[i];
                break;
            }
        }
        LOG_ASSERT_FD(fd,probeNextTs > currCycle.simTime);
        
        // *** unrelated to YProbe...just makes use of the "calc every so often" mechanism
        
        // calc current bearing and distance for pure informational purpose ***
        vecView = positionTy(dataRefs.GetViewPos()).between(ppos);
        // update AI slotting priority
        CalcAIPrio();
        // update the a/c label with fresh values
        LabelUpdate();
    }
    
    // Success
    return true;
}

// return a string indicating the use of nav/beacon/strobe/landing lights
std::string LTAircraft::GetLightsStr() const
{
    char buf[30];
    snprintf(buf, sizeof(buf), "%s/%s/%s/%s/%s",
            surfaces.lights.navLights ? "nav" : "---",
            surfaces.lights.bcnLights ? "bcn" : "---",
            surfaces.lights.strbLights ? "strb" : "----",
            surfaces.lights.taxiLights ? "taxi" : "----",
            surfaces.lights.landLights ? "land" : "----"
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
    pOut->bits.taxi = surfaces.lights.taxiLights;
    pOut->bits.land = surfaces.lights.landLights;
    pOut->bits.bcn  = surfaces.lights.bcnLights;
    pOut->bits.strb = surfaces.lights.strbLights;
    pOut->bits.nav  = surfaces.lights.navLights;
    pOut->bits.filler1 = 0;
    pOut->bits.multiIdx = multiIdx;
    pOut->bits.filler2 = 0;
    pOut->bits.filler3 = 0;
    
    // v1.22 additions
    if (size >= LTAPIBulkData_v122) {
        pOut->lat = GetPPos().lat();
        pOut->lon = GetPPos().lon();
        pOut->alt_ft = GetPPos().alt_ft();
    }
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
    STRCPY_ATMOST(pOut->acClass,        doc8643.classification);
    STRCPY_ATMOST(pOut->wtc,            doc8643.wtc);
    STRCPY_ATMOST(pOut->opIcao,         stat.opIcao);
    STRCPY_ATMOST(pOut->man,            stat.man);
    STRCPY_ATMOST(pOut->model,          stat.mdl);
    STRCPY_ATMOST(pOut->catDescr,       stat.catDescr);
    STRCPY_ATMOST(pOut->op,             stat.op);
    // flight data
    STRCPY_ATMOST(pOut->callSign,       stat.call);
    STRCPY_ATMOST(pOut->squawk,         dyn.GetSquawk());
    STRCPY_ATMOST(pOut->flightNumber,   stat.flight);
    STRCPY_ATMOST(pOut->origin,         stat.originAp);
    STRCPY_ATMOST(pOut->destination,    stat.destAp);
    STRCPY_ATMOST(pOut->trackedBy,      dyn.pChannel ? dyn.pChannel->ChName() : "-");
    
    // v1.22 additions
    if (size >= LTAPIBulkInfoTexts_v122) {
        STRCPY_ATMOST(pOut->cslModel,   GetModelName());
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
    if (b != bVisible)                  // is new visibility a change?
    {
        bVisible = b;
        LOG_MSG(logINFO, bVisible ? INFO_AC_SHOWN : INFO_AC_HIDDEN,
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
    bool bPrevVisible = bVisible;
    
    // automatic is off -> take over manually given state
    if (!dataRefs.IsAutoHidingActive() || !bAutoVisible)
        bVisible = bSetVisible;
    // hide while taxiing...and we are taxiing?
    else if (dataRefs.GetHideTaxiing() &&
        (phase == FPH_TAXI || phase == FPH_STOPPED_ON_RWY))
        bVisible = false;
    // hide below certain height...and we are below that?
    else if (dataRefs.GetHideBelowAGL() > 0 &&
             GetPHeight_ft() < dataRefs.GetHideBelowAGL())
        bVisible = false;
    else
        // otherwise we are visible
        bVisible = true;
    
    // inform about a change
    if (bPrevVisible != bVisible)
        LOG_MSG(logINFO, bVisible ? INFO_AC_SHOWN_AUTO : INFO_AC_HIDDEN_AUTO,
                labelInternal.c_str());

    // return new visibility
    return bVisible;
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
    double userSpeed, userTrack;
    positionTy posUser = dataRefs.GetUsersPlanePos(userSpeed, userTrack);
    if (posUser.IsOnGnd())              // if on the ground
        userTrack = posUser.heading();      // heading is more reliable
    const double bearing = posUser.angle(ppos);
    const double diff = abs(HeadingDiff(userTrack, bearing));
    
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
        CalcCameraViewPos();                        // calc first position
        
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
    else if (pExtViewAc == this) {      // me again? -> switch off
        pExtViewAc = nullptr;
        CameraRegisterCommands(false);
        XPLMDontControlCamera();
        
        // if a previous view is known we make sure we go back there
        if (prevView) {
            dataRefs.SetViewType(prevView);
            prevView = VIEW_UNKNOWN;
        }
    }
    else {                              // view another plane
        pExtViewAc = this;
        CalcCameraViewPos();
    }
    
}

// calculate the correct external camera position
void LTAircraft::CalcCameraViewPos()
{
    if (IsInCameraView()) {
        posExt = ppos;
        
        // move position back along the longitudinal axes
        posExt += vectorTy (GetHeading(), mdl.EXT_CAMERA_LON_OFS + extOffs.x);
        // move position a bit to the side
        posExt += vectorTy (GetHeading()+90, mdl.EXT_CAMERA_LAT_OFS + extOffs.z);
        // and move a bit up
        posExt.alt_m() += mdl.EXT_CAMERA_VERT_OFS + extOffs.y;

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
        return 0;
    }

    // we have camera control, position has been calculated already in CalcPPos,
    // take it from posExt, fill output structure, apply movement by commands and pilot's head
    // factor of 10 means: If head moves 1m then view moves 10m...just imagine how big a wide body airliner is...
    outCameraPosition->x =        (float)posExt.X();
    outCameraPosition->y =        (float)posExt.Y();
    outCameraPosition->z =        (float)posExt.Z();
    outCameraPosition->heading  = (float)pExtViewAc->GetHeading() + extOffs.heading;
    outCameraPosition->pitch =                                      extOffs.pitch;
    outCameraPosition->roll =                                       extOffs.roll;
    outCameraPosition->zoom =                                       extOffs.zoom;
    
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
XPMPPlaneCallbackResult LTAircraft::GetPlanePosition(XPMPPlanePosition_t* outPosition)
{
    try {
        // object invalid (due to exceptions most likely), don't use anymore, don't call LT functions
        if (!IsValid())
            return xpmpData_Unavailable;
        
        // We (LT) don't get called anywhere else once per frame.
        // XPMP API calls directly for aircraft positions.
        // So we need to figure out our way if we are called the first time of a cycle
        int cycle = XPLMGetCycleNumber();
        if ( cycle != currCycle.num )            // new cycle!
            NextCycle(cycle);
        
#ifdef DEBUG
        fd.bIsSelected = bIsSelected = (key() == dataRefs.GetSelectedAcKey());
#endif
        
        // libxplanemp provides us with the multiplayer index, i.e. the plane's
        // index if reported via sim/multiplayer/position dataRefs.
        // We just store it.
        multiIdx = outPosition->multiIdx;
        
        // calculate new position and return it
        if (!dataRefs.IsReInitAll() &&          // avoid any calc if to be re-initialized
            CalcPPos())
        {
            // copy ppos (by type conversion)
            *outPosition = ppos;
            
            if (IsVisible()) {
                outPosition->aiPrio = aiPrio;       // AI slotting priority
                // alter altitude by main gear deflection, so plane moves down
                if (IsOnGrnd())
                    outPosition->elevation -= gearDeflection.is() / M_per_FT;
            } else {
                // if invisible move a/c to unreachable position
                outPosition->lat = AC_HIDE_LAT;
                outPosition->lon = AC_HIDE_LON;
                outPosition->elevation = AC_HIDE_ALT;
                outPosition->aiPrio = 100;
            }
            
            // add the label
            memcpy(outPosition->label, szLabelAc, sizeof(outPosition->label));
            // color depends on setting and maybe model
            if (dataRefs.IsLabelColorDynamic())
                memmove(outPosition->label_color, mdl.LABEL_COLOR, sizeof(outPosition->label_color));
            else
                dataRefs.GetLabelColor(outPosition->label_color);
            return xpmpData_NewData;
        }

        // no new position available...what a shame, return the last one
        *outPosition = ppos;
        return xpmpData_Unchanged;

    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
    } catch (...) {}

    // for any kind of exception: don't use this object any more!
    SetInvalid();
    return xpmpData_Unavailable;
}

XPMPPlaneCallbackResult LTAircraft::GetPlaneSurfaces(XPMPPlaneSurfaces_t* outSurfaces)
{
    try {
        // object invalid (due to exceptions most likely), don't use anymore, don't call LT functions
        if (!IsValid())
            return xpmpData_Unavailable;
        
        
        if (!dataRefs.IsReInitAll()) {
            // get current gear/flaps value (might be moving)
            surfaces.gearPosition = (float)gear.get();
            surfaces.slatRatio = surfaces.flapRatio = (float)flaps.get();
            surfaces.spoilerRatio = surfaces.speedBrakeRatio = (float)spoilers.get();
            surfaces.reversRatio = (float)reversers.get();

            // for engine / prop rotation we derive a value based on flight model
            if (doc8643.hasRotor())
                surfaces.engRotRpm = surfaces.propRotRpm = float(mdl.PROP_RPM_MAX);
            else
                surfaces.engRotRpm = surfaces.propRotRpm =
                    float(mdl.PROP_RPM_MAX/2 + surfaces.thrust * mdl.PROP_RPM_MAX/2);
            
            // Make props and rotors move based on rotation speed and time passed since last cycle
            surfaces.engRotDegree += (float)RpmToDegree(surfaces.engRotRpm, currCycle.diffTime);
            while (surfaces.engRotDegree >= 360.0f)
                surfaces.engRotDegree -= 360.0f;
            surfaces.propRotDegree = surfaces.engRotDegree;
            
            // Gear deflection - has an effect during touch-down only
            surfaces.tireDeflect = (float)gearDeflection.get();
            
            // Tire rotation similarly
            surfaces.tireRotRpm = (float)tireRpm.get();
            surfaces.tireRotDegree += (float)RpmToDegree(surfaces.tireRotRpm, currCycle.diffTime);
            while (surfaces.tireRotDegree >= 360.0f)
                surfaces.tireRotDegree -= 360.0f;

            // 'moment' of touch down?
            // (We use the reversers deploy time for this...that's 2s)
            surfaces.touchDown = reversers.isIncrease() && reversers.inMotion();
        }
        
        // just copy over our entire structure
        *outSurfaces = surfaces;
        
        return xpmpData_NewData;

    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
    } catch (...) {}

    // for any kind of exception: don't use this object any more!
    SetInvalid();
    return xpmpData_Unavailable;
}

XPMPPlaneCallbackResult LTAircraft::GetPlaneRadar(XPMPPlaneRadar_t* outRadar)
{
    XPMPPlaneCallbackResult ret = xpmpData_Unchanged;
    try {
        // object invalid (due to exceptions most likely), don't use anymore, don't call LT functions
        if (!IsValid())
            return xpmpData_Unavailable;
        
        // for radar 'calculation' we need some dynData
        // but radar doesn't change often...just only check every 100th cycle
        if (!dataRefs.IsReInitAll() &&
            currCycle.num % 100 == 0 )
        {
            // fetch new data if available
            LTFlightData::FDDynamicData dynCopy;
            if ( fd.TryGetSafeCopy(dynCopy) )
            {
                // copy fresh radar data
                radar               = dynCopy.radar;
                ret = xpmpData_NewData;
            }
        }
        
        // GetPlaneSurfaces fetches fresh data every 10th cycle
        // just copy over our entire structure
        *outRadar = radar;
        
        // if invisible we deactivate TCAS/AI/multiplayer
        if (!IsVisible()) {
            outRadar->mode = xpmpTransponderMode_Standby;
            ret = xpmpData_NewData;
        }
        
        return ret;

    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
    } catch (...) {}

    // for any kind of exception: don't use this object any more!
    SetInvalid();
    return xpmpData_Unavailable;
}

XPMPPlaneCallbackResult LTAircraft::GetInfoTexts(XPMPInfoTexts_t* outInfo)
{
    try {
        // object invalid (due to exceptions most likely), don't use anymore, don't call LT functions
        if (!IsValid() || dataRefs.IsReInitAll())
            return xpmpData_Unavailable;
        
        // Is there new data to send?
        if (ShallSendNewInfoData())
        {
            // fetch new data if available
            LTFlightData::FDStaticData statCopy;
            if ( fd.TryGetSafeCopy(statCopy) )
            {
                // not even initialized???
                if (!statCopy.isInit())
                    return xpmpData_Unavailable;
                
                // copy data over to libxplanemp
                assert(outInfo->size == sizeof(XPMPInfoTexts_t));
                memset(outInfo, 0, sizeof(XPMPInfoTexts_t));
                outInfo->size = sizeof(XPMPInfoTexts_t);
                strcpy_s(outInfo->tailNum,      sizeof(outInfo->tailNum),       strAtMost(statCopy.reg, sizeof(outInfo->tailNum) - 1).c_str());
                strcpy_s(outInfo->icaoAcType,   sizeof(outInfo->icaoAcType),    strAtMost(statCopy.acTypeIcao, sizeof(outInfo->icaoAcType) - 1).c_str());
                strcpy_s(outInfo->manufacturer, sizeof(outInfo->manufacturer),  strAtMost(statCopy.man, sizeof(outInfo->manufacturer) - 1).c_str());
                strcpy_s(outInfo->model,        sizeof(outInfo->model),         strAtMost(statCopy.mdl, sizeof(outInfo->model) - 1).c_str());
                strcpy_s(outInfo->icaoAirline,  sizeof(outInfo->icaoAirline),   strAtMost(statCopy.opIcao, sizeof(outInfo->icaoAirline) - 1).c_str());
                strcpy_s(outInfo->airline,      sizeof(outInfo->airline),       strAtMost(statCopy.op, sizeof(outInfo->airline) - 1).c_str());
                strcpy_s(outInfo->flightNum,    sizeof(outInfo->flightNum),     strAtMost(statCopy.flight, sizeof(outInfo->flightNum) - 1).c_str());
                strcpy_s(outInfo->aptFrom,      sizeof(outInfo->aptFrom),       strAtMost(statCopy.originAp, sizeof(outInfo->aptFrom) - 1).c_str());
                strcpy_s(outInfo->aptTo,        sizeof(outInfo->aptTo),         strAtMost(statCopy.destAp,      sizeof(outInfo->aptTo)-1).c_str());

                // so wen send new data
                bSendNewInfoData = false;
                return xpmpData_NewData;
            }
        }
        return xpmpData_Unchanged;
        
    } catch (const std::exception& e) {
        LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
    } catch (...) {}
    
    // for any kind of exception: don't use this object any more!
    SetInvalid();
    return xpmpData_Unavailable;
}

// fetches and then returns the name of the aircraft model in use
std::string LTAircraft::GetModelName() const
{
    char buf[256];
    XPMPGetPlaneModelName(mPlane, buf, sizeof(buf));
    return std::string(buf);
}

// change the model (e.g. when model-defining static data changed)
void LTAircraft::ChangeModel (const LTFlightData::FDStaticData& statData)
{
    const std::string oldModelName(GetModelName());
    CalcLabelInternal(statData);
    XPMPChangePlaneModel(mPlane,
                         statData.acTypeIcao.c_str(),
                         statData.opIcao.c_str(),
                         statData.reg.c_str());
    
    // if there was an actual change inform the log
    if (oldModelName != GetModelName()) {
        LOG_MSG(logINFO,INFO_AC_MDL_CHANGED,
                labelInternal.c_str(),
                statData.opIcao.c_str(),
                GetModelName().c_str());
    }
}

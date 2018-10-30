//
//  LTAircraft.cpp
//  LiveTraffic

/*
 * Copyright (c) 2018, Birger Hoppe
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

#include <fstream>
#include <regex>

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
        prevCycle.elapsedTime = XPLMGetElapsedTime() - 0.1;
        prevCycle.simTime  = dataRefs.GetSimTime() - 0.1;
    }
    currCycle.num = newCycle;
    currCycle.elapsedTime = XPLMGetElapsedTime();
    currCycle.simTime  = dataRefs.GetSimTime();
    
    // the time that has passed since the last cycle
    currCycle.diffTime  = currCycle.simTime - prevCycle.simTime;
    
    // time should move forward (positive difference) and not too much either
    // (but no problem if no a/c yet displayed, i.e. this call being the first)
    if (dataRefs.GetNumAircrafts() > 0 &&
        (currCycle.diffTime < 0 || currCycle.diffTime > SIMILAR_TS_INTVL) ) {
        dataRefs.SetReInitAll(true);
        SHOW_MSG(logWARN, ERR_TIME_NONLINEAR, currCycle.diffTime);
        return false;
    }
    
    return true;
}

// comparing 2 doubles for near-equality
bool dequal ( const double d1, const double d2 )
{
    const double epsilon = 0.00001;
    return ((d1 - epsilon) < d2) &&
           ((d1 + epsilon) > d2);
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

// start a move to the given target in the specified time frame
void MovingParam::moveTo ( double tval )
{
    LOG_ASSERT((defMin <= tval) && (tval <= defMax));
    
    // current value equals target already
    if (dequal(tval, val))
        SetVal(tval);                     // just set the target value bit-euqal, no moving
    // we shall move to a (new) given target:
    // calc required duration by using defining parameters
    else if ( valTo != tval ) {
        // set origin and desired target value
        valFrom = val;
        valTo = tval;
        valDist = valTo - valFrom;
        bIncrease = valDist > 0;

        // full travel from defMin to defMax takes defDuration
        // So: How much time shall we use? = Which share of the full duration do we need?
        // And: When will we be done?
        // timeTo = fabs(valDist/defDist) * defDuration + timeFrom;
        timeFrom = currCycle.simTime;
        timeTo = fma(fabs(valDist/defDist), defDuration, timeFrom);
    }
}

// pre-program a move, which is to finish by the given time
void MovingParam::moveToBy (double _from, bool _increase, double _to,
                            double _startTS, double _by_ts,
                            bool _startEarly)
{
    // current value equals target already
    if (dequal(_to, val)) {
        SetVal(_to);                // just set the target value bit-euqal, no moving
    }
    // we shall move to a (new) given target:
    // calc required duration by using defining parameters
    else if ( valTo != _to ) {
        // default values
        if (isnan(_from))       _from = val;
        if (isnan(_startTS))    _startTS = currCycle.simTime;
        
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
    if (isnan(_from))
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
    startTime = accelStartTime = isnan(_startTime) ? currCycle.simTime : _startTime;

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
    if (isnan(ts))
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
    if (isnan(ts))
        ts = currCycle.simTime;
    LOG_ASSERT(ts >= startTime);
    
    // shortcut for constant speed: 𝑑(∆t) = startSpeed × ∆t
    if (isnan(accelStartTime))
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
    // split into name and value
    static std::regex re ("(\\w+)\\s+(-?\\d+(\\.\\d+)?)");
    std::smatch m;
    std::regex_search(text, m, re);
    
    // at least two matches expected
    if (m.length() < 2) {
        LOG_MSG(logWARN, ERR_CFG_FORMAT, fileName, ln, text.c_str());
        return false;
    }
    
    // name and value
    std::string name (m[1]);
    double val = std::atof(m[2].str().c_str());
                           
    // now find correct member variable and assign value
#define FM_ASSIGN(nameOfVal) if (name == #nameOfVal) fm.nameOfVal = val
    FM_ASSIGN(GEAR_DURATION);
    else FM_ASSIGN(FLAPS_DURATION);
    else FM_ASSIGN(VSI_STABLE);
    else FM_ASSIGN(ROTATE_TIME);
    else FM_ASSIGN(VSI_FINAL);
    else FM_ASSIGN(VSI_INIT_CLIMB);
    else FM_ASSIGN(SPEED_INIT_CLIMB);
    else FM_ASSIGN(AGL_GEAR_DOWN);
    else FM_ASSIGN(AGL_GEAR_UP);
    else FM_ASSIGN(AGL_FLARE);
    else FM_ASSIGN(MAX_TAXI_SPEED);
    else FM_ASSIGN(TAXI_TURN_TIME);
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
    else FM_ASSIGN(PITCH_RATE);
    else FM_ASSIGN(LIGHT_PATTERN);
    else FM_ASSIGN(LIGHT_LL_ALT);
    else {
        LOG_MSG(logWARN, ERR_FM_UNKNOWN_NAME, fileName, ln, text.c_str());
        return false;
    }
    
    return true;
}

// process a line in the [Map] section by splitting values using a RegEx
// and then assign the value to the fm object
bool fm_processMapLine (const char* fileName, int ln,
                        std::string& text, LTAircraft::FlightModel& fm)
{
    // split into name and value
    static std::regex re ("(\\w+)\\s+(.+)$");
    std::smatch m;
    std::regex_search(text, m, re);
    
    // at least two matches expected
    if (m.length() < 2) {
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
    // open the Flight Model file
    std::string sFileName (LTCalcFullPath(PATH_FLIGHT_MODELS));
#ifdef APL
    // Mac: convert to Posix
    LTHFS2Posix(sFileName);
#endif
    std::ifstream fIn (sFileName);
    if (!fIn) {
        // if there is no FlightModel file just return
        // that's no real problem, we can use defaults, but unexpected
        SHOW_MSG(logWARN, ERR_CFG_FILE_OPEN_IN,
                 sFileName.c_str(), std::strerror(errno));
        return false;
    }
    
    // first line is supposed to be the version - and we know of exactly one:
    std::string sDataRef, sVal;
    fIn >> sDataRef >> sVal;
    if (!fIn ||
        sDataRef != LIVE_TRAFFIC ||
        sVal != LT_VERSION)
    {
        SHOW_MSG(logERR, ERR_CFG_FILE_VER, sFileName.c_str());
        return false;
    }
    
    // then follow sections and their entries
    // state signifies what kind of section we are currently reading
    enum fmFileStateTy { FM_NO_SECTION, FM_MODEL_SECTION, FM_MAP } fmState = FM_NO_SECTION;
    FlightModel fm;
    int errCnt = 0;
    for (int ln=1; fIn && errCnt <= ERR_CFG_FILE_MAXWARN; ln++) {
        // read entire line
        char lnBuf[255];
        lnBuf[0] = 0;
        fIn.getline(lnBuf, sizeof(lnBuf));
        std::string text(lnBuf);
        
        // remove trailing comments starting with '#'
        size_t pos = text.find('#');
        if (pos != std::string::npos)
            text.erase(pos);
    
        // remove leading and trailing white space
        if ((pos = text.find_first_not_of(WHITESPACE)) != std::string::npos)
            text.erase(0, pos);
        if ((pos = text.find_last_not_of(WHITESPACE)) != std::string::npos)
            text.erase(pos + 1);
                   
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
                std::string::size_type pos = text.find(FM_PARENT_SEPARATOR);
                if (pos != std::string::npos) {
                    // parent's name and model
                    const std::string parent (text.substr(pos+1,
                                                          text.length()-pos-1));
                    const FlightModel* pParentFM = GetFlightModel(parent);
                    
                    // init model from parent
                    if (pParentFM)
                        fm = *pParentFM;
                    else
                        LOG_MSG(logWARN, ERR_FM_UNKNOWN_PARENT, sFileName.c_str(), ln,
                                text.c_str());
                    
                    // change name to new section's name (without ":parent")
                    fm.modelName = text.substr(0,pos);
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
                if (!fm_processMapLine(sFileName.c_str(), ln, text, fm))
                    errCnt++;
                break;
        }
        
    }
    
    // problem was not just eof?
    if (!fIn && !fIn.eof()) {
        SHOW_MSG(logERR, ERR_CFG_FILE_READ,
                 sFileName.c_str(), std::strerror(errno));
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
XPCAircraft(inFd.WaitForSafeCopyStat().acTypeIcao.c_str(),  // repeated calls to WaitForSafeCopyStat look inefficient
            inFd.WaitForSafeCopyStat().opIcao.c_str(),      // ...but if the lock is held by the calling function already
            inFd.WaitForSafeCopyStat().op.c_str()),         // ...then these are quick recursive calls
// class members
fd(inFd),
mdl(FlightModel::FindFlightModel(inFd.WaitForSafeCopyStat().acTypeIcao)),   // find matching flight model
phase(FPH_UNKNOWN),
rotateTs(NAN),
tsLastCalcRequested(0),
vsi(0.0),
bOnGrnd(false), bArtificalPos(false), bNeedNextVec(false),
gear(mdl.GEAR_DURATION),
flaps(mdl.FLAPS_DURATION),
heading(mdl.TAXI_TURN_TIME, 360, 0, true),
pitch((mdl.PITCH_MAX-mdl.PITCH_MIN)/mdl.PITCH_RATE, mdl.PITCH_MAX, mdl.PITCH_MIN),
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
        labelAc = fd.ComposeLabel();
        // standard internal label (e.g. for logging) is transpIcao + ac type + company icao
        labelInternal = key() + " (" + statCopy.acTypeIcao + " " + statCopy.opIcao + ")";
        
        // init surfaces
        memset ( &surfaces, 0, sizeof(surfaces));
        surfaces.size = sizeof(surfaces);
        
        // init moving params where necessary
        pitch.SetVal(0);
        
        // calculate our first position, must also succeed
        if (!CalcPPos())
            LOG_MSG(logERR,ERR_AC_CALC_PPOS,fd.key().c_str());
        
        // tell the world we've added something
        dataRefs.IncNumAircrafts();
        LOG_MSG(logINFO,INFO_AC_ADDED,
                labelInternal.c_str(),
                GetModelName().c_str(),
                mdl.modelName.c_str(),
                vecView.angle, vecView.dist/M_per_KM);
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// Destructor
LTAircraft::~LTAircraft()
{
    // Release probe handle
    if (probeRef)
        XPLMDestroyProbe(probeRef);
    
    // Decrease number of visible aircrafts and log a message about that fact
    dataRefs.DecNumAircrafts();
    LOG_MSG(logINFO,INFO_AC_REMOVED,labelInternal.c_str());
}

// MARK: LTAircraft stringify for debugging output purposes
LTAircraft::operator std::string() const
{
    char buf[500];
    snprintf(buf,sizeof(buf),"a/c %s ppos: %s Y: %.0ff Phase: %02d %s\nposList:\n",
             key().c_str(),
             std::string(ppos).c_str(), terrainAlt,
             phase, FlightPhase2String(phase).c_str());
    return std::string(buf) + positionDeque2String(posList);
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
    const positionTy& lastPos = *std::prev(posList.cend());
    if ((lastPos.ts() <= currCycle.simTime + 2*TIME_REQU_POS) &&
        (tsLastCalcRequested + 2*TIME_REQU_POS <= currCycle.simTime))
    {
        fd.TriggerCalcNewPos(std::max(currCycle.simTime,posList[1].ts()));
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
    std::string debFrom ( from );
    std::string debTo   ( to );
    std::string debVec  ( from.between(to) );
#endif
    LOG_ASSERT_FD(fd,duration > 0);

    // *** position switch ***
    
    // some things only change when we work with new positions compared to last frame
    if ( bPosSwitch ) {
        // *** vector we will be flying now from 'from' to 'to':
        vec = from.between(to);
        LOG_ASSERT_FD(fd,!isnan(vec.speed) && !isnan(vec.vsi));
        
        // vertical speed between the two points [ft/m] is constant:
        vsi = vec.vsi_ft();
        
        // first time inits
        if (phase == FPH_UNKNOWN) {
            // check if starting on the ground
            bOnGrnd = from.onGrnd == positionTy::GND_ON;
            
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
        heading.moveQuickestToBy(NAN,
                                 vec.dist > SIMILAR_POS_DIST ?  // if vector long enough:
                                 vec.angle :                    // turn to vector heading
                                 AvgHeading(from.heading(),to.heading()),   // otherwise only turn to avg between from- and target-heading
                                 NAN, (from.ts()+to.ts())/2,    // by half the vector flight time
                                 true);                         // start immediately
        
        // *** Pitch ***
        
        // Fairly simplistic...in the range -2 to +18 depending linearly on vsi only
        double toPitch =
        bOnGrnd ? 0 :
        GetVSI_ft() < mdl.PITCH_MIN_VSI ? mdl.PITCH_MIN :
        GetVSI_ft() > mdl.PITCH_MAX_VSI ? mdl.PITCH_MAX :
        (mdl.PITCH_MIN +
         (GetVSI_ft()-mdl.PITCH_MIN_VSI)/(mdl.PITCH_MAX_VSI-mdl.PITCH_MIN_VSI)*
         (mdl.PITCH_MAX-mdl.PITCH_MIN));
        
        // another approach for climbing phases:
        // a/c usually flies more or less with nose pointing upward toward climb,
        // nearly no drag, so try calculating the flight path angle and use
        // it also as pitch
        if ( GetVSI_ft() > mdl.VSI_STABLE ) {
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
        pitch.moveQuickestToBy(NAN, toPitch, NAN, to.ts(), true);
        
        // *** speed/acceleration/decelaration
        
        // Only skip this in case of short final, i.e. if currently off ground
        // but next pos is on the ground, because in that case the
        // next vector is the vector for roll-out on the ground
        // with significantly reduced speed due to breaking, that speed
        // is undesirable at touch-down; for short final we assume constant speed
        bNeedNextVec = false;
        if (! (from.onGrnd < positionTy::GND_ON &&
               to.onGrnd == positionTy::GND_ON) ) {
            // will be calculated soon outside "if (bPosSwitch)"
            // (need provision for case we don't get the data access lock right now)
            bNeedNextVec = true;
        }
        else {
            // short final, i.e. last vector before touch down
            
            // keep constant speed:
            // avg ground speed between the two points [kt]
            speed.SetSpeed(vec.speed);
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
        if (!isnan(toSpeed)) {
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
        speed.m_s() > 0 &&
        !bArtificalPos)
    {
        // init deceleration down to zero
        speed.StartAccel(speed.m_s(),
                         0,
                         mdl.ROLL_OUT_DECEL);
        
        // the vector to the stopping point
        vectorTy vec(ppos.heading(),            // keep current heading
                     speed.getTargetDeltaDist());// distance needed to stop
        
        // add ppos and the stop point (ppos + above vector) to the list of positions
        // (they will be activated with the next frame only)
        posList.emplace_back(ppos);
        positionTy& stopPoint = posList.emplace_back(ppos.destPos(vec));
        stopPoint.ts() = speed.getTargetTime();
        bArtificalPos = true;                   // flag: we are working with an artifical position now
        if (dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_INVENTED_STOP_POS,std::string(stopPoint).c_str());
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
    // soon anyway
    if (f > 1 && !ppos.isNormal()) {
        LOG_MSG(logDEBUG,ERR_POS_UNNORMAL,key().c_str(),std::string(ppos).c_str());
        for (f -= 0.1; f > 1 && !ppos.isNormal(); f -= 0.1)
            ppos.v = from.v * (1-f) + to.v * f;
    }    
    
    // *** Attitude ***/

    // half-way through prepare turning to end heading
    if ( f > 0.5 && heading.toVal() != to.heading() )
        heading.moveQuickestToBy(NAN, to.heading(), // target heading
                                 NAN, to.ts(),      // by target timestamp
                                 false);            // start as late as possible
    // current heading
    ppos.heading() = heading.get();
    // current pitch
    ppos.pitch() = pitch.get();
    
#ifdef DEBUG
    std::string debPpos ( ppos );
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
    } else {
        // not on the ground
        // just lifted off? then recalc vsi
        if (phase == FPH_LIFT_OFF && vsi == 0) {
            vsi = ppos.between(to).vsi_ft();
        }
    }
    
    // success
    return true;
}

// From ppos and altitudes we derive other a/c parameters like gear, flaps etc.
// Ultimately, we calculate flight phases based on flight model assumptions
void LTAircraft::CalcFlightModel (const positionTy& from, const positionTy& to)
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
    if (ppos.onGrnd == positionTy::GND_ON &&
        to.onGrnd == positionTy::GND_ON) {
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
    if ( phase < FPH_ROTATE && currCycle.simTime >= rotateTs ) {
        phase = FPH_ROTATE;
        rotateTs = NAN;             // 'eat' the timestamp
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
        surfaces.thrust            = 0.1;
        surfaces.lights.timeOffset = (unsigned int)rand();
        surfaces.lights.landLights = 0;
        surfaces.lights.bcnLights  = 1;
        surfaces.lights.strbLights = 0;
        surfaces.lights.navLights  = 1;
        // FIXME: Identify Airbus/GA for correct patterns
        surfaces.lights.flashPattern = xpmp_Lights_Pattern_EADS;
        
        gear.down();
        flaps.up();
    }
    
    // Phase Take Off
    if (ENTERED(FPH_TAKE_OFF)) {
        surfaces.lights.strbLights = 1;
        surfaces.lights.landLights = 1;
        surfaces.thrust = 1.0;          // a bit late...but anyway ;)
        flaps.down();
    }
    
    // Lift Off
    if (ENTERED(FPH_ROTATE)) {
        // (as we don't do any counter-measure in the next ENTERED-statements
        //  we can lift the nose only if we are exatly AT rotate phase)
        if (phase == FPH_ROTATE)
            pitch.max();
    }
    
    // entered Initial Climb
    if (ENTERED(FPH_INITIAL_CLIMB)) {
        gear.up();
    }
    
    // entered climb (from below)
    if (ENTERED(FPH_CLIMB)) {
        surfaces.thrust = 0.8;
        flaps.up();
    }
    
    // cruise
    if (ENTERED(FPH_CRUISE)) {
        // FIXME: Landing lights need to stay on til FL100 (Issue #10)
        surfaces.lights.landLights = 0;
        surfaces.thrust = 0.6;
    }

    // descend
    if (ENTERED(FPH_DESCEND)) {
        surfaces.thrust = 0.1;
    }
    
    // approach
    if (ENTERED(FPH_APPROACH)) {
        surfaces.lights.landLights = 1;
        surfaces.thrust = 0.2;
        flaps.down();
    }
    
    // final
    if (ENTERED(FPH_FINAL)) {
        surfaces.thrust = 0.3;
        gear.down();
    }
    
    // flare
    if (ENTERED(FPH_FLARE)) {
        pitch.moveTo(mdl.PITCH_FLARE);      // flare!
    }
    
    // touch-down
    if (ENTERED(FPH_TOUCH_DOWN)) {
        surfaces.spoilerRatio = surfaces.speedBrakeRatio = 1.0;
        ppos.onGrnd = positionTy::GND_ON;
        pitch.moveTo(0);
    }
    
    // roll-out
    if (ENTERED(FPH_ROLL_OUT)) {
        surfaces.thrust = -0.9;         // reversers...does that work???
    }
    
    // taxiing off the runway after landing (cycle phase back to beginning)
    if ( bFPhPrev >= FPH_APPROACH && phase == FPH_TAXI ) {
        flaps.up();
        surfaces.spoilerRatio = surfaces.speedBrakeRatio = 0.0;
        surfaces.thrust = 0.1;
        surfaces.lights.landLights = 0;
        surfaces.lights.strbLights = 0;
    }
    
    // *** safety measures ***
    
    // Need gear on the ground
    if ( bOnGrnd ) {
        gear.down();
        pitch.moveTo(0);
    }
    else {
        // need flaps below flap speeds
        if ( speed.kt() < std::min(mdl.FLAPS_UP_SPEED,mdl.FLAPS_DOWN_SPEED) ) {
            flaps.down();
        }
        
        // no flaps above flap speeds
        if ( speed.kt() > std::max(mdl.FLAPS_UP_SPEED,mdl.FLAPS_DOWN_SPEED) ) {
            flaps.up();
        }
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
    //  but less often high up in the air)
    if ( currCycle.simTime < probeNextTs )
        return true;
    
    // This is terrain altitude right beneath us in [ft]
    terrainAlt = YProbe_at_m(ppos, probeRef) / M_per_FT;
    
    // lastly determine when to do a probe next, more often if closer to the ground
    LOG_ASSERT_FD(fd,sizeof(PROBE_HEIGHT_LIM) == sizeof(PROBE_DELAY));
    for ( int i=0; i < sizeof(PROBE_HEIGHT_LIM)/sizeof(PROBE_HEIGHT_LIM[0]); i++)
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
    
    // Success
    return true;
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
        
        // calculate new position and return it
        if (!dataRefs.IsReInitAll() &&          // avoid any calc if to be re-initialized
            CalcPPos())
        {
            *outPosition = ppos;                // copy ppos (by type conversion), and add the label
            strncpy ( outPosition->label, labelAc.c_str(), sizeof(outPosition->label)-1 );
            outPosition->label[sizeof(outPosition->label)-1] = 0;
            return xpmpData_NewData;
        }
        else {
            // no new position available...what a shame, return the last one
            *outPosition = ppos;
            return xpmpData_Unchanged;
        }
    }
    catch (...) {
        // for any kind of exception: don't use this object any more!
        bValid = false;
        return xpmpData_Unavailable;
    }
}

XPMPPlaneCallbackResult LTAircraft::GetPlaneSurfaces(XPMPPlaneSurfaces_t* outSurfaces)
{
    try {
        // object invalid (due to exceptions most likely), don't use anymore, don't call LT functions
        if (!IsValid())
            return xpmpData_Unavailable;
        
        
        if (!dataRefs.IsReInitAll()) {
            // get current gear/flaps value (might be moving)
            surfaces.gearPosition = gear.get();
            surfaces.flapRatio = flaps.get();
        }
        
        // just copy over our entire structure
        *outSurfaces = surfaces;
        
        return xpmpData_NewData;
    }
    catch (...) {
        // for any kind of exception: don't use this object any more!
        bValid = false;
        return xpmpData_Unavailable;
    }
}

XPMPPlaneCallbackResult LTAircraft::GetPlaneRadar(XPMPPlaneRadar_t* outRadar)
{
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
            }
        }
        
        // GetPlaneSurfaces fetches fresh data every 10th cycle
        // just copy over our entire structure
        *outRadar = radar;
        
        // assume every 100th cycle something change...not more often at least
        return currCycle.num % 100 == 0 ? xpmpData_NewData : xpmpData_Unchanged;
    }
    catch (...) {
        // for any kind of exception: don't use this object any more!
        bValid = false;
        return xpmpData_Unavailable;
    }
}

// fetches and then returns the name of the aircraft model in use
std::string LTAircraft::GetModelName() const
{
    char buf[256];
    XPMPGetPlaneModelName(mPlane, buf, sizeof(buf));
    return std::string(buf);
}

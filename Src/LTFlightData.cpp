/// @file       LTFlightData.cpp
/// @brief      LTFlightData represents the tracking data of one aircraft, even before it is drawn
/// @details    Keeps statis and dynamic tracking data.\n
///             Dynamic tracking data is kept as a list.\n
///             Various optimizations and cleansing applied to dynamic data in a separate thread.\n
///             Provides fresh tracking data to LTAircraft upon request.
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

//
//MARK: Global
//

// the global map of all received flight data,
// which also includes pointer to the simulated aircraft
mapLTFlightDataTy mapFd;
// modifying the map is controlled by a mutex
// (note that mapFdMutex must be locked before dataAccessMutex
//  to avoid deadlocks, mapFdMutex is considered a higher-level lock)
std::mutex      mapFdMutex;

// flag to indicate that there is no new positional data
// to analyse for terrain altitude and subsequently
// add to posDeque, i.e. if true AppendAllNewPos returns immediately
std::atomic_flag flagNoNewPosToAdd = ATOMIC_FLAG_INIT;

//
//MARK: Flight Data Subclasses
//
LTFlightData::FDDynamicData::FDDynamicData () :
gnd(false), inHg(0.0),                  // positional
brng(0.0), dst(0.0),                    // relative position
spd(0.0), vsi(0.0),                     // movement
ts(0),
pChannel(nullptr)
{
    // radar
    memset(&radar, 0, sizeof(radar));
    radar.size = sizeof(radar);
    radar.mode = xpmpTransponderMode_ModeC;
}

// formatted Squawk Code
std::string LTFlightData::FDDynamicData::GetSquawk() const
{
    if (radar.code <= 0 || radar.code > 9999)
        return "-";
    else
    {
        char s[10];
        snprintf(s, sizeof(s), "%04ld", radar.code);
        return std::string(s);
    }
}

LTFlightData::FDStaticData& LTFlightData::FDStaticData::operator |= (const FDStaticData& other)
{
    // copy filled, and only filled data over current data
    // do it field-by-field only for fields which are actually filled
    
    // acTypeICAO
    // We never overwrite with nothing, ie. the new value must be _something_
    if (!other.acTypeIcao.empty())
    {
        // Accept anything if we are currently empty (unknown/default) or a car, can't be worse...
        if (acTypeIcao.empty() ||
            acTypeIcao == dataRefs.GetDefaultCarIcaoType())
        {
            acTypeIcao = other.acTypeIcao;
        }
        // else: we are non-empty, non-default -> no change, no matter what is delivered,
        //       to avoid ping-ponging the a/c plane when different channels have different opinion
    }
    
    // a/c details
    if (!other.country.empty()) country = other.country;
    if (!other.man.empty()) man = other.man;
    if (other.mdl.length() > mdl.length()) mdl = other.mdl;
    if (!other.catDescr.empty()) catDescr = other.catDescr;
    if (other.year) year = other.year;
    if (other.mil) mil = other.mil;     // this only overwrite if 'true'...
    if (other.trt) trt = other.trt;
    
    // flight
    if (!other.call.empty()) call = other.call;
    
    // little trick for priority: we trust the info with the longer flight number
    if (other.flight.length() > flight.length() ||
        // or no flight number info at all...
        (other.flight.empty() && flight.empty())) {
        if (!other.originAp.empty()) originAp = other.originAp;
        if (!other.destAp.empty()) destAp = other.destAp;
        if (!other.flight.empty()) flight = other.flight;
    }
    
    // operator / Airline
    if (!other.op.empty()) op = other.op;
    if (!other.opIcao.empty()) opIcao = other.opIcao;
    
    // now initialized
    bInit = true;

    // find DOC8643 and fill man/mdl from there if needed
    pDoc8643 = &(Doc8643::get(acTypeIcao));
    LOG_ASSERT(pDoc8643 != NULL);
    if (man.empty())
        man = pDoc8643->manufacturer;
    if (mdl.empty())
        mdl = pDoc8643->model;
    
    return *this;
}

// route (this is "originAp - destAp", but considers emoty txt)
std::string LTFlightData::FDStaticData::route () const
{
    // keep it an empty string if there is no info at all
    if (originAp.empty() && destAp.empty())
        return std::string();
    
    // if there is some info then replace missing info with question mark
    std::string s(originAp.empty() ? "?" : originAp);
    s += '-';
    s += destAp.empty() ? "?" : destAp;
    return s;
}

// returns flight, call sign, registration, or trans hex code
std::string LTFlightData::FDStaticData::acId (const std::string _default) const
{
    return
    !flight.empty() ?   flight  :
    !call.empty() ?     call    :
    !reg.empty() ?      reg     :
    _default;
}

// route (this is "originAp-destAp", but considers empty txt)
std::string LTFlightData::FDStaticData::flightRoute() const
{
    const std::string r(route());
    // keep it an empty string if there is no info at all
    if (flight.empty() && r.empty())
        return std::string();
    
    // if there is some info missing then just return the other
    if (flight.empty())
        return r;
    if (r.empty())
        return flight;
    
    // we have both...put it together
    return (flight + ": ") + r;
}

// set the key value
std::string LTFlightData::FDKeyTy::SetKey (FDKeyType _eType, unsigned long _num)
{
    eKeyType = _eType;
    num = _num;
    return key = SetVal(eKeyType,num);
}

std::string LTFlightData::FDKeyTy::SetKey (FDKeyType _eType, const std::string _key, int base)
{
    return SetKey(_eType, std::stoul(_key, nullptr, base));
}


// FDKeyTy: Set any value
std::string LTFlightData::FDKeyTy::SetVal (FDKeyType _eType, unsigned long _num)
{
    // convert to uppercase hex string
    char buf[50];
    switch(_eType) {
        case KEY_OGN:
            snprintf(buf, sizeof(buf), "%08lX", _num);
            return ogn = buf;
        case KEY_RT:
            snprintf(buf, sizeof(buf), "%08lX", _num);
            return rtId = buf;
        case KEY_FLARM:
            snprintf(buf, sizeof(buf), "%06lX", _num);
            return flarm = buf;
        case KEY_ICAO:
            snprintf(buf, sizeof(buf), "%06lX", _num);
            return icao = buf;
        default:
            // must not happen
            LOG_ASSERT(eKeyType!=KEY_UNKNOWN);
            return "";
    }

}

std::string LTFlightData::FDKeyTy::SetVal (FDKeyType _eType, const std::string _val, int base)
{
    return SetVal(_eType, std::stoul(_val, nullptr, base));
}

// matches any string?
bool LTFlightData::FDKeyTy::isMatch (const std::string t) const
{
    return (t == key ||
            t == icao ||
            t == flarm ||
            t == rtId ||
            t == ogn);
}


//
//MARK: Flight Data
//

// Constructor
LTFlightData::LTFlightData () :
rcvr(0),sig(0),
rotateTS(NAN),
// created "now"...if no positions are ever added then it will be removed after 2 x outdated interval
youngestTS(dataRefs.GetSimTime() + 2 * dataRefs.GetAcOutdatedIntvl()),
pAc(nullptr), probeRef(NULL),
bValid(true)
{}

// Copy Constructor (needed for emplace into map) doesn't copy mutex
LTFlightData::LTFlightData(const LTFlightData& fd)
{
    // all logic is in the copy assignment operator
    *this = fd;
}

// Destructor makes sure lock is available and aircraft is removed, too
LTFlightData::~LTFlightData()
{
    try {
//        LOG_MSG(logDEBUG, "FD destroyed for %s", key().c_str());
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // make sure aircraft is removed, too
        DestroyAircraft();
        // Release probe handle
        if (probeRef)
            XPLMDestroyProbe(probeRef);
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// Copy assignment operator copies all but the mutex
LTFlightData& LTFlightData::operator=(const LTFlightData& fd)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy data
        acKey               = fd.acKey;             // key
        rcvr                = fd.rcvr;
        sig                 = fd.sig;
        labelStat           = fd.labelStat;
        labelCfg            = fd.labelCfg;
        posDeque            = fd.posDeque;          // dynamic data
        posToAdd            = fd.posToAdd;
        dynDataDeque        = fd.dynDataDeque;
        rotateTS            = fd.rotateTS;
        youngestTS          = fd.youngestTS;
        statData            = fd.statData;          // static data
        pAc                 = fd.pAc;
        probeRef            = fd.probeRef;
        bValid              = fd.bValid;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return *this;
}

// set this FD invalid (which will cause it's removal)
void LTFlightData::SetInvalid()
{
    bValid = false;
    // also need to make aircraft invalid so it won't be drawn again
    if (pAc)
        pAc->SetInvalid();
}

// Set the object's key, usually right after creation in fdMap
void LTFlightData::SetKey (const FDKeyTy& _key)
{
    acKey = _key;
//    LOG_MSG(logDEBUG, "FD crated for %s", key().c_str());
}


// Search support: icao, registration, call sign, flight number matches?
bool LTFlightData::IsMatch (const std::string t) const
{
    // we can compare key without lock
    if (acKey.isMatch(t))
        return true;
    
    // everything else must be guarded
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // compare with registration, flight number, call sign, squawk code
        if (statData.flight == t    ||
            statData.reg == t       ||
            statData.call == t      ||
            GetUnsafeDyn().GetSquawk() == t)
            return true;
        
        // no match
        return false;
        
        // copy data
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return false;
}

bool LTFlightData::validForAcCreate(double simTime) const
{
    // the obvious stuff first: we need basic data
    if ( empty() || dynDataDeque.empty() || posDeque.size() < 2 )
        return false;

    // simTime defaults to 'now'
    if (std::isnan(simTime))
        simTime = dataRefs.GetSimTime();

    // so we have two positions...one in the past, one in the future?
    return
    posDeque.front().ts() <= simTime && simTime < posDeque[1].ts();
}

// is the data outdated (considered too old to be useful)?
bool LTFlightData::outdated (double simTime) const
{
    // general re-init necessary?
    if (dataRefs.IsReInitAll())
        return true;
    
    // a/c turned invalid (due to exceptions)?
    if (pAc && !pAc->IsValid())
        return true;
    
    // flown out of sight? -> outdated, remove
    if (pAc &&
        pAc->GetVecView().dist > dataRefs.GetFdStdDistance_m())
        return true;
    
    // cover the special case of finishing landing and roll-out without live positions
    // i.e. during approach and landing we don't destroy the aircraft
    //      until it finally stopped on the runway
    if (pAc &&
        pAc->GetFlightPhase() >= FPH_APPROACH &&
        pAc->GetFlightPhase() <  FPH_STOPPED_ON_RWY)
    {
        return false;
    }
    
    // invalid and
    // youngestTS longer ago than allowed?
    return posDeque.empty() &&
    youngestTS + dataRefs.GetAcOutdatedIntvl() < (std::isnan(simTime) ? dataRefs.GetSimTime() : simTime);
}

#define ADD_LABEL(b,txt) if (b && !txt.empty()) { labelStat += txt; labelStat += ' '; }
// update static data parts of the a/c label for reuse for performance reasons
void LTFlightData::UpdateStaticLabel()
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // the configuration: which parts to include in the label?
        const DataRefs::LabelCfgTy cfg = dataRefs.GetLabelCfg();
        
        // add parts as per config
        labelStat.clear();
        ADD_LABEL(cfg.bIcaoType,    statData.acTypeIcao);
        ADD_LABEL(cfg.bAnyAcId,     statData.acId(key()));
        ADD_LABEL(cfg.bTranspCode,  key());
        ADD_LABEL(cfg.bReg,         statData.reg);
        ADD_LABEL(cfg.bIcaoOp,      statData.opIcao);
        ADD_LABEL(cfg.bCallSign,    statData.call);
        ADD_LABEL(cfg.bFlightNo,    statData.flight);
        ADD_LABEL(cfg.bRoute,       statData.route());
        
        // this is the config we did the label for
        labelCfg = cfg;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// produce a/c label
#define ADD_LABEL_NUM(b,num) if (b) { label += std::to_string(lround(num)); label += ' '; }
std::string LTFlightData::ComposeLabel() const
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

        // the configuration: which parts to include in the label?
        const DataRefs::LabelCfgTy cfg = dataRefs.GetLabelCfg();
        std::string label(labelStat);       // copy static parts
        
        // only possible if we have an aircraft
        if (pAc) {
            // current position of a/c
            const positionTy& pos = pAc->GetPPos();
            // add more items as per configuration
            if (cfg.bPhase) { label +=  pAc->GetFlightPhaseString(); trim(label); label += ' '; }
            ADD_LABEL_NUM(cfg.bHeading,     pos.heading());
            ADD_LABEL_NUM(cfg.bAlt,         pos.alt_ft());
            if (cfg.bHeightAGL) {
                label += pAc->IsOnGrnd() ? positionTy::GrndE2String(GND_ON) :
                           std::to_string(long(pAc->GetPHeight_ft()));
                trim(label);
                label += ' ';
            }
            ADD_LABEL_NUM(cfg.bSpeed,       pAc->GetSpeed_kt());
            ADD_LABEL_NUM(cfg.bVSI,         pAc->GetVSI_ft());
        }
        
        // remove the trailing space
        if (!label.empty())
            label.pop_back();
        
        return label;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return "?";
}


//
//MARK: Flight Data - Mutex-Controlled access Dynamic
//

// Data Cleansing of the buffered positions (called from CalcNextPos)
void LTFlightData::DataCleansing (bool& bChanged)
{
    // nothing to cleanse?
    if (posDeque.empty())
        return;
    
    // The flight model to use
    const LTAircraft::FlightModel& mdl = pAc ? pAc->mdl :
    LTAircraft::FlightModel::FindFlightModel(statData.acTypeIcao);

    // *** Keep last pos in posDeque above 2.5° ILS path
    // Relevant if:
    // - airborne
    // - descending
    if (posDeque.size() >= 2)
    {
        positionTy& last = posDeque.back();
        const positionTy& prev = *std::prev(posDeque.cend(),2);
        double terrain_alt_m = pAc ? pAc->GetTerrainAlt_m() : NAN;
        if (!std::isnan(last.alt_m()) &&            // do we have an altitude at all?
            last.alt_m() <= KEEP_ABOVE_MAX_ALT &&   // not way too high (this skips planes which are just cruising
            (std::isnan(terrain_alt_m) || (last.alt_m() - terrain_alt_m) < KEEP_ABOVE_MAX_AGL) && // pos not too high AGL
            prev.vsi_ft(last) < -mdl.VSI_STABLE)    // sinking considerably (this also skips taxiing on the ground as during taxiing we aren't sinking)
        {
            // Try to find a rwy this plane might be headed for
            // based on the last known position
            posRwy = LTAptFindRwy(mdl, last, prev.speed_m(last));
            if (posRwy.isNormal()) {            // found a suitable runway?
                // Now, with this runway, check/correct all previous positions
                for (positionTy& pos: posDeque) {
                    const double dist = DistLatLon(pos.lat(), pos.lon(),
                                                   posRwy.lat(), posRwy.lon());
                    // Are we flying below the 2.5° glidescope? ("- 0.5" to avoid rounding problems)
                    if (pos.alt_m() - posRwy.alt_m() < dist * KEEP_ABOVE_RATIO - 0.5) {
                        // Fix it!
                        const double old_alt_ft = pos.alt_ft();
                        pos.alt_m() = posRwy.alt_m() + dist * KEEP_ABOVE_RATIO;
                        pos.f.onGrnd = GND_OFF;            // we even lift ground positions into the air!
                        bChanged = true;
                        if (dataRefs.GetDebugAcPos(key())) {
                            LOG_MSG(logDEBUG, DBG_KEEP_ABOVE,
                                    old_alt_ft, pos.dbgTxt().c_str());
                        }
                    }
                }
            }
        }
    }
    
    //
    // *** Remove weird positions ***
    //
    // A position is 'weird', if
    // - VSI would be more than +/- 2 * mdl.VSI_INIT_CLIMB
    // - heading change would be more than
    if (( pAc && posDeque.size() >= 1) ||
        (!pAc && posDeque.size() >= 3))
    {
        positionTy pos1;
        vectorTy v2;
        double h1 = NAN;
        dequePositionTy::iterator iter = posDeque.begin();
        
        // position _before_ the first position in the deque
        if (pAc) {
            pos1 = pAc->GetToPos();
            h1 = pAc->GetTrack();       // and the heading towards pos1
            // if (still) the to-Pos is current iter pos then increment
            // (could be that plane's current 'to' is still the first
            //  in out queue)
            while (iter != posDeque.end() &&
                   pos1.cmp(*iter) == 0)
                ++iter;
        } else {
            // in this case we have at least 3 positions
            pos1 = *std::next(iter);
            vectorTy v1 = iter->between(pos1);
            h1 = v1.dist > SIMILAR_POS_DIST ?
            v1.angle : pos1.heading();
            std::advance(iter, 2);
        }
        
        // loop over (remaining) pos and verify their validity
        while (iter != posDeque.end())
        {
            // is pos not OK compared to previous one?
            if (!IsPosOK(pos1, *iter, &h1, &bChanged))
            {
                // is there any valid pos to go to after iter?
                bool bFoundValidNext = false;
                for (dequePositionTy::iterator next = std::next(iter);
                     next != posDeque.end();
                     ++next)
                {
                    double h2 = h1;
                    if (IsPosOK(pos1, *next, &h2, &bChanged))
                    {
                        bFoundValidNext = true;
//                        if constexpr (VERSION_BETA) {
//                            LOG_MSG(logDEBUG, "%s:   Valid next pos: %s",
//                                    keyDbg().c_str(),
//                                    next->dbgTxt().c_str() );
//                            LOG_MSG(logDEBUG, Positions2String().c_str() );
//                        }
                        // that means we need to remove all positions from
                        // 'iter' to _before_ next.
                        // BUT because std::deque::erase can invalidate _all_ iterators
                        // (including next and cend!) we cannot do it in one go...after the first erase all iterators are invalid.
                        // Make use of the fact that the deque is sorted by timestamp.
                        const double rmTsFrom = iter->ts();
                        const double rmTsTo = next->ts();
                        while (iter != posDeque.end() && iter->ts() < rmTsTo)
                        {
                            // if current iter falls into the to-be-deleted range
                            if (rmTsFrom <= iter->ts() && iter->ts() < rmTsTo) {
                                posDeque.erase(iter);               // now all iterators are invalid!
                                bChanged = true;
                                iter = posDeque.begin();
                            }
                            else {
                                // otherwise just try next
                                ++iter;
                            }
                        }
                        
                        // break out of search loop
                        // (iter now points to where 'next' was before, but with updated iterators)
                        LOG_ASSERT_FD(*this, iter != posDeque.end() && dequal(iter->ts(),rmTsTo));
                        break;
                    } // if found valid next pos
                } // for searching valid next pos
                
                // did we find nothing???
                if (!bFoundValidNext)
                {
                    // Well...let's hope some better data is coming later
                    // from the network -> stop cleansing here.
                    
                    // There is just one thing we want to avoid:
                    // That the non-continguous data is the next to be picked
                    // by the aircraft. That would make rocket/sliding
                    // planes.
                    if (pAc && iter == posDeque.begin()) {
                        // The incontiguous data is right the next one
                        // to be picked...from here we have 2 choices:
                        // - remove the plane (if there are at least 2 pos
                        //   in the deque as we assume that this starts a
                        //   new stretch of continuous data)
                        // - remove the data (if this is the only pos in the
                        //   deque as a single pos wouldn't allow the plane
                        //   to reappear)
                        if (posDeque.size() <= 1) {
                            // that's the only pos: remove it
                            if (dataRefs.GetDebugAcPos(key())) {
                                LOG_MSG(logDEBUG, DBG_INV_POS_REMOVED,
                                        keyDbg().c_str(),
                                        iter->dbgTxt().c_str());
                            }
                            posDeque.clear();
                            iter = posDeque.end();
                            bChanged = true;
                            if (dataRefs.GetDebugAcPos(key()))
                                LOG_MSG(logDEBUG,DBG_NO_MORE_POS_DATA,Positions2String().c_str());
                        } else {
                            // there are at least 2 pos in the deque
                            // -> remove the aircraft, will be recreated at
                            //    the new pos later automatically
                            pAc->SetInvalid();
//                            if constexpr (VERSION_BETA) {
//                                LOG_MSG(logDEBUG, Positions2String().c_str());
//                            }
                            LOG_MSG(logDEBUG, DBG_INV_POS_AC_REMOVED,
                                    keyDbg().c_str());
                        }
                    }
                    
                    // stop cleansing
                    break;
                } // if not found any valid next position
//                else if constexpr (VERSION_BETA) {
//                    LOG_MSG(logDEBUG, Positions2String().c_str() );
//                }
            } // if invalid pos
            else
            {
                // just move on to next position in deque
                h1 = v2.angle;
                pos1 = *iter;
                ++iter;
            }
        } // inner while loop over positions
    } // outer if of data cleansing
    
    // *** Hovering-along-the-runway detection ***
    
    // RealTraffic's data sometimes has the issue that after approach
    // a plane does not touch down but instead there is actual tracking
    // data that lets the plane fly along the runway a few dozen feet
    // above ground. Looks like calculated predictive data for
    // case of missing ADS-B data... But prevents LiveTraffic from
    // just using its autoland feature, which would look a lot better.
    // So let's remove that hovering stuff
    
    const LTChannel* pChn = nullptr;
    if (pAc && !posDeque.empty() &&
        FPH_APPROACH <= pAc->GetFlightPhase() &&
        pAc->GetFlightPhase() < FPH_LANDING &&
        GetCurrChannel(pChn) && pChn->DoHoverDetection())
    {
        // We have a plane which is in approach.
        const double maxHoverAlt_m = pAc->GetTerrainAlt_m() + (MAX_HOVER_AGL * M_per_FT);
        
        // What we now search for is data at level altitude following a descend.
        // So we follow our positions as long as they are descending.
        // Then we remove all data which is hovering at level altitude
        // some few dozen feet above ground.
        
        // this increments iter as long as the next pos is descending
        positionTy prevPos = pAc->GetToPos();       // we start comparing with current 'to'-pos of aircraft
        dequePositionTy::const_iterator iter;
        for (iter = posDeque.cbegin();              // start at the beginning
             
             iter != posDeque.cend() &&             // it's not yet the end, AND
             !iter->IsOnGnd() &&                    // not on ground, AND
             iter->vsi_ft(prevPos) < -mdl.VSI_STABLE; // descending considerably
             
             prevPos = *iter++ );                   // increment
        
        // 'prevPos' now is the last pos of the descend and will no longer change
        // 'iter' points to the first pos _after_ descend
        // and is the first deletion candidate.
        // Delete all positions hovering above the runway.
        while (iter != posDeque.cend() &&                         // not the end,
               !iter->IsOnGnd() &&                                // between ground and
               iter->alt_m() < maxHoverAlt_m &&                   // max hover altitude
               std::abs(iter->vsi_ft(prevPos)) <= mdl.VSI_STABLE) // and flying level
        {
            // remove that hovering position
            if (dataRefs.GetDebugAcPos(key())) {
                LOG_MSG(logDEBUG, DBG_HOVER_POS_REMOVED,
                        keyDbg().c_str(),
                        iter->dbgTxt().c_str());
            }
            iter = posDeque.erase(iter);        // erase and returns element thereafter
            bChanged = true;
        }
    }
}

// Smoothing data means:
// We change timestamps(!) of tracking data in order to have
// speed change smoothly.
// This is particularly necessary if position's timestamps aren't
// reliable as speed is a function of
// distance (between positions, which are assumed reliable) and
// time (between timestamps, which in _this_ function are assumed unreliable).
// Introduced with RealTraffic, which doesn't transmit the position's timestamp,
// hence timestamps are unreliable between [ts-10s;ts].
void LTFlightData::DataSmoothing (bool& bChanged)
{
    double gndRange = 0.0;
    double airbRange = 0.0;
    
    // shall we do data smoothing at all?
    const LTChannel* pChn = nullptr;
    if (!GetCurrChannel(pChn) || !pChn->DoDataSmoothing(gndRange,airbRange))
        return;
    
    // find first and last positions for smoothing
    const positionTy& posFirst = posDeque[0];
    const double tsRange = posFirst.IsOnGnd() ? gndRange : airbRange;
    dequePositionTy::iterator itLast = posDeque.begin();
    for (++itLast; itLast != posDeque.end(); ++itLast) {
        // there are various 'stop' conditions
        //  most important: leaving allowed smoothing range (in seconds)
        if (itLast->ts() - posFirst.ts() > tsRange  ||
            // don't smooth across gnd status changes
            itLast->f.onGrnd != posFirst.f.onGrnd       ||
            // don't smooth across artifically calculated positions
            itLast->f.flightPhase != FPH_UNKNOWN)
            break;
    }
    // we went one too far...so how far did we go into the deque?
    --itLast;
    const size_t numPos = std::distance(posDeque.begin(), itLast);
    
    // not far enough for any smoothing?
    if (numPos < 2)
        return;
    
    // what is the total distance travelled between first and last?
    // (to take curves into account we need to sum up individual distances)
    double dist = 0.0;
    dequePositionTy::iterator itPrev = posDeque.begin();        // previous pos
    for (dequePositionTy::iterator it = std::next(itPrev);      // next pos
         itPrev != itLast;
         ++it, ++itPrev)
    {
        dist += itPrev->dist(*it);                              // distance between prev and next
    }
    const double totTime = itLast->ts() - posFirst.ts();
    // sanity check: some reasonable time
    if (totTime < 1.0)
        return;
    // avg speed:
    const double speed = dist / totTime;
    // sanity check: some reasonable speed to avoid INF and NAN values
    if (speed < 1.0)
        return;

    // all positions between first and last are now to be moved in a way
    // that the speed stays constant in all segments
    itPrev = posDeque.begin();
    for (dequePositionTy::iterator it = std::next(itPrev);
         it != itLast;
         ++it, ++itPrev)
    {
        // speed is constant, but distances differs from leg to leg
        // and, thus, determines time difference:
        it->ts() = itPrev->ts() + itPrev->dist(*it) / speed;
    }
    
    // If previously there where two (or more) positions with the exact same
    // position but different timestamps then these positions now have the very
    // same timestamp. (Distance between them is 0, with the above calculation
    // time difference now is also 0.) We must remove these duplicates:
    dequePositionTy::iterator dup;
    while ((dup = std::adjacent_find(posDeque.begin(), posDeque.end(),
                                     // find two adjacent positions with same timestamp:
                                     [](const positionTy& a, const positionTy& b){return dequal(a.ts(),b.ts());})) != posDeque.end())
    {
        posDeque.erase(dup);
    }
    
    // so we changed data
    bChanged = true;
}

// shift ground positions to taxiways, insert positions at taxiway nodes
void LTFlightData::SnapToTaxiways (bool& bChanged)
{
    // Not enabled at all? (Or no positions at all?)
    if (dataRefs.GetFdSnapTaxiDist_m() <= 0 ||
        posDeque.empty())
        return;
    
    // Loop over position in the deque
    dequePositionTy::iterator iter = posDeque.begin();
    while (iter != posDeque.end())
    {
        // Only act on positions on the ground,
        // which have (not yet) been artificially added
        positionTy& pos = *iter;
        if (pos.IsOnGnd() && !pos.IsPostProcessed())
        {
            // Try snapping to a rwy or taxiway
            if (LTAptSnap(*this, iter, true))
                bChanged = true;
        } // non-artificial ground position
        
        // move on to next
        ++iter;
    } // while all posDeque positions
}


// based on buffered positions calculate the next position to fly to
// (usually called in a separate thread via TriggerCalcNewPos,
//  with 'simTime' slightly [~0.5s] into the future,
//  called by LTAircraft shortly before running out of positions and
//  calling TryFetchNewPos)
//
// simTime should only be set when called from LTAircraft,
// others should pass in NAN.
bool LTFlightData::CalcNextPos ( double simTime )
{
    bool bChanged = false;          // change any positions?
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // *** maintenance of flight data deque ***
        const LTAircraft::FlightModel& mdl = pAc ? pAc->mdl :
        LTAircraft::FlightModel::FindFlightModel(statData.acTypeIcao);

        // if no simTime given use a/c's 'to' position, or current sim time
        if (std::isnan(simTime)) {
            if (pAc)
                simTime = pAc->GetToPos().ts();
            else
                simTime = dataRefs.GetSimTime();
        }

        // remove from front until [0] <= simTime < [1] (or just one element left)
        while (dynDataDeque.size() >= 2 && dynDataDeque[1].ts <= simTime)
            dynDataDeque.pop_front();
        
        // *** Maintenance of positions queue ***
        
        // *** Data Smoothing ***
        // (potentially changes timestamp, so needs to be befure
        //  maintenance, which relies on timestamps)
        DataSmoothing(bChanged);
        
        // *** maintenance of buffered positions ***
        
        // Differs depending on: is there an a/c yet?
        if ( pAc ) {
            // if there is an a/c then we just remove all positions before 'simTime'
            while (!posDeque.empty() && posDeque.front().ts() <= simTime + 0.05f) {
                posDeque.pop_front();
                bChanged = true;
            }
            
            // no positions left?
            if (posDeque.empty()) {
                // If descending: Try finding a runway to land on
                if (pAc->GetVSI_ft() < -pAc->mdl.VSI_STABLE)
                {
                    const positionTy& acTo = pAc->GetToPos();
                    posRwy = LTAptFindRwy(*pAc, dataRefs.GetDebugAcPos(key()));
                    if (posRwy.isNormal()) {
                        // found a landing spot!
                        // If it is 'far' away in terms of time then we don't add it
                        // directly...maybe the channel wakes up and gives us
                        // real data that we don't want to miss.
                        // So we add only a part of the way to the rwy.
                        vectorTy vecRwy = acTo.between(posRwy);

                        // At most we travel as far as a refresh interval takes us
                        // and never too close to the touch-down point...
                        // we need room for the alignment point with the runway
                        const double d_ts = posRwy.ts() - simTime;
                        if (d_ts > (double)dataRefs.GetFdRefreshIntvl() &&
                            vecRwy.dist > 3 * ART_RWY_ALIGN_DIST)
                        {
                            // shorten the distance so it only takes as long as a refresh interval
                            vecRwy.dist *= (double)dataRefs.GetFdRefreshIntvl() / d_ts;
                            positionTy posInterm = acTo + vecRwy;
                            posInterm.f.flightPhase = FPH_APPROACH;
                            // Add the it to the queue
                            if (dataRefs.GetDebugAcPos(key()))
                                LOG_MSG(logDEBUG, "%s: Added intermediate %s",
                                        keyDbg().c_str(),
                                        std::string(posInterm).c_str());
                            posDeque.emplace_back(std::move(posInterm));
                        } else {
                            // The final leg down onto the runway.
                            // Little trick here: We add 2 stops to make sure
                            // that latest shortly before touching down we
                            // are fully aligned with the runway.
                            // Rwy heading is given in posRwy.heading().
                            positionTy posBefore =
                            posRwy + vectorTy(std::fmod(posRwy.heading() + 180.0, 360.0),   // angle (reversed!)
                                              ART_RWY_ALIGN_DIST,                           // distance
                                              -vecRwy.vsi,                                  // VSI (reversed!)
                                              std::min(vecRwy.speed,                        // speed (capped at max final speed)
                                                       pAc->mdl.FLAPS_DOWN_SPEED * ART_FINAL_SPEED_F / KT_per_M_per_S));
                            // Timestamp is now beyond posRwy.ts() as time always moves forward,
                            // but posBefore is _before_ posRwy:
                            posBefore.ts() -= 2 * (posBefore.ts() - posRwy.ts());
                            posBefore.pitch() = 0.0;
                            posBefore.f.onGrnd = GND_OFF;
                            posBefore.f.flightPhase = FPH_FINAL;
                            
                            // Add both position to the queue
                            if (dataRefs.GetDebugAcPos(key()))
                                LOG_MSG(logDEBUG, "%s: Added final %s",
                                        keyDbg().c_str(),
                                        std::string(posBefore).c_str());
                            posDeque.emplace_back(std::move(posBefore));
                            if (dataRefs.GetDebugAcPos(key()))
                                LOG_MSG(logDEBUG, "%s: Added touch-down %s",
                                        keyDbg().c_str(),
                                        std::string(posRwy).c_str());
                            posDeque.push_back(posRwy);     // make a copy, we want to keep posRwy!
                        }
                        bChanged = true;
                    }
                }
                // No more positions on the ground: Make the a/c stop
                // by adding the last known position just once again as artifical stop.
                else if (pAc->IsOnGrnd()) {
                    positionTy stopPos = pAc->GetToPos();
                    if (stopPos.IsOnGnd() &&
                        stopPos.f.flightPhase != FPH_TOUCH_DOWN &&      // don't copy touch down pos, that looks ugly, and hinders auto-land/stop
                        stopPos.f.flightPhase != FPH_STOPPED_ON_RWY)    // avoid adding several stops
                    {
                        stopPos.ts() = simTime + 10.0;                  // just assume _some_ time
                        stopPos.f.flightPhase = FPH_STOPPED_ON_RWY;     // indicator for aritifical stop (not only on rwy now...)
                        if (dataRefs.GetDebugAcPos(key()))
                            LOG_MSG(logDEBUG, "%s: Added stop-position %s",
                                    keyDbg().c_str(),
                                    std::string(stopPos).c_str());
                        posDeque.emplace_back(std::move(stopPos));      // add it to the deque
                        bChanged = true;
                    }
                }

                // still no positions left?
                if (posDeque.empty())
                {
                    if (dataRefs.GetDebugAcPos(key()))
                        LOG_MSG(logDEBUG,DBG_NO_MORE_POS_DATA,Positions2String().c_str());
                    return false;
                }
            }
        } else {
            // If there is no a/c yet then we need one past and
            // one or more future positions
            // If already the first pos is in the future then we aren't valid yet
            if (posDeque.size() < 2 || simTime < posDeque.front().ts())
                return false;
        }
        
        // *** Data Cleansing ***
        DataCleansing(bChanged);
        
        // *** Snap to taxiways ***
        SnapToTaxiways(bChanged);

#ifdef DEBUG
        std::string deb0   ( !posDeque.empty() ? posDeque.front().dbgTxt() : "<none>" );
        std::string deb1   ( posDeque.size() >= 2 ? std::string(posDeque[1].dbgTxt()) : "<none>" );
        std::string debvec ( posDeque.size() >= 2 ? std::string(posDeque.front().between(posDeque[1])) : "<none>" );
#endif
        
        // *** Landing / Take-Off Detection ***
        
        if ( pAc && !posDeque.empty() ) {
            // clear outdated rotate timestamp
            if (!std::isnan(rotateTS) && (rotateTS + 2 * mdl.ROTATE_TIME < simTime) )
                rotateTS = NAN;
            
            // *** Landing ***
            
            // If current pos is in the air and next pos is approaching or touching ground
            // then we have live positional data on the ground.
            // However, if we would fly directly to next pos then we would touch down
            // then at next pos only (earliest), which might be a point far down the runway.
            // To simulate touching down at the _beginning_ of the runway and
            // then rolling out to (or through) next pos we determine this case
            // and then insert an artifical touch down position, which just keeps going with
            // previous vsi and speed down to the ground.
            const positionTy& toPos_ac = pAc->GetToPos();   // a/c's current to-position
            positionTy& next = posDeque.front();            // next pos waiting in posDeque

            if (!toPos_ac.IsOnGnd() &&                      // currently not heading for ground
                next.IsOnGnd() &&                           // future: on ground
                pAc->GetVSI_ft() < -mdl.VSI_STABLE) {       // right now descending considerably
                // Case determined: We are landing and have live positional
                //                  data down the runway
                const double descendAlt      = toPos_ac.alt_m() - next.alt_m(); // height to sink
                const double timeToTouchDown = descendAlt / -pAc->GetVSI_m_s(); // time to sink
                const double tsOfTouchDown   = toPos_ac.ts() + timeToTouchDown; // when to touch down
                // but only reasonably a _new_ position if between to pos and next
                // with some minima distance
                if (timeToTouchDown > TIME_REQU_POS &&
                    tsOfTouchDown + TIME_REQU_POS < next.ts())
                {
                    vectorTy vecTouch(toPos_ac.angle(next),                      // angle: as per last leg
                                      timeToTouchDown * pAc->GetSpeed_m_s(),     // distance
                                      pAc->GetVSI_m_s(),                         // vsi
                                      pAc->GetSpeed_m_s());                      // speed
                    
                    // insert touch-down point at beginning of posDeque
                    positionTy& touchDownPos = posDeque.emplace_front(toPos_ac.destPos(vecTouch));
                    touchDownPos.f.onGrnd = GND_ON;
                    touchDownPos.f.flightPhase = FPH_TOUCH_DOWN;
                    touchDownPos.alt_m() = NAN;          // will set correct terrain altitude during TryFetchNewPos
                    
                    // Snap the touch down pos to the rwy:
                    dequePositionTy::iterator iter = posDeque.begin();
                    LTAptSnap(*this, iter, false);
                    
                    // output debug info on request
                    if (dataRefs.GetDebugAcPos(key())) {
                        LOG_MSG(logDEBUG,DBG_INVENTED_TD_POS,touchDownPos.dbgTxt().c_str());
                    }
                }
                else
                {
                    // not enough distance to 'next', so we declare 'next' the landing spot
                    next.f.flightPhase = FPH_TOUCH_DOWN;
                }
                    
                // Remove positions down the runway until the last RWY position
                // That allows for a better deceleration simulation.
                while (posDeque.size() > 2 &&       // keep at least two positions
                       posDeque[1].IsOnGnd() &&
                       posDeque[1].f.specialPos == SPOS_RWY &&
                       posDeque[2].f.specialPos == SPOS_RWY)
                {
                    // remove the second element (first is the just inserted touch-down pos)
                    posDeque.erase(std::next(posDeque.begin()));
                }
                
                // do Data Cleansing again, just to be sure the new
                // position does not screw up our flight path
                DataCleansing(bChanged);
                bChanged = true;
            } // (landing case)
            
            // *** Take Off ***
            
            // Similar issue as with landing, just reverse:
            // A/c is to take off (rotate and leave the runway) somewhere
            // between an on-ground position and an off-ground position.
            // We need to stick to the ground till that point. Where is that point?
            // If we'd just follow the known flight data then we likely
            // 'climb' with 100-200 ft/min from the start of the runway (A)
            // slowly to some point down the runway and only slightly in the air (B)
            // and then only change to initial climbing rate to a point
            // higher up in the air (C).
            // What we want is to stick to the ground, accelerate and leave
            // shortly before (B) at the right angle to extend climbing right
            // away to (C). (B)-(C) is a path really flown while (A)-(B)
            // is not 'flown' directly, but first there's the take-off-roll
            // followed by rotating and taking off.
            // So we extend the vector (B)-(C) _backwards_ to find the
            // take-off-point, achieved using vsi of (B)-(C).
            // Point of rotate is 3s earlier.
            
            // for the take off case we look further ahead to catch the case
            // that a data point is right between start of rotating and actual lift off
            else for (size_t i = 0;
                      std::isnan(rotateTS) && posDeque.size() >= i+1;
                      i++ )
            {
                // i == 0 is as above with actual a/c present position
                // in later runs we use future data from our queue
                const positionTy& ppos_i  = i == 0 ? pAc->GetPPos() : posDeque[i-1];
                positionTy& to_i          = posDeque[i];
                const double to_i_ts      = to_i.ts();  // the reference might become invalid later once we start erasing, so we copy this timestamp that we need
                
                // we look up to 60s into the future
                if (ppos_i.ts() > simTime + MDL_TO_LOOK_AHEAD)
                    break;
                
                // Now: is there a change from on-ground to off-ground?
                if (ppos_i.IsOnGnd() && !to_i.IsOnGnd())
                {
                    // direct vector from (A)-(B), in which the take off happens:
                    const vectorTy vec (ppos_i.between(to_i));
                    
                    // Get the vsi and speed after 'to' (the (B)-(C) vector's)
                    double climbVsi = mdl.VSI_INIT_CLIMB * Ms_per_FTm;
                    double climbSpeed = mdl.SPEED_INIT_CLIMB / KT_per_M_per_S;
                    if (posDeque.size() >= i+2) {     // take the data from the vector _after_ to
                        vectorTy climbVec (to_i.between(posDeque[i+1]));
                        climbVsi = climbVec.vsi;
                        climbSpeed = climbVec.speed;
                    }
                    
                    // Determine how much before 'to' is that take-off point
                    // We assume ppos_i, which is ON_GND, has good terrain alt
                    const double toTerrAlt = ppos_i.alt_m();
                    const double height_m = to_i.alt_m() - toTerrAlt; // height to climb to reach 'to'?
                    const double toClimb_s = height_m / climbVsi;   // how long to climb to reach 'to'?
                    const double takeOffTS = to_i.ts() - toClimb_s;   // timestamp at which to start the climb, i.e. take off
                    
                    // Continue only for timestamps in the future,
                    // i.e. if take off is calculated to be after current to-position
                    if (pAc->GetToPos().ts() < takeOffTS)
                    {
                        rotateTS = takeOffTS - mdl.ROTATE_TIME;         // timestamp when to rotate

                        // find the TO position by applying a reverse vector to the pointer _after_ take off
                        vectorTy vecTO(fmod(vec.angle + 180, 360),  // angle (reverse!)
                                       climbSpeed * toClimb_s,      // distance
                                       -climbVsi,                   // vsi (reverse!)
                                       climbSpeed);                 // speed
                        // insert take-off point ('to' minus vector from take-off to 'to')
                        // at beginning of posDeque
                        positionTy takeOffPos = to_i.destPos(vecTO);
                        takeOffPos.f.onGrnd = GND_ON;
                        takeOffPos.f.flightPhase = FPH_LIFT_OFF;
                        takeOffPos.alt_m() = NAN;                   // TryFetchNewPos will calc terrain altitude
                        takeOffPos.heading() = vec.angle;           // from 'reverse' back to forward
                        takeOffPos.ts() = takeOffTS;                // ts was computed forward...we need it backward
                        
                        // find insert position, remove on-runway positions along the way
                        bool bDelRwyPos = false;
                        dequePositionTy::iterator toIter = posDeque.end();
                        for (dequePositionTy::iterator iter = posDeque.begin();
                             iter != posDeque.end();
                             )
                        {
                            // before take off...
                            if (*iter < takeOffPos) {
                                // Keep the first RWY position, but remove any later RWY positions,
                                // Which allows the accelerate algorithm to accelerate all the distance to take-off point
                                if (iter->f.specialPos == SPOS_RWY) {
                                    if (bDelRwyPos) {
                                        iter = posDeque.erase(iter);
                                        continue;               // start over loop with next element after the erased one
                                    }
                                    bDelRwyPos = true;          // any further RWY positions can be deleted
                                }
                                
                                // before take off we stay on the ground
                                if (!iter->IsOnGnd()) {
                                    iter->f.onGrnd = GND_ON;
                                    iter->alt_m() = NAN;            // TryFetchNewPos will calc terrain altitude
                                }
                            } else {
                                // found insert position! Insert and snap it to the rwy
                                toIter = posDeque.insert(iter, takeOffPos);
                                break;
                            }
                            
                            ++iter;
                        }
                        
                        // found no insert position??? need to add it to the end
                        if (toIter == posDeque.end())
                            posDeque.push_back(takeOffPos);
                        else
                        {
                            // we did find an insert position
                            // we now also remove everything between this
                            // inserted take-off position and the first
                            // in-flight position, which is to_i.
                            // (This can remove ppos_i!)
                            dequePositionTy::iterator rmIter = posDeque.begin();
                            // but runs only until first in-flight position (to_i, we saved its timestamp)
                            while (rmIter != posDeque.end() && rmIter->ts() < to_i_ts)
                            {
                                // skip positions before and including take off pos
                                if (rmIter->ts() <= takeOffTS + 0.001)
                                    rmIter++;
                                else
                                    // a position after take off but before in-flight is to be removed
                                    rmIter = posDeque.erase(rmIter);
                            }
                        }
                        
                        // output debug info on request
                        if (dataRefs.GetDebugAcPos(key())) {
                            LOG_MSG(logDEBUG,DBG_INVENTED_TO_POS,takeOffPos.dbgTxt().c_str());
                        }
                        bChanged = true;
                        
                        // do Data Cleansing again, just to be sure the new
                        // position does not screw up our flight path
                        DataCleansing(bChanged);

                        // leave loop of szenarios
                        break;
                    }
                } // (take off case)
            } // loop over szenarios
        } // (has a/c and do landing / take-off detection)
        
        // *** Snap any newly inserted positions to taxiways ***
        if (bChanged)
            SnapToTaxiways(bChanged);
        
        // A lot might have changed now, even added.
        // If there is no aircraft yet then we need to "normalize"
        // to creation conditions: One pos in the past, the next in the future
        if ( !pAc )
        {
            // The first pos is in the past, good, make sure it's the only one
            // [0] <= simTime < [1]
            while (posDeque.size() >= 2 && posDeque[1].ts() <= simTime) {
                posDeque.pop_front();
                bChanged = true;
            }
            
            // Unlikely, but theoretically there could now be just one (past) pos left
            if (posDeque.size() < 2)
                return false;
        }
        
        // if something changed
        if (bChanged) {
            // recalc all headings
            for (dequePositionTy::iterator iter = posDeque.begin();
                 iter != posDeque.end();
                 ++iter)
                CalcHeading(iter);
            
            // output all positional information as debug info on request
            if (dataRefs.GetDebugAcPos(key())) {
                LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
            }
        }
        
        // posDeque should still be sorted, i.e. no two adjacent positions a,b should be a > b
        LOG_ASSERT_FD(*this,
                      std::adjacent_find(posDeque.cbegin(), posDeque.cend(),
                                         [](const positionTy& a, const positionTy& b)
                                         {return a > b;}
                                         ) == posDeque.cend());
        
        // success
        return true;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return false;
}

// the mutex used to synch access to the list of keys which await pos calculation
std::mutex calcNextPosListMutex;
// and that list of pairs <key,simTime>
typedef std::pair<LTFlightData::FDKeyTy,double> keyTimePairTy;
typedef std::deque<keyTimePairTy> dequeKeyTimeTy;
dequeKeyTimeTy dequeKeyPosCalc;

// The main function for the position calculation thread
// It receives keys to work on in the dequeKeyPosCalc list and calls
// the CalcNextPos function on the respective flight data objects
void LTFlightData::CalcNextPosMain ()
{
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_CalcPos");

    // loop till said to stop
    while ( !bFDMainStop ) {
        keyTimePairTy pair;
        
        // thread-safely access the list of keys to fetch one for processing
        try {
            std::lock_guard<std::mutex> lock (calcNextPosListMutex);
            if ( !dequeKeyPosCalc.empty() ) {   // something's in the list, take it
                pair = dequeKeyPosCalc.front();
                dequeKeyPosCalc.pop_front();
            }
        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "CalcNextPosMain", e.what());
            pair = keyTimePairTy();
        }
        
        // there was something in the list to process? Do so!
        if (!pair.first.empty()) {
            try {
                // find the flight data object in the map and calc position
                LTFlightData& fd = mapFd.at(pair.first);
                
                // LiveTraffic Top Level Exception Handling:
                // CalcNextPos can cause exceptions. If so make fd object invalid and ignore it
                try {
                    if (fd.IsValid())
                        fd.CalcNextPos(pair.second);
                } catch (const std::exception& e) {
                    LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
                    fd.SetInvalid();
                } catch (...) {
                    fd.SetInvalid();
                }
                
            } catch(const std::out_of_range&) {
                // just ignore exception...fd object might have gone in the meantime
            }
        }
            
        // sleep till woken up for processing or stopping
        {
            std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
            FDThreadSynchCV.wait(lk, []{return bFDMainStop || !dequeKeyPosCalc.empty();});
            lk.unlock();
        }
    }
}

// Add a new key to the list of positions to calculate
// and wake up the calculation thread
void LTFlightData::TriggerCalcNewPos ( double simTime )
{
    // thread-safely add the key to the list and start the calc thread
    try {
        std::lock_guard<std::mutex> lock (calcNextPosListMutex);
        
        // search for key in the list, if already included update simTime and return
        for (keyTimePairTy &i: dequeKeyPosCalc)
            if(i.first==key()) {
                i.second = fmax(simTime,i.second);   // update simTime to latest
                return;
            }
        
        // not in list, so add to list of keys to calculate including simTime
        dequeKeyPosCalc.emplace_back(key(),simTime);
        
        // trigger the calc thread to wake up
        FDThreadSynchCV.notify_all();
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "TriggerCalcNewPos", e.what());
    }
}


// calc heading from positions in a positionList around a given position (it)
// if there is only (it), then use heading from flight data
// if there are 2 positions then return heading from first to second
// if there are 3 positions, then calc both headings (from 1st to 2nd, from 2nd to 3rd)
//                           and return its average
//         ====>        <- this is the return value if 3 positions exist
//         P_it                p1   -> p_it =  30°
//          ^ \                p_it -> p3   = 150°
//         /   _|              return       = (30° + 150°) / 2 = 90°
//        /     P3
//       P1
//
// (Thinking behind it: By the time we reach P2 we should be flying a curve
//  and should be at heading as returned -- before changing course to P3)
//
// Also consider the distance between points: a distance less than 5m is
// considered "no point" for the purpose of this calculation.
// Such short distances will be seen when a/c stops on the gound:
// Very similar positions next to each other, which we don't want the entire
// a/c to turn to. Instead, favor the longer vector, or, if none of the
// vectors is long enough, fall back to the heading as reported in the
// flight data.
void LTFlightData::CalcHeading (dequePositionTy::iterator it)
{
    // skip any fiddling with the heading in case it is fixed
    if (it->f.bHeadFixed)
        return;
    
    // vectors to / from the position at "it"
    vectorTy vecTo, vecFrom;
    
    // is there a predecessor to "it"?
    if (it != posDeque.cbegin()) {
        const positionTy& prePos = *std::prev(it);
        vecTo = prePos.between(*it);
        if (vecTo.dist < SIMILAR_POS_DIST)      // distance from predecessor to it too short
        {
            it->heading() = prePos.heading();   // by default don't change heading for this short distance to avoid turning planes "on the spot"
            if (!std::isnan(it->heading()))     // if we now have a heading -> just use it
                return;
            vecTo = vectorTy();                 // clear the vector
        }
    } else if (pAc) {
        // no predecessor in the queue...but there is an a/c, take that
        const positionTy& prePos = pAc->GetToPos();
        vecTo = prePos.between(*it);
        if (vecTo.dist < SIMILAR_POS_DIST)      // distance from predecessor to it too short
        {
            it->heading() = prePos.heading();   // by default don't change heading for this short distance to avoid turning planes "on the spot"
            if (!std::isnan(it->heading()))     // if we now have a heading -> just use it
                return;
            vecTo = vectorTy();                 // clear the vector
        }
    }
    
    // is there a successor to it?
    if (std::next(it) != posDeque.cend()) {
        vecFrom = it->between(*std::next(it));
        if (vecFrom.dist < SIMILAR_POS_DIST)    // clear the vector if too short
            vecFrom = vectorTy();
    }
    
    // if both vectors are available return the average between both angles
    if (!std::isnan(vecTo.angle) && !std::isnan(vecFrom.angle))
        // with the linear factor in favor of the _shorter_ vector
        // (Idea: we have more time to turn on the longer side, so at the junction
        //        the heading should be closer to the shorter vector's heading)
        it->heading() = HeadingAvg(vecTo.angle,
                                   vecFrom.angle,
                                   vecFrom.dist,
                                   vecTo.dist);
    // if just one vector is available take that one
    else if (!std::isnan(vecFrom.angle))
        it->heading() = vecFrom.angle;
    else if (!std::isnan(vecTo.angle))
        it->heading() = vecTo.angle;
    // no calculated vector available...if the position came with some heading leave it untouched
    // else we fall back to the heading delivered by the flight data
    else if (std::isnan(it->heading())) {
        FDDynamicData *pBefore = nullptr, *pAfter = nullptr;
        dequeFDDynFindAdjacentTS(it->ts(), pBefore, pAfter);
        // get the best heading out of it
        if (pAfter && pBefore)
            it->heading() = HeadingAvg(pBefore->heading,
                                       pAfter->heading,
                                       pAfter->ts - it->ts(),   // factor "before" higher if close (and after is further away!)
                                       it->ts() - pBefore->ts); // factor "after" higher if close (and before is further away!)
        else if (pBefore)
            it->heading() = pBefore->heading;
        else if (pAfter)
            it->heading() = pAfter->heading;
        else
            it->heading() = 0;
    }
    
    // just as a safeguard...they can't be many situations this triggers,
    // but we don't want nan values any longer after this
    if (std::isnan(it->heading()))
        it->heading() = 0;
}

// check if thisPos would be OK after lastPos,
// pHeading: if given overrides lastPos.Heading()
//           if NAN, then no check for heading
bool LTFlightData::IsPosOK (const positionTy& lastPos,
                            const positionTy& thisPos,
                            double* pHeading,
                            bool* /*pbChanged*/)
{
    // aircraft model to use
    const LTAircraft::FlightModel& mdl = pAc ? pAc->mdl :
    LTAircraft::FlightModel::FindFlightModel(statData.acTypeIcao);
    // if pHeading not given we assume we can take it from lastPos
    const double lastHead = pHeading ? *pHeading : lastPos.heading();
    // vector from last to this
    const vectorTy v = lastPos.between(thisPos);
    if (pHeading) *pHeading = v.angle;      // return heading from lastPos to thisPos
    // maximum turn allowed depends on 'on ground' or not
    const double maxTurn = (thisPos.IsOnGnd() ?
                            MDL_MAX_TURN_GND : MDL_MAX_TURN);

    // angle between last and this, i.e. turn angle at thisPos
    const double hDiff = (std::isnan(lastHead) ? 0.0 :
                          lastPos.f.bHeadFixed || thisPos.f.bHeadFixed ? 0.0 :
                          v.dist <= SIMILAR_POS_DIST ? 0.0 :
                          HeadingDiff(lastHead, v.angle));
    
    // Too much of a turn? VSI/speed out of range?
    if (-maxTurn > hDiff || hDiff > maxTurn   ||
        v.vsi_ft() < -3 * mdl.VSI_INIT_CLIMB   ||
        v.vsi_ft() > 3 * mdl.VSI_INIT_CLIMB    ||
        (!std::isnan(v.speed_kn()) && v.speed_kn() > 4 * mdl.FLAPS_DOWN_SPEED) ||
        // too slow speed up in the air?
        ((!lastPos.IsOnGnd() || !thisPos.IsOnGnd()) &&
         !std::isnan(v.speed_kn()) && v.speed_kn() < mdl.MAX_TAXI_SPEED) )
    {
        if constexpr (VERSION_BETA) {
            LOG_MSG(logDEBUG, "%s: Invalid vector %s with headingDiff = %.0f",
                    keyDbg().c_str(),
                    std::string(v).c_str(), hDiff );
        }
        return false;
    }
    
    // all OK
    return true;
}

// adds a new position to the queue of positions to analyse
void LTFlightData::AddNewPos ( positionTy& pos )
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

        // if there is an a/c then we shall no longer add positions
        // before the current 'to' position of the a/c
        if (pAc && pos <= pAc->GetToPos()) {
            // pos is before or close to 'to'-position: don't add!
            if (dataRefs.GetDebugAcPos(key()))
                LOG_MSG(logDEBUG,DBG_SKIP_NEW_POS,pos.dbgTxt().c_str());
            return;
        }

        // add pos to the queue of data to be added
        // (we shall not do Y probes but need accurate GND info...)
        posToAdd.emplace_back(pos);
        flagNoNewPosToAdd.clear();

        if (dataRefs.GetDebugAcPos(key()))
            LOG_MSG(logDEBUG,DBG_ADDED_NEW_POS,pos.dbgTxt().c_str());
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// walks all flight data objects and works the posToAdd queue
// called from flight loop callback, i.e. from the main thread
void LTFlightData::AppendAllNewPos()
{
    // short-cut if nothing to do
    if (flagNoNewPosToAdd.test_and_set())
        return;

    // somewhere there is something to do
    // need access to flight data map
    try {
        std::unique_lock<std::mutex> lock (mapFdMutex, std::try_to_lock);
        if (!lock) {
            // couldn't get the lock right away
            // -> return, we don't want to hinder rendering
            flagNoNewPosToAdd.clear();      // but need to try again
            return;
        }
        
        // look all flight data objects and check for new data to analyse
        for (mapLTFlightDataTy::value_type& fdPair: mapFd) {
            LTFlightData& fd = fdPair.second;
            try {
                if (fd.IsValid())
                    fd.AppendNewPos();
            } catch (const std::exception& e) {
                LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
                fd.SetInvalid();
            } catch (...) {
                fd.SetInvalid();
            }
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFdMutex", e.what());
        flagNoNewPosToAdd.clear();
    }
}

// analyse and add new positional data
// called from AppendAllNewPos, i.e. from within flight loop callback
void LTFlightData::AppendNewPos()
{
    // short-cut if nothing to do...we dare doing that without lock
    if (posToAdd.empty())
        return;
    
    try {
        // access guarded by a mutex, but we don't wait (inside the flight loop)
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock);
        if (!lock) {
            flagNoNewPosToAdd.clear();          // need to try it again
            return;
        }
        
       // loop the positions to add
        while (!posToAdd.empty())
        {
            // take next pos from queue
            positionTy pos = posToAdd.front();
            posToAdd.pop_front();
            
            // *** ground status *** (plays a role in merge determination)
            // will set ground altitude if on ground
            TryDeriveGrndStatus(pos);
            
            // *** insert/merge position ***
            
            // based on timestamp find possible "similar" position
            // or insert position.
            // and if so merge with that position to avoid too many position in
            // a very short time frame, as that leads to zick-zack courses in a
            // matter of meters only as can happen when merging different data streams
            dequePositionTy::iterator i =   // first: find a merge partner
            std::find_if(posDeque.begin(), posDeque.end(),
                         [&pos](const positionTy& p){return p.canBeMergedWith(pos);});
            if (i != posDeque.end()) {      // found merge partner!
                // make sure we don't overlap with predecessor/successor position
                if (((i == posDeque.begin()) || (*std::prev(i) < pos)) &&
                    ((std::next(i) == posDeque.end()) || (*std::next(i) > pos)) &&
                    !i->IsPostProcessed())      // don't merge with optimized positions...rather throw the new one away
                {
                    *i |= pos;                  // merge them (if pos.heading is nan then i.heading prevails)
                    if (dataRefs.GetDebugAcPos(key()))
                        LOG_MSG(logDEBUG,DBG_MERGED_POS,pos.dbgTxt().c_str(),i->ts());
                }
                else
                {
                    // pos would overlap with surrounding positions
                    if (dataRefs.GetDebugAcPos(key()))
                        LOG_MSG(logDEBUG,DBG_SKIP_NEW_POS,pos.dbgTxt().c_str());
                    continue;                   // skip
                }
            }
            else
            {
                // second: find insert-before position
                i = std::find_if(posDeque.begin(), posDeque.end(),
                                 [&pos](const positionTy& p){return p > pos;});
                
                // *** Sanity Check if we have valid vectors already ***
                if (pAc || !posDeque.empty())
                {
                    // We only insert if we don't cause invalid vector
                    // for perfect heading calc we need the _vector_ before insert pos,
                    // and for that we need 2 positions before insert
                    const positionTy* pBefore = nullptr;
                    double heading = NAN;
                    size_t idx = std::distance(posDeque.begin(), i);
                    if (idx >= 2) {
                        pBefore = &(posDeque[idx-1]);
                        heading = posDeque[idx-2].angle(*pBefore);
                    } else if (idx == 1) {
                        pBefore = &(posDeque[0]);
                        if (pAc)
                            heading = pAc->GetToPos().angle(*pBefore);
                    } else if (pAc) {            // idx == 0 -> insert at beginning
                        pBefore = &(pAc->GetToPos());
                        heading = pAc->GetTrack();
                    }
 
                    // now check if OK
                    if (pBefore && !IsPosOK(*pBefore, pos, &heading)) {
                        if (dataRefs.GetDebugAcPos(key()))
                            LOG_MSG(logDEBUG,ERR_IGNORE_POS,keyDbg().c_str(),pos.dbgTxt().c_str());
                        continue;                   // skip
                    }
                }
                
                // pos is OK, add/insert it
                if (i == posDeque.end()) {      // new pos is to be added at end
                    posDeque.emplace_back(pos);
                    i = std::prev(posDeque.end());
                }
                else {                          // found real insert position: before i
                    i = posDeque.emplace(i, pos);
                }
            }
            
            // i now points to the inserted/merged element
            positionTy& p = *i;
            
            // *** heading ***
            
            // Recalc heading of adjacent positions: before p, p itself, and after p
            if (i != posDeque.begin())              // is there anything before i?
                CalcHeading(std::prev(i));
            
            CalcHeading(i);                         // i itself, latest here a nan heading is rectified
            
            if (std::next(i) != posDeque.end())     // is there anything after i?
                CalcHeading(std::next(i));
            
            // *** pitch ***
            // just a rough value, LTAircraft::CalcPPos takes care of the details
            if (p.IsOnGnd())
                p.pitch() = 0;
            else
                p.pitch() = 2;
            
            // *** roll ***
            // LTAircraft::CalcPPos takes care of the details
            p.roll() = 0;
            
            // *** last checks ***
            
            // should be fully valid position now
            LOG_ASSERT_FD(*this, p.isFullyValid());
        }
        
        // posDeque should be sorted, i.e. no two adjacent positions a,b should be a > b
        if constexpr (VERSION_BETA) {
            LOG_ASSERT_FD(*this,
                          std::adjacent_find(posDeque.cbegin(), posDeque.cend(),
                                             [](const positionTy& a, const positionTy& b)
                                             {return a > b;}
                                             ) == posDeque.cend());
        }
        
        // now the youngest timestamp is this one of the last known position:
        if (!posDeque.empty()) {
            youngestTS = posDeque.back().ts();
        
            // *** trigger recalc ***
            TriggerCalcNewPos(NAN);
        }
        
        // print all positional information as debug info on request
        if (dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// Called by a/c: reads available positions if lock available
LTFlightData::tryResult LTFlightData::TryFetchNewPos (dequePositionTy& acPosList,
                                                      double& _rotateTS)
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( !lock )                            // didn't get the lock -> return
            return TRY_NO_LOCK;
        
        // we are called from X-Plane's main thread,
        // so we take our chance to determine proper terrain altitudes
        for (positionTy& pos: posDeque) {
            if ((pos.IsOnGnd() && std::isnan(pos.alt_m())) ||    // GND_ON but alt unknown
                pos.f.onGrnd == GND_UNKNOWN) {    // GND_UNKNOWN
                TryDeriveGrndStatus(pos);
            }
        }
        
        // the very first call (i.e. FD doesn't even know the a/c's ptr yet)?
        if (!pAc) {
            // there must be two positions, one in the past, one in the future!
            LOG_ASSERT_FD(*this, validForAcCreate());
            // move the first two positions to the a/c, so that the a/c can start flying from/to
            acPosList.emplace_back(std::move(posDeque.front()));
            posDeque.pop_front();
            acPosList.emplace_back(std::move(posDeque.front()));
            posDeque.pop_front();
        } else {
            // there is an a/c...only use stuff past current 'to'-pos
            const positionTy& to = pAc->GetToPos();
            LOG_ASSERT_FD(*this, !std::isnan(to.ts()));
            
            // Remove outdated positions from posDeque,
            // ie. all positions before 'to'
            while (!posDeque.empty() && posDeque.front() < to)
                posDeque.pop_front();
            
            // nothing left???
            if (posDeque.empty())
                return TRY_NO_DATA;
            
            // move that next position to the a/c
            acPosList.emplace_back(std::move(posDeque.front()));
            posDeque.pop_front();
        }
        
        // store rotate timestamp if there is one (never overwrite with NAN!)
        if (!std::isnan(rotateTS))
            _rotateTS = rotateTS;
        
        // output all positional information as debug info on request
        if (dataRefs.GetDebugAcPos(key()))
            LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
        
        // return success as something has been added
        return TRY_SUCCESS;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Caught some error
    return TRY_TECH_ERROR;
}


// determine ground-status based on comparing altitude to terrain
// Note: If pos.onGnd == GND_ON then this will not change, but the altitude will be set to terrain altitude
//       If pos.onGnd != GND_ON then onGnd will be decided based on comparing altitude to terrain altitude
bool LTFlightData::TryDeriveGrndStatus (positionTy& pos)
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // what's the terrain altitude at that pos?
            double terrainAlt = YProbe_at_m(pos);
            if (std::isnan(terrainAlt))
                return false;
            
            // Now 2 options:
            // If position already says itself: I'm on the ground, then keep it like that
            // Otherwise decide based on altitude _if_ it's on the ground
            if (!pos.IsOnGnd() &&
                // say it's on the ground if below terrain+50ft
                pos.alt_m() < terrainAlt + FD_GND_AGL)
                pos.f.onGrnd = GND_ON;

            // if it was or now is on the ground correct the altitue to terrain altitude
            // (very slightly below to be sure to actually touch down even after rounding effects)
            if (pos.IsOnGnd())
                pos.alt_m() = terrainAlt - MDL_CLOSE_TO_GND;
            else
                // make sure it's either GND_ON or GND_OFF, nothing lese
                pos.f.onGrnd = GND_OFF;

            // successfully determined a status
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// determine terrain alt at pos
double LTFlightData::YProbe_at_m (const positionTy& pos)
{
    return ::YProbe_at_m(pos, probeRef);
}

// returns vector at timestamp (which has speed, direction and the like)
LTFlightData::tryResult LTFlightData::TryGetNextPos (double ts, positionTy& pos) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // find first posititon _after_ ts
            dequePositionTy::const_iterator i =
            std::find_if(posDeque.cbegin(),posDeque.cend(),
                         [ts](const positionTy& p){return p.ts() > ts;});
            
            // no positions found -> no data!
            if (i == posDeque.cend())
                return TRY_NO_DATA;
            
            // return the position
            pos = *i;
            return TRY_SUCCESS;
        }
        else
            return TRY_NO_LOCK;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return TRY_TECH_ERROR;
}

// stringify all position information - mainly for debugging purposes
std::string LTFlightData::Positions2String () const
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // 0. current sim time
        time_t t = time_t(dataRefs.GetSimTime());
        struct tm tm;
        gmtime_s(&tm, &t);

        char szBuf[50];
        snprintf(szBuf,sizeof(szBuf),
                 "a/c %s %s SimTime: %.1f - ",
                 key().c_str(),
                 statData.acId("-").c_str(),
                 dataRefs.GetSimTime());
        std::string ret(szBuf);

        strftime(szBuf,
                 sizeof(szBuf) - 1,
                 "%F %T",
                 &tm);
        ret += szBuf;
        ret += '\n';
        
        // 1. the data actually used by the a/c
        if(pAc) {
            ret += *pAc;
        } else {
            ret += "pAc == <null>\n";
        }
        
        // 2. flight data buffer with cleansed data
        ret += "posDeque:\n";
        ret += positionDeque2String(posDeque);
        
        // 3. buffer of new data to add as read from original source
        ret += "posToAdd:\n";
        ret += positionDeque2String(posToAdd);
        
        return ret;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // if we get here something's wrong
    return std::string();
}

// add dynamic data (if new one is more up-to-date)
void LTFlightData::AddDynData (const FDDynamicData& inDyn,
                               int _rcvr, int _sig,
                               positionTy* pos)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // We don't mix channels. They aren't in synch, mixing them leads
        // to planes jumping around and other weird behaviour.
        // We allow a change of channel only if the current channel seems
        // outdated and unresponsive.
        // We allow a change of channel if this prevents the aircraft from
        // disappearing, i.e. if this is the last update before a/c outdated period,
        // or in other words: new ts + refresh period > old ts + outdated period
        if (!dynDataDeque.empty()) {
            const FDDynamicData& last = dynDataDeque.back();
            if (last.pChannel != inDyn.pChannel &&
                last.pChannel && inDyn.pChannel) {
                // Firstly, if no a/c yet created, then we prioritize with
                // which channel an a/c is created. The higher the channel
                // number the better.
                if (!pAc)
                {
                    if (inDyn.pChannel->GetChannel() <= last.pChannel->GetChannel())
                        // lower prio -> ignore data
                        return;
                    
                    // so we throw away the lower prio channel's data
                    const LTChannel* pLstChn = last.pChannel;           // last is going to become invalid, save the ptr for the log message
                    dynDataDeque.clear();
                    posDeque.clear();
                    LOG_MSG(logDEBUG, DBG_AC_CHANNEL_SWITCH,
                            keyDbg().c_str(),
                            pLstChn ? pLstChn->ChName() : "<null>",
                            inDyn.pChannel ? inDyn.pChannel->ChName() : "<null>")
                }
                else
                {
                    // We compare timestamps of the actual positions and as a safeguard
                    // accept positions only pointing roughly in the same direction
                    // as we don't want a/c to turn back and forth due to channel switch.
                    if (!pos) return;
                    const positionTy& lastPos = (posDeque.empty() ?
                                                 pAc->GetToPos() :
                                                 posDeque.back());
                    if (pos->ts() + dataRefs.GetFdRefreshIntvl() <=
                        lastPos.ts() + dataRefs.GetAcOutdatedIntvl())
                        // not big enough a difference in timestamps yet
                        return;
                    
                    // check for weird heading changes, wrong speed etc.);
                    if (!IsPosOK(lastPos, *pos))
                        return;

                    // accept channel switch!
                    LOG_MSG(logDEBUG, DBG_AC_CHANNEL_SWITCH,
                            keyDbg().c_str(),
                            last.pChannel ? last.pChannel->ChName() : "<null>",
                            inDyn.pChannel ? inDyn.pChannel->ChName() : "<null>")
                }
            }
        }
        
        // only need to bother adding data if it is newer than current data
        if (dynDataDeque.empty() || dynDataDeque.front() < inDyn)
        {
            // must not yet have similar timestamp in our list
            if (std::find_if(dynDataDeque.cbegin(),dynDataDeque.cend(),
                             [&inDyn](const FDDynamicData& i){return inDyn.similarTo(i);}) == dynDataDeque.cend())
            {
                // add to list and keep sorted
                dynDataDeque.emplace_back(inDyn);
                std::sort(dynDataDeque.begin(),dynDataDeque.end());
            }
            
            // either way: we 'like' this receiver
            rcvr = _rcvr;
            sig = _sig;
        }
            
        // also store the pos (lock is held recursively)
        if (pos)
            AddNewPos(*pos);
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// tries to lock, then copies, returns true if copy took place
bool LTFlightData::TryGetSafeCopy ( FDDynamicData& outDyn ) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // we got the lock, return a copy of the data
            outDyn = dynDataDeque.empty() ? FDDynamicData() : dynDataDeque.front();
            // Success!
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// waits for lock and returns a copy
LTFlightData::FDDynamicData LTFlightData::WaitForSafeCopyDyn (bool bFirst) const
{
    LTFlightData::FDDynamicData ret;
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy the the data under lock protection
        if (!dynDataDeque.empty())
            ret = bFirst ? dynDataDeque.front() : dynDataDeque.back();
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // return the data (ideally the copy created under lock protection)
    return ret;
}

LTFlightData::FDDynamicData LTFlightData::GetUnsafeDyn() const
{
    return dynDataDeque.empty() ? FDDynamicData() : dynDataDeque.front();
}

// returns false if there is no dynamic data
// returns true and set chn to the current channel if there is dynamic data
bool LTFlightData::GetCurrChannel (const LTChannel* &pChn) const
{
    if (dynDataDeque.empty()) {
        pChn = nullptr;
        return false;
    }

    pChn = dynDataDeque.front().pChannel;
    return pChn != nullptr;
}


// find two positions around given timestamp ts
// pBefore and pAfter can come back NULL!
// if pbSimilar is not NULL then function also checks for 'similar' pos
// if a 'similar' pos (ts within SIMILAR_TS_INTVL) is found then
// *pbSimilar is set to true and pBefore points to that one.
// Calling function must own lock to ensure pointers remain valid
void LTFlightData::dequeFDDynFindAdjacentTS (double ts,
                                             LTFlightData::FDDynamicData*& pBefore,
                                             LTFlightData::FDDynamicData*& pAfter,
                                             bool* pbSimilar)
{
    // init
    pBefore = pAfter = nullptr;
    if (pbSimilar)
        *pbSimilar = false;
    
    // loop
    for (FDDynamicData& d: dynDataDeque) {
        
        // test for similarity
        if (pbSimilar) {
            if (abs(d.ts-ts) < SIMILAR_TS_INTVL) {
                *pbSimilar = true;
                pBefore = &d;
                return;
            }
        }
        
        // test for range before/after
        if (d.ts <= ts)
            pBefore = &d;           // while less than timestamp keep pBefore updated
        else {
            pAfter = &d;            // now found (first) data greater then ts
            return;                 // short-cut...ts in dynDataDeque would only further increase
        }
    }
}


//
//MARK: Flight Data - Mutex-Controlled access Static
//

// update static data
void LTFlightData::UpdateData (const LTFlightData::FDStaticData& inStat)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // decide if we need more master data to be fetched by
        // master data channels. If statData is not initialized at all
        // (first call is always by dynamic data fetch), or
        // if the callSign changes (which includes if it changes from empty to something)
        // as the callSign is the source for route information
        if (!statData.isInit() ||
            (!inStat.call.empty() && inStat.call != statData.call))
        {
            LTACMasterdataChannel::RequestMasterData (key(), inStat.call);
        }
        
        // merge inStat into our statData (copy only filled fields,
        // which are not model-defining):
        statData |= inStat;
        
        // *** take care of changes in model-defining fields ***
        // (a/c type, operator, registration)
        bool bMdlInfoChange = false;
        
        // operator ICAO: we only accept a change from nothing to something
        if (statData.opIcao.empty() && !inStat.opIcao.empty())
        {
            statData.opIcao = inStat.opIcao;
            bMdlInfoChange = true;
        }
        
        // registration: we only accept a change from nothing to something
        if (statData.reg.empty() && !inStat.reg.empty())
        {
            statData.reg = inStat.reg;
            bMdlInfoChange = true;
        }
        
        // Re-determine a/c model (only if it was determined before:
        // the first determination shall be made as late as possible
        // in LTFlightData::CreateAircraft())
        if (pAc && DetermineAcModel())
            bMdlInfoChange = true;
        
        if (pAc) {
            // if model-defining fields changed then (potentially) change the CSL model
            if (bMdlInfoChange)
                pAc->SetUpdateModel();
            // Make Aircraft send updated info texts
            pAc->SetSendNewInfoData();
        }
        
        // update the static parts of the label
        UpdateStaticLabel();
        
   } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// tries to lock, then copies, returns true if copy took place
bool LTFlightData::TryGetSafeCopy ( LTFlightData::FDStaticData& outStat ) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock);
        if ( lock )
        {
            // we got the lock, return a copy of the data
            outStat = statData;
            // Success!
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// waits for lock and returns a copy
LTFlightData::FDStaticData LTFlightData::WaitForSafeCopyStat() const
{
    LTFlightData::FDStaticData ret;
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy the data under lock protection
        ret = statData;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // return the data (ideally the copy created under lock protection)
    return ret;
}

//
//MARK: Flight Data - Aircraft Maintenance
//      (call from flight loop as possibly XPMP/XPLM calls are invoked)
//

// checks if initial position to be calculated or aircraft to be created
// returns if a/c is to be deleted
bool LTFlightData::AircraftMaintenance ( double simTime )
{
    try {
        // try to lock data access
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock);
        if ( !lock )                // we didn't get the lock, just return w/o deletion
            return false;
        
        // if the a/c became invalid then remove the aircraft object,
        // but retain the remaining flight data
        if (pAc && !pAc->IsValid())
            DestroyAircraft();
        
        // if outdated just return 'delete me'
        if ( outdated(simTime) )
            return true;
        
        // do we need to recalc the static part of the a/c label due to config change?
        if (dataRefs.GetLabelCfg() != labelCfg)
            UpdateStaticLabel();
        
        // doesn't yet have an associated aircraft but two positions?
        if ( !hasAc() && posDeque.size() >= 2 ) {
            // is already valid for a/c creation?
            if ( validForAcCreate(simTime) )
                // then do create the aircraft
                CreateAircraft(simTime);
            else // not yet valid
                // but the oldest position is at or before current simTime?
                // then chances are good that we can calculate positions
                if ( posDeque.front().ts() <= simTime)
                    // start thread for position calculation...next time we might be valid for creation
                    TriggerCalcNewPos(NAN);
        }
        
        // don't delete me
        return false;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
        // if an exception occurs this object is declared invalid and removed
        SetInvalid();
    } catch(...) {
        // if an exception occurs this object is declared invalid and removed
        SetInvalid();
    }
    
    // in case of error return 'delete me'
    return true;
}


// try interpreting model text or check for ground vehicle
bool LTFlightData::DetermineAcModel()
{
    const std::string prevType = statData.acTypeIcao;

    // Debugging model matching: If the model is fixed, then it is what it is
    if (!dataRefs.cslFixAcIcaoType.empty()) {
        statData.acTypeIcao = dataRefs.cslFixAcIcaoType;
        return statData.acTypeIcao != prevType;
    }
    
    // We don't change the a/c type if it is already something reasonable
    if (!prevType.empty() &&
        prevType != dataRefs.GetDefaultCarIcaoType())
        return false;
    
    // Try finding a CSL model by interpreting the human-readable model text
    std::string mdl (statData.mdl);
    str_toupper(trim(mdl));
    statData.acTypeIcao = ModelIcaoType::getIcaoType(mdl);
    if ( !statData.acTypeIcao.empty() )
    {
        // yea, found something by mdl!
        if (prevType != statData.acTypeIcao) {
            LOG_MSG(logWARN,ERR_NO_AC_TYPE_BUT_MDL,
                    key().c_str(),
                    statData.man.c_str(), statData.mdl.c_str(),
                    statData.acTypeIcao.c_str());
            return true;
        }
        return false;
    }
    
    // Ground vehicle maybe? Shall be on the ground then with reasonable speed
    // (The info if this _could_ be a car is delivered by the channels via
    //  the acTypeIcao, here we just validate if the dynamic situation
    //  fits a car.)
    if (prevType == dataRefs.GetDefaultCarIcaoType())
    {
        if ((pAc &&                                 // plane exists?
             pAc->IsOnGrnd() &&                     // must be on ground with reasonable speed
             pAc->GetSpeed_kt() < pAc->mdl.MAX_TAXI_SPEED) ||
            (!pAc &&                                // no plane yet:
             !posDeque.empty() &&                   // analyse first posDeque data
             posDeque.front().IsOnGnd()))
        {
            // We now decide for surface vehicle
            statData.acTypeIcao = dataRefs.GetDefaultCarIcaoType();
            return statData.acTypeIcao != prevType;
        }
    }
            
    // we have no better idea than default
    statData.acTypeIcao.clear();
    return prevType != statData.acTypeIcao;
}

// checks if there is a slot available to create this a/c, tries to remove the farest a/c if too many a/c rendered
/// @warning Caller must own `mapFdMutex`!
bool LTFlightData::AcSlotAvailable (double simTime)
{
    // time we had shown the "Too many a/c" warning last:
    static double tTooManyAcMsgShown = 0.0;

    // Haven't reach the limit in terms of number of a/c yet?
    if (dataRefs.GetNumAc() < dataRefs.GetMaxNumAc())
        return true;
    
    // Have no positions? (Need one to determine distance to camera)
    if (posDeque.empty())
        return false;
    
    // Now we need to see if we are closer to the camera than other a/c.
    // If so remove the farest a/c to make room for us.
    LTFlightData* pFarestAc = nullptr;

    // NOTE: We can loop mapFd without lock only because we assume that
    //       calling function owns mapFdMutex already!
    // find the farest a/c...if it is further away than us:
    double farestDist = CoordDistance(dataRefs.GetViewPos(), posDeque.front());
    for (mapLTFlightDataTy::value_type& p: mapFd)
    {
        LTFlightData& fd = p.second;
        if (fd.hasAc() && fd.pAc->GetVecView().dist > farestDist) {
            farestDist = fd.pAc->GetVecView().dist;
            pFarestAc = &fd;
        }
    }
    
    // So we definitely have too many aircraft!
    if (tTooManyAcMsgShown + 180.0 < simTime) {     // Show message at most every 3 minutes
        SHOW_MSG(logWARN,MSG_TOO_MANY_AC,dataRefs.GetMaxNumAc());
        tTooManyAcMsgShown = simTime;
    }

    // If we didn't find an active a/c farther away than us then bail with message
    if (!pFarestAc) {
        return false;
    }
    
    // We found the a/c farest away...remove it!
    pFarestAc->DestroyAircraft();
    return true;
}



// create (at most one) aircraft from this flight data
bool LTFlightData::CreateAircraft ( double simTime )
{
    // short-cut if exists already
    if ( hasAc() ) return true;
    
    // exit if too many a/c shown and this one wouldn't be one of the nearest ones
    if (!AcSlotAvailable(simTime))
        return false;
    
    try {
        // get the  mutex, not so much for protection,
        // but to speed up creation (which read-accesses lots of data and
        // thus makes many calls to the lock, which are now just quick recursive calls)
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // make sure positional data is up-to-date
        // (also does a last validation...and now with lock, so that state is secured)
        if ( !CalcNextPos(simTime) )
            return false;
        
        // This can have change data in the posDeque...let's see if we are still valid for a/c create
        // Remove outdated positions from posDeque, ie. all positions before simTime
        while (posDeque.size() >= 2 && posDeque[1].ts() <= simTime)
            posDeque.pop_front();
        if ( !validForAcCreate(simTime) )
            return false;
        
        // There are yet unsolved errors where the subsequent aircraft creation failes with an
        // empty posDeque, though we just - while holding the dataAccessMutex - have verified
        // that we are valid for creation. See Issue #174.
        // Next time we see that bug we want to know what NOW is in posDeque:
        const std::string sPosDeque = positionDeque2String(posDeque);
        
        // Make sure we have a valid a/c model now
        DetermineAcModel();
        if (statData.acTypeIcao.empty()) {          // we don't...
            LOG_MSG(logWARN,ERR_NO_AC_TYPE,
                    key().c_str(),
                    statData.man.c_str(), statData.mdl.c_str(),
                    dataRefs.GetDefaultAcIcaoType().c_str());
        }

        // create the object (constructor will recursively re-access the lock)
        try {
            pAc = new LTAircraft(*this);
        } catch (const std::exception& e) {
            LOG_MSG(logERR, ERR_EXCEPTION_AC_CREATE,
                    key().c_str(), statData.acTypeIcao.c_str(),
                    e.what(), sPosDeque.c_str());
            pAc = nullptr;
        }
        catch(...) {
            LOG_MSG(logERR, ERR_UNKN_EXCP_AC_CREATE,
                    key().c_str(), statData.acTypeIcao.c_str(),
                    sPosDeque.c_str());
            pAc = nullptr;
        }
        if (!pAc)
        {
            LOG_MSG(logERR,ERR_NEW_OBJECT,key().c_str());
            return false;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // success
    return true;
}

// remove the linked aircraft
void LTFlightData::DestroyAircraft ()
{
    if ( pAc )
        delete pAc;
    pAc = nullptr;
}

// static function to
// update the CSL model of all aircraft (e.g. after loading new CSL models)
void LTFlightData::UpdateAllModels ()
{
    try {
        // access guarded by the fd mutex
        std::lock_guard<std::mutex> lock (mapFdMutex);
        
        // iterate all flight data
        for ( mapLTFlightDataTy::value_type& fdPair: mapFd )
        {
            // if there is an aircraft update it's flight model
            LTAircraft* pAc = fdPair.second.GetAircraft();
            if (pAc)
                pAc->SetUpdateModel();
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "mapFd", e.what());
    }
}

// finds the closest a/c roughly in the given direction ('focus a/c')
const LTFlightData* LTFlightData::FindFocusAc (const double bearing)
{
    constexpr double maxDiff = 20;
    const LTFlightData* ret = nullptr;
    double bestRating = std::numeric_limits<double>::max();
    
    // walk the map of flight data
    for ( std::pair<const LTFlightData::FDKeyTy,LTFlightData>& fdPair: mapFd )
    {
        // no a/c? -> not relevant
        if (!fdPair.second.pAc)
            continue;
        
        // should be +/- 45° of bearing
        const vectorTy vecView = fdPair.second.pAc->GetVecView();
        double hDiff = abs(HeadingDiff(bearing, vecView.angle));
        if (hDiff > maxDiff)
            continue;
        
        // calculate a rating based on deviation from bearing plus distance
        // Reasoning: An a/c directly in front of us shall be prefered if
        //            it is less than twice as far away as an a/c 45° to the side.
        double rating = (1 + hDiff / maxDiff) * vecView.dist;
        
        // best one so far?
        if ( rating < bestRating ) {
            bestRating = rating;
            ret = &fdPair.second;
        }
    }
    
    // return what we thing is focus
    return ret;
}

#ifdef DEBUG
// This helps focusing on one aircraft and debug through the position calculation code
void LTFlightData::RemoveAllAcButSelected ()
{
    // access guarded by the fd mutex
    std::lock_guard<std::mutex> lock (mapFdMutex);
    
    // hard and directly remove all other aircraft without any further ado
    for (mapLTFlightDataTy::iterator i = mapFd.begin();
         i != mapFd.end();)
    {
        if (!i->second.bIsSelected)
            i = mapFd.erase(i);
        else
            ++i;
    }
    
    // reduce allow a/c to 1 so no new aircraft gets created
    dataRefs.SetMaxNumAc(1);
}
#endif


//
// MARK: mapLTFlightDataTy
//

// Find "i-th" aircraft, i.e. the i-th flight data with assigned pAc
mapLTFlightDataTy::iterator mapFdAcByIdx (int idx)
{
    // let's find the i-th aircraft by looping over all flight data
    // and count those objects, which have an a/c
    int i = 0;
    for (mapLTFlightDataTy::iterator fdIter = mapFd.begin();
         fdIter != mapFd.end();
         ++fdIter)
    {
        if (fdIter->second.hasAc())         // has an a/c
            if ( ++i == idx )               // and it's the i-th!
                return fdIter;
    }
    
    // not found
    return mapFd.end();
}

//
// MARK: LTFlightDataList
//

LTFlightDataList::LTFlightDataList ( OrderByTy ordrBy )
{
    // copy the entire map into a simple list
    lst.reserve(mapFd.size());
    for ( std::pair<const LTFlightData::FDKeyTy,LTFlightData>& fdPair: mapFd )
        lst.emplace_back(&fdPair.second);
    
    // apply the initial ordering
    ReorderBy(ordrBy);
}


void LTFlightDataList::ReorderBy(OrderByTy ordrBy)
{
    // quick exit if already sorted that way
    if (orderedBy == ordrBy)
        return;
    
    // range to sort
    vecLTFlightDataRefTy::iterator from = lst.begin();
    vecLTFlightDataRefTy::iterator to   = lst.end();
    
#define SORT_BY_STAT(OrdrBy,cmp)                                            \
case OrdrBy:                                                                \
    std::sort(from, to, [](LTFlightData*const& a, LTFlightData*const& b )   \
              { return cmp; } );                                            \
    break;
    
#define SORT_BY_PAC(OrdrBy,cmp)                                             \
case OrdrBy:                                                                \
    std::sort(from, to, [](LTFlightData*const& a, LTFlightData*const& b )   \
              { return                                                      \
                  !b->pAc && !a->pAc ? a->key() < b->key() :                \
                  !b->pAc ? true :                                          \
                  !a->pAc ? false :                                         \
                  (cmp); });                                                \
    break;

    
    // static fields can always be applied
    switch (ordrBy) {
        case ORDR_UNKNOWN: break;
        SORT_BY_STAT(ORDR_REG,          a->statData.reg < b->statData.reg);
        SORT_BY_STAT(ORDR_AC_TYPE_ICAO, a->statData.acTypeIcao < b->statData.acTypeIcao);
        SORT_BY_STAT(ORDR_CALL,         a->statData.call < b->statData.call);
        SORT_BY_STAT(ORDR_ORIGIN_DEST,  a->statData.route() < b->statData.route());
        SORT_BY_STAT(ORDR_FLIGHT,       a->statData.flight < b->statData.flight);
        SORT_BY_STAT(ORDR_OP_ICAO,      a->statData.opIcao == b->statData.opIcao ?
                                        a->key() < b->key() :
                                        a->statData.opIcao < b->statData.opIcao);
        SORT_BY_PAC(ORDR_DST,           a->pAc->GetVecView().dist < b->pAc->GetVecView().dist);
        SORT_BY_PAC(ORDR_SPD,           a->pAc->GetSpeed_m_s() < b->pAc->GetSpeed_m_s());
        SORT_BY_PAC(ORDR_VSI,           a->pAc->GetVSI_ft() < b->pAc->GetVSI_ft());
        SORT_BY_PAC(ORDR_ALT,           a->pAc->GetAlt_m() < b->pAc->GetAlt_m());
        SORT_BY_PAC(ORDR_PHASE,         a->pAc->GetFlightPhase() == b->pAc->GetFlightPhase() ?
                                        a->key() < b->key() :
                                        a->pAc->GetFlightPhase() < b->pAc->GetFlightPhase());
    }
    
    // no ordered the new way
    orderedBy = ordrBy;
}

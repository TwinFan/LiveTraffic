/// @file       LTApt.cpp
/// @brief      Access to X-Plane's `apt.dat` file(s) and data
/// @details    Scans `apt.dat` file for airport, runway, and taxiway information.\n
///             Finds potential runway for an auto-land flight.\n
///             Finds center lines on runways and taxiways to snap positions to.
/// @author     Birger Hoppe
/// @copyright  (c) 2020 Birger Hoppe
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

// File paths

/// Path to the `scenery_packs.ini` file, which defines order and activation status of scenery packs
#define APTDAT_SCENERY_PACKS "Custom Scenery/scenery_packs.ini"
/// How a line in `scenery_packs.ini` file needs to start in order to be processed by us
#define APTDAT_SCENERY_LN_BEGIN "SCENERY_PACK "
/// Path to add after the scenery pack location read from the ini file
#define APTDAT_SCENERY_ADD_LOC "Earth nav data/apt.dat"
/// Path to the global airports file under Resources / Default
#define APTDAT_RESOURCES_DEFAULT "Resources/default scenery/default apt dat/"

// Log output
#define WARN_APTDAT_NOT_OPEN "Can't open '%s': %s"
#define WARN_APTDAT_FAILED   "Could not open ANY apt.dat file. No runway/taxiway info available to guide ground traffic."
#define WARN_APTDAT_READ_FAIL "Could not completely read '%s'. Some runway/taxiway info will be missing to guide ground traffic: %s"

/// This flag stops the file reading thread
volatile bool bStopThread = false;

class Apt;

//
// MARK: Airports, Runways and Taxiways
//

// TODO: Runways are just node/edges, identify by nodeTy
//       List of edges to be sorted by (angle).
//       It allows faster search for
//       matching edges as we can limit the search range by angle.

/// @brief A node of a taxi way
/// @details Depending on scenery and search range we might need to read and store
///          tenth of thousans of these, so we limit the members as much as possible,
///          e.g. we don't use ::positionTy but only lat/lon/x/z.
class TaxiNode {
public:
    double      lat;                    ///< latitude
    double      lon;                    ///< longitude
    double      x = NAN;                ///< local coordinates, east axis
    double      z = NAN;                ///< local coordinates, south axis
public:
    /// Default constructor leaves all empty
    TaxiNode () : lat(NAN), lon(NAN) {}
    /// Typical constructor requires a location
    TaxiNode (double _lat, double _lon) : lat(_lat), lon(_lon) {}
    /// Destructor does not actually do anything, but is recommended in a good virtual class definition
    virtual ~TaxiNode () {}

    /// Is node valid in terms of geographic coordinates?
    bool HasGeoCoords () const { return !std::isnan(lat) && !std::isnan(lon); }
    /// Is node valid in terms of local coordinates?
    bool HasLocalCoords () const { return !std::isnan(x) && !std::isnan(z); }
    /// @brief Update local coordinates
    /// @param bForce `False` only calculate x/z if not yet known, `true` recalculate no matter what
    /// @param _alt_m Default altitude to use if member TaxiNode::alt_m is not filled
    virtual void LocalCoordsUpdate (bool bForce, double _alt_m)
    {
        double y;
        if (bForce || std::isnan(x))
            XPLMWorldToLocal(lat, lon,
                             _alt_m,
                             &x, &y, &z);
    }
};

/// Vector of taxi nodes
typedef std::vector<TaxiNode> vecTaxiNodesTy;

/// A runway endpoint is a special node of which we need to know the altitude
class RwyEndPt : public TaxiNode {
public:
    std::string id;                     ///< rwy identifier, like "23" or "05R"
    double      alt_m = NAN;            ///< ground altitude in meter
    double      y = NAN;                ///< local coordinates, vertical (up) axis
public:
    /// Default constructor leaves all empty
    RwyEndPt () : TaxiNode () {}
    /// Typical constructor fills id and location
    RwyEndPt (const std::string& _id, double _lat, double _lon) :
    TaxiNode(_lat, _lon), id(_id) {}
    /// Destructor does not actually do anything, but is recommended in a good virtual class definition
    virtual ~RwyEndPt () {}

    /// @brief Update local coordinates, make use of stored altitude if available
    /// @param bForce `False` only calculate x/z if not yet known, `true` recalculate no matter what
    /// @param _alt_m Default altitude to use if member TaxiNode::alt_m is not filled
    virtual void LocalCoordsUpdate (bool bForce, double _alt_m)
    {
        if (bForce || std::isnan(x))
            XPLMWorldToLocal(lat, lon,
                             std::isnan(alt_m) ? _alt_m : alt_m,
                             &x, &y, &z);
        // We only keep the y value if it related to _our_ altitude
        if (std::isnan(alt_m))
            y = NAN;
    }
    
    /// Compute altitude if not yet known
    void ComputeAlt (XPLMProbeRef& yProbe)
    {
        if (std::isnan(alt_m))
            alt_m = YProbe_at_m(positionTy(lat,lon,0.0), yProbe);
    }
};

/// Vector of runway endpoints
typedef std::vector<RwyEndPt> vecRwyEndPtTy;

/// @brief An edge in the taxi / rwy network, connected two nodes
/// @details TaxiEdge can only store _indexes_ into the vector of nodes,
///          which is Apt::vecTaxiNodes. tt cannot directly store pointers or references,
///          as the memory location might change when the vector reorganizes due to
///          additions.\n
///          This also means that some functions otherwise better suites here are now
///          moved to Apt as only Apt has access to all vectors.
class TaxiEdge {
public:
    /// Taxiway or runway?
    enum nodeTy {
        UNKNOWN_WAY = 0,                ///< edge is of undefined type
        RUN_WAY = 1,                    ///< edge is for runway
        TAXI_WAY,                       ///< edge is for taxiway
    };
    
protected:
    nodeTy type;                        ///< type of node (runway, taxiway)
    size_t      a = 0;                  ///< from node (index into vecTaxiNodes)
    size_t      b = 0;                  ///< to node (index into vecTaxiNodes)
public:
    double angle;                       /// angle/heading from a to b
    double dist_m;                      /// distance in meters between a and b
public:
    /// Constructor
    TaxiEdge (nodeTy _t, size_t _a, size_t _b, double _angle, double _dist_m) :
    type(_t), a(_a), b(_b), angle(_angle), dist_m(_dist_m)
    {
        // Normalize edges for 0 <= angle < 180
        if (angle >= 180.0) {
            std::swap(a,b);
            angle -= 180.0;
        }
    }
    /// Special Constructor for comparison objects only
    TaxiEdge (double _angle) :
    type(TAXI_WAY), a(0), b(0), angle(_angle), dist_m(NAN)
    {}
    
    /// Return the node's type
    nodeTy GetType () const { return type; }
    
    // Poor man's polymorphism: rwy endpoints are stored at a different place
    // than taxiway nodes. And we only store indexes as pointers are
    // unreliabe. The following functions return the proper object.
    /// Return the a node, ie. the starting point of the edge
    const TaxiNode& GetA (const Apt& apt) const;
    /// Return the b node, ie. the ending point of the edge
    const TaxiNode& GetB (const Apt& apt) const;
    /// Return the first runway endpoint of a runway
    const RwyEndPt& GetRwyEP_A (const Apt& apt) const
    { return dynamic_cast<const RwyEndPt&>(GetA(apt)); }
    /// Return the second runway endpoint of a runway
    const RwyEndPt& GetRwyEP_B (const Apt& apt) const
    { return dynamic_cast<const RwyEndPt&>(GetB(apt)); }

    /// Comparison function for sorting and searching
    static bool CompHeadLess (const TaxiEdge& a, const TaxiEdge& b)
    { return a.angle < b.angle; }
};

/// Vector of taxi edges
typedef std::vector<TaxiEdge> vecTaxiEdgeTy;
/// List of const pointers to taxi edges (for search function results)
typedef std::list<const TaxiEdge*> lstTaxiEdgeCPtrTy;

/// Represents an airport as read from apt.dat
class Apt {
protected:
    static XPLMProbeRef YProbe;         ///< Y Probe for terrain altitude computation
    std::string id;                     ///< ICAO code or other unique id
    boundingBoxTy bounds;               ///< bounding box around airport, calculated from rwy and taxiway extensions
    double alt_m = NAN;                 ///< the airport's altitude
    vecTaxiNodesTy vecTaxiNodes;        ///< vector of taxi network nodes
    vecRwyEndPtTy  vecRwyEndPts;        ///< vector of runway endpoints
    vecTaxiEdgeTy  vecTaxiEdges;        ///< vector of taxi network edges, each connecting any two nodes

public:
    /// Constructor expects an id
    Apt (const std::string& _id = "") : id(_id) {}
    
    /// Id of the airport, typicall the ICAO code
    std::string GetId () const { return id; }
    /// Is any id defined? (Used as indicator while reading in `apt.dat`)
    bool HasId () const { return !id.empty(); }
    
    /// Valid airport definition requires an id and some taxiways / runways
    bool IsValid () const { return HasId() && !vecTaxiEdges.empty(); }
    
    /// Return a reasonable altitude...effectively one of the rwy ends' altitude
    double GetAlt_m () const { return alt_m; }
    
    // --- MARK: Taxiways
    
    /// The vector of taxi network nodes
    const vecTaxiNodesTy& GetTaxiNodesVec () const { return vecTaxiNodes; }
    /// The list of taxi network edges
    const vecTaxiEdgeTy& GetTaxiEdgeVec () const { return vecTaxiEdges; }
    
    /// Any taxiways/runways defined?
    bool HasTaxiWays () const { return !vecTaxiEdges.empty(); }
    
    /// @brief Add a new taxi network node
    void AddTaxiNode (double lat, double lon, size_t idx)
    {
        // Potentially expands the airport's boundary
        bounds.enlarge_pos(lat, lon);

        // Expected case: Just the next index
        if (idx == vecTaxiNodes.size())
            vecTaxiNodes.emplace_back(lat, lon);
        else {
            // make sure the vector is large enough
            if (idx > vecTaxiNodes.size())
                vecTaxiNodes.resize(idx+1);
            // then assign the value
            vecTaxiNodes[idx] = TaxiNode(lat,lon);
        }
    }
    
    /// @brief Add a new taxi network edge, which must connect 2 existing nodes
    /// @return Successfully inserted, ie. found the 2 nodes?
    bool AddTaxiEdge (size_t n1, size_t n2)
    {
        // Indexes must be valid
        if (n1 >= vecTaxiNodes.size() ||
            n2 >= vecTaxiNodes.size())
        {
            LOG_MSG(logDEBUG, "apt.dat: Node %lu or &lu not found! Edge not added.", n1, n2);
            return false;
        }
        
        // Actual nodes must be valid
        const TaxiNode& a = vecTaxiNodes[n1];
        const TaxiNode& b = vecTaxiNodes[n2];
        if (!a.HasGeoCoords() || !b.HasGeoCoords())
        {
            LOG_MSG(logDEBUG, "apt.dat: Node %lu or &lu invalid! Edge not added.", n1, n2);
            return false;
        }
        
        // Add the edge
        vecTaxiEdges.emplace_back(TaxiEdge::TAXI_WAY, n1, n2,
                                  CoordAngle(a.lat, a.lon, b.lat, b.lon),
                                  CoordDistance(a.lat, a.lon, b.lat, b.lon));
        return true;
    }
    
    /// @brief Update local coordinate system values (taxi nodes and rwy ends)
    /// @param bForce `true` recalc all values, `false` calc only missing values
    void LocalCoordsUpdate (bool bForce)
    {
        for (TaxiNode& n: vecTaxiNodes)
            n.LocalCoordsUpdate(bForce, alt_m);
        for (RwyEndPt& re: vecRwyEndPts)
            re.LocalCoordsUpdate(bForce, alt_m);
    }
    
    
    /// @brief Returns a list of taxiways matching a given heading range
    /// @param _headSearch The heading we search for and which the edge has to match
    /// @param _angleTolerance Maximum difference between `_headSearch` and TaxiEdge::angle to be considered a match
    /// @param[out] lst Matching egdes are added to this list,
    /// @param _restrictType (Optional) Restrict returned edges to this type, or `UNKNOWN_WAY` to not restrict results
    /// @return Anything found? Basically: `!vec.empty()`
    bool FindEdgesForHeading (double _headSearch,
                              double _angleTolerance,
                              lstTaxiEdgeCPtrTy& lst,
                              TaxiEdge::nodeTy _restrictType = TaxiEdge::UNKNOWN_WAY) const
    {
        // vecTaxiEdges is sorted by heading (see AddApt)
        // and TaxiEdge::heading is normalized to [0..180).
        // So we can more quickly find potential matches by
        // looking in that range of edges only around our target heading pos.heading()
        // "Normalize" search heading even further to [0..180)
        bool bHeadInverted = false;
        if (_headSearch >= 180.0) {
            _headSearch -= 180.0;
            bHeadInverted = true;
        }
        // We allow for some tolerance
        const double headBegin = _headSearch - _angleTolerance;     // might now be < 0 !
        const double headEnd   = _headSearch + _angleTolerance;     // might now be >= 180 !
        
        // We need one or two search ranges
        std::vector< std::pair<double,double> > vecRanges;
        // normal case: just one search range
        if (0.0 <= headBegin && headEnd < 180.0) {
            vecRanges.emplace_back(headBegin,   headEnd);
        } else if (headBegin < 0.0) {
            const double headBeginInv = headBegin + 180.0;              // inverse...if headBegin < 0 then this is the start point in the upper range close to 180°
            vecRanges.emplace_back(0.0,         headEnd);
            vecRanges.emplace_back(headBeginInv,180.0);
        } else {        // headEnd >= 180.0
            const double headEndInv   = headEnd   - 180.0;              // inverse...if headEnd >= 180 then this is the end point in the lower range close to 0°
            vecRanges.emplace_back(0.0,         headEndInv);
            vecRanges.emplace_back(headBegin,   180.0);
        }
        
        // search all (up to 2) heading ranges now
        for (const std::pair<double,double>& rngPair: vecRanges)
        {
            // within that heading range, add all matching edges
            for (vecTaxiEdgeTy::const_iterator iter = std::lower_bound(vecTaxiEdges.cbegin(),
                                                                       vecTaxiEdges.cend(),
                                                                       TaxiEdge(rngPair.first),
                                                                       TaxiEdge::CompHeadLess);
                 iter != vecTaxiEdges.cend() && iter->angle <= rngPair.second;
                 ++iter)
            {
                // Check for type limitation, then add to `vec`
                const TaxiEdge& e = *iter;
                if (_restrictType == TaxiEdge::UNKNOWN_WAY ||
                    _restrictType == e.GetType())
                    lst.push_back(&e);
            }
        }
        
        // Found anything?
        return !lst.empty();
    }
    
    
    /// @brief Find closest taxi edge matching the passed position including its heading
    /// @param pos Search position, only nearby nodes with a similar heading are considered
    /// @param[out] basePt Receives the coordinates of the base point in case of a match. Only lat and lon will be modified.
    /// @param _maxDist_m Maximum distance in meters between `pos` and edge to be considered a match
    /// @param _angleTolerance Maximum difference between `pos.heading()` and TaxiEdge::angle to be considered a match
    /// @return Pointer to closest taxiway edge or `nullptr` if no match was found
    const TaxiEdge* FindClosestEdge (const positionTy& pos,
                                     positionTy& basePt,
                                     int _maxDist_m,
                                     double _angleTolerance = ART_EDGE_ANGLE_TOLERANCE) const
    {
        const TaxiEdge* bestEdge = nullptr;
        const TaxiNode* bestFrom = nullptr;
        const TaxiNode* bestTo   = nullptr;
        distToLineTy bestDist;
        bestDist.dist2 = (double)sqr(_maxDist_m);
        // At maximum, we allow that the base of the shortest dist to edge is about GetFdSnapTaxiDist_m outside of line ends
        const double maxDistBeyondLineEnd2 = (double)sqr(_maxDist_m);;
        
        // We calculate in local coordinates
        double pt_x = NAN, pt_y = NAN, pt_z = NAN;
        XPLMWorldToLocal(pos.lat(), pos.lon(), pos.alt_m(),
                         &pt_x, &pt_y, &pt_z);
        
        // Get a list of edges matching pos.heading()
        lstTaxiEdgeCPtrTy lstEdges;
        const double headSearch = HeadingNormalize(pos.heading());
        if (!FindEdgesForHeading(headSearch,
                                 _angleTolerance,
                                 lstEdges))
            return nullptr;
        
        // Edges are normalized to angle of [0..180),
        // do we fly the other way round?
        const bool bHeadInverted = headSearch >= 180.0;

        // Analyze the edges to find the closest edge
        for (const TaxiEdge* e: lstEdges)
        {
            // Fetch from/to nodes from the edge
            const TaxiNode& from = bHeadInverted ? e->GetB(*this) : e->GetA(*this);
            const TaxiNode& to   = bHeadInverted ? e->GetA(*this) : e->GetB(*this);

            // Edges need to have local coordaintes for what comes next
            if (!from.HasLocalCoords() || !to.HasLocalCoords())
                continue;                       // no match due to heading
            
            // Distance to this edge
            distToLineTy dist;
            DistPointToLineSqr(pt_x, pt_z,          // plane's position (x is southward, z is eastward)
                               from.x, from.z,      // edge's starting point
                               to.x, to.z,          // edge's end point
                               dist);
            
            // If distance is farther then best we know: skip
            if (dist.dist2 >= bestDist.dist2)
                continue;
            
            // If base of shortest path to point is too far outside actual line
            if (dist.DistSqrOfBaseBeyondLine() > maxDistBeyondLineEnd2)
                continue;
            
            // We have a new best match!
            bestEdge = e;
            bestFrom = &from;
            bestTo   = &to;
            bestDist = dist;
        }
        
        // Nothing found?
        if (!bestEdge || !bestFrom || !bestTo)
            return nullptr;
        
        // Compute base point on the line,
        // ie. the point on the line with shortest distance
        // to pos
        DistResultToBaseLoc(bestFrom->x, bestFrom->z,   // edge's starting point
                            bestTo->x, bestTo->z,       // edge's end point
                            bestDist,
                            pt_x, pt_z);                // base point's local coordinates
        double lat = NAN, lon = NAN, alt = NAN;
        XPLMLocalToWorld(pt_x, pt_y, pt_z,
                         &lat, &lon, &alt);
        
        basePt.lat() = lat;
        basePt.lon() = lon;
        return bestEdge;
    }
    
    /// @brief Find best matching taxi edge based on passed-in position/heading info
    bool SnapToTaxiway (positionTy& pos, bool bLogging) const
    {
        const double old_lat = pos.lat(), old_lon = pos.lon();
        
        // Find the closest edge and right away move pos there
        if (!FindClosestEdge(pos, pos, dataRefs.GetFdSnapTaxiDist_m()))
            return false;

        // found a match, say hurray
        if (bLogging) {
            LOG_MSG(logDEBUG, "Snapped to taxiway from (%7.4f, %7.4f) to (%7.4f, %7.4f)",
                    old_lat, old_lon, pos.lat(), pos.lon());
        }
        
        // this is now an artificially moved position, don't touch any further
        pos.flightPhase = LTAPIAircraft::FPH_TAXI;
        
        return true;
    }
    
    // --- MARK: Runways
    
    /// The vector of runway endpoints
    const vecRwyEndPtTy& GetRwyEndPtVec () const { return vecRwyEndPts; }
    
    /// Any runway endpoints defined?
    bool HasRwyEndpoints () const { return !vecRwyEndPts.empty(); }
    
    /// Adds both rwy ends from apt.dat information fields
    void AddRwyEnds (double lat1, double lon1, double displaced1, const std::string& id1,
                     double lat2, double lon2, double displaced2, const std::string& id2)
    {
        // Original position of outer end of runway
        positionTy re1 (lat1,lon1,NAN,NAN,NAN,NAN,NAN,positionTy::GND_ON);
        positionTy re2 (lat2,lon2,NAN,NAN,NAN,NAN,NAN,positionTy::GND_ON);
        vectorTy vecRwy = re1.between(re2);

        // move by displayed threshold
        // and then by another 10% of remaining length to determine actual touch-down point
        vecRwy.dist -= displaced1;
        vecRwy.dist -= displaced2;
        re1 += vectorTy (vecRwy.angle,   displaced1 + vecRwy.dist * ART_RWY_TD_POINT_F );
        re2 += vectorTy (vecRwy.angle, -(displaced2 + vecRwy.dist * ART_RWY_TD_POINT_F));
        // Also adapt out knowledge of rwy length: 80% if previous value are left
        vecRwy.dist *= (1 - 2 * ART_RWY_TD_POINT_F);
        
        // 1st rwy end
        bounds.enlarge(re1);
        vecRwyEndPts.emplace_back(id1, re1.lat(), re1.lon());
        
        // 2nd rwy end
        bounds.enlarge(re2);
        vecRwyEndPts.emplace_back(id2, re2.lat(), re2.lon());
        
        // The edge between them, making up the actual runway
        vecTaxiEdges.emplace_back(TaxiEdge::RUN_WAY,
                                  vecRwyEndPts.size()-2,    // index of rwyEp1
                                  vecRwyEndPts.size()-1,    // index of rwyEp2
                                  vecRwy.angle,
                                  vecRwy.dist);
    }

    /// @brief Update rwy ends and airport with proper altitude
    /// @note Must be called from XP's main thread, otherwise Y probes won't work
    void UpdateAltitudes ()
    {
        // Airport: Center of boundaries
        alt_m = YProbe_at_m(bounds.center(), YProbe);
        
        // rwy ends
        for (RwyEndPt& re: vecRwyEndPts)            // for all rwy endpoints
            re.ComputeAlt(YProbe);
    }
    
    /// Destroy the YProbe
    static void DestroyYProbe ()
    {
        if (YProbe) {
            XPLMDestroyProbe(YProbe);
            YProbe = NULL;
        }
    }

    /// Return iterator to first rwy, or `GetTaxiEdgeVec().cend()` if none found
    vecTaxiEdgeTy::const_iterator FirstRwy () const
    {
        return std::find_if(vecTaxiEdges.cbegin(), vecTaxiEdges.cend(),
                            [](const TaxiEdge& te){return te.GetType() == TaxiEdge::RUN_WAY;});
    }
    
    /// Return iterator to next rwy after `i`, or `GetTaxiEdgeVec().cend()` if none found
    vecTaxiEdgeTy::const_iterator NextRwy (vecTaxiEdgeTy::const_iterator i) const
    {
        return std::find_if(++i, vecTaxiEdges.cend(),
                            [](const TaxiEdge& te){return te.GetType() == TaxiEdge::RUN_WAY;});
    }
    
    /// Returns a human-readable string with all runways, mostly for logging purposes
    std::string GetRwysString () const
    {
        std::string s;
        // loop all runways
        for (vecTaxiEdgeTy::const_iterator i = FirstRwy();
             i != vecTaxiEdges.cend();
             i = NextRwy(i))
        {
            if (!s.empty()) s += " / ";     // divider between runways
            try {
                s += i->GetRwyEP_A(*this).id;   // add ids of runways
                s += '-';
                s += i->GetRwyEP_B(*this).id;
            } catch(...) {}                 // shouldn't happen...!
        }
        return s;
    }
    
    // --- MARK: Bounding box

    /// Returns the bounding box of the airport as defined by all runways and taxiways
    const boundingBoxTy& GetBounds () const { return bounds; }
    
    /// Does airport contain this point?
    bool Contains (const positionTy& pos) const { return bounds.contains(pos); }
    
    /// Enlarge the bounding box by a few meters
    void EnlargeBounds_m (double meter) { bounds.enlarge_m(meter); }
    
    // --- MARK: Static Functions
    
    /// @brief Add airport to list of airports
    static void AddApt (Apt&& apt);


};  // class Apt

// Y Probe for terrain altitude computation
XPLMProbeRef Apt::YProbe = NULL;

/// Map of airports, key is the id (typically: ICAO code)
typedef std::map<std::string, Apt> mapAptTy;

/// Global map of airports
static mapAptTy gmapApt;

/// Lock to access global map of airports
static std::mutex mtxGMapApt;

// Add airport to list of airports
/// @details It is actually expected that `apt` is not yet known and really added to the map,
///          that's why the fancy debug log message is formatted first.
///          In the end, map::emplace certainly makes sure and wouldn't actually add duplicates.
void Apt::AddApt (Apt&& apt)
{
    // At this stage the airport is defined.
    // We'll now add as much space to the bounding box as
    // defined for taxiway snapping, so that positions
    // slightly outside the airport are still considered for searching:
    apt.EnlargeBounds_m(double(dataRefs.GetFdSnapTaxiDist_m()));
    
    // We sort the edges by heading, which allows for faster finding
    // of suitable edges
    std::sort(apt.vecTaxiEdges.begin(),
              apt.vecTaxiEdges.end(),
              TaxiEdge::CompHeadLess);
    
    // Fancy debug-level logging message, listing all runways
    LOG_MSG(logDEBUG, "apt.dat: Added %s at %s with %lu runways (%s) and [%lu|%lu] taxi nodes|edges",
            apt.GetId().c_str(),
            std::string(apt.GetBounds()).c_str(),
            apt.GetRwyEndPtVec().size() / 2,
            apt.GetRwysString().c_str(),
            apt.GetTaxiNodesVec().size(),
            apt.GetTaxiEdgeVec().size() - apt.GetRwyEndPtVec().size()/2);

    // Access to the list of airports is guarded by a lock
    {
        std::lock_guard<std::mutex> lock(mtxGMapApt);
        std::string key = apt.GetId();          // make a copy of the key, as `apt` gets moved soon:
        gmapApt.emplace(std::move(key), std::move(apt));
    }
}

/// Return the a node, ie. the starting point of the edge
const TaxiNode& TaxiEdge::GetA (const Apt& apt) const
{
    if (type == RUN_WAY)
        return apt.GetRwyEndPtVec()[a];
    else
        return apt.GetTaxiNodesVec()[a];
}

/// Return the b node, ie. the ending point of the edge
const TaxiNode& TaxiEdge::GetB (const Apt& apt) const
{
    if (type == RUN_WAY)
        return apt.GetRwyEndPtVec()[b];
    else
        return apt.GetTaxiNodesVec()[b];
}


//
// MARK: File Reading Thread
// This code runs in the thread for file reading operations
//

/// Read airports in the one given `apt.dat` file
static void ReadOneAptFile (std::ifstream& fIn, const boundingBoxTy& box)
{
    // Walk the file
    Apt apt;
    while (!bStopThread && fIn)
    {
        // read a line
        std::string lnBuf;
        safeGetline(fIn, lnBuf);

        // test for beginning of an airport
        if (lnBuf.size() > 10 &&
            lnBuf[0] == '1' &&
            (lnBuf[1] == ' ' || lnBuf[1] == '\t'))
        {
            // found an airport's beginning
            
            // If the previous airport is valid add it to the list
            if (apt.IsValid())
                Apt::AddApt(std::move(apt));
            else
                // clear the airport object nonetheless
                apt = Apt();
            
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(lnBuf, " \t", true);
            if (fields.size() >= 5 &&           // line contains an airport id, and
                gmapApt.count(fields[4]) == 0)  // airport is not yet defined in map
            {
                // re-init apt object, now with the proper id defined
                apt = Apt(fields[4]);
            }
        }
        
        // test for a runway...just to find location info
        else if (apt.HasId() &&             // an airport identified and of interest?
            lnBuf.size() > 20 &&            // line long enough?
            lnBuf[0] == '1' &&              // starting with "100 "?
            lnBuf[1] == '0' &&
            lnBuf[2] == '0' &&
            (lnBuf[3] == ' ' || lnBuf[3] == '\t'))
        {
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(lnBuf, " \t", true);
            if (fields.size() == 26) {      // runway description has to have 26 fields
                const double lat = std::atof(fields[ 9].c_str());
                const double lon = std::atof(fields[10].c_str());
                if (-90.0 <= lat && lat <= 90.0 &&
                    -180.0 <= lon && lon < 180.0)
                {
                    // Have we accepted the airport already?
                    // Or - this being the first rwy - does the rwy lie in the search bounding box?
                    if (apt.HasTaxiWays() ||
                        box.contains(positionTy(lat,lon)))
                    {
                        // add both runway ends to the airport
                        apt.AddRwyEnds(lat, lon,
                                       std::atof(fields[11].c_str()),   // displayced
                                       fields[ 8],                      // id
                                       // other rwy end:
                                       std::atof(fields[18].c_str()),   // lat
                                       std::atof(fields[19].c_str()),   // lon
                                       std::atof(fields[20].c_str()),   // displayced
                                       fields[17]);                     // id
                    }
                    // airport is outside bounding box -> mark it uninteresting
                    else
                    {
                        // clear the airport object
                        apt = Apt();
                    }
                }   // if lat/lon in acceptable range
            }       // if line contains 26 field values
        }           // if a runway line startin with "100 "
        
        // test for a taxi network node
        else if (apt.HasId() &&
                 lnBuf.size() >= 15 &&           // line long enough?
                 lnBuf[0] == '1' &&              // starting with "1201 "?
                 lnBuf[1] == '2' &&
                 lnBuf[2] == '0' &&
                 lnBuf[3] == '1' &&
                 (lnBuf[4] == ' ' || lnBuf[4] == '\t'))
        {
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(lnBuf, " \t", true);
            // We need fields 2, 3, the location, and 5, the index, only
            if (fields.size() >= 5) {
                // Convert and briefly test the given location
                const double lat = std::atof(fields[1].c_str());
                const double lon = std::atof(fields[2].c_str());
                const size_t idx = (size_t)std::atol(fields[4].c_str());
                if (-90.0 <= lat && lat <= 90.0 &&
                    -180.0 <= lon && lon < 180.0)
                {
                    apt.AddTaxiNode(lat, lon, idx);
                }   // has valid location
            }       // enough fields in line?
        }           // if a taxi network node ("1201 ")

        // test for a taxi network edge
        else if (apt.HasId() &&
                 lnBuf.size() >= 8 &&            // line long enough?
                 lnBuf[0] == '1' &&              // starting with "1201 "?
                 lnBuf[1] == '2' &&
                 lnBuf[2] == '0' &&
                 lnBuf[3] == '2' &&
                 (lnBuf[4] == ' ' || lnBuf[4] == '\t'))
        {
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(lnBuf, " \t", true);
            // We need fields 2, 3 only, the node indexes
            if (fields.size() >= 3) {
                // Convert indexes and try adding the node
                const size_t n1 = (size_t)std::atol(fields[1].c_str());
                const size_t n2 = (size_t)std::atol(fields[2].c_str());
                apt.AddTaxiEdge(n1, n2);
            }       // enough fields in line?
        }           // if a taxi network edge ("1202 ")

    }               // for each line of the apt.dat file
    
    // If the last airport read is valid don't forget to add it to the list
    if (apt.IsValid())
        Apt::AddApt(std::move(apt));
}

/// @brief Remove airports that are now considered too far away
void PurgeApt (const boundingBoxTy& _box)
{
    // Access is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGMapApt);

    // loop all airports and remove those, whose center point is outside the box
    mapAptTy::iterator iter = gmapApt.begin();
    while (iter != gmapApt.end())
    {
        // Is airport still in box?
        Apt& apt = iter->second;
        if (apt.GetBounds().overlap(_box)) {
            // keep it, move on to next airport
            ++iter;
        } else {
            // remove it, move on to next airport
            LOG_MSG(logDEBUG, "apt.dat: Removed %s at %s",
                    apt.GetId().c_str(),
                    std::string(apt.GetBounds()).c_str());
            iter = gmapApt.erase(iter);
        }
    }
    
    LOG_MSG(logDEBUG, "Done purging, %d airports left", (int)gmapApt.size());
}

/// @brief Read airports from apt.dat files around a given center position
/// @details This function first walks along the `scenery_packs.ini` file
///          and reads all `apt.dat` files available in the scenery packs listed there in the given order.
///          Lastly, it also reads the generic `apt.dat` file given in `APTDAT_RESOURCES_DEFAULT`.
/// @see Understanding scener order: https://www.x-plane.com/kb/changing-custom-scenery-load-order-in-x-plane-10/
/// @param ctr Center position
/// @param radius Search radius around center position in meter
void AsyncReadApt (positionTy ctr, double radius)
{
    static size_t lenSceneryLnBegin = strlen(APTDAT_SCENERY_LN_BEGIN);
    
    // To avoid costly distance calculations we define a bounding box
    // just by calculating lat/lon values north/east/south/west of given pos
    // and include all airports with coordinates falling into it
    const boundingBoxTy box (ctr, radius);
    
    // --- Cleanup first: Remove too far away airports ---
    PurgeApt(box);
    
    // --- Add new airports ---
    // Count the number of files we have accessed
    int cntFiles = 0;

    // Try opening scenery_packs.ini
    std::ifstream fScenery (LTCalcFullPath(APTDAT_SCENERY_PACKS));
    while (!bStopThread && fScenery.good() && fScenery.is_open())
    {
        // read a line from scenery_packs.ini
        std::string lnScenery;
        safeGetline(fScenery, lnScenery);
        
        // we only process lines starting with "SCENERY_PACK ",
        // ie. we skip any header info and also lines with SCENERY_PACK_DISABLED
        if (lnScenery.length() <= lenSceneryLnBegin ||
            lnScenery.substr(0,lenSceneryLnBegin) != APTDAT_SCENERY_LN_BEGIN)
            continue;
        
        // the remainder is a path into X-Plane's main folder
        lnScenery.erase(0,lenSceneryLnBegin);
        lnScenery = LTCalcFullPath(lnScenery);      // make it a full path
        lnScenery += APTDAT_SCENERY_ADD_LOC;        // add the location to the actual `apt.dat` file
        
        // open that apt.dat
        std::ifstream fIn (lnScenery);
        if (fIn.good() && fIn.is_open()) {
            LOG_MSG(logDEBUG, "Reading apt.dat from %s", lnScenery.c_str());
            ReadOneAptFile(fIn, box);
            cntFiles++;
        }
        
        // problem was not just "not found" (which we ignore for scenery packs) or eof?
        if (!fIn && errno != ENOENT && !fIn.eof()) {
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            LOG_MSG(logERR, ERR_CFG_FILE_READ,
                    lnScenery.c_str(), sErr);
        }
        
        fIn.close();

    } // processing scenery_packs.ini
    
    // Last but not least we also process the global generic apt.dat file
    if (!bStopThread)
    {
        const std::string sFileName = LTCalcFullPath(APTDAT_RESOURCES_DEFAULT APTDAT_SCENERY_ADD_LOC);
        std::ifstream fIn (sFileName);
        if (fIn.good() && fIn.is_open()) {
            LOG_MSG(logDEBUG, "Reading apt.dat from %s", sFileName.c_str());
            ReadOneAptFile(fIn, box);
            cntFiles++;
        }

        // problem was not just eof?
        if (!fIn && !fIn.eof()) {
            char sErr[SERR_LEN];
            strerror_s(sErr, sizeof(sErr), errno);
            LOG_MSG(logERR, ERR_CFG_FILE_READ,
                    sFileName.c_str(), sErr);
        }
        
        fIn.close();
    }
    
    // Not successful in opening ANY apt.dat file?
    if (!cntFiles) {
        SHOW_MSG(logWARN, WARN_APTDAT_FAILED);
        return;
    }
    
    LOG_MSG(logDEBUG, "Done reading from %d apt.dat files, have now %d airports",
            cntFiles, (int)gmapApt.size());
}

//
// MARK: Utility Functions
//

/// Find airport, which contains passed-in position, can be `nullptr`
Apt* LTAptFind (const positionTy& pos)
{
    for (auto& pair: gmapApt)
        if (pair.second.Contains(pos))
            return &pair.second;
    return nullptr;
}

//
// MARK: X-Plane Main Thread
// This code runs in X-Plane's thread, called from XP callbacks
//

/// Is currently an async operation running to refresh the airports from apt.dat?
static std::future<void> futRefreshing;
        
/// Last position for which airports have been read
static positionTy lastCameraPos;

/// New airports added, so that a call to LTAptUpdateRwyAltitude(9 is necessary?
static bool bAptsAdded = false;
        
// Start reading apt.dat file(s)
bool LTAptEnable ()
{
    LTAptRefresh();
    return true;
}

/// Update altitudes of runways
void LTAptUpdateRwyAltitudes ()
{
    // access is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGMapApt);

    // loop all airports and their runways
    for (mapAptTy::value_type& p: gmapApt)
        p.second.UpdateAltitudes();
    
    LOG_MSG(logDEBUG, "apt.dat: Finished updating ground altitudes");
}

// Update the airport data with airports around current camera position
void LTAptRefresh ()
{
    // Safety check: Thread already running?
    // Future object is valid, i.e. initialized with an async operation?
    if (futRefreshing.valid() &&
        // but status is not yet ready?
        futRefreshing.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            // then stop here
            return;
        
    // Distance since last read not far enough?
    // Must have travelled at least as far as standard search radius for planes:
    const positionTy camera = DataRefs::GetViewPos();
    if (!camera.isNormal(true))                     // have no good camery position (yet)
        return;

    double radius = dataRefs.GetFdStdDistance_m();
    if (lastCameraPos.dist(camera) < radius)        // is false if lastCameraPos is NAN
    {
        // Didn't move far, so no new scan for new airports needed.
        // But do we need to check for rwy altitudes after last scan of apt.dat file?
        if (bAptsAdded) {
            LTAptUpdateRwyAltitudes();
            LTAptLocalCoordsUpdate(false);
        }
        bAptsAdded = false;
        return;
    }
    else
        lastCameraPos = camera;

    // Start the thread to read apt.dat, using current camera position as center point
    // and _double_ plane search radius as search radius
    radius *= 2;
    LOG_MSG(logDEBUG, "Starting thread to read apt.dat for airports %.1fnm around %s",
            radius / M_per_NM, std::string(lastCameraPos).c_str());
    bStopThread = false;
    futRefreshing = std::async(std::launch::async,
                               AsyncReadApt, lastCameraPos, radius);
    // need to check for rwy altitudes soon!
    bAptsAdded = true;
}

// Update local coordinate system's values due to ref point change
void LTAptLocalCoordsUpdate (bool bForce)
{
    // access is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGMapApt);
    for (auto& pair: gmapApt)
        pair.second.LocalCoordsUpdate(bForce);
    LOG_MSG(logDEBUG, "apt.dat: Finished updating local coordinates");
}

// Return the best possible runway to auto-land at
positionTy LTAptFindRwy (const LTAircraft& _ac)
{
    // --- Preparation of aircraft-related data ---
    // allowed VSI range depends on aircraft model, converted to m/s
    const double vsi_min = _ac.mdl.VSI_FINAL * ART_RWY_MAX_VSI_F * Ms_per_FTm;
    const double vsi_max = _ac.mdl.VSI_FINAL / ART_RWY_MAX_VSI_F * Ms_per_FTm;
    
    // last known go-to position of aircraft, serving as start of search
    const positionTy& from = _ac.GetToPos();
    // The heading we compare the runway with is normalized to [0..180)
    double headSearch = HeadingNormalize(from.heading());
    bool bHeadInverted = false;
    if (headSearch >= 180.0) {
        headSearch -= 180.0;
        bHeadInverted = true;
    }

    // The speed to use, cut off at a reasonable approach speed:
    const double speed_m_s = std::min (_ac.GetSpeed_m_s(),
                                       _ac.mdl.FLAPS_DOWN_SPEED * ART_APPR_SPEED_F / KT_per_M_per_S);
    
    // --- Variables holding Best Match ---
    const Apt* bestApt = nullptr;               // best matching apt
    const RwyEndPt* bestRwyEndPt = nullptr;     // best matching runway endpoint
    // The heading diff of the best match to its runway
    // (initialized to the max allowed value so that worse heading diffs aren't considered)
    double bestHeadingDiff = ART_RWY_MAX_HEAD_DIFF;
    // when would we arrive there?
    double bestArrivalTS = NAN;
    
    // --- Iterate the airports ---
    // Access to the list of airports is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGMapApt);

    // loop over airports
    for (mapAptTy::const_iterator iterApt = gmapApt.cbegin();
         iterApt != gmapApt.cend();
         ++iterApt)
    {
        const Apt& apt = iterApt->second;
        
        // Find the runways matching the current plane's heading
        lstTaxiEdgeCPtrTy lstRwys;
        if (apt.FindEdgesForHeading(headSearch,
                                    ART_RWY_MAX_HEAD_DIFF,
                                    lstRwys,
                                    TaxiEdge::RUN_WAY))
        {
            // loop over found runways of this airport
            for (const TaxiEdge* e: lstRwys)
            {
                // The rwy end point we are (potentially) aiming at
                const RwyEndPt& rwyEP = bHeadInverted ? e->GetRwyEP_B(apt) : e->GetRwyEP_A(apt);
                
                // We need to know the runway's altitude for what comes next
                if (std::isnan(rwyEP.alt_m))
                    continue;

                // Heading towards rwy, compared to current flight's heading
                // (Find the rwy which requires least turn now.)
                const double bearing = CoordAngle(from.lat(), from.lon(), rwyEP.lat, rwyEP.lon);
                const double headingDiff = fabs(HeadingDiff(from.heading(), bearing));
                if (headingDiff > bestHeadingDiff)      // worse than best known match?
                    continue;
                
                // 3. Vertical speed, for which we need to know distance / flying time
                const double dist = CoordDistance(from.lat(), from.lon(), rwyEP.lat, rwyEP.lon);
                const double d_ts = dist / speed_m_s;
                const double vsi = (rwyEP.alt_m - from.alt_m()) / d_ts;
                if (vsi < vsi_min || vsi > vsi_max)
                    continue;
                
                // We've got a match!
                bestApt = &apt;
                bestRwyEndPt = &rwyEP;
                bestHeadingDiff = headingDiff;      // the heading diff (which would be a selection criterion on several rwys match)
                bestArrivalTS = from.ts() + d_ts;   // the arrival timestamp
            }
        }
    }
    
    // Didn't find a suitable runway?
    if (!bestRwyEndPt || !bestApt) {
        LOG_MSG(logDEBUG, "Didn't find runway for %s with heading %.0f°",
                std::string(_ac).c_str(),
                from.heading());
        return positionTy();
    }
    
    // Found a match!
    positionTy retPos = positionTy(bestRwyEndPt->lat,
                                   bestRwyEndPt->lon,
                                   bestRwyEndPt->alt_m);
    retPos.ts() = bestArrivalTS;                        // Arrival time
    retPos.flightPhase = LTAPIAircraft::FPH_TOUCH_DOWN; // This is a calculated touch-down point
    LOG_MSG(logDEBUG, "Found runway %s/%s at %s for %s",
            bestApt->GetId().c_str(),
            bestRwyEndPt->id.c_str(),
            std::string(retPos).c_str(),
            std::string(_ac).c_str());
    return retPos;
}


// Snaps the passed-in position to the nearest rwy or taxiway if appropriate
bool LTAptSnap (positionTy& pos, bool bLogging)
{
    // Configured off?
    if (dataRefs.GetFdSnapTaxiDist_m() <= 0)
        return false;
    
    // Access to the list of airports is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGMapApt);

    // Which airport are we looking at?
    Apt* pApt = LTAptFind(pos);
    if (!pApt)                          // not a position in any airport's bounding box
        return false;

    // Let's snap!
    return pApt->SnapToTaxiway(pos, bLogging);
}


// Cleanup
void LTAptDisable ()
{
    // Stop all threads
    bStopThread = true;
    
    // wait for refresh function
    if (futRefreshing.valid())
        futRefreshing.wait();
    
    // destroy the Y Probe
    Apt::DestroyYProbe();
}

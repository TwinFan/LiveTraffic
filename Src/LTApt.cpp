/// @file       LTApt.cpp
/// @brief      Access to X-Plane's `apt.dat` file(s) and data
/// @details    Scans `apt.dat` file for airport, runway, and taxiway information.\n
///             Finds potential runway for an auto-land flight.\n
///             Finds center lines on runways and taxiways to snap positions to.
/// @details    Definitions:\n
///             Node: A position, where edges connect, relates to a 111-116 line in `apt.dat`\n
///             Edge: The connection of two nodes, relates to two consecutive 111-116 lines in `apt.dat`\n
///             Path: A set of connecting edges, relates to a gorup of 111-116 lines in `apt.dat`, headed by a 120 line\n
/// @see        More information on reading from `apt.dat` is on [a separate page](@ref apt_dat).
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
/// XP12 Alpha introduces a special entry directing us to someplace else
#define APTDAT_SCENERY_GLOBAL_APTS "*GLOBAL_AIRPORTS*"
/// Path to add after the scenery pack location read from the ini file
#define APTDAT_SCENERY_ADD_LOC "Earth nav data/apt.dat"
/// Path to the global airports file under Resources / Default
#define APTDAT_RESOURCES_DEFAULT "Resources/default scenery/default apt dat/"
/// Path to the global airports file starting in XP12 under Resources / Default
#define APTDAT_GLOBAL_AIRPORTS "Global Scenery/Global Airports/"

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

/// Vector of indexes into another vector (e.g. indexes into the vector of edges, sorted by angle)
typedef std::vector<size_t> vecIdxTy;

/// @brief A position as read from apt.dat, stored temporarily, before turned into a TaxiNode
struct TaxiTmpPos : public ptTy {
    size_t      refCnt = 0;             ///< number of usages in paths
    
    /// Constructor
    TaxiTmpPos (double _lat, double _lon) : ptTy (_lon,_lat), refCnt(1) {}
    
    // access to latitude/longitude
    double& lat()       { return y; }         /// latitude (stored in `y`)
    double  lat() const { return y; }         /// latitude (stored in `y`)
    double& lon()       { return x; }         /// longitude (stored in `y`)
    double  lon() const { return x; }         /// longitude (stored in `y`)

    /// Increase ref count
    size_t inc() { return ++refCnt; }
    /// Is a joint in the sense that 2 or more _paths_ (not egdes) connect?
    bool isJoint() const { return refCnt >= 2; }
};

/// A map sorted by lat. That makes it a little easier to find "nearby" positions
typedef std::map<double,TaxiTmpPos> mapTaxiTmpPosTy;

/// @brief Temporarily stores a path definition as read from apt.dat before post-processing
struct TaxiTmpPath {
    std::list<TaxiTmpPos> listPos;      ///< list of positions making up the path
};

/// The list of paths read from apt.dat before post-processing
typedef std::list<TaxiTmpPath> listTaxiTmpPathTy;

/// @brief A node of a taxi way
/// @details Depending on scenery and search range we might need to read and store
///          tenth of thousans of these, so we limit the members as much as possible,
///          e.g. we don't use ::positionTy but only lat/lon/x/z.
class TaxiNode {
public:
    double      lat;                    ///< latitude
    double      lon;                    ///< longitude
    vecIdxTy    vecEdges;               ///< vector of edges connecting to this node, stored as indexes into Apt::vecTaxiEdges
    // attributes needed by Dijkstra's shortest path algorithm
    double      pathLen  = HUGE_VAL;    ///< current best known path length to this node
    size_t      prevIdx  = ULONG_MAX;   ///< previous node on shortest path
    bool        bVisited = false;       ///< has node been fully analyzed
public:
    /// Default constructor leaves all empty
    TaxiNode () : lat(NAN), lon(NAN) {}
    /// Typical constructor requires a location
    TaxiNode (double _lat, double _lon) : lat(_lat), lon(_lon) {}
    
    /// Initialize Dijkstra attribues
    void InitDijkstraAttr ()
    { pathLen = HUGE_VAL; prevIdx = ULONG_MAX; bVisited = false; }

    /// Is node valid in terms of geographic coordinates?
    bool HasGeoCoords () const { return !std::isnan(lat) && !std::isnan(lon); }

    /// Compares to given lat/long
    bool CompEqualLatLon (double _lat, double _lon) const
    { return dequal(lat, _lat) && dequal(lon, _lon); }
    
    /// Comparison function for equality based on lat/lon
    static bool CompEqualLatLon (const TaxiNode& a, const TaxiNode& b)
    { return a.CompEqualLatLon(b.lat, b.lon); }

    /// Equality is based solely on geographic position
    bool operator== (const TaxiNode& o) const
    { return CompEqualLatLon(o.lat, o.lon); }
    
    /// Proximity is based on a given max distance
    bool IsCloseTo (const TaxiNode& o, double _maxDist)
    { return DistLatLonSqr(lat, lon, o.lat, o.lon) <= (_maxDist*_maxDist); }
};

/// Vector of taxi nodes
typedef std::vector<TaxiNode> vecTaxiNodesTy;

/// A runway endpoint is a special node of which we need to know the altitude
class RwyEndPt : public TaxiNode {
public:
    std::string id;                     ///< rwy identifier, like "23" or "05R"
    double      alt_m = NAN;            ///< ground altitude in meter
    double      heading = NAN;          ///< rwy heading

public:
    /// Default constructor leaves all empty
    RwyEndPt () : TaxiNode () {}
    /// Typical constructor fills id and location
    RwyEndPt (const std::string& _id, double _lat, double _lon, double _heading) :
    TaxiNode(_lat, _lon), id(_id), heading(_heading) {}

    /// Compute altitude if not yet known
    void ComputeAlt (XPLMProbeRef& yProbe)
    {
        if (std::isnan(alt_m))
            alt_m = YProbe_at_m(positionTy(lat,lon,0.0), yProbe);
    }
};

/// Vector of runway endpoints
typedef std::vector<RwyEndPt> vecRwyEndPtTy;

/// Startup location (row code 1300)
class StartupLoc : public TaxiNode {
public:
    std::string id;                     ///< all text after the coordinates, mostly for internal id purposes
    double  heading = NAN;              ///< heading the plane stands at this location
    ptTy    viaPos;                     ///< position via which to leave startup location,

public:
    /// Default constructor leaves all empty
    StartupLoc () : TaxiNode () {}
    /// Typical constructor fills id and location
    StartupLoc (const std::string& _id, double _lat, double _lon, double _heading) :
    TaxiNode(_lat, _lon), id(_id), heading(_heading) {}
};

/// Vector of startup locations
typedef std::vector<StartupLoc> vecStartupLocTy;

/// @brief An edge in the taxi / rwy network, connected two nodes
/// @details TaxiEdge can only store _indexes_ into the vector of nodes,
///          which is Apt::vecTaxiNodes. It cannot directly store pointers or references,
///          as the memory location might change when the vector reorganizes due to
///          additions.\n
///          This also means that some functions otherwise better suited here are now
///          moved to Apt as only Apt has access to all vectors.
class TaxiEdge {
public:
    /// Taxiway or runway?
    enum edgeTy {
        UNKNOWN_WAY = 0,                ///< edge is of undefined type
        RUN_WAY = 1,                    ///< edge is for runway
        TAXI_WAY,                       ///< edge is for taxiway
        REMOVED_WAY,                    ///< edge has been removed during post-processing
    };
    
protected:
    edgeTy type = UNKNOWN_WAY;          ///< type of node (runway, taxiway)
    size_t      a = UINT_MAX;           ///< from node (index into vecTaxiNodes)
    size_t      b = UINT_MAX;           ///< to node (index into vecTaxiNodes)
public:
    double angle;                       ///< angle/heading from a to b
    double dist_m;                      ///< distance in meters between a and b
public:
    /// Constructor
    TaxiEdge (edgeTy _t, size_t _a, size_t _b, double _angle, double _dist_m) :
    type(_t), a(_a), b(_b), angle(_angle), dist_m(_dist_m)
    {
        Normalize();
    }
    
    /// Normalize myself to 0 <= angle < 180
    void Normalize ()
    {
        if (angle >= 180.0) {
            std::swap(a,b);
            angle -= 180.0;
        }
    }
    
    /// a valid egde to be used?
    bool isValid () const { return type == RUN_WAY || type == TAXI_WAY; }
    
    /// Return the node's type
    edgeTy GetType () const { return type; }
    
    /// Equality is based on type and nodes
    bool operator== (const TaxiEdge& o) const
    { return type == o.type && a == o.a && b == o.b; }
    
    /// Return the a node, ie. the starting point of the edge
    const TaxiNode& GetA (const Apt& apt) const;
    /// Return the b node, ie. the ending point of the edge
    const TaxiNode& GetB (const Apt& apt) const;
    
    /// Return the angle, adjust in a way that it points away from node n (which must be either TaxiEdge::a or TaxiEdge::b)
    double GetAngleFrom (size_t n) const { return n == a ? angle : angle + 180.0; }
    /// Returns the edge's angle, which is closest to the given heading
    double GetAngleByHead (double heading) const
    { return std::abs(HeadingDiff(heading, angle)) < 90.0 ? angle : angle + 180.0; }
    /// Return the taxi node, that is the "start" when heading in the given direction
    const TaxiNode& startByHeading (const Apt& apt, double heading) const
    { return std::abs(HeadingDiff(heading, angle)) < 90.0 ? GetA(apt) : GetB(apt); }
    /// Return the taxi node, that is the "end" when heading in the given direction
    const TaxiNode& endByHeading (const Apt& apt, double heading) const
    { return std::abs(HeadingDiff(heading, angle)) < 90.0 ? GetB(apt) : GetA(apt); }

    size_t startNode() const { return a; }      ///< index of start node
    size_t endNode()   const { return b; }      ///< index of end node
    size_t startByHeading (double heading) const  ///< Return the index of that node that is the edge's start if if looking in given direction
    { return std::abs(HeadingDiff(heading, angle)) < 90.0 ? a : b; }
    size_t endByHeading (double heading) const  ///< Return the index of that node that is the edge's end if if looking in given direction
    { return std::abs(HeadingDiff(heading, angle)) < 90.0 ? b : a; }
    
    size_t otherNode(size_t n) const { return n == a ? b : a; } ///< returns the "other" node (`n` should be TaxiEdge::a or TaxiEdge::b)

    /// sets a new end node, usually when splitting edges
    void SetEndNode (size_t _b, double _angle, double _dist_m)
    {
        b = _b;
        angle = _angle;
        dist_m = _dist_m;
        Normalize();
    }
    
    /// Replaces on node with the other (but does not recalc angle/dist!)
    void ReplaceNode (size_t oldIdxN, size_t newIdxN)
    {
        if (a == oldIdxN)
            a = newIdxN;
        else if (b == oldIdxN)
            b = newIdxN;
        // if this leads to both nodes being the same then we are removed
        if (a == b) {
            type = REMOVED_WAY;
            angle = NAN;
            dist_m = NAN;
        }
    }
    
};

/// Vector of taxi edges
typedef std::vector<TaxiEdge> vecTaxiEdgeTy;

/// Represents an airport as read from apt.dat
class Apt {
protected:
    std::string id;                     ///< ICAO code or other unique id
    boundingBoxTy bounds;               ///< bounding box around airport, calculated from rwy and taxiway extensions
    double alt_m = NAN;                 ///< the airport's altitude
    vecTaxiNodesTy vecTaxiNodes;        ///< vector of taxi network nodes
    vecRwyEndPtTy  vecRwyEndPts;        ///< vector of runway endpoints
    vecTaxiEdgeTy  vecTaxiEdges;        ///< vector of taxi network edges, each connecting any two nodes
    vecIdxTy       vecTaxiEdgesIdxHead; ///< vector of indexes into Apt::vecTaxiEdges, sorted by TaxiEdge::angle
    vecStartupLocTy vecStartupLocs;     ///< vector of startup locations
    
    static vecTaxiNodesTy vecRwyNodes;  ///< temporary storage for rwy ends (to add egdes for the rwy later)
    static mapTaxiTmpPosTy mapPos;      ///< temporary storage for positions while reading apt.dat
    static listTaxiTmpPathTy listPaths; ///< temporary storage for paths while reading apt.dat
    static vecIdxTy vecPathEnds;        ///< temporary storage for path endpoints (idx into Apt::vecTaxiNodes)
    static XPLMProbeRef YProbe;         ///< Y Probe for terrain altitude computation
    
#ifdef DEBUG
public:
    vecTaxiNodesTy vecBezierHandles;
#endif

public:
    /// Constructor expects an id
    Apt (const std::string& _id = "") : id(_id) {}
    
    /// Id of the airport, typicall the ICAO code
    std::string GetId () const { return id; }
    /// Is any id defined? (Used as indicator while reading in `apt.dat`)
    bool HasId () const { return !id.empty(); }
    
    /// Valid airport definition requires an id and some taxiways / runways
    bool IsValid () const { return HasId() && HasRwyEndpoints(); }
    /// Temporary arrays filled, so we can created nodes/edges?
    bool HasTempNodesEdges () const { return !mapPos.empty() && !listPaths.empty(); }
    
    /// Return a reasonable altitude...effectively one of the rwy ends' altitude
    double GetAlt_m () const { return alt_m; }
    
    // --- MARK: Temporary data while reading apt.dat
    
    /// @brief Find a similar position in Apt::mapPos
    /// @param _lat Latitude to search for
    /// @param _lon Longitude to search for
    /// @param _bDoInc Shall the reference counter of the found object be incremented?
    /// @return Pointer to closest position, or `nullptr` if no position is deemed "similar"
    TaxiTmpPos* GetSimilarTaxiTmpPos (double _lat, double _lon, bool _bDoInc)
    {
        constexpr double latDiff = Dist2Lat(APT_MAX_SIMILAR_NODE_DIST_M);
        const     double lonDiff = Dist2Lon(APT_MAX_SIMILAR_NODE_DIST_M, _lat);

        // mapPos is sorted by latitude, so we can restrict our search
        // to the matching latitude range
        double bestDist2 = HUGE_VAL;
        mapTaxiTmpPosTy::iterator bestIter = mapPos.end();
        for (mapTaxiTmpPosTy::iterator lIter = mapPos.lower_bound(_lat - latDiff);
             lIter != mapPos.end() && lIter->second.lat() <= _lat + latDiff;
             ++lIter)
        {
            // checking also for matching longitude range
            if (std::abs(lIter->second.lat() - _lat) < latDiff &&
                std::abs(lIter->second.lon() - _lon) < lonDiff) {
                // find shortest distance
                const double dist2 = DistLatLonSqr(_lat, _lon, lIter->second.lat(), lIter->second.lon());
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    bestIter = lIter;
                }
            }
        }
        
        // Found something?
        if (bestIter != mapPos.end()) {
            if (_bDoInc)
                bestIter->second.inc();
            return &(bestIter->second);
        }
        
        // Found nothing
        return nullptr;
    }
    
    /// Add a temporary position or increment reference counter of a "similar" one
    void AddTaxiTmpPos (double _lat, double _lon)
    {
        if (!GetSimilarTaxiTmpPos(_lat, _lon, true))
            mapPos.emplace(_lat, TaxiTmpPos(_lat,_lon));
    }
    
    /// Moves the edge to the temporary storage
    void AddTaxiTmpPath (TaxiTmpPath&& path)
    {
        listPaths.emplace_back(std::move(path));
    }
    
    // --- MARK: Taxiways
    
    /// The vector of taxi network nodes
    const vecTaxiNodesTy& GetTaxiNodesVec () const { return vecTaxiNodes; }
    /// The list of taxi network edges
    const vecTaxiEdgeTy& GetTaxiEdgeVec () const { return vecTaxiEdges; }
    
    /// Any taxiways/runways defined?
    bool HasTaxiWays () const { return !vecTaxiEdges.empty(); }
    
    /// Is given node connected to a rwy?
    bool IsConnectedToRwy (size_t idxN) const
    {
        const TaxiNode& n = vecTaxiNodes[idxN];
        for (size_t idxE: n.vecEdges)
            if (vecTaxiEdges[idxE].GetType() == TaxiEdge::RUN_WAY)
                return true;
        return false;
    }
    
    /// Return the edge's idx, which connects the two given nodes, or `EDGE_UNAVAIL`
    size_t GetEdgeBetweenNodes (size_t idxA, size_t idxB)
    {
        const TaxiNode& a = vecTaxiNodes.at(idxA);
        for (size_t idxE: a.vecEdges) {
            const TaxiEdge& e = vecTaxiEdges[idxE];
            if (e.otherNode(idxA) == idxB)
                return idxE;
        }
        return EDGE_UNAVAIL;
    }

    /// return index of closest taxi node within a "close-by" distance (or ULONG_MAX if none close enough)
    size_t GetSimilarTaxiNode (double _lat, double _lon,
                               size_t dontCombineWith = ULONG_MAX) const
    {
        constexpr double latDiff = Dist2Lat(APT_MAX_SIMILAR_NODE_DIST_M);
        const     double lonDiff = Dist2Lon(APT_MAX_SIMILAR_NODE_DIST_M, _lat);
        
        double bestDist2 = HUGE_VAL;
        const vecTaxiNodesTy::const_iterator dontIter =
        dontCombineWith == ULONG_MAX ? vecTaxiNodes.cend() :
                                       std::next(vecTaxiNodes.cbegin(),(long)dontCombineWith);
        vecTaxiNodesTy::const_iterator bestIter = vecTaxiNodes.cend();
        for (auto iter = vecTaxiNodes.cbegin(); iter != vecTaxiNodes.cend(); ++iter)
        {
            // quick check: reasonable lat/lon range
            if (iter != dontIter &&         // not the one node to skip
                std::abs(iter->lat - _lat) < latDiff &&
                std::abs(iter->lon - _lon) < lonDiff)
            {
                // find shortest distance
                const double dist2 = DistLatLonSqr(_lat, _lon, iter->lat, iter->lon);
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    bestIter = iter;
                }
            }
        }
        
        // Found something?
        return (bestIter != vecTaxiNodes.cend() ?
                (size_t)std::distance(vecTaxiNodes.cbegin(), bestIter) :
                ULONG_MAX);
    }
    
    /// @brief Add a new taxi network node
    /// @return Index of node in Apt::vecTaxiNodes
    size_t AddTaxiNode (double lat, double lon,
                        size_t dontCombineWith = ULONG_MAX,
                        bool bSearchForSimilar = true)
    {
        // Is there a similar close-by node already?
        if (bSearchForSimilar) {
            const size_t idx = GetSimilarTaxiNode(lat, lon, dontCombineWith);
            if (idx != ULONG_MAX)
                return idx;
        }
        
        bounds.enlarge_pos(lat, lon);           // Potentially expands the airport's boundary
        vecTaxiNodes.emplace_back(lat, lon);    // Add the node to the back of the list
        return vecTaxiNodes.size()-1;           // return the index
    }
    
    /// @brief Add a new taxi network node at a given index position
    void AddTaxiNodeFixed (double lat, double lon, size_t idx)
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
    /// @return Index into Apt::vecTaxiEdges, or ULONG_MAX if unsuccessful
    size_t AddTaxiEdge (size_t n1, size_t n2,
                        TaxiEdge::edgeTy _type = TaxiEdge::TAXI_WAY)
    {
        // Actual nodes must be valid, throws exception if not
        TaxiNode& a = vecTaxiNodes.at(n1);
        TaxiNode& b = vecTaxiNodes.at(n2);
        if (!a.HasGeoCoords() || !b.HasGeoCoords())
        {
            LOG_MSG(logDEBUG, "apt.dat: Node %lu or %lu invalid! Edge not added.",
                    (long unsigned)n1, (long unsigned)n2);
            return ULONG_MAX;
        }
        
        // Add the edge
        vecTaxiEdges.emplace_back(_type, n1, n2,
                                  CoordAngle(a.lat, a.lon, b.lat, b.lon),
                                  DistLatLon(a.lat, a.lon, b.lat, b.lon));
        
        // Tell the nodes they've got a new connection
        const size_t eIdx = vecTaxiEdges.size()-1;
        a.vecEdges.push_back(eIdx);
        b.vecEdges.push_back(eIdx);
        
        return eIdx;
    }
    
    /// Recalc heading and angle of a given edge
    void RecalcTaxiEdge (size_t eIdx)
    {
        TaxiEdge& e = vecTaxiEdges.at(eIdx);
        const TaxiNode& a = e.GetA(*this);
        const TaxiNode& b = e.GetB(*this);
        e.angle  = CoordAngle(a.lat, a.lon, b.lat, b.lon);
        e.dist_m = DistLatLon(a.lat, a.lon, b.lat, b.lon);
        
        // Did this change the orientation? Then we need to swap a<->b
        e.Normalize();
    }
    
    /// Split an edge by inserting a given node
    void SplitEdge (size_t eIdx, size_t insNode)
    {
        // 1. Remember the original target edge
        TaxiEdge& e = vecTaxiEdges.at(eIdx);
        if (insNode == e.startNode() || insNode == e.endNode())
            return;
        size_t joinOrigB = e.endNode();
        TaxiNode& origB = vecTaxiNodes[joinOrigB];
        
        // 2. Short-cut existing node at new joint
        const TaxiNode& a = e.GetA(*this);
        TaxiNode& b = vecTaxiNodes.at(insNode);
        
        const double newAngle = CoordAngle(a.lat, a.lon, b.lat, b.lon);
        const double newDist  = DistLatLon(a.lat, a.lon, b.lat, b.lon);
        
        // It could be that insNode is slightly _before_ a
        if (HeadingDiff(newAngle, e.angle) > 90.0)
        {
            // then we connect them by inserting a completely new node before a
            AddTaxiEdge(insNode, e.startNode(), e.GetType());
        }
        // else it could be that it is slightly _beyond_ origB
        else if (newDist > e.dist_m)
        {
            // then we connect them by inserting a completely new node after origB
            AddTaxiEdge(joinOrigB, insNode, e.GetType());
        }
        else
        {
            // Typical case of insNode between a and origB:
            // We have e now end at insNode:
            e.SetEndNode(insNode, newAngle, newDist);

            // Node insNode/b now got one more edge connection, origB currently one less
            b.vecEdges.push_back(eIdx);
            for (auto iter = origB.vecEdges.begin();    // std::remove(_if) should have done the job...but it simply didn't
                 iter != origB.vecEdges.end();)
            {
                if (*iter == eIdx)
                    iter = origB.vecEdges.erase(iter);
                else
                    ++iter;
            }
            
            // 3. Add new edge between insNode and joinOrigB
            AddTaxiEdge(insNode, joinOrigB, e.GetType());
        }
    }
    
    /// @brief Replace one node with the other. Afterwards, oldN is unused
    /// @details All edges connect to `old` are connected to `new` instead.
    ///          This only changes the respective node of the attached edges.
    void ReplaceNode (size_t oldIdxN, size_t newIdxN)
    {
        // All edges using oldN need to be changed to use newN instead
        TaxiNode& oldN = vecTaxiNodes.at(oldIdxN);
        TaxiNode& newN = vecTaxiNodes.at(newIdxN);
        while (!oldN.vecEdges.empty())
        {
            // The edge to work on
            const size_t idxE = oldN.vecEdges.back();
            oldN.vecEdges.pop_back();
            TaxiEdge& e = vecTaxiEdges[idxE];
            
            // Replace the node in the edge and recalculate the edge
            e.ReplaceNode(oldIdxN, newIdxN);
            if (e.isValid()) {
                RecalcTaxiEdge(idxE);
                // Add the edge to the new node
                newN.vecEdges.push_back(idxE);
            } else {
                // no longer a valid edge, remove it from the node
                for (auto i = newN.vecEdges.begin();
                     i != newN.vecEdges.end();)
                {
                    if (*i == idxE)
                        i = newN.vecEdges.erase(i);
                    else
                        ++i;
                }
            }
        }
    }
    
    /// @brief Fill the indirect vector, which sorts edges by heading
    void SortTaxiEdges ()
    {
        // We add all valid(!) edges to the sort array
        vecTaxiEdgesIdxHead.clear();
        vecTaxiEdgesIdxHead.reserve(vecTaxiEdges.size());
        for (size_t eIdx = 0; eIdx < vecTaxiEdges.size(); ++eIdx)
            if (vecTaxiEdges[eIdx].isValid())
                vecTaxiEdgesIdxHead.push_back(eIdx);
        
        // Now sort the index array by the angle of the linked edge
        std::sort(vecTaxiEdgesIdxHead.begin(),
                  vecTaxiEdgesIdxHead.end(),
                  [&](size_t a, size_t b)
                  { return vecTaxiEdges[a].angle < vecTaxiEdges[b].angle; });
    }
    
    /// @brief Returns a list of taxiways matching a given heading range
    /// @param _headSearch The heading we search for and which the edge has to match
    /// @param _angleTolerance Maximum difference between `_headSearch` and TaxiEdge::angle to be considered a match
    /// @param[out] lst Matching egdes are added to this list,
    /// @param _restrictType (Optional) Restrict returned edges to this type, or `UNKNOWN_WAY` to not restrict results
    /// @return Anything found? Basically: `!vec.empty()`
    bool FindEdgesForHeading (double _headSearch,
                              double _angleTolerance,
                              vecIdxTy& lst,
                              TaxiEdge::edgeTy _restrictType = TaxiEdge::UNKNOWN_WAY) const
    {
        // vecTaxiEdges is sorted by heading (see AddApt)
        // and TaxiEdge::heading is normalized to [0..180).
        // So we can more quickly find potential matches by
        // looking in that range of edges only around our target heading pos.heading()
        // "Normalize" search heading even further to [0..180)
        if (_headSearch >= 180.0)
            _headSearch -= 180.0;

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
            for (vecIdxTy::const_iterator iter =
                 std::lower_bound(vecTaxiEdgesIdxHead.cbegin(),
                                  vecTaxiEdgesIdxHead.cend(),
                                  rngPair.first,
                                  [&](const size_t& idx, double _angle)
                                  { return vecTaxiEdges[idx].angle < _angle; });
                 iter != vecTaxiEdgesIdxHead.cend() && vecTaxiEdges[*iter].angle <= rngPair.second;
                 ++iter)
            {
                // Check for type limitation, then add to `vec`
                if (_restrictType == TaxiEdge::UNKNOWN_WAY ||
                    _restrictType == vecTaxiEdges[*iter].GetType())
                    lst.push_back(*iter);
            }
        }
        
        // Found anything?
        return !lst.empty();
    }
    
    
    /// @brief Find closest taxi edge matching the passed position including its heading
    /// @details Calculations are done based on approximate  distances between
    ///          geographic world coordinates, measured in meter.
    ///          The passed-in position is considered the (0|0) point,
    ///          while the nodes to be analyzed are converted to distances to this point
    ///          before passed on to the DistPointToLineSqr() function.
    ///          The resulting base point is then converted back to geo world coords.
    /// @param _pos Search position, only nearby nodes with a similar heading are considered
    /// @param[out] _basePt Receives the coordinates of the base point and the edge's index in case of a match. `positionTy::lat`, `lon`, `heading`, and `edgeIdx` will be modified.
    /// @param _maxDist_m Maximum distance in meters between `pos` and edge to be considered a match
    /// @param _angleTolerance Maximum difference between `pos.heading()` and TaxiEdge::angle to be considered a match
    /// @param _angleToleranceExt Second priority tolerance, considered only if such a node is more than 5m closer than one that better fits angle
    /// @param _vecSkipEIdx (optional) Do not return any of these edge
    /// @return Pointer to closest taxiway edge or `nullptr` if no match was found
    const TaxiEdge* FindClosestEdge (const positionTy& _pos,
                                     positionTy& _basePt,
                                     double _maxDist_m,
                                     double _angleTolerance,
                                     double _angleToleranceExt,
                                     const vecIdxTy& _vecSkipEIdx = vecIdxTy()) const
    {
        const TaxiEdge* bestEdge = nullptr;
        size_t bestEdgeIdx = EDGE_UNKNOWN;
        double best_from_x = NAN;
        double best_from_y = NAN;
        double best_to_x   = NAN;
        double best_to_y   = NAN;
        double bestPrioDist= NAN;
        distToLineTy bestDist;
        // maxDist^2, used in comparisons
        const double maxDist2 = sqr(_maxDist_m);
        // This is what we add to the square distance for second prio match...
        // ...it is not exactly (dist+5m)^2 = dist^2 + 2 * 5 * dist + 5 ^ 2
        // ...but as close as we can get when we want to avoid sqrt for performance reasons
        constexpr double SCND_PRIO_ADD = 3 * ART_EDGE_ANGLE_EXT_DIST + ART_EDGE_ANGLE_EXT_DIST*ART_EDGE_ANGLE_EXT_DIST;
        // Init as: Nothing found
        _basePt.edgeIdx = EDGE_UNAVAIL;
        
        // Get a list of edges matching pos.heading()
        vecIdxTy lstEdges;
        const double headSearch = HeadingNormalize(_pos.heading());
        if (_angleToleranceExt < 90.0) {            // ...if there actually is a limiting heading tolerance
            if (!FindEdgesForHeading(headSearch,
                                     std::max(_angleTolerance, _angleToleranceExt),
                                     lstEdges))
                return nullptr;
        }
        
        // Analyze the edges to find the closest edge
        // Either use the limited list of edges matching a heading or just all edges
        const vecIdxTy& edgesToSearch = lstEdges.empty() ? vecTaxiEdgesIdxHead : lstEdges;
        for (size_t eIdx: edgesToSearch)
        {
            // Skip edge if wanted so
            if (std::any_of(_vecSkipEIdx.cbegin(), _vecSkipEIdx.cend(),
                            [eIdx](size_t _e){return eIdx == _e;}))
                continue;
            
            // Skip edge if invalid
            const TaxiEdge& e = vecTaxiEdges[eIdx];
            if (!e.isValid())
                continue;
            
            // Skip edge if pos must be on a rwy but edge is not a rwy
            if (isRwyPhase(_pos.f.flightPhase) &&
                e.GetType() != TaxiEdge::RUN_WAY)
                continue;

            // Fetch from/to nodes from the edge
            const TaxiNode& from  = e.startByHeading(*this, headSearch);
            const TaxiNode& to    = e.endByHeading(*this, headSearch);
            const double edgeAngle = e.GetAngleByHead(headSearch);

            // Compute temporary "coordinates", relative to the search position
            const double from_x = Lon2Dist(from.lon - _pos.lon(), _pos.lat());      // x is eastward
            const double from_y = Lat2Dist(from.lat - _pos.lat());                 // y is northward
            const double to_x   = Lon2Dist(to.lon   - _pos.lon(), _pos.lat());
            const double to_y   = Lat2Dist(to.lat   - _pos.lat());
            
            // As a quick check: (0|0) must be in the bounding box of [from-to] extended by _maxDist_m to all sides
            if (std::min(from_x, to_x) - _maxDist_m > 0.0 ||    // left
                std::max(from_y, to_y) + _maxDist_m < 0.0 ||    // top
                std::max(from_x, to_x) + _maxDist_m < 0.0 ||    // right
                std::min(from_y, to_y) - _maxDist_m > 0.0)      // bottom
                continue;

            // Distance to this edge
            distToLineTy dist;
            DistPointToLineSqr(0.0, 0.0,            // plane's position is now by definition in (0|0)
                               from_x, from_y,      // edge's starting point
                               to_x, to_y,          // edge's end point
                               dist);
            
            // If too far away, skip (this considers if base point is outside actual line)
            double prioDist = dist.DistSqrPlusOuts();
            if (prioDist > maxDist2)
                continue;
            
            // Distinguish between first prio angle match and second prio angle match
            if (std::abs(HeadingDiff(edgeAngle, headSearch)) > _angleTolerance) {
                // So this is a second prio match in terms of angle to the edge
                // For runways, we require first prio!
                if (e.GetType() == TaxiEdge::RUN_WAY)
                    continue;
                
                // For others, we consider this, but with higher calculated distance
                prioDist += SCND_PRIO_ADD;
            }
            
            // If priorized distance is farther than best we know: skip
            if (prioDist >= bestPrioDist)
                continue;
            
            // If base of shortest path to point is too far outside actual line
            if (dist.DistSqrOfBaseBeyondLine() > maxDist2)
                continue;
            
            // We have a new best match!
            bestEdge = &e;
            bestEdgeIdx  = eIdx;
            best_from_x  = from_x;
            best_from_y  = from_y;
            best_to_x    = to_x;
            best_to_y    = to_y;
            bestPrioDist = prioDist;
            bestDist     = dist;
        }
        
        // Nothing found?
        if (!bestEdge)
            return nullptr;
        
        // Compute base point on the line,
        // ie. the point on the line with shortest distance
        // to pos
        double base_x = NAN, base_y = NAN;
        DistResultToBaseLoc(best_from_x, best_from_y,   // edge's starting point
                            best_to_x, best_to_y,       // edge's end point
                            bestDist,
                            base_x, base_y);            // base point's local coordinates

        // Now only convert back from our local pos-based coordinate system
        // to geographic world coordinates
        _basePt.lon() = _pos.lon() + (std::isnan(base_x) ? 0.0 : Dist2Lon(base_x, _pos.lat()));
        _basePt.lat() = _pos.lat() + (std::isnan(base_y) ? 0.0 : Dist2Lat(base_y));
        _basePt.heading() = bestEdge->GetAngleByHead(_pos.heading());
        _basePt.f.bHeadFixed = true;                    // We want the plane to head exactly as the line does!
        _basePt.f.specialPos = bestEdge->GetType() == TaxiEdge::RUN_WAY ? SPOS_RWY : SPOS_TAXI;
        _basePt.edgeIdx = bestEdgeIdx;
        
        // return the found egde
        return bestEdge;
    }
    
    /// @brief Return the type of edge the given position is on
    /// @param _pos Position to analyse
    /// @param[out] _pIdxE Optionally receives the edge's index
    TaxiEdge::edgeTy GetPosEdgeType (const positionTy& _pos, size_t* _pIdxE = nullptr) const
    {
        // Haven't yet analysed this position?
        size_t idxE = _pos.edgeIdx;
        if (idxE == EDGE_UNKNOWN) {
            // Find the closest edge and update idxE
            positionTy basePos = _pos;
            FindClosestEdge(_pos, basePos,
                            dataRefs.GetFdSnapTaxiDist_m(),
                            ART_EDGE_ANGLE_TOLERANCE,
                            ART_EDGE_ANGLE_TOLERANCE_EXT);
            idxE = basePos.edgeIdx;
        }
        
        // return the edge's index if requested
        if (_pIdxE) *_pIdxE = idxE;
        
        // position is analyzed, but no edge found?
        if (idxE == EDGE_UNAVAIL)
            return TaxiEdge::UNKNOWN_WAY;
        
        // return the edge's type
        return vecTaxiEdges.at(idxE).GetType();
    }
    
    /// @brief Return the type of edge the given position is on
    /// @param[in,out] _pos Position to analyse; will set positionTy.edgeIdx if yet unknown
    TaxiEdge::edgeTy GetPosEdgeType (positionTy& _pos) const
    { return GetPosEdgeType(_pos, &_pos.edgeIdx); }

    /// @brief Processes the temporary map/list of nodes/edges and transforms them to permanent ones,
    /// @details thereby keeping joints (positions where edges meet) but streamlining other nodes,
    ///          so we don't add all nodes
    void PostProcessPaths ()
    {
        // -- 1. Identify joints and add them upfront to our network
        for (const auto& p: mapPos)
            if (p.second.isJoint())
                AddTaxiNode(p.second.lat(), p.second.lon(), ULONG_MAX, false);
        // all joints added, so up to here it is all full of joints:
        const size_t firstNonJoint = vecTaxiNodes.size();

        // -- 2. Process the edges, thereby adding more nodes
        for (const TaxiTmpPath& p: listPaths)
        {
            // The indexes to be used when adding the edge.
            // idxA points to the (already added) first node,
            // idxB to the just now added second node
            size_t idxA=ULONG_MAX, idxB=ULONG_MAX;
            
            // The first node of the entire list is definitely used, add it already
            const size_t idxA_First =
            idxA = AddTaxiNode(p.listPos.front().lat(),
                               p.listPos.front().lon());
            // Remember this node as one of the path's endpoint
            vecPathEnds.push_back(idxA);
            
            // The very last node will also be added later.
            // Between these two:
            // Combine adges til
            // a) reaching a joint, or
            // b) heading changes too much.
            // Add the remainder to the airport's taxi network
            double firstAngle = NAN;
            int numSkipped = 0;                         // number of skipped nodes in a row
            for (auto iEnd = p.listPos.cbegin();
                 iEnd != std::prev(p.listPos.cend());
                 ++iEnd)
            {
                const TaxiTmpPos& b = *iEnd;            // last node that is confirmed to be part of the edge
                const TaxiTmpPos& c = *std::next(iEnd); // next node, to be validated if still in the edge
                double bcAngle = CoordAngle(b.lat(), b.lon(), c.lat(), c.lon());
                if (std::isnan(firstAngle)) {           // new edge has just started, this is our reference angle
                    firstAngle = bcAngle;
                    numSkipped = 0;
                }
                else
                {
                    // is b a joint?
                    idxB = GetSimilarTaxiNode(b.lat(), b.lon());
                    if (idxB < firstNonJoint ||
                        // so many nodes...there's a reason for them, isn't it?
                        numSkipped >= 4 ||
                        // or has heading changed too much?
                        std::abs(HeadingDiff(firstAngle, bcAngle)) > APT_MAX_TAXI_SEGM_TURN)
                    {
                        // Add the edge to this node
                        if (idxB == ULONG_MAX)
                            idxB = AddTaxiNode(b.lat(), b.lon(), ULONG_MAX, false);
                        if (idxA != idxB) {
                            AddTaxiEdge(idxA, idxB);
                            // start a new edge, first segment will be b-c
                            idxA = idxB;
                            firstAngle = bcAngle;
                            numSkipped = 0;
                        }
                    }
                    else
                        ++numSkipped;
                }
            }
            
            // The last node of the list is also always to be added
            idxB = AddTaxiNode(p.listPos.back().lat(),
                               p.listPos.back().lon(),
                               idxA_First);     // never combine with very first node; this ensures that at least one edge will be added!
            if (idxA != idxB) {
                AddTaxiEdge(idxA, idxB);
                // Remember this node as the path's other endpoint
                vecPathEnds.push_back(idxB);
            }
        }
    }

    
    /// @brief For each path end, try connecting them to some edge (which might by a rwy)
    /// @details This shall\n
    ///          a) connect runways to taxiways\n
    ///          b) taxiway joints (which don't happen to have a directly overlapping node)
    void JoinPathEnds ()
    {
        // We had added entpoints to vecPathsEnds in a random order.
        // Let's reduce this list to a unique list of indexes
        std::sort(vecPathEnds.begin(), vecPathEnds.end());
        auto lastPE = std::unique(vecPathEnds.begin(), vecPathEnds.end());
        vecPathEnds.erase(lastPE,vecPathEnds.end());
        
        // Loop all path ends and see if they are in need of another connection
        for (size_t idxN: vecPathEnds)
        {
            // The node we deal with
            TaxiNode& n = vecTaxiNodes[idxN];
            
            // The exclusion edge list: With these edges we don't want to join:
            // 1. All our direct edges
            vecIdxTy vecEdgeExclusions = n.vecEdges;
            // Let's reduce this exclusion list to a unique list of indexes
            std::sort(vecEdgeExclusions.begin(), vecEdgeExclusions.end());
            auto lastEExcl = std::unique(vecEdgeExclusions.begin(), vecEdgeExclusions.end());
            vecEdgeExclusions.erase(lastEExcl,vecEdgeExclusions.end());

            // Try finding _another_ edge this one can connect to
            positionTy pos(n.lat, n.lon, 0.0, NAN, vecTaxiEdges[n.vecEdges.front()].GetAngleFrom(idxN));
            const TaxiEdge* pJoinE = FindClosestEdge(pos, pos,
                                                     // larger distance allowed if I'm a single node, smaller only if I already have connections
                                                     n.vecEdges.size() <= 1 ? APT_JOIN_MAX_DIST_M : APT_MAX_SIMILAR_NODE_DIST_M,
                                                     APT_JOIN_ANGLE_TOLERANCE,
                                                     90.0,      // don't limit by heading...search all edges!
                                                     vecEdgeExclusions);
            if (!pJoinE)
                continue;
            
            if (std::isnan(pos.lat()) || std::isnan(pos.lon()))
                continue;
            
            // We found just another taxi edge, which we combine:
            // We'll now split that found edge by inserting the
            // open node, which we move to the base position,
            // so that it is exactly on the edge that we split.
            // The "join" edge doesn't move.
            const size_t joinIdxE = pos.edgeIdx;

            // Move the open node to the base location, ie. to the closest
            // point on the pJoinE edge (which is at max APT_JOIN_MAX_DIST_M meters away)
            n.lat = pos.lat();
            n.lon = pos.lon();
            
            // Along this edge, there could be nodes which are more or less
            // equal to our node n. Eg., this happens with taxiways,
            // which leave runwas in opposite directions:
            // Both taxiways (left/right) have an open end on the rwy,
            // one to the left, one to the right of the rwy centerline.
            // The algorithm will find one of them first and merge with
            // the rwy. Once we find the other side we should combine that
            // node now with the already merged node, so that both taxiways
            // join with the rwy in one single joint node.
            size_t nearIdxN = ULONG_MAX;
            if (n.IsCloseTo(vecTaxiNodes[pJoinE->startNode()], APT_MAX_SIMILAR_NODE_DIST_M))
                nearIdxN = pJoinE->startNode();
            else if (n.IsCloseTo(vecTaxiNodes[pJoinE->endNode()], APT_MAX_SIMILAR_NODE_DIST_M))
                nearIdxN = pJoinE->endNode();
            
            // One of the nodes is indeed nearby?
            if (nearIdxN < ULONG_MAX) {
                ReplaceNode(idxN, nearIdxN);
            }
            // Not nearby:
            else {
                // Moving n has slightly changed all edges of n, recalc distance and angle
                for (size_t idxEE: n.vecEdges)
                    RecalcTaxiEdge(idxEE);
                // Split pJoinE at the base position, now n (whose index is idxN)
                SplitEdge(joinIdxE, idxN);
            }

            // To ensure FindClosestEdge works we need to sort
            SortTaxiEdges();
    #ifdef DEBUG
            LOG_ASSERT(ValidateNodesEdges());
    #endif
        }           // for all path ends (which are nodes)
    }
    
    /// @brief Find shortest path in taxi network with a maximum length between 2 nodes
    /// @see https://en.wikipedia.org/wiki/Dijkstra's_algorithm
    /// @param _startN Start node in Apt::vecTaxiNodes
    /// @param _endN End node in Apt::vecTaxiNodes
    /// @param _maxLen Maximum path length, no longer paths will be pursued or returned
    /// @param _headingAtStart The current heading at the start node, affects how the start leg may be picked to avoid sharp turns
    /// @param _headingAtEnd The expected heading at the end node, affects how the final leg to the endN may be picked
    /// @return List of node indexes _including_ `_end` and `_start` in _reverse_ order,
    ///         or an empty list if no path of suitable length was found
    vecIdxTy ShortestPath (size_t _startN, size_t _endN, double _maxLen,
                           double _headingAtStart,
                           double _headingAtEnd)
    {
        // Sanity check: _start and _end should differ
        if (_startN == _endN)
            return vecIdxTy();


        // Initialize the Dijkstra values in the nodes array
        for (TaxiNode& n: vecTaxiNodes)
            n.InitDijkstraAttr();

        // This array stores nodes we need to visit
        // (have an initial distance, but aren't fully visited yet)
        vecIdxTy vecVisit;

        // The start place is the given taxiway node
        TaxiNode& startN = vecTaxiNodes.at(_startN);
        const TaxiNode& endN   = vecTaxiNodes.at(_endN);
        startN.pathLen = 0.0;
        startN.prevIdx = ULONG_MAX-1;   // we use "ULONG_MAX-1" for saying "is a start node"
        vecVisit.push_back(_startN);

        // General heading between start and end (reversed)
        // defines first heading (how we leave _startN) and
        // how far the plane is allowed to turn
        
        // outer loop controls currently visited node and checks if end already found
        while (!vecVisit.empty() && endN.prevIdx == ULONG_MAX)
        {
            // fetch node with shortest yet known distance
            // (this isn't awfully efficient, but keeping a separate map or prio-queue
            //  sorted while updating nodes in the next loop
            //  is not simple either. I expect vecVisit to stay short
            //  due to cut-off at _maxLen, so I've decided this way:)
            vecIdxTy::iterator shortestIter = vecVisit.begin();
            double shortestDist = vecTaxiNodes[*shortestIter].pathLen;
            for (vecIdxTy::iterator i = std::next(shortestIter);
                 i != vecVisit.end(); ++i)
            {
                if (vecTaxiNodes[*i].pathLen < shortestDist) {
                    shortestIter = i;
                    shortestDist = vecTaxiNodes[*i].pathLen;
                }
            }
            const size_t shortestNIdx  = *shortestIter;
            TaxiNode& shortestN = vecTaxiNodes[shortestNIdx];
            
            // To avoid too sharp corners we need to know the angle by which we reach this shortest node
            const size_t idxEdgeToShortestN =
            shortestN.prevIdx >= ULONG_MAX-1 ? EDGE_UNKNOWN :
            GetEdgeBetweenNodes(shortestN.prevIdx, shortestNIdx);
            // start heading for when leaving first node, otherwise heading between previous and current node
            const double angleToShortestN =
            idxEdgeToShortestN == EDGE_UNKNOWN ? _headingAtStart :
            vecTaxiEdges[idxEdgeToShortestN].GetAngleFrom(shortestN.prevIdx);
            
            // This one is now already counted as "visited" so no more updates to its pathLen!
            shortestN.bVisited = true;
            vecVisit.erase(shortestIter);

            // Update all connected nodes with best possible distance
            for (size_t eIdx: shortestN.vecEdges)
            {
                const TaxiEdge& e = vecTaxiEdges[eIdx];
                if (!e.isValid()) continue;
                
                size_t updNIdx    = e.otherNode(shortestNIdx);
                TaxiNode& updN    = vecTaxiNodes[updNIdx];
                
                // if aleady visited then no need to re-assess
                if (updN.bVisited)
                    continue;
                
                // Don't allow turns of more than 100°,
                // ie. edge not valid if it would turn more than that
                const double eAngle = e.GetAngleFrom(shortestNIdx);
                if (!std::isnan(angleToShortestN) &&
                    std::abs(HeadingDiff(angleToShortestN,
                                         eAngle)) > APT_MAX_PATH_TURN)
                    continue;
                
                // If the node being analyzed is the end node, then we also
                // need to verify if the heading from end node to actual a/c position
                // would not again cause too sharp a turn:
                if (updNIdx == _endN && !std::isnan(_headingAtEnd) &&
                    std::abs(HeadingDiff(_headingAtEnd, eAngle)) > APT_MAX_PATH_TURN)
                    continue;
                
                // Calculate the yet known best distance to this node
                const double lenToUpd = shortestDist + e.dist_m;
                if (lenToUpd > _maxLen ||               // too far out?
                    updN.pathLen <= lenToUpd)           // node has a faster path already
                    continue;

                // Update this node with new best values
                updN.pathLen = lenToUpd;        // best new known distance
                updN.prevIdx = shortestNIdx;     // predecessor to achieve that distance
                
                // Have we reached the wanted end node?
                if (updNIdx == _endN)
                    break;
                
                // this node is now ready to be visited
                push_back_unique(vecVisit, updNIdx);
            }
        }
        
        // Found nothing? -> return empty list
        if (endN.prevIdx == ULONG_MAX)
            return vecIdxTy();
        
        // put together the nodes from _start through _end in the right order
        vecVisit.clear();
        for (size_t nIdx = _endN;
             nIdx < ULONG_MAX-1;                    // until nIdx becomes invalid
             nIdx = vecTaxiNodes[nIdx].prevIdx)     // move on to _previous_ node on shortest path
        {
            LOG_ASSERT(nIdx < vecTaxiNodes.size());
            vecVisit.push_back(nIdx);
        }
        return vecVisit;
    }
    
    /// @brief Find best matching taxi edge based on passed-in position/heading info
    bool SnapToTaxiway (LTFlightData& fd, dequePositionTy::iterator& posIter,
                        bool bInsertTaxiTurns)
    {
        // The position we consider and that we potentially change
        // by snapping to a taxiway
        positionTy& pos = *posIter;
        const double old_lat = pos.lat(), old_lon = pos.lon();
        
        // Previous position, could be NULL!
        const positionTy* pPrevPos = nullptr;
        if (posIter != fd.posDeque.begin())
            pPrevPos = &(*std::prev(posIter));
        else if (fd.hasAc())
            pPrevPos = &(fd.pAc->GetToPos());

        // 1. --- Try to match pos with a startup location
        double distStartup = NAN;
        const StartupLoc* pStartLoc = FindStartupLoc(pos,
                                                     dataRefs.GetFdSnapTaxiDist_m() * 3,
                                                     &distStartup);
        if (pStartLoc)
        {
            // pos is close to a startup location, so we definitely set
            // and keep the startup location's heading
            pos.heading() = pStartLoc->heading;
            pos.f.bHeadFixed = true;
        }
        
        // 2. --- Find any edge ---
        // Find the closest edge and right away move pos there
        // Heading (at the edge):
        // On the ground it is quite possible to do tight turns.
        // Using the direct vector between last and next location
        // might not point the right way, especially if both positions
        // are a great time apart. But it is hard to guess if the
        // vector heading or the (also often not accurate) heading
        // delivered with the tracking data is the better...there's a reason
        // LiveTraffic recalculates headings between positions.
        // So the approach here is as follows:
        // If the previous position is a rwy then we trust
        // tracking data heading (because after exiting a rwy it is not
        // uncommon to turn around 180°), otherwise we trust our average heading.
        positionTy posForSearching = pos;
        if (!pStartLoc && pPrevPos && pPrevPos->f.specialPos == SPOS_RWY)
        {
            LTFlightData::FDDynamicData *pDynDat = nullptr, *pDynAfter = nullptr;
            bool bSimilar = false;
            fd.dequeFDDynFindAdjacentTS(posForSearching.ts(), pDynDat, pDynAfter, &bSimilar);
            if (bSimilar && pDynDat)
                posForSearching.heading() = pDynDat->heading;
        }
        const TaxiEdge* pEdge = FindClosestEdge(posForSearching, pos,
                                                dataRefs.GetFdSnapTaxiDist_m(),
                                                ART_EDGE_ANGLE_TOLERANCE,
                                                ART_EDGE_ANGLE_TOLERANCE_EXT);
        
        // specialPos might have been set to SPOS_TAXI,
        // but for startup positions we do want it to be:
        if (pStartLoc)
            pos.f.specialPos = SPOS_STARTUP;
        
        // Nothing found?
        if (!pEdge) {
            
            // No edge found, but a startup location?
            if (pStartLoc)
            {
                // Then we should move onto the path leading away from the location
                ProjectPosOnStartupPath(pos, *pStartLoc);
                if (dataRefs.GetDebugAcPos(fd.key()))
                    LOG_MSG(logDEBUG, "Snapped to startup location path from (%.5f, %.5f) to (%.5f, %.5f)",
                            old_lat, old_lon, pos.lat(), pos.lon());
                return true;
            }
            
            // --- Test for Black Hole Horizon problem:
            //     When planes briefly wait on taxiways then it can happen
            //     that the previous pos was close enough to a taxiway and
            //     it snapped while this new pos was just a bit too far
            //     away and did not snap, but otherwise didn't move ahead much
            //     either. The snap distance serves as the Black Hole Horizon:
            //     While some positions are snapped and move up to that distance,
            //     others just outside don't move. They appear to be
            //     about 90° away from that previous pos: The planes turns and
            //     rolls there, ending up with the nose pointing away from
            //     the taxiway.
            //     We try to catch that here: If we are just outside snapping distance
            //     and turned about 90° away from previous pos's edge,
            //     then we snap onto previous pos nonetheless.
            if (!pPrevPos || !pPrevPos->HasTaxiEdge())
                return false;
            
            // There is. Relative to current position...where?
            vectorTy vec = pPrevPos->between(pos);
            // Too far out?
            if (vec.dist > dataRefs.GetFdSnapTaxiDist_m() * 2.0)
                return false;
            
            // The edge that previous pos is on. Angle pretty much like 90°?
            const TaxiEdge& ePrev = vecTaxiEdges.at(pPrevPos->edgeIdx);
            const double headDiff = std::abs(HeadingDiff(vec.angle, ePrev.angle));
            if (std::abs(headDiff- 90.0) < APT_RECT_ANGLE_TOLERANCE ||
                std::abs(headDiff-270.0) < APT_RECT_ANGLE_TOLERANCE)
            {
                // Angle of vector between prevPos and pos is about rectangular
                // compared to prevPos's edge --> snap pos on PrevPos
                pos.lat()       = pPrevPos->lat();
                pos.lon()       = pPrevPos->lon();
                pos.alt_m()     = pPrevPos->alt_m();
                pos.heading()   = ePrev.GetAngleByHead(pPrevPos->heading());
                pos.edgeIdx     = pPrevPos->edgeIdx;
                pos.f           = pPrevPos->f;
                if (dataRefs.GetDebugAcPos(fd.key()))
                    LOG_MSG(logDEBUG, "Snapped to taxiway from (%.5f, %.5f) to (%.5f, %.5f; edge %lu) based on previously snapped position",
                            old_lat, old_lon, pos.lat(), pos.lon(), (long unsigned)pos.edgeIdx);
                return true;
            }
            
            return false;
        }
        
        // --- found a match, say hurray ---
        if (dataRefs.GetDebugAcPos(fd.key())) {
            LOG_MSG(logDEBUG, "Snapped to taxiway from (%.5f, %.5f) to (%.5f, %.5f; edge %lu)",
                    old_lat, old_lon, pos.lat(), pos.lon(), (long unsigned)pos.edgeIdx);
        }
            
        // this is now an artificially moved position, don't touch any further
        // (we don't mark positions on a runway yet...would be take off or rollout to be distinguished)
        if (pEdge->GetType() != TaxiEdge::RUN_WAY)
            pos.f.flightPhase = FPH_TAXI;
        
        // --- Insert shortest path along taxiways ---
        // if wanted, that is, and if there is a previous position
        if (!bInsertTaxiTurns || !pPrevPos)
            return true;
        
        // Direct distance from pos to *pPrevPos, used in some sanity checks
        const double distPrevPosPos = pPrevPos->dist(pos);
        
        // That pos must be on an edge, too
        if (!pPrevPos->HasTaxiEdge() ||
            // That previous edge isn't by chance the same we just now found? Then the shortest path is to go straight...
            (pos.edgeIdx == pPrevPos->edgeIdx) ||
            // Also, we don't search for path between any two rwy nodes
            (GetPosEdgeType(pos) == TaxiEdge::RUN_WAY && GetPosEdgeType(*pPrevPos) == TaxiEdge::RUN_WAY))
            return true;

        // - relevant nodes: usually the ones away from (prev)pos,
        //                   but if we are very close to a joint node,
        //                   then we pick that joint node. This increased the
        //                   number of possible paths. In case of two edges
        //                   closeby it is well possible that `FindClosestEdge`
        //                   picked the "wrong" one. Then this notion to use
        //                   an adjacent joint will often rectify this error.
        
        // previous edge's relevant node
        bool bSkipStart = false;
        const TaxiEdge& prevE = vecTaxiEdges[pPrevPos->edgeIdx];
        size_t prevErelN = prevE.endByHeading(pPrevPos->heading());
        {
            const TaxiNode& othN = vecTaxiNodes[prevE.otherNode(prevErelN)];
            if (DistLatLonSqr(othN.lat, othN.lon, pPrevPos->lat(), pPrevPos->lon()) <= sqr(2*APT_MAX_SIMILAR_NODE_DIST_M)) {
                prevErelN = prevE.otherNode(prevErelN);
                bSkipStart = true;      // this node is now _before_ prevPos, don't add that to the deque!
            }
            else
            {
                // Sanity check: if the distance to reaching the first node
                // is more than we shall travel in total we're making a mistake
                const TaxiNode& prevErelNode = vecTaxiNodes[prevErelN];
                if (DistLatLon(pPrevPos->lat(), pPrevPos->lon(),
                               prevErelNode.lat, prevErelNode.lon) > distPrevPosPos)
                    // Then it is simpler to just go straight without any taxiway path
                    return true;
            }
        }
        
        // current edge's relevant node
        bool bSkipEnd = false;
        size_t currEstartN = pEdge->startByHeading(pos.heading());
        {
            const TaxiNode& othN = vecTaxiNodes[pEdge->otherNode(currEstartN)];
            if (DistLatLonSqr(othN.lat, othN.lon, pos.lat(), pos.lon()) <= sqr(2*APT_MAX_SIMILAR_NODE_DIST_M)) {
                currEstartN = pEdge->otherNode(currEstartN);
                bSkipEnd = true;      // this node is now _beyond_ pos, don't add that to the deque!
            }
            else
            {
                // Sanity check: if the distance to reaching the last node
                // is more than we shall travel in total we're making a mistake
                const TaxiNode& currErelNode = vecTaxiNodes[currEstartN];
                if (DistLatLon(pos.lat(), pos.lon(),
                               currErelNode.lat, currErelNode.lon) > distPrevPosPos)
                    // Then it is simpler to just go straight without any taxiway path
                    return true;
            }
        }
        
        // for the maximum allowed path length let's consider taxiing speed,
        // but allow 3x taxiing speed if beginning leg is still on a rwy
        // (consider high-speed exits!).
        const LTAircraft::FlightModel& mdl = LTAircraft::FlightModel::FindFlightModel(fd);
        const double maxLen =
        (pos.ts() - pPrevPos->ts()) * mdl.MAX_TAXI_SPEED / KT_per_M_per_S *
        (prevE.GetType() == TaxiEdge::RUN_WAY ? 3.0 : 1.0);     // allow much more length in case we are turning off a rwy, might still have high speed
        
        // let's try finding a shortest path
        vecIdxTy vecPath = ShortestPath(prevErelN,
                                        currEstartN,
                                        maxLen,
                                        prevE.GetAngleByHead(pPrevPos->heading()),
                                        pEdge->GetAngleByHead(pos.heading()));
        
        // We might skip front/start nodes, remove them now if so
        if (vecPath.size() >= 2 && bSkipEnd)
            vecPath.erase(vecPath.begin());             // vecPath is in reverse order, so last element is at begin
        if (vecPath.size() >= 2 && bSkipStart)
            vecPath.pop_back();                         // vecPath is in reverse order!
        
        // Special handling for rwy nodes at beginning of path:
        // We don't need several rwy nodes, a rwy is a straight line anyway,
        // and without intermediate nodes calculation of proper decelaration
        // becomes possible
        while (vecPath.size() >= 2)
        {
            // vecPath is in reverse order, so use reverse iterator
            // edge between the first two nodes
            size_t eIdx = GetEdgeBetweenNodes(*vecPath.crbegin(), *std::next(vecPath.crbegin()));
            LOG_ASSERT(eIdx != EDGE_UNAVAIL);
            // stop processing if not a rwy
            if (vecTaxiEdges[eIdx].GetType() != TaxiEdge::RUN_WAY)
                break;
            // it is a runway, so remove the first node (which, as vecPath is in reverse order, happens to be the back node)
            vecPath.pop_back();
        }
        
        // if we removed nodes from the start of the path then we need to adjust path len in the nodes now:
        // The start node has to have pathLen == 0.0
        if (vecPath.size() >= 2 && vecTaxiNodes[vecPath.back()].pathLen > 0.0) {
            const double adjust = vecTaxiNodes[vecPath.back()].pathLen;
            for (size_t nIdx: vecPath)
                vecTaxiNodes[nIdx].pathLen -= adjust;
        }

        // Some path left?
        if (vecPath.size() >= 2)
        {
            const TaxiNode& endN = vecTaxiNodes[vecPath.front()];   // end of path
            const TaxiNode& startN = vecTaxiNodes[vecPath.back()];  // start of path

            // distance from prevPos to path's start
            const double distToStart = DistLatLon(pPrevPos->lat(), pPrevPos->lon(), startN.lat, startN.lon);
            // length of total path as defined in vecPath
            const double pathLen = endN.pathLen;
            // distane from path's end to pos
            const double distFromEnd = DistLatLon(endN.lat, endN.lon, pos.lat(), pos.lon());
            // end-2-end distance including all segments
            const double distE2E = distToStart + pathLen + distFromEnd;
            
            // average speed for the complete end-2-end distance
            double speed = distE2E / (pos.ts() - pPrevPos->ts());
            // ts for first path node: Allow for some time to go from prevPos to start of path:
            double startTS = pPrevPos->ts() + distToStart / speed;

            // Special handling if we are coming from a rwy:
            // We allow for high speed on the path from prevPos to the start of
            // the path, which supposingly is the point turning off from the rwy
            // (we had removed all other rwy nodes just a few lines above)
            if (prevE.GetType() == TaxiEdge::RUN_WAY &&
                speed > mdl.MAX_TAXI_SPEED * 0.60 / KT_per_M_per_S)
            {
                // Average speed was higher than what we would taxi with,
                // so we reduce the speed to reasonable taxiing speed
                // and make sure that all the path is executed with taxiing speed,
                // which allows for higher speed from prevPos to the start of the path:
                const double taxiSpeed = mdl.MAX_TAXI_SPEED * 0.60 / KT_per_M_per_S;
                const double newStartTaxiTS = pos.ts() - (pathLen + distFromEnd) / taxiSpeed;
                const double rwySpeed = distToStart / (newStartTaxiTS - pPrevPos->ts());
                // Validate the above new values otherwise we might start the
                // new path before pPrevPos, which would be bad...
                if (newStartTaxiTS > pPrevPos->ts() &&      // taxiing must start after pPrevPos (on rwy)
                    rwySpeed <= mdl.SPEED_INIT_CLIMB * 1.5) // speed on rwy must still be reasonable
                {
                    speed = taxiSpeed;
                    startTS = newStartTaxiTS;
                }
            }

            // remaining time from first path's node to pos
            const double timeStartToPos = pos.ts() - startTS;
            // remaining distance from first path's node to pos
            const double distStartToPos = pathLen + distFromEnd;

            // path is returned in reverse order, so work on it reversely
            size_t prevIdxN = ULONG_MAX;
            bool bFirstNode = true;
            double segmLen = 0.0;           // length of the inserted segment if combined from several shortest path segments
            for (vecIdxTy::const_reverse_iterator iter = vecPath.crbegin();
                 iter != vecPath.crend();
                 ++iter)
            {
                // Is this (going to be) the last node?
                const bool bLastNode = std::next(iter) == vecPath.crend();

                // create a proper position and insert it into fd's posDeque
                const TaxiNode& n = vecTaxiNodes[*iter];
                positionTy insPos (n.lat, n.lon, NAN,   // lat, lon, altitude
                                   startTS + timeStartToPos * n.pathLen / distStartToPos,
                                   NAN,                 // heading will be populated later
                                   0.0, 0.0,            // on the ground no pitch/roll
                                   GND_ON,
                                   UNIT_WORLD,
                                   UNIT_DEG,
                                   FPH_TAXI);
                
                // Which edge is this pos on? (Or, as it is a node: one of the edges it is connected to)
                if (prevIdxN == ULONG_MAX)
                    insPos.edgeIdx = pPrevPos->edgeIdx;
                else {
                    insPos.edgeIdx = GetEdgeBetweenNodes(*iter, prevIdxN);
                    segmLen += vecTaxiEdges[insPos.edgeIdx].dist_m;
                }
                prevIdxN = *iter;
                
                // insPos is now either on a taxiway or a runway
                insPos.f.specialPos =
                vecTaxiEdges[insPos.edgeIdx].GetType() == TaxiEdge::RUN_WAY ?
                SPOS_RWY : SPOS_TAXI;
                
                // A few short segments might be combined into one
                if (bFirstNode || bLastNode                     ||  // we always add first and last nodes
                    insPos.f.specialPos == SPOS_RWY             ||  // we always add RWY nodes
                    segmLen >= APT_PATH_MIN_SEGM_LEN)               // we add once the segment length is long enough
                {
                    // Insert before the position that was passed in
                    posIter = fd.posDeque.insert(posIter, insPos);  // posIter now points to inserted element
                    ++posIter;                                      // posIter points to originally passed in element again
                    segmLen = 0.0;
                    bFirstNode = false;
                }
            }
            
            if (dataRefs.GetDebugAcPos(fd.key())) {
                LOG_MSG(logDEBUG, "Inserted %lu taxiway nodes",
                        (long unsigned)(vecPath.size() - (size_t)bSkipStart - (size_t)bSkipEnd));
            }

            // posDeque should still be sorted, i.e. no two adjacent positions a,b should be a > b
            LOG_ASSERT_FD(fd,
                          std::adjacent_find(fd.posDeque.cbegin(), fd.posDeque.cend(),
                                             [](const positionTy& a, const positionTy& b)
                                             {return a > b;}
                                             ) == fd.posDeque.cend());
        } // if found a shortest path
        // Not found a shortest path -> try finding edges' intersection
        else
        {
            // Let's try finding the intersection point of the 2 edges we are on
            const TaxiNode& currA = pEdge->GetA(*this);
            const TaxiNode& currB = pEdge->GetB(*this);
            const TaxiNode& prevA = prevE.GetA(*this);
            const TaxiNode& prevB = prevE.GetB(*this);
            positionTy intersec =
            CoordIntersect({prevA.lon, prevA.lat}, {prevB.lon, prevB.lat},
                           {currA.lon, currA.lat}, {currB.lon, currB.lat});
            intersec.pitch() = 0.0;
            intersec.roll()  = 0.0;
            intersec.f.onGrnd  = GND_ON;
            intersec.f.flightPhase = FPH_TAXI;
            intersec.f.bCutCorner = true;       // the corner of this position can be cut short
            
            // It is essential that the intersection is in front (rather than behind)
            vectorTy vecPrevInters = pPrevPos->between(intersec);
            if (std::abs(HeadingDiff(pPrevPos->heading(),vecPrevInters.angle)) < 90.0)
            {
                vectorTy vecIntersCurr = intersec.between(pos);
                
                // turning angle at intersection must not be too sharp
                if (std::abs(HeadingDiff(vecPrevInters.angle, vecIntersCurr.angle)) <= APT_MAX_PATH_TURN)
                {
                    double avgSpeed = (vecPrevInters.dist + vecIntersCurr.dist) / (pos.ts() - pPrevPos->ts());
                    
                    // Distance needs to be manageable, which means:
                    // On the ground max MAX_TAXI_SPEED,
                    // when turning off a rwy then the taxi part is restricted to MAX_TAXI_SPEED
                    if (prevE.GetType() == TaxiEdge::RUN_WAY &&
                        avgSpeed > mdl.MAX_TAXI_SPEED)
                    {
                        intersec.ts() = pos.ts() - vecIntersCurr.dist/mdl.MAX_TAXI_SPEED;
                        // intersection moves too close (in terms of time) to previous position?
                        if (intersec.ts() < pPrevPos->ts() + SIMILAR_TS_INTVL)
                            intersec.ts() = NAN;        // then we don't use it
                    }
                    else if (avgSpeed <= mdl.MAX_TAXI_SPEED)
                        // define ts so that we run constant speed from prevPos via intersec to pos
                        intersec.ts() = pPrevPos->ts() + (pos.ts()-pPrevPos->ts()) * vecPrevInters.dist / (vecPrevInters.dist+vecIntersCurr.dist);
                    
                    // Did we find a valid timestamp? -> Add the pos into posDeque
                    if (!std::isnan(intersec.ts())) {
                        posIter = fd.posDeque.insert(posIter, intersec);// posIter now points to inserted element
                        ++posIter;                                      // posIter points to originally passed in element again
                        if (dataRefs.GetDebugAcPos(fd.key()))
                            LOG_MSG(logDEBUG, "Inserted artificial intersection node");
                    }

                    // posDeque should still be sorted, i.e. no two adjacent positions a,b should be a > b
                    LOG_ASSERT_FD(fd,
                                  std::adjacent_find(fd.posDeque.cbegin(), fd.posDeque.cend(),
                                                     [](const positionTy& a, const positionTy& b)
                                                     {return a > b;}
                                                     ) == fd.posDeque.cend());
                }
            }
        }

        // snapping successful
        return true;
    }
    
#ifdef DEBUG
    /// Validates if back references of edges to nodes are still OK
    bool ValidateNodesEdges (bool _bValidateIdxHead = true) const
    {
        bool bRet = true;
        
        // Validate vecTaxiNodes and vecTaxiEdges
        for (size_t idxN = 0; idxN < vecTaxiNodes.size(); ++idxN)
        {
            const TaxiNode& n = vecTaxiNodes[idxN];
            for (size_t idxE: n.vecEdges)
            {
                const TaxiEdge& e = vecTaxiEdges[idxE];
                if (!e.isValid()) {
                    LOG_MSG(logFATAL, "Node %lu includes edge %lu, which is invalid (%lu/%lu)!",
                            idxN, idxE, e.startNode(), e.endNode());
                    bRet = false;
                }
                if (e.startNode() != idxN &&
                    e.endNode()   != idxN) {
                    LOG_MSG(logFATAL, "Node %lu includes edge %lu, which however goes %lu - %lu!",
                            idxN, idxE, e.startNode(), e.endNode());
                    bRet = false;
                }
            }
            if (!n.HasGeoCoords()) {
                LOG_MSG(logFATAL, "Node %lu has no geo coordinates!", idxN);
                bRet = false;
            }
        }
        
        // Validate vecTaxiEdges
        for (size_t idxE = 0; idxE < vecTaxiEdges.size(); ++idxE)
        {
            const TaxiEdge& e = vecTaxiEdges[idxE];
            if (e.isValid() && e.startNode() == e.endNode()) {
                LOG_MSG(logFATAL, "Valid edge %lu has a == b == %lu",
                        idxE, e.startNode());
                bRet = false;
            }
        }
        
        // Validate the index array sorted by heading
        if (_bValidateIdxHead)
        {
            if (vecTaxiEdgesIdxHead.size() > vecTaxiEdges.size()) {
                LOG_MSG(logFATAL, "vecTaxiEdgesIdxHead.size() = %lu > %lu = vecTaxiEdges.size()",
                        vecTaxiEdgesIdxHead.size(), vecTaxiEdges.size());
                bRet = false;
            }
            double prevAngle = -1.0;
            for (size_t idxE: vecTaxiEdgesIdxHead)
            {
                if (idxE >= vecTaxiEdges.size() ||
                    std::isnan(vecTaxiEdges[idxE].angle) ||
                    vecTaxiEdges[idxE].angle < prevAngle)
                {
                    LOG_MSG(logFATAL, "vecTaxiEdgesIdxHead wrongly sorted, edge %lu (heading %.1f) at wrong place after heading %.1f",
                            idxE, vecTaxiEdges[idxE].angle, prevAngle);
                    bRet = false;
                }
            }
        }
        
        return bRet;
    }
#endif
    
    // --- MARK: Runways
    
    /// The vector of runway endpoints
    const vecRwyEndPtTy& GetRwyEndPtVec () const { return vecRwyEndPts; }
    
    /// Any runway endpoints defined?
    bool HasRwyEndpoints () const { return !vecRwyEndPts.empty(); }
    
    /// Add egdes into the taxinetwork for the runway
    void AddRwyEdges ()
    {
        // rwy end nodes are collected in vecRwyNodes in pairs
        for (auto i = vecRwyNodes.cbegin();
             i != vecRwyNodes.cend();
             i = std::next(i,2))
        {
            const TaxiNode& a = *i;
            const TaxiNode& b = *std::next(i);
            const size_t idxA = AddTaxiNode(a.lat, a.lon);
            const size_t idxB = AddTaxiNode(b.lat, b.lon);
            AddTaxiEdge(idxA, idxB, TaxiEdge::RUN_WAY);
        }
    }
    
    /// Adds both rwy ends from apt.dat information fields
    void AddRwyEnds (double lat1, double lon1, double displaced1, const std::string& id1,
                     double lat2, double lon2, double displaced2, const std::string& id2)
    {
        // Add this original extend of the runway to the temporary storage,
        // so later on we add a proper edge for the taxi network
        vecRwyNodes.emplace_back(lat1, lon1);
        vecRwyNodes.emplace_back(lat2, lon2);

        // Original position of outer end of runway
        positionTy re1 (lat1,lon1,NAN,NAN,NAN,NAN,NAN,GND_ON);
        positionTy re2 (lat2,lon2,NAN,NAN,NAN,NAN,NAN,GND_ON);
        vectorTy vecRwy = re1.between(re2);
        
        // move by displayed threshold
        // and then by another 10% of remaining length to determine actual touch-down point
        vecRwy.dist -= displaced1;
        vecRwy.dist -= displaced2;
        re1 += vectorTy (vecRwy.angle,   displaced1 + vecRwy.dist * ART_RWY_TD_POINT_F );
        re2 += vectorTy (vecRwy.angle, -(displaced2 + vecRwy.dist * ART_RWY_TD_POINT_F));
        // Also adapt our knowledge of rwy length: 80% if previous value are left
        vecRwy.dist *= (1 - 2 * ART_RWY_TD_POINT_F);
        
        // 1st rwy end
        bounds.enlarge(re1);
        vecRwyEndPts.emplace_back(id1, re1.lat(), re1.lon(), vecRwy.angle);
        
        // 2nd rwy end, opposite direction
        bounds.enlarge(re2);
        vecRwyEndPts.emplace_back(id2, re2.lat(), re2.lon(), std::fmod(vecRwy.angle+180.0, 360.0));
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

    /// Returns a human-readable string with all runways, mostly for logging purposes
    std::string GetRwysString () const
    {
        std::string s;
        // loop all runways (the two ends of a rwy are always added together to the vector)
        for (vecRwyEndPtTy::const_iterator i = vecRwyEndPts.cbegin();
             i != vecRwyEndPts.cend();
             ++i)
        {
            if (!s.empty()) s += " / ";     // divider between runways
            s += i->id;                     // add ids of runways
            s += '-';
            if ((++i) != vecRwyEndPts.cend())
                s += i->id;
        }
        return s;
    }
    
    // --- MARK: Startup locations

    /// The vevtor of startup locations
    const vecStartupLocTy& GetStartupLocVec() const { return vecStartupLocs; }
    
    /// Add a startup location
    void AddStartupLoc (const std::string& _id,
                        double _lat, double _lon,
                        double _heading)
    {
        // Heading could be defined negative
        while (_heading < 0.0)
            _heading += 360.0;

        // resonabilit check...then add to our list of startup locations
        if ( -90.0 <= _lat && _lat <= 90.0 &&
            -180.0 <= _lon && _lon < 180.0)
        {
            // The startup location seems to be aligned with the plane's tip
            // while all CSL model's origin is at the center of the full.
            // To (partly) make up for this we move out the startup location
            // by about 10m. (`viaPos` is here just used as temp variable.)
            const positionTy origPos (_lat, _lon);
            vectorTy vec (std::fmod(_heading+180.0, 360.0),
                          APT_STARTUP_MOVE_BACK);
            const positionTy startPos = CoordPlusVector(origPos, vec);

            // now add this moved out position to our list
            vecStartupLocs.emplace_back(_id, startPos.lat(), startPos.lon(), _heading);
            StartupLoc& loc = vecStartupLocs.back();
            bounds.enlarge_pos(loc.lat, loc.lon);       // make sure it becomes part of the airport boundary
            
            // Add another position 50m out as the "via" pos: via which we roll to the startup location
            vec.dist = APT_STARTUP_VIA_DIST;
            loc.viaPos = CoordPlusVector(startPos, vec);
        }
    }
    
    /// @brief Find closest startup location, or `nullptr` if non close enough
    /// @param pos Search near this position (only lat/lon are used(
    /// @param _maxDist Search distance, only return a startup location maximum this far away
    /// @param[out] _outDist Distance to returned startup location, `NAN` if none found.
    const StartupLoc* FindStartupLoc (const positionTy& pos,
                                      double _maxDist = APT_JOIN_MAX_DIST_M,
                                      double* _outDist = nullptr) const
    {
        const StartupLoc* pRet = nullptr;
        _maxDist *= _maxDist;                   // square, more performant for comparison
        for (const StartupLoc& loc: vecStartupLocs)
        {
            const double dist = DistLatLonSqr(loc.lat, loc.lon,
                                              pos.lat(), pos.lon());
            if (dist < _maxDist)
            {
                _maxDist = dist;
                pRet = &loc;
            }
        }
        
        // return results
        if (_outDist)
            *_outDist = pRet ? std::sqrt(_maxDist) : NAN;
        return pRet;
    }
    
    /// @brief Project pos onto the path leading away from the startup location
    void ProjectPosOnStartupPath (positionTy& _pos, const StartupLoc& _startLoc)
    {
        // One thing is for sure: the heading must match startup location
        _pos.heading() = _startLoc.heading;
        _pos.f.bHeadFixed = true;
        _pos.f.specialPos = SPOS_STARTUP;
        // And the altitude needs re-comupting
        _pos.alt_m() = NAN;
        
        // Compute temporary "coordinates" in meters, relative to the search position
        distToLineTy dist;
        const double start_x = Lon2Dist(_startLoc.lon      - _pos.lon(), _pos.lat());   // x is eastward
        const double start_y = Lat2Dist(_startLoc.lat      - _pos.lat());               // y is northward
        const double via_x   = Lon2Dist(_startLoc.viaPos.x - _pos.lon(), _pos.lat());
        const double via_y   = Lat2Dist(_startLoc.viaPos.y - _pos.lat());
        DistPointToLineSqr(0.0, 0.0, start_x, start_y, via_x, via_y, dist);
        
        // We don't want the plane to crash into the gate, so we stop the plane
        // at the startup location
        if (dist.leg2_len2 > dist.len2)             // is base beyond startup location?
        {
            _pos.lat() = _startLoc.lat;
            _pos.lon() = _startLoc.lon;
        } else {
            // otherwise move to projection on the path to the startup location
            double base_x = NAN, base_y = NAN;
            DistResultToBaseLoc(start_x, start_y,
                                via_x, via_y,
                                dist, base_x, base_y);
            _pos.lon() += Dist2Lon(base_x, _pos.lat());
            _pos.lat() += Dist2Lat(base_y);
        }
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

/// Map of airports, key is the id (typically: ICAO code)
typedef std::map<std::string, Apt> mapAptTy;

/// Global map of airports
static mapAptTy gmapApt;

/// Lock to access global map of airports
static std::mutex mtxGMapApt;

// Temporary storage while reading an airport from apt.dat
vecTaxiNodesTy Apt::vecRwyNodes;
mapTaxiTmpPosTy Apt::mapPos;
listTaxiTmpPathTy Apt::listPaths;
vecIdxTy Apt::vecPathEnds;

// Y Probe for terrain altitude computation
XPLMProbeRef Apt::YProbe = NULL;

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
    
    // Post-process the temporary maps/lists into proper apt vectors
    // (Only if we processed the 120 taxiways, not if we used the 1200 taxi routes)
    if (apt.HasTempNodesEdges()) {
        apt.PostProcessPaths();         // add nodes and edges for taxways
//#ifdef DEBUG
//        LOG_ASSERT(apt.ValidateNodesEdges(false));
//#endif
        apt.AddRwyEdges();              // add edges for each runway
//#ifdef DEBUG
//        LOG_ASSERT(apt.ValidateNodesEdges(false));
//#endif
    }
    
    // Prepare the indirect array, which sorts by edge angle
    // for faster finding of edges by heading
    apt.SortTaxiEdges();
    
    // Now connect open ends, ie. try finding joints between a node and existing edges
    apt.JoinPathEnds();
#ifdef DEBUG
    LOG_ASSERT(apt.ValidateNodesEdges());
#endif

    // Fancy debug-level logging message, listing all runways
    // (here already as `apt` gets moved soon and becomes reset)
    LOG_MSG(logDEBUG, "apt.dat: Added %s at %s with %lu runways (%s) and [%lu|%lu] taxi nodes|edges",
            apt.GetId().c_str(),
            std::string(apt.GetBounds()).c_str(),
            (long unsigned)(apt.GetRwyEndPtVec().size() / 2),
            apt.GetRwysString().c_str(),
            (long unsigned)apt.GetTaxiNodesVec().size(),
            (long unsigned)apt.GetTaxiEdgeVec().size());

    // Access to the list of airports is guarded by a lock
    const std::string key = apt.GetId();          // make a copy of the key, as `apt` gets moved soon:
    {
        std::lock_guard<std::mutex> lock(mtxGMapApt);
        gmapApt.emplace(key, std::move(apt));
    }
    
    // clear all temporary storage
    vecRwyNodes.clear();
    mapPos.clear();
    listPaths.clear();
    vecPathEnds.clear();
}

/// Return the a node, ie. the starting point of the edge
const TaxiNode& TaxiEdge::GetA (const Apt& apt) const
{
    return apt.GetTaxiNodesVec()[a];
}

/// Return the b node, ie. the ending point of the edge
const TaxiNode& TaxiEdge::GetB (const Apt& apt) const
{
    return apt.GetTaxiNodesVec()[b];
}


//
// MARK: File Reading Thread
// This code runs in the thread for file reading operations
//
    
/// @brief List of accepted Line Type Codes
/// @see   More information on reading from `apt.dat` is on [a separate page](@ref apt_dat).
static const std::array<int,8> APT_LINE_TYPES { 1, 7, 10, 11, 51, 57, 60, 61 };
/// @brief These row types terminate a path
/// @see   More information on reading from `apt.dat` is on [a separate page](@ref apt_dat).
static const std::array<int,4> APT_PATH_TERM_ROW_CODES { 113, 114, 115, 116 };

/// @brief Process one "120" section of an `apt.dat` file, which contains a taxi line definitions in the subsequent 111-116 lines
/// @details Starts reading in the next line, expecting nodes in lines starting with 111-116.
///          According to specs, such a section has to end with 113-116. But we don't rely on it,
///          so we are more flexible in case of errorneous files. We read until we find a line _not_ starting
///          with 111-116 and return that back to the caller to be processed again.\n
///          We only process line segments with Line Type Codes for taxiway centerlines.
///          A segment ends on _any_ line with no or a non-matching line type code.
///          Such a segment becomes a path in LiveTraffic. One 120-section of apt.dat
///          can contain many such segments ending in lines with no line type code.\n
///          All nodes are temporarily stored in a local list. After reading finished, some nodes are removed,
///          as in actual files nodes can be very close together (up to being identical!).
///          We combine nodes to longer egdes until the edge's angle turns more than 15° away
///          from the orginal heading. Then only the next edge begins. This thins out nodes and egdes.
///          The remaining nodes and edges are added to the apt's taxiway network.
/// @see     More information on reading from `apt.dat` is on [a separate page](@ref apt_dat).
/// @returns the next line read from the file, which is after the "120" section
static std::string ReadOneTaxiLine (std::ifstream& fIn, Apt& apt, unsigned long& lnNr)
{
    TaxiTmpPath path;               // holds the path (centerline positions) we are reading now
    ptTy prevBezPt;                 // previous bezier point
    std::string ln;
    while (fIn)
    {
        // read a line from the input file
        safeGetline(fIn, ln);
        ++lnNr;

        // ignore empty lines
        if (ln.empty()) continue;
        
        // tokenize the line
        std::vector<std::string> fields = str_tokenize(ln, " \t", true);
        
        // We need at minimum 3 fields (line id, latitude, longitude)
        if (fields.size() < 3) break;
        
        // Not any of "our" line codes (we treat them all equal)? -> stop
        int lnCod = std::stoi(fields[0]);
        if (lnCod < 111 || lnCod > 116)
            break;
        
        // Check for the Line Type Code to be Taxi Centerline
        int lnTypeCode = 0;

        // In case of line codes 111, 113 the Line Type Code is in field 3
        if (lnCod == 111 || lnCod == 113) {
            if (fields.size() >= 4)
                lnTypeCode = std::stoi(fields[3]);
        // In case of line codes 112, 114 the Line Type Code is in field 5
        } else if (lnCod == 112 || lnCod == 114) {
            if (fields.size() >= 6)
                lnTypeCode = std::stoi(fields[5]);
        }
        
        // Is this a node starting/continuing a taxi centerline?
        const bool bIsCenterline = std::any_of(APT_LINE_TYPES.cbegin(),  APT_LINE_TYPES.cend(),
                                               [lnTypeCode](int c){return c == lnTypeCode;});
        // If this node does not start/continue a centerline, does it at least end an already started one?
        const bool bEndsCenterline =
        !path.listPos.empty() &&       // is there any path to terminate?
        (!bIsCenterline || std::any_of(APT_PATH_TERM_ROW_CODES.cbegin(),  APT_PATH_TERM_ROW_CODES.cend(),
                                       [lnCod](int c){return c == lnCod;}));

        // Do we need to process this node?
        if (bIsCenterline || bEndsCenterline)
        {
            // Read location and Bezier control point
            ptTy pos (std::stod(fields[2]), std::stod(fields[1]));    // lon, lat
            ptTy bezPt;
            if ((lnCod == 112 || lnCod == 114 || lnCod == 116) &&
                fields.size() >= 5)
            {
                // read Bezier control point
                bezPt.x = std::stod(fields[4]);         // lon
                bezPt.y = std::stod(fields[3]);         // lat
#ifdef DEBUG
                // remember Bezier handle for output to GPS Visualizer
                TaxiNode& n = apt.vecBezierHandles.emplace_back(pos.y, pos.x);
                n.prevIdx = lnNr;
                n.bVisited = false;
                apt.vecBezierHandles.emplace_back(bezPt.y, bezPt.x);
                // if there is a previous pos (to which we will apply the control point, too, just mirrored)
                // then also add the mirrored handle
                if (!path.listPos.empty())
                {
                    TaxiNode& n2 = apt.vecBezierHandles.emplace_back(pos.y, pos.x);
                    n2.prevIdx = lnNr;
                    n2.bVisited = true;                 // indicates "mirrored"
                    apt.vecBezierHandles.emplace_back(bezPt.mirrorAt(pos).y, bezPt.mirrorAt(pos).x);
                }
#endif
            }
            
            // If position is different from previous
            // (there are quite a number of _exactly_ equal subsequent nodes
            //  in actual apt.dat, which we filter out this way)
            if (path.listPos.empty() || path.listPos.back() != pos)
            {
                // We need a loop here as in case of row codes 113/114 we "close a loop", which requires to add to path segments
                for(;;) {
                    // If necessary add additional nodes along the Bezier curve
                    // from the previous node to the current
                    if (!path.listPos.empty() && (prevBezPt.isValid() || bezPt.isValid()))
                    {
                        // the previous node, where the Bezier curve starts
                        const TaxiTmpPos& prevPos = path.listPos.back();
                        // length of the straight line from prevPos to pos
                        const double eLen = DistLatLon(pos.y, pos.x, prevPos.lat(), prevPos.lon());
                        if (eLen > APT_MAX_SIMILAR_NODE_DIST_M) {
                            // the second Bezier control point needs to be mirrored at that pos
                            const ptTy mbezPt = bezPt.mirrorAt(pos);
                            // number of segments we will create, at least 2 (ie. at least split in half)
                            const int numSegm = std::max (2, int(eLen / APT_JOIN_MAX_DIST_M / 2));
                            for (int s = 1; s < numSegm; ++s)
                            {
                                // Calculate a point on the Bezier curve
                                const ptTy p =
                                prevBezPt.isValid() && mbezPt.isValid() ? Bezier(double(s)/numSegm, prevPos, prevBezPt, mbezPt, pos) :
                                !mbezPt.isValid()                       ? Bezier(double(s)/numSegm, prevPos, prevBezPt,         pos) :
                                                                          Bezier(double(s)/numSegm, prevPos,            mbezPt, pos);
                                // Add the Bezier curve node to our backlog
                                apt.AddTaxiTmpPos(p.y, p.x);            // add the node to the airport's temporary list of nodes
                                path.listPos.emplace_back(p.y, p.x);    // add the node position to the path (temporary storage)
                            }
                        }
                    }
                    
                    // Add the actual node to our backlog
                    apt.AddTaxiTmpPos(pos.y, pos.x);            // add the node to the airport's temporary list of nodes
                    path.listPos.emplace_back(pos.y, pos.x);    // add the node position to the path (temporary storage)
                    
                    // Exit the loop if not row codes 113/114 (closing loop)
                    if (lnCod != 113 && lnCod != 114)
                        break;
                    
                    // As we are closing a loop we also need to add the segment
                    // from this point back to the beginning, so we add the first point once again:
                    pos.x = path.listPos.front().x;
                    pos.y = path.listPos.front().y;
                    prevBezPt = bezPt;
                    bezPt.clear();
                    lnCod += 2;         // this makes sure we break out of the loop next time
                }
            }
        
            // If this ends a path then we add the entire path to our repository
            if (bEndsCenterline)
            {
                apt.AddTaxiTmpPath(std::move(path));    // move the entire path to the temporary list of paths for post-processing
                path.listPos.clear();
            }
            
            // move on to next node
            prevBezPt = bezPt;
        } // is centerline or ends a centerline
        else
        {
            // don't process this node, clear temp stuff
            prevBezPt.clear();
        }
    }
    
    // return the last line so it can be processed again
    return ln;
}

/// @brief Read airports in the one given `apt.dat` file
/// @details    The function process the following line types:\n
///             1 - Airport header to start a new airport and learn its name/id\n
///             100 - Runway definitions\n
///             120 - Line segments (incl. subsequent 111-116 codes), or alternatively, if no 120 code is found:\n
///             1201, 1202  - Taxi route netwirk
/// @see        More information on reading from `apt.dat` is on [a separate page](@ref apt_dat).
static void ReadOneAptFile (std::ifstream& fIn, const boundingBoxTy& box)
{
    // Walk the file
    std::string ln;
    unsigned long lnNr = 0;             // for debugging purposes we are interested to track the file's line number
    bool bProcessGivenLn = false;       // process a line returned by a sub-routine?
    // Are we reading 120 taxi centerlines or 1200 taxi route network?
    enum netwTypeTy { NETW_UNKOWN=0, NETW_CENTERLINES, NETW_TAXIROUTES } netwType = NETW_UNKOWN;
    Apt apt;
    while (!bStopThread && (bProcessGivenLn || fIn))
    {
        // Either process a given line or fetch a new one
        if (bProcessGivenLn) {
            // the line is in `ln` already, just reset the flag
            bProcessGivenLn = false;
        } else {
            // read a fresh line from the file
            ln.clear();
            safeGetline(fIn, ln);
            ++lnNr;
        }
        
        // ignore empty lines
        if (ln.empty()) continue;
        
        // test for beginning of an airport
        if (ln.size() > 10 &&
            ln[0] == '1' &&
            (ln[1] == ' ' || ln[1] == '\t'))
        {
            // found an airport's beginning
            
            // If the previous airport is valid add it to the list
            if (apt.IsValid())
                Apt::AddApt(std::move(apt));
            else
                // clear the airport object nonetheless
                apt = Apt();
            
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(ln, " \t", true);
            if (fields.size() >= 5 &&           // line contains an airport id, and
                gmapApt.count(fields[4]) == 0)  // airport is not yet defined in map
            {
                // re-init apt object, now with the proper id defined
                apt = Apt(fields[4]);
                netwType = NETW_UNKOWN;
            }
        }
        
        // test for a runway...just to find location info
        else if (apt.HasId() &&             // an airport identified and of interest?
            ln.size() > 20 &&            // line long enough?
            ln[0] == '1' &&              // starting with "100 "?
            ln[1] == '0' &&
            ln[2] == '0' &&
            (ln[3] == ' ' || ln[3] == '\t'))
        {
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(ln, " \t", true);
            if (fields.size() == 26) {      // runway description has to have 26 fields
                const double lat = std::stod(fields[ 9]);
                const double lon = std::stod(fields[10]);
                if (-90.0 <= lat && lat <= 90.0 &&
                    -180.0 <= lon && lon < 180.0)
                {
                    // Have we accepted the airport already?
                    // Or - this being the first rwy - does the rwy lie in the search bounding box?
                    if (apt.HasRwyEndpoints() ||
                        box.contains(positionTy(lat,lon)))
                    {
                        // add both runway ends to the airport
                        apt.AddRwyEnds(lat, lon,
                                       std::stod(fields[11]),       // displaced
                                       fields[ 8],                  // id
                                       // other rwy end:
                                       std::stod(fields[18]),       // lat
                                       std::stod(fields[19]),       // lon
                                       std::stod(fields[20]),       // displayced
                                       fields[17]);                 // id
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
        
        // test for the start of a taxi line segment
        // This is valid for 120 as well as 120x:
        else if (apt.HasRwyEndpoints() &&
                 ln.size() >= 3 &&
                 ln[0] == '1' &&
                 ln[1] == '2' &&
                 ln[2] == '0')
        {
            if (ln == "120 RM" || ln == "120 TB") {
                // specifically ignore these sections, they draw markings
                // for gate positions, taxiway borders etc.
                // often using taxi centerline codes,
                // but the markings aren't actually taxiways
            }
            // Standard Line segment, that could be a centerline?
            else if (netwType != NETW_TAXIROUTES &&                         // not yet decided for the other type of network?
                (ln.size() == 3 ||                                          // was just the text "120"
                 (ln.size() >= 4 && (ln[3] == ' ' || ln[3] == '\t'))))      // or "120 " plus more
            {
                // Read the entire line segment
                ln = ReadOneTaxiLine(fIn, apt, lnNr);
                bProcessGivenLn = true;         // process the returned line read from the file
                if (apt.HasTempNodesEdges())    // did we (latest now) add taxi segments?
                    netwType = NETW_CENTERLINES;
            }
            else if (netwType != NETW_CENTERLINES)
            {
                // separate the line into its field values
                std::vector<std::string> fields = str_tokenize(ln, " \t", true);
                int lnCode = std::stoi(fields[0]);
                
                // 1201 - Taxi route network node
                if (lnCode == 1201 && fields.size() >= 5) {
                    // Convert and briefly test the given location
                    const double lat = std::stod(fields[1]);
                    const double lon = std::stod(fields[2]);
                    const size_t idx = std::stoul(fields[4]);
                    if (-90.0 <= lat && lat <= 90.0 &&
                        -180.0 <= lon && lon < 180.0)
                    {
                        netwType = NETW_TAXIROUTES;
                        apt.AddTaxiNodeFixed(lat, lon, idx);
                    }   // has valid location
                }
                else if (lnCode == 1202 && fields.size() >= 3) {
                    // Convert indexes and try adding the node
                    const size_t n1 = std::stoul(fields[1]);
                    const size_t n2 = std::stoul(fields[2]);
                    bool bRunway = (fields.size() >= 5 &&
                                    fields[4] == "runway");
                    apt.AddTaxiEdge(n1, n2,
                                    bRunway ? TaxiEdge::RUN_WAY : TaxiEdge::TAXI_WAY);
                }
            }       // not NETW_CENTERLINE
        }           // "120"
        
        // Startup locations, row code 1300
        else if (apt.HasRwyEndpoints() &&
                 ln.size() > 20 &&            // line long enough?
                 ln[0] == '1' &&              // starting with "100 "?
                 ln[1] == '3' &&
                 ln[2] == '0' &&
                 ln[3] == '0' &&
                 (ln[4] == ' ' || ln[4] == '\t'))
        {
            // separate the line into its field values
            std::vector<std::string> fields = str_tokenize(ln, " \t", true);
            if (fields.size() >= 4)
            {
                const double lat  = std::stod(fields[1]);       // latitude
                const double lon  = std::stod(fields[2]);       // longigtude
                const double head = std::stod(fields[3]);       // heading
                std::string id;                                 // all the rest makes up the id
                for (size_t i = 4; i < fields.size(); ++i)
                    id += fields[i] + ' ';
                if (!id.empty()) id.pop_back();                 // remove the last separating space
                apt.AddStartupLoc(id, lat, lon, head);
            }
        }

    }               // for each line of the apt.dat file
    
    // If the last airport read is valid don't forget to add it to the list
    if (!bStopThread && apt.IsValid())
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
    
    // This is a thread main function, set thread's name
    SET_THREAD_NAME("LT_ReadApt");

    // To avoid costly distance calculations we define a bounding box
    // just by calculating lat/lon values north/east/south/west of given pos
    // and include all airports with coordinates falling into it
    const boundingBoxTy box (ctr, radius);
    
    // --- Cleanup first: Remove too far away airports ---
    PurgeApt(box);
    
    // --- Add new airports ---
    // Count the number of files we have accessed
    int cntFiles = 0;
    bool bLooksLikeXP12 = false;            // XP12 Alpha introduced the *GLOBAL AIRPORTS* entry

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

        // Remove the SCENERY_PACK part
        lnScenery.erase(0, lenSceneryLnBegin);

        // Starting with XP12 Alpha there's a special entry we can't read right here but need to take care of later
        if (lnScenery == APTDAT_SCENERY_GLOBAL_APTS) {
            bLooksLikeXP12 = true;
            continue;
        }

        // the remainder is a path into X-Plane's main folder
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
        const std::string sFileName = LTCalcFullPath(bLooksLikeXP12 ? APTDAT_GLOBAL_AIRPORTS APTDAT_SCENERY_ADD_LOC : APTDAT_RESOURCES_DEFAULT APTDAT_SCENERY_ADD_LOC);
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
    
    LOG_MSG(logINFO, "Done reading from %d apt.dat files, have now %d airports",
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
    // If not doing snapping, then not doing reading...
    if (dataRefs.GetFdSnapTaxiDist_m() <= 0)
        return;
    
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
        }
        bAptsAdded = false;
        return;
    }
    else
        lastCameraPos = camera;

    // Start the thread to read apt.dat, using current camera position as center point
    // and _double_ plane search radius as search radius
    radius *= 2;
    LOG_MSG(logINFO, "Starting thread to read apt.dat for airports %.1fnm around %s",
            radius / M_per_NM, std::string(lastCameraPos).c_str());
    bStopThread = false;
    futRefreshing = std::async(std::launch::async,
                               AsyncReadApt, lastCameraPos, radius);
    // need to check for rwy altitudes soon!
    bAptsAdded = true;
}

// Return the best possible runway to auto-land at
positionTy LTAptFindRwy (const LTAircraft::FlightModel& _mdl,
                         const positionTy& _from,
                         double _speed_m_s,
                         std::string& _rwyId,
                         const std::string& _logTxt)
{
    // --- Preparation of aircraft-related data ---
    // allowed VSI range depends on aircraft model, converted to m/s
    const double vsi_min = -_mdl.VSI_MAX * Ms_per_FTm;
    const double vsi_max =  _mdl.VSI_FINAL * ART_RWY_MAX_VSI_F * Ms_per_FTm;
    
    // The speed to use: cut off at a reasonable approach speed:
    if (_speed_m_s > _mdl.FLAPS_DOWN_SPEED * ART_APPR_SPEED_F / KT_per_M_per_S)
        _speed_m_s = _mdl.FLAPS_DOWN_SPEED * ART_APPR_SPEED_F / KT_per_M_per_S;
    
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
        
        // Find the rwy endpoints matching the current plane's heading
        for (const RwyEndPt& re: apt.GetRwyEndPtVec())
        {
            // skip if rwy heading differs too much from flight heading
            if (std::abs(HeadingDiff(re.heading, _from.heading())) > ART_RWY_MAX_HEAD_DIFF)
                continue;
            
            // We need to know the runway's altitude for what comes next
            if (std::isnan(re.alt_m))
                continue;
            
            // Heading towards rwy, compared to current flight's heading
            // (Find the rwy which requires least turn now.)
            const double bearing = CoordAngle(_from.lat(), _from.lon(), re.lat, re.lon);
            const double headingDiff = std::abs(HeadingDiff(_from.heading(), bearing));
            if (headingDiff > bestHeadingDiff)      // worse than best known match?
                continue;
            
            // 3. Vertical speed, for which we need to know distance / flying time
            const double dist = CoordDistance(_from.lat(), _from.lon(), re.lat, re.lon);
            if (dist > ART_RWY_MAX_DIST)        // too far out
                continue;
            const double d_ts = dist / _speed_m_s;
            const double agl = _from.alt_m() - re.alt_m;
            const double vsi = (-agl) / d_ts;
            if (vsi < vsi_min)                  // would need too steep sinking?
                continue;
            
            // flying more than 300ft/100m above ground?
            if (agl > 100.0) {
                // also consider max_vsi
                if (vsi > vsi_max)              // would fly too flat? -> too far out?
                    continue;
            } else {
                // pretty close too the ground, cut off at shorter distance
                if (dist > 3000.0)              // 3000m is a runway length...we shouldn't look further that close to the ground
                    continue;
            }
            
            // We've got a match!
            bestApt = &apt;
            bestRwyEndPt = &re;
            bestHeadingDiff = headingDiff;      // the heading diff (which would be a selection criterion on several rwys match)
            bestArrivalTS = _from.ts() + d_ts;   // the arrival timestamp
        }
    }
    
    // Didn't find a suitable runway?
    if (!bestRwyEndPt || !bestApt) {
        if (!_logTxt.empty())
            LOG_MSG(logDEBUG, "Didn't find runway for %s with heading %.0f",
                    _logTxt.c_str(),
                    _from.heading());
        _rwyId.clear();
        return positionTy();
    }
    
    // Found a match!
    positionTy retPos = positionTy(bestRwyEndPt->lat,
                                   bestRwyEndPt->lon,
                                   bestRwyEndPt->alt_m,
                                   bestArrivalTS,
                                   bestRwyEndPt->heading,
                                   _mdl.PITCH_FLARE,
                                   0.0,
                                   GND_ON,
                                   UNIT_WORLD, UNIT_DEG,
                                   FPH_TOUCH_DOWN);
    retPos.f.bHeadFixed = true;
    retPos.f.specialPos = SPOS_RWY;
    _rwyId = bestApt->GetId();
    _rwyId += '/';
    _rwyId += bestRwyEndPt->id;
    if (!_logTxt.empty())
        LOG_MSG(logDEBUG, "Found runway %s at %s for %s",
                _rwyId.c_str(),
                std::string(retPos).c_str(),
                _logTxt.c_str());
    return retPos;
}


// Snaps the passed-in position to the nearest rwy or taxiway if appropriate
bool LTAptSnap (LTFlightData& fd, dequePositionTy::iterator& posIter,
                bool bInsertTaxiTurns)
{
    // Configured off?
    if (dataRefs.GetFdSnapTaxiDist_m() <= 0)
        return false;
    
    // Access to the list of airports is guarded by a lock
    std::lock_guard<std::mutex> lock(mtxGMapApt);

    // Which airport are we looking at?
    Apt* pApt = LTAptFind(*posIter);
    if (!pApt)                          // not a position in any airport's bounding box
        return false;

    // Let's snap!
    return pApt->SnapToTaxiway(fd, posIter, bInsertTaxiTurns);
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
    
    // remove all airport data
    gmapApt.clear();
    lastCameraPos = positionTy();
    bAptsAdded = false;
}


// Dumps the entire taxi network into a CSV file readable by GPS Visualizer
/// @see For a suggestion of settings for display:
/// https://www.gpsvisualizer.com/map_input?bg_map=google_openstreetmap&bg_opacity=70&form=leaflet&google_wpt_sym=diamond&trk_list=0&trk_opacity=100&trk_width=2&units=metric&width=1400&wpt_color=aqua
bool LTAptDump (const std::string& _aptId)
{
    // find the airport by id
    if (gmapApt.count(_aptId) < 1) return false;
    try {
        const Apt& apt = gmapApt.at(_aptId);
        
        // open the output file
        const std::string fileName (dataRefs.GetXPSystemPath() + _aptId + ".csv");
        std::ofstream out (fileName, std::ios_base::out | std::ios_base::trunc);
        // column headers
        out << "type,BOT,symbol,color,rotation,latitude,longitude,time,speed,course,name,desc\n";
        out.precision(11);               // precision is all digits, so we need something like 123.45678901
        
        // Dump all rwy endpoints as Waypoints
        for (const RwyEndPt& re: apt.GetRwyEndPtVec())
            out
            << "W,,"                                    // type, BOT
            << "arrow,"                                 // symbol
            << "red,"                                   // color
            << std::lround(re.heading) << ','           // rotation
            << re.lat << ',' << re.lon << ','           // latitude,longitude
            << ",,,"                                    // time,speed,course
            << re.id << ','                             // name
            << std::lround(re.heading) << "°,"          // desc
            << "\n";
        
        // Dump all startup locations as Waypoints
        for (const StartupLoc& loc: apt.GetStartupLocVec())
            out
            << "W,,"                                    // type, BOT
            << "wedge,"                                 // symbol
            << "orange,"                                // color
            << std::lround(loc.heading) << ','          // rotation
            << loc.lat << ',' << loc.lon << ','         // latitude,longitude
            << ",,,"                                    // time,speed,course
            << loc.id << ','                            // name
            << std::lround(loc.heading) << "°,"         // desc
            << "\n";
        
        // Dump all startup paths as tracks
        for (const StartupLoc& loc: apt.GetStartupLocVec())
        {
            out
            << "T,1,,"                                  // type, BOT, symbol
            << "orange,"                                // color
            << std::lround(loc.heading) << ','          // rotation
            << loc.lat << ',' << loc.lon << ','         // latitude,longitude
            << ",,"                                     // time,speed
            << std::lround(loc.heading) << ','          // course
            << "Path leaving " << loc.id << ','         // name
            << std::lround(loc.heading) << "°,"         // desc
            << "\n";

            out
            << "T,0,,"                                  // type, BOT, symbol
            << "orange,"                                // color
            << std::lround(loc.heading) << ','          // rotation
            << loc.viaPos.y << ',' << loc.viaPos.x << ','  // latitude,longitude
            << ",,"                                     // time,speed
            << std::lround(loc.heading) << ','          // course
            << ','                                      // name, desc
            << "\n";

        }
        
        // Dump all nodes as Waypoints
        size_t i = 0;
        for (const TaxiNode& n: apt.GetTaxiNodesVec()) {
            out
            << "W,,"                                    // type, BOT
            << (n.vecEdges.size() == 0 ? "pin," :       // symbol
                n.vecEdges.size() == 1 ? "circle," :
                n.vecEdges.size() == 2 ? "square," :
                n.vecEdges.size() == 3 ? "triangle," :
                n.vecEdges.size() == 4 ? "diamond," : "star,")
            << (apt.IsConnectedToRwy(i) ? "red," :        // color: red if rwy, blue if 2 edges, else auqa
                n.vecEdges.size() == 2 ? "blue," : "aqua,")
            << "0,"                                     // rotation
            << n.lat << ',' << n.lon << ','             // latitude,longitude
            << ",,,"                                    // time,speed,course
            << "Node " << i << ','                      // name
            << n.vecEdges.size() << " edges"            // desc
            << "\n";
            i++;
        }
        
        // Dump all edges as Tracks
        i = 0;
        for (const TaxiEdge& e: apt.GetTaxiEdgeVec())
        {
            const TaxiNode& a = e.GetA(apt);
            const TaxiNode& b = e.GetB(apt);
            
            out
            << "T,1,,"                                  // type, BOT, symbol
            << (e.GetType() == TaxiEdge::RUN_WAY ? "red," : "blue,")  // color
            << std::lround(e.angle) << ','              // rotation
            << a.lat << ',' << a.lon << ','             // latitude,longitude
            << ",,"                                     // time,speed
            << std::lround(e.angle) << ','              // course
            << "Edge " << (i++) << ','                  // name
            << std::lround(e.angle) << "°, nodes " << e.startNode() << '-' << e.endNode() // desc
            << "\n";

            out
            << "T,0,,"                                  // type, BOT, symbol
            << (e.GetType() == TaxiEdge::RUN_WAY ? "red," : "blue,")  // color
            << std::lround(e.angle) << ','              // rotation
            << b.lat << ',' << b.lon << ','             // latitude,longitude
            << ",,"                                     // time,speed
            << std::lround(e.angle) << ','              // course
            << ','                                      // name, desc
            << "\n";

        }
            
#ifdef DEBUG
        // Dump all Bezier handles
        for (auto iter = apt.vecBezierHandles.cbegin();
             iter != apt.vecBezierHandles.cend();
             ++iter)
        {
            const TaxiNode& a = *iter;
            const TaxiNode& b = *(++iter);
            
            out
            << "T,1,,"                                  // type, BOT, symbol
            << (a.bVisited ? "orange," : "magenta,")    // color (mirrored control point or not?)
            <<  ','                                     // rotation
            << a.lat << ',' << a.lon << ','             // latitude,longitude
            << ",,"                                     // time,speed
            << ','                                      // course
            << "Bezier Handle Ln " << a.prevIdx << ','  // name
            << (a.bVisited ? "mirrored" : "")           // desc
            << "\n";

            out
            << "T,0,,"                                  // type, BOT, symbol
            << (a.bVisited ? "orange," : "magenta,")    // color (mirrored control point or not?)
            <<  ','                                     // rotation
            << b.lat << ',' << b.lon << ','             // latitude,longitude
            << ",,"                                     // time,speed
            << ','                                      // course
            << ','                                      // name, desc
            << "\n";
        }
#endif
        
        // Close the file
        out.close();
        return true;
    }
    catch (...) {
        return false;
    }
}

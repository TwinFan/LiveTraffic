# LTApt.cpp

## Call Hierarchy

```mermaid
---
config:
  layout: elk
---
flowchart LR
    Find["LTAptFind()"] --> Contains["Apt::Contains()"]
    FindRwy["LTAptFindRwy()"] --> GetRwyVec["Apt::GetRwyEndPtVec()"]
    SnapRwy["LTAptSnapIfOverRwy()"] --> Find & FindClosest["Apt::FindClosestEdge()"]
    FindStartupAPI["LTAptFindStartupLoc()"] --> Available["LTAptAvailable()"] & Find & FindStartup["Apt::FindStartupLoc()"]
    Snap["LTAptSnap()"] --> Available & Find & SnapTaxi["Apt::SnapToTaxiway()"]
    SnapTaxi --> FindStartup & ProjectStartup["Apt::ProjectPosOnStartupPath()"] & GetPosType["Apt::GetPosEdgeType()"] & FindClosest  & Shortest["Apt::ShortestPath()"] & EdgeBetween["Apt::GetEdgeBetweenNodes()"] & GetA["TaxiEdge::GetA()"] & GetB["TaxiEdge::GetB()"]
    FindClosest --> FindHeading["Apt::FindEdgesForHeading()"]
    GetPosType --> FindClosest
    Shortest --> EdgeBetween
    Disable["LTAptDisable()"] --> DestroyProbe["Apt::DestroyYProbe()"]
    Dump["LTAptDump()"] --> GetRwyVec & GetStartupVec["Apt::GetStartupLocVec()"] & GetTaxiNodes["Apt::GetTaxiNodesVec()"] & GetTaxiEdges["Apt::GetTaxiEdgeVec()"] & ConnectedRwy["Apt::IsConnectedToRwy()"] & GetA & GetB
```

### LTApt::SnapToTaxiway()

The function first tries to snap the current aircraft position to a startup location or the closest taxi/runway edge. If that fails, it has a fallback for a specific near-edge drift case based on the previous snapped position. If snapping succeeds and taxi-turn insertion is enabled, it tries to insert intermediate positions along the airport taxi graph using `ShortestPath()`; if no graph path works, it falls back to inserting a geometric edge-intersection point when that looks plausible.

```mermaid
flowchart TD
  Start["Apt::SnapToTaxiway(fd, posIter, bInsertTaxiTurns)"] --> Init["Read current pos and previous pos"]
  Init --> Startup["Look for nearby startup location"]

  Startup --> StartupFound{"Startup location found?"}
  StartupFound -- yes --> FixStartupHead["Set heading from startup location"]
  StartupFound -- no --> PrepSearch["Prepare position for edge search"]
  FixStartupHead --> PrepSearch

  PrepSearch --> PrevRwy{"Previous pos was runway?"}
  PrevRwy -- yes --> UseTrackingHead["Use tracking-data heading if suitable"]
  PrevRwy -- no --> FindEdge["FindClosestEdge()"]
  UseTrackingHead --> FindEdge

  FindEdge --> EdgeFound{"Taxi/runway edge found?"}

  EdgeFound -- no --> HasStartup{"Startup location found?"}
  HasStartup -- yes --> ProjectStartup["ProjectPosOnStartupPath()"]
  ProjectStartup --> Success["return true"]

  HasStartup -- no --> PrevSnapped{"Previous pos has taxi edge?"}
  PrevSnapped -- no --> Fail["return false"]
  PrevSnapped -- yes --> BlackHole["Check 'black hole horizon' case"]

  BlackHole --> BlackHoleMatch{"Near previous pos and about 90 deg off previous edge?"}
  BlackHoleMatch -- yes --> SnapToPrev["Copy previous snapped position/edge/flags"]
  SnapToPrev --> Success
  BlackHoleMatch -- no --> Fail

  EdgeFound -- yes --> MarkFound["Position is snapped to closest edge"]
  MarkFound --> EdgeIsRwy{"Edge is runway?"}
  EdgeIsRwy -- no --> SetTaxiPhase["Set flight phase to FPH_TAXI"]
  EdgeIsRwy -- yes --> InsertPathGate
  SetTaxiPhase --> InsertPathGate

  InsertPathGate{"Insert taxi turns requested and previous pos exists?"}
  InsertPathGate -- no --> Success
  InsertPathGate -- yes --> PathEligible{"Previous/current edges suitable for path search?"}

  PathEligible -- no --> Success
  PathEligible -- yes --> PickNodes["Pick relevant start/end taxi nodes"]

  PickNodes --> SanityOK{"Node distances sane?"}
  SanityOK -- no --> Success
  SanityOK -- yes --> CalcMaxLen["Calculate max allowed path length"]

  CalcMaxLen --> ShortestPath["ShortestPath() through taxi graph"]
  ShortestPath --> PathFound{"Path has at least 2 nodes?"}

  PathFound -- yes --> TrimPath["Trim skipped/start runway nodes and adjust lengths"]
  TrimPath --> InsertNodes["Insert artificial taxiway positions into fd.posDeque"]
  InsertNodes --> AssertSorted["Assert posDeque remains sorted"]
  AssertSorted --> Success

  PathFound -- no --> Intersection["Try geometric intersection of previous and current edges"]
  Intersection --> InterValid{"Intersection is ahead, turn is sane, speed/timing valid?"}
  InterValid -- yes --> InsertIntersection["Insert artificial intersection position"]
  InsertIntersection --> AssertSorted2["Assert posDeque remains sorted"]
  AssertSorted2 --> Success
  InterValid -- no --> Success
```



/** Copyright (C) 2018 Ultimaker - Released under terms of the AGPLv3 License */

#include "PolygonConnector.h"

#include "linearAlg2D.h"

namespace cura 
{

Polygons PolygonConnector::connect()
{
    Polygons ret;
    
    std::vector<Polygon> to_connect;
    to_connect.reserve(input_polygons.size());
    for (ConstPolygonPointer poly : input_polygons)
    {
        to_connect.emplace_back(*poly); // copy into list
    }

    while (!to_connect.empty())
    {
        Polygon current = std::move(to_connect.back());
        to_connect.pop_back();

        std::optional<PolygonBridge> bridge = getBridge(current, to_connect);
        if (bridge)
        {
            all_bridges.push_back(*bridge); // just for keeping scores
            // remove other poly from the list and put the newly connected one on the list
            // i.e. replace the old other poly by the new one
            PolygonRef other_poly(*const_cast<ClipperLib::Path*>(bridge->a.to.poly.operator->())); // const casting a ConstPolygonPointer is difficult!
            other_poly = std::move(connectPolygonsAlongBridge(*bridge)); // connect the bridged parts and overwrite the other polygon with it.

            // don't store the current poly, it has just been connected and stored
        }
        else
        {
            ret.add(std::move(current));
        }
    }

    return ret;
}


Polygon PolygonConnector::connectPolygonsAlongBridge(const PolygonConnector::PolygonBridge& bridge)
{
    // enforce the following orientations:
    //
    // <<<<<<X......X<<<<<<< poly2
    //       ^      v
    //       ^      v
    //       ^ a  b v bridge
    //       ^      v
    // >>>>>>X......X>>>>>>> poly1
    //
    // this should work independent from whether it is a hole polygon or a outline polygon
    Polygon ret;
    addPolygonSegment(bridge.b.from, bridge.a.from, ret);
    addPolygonSegment(bridge.a.to, bridge.b.to, ret);
    return ret;
}

void PolygonConnector::addPolygonSegment(const ClosestPolygonPoint& start, const ClosestPolygonPoint& end, PolygonRef result)
{
    // <<<<<<<.start     end.<<<<<<<<
    //        ^             v
    //        ^             v
    // >>>>>>>.end.....start.>>>>>>>
    assert(start.poly == end.poly && "We can only bridge from one polygon from the other if both connections depart from the one polygon!");
    ConstPolygonRef poly = *end.poly;
    char dir = getPolygonDirection(end, start); // we get the direction of the polygon in between the bridge connections, while we add the segment of the polygon not in between the segments

    result.add(start.p());
    bool first_iter = true;
    for (size_t vert_nr = 0; vert_nr < poly.size(); vert_nr++)
    {
        size_t vert_idx =
            (dir > 0)?
            (start.point_idx + 1 + vert_nr) % poly.size()
            : (static_cast<size_t>(start.point_idx) - vert_nr + poly.size()) % poly.size(); // cast in order to accomodate subtracting
        if (!first_iter // don't return without adding points when the starting point and ending point are on the same polygon segment
            && vert_idx == (end.point_idx + ((dir > 0)? 1 : 0)) % poly.size())
        { // we've added all verts of the original polygon segment between start and end
            break;
        }
        first_iter = false;
        result.add(poly[vert_idx]);
    }
    result.add(end.p());
}

char PolygonConnector::getPolygonDirection(const ClosestPolygonPoint& from, const ClosestPolygonPoint& to)
{
    assert(from.poly == to.poly && "We can only bridge from one polygon from the other if both connections depart from the one polygon!");
    ConstPolygonRef poly = *from.poly;
    if (from.point_idx == to.point_idx)
    {
        const Point prev_vert = poly[from.point_idx];
        const coord_t from_dist2 = vSize2(from.p() - prev_vert);
        const coord_t to_dist2 = vSize2(to.p() - prev_vert);
        return (to_dist2 > from_dist2)? 1 : -1;
    }
    // TODO: replace naive implementation by robust implementation
    // naive idea: there are less vertices in between the connection points than around
    const size_t a_to_b_vertex_count = (static_cast<size_t>(to.point_idx) - from.point_idx + poly.size()) % poly.size(); // cast in order to accomodate subtracting
    if (a_to_b_vertex_count > poly.size() / 2)
    {
        return -1; // from a to b is in the reverse direction as the vertices are saved in the polygon
    }
    else
    {
        return 1; // from a to b is in the same direction as the vertices are saved in the polygon
    }
}

std::optional<PolygonConnector::PolygonBridge> PolygonConnector::getBridge(ConstPolygonRef from_poly, std::vector<Polygon>& to_polygons)
{
    std::optional<PolygonConnector::PolygonConnection> connection = getConnection(from_poly, to_polygons);
    if (!connection || connection->getDistance2() > max_dist * max_dist)
    {
        return std::optional<PolygonConnector::PolygonBridge>();
    }

    // try to get the other connection forward
    std::optional<PolygonConnector::PolygonConnection> second_connection = PolygonConnector::getSecondConnection(*connection, line_width);
    if (!second_connection)
    {
        // try to get a bridge with the connections on both sides of the initially calculated connection
        second_connection = PolygonConnector::getSecondConnection(*connection, line_width / 2);
        if (second_connection)
        {
            connection = PolygonConnector::getSecondConnection(*connection, line_width);
        }
    }
    if (connection && second_connection)
    {
        PolygonBridge result;
        result.a = *connection;
        result.b = *second_connection;
        // ensure that b is always the right connection and a the left
        Point a_vec = result.a.to.p() - result.a.from.p();
        Point shift = turn90CCW(a_vec);
        if (dot(shift, result.b.from.p() - result.a.from.p()) > 0)
        {
            std::swap(result.a, result.b);
        }
        return result;
    }
    else
    { // the polygon is too small to have a bridge attached from [connection]
        // TODO: try fitting the bridge from the extrema of the poly
        return std::optional<PolygonConnector::PolygonBridge>();
    }
}

std::optional<PolygonConnector::PolygonConnection> PolygonConnector::getSecondConnection(PolygonConnection& first, coord_t shift_distance)
{
    bool forward = true;
    std::optional<ClosestPolygonPoint> from_a = PolygonUtils::getNextParallelIntersection(first.from, first.to.p(), shift_distance, forward);
    if (!from_a)
    { // then there's also not going to be a b
        return std::optional<PolygonConnector::PolygonConnection>();
    }
    std::optional<ClosestPolygonPoint> from_b = PolygonUtils::getNextParallelIntersection(first.from, first.to.p(), shift_distance, !forward);

    std::optional<ClosestPolygonPoint> to_a = PolygonUtils::getNextParallelIntersection(first.to, first.from.p(), shift_distance, forward);
    if (!to_a)
    {
        return std::optional<PolygonConnector::PolygonConnection>();
    }
    std::optional<ClosestPolygonPoint> to_b = PolygonUtils::getNextParallelIntersection(first.to, first.from.p(), shift_distance, !forward);


    const Point shift = turn90CCW(first.from.p() - first.to.p());
    
    PolygonConnection best;
    coord_t best_total_distance2 = std::numeric_limits<coord_t>::max();
    for (unsigned int from_idx = 0; from_idx < 2; from_idx++)
    {
        std::optional<ClosestPolygonPoint> from_opt = (from_idx == 0)? from_a : from_b;
        if (!from_opt)
        {
            continue;
        }
        for (unsigned int to_idx = 0; to_idx < 2; to_idx++)
        {
            std::optional<ClosestPolygonPoint> to_opt = (to_idx == 0)? to_a : to_b;
            // for each combination of from and to
            if (!to_opt)
            {
                continue;
            }
            ClosestPolygonPoint from = *from_opt;
            ClosestPolygonPoint to = *to_opt;

            coord_t from_projection = dot(from.p() - first.to.p(), shift);
            coord_t to_projection = dot(to.p() - first.to.p(), shift);
            if (from_projection * to_projection <= 0)
            { // ends lie on different sides of the first connection
                continue;
            }

            const coord_t total_distance2 = vSize2(from.p() - to.p()) + vSize2(from.p() - first.from.p()) + vSize(to.p() - first.to.p());
            if (total_distance2 < best_total_distance2)
            {
                best.from = from;
                best.to = to;
                best_total_distance2 = total_distance2;
            }
            
            if (to_opt == to_b)
            {
                break;
            }
        }
        if (from_opt == from_b)
        {
            break;
        }
    }
    if (best_total_distance2 > max_dist * max_dist + 2 * (shift_distance + 10) * (shift_distance + 10))
    {
        return std::optional<PolygonConnector::PolygonConnection>();
    }
    else
    {
        return best;
    }
}


std::optional<PolygonConnector::PolygonConnection> PolygonConnector::getConnection(ConstPolygonRef from_poly, std::vector<Polygon>& to_polygons)
{
    PolygonConnection best_connection;
    coord_t best_connection_distance2 = std::numeric_limits<coord_t>::max();
    ClosestPolygonPoint from_location(from_poly);
    for (ConstPolygonRef to_poly : to_polygons)
    {
        if (to_poly.data() == from_poly.data())
        { // don't connect a polygon to itself
            continue;
        }
        ClosestPolygonPoint to_location(to_poly);
        PolygonUtils::findSmallestConnection(from_location, to_location);

        coord_t connection_distance2 = vSize2(to_location.p() - from_location.p());
        if (connection_distance2 < best_connection_distance2)
        {
            best_connection.from = from_location;
            best_connection.to = to_location;
            best_connection_distance2 = connection_distance2;
            if (connection_distance2 < (line_width + 10) * (line_width + 10))
            {
                return best_connection;
            }
        }
    }
    if (best_connection_distance2 == std::numeric_limits<coord_t>::max())
    {
        return std::optional<PolygonConnector::PolygonConnection>();
    }
    else
    {
        return best_connection;
    }
}


}//namespace cura


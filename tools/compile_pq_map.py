#!/usr/bin/env python3
"""Build compact ESP32-native road/trail geometry around one GPX route."""

import argparse
import gzip
import math
import pathlib
import xml.etree.ElementTree as ET

from mapbox_vector_tile import decode
from pmtiles.reader import MmapSource, Reader, all_tiles
from shapely.geometry import LineString, MultiLineString
from shapely.ops import linemerge, unary_union

EARTH = 6_371_000.0
TILE_EXTENT = 4096.0


def read_gpx(path):
    root = ET.parse(path).getroot()
    segments = root.findall(".//{*}trkseg")
    if not segments:
        raise ValueError("GPX has no track segment")
    points = []
    for segment in segments:
        for point in segment.findall("{*}trkpt"):
            value = (float(point.attrib["lat"]), float(point.attrib["lon"]))
            if not points or value != points[-1]:
                points.append(value)
    if len(points) < 2:
        raise ValueError("GPX needs at least two distinct track points")
    return points


def local_xy(lat, lon, lat0, lon0, lon_scale):
    return (
        math.radians(lon - lon0) * EARTH * lon_scale,
        math.radians(lat - lat0) * EARTH,
    )


def tile_xy_to_local(z, tile_x, tile_y, x, y, lat0, lon0, lon_scale):
    scale = 1 << z
    world_x = tile_x + x / TILE_EXTENT
    # mapbox-vector-tile decodes MVT coordinates with a bottom-left origin.
    world_y = tile_y + (TILE_EXTENT - y) / TILE_EXTENT
    lon = world_x / scale * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1.0 - 2.0 * world_y / scale))))
    return local_xy(lat, lon, lat0, lon0, lon_scale)


def line_parts(geometry):
    if geometry["type"] == "LineString":
        return [geometry["coordinates"]]
    if geometry["type"] == "MultiLineString":
        return geometry["coordinates"]
    return []


def map_kind(properties):
    detail = properties.get("kind_detail", "")
    if detail in {"path", "track", "cycleway", "bridleway", "footway", "pedestrian"}:
        return 2
    if detail in {"motorway", "trunk", "primary", "secondary", "tertiary"}:
        return 0
    return 1


def merged_lines(lines):
    if not lines:
        return []
    geometry = linemerge(unary_union(lines))
    if isinstance(geometry, LineString):
        return [geometry]
    if isinstance(geometry, MultiLineString):
        return list(geometry.geoms)
    return [item for item in geometry.geoms if isinstance(item, LineString)]


def build(gpx_path, pmtiles_path, output_path, corridor_meters):
    geographic_route = read_gpx(gpx_path)
    lat0, lon0 = geographic_route[0]
    lon_scale = math.cos(math.radians(lat0))
    route = [local_xy(lat, lon, lat0, lon0, lon_scale) for lat, lon in geographic_route]
    corridor = LineString(route).buffer(corridor_meters)
    categorized = {0: [], 1: [], 2: []}

    with open(pmtiles_path, "rb") as source_file:
        source = MmapSource(source_file)
        reader = Reader(source)
        for (zoom, tile_x, tile_y), compressed in all_tiles(source):
            if zoom != 15:
                continue
            tile = decode(gzip.decompress(compressed))
            for feature in tile.get("roads", {}).get("features", []):
                kind = map_kind(feature.get("properties", {}))
                for coordinates in line_parts(feature["geometry"]):
                    points = [
                        tile_xy_to_local(zoom, tile_x, tile_y, x, y,
                                         lat0, lon0, lon_scale)
                        for x, y in coordinates
                    ]
                    if len(points) < 2:
                        continue
                    clipped = LineString(points).intersection(corridor)
                    if clipped.is_empty:
                        continue
                    if isinstance(clipped, LineString):
                        categorized[kind].append(clipped)
                    elif isinstance(clipped, MultiLineString):
                        categorized[kind].extend(clipped.geoms)

    map_points = []
    map_lines = []
    for kind in (0, 1, 2):
        for line in merged_lines(categorized[kind]):
            line = line.simplify(4.0, preserve_topology=False)
            if line.length < 18.0:
                continue
            quantized = []
            for x, y in line.coords:
                point = (round(x), round(y))
                if not quantized or point != quantized[-1]:
                    quantized.append(point)
            if len(quantized) < 2:
                continue
            first = len(map_points)
            map_points.extend(quantized)
            map_lines.append((first, len(quantized), kind))

    route_rows = []
    distance = 0.0
    for index, (x, y) in enumerate(route):
        if index:
            distance += math.hypot(x - route[index - 1][0], y - route[index - 1][1])
        route_rows.append((round(x), round(y), round(distance)))

    def point_rows(points):
        return "\n".join(f"  {{{x}, {y}}}," for x, y in points)

    def line_rows(lines):
        return "\n".join(f"  {{{first}, {count}, {kind}}}," for first, count, kind in lines)

    def route_point_rows(points):
        return "\n".join(f"  {{{x}, {y}, {meters}}}," for x, y, meters in points)

    name = pathlib.Path(gpx_path).stem.replace("_", " ")
    content = f"""// Generated by tools/compile_pq_map.py; do not edit.
#pragma once
#include <cstdint>

struct PqMapPoint {{ int16_t east, north; }};
struct PqMapLine {{ uint16_t first, count; uint8_t kind; }};
struct PqRoutePoint {{ int16_t east, north; uint16_t meters; }};

constexpr char PQ_MAP_ROUTE_NAME[] = \"{name}\";
constexpr double PQ_MAP_LAT0 = {lat0:.7f};
constexpr double PQ_MAP_LON0 = {lon0:.7f};
constexpr PqMapPoint PQ_MAP_POINTS[] = {{
{point_rows(map_points)}
}};
constexpr PqMapLine PQ_MAP_LINES[] = {{
{line_rows(map_lines)}
}};
constexpr PqRoutePoint PQ_ROUTE_POINTS[] = {{
{route_point_rows(route_rows)}
}};
constexpr uint16_t PQ_MAP_LINE_COUNT = sizeof(PQ_MAP_LINES) / sizeof(PQ_MAP_LINES[0]);
constexpr uint16_t PQ_ROUTE_POINT_COUNT = sizeof(PQ_ROUTE_POINTS) / sizeof(PQ_ROUTE_POINTS[0]);
constexpr uint16_t PQ_ROUTE_LENGTH_METERS = PQ_ROUTE_POINTS[PQ_ROUTE_POINT_COUNT - 1].meters;
"""
    pathlib.Path(output_path).write_text(content)
    size = pathlib.Path(output_path).stat().st_size
    print(f"route_points={len(route_rows)} route_miles={distance / 1609.344:.3f}")
    print(f"map_lines={len(map_lines)} map_points={len(map_points)} header_bytes={size}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("gpx")
    parser.add_argument("pmtiles")
    parser.add_argument("output")
    parser.add_argument("--corridor-miles", type=float, default=0.5)
    args = parser.parse_args()
    build(args.gpx, args.pmtiles, args.output, args.corridor_miles * 1609.344)


if __name__ == "__main__":
    main()

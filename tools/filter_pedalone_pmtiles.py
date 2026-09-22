#!/usr/bin/env python3
"""Reduce a Protomaps basemap extract to PedalOne display layers."""

import argparse
import gzip

from mapbox_vector_tile import decode, encode
from pmtiles.reader import MmapSource, Reader, all_tiles
from pmtiles.tile import Compression, zxy_to_tileid
from pmtiles.writer import write


KEEP_PROPERTIES = {
    "roads": ("kind", "kind_detail", "access", "route", "name"),
    "landuse": ("kind",),
    "water": ("kind", "kind_detail"),
}

HIDDEN_ROAD_DETAILS = {"sidewalk", "steps", "crossing", "corridor", "runway", "taxiway"}
VISIBLE_LANDUSE = {
    "bare_rock", "farmland", "forest", "grass", "grassland", "meadow",
    "nature_reserve", "park", "recreation_ground", "sand", "scrub",
    "wetland", "wood",
}
VISIBLE_WATER = {"canal", "lake", "ocean", "river", "stream", "water"}


def reduced_feature(feature, layer_name, zoom):
    allowed = KEEP_PROPERTIES[layer_name]
    properties = {
        key: value
        for key, value in feature.get("properties", {}).items()
        if key in allowed and value not in (None, "", False)
    }
    # Street names are useful at riding scale. Omitting them from overview
    # tiles saves space and prevents the round display from becoming crowded.
    if zoom < 14:
        properties.pop("name", None)
    return {
        "geometry": feature["geometry"],
        "properties": properties,
        "type": feature.get("type"),
    }


def visible(feature, layer_name):
    properties = feature.get("properties", {})
    if layer_name == "roads":
        return (
            properties.get("kind") not in {"aeroway", "rail"}
            and properties.get("kind_detail") not in HIDDEN_ROAD_DETAILS
        )
    if layer_name == "landuse":
        return properties.get("kind") in VISIBLE_LANDUSE
    if layer_name == "water":
        return properties.get("kind") in VISIBLE_WATER
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--minzoom", type=int, default=11)
    args = parser.parse_args()

    with open(args.input, "rb") as source_file:
        source = MmapSource(source_file)
        reader = Reader(source)
        header = reader.header()
        metadata = reader.metadata()
        kept_tiles = 0
        kept_features = {name: 0 for name in KEEP_PROPERTIES}

        with write(args.output) as output:
            for (zoom, x, y), compressed in all_tiles(source):
                if zoom < args.minzoom:
                    continue
                tile = decode(gzip.decompress(compressed))
                layers = []
                for layer_name in KEEP_PROPERTIES:
                    source_layer = tile.get(layer_name)
                    if not source_layer or not source_layer["features"]:
                        continue
                    features = [
                        reduced_feature(feature, layer_name, zoom)
                        for feature in source_layer["features"]
                        if visible(feature, layer_name)
                    ]
                    if not features:
                        continue
                    kept_features[layer_name] += len(features)
                    layers.append({"name": layer_name, "features": features})
                if not layers:
                    continue
                encoded = encode(layers, default_options={"extents": 4096})
                output.write_tile(
                    zxy_to_tileid(zoom, x, y),
                    gzip.compress(encoded, compresslevel=9),
                )
                kept_tiles += 1

            header["tile_compression"] = Compression.GZIP
            header["center_zoom"] = max(args.minzoom, min(14, header["max_zoom"]))
            metadata["name"] = "PedalOne simple map"
            metadata["description"] = "Roads, trails, land use, and water"
            metadata["vector_layers"] = [
                {
                    "id": name,
                    "minzoom": args.minzoom,
                    "maxzoom": header["max_zoom"],
                    "fields": {key: "String" for key in properties},
                }
                for name, properties in KEEP_PROPERTIES.items()
            ]
            output.finalize(header, metadata)

    print(f"tiles={kept_tiles}")
    for name, count in kept_features.items():
        print(f"{name}_features={count}")


if __name__ == "__main__":
    main()

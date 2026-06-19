from __future__ import annotations

import math
import zipfile
from pathlib import Path


SEGMENTS = 96
OUTER_RADIUS_MM = 10.0
INNER_RADIUS_MM = 9.0
TOP_HOLE_RADIUS_MM = 1.0
HEIGHT_MM = 15.0
TOP_CAP_THICKNESS_MM = 1.0
INNER_CAVITY_TOP_Z_MM = HEIGHT_MM - TOP_CAP_THICKNESS_MM
OUTPUT_FILE = "hollow_cylinder_open_bottom_20od_18id_15h_top_hole_2mm.3mf"


def build_ring(radius_mm: float, z_mm: float) -> list[tuple[float, float, float]]:
    return [
        (
            radius_mm * math.cos(2.0 * math.pi * index / SEGMENTS),
            radius_mm * math.sin(2.0 * math.pi * index / SEGMENTS),
            z_mm,
        )
        for index in range(SEGMENTS)
    ]


def add_wall(
    triangles: list[tuple[int, int, int]],
    bottom_ring: list[int],
    top_ring: list[int],
    outward: bool,
) -> None:
    for index in range(SEGMENTS):
        next_index = (index + 1) % SEGMENTS
        if outward:
            triangles.append((bottom_ring[index], bottom_ring[next_index], top_ring[next_index]))
            triangles.append((bottom_ring[index], top_ring[next_index], top_ring[index]))
        else:
            triangles.append((bottom_ring[index], top_ring[next_index], bottom_ring[next_index]))
            triangles.append((bottom_ring[index], top_ring[index], top_ring[next_index]))


def add_annulus(
    triangles: list[tuple[int, int, int]],
    outer_ring: list[int],
    inner_ring: list[int],
    upward: bool,
) -> None:
    for index in range(SEGMENTS):
        next_index = (index + 1) % SEGMENTS
        if upward:
            triangles.append((outer_ring[index], outer_ring[next_index], inner_ring[next_index]))
            triangles.append((outer_ring[index], inner_ring[next_index], inner_ring[index]))
        else:
            triangles.append((outer_ring[index], inner_ring[next_index], outer_ring[next_index]))
            triangles.append((outer_ring[index], inner_ring[index], inner_ring[next_index]))


def model_xml(vertices: list[tuple[float, float, float]], triangles: list[tuple[int, int, int]]) -> str:
    vertices_xml = "\n".join(
        f'          <vertex x="{x:.6f}" y="{y:.6f}" z="{z:.6f}" />'
        for x, y, z in vertices
    )
    triangles_xml = "\n".join(
        f'          <triangle v1="{v1}" v2="{v2}" v3="{v3}" />'
        for v1, v2, v3 in triangles
    )
    return f"""<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<model unit=\"millimeter\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">
  <resources>
    <object id=\"1\" type=\"model\">
      <mesh>
        <vertices>
{vertices_xml}
        </vertices>
        <triangles>
{triangles_xml}
        </triangles>
      </mesh>
    </object>
  </resources>
  <build>
    <item objectid=\"1\" />
  </build>
</model>
"""


def content_types_xml() -> str:
    return """<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">
  <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\" />
  <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\" />
</Types>
"""


def package_relationships_xml() -> str:
    return """<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">
  <Relationship Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\" Target=\"/3D/3dmodel.model\" />
</Relationships>
"""


def build_geometry() -> tuple[list[tuple[float, float, float]], list[tuple[int, int, int]]]:
    vertices: list[tuple[float, float, float]] = []

    def add_ring(radius_mm: float, z_mm: float) -> list[int]:
        ring: list[int] = []
        for vertex in build_ring(radius_mm, z_mm):
            ring.append(len(vertices))
            vertices.append(vertex)
        return ring

    outer_bottom = add_ring(OUTER_RADIUS_MM, 0.0)
    outer_top = add_ring(OUTER_RADIUS_MM, HEIGHT_MM)
    inner_bottom = add_ring(INNER_RADIUS_MM, 0.0)
    inner_top = add_ring(INNER_RADIUS_MM, INNER_CAVITY_TOP_Z_MM)
    hole_bottom = add_ring(TOP_HOLE_RADIUS_MM, INNER_CAVITY_TOP_Z_MM)
    hole_top = add_ring(TOP_HOLE_RADIUS_MM, HEIGHT_MM)

    triangles: list[tuple[int, int, int]] = []
    add_wall(triangles, outer_bottom, outer_top, outward=True)
    add_wall(triangles, inner_bottom, inner_top, outward=False)
    add_wall(triangles, hole_bottom, hole_top, outward=False)
    add_annulus(triangles, outer_bottom, inner_bottom, upward=False)
    add_annulus(triangles, outer_top, hole_top, upward=True)
    add_annulus(triangles, inner_top, hole_bottom, upward=False)

    return vertices, triangles


def main() -> None:
    vertices, triangles = build_geometry()
    output_path = Path(__file__).with_name(OUTPUT_FILE)

    with zipfile.ZipFile(output_path, mode="w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("[Content_Types].xml", content_types_xml())
        archive.writestr("_rels/.rels", package_relationships_xml())
        archive.writestr("3D/3dmodel.model", model_xml(vertices, triangles))

    print(output_path)
    print(f"vertices={len(vertices)} triangles={len(triangles)}")


if __name__ == "__main__":
    main()
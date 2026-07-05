from __future__ import annotations

from pathlib import Path

import numpy as np
import trimesh

import generate_pen_holder_ra2026 as ra_logo

SOURCE_BOX_FILE = "hollow_rectangular_box_100x80x80mm_open_bottom.3mf"
OUTPUT_BOX_FILE = SOURCE_BOX_FILE
TARGET_FACE_WIDTH_MM = 76.0
TEXT_DEPTH_MM = 1.8


def load_box_mesh() -> trimesh.Trimesh:
    mesh = trimesh.load(Path(__file__).with_name(SOURCE_BOX_FILE), force="mesh")
    if not isinstance(mesh, trimesh.Trimesh) or not mesh.is_watertight:
        raise ValueError("Failed to load a watertight rectangular box mesh.")
    return mesh


def choose_target_facet(mesh: trimesh.Trimesh) -> int:
    candidates: list[tuple[float, float, int]] = []
    for facet_index, facet in enumerate(mesh.facets):
        normal = mesh.facets_normal[facet_index]
        if abs(normal[2]) > 1e-6:
            continue

        area = float(mesh.area_faces[facet].sum())
        centroid = mesh.triangles_center[facet].mean(axis=0)
        outward_score = float(centroid[0])
        candidates.append((area, outward_score, facet_index))

    if not candidates:
        raise ValueError("No vertical exterior facet was found on the box mesh.")

    candidates.sort(reverse=True)
    return candidates[0][2]


def build_logo_polygon() -> tuple[object, tuple[float, float]]:
    mask = ra_logo.build_logo_mask_from_image()
    polygon = ra_logo.mask_to_polygon(mask)
    polygon = ra_logo.shapely.affinity.scale(
        polygon,
        xfact=1.0 / ra_logo.PIXELS_PER_MM,
        yfact=1.0 / ra_logo.PIXELS_PER_MM,
        origin=(0.0, 0.0),
    )

    min_x, min_y, max_x, max_y = polygon.bounds
    width = max_x - min_x
    scale_factor = TARGET_FACE_WIDTH_MM / width
    polygon = ra_logo.shapely.affinity.scale(
        polygon,
        xfact=scale_factor,
        yfact=scale_factor,
        origin=(0.0, 0.0),
    )
    min_x, min_y, max_x, max_y = polygon.bounds
    polygon = ra_logo.shapely.affinity.translate(polygon, xoff=-min_x, yoff=-min_y)
    return polygon, (max_x - min_x, max_y - min_y)


def wrap_logo_to_facet(
    logo_mesh: trimesh.Trimesh,
    mesh: trimesh.Trimesh,
    facet_index: int,
    logo_size_mm: tuple[float, float],
) -> trimesh.Trimesh:
    facet = mesh.facets[facet_index]
    normal = mesh.facets_normal[facet_index]
    facet_vertices = np.unique(mesh.faces[facet].reshape(-1))
    face_points = mesh.vertices[facet_vertices]
    plane_origin = face_points[0]

    vertical_axis = np.array([0.0, 0.0, 1.0])
    horizontal_axis = np.cross(vertical_axis, normal)
    horizontal_axis /= np.linalg.norm(horizontal_axis)

    relative_points = face_points - plane_origin
    u_coords = relative_points @ horizontal_axis
    v_coords = relative_points @ vertical_axis
    min_u = float(u_coords.min())
    max_u = float(u_coords.max())
    min_v = float(v_coords.min())
    max_v = float(v_coords.max())

    logo_width_mm, logo_height_mm = logo_size_mm
    origin = (
        plane_origin
        + horizontal_axis * ((min_u + max_u - logo_width_mm) / 2.0)
        + vertical_axis * ((min_v + max_v - logo_height_mm) / 2.0)
        + normal * 0.001
    )

    flat_vertices = logo_mesh.vertices.copy()
    wrapped_vertices = np.zeros_like(flat_vertices)
    for index, (flat_x, flat_y, depth) in enumerate(flat_vertices):
        wrapped_vertices[index] = (
            origin
            + (horizontal_axis * flat_x)
            + (vertical_axis * flat_y)
            + (normal * depth)
        )

    wrapped = trimesh.Trimesh(
        vertices=wrapped_vertices,
        faces=logo_mesh.faces.copy(),
        process=False,
    )
    wrapped.merge_vertices()
    wrapped.remove_unreferenced_vertices()
    return wrapped


def build_logo_meshes(mesh: trimesh.Trimesh) -> list[trimesh.Trimesh]:
    polygon, logo_size_mm = build_logo_polygon()
    facet_index = choose_target_facet(mesh)
    polygon_parts = list(getattr(polygon, "geoms", [polygon]))
    meshes: list[trimesh.Trimesh] = []

    for part in polygon_parts:
        extrusion_parts = [part]
        if isinstance(part, ra_logo.shapely.geometry.Polygon) and part.interiors:
            extrusion_parts = ra_logo.split_polygon_for_extrusion(part)

        for extrusion_part in extrusion_parts:
            flat_mesh = trimesh.creation.extrude_polygon(
                extrusion_part,
                height=TEXT_DEPTH_MM,
                engine="triangle",
            )
            meshes.append(wrap_logo_to_facet(flat_mesh, mesh, facet_index, logo_size_mm))

    return meshes


def main() -> None:
    box_mesh = load_box_mesh()
    logo_meshes = build_logo_meshes(box_mesh)
    final_mesh = trimesh.boolean.union([box_mesh, *logo_meshes], engine="manifold")
    if final_mesh is None or not final_mesh.is_watertight:
        raise ValueError("Failed to create a watertight union for the box logo.")

    output_path = Path(__file__).with_name(OUTPUT_BOX_FILE)
    final_mesh.export(output_path)

    print(output_path)
    print(f"vertices={len(final_mesh.vertices)} faces={len(final_mesh.faces)}")
    print(f"bounds={final_mesh.bounds.tolist()}")


if __name__ == "__main__":
    main()
from __future__ import annotations

from pathlib import Path

import numpy as np
import shapely.affinity
import shapely.geometry
import shapely.ops
import trimesh
from PIL import Image, ImageDraw, ImageFont

OUTER_DIAMETER_MM = 100.0
OUTER_RADIUS_MM = OUTER_DIAMETER_MM / 2.0
HEIGHT_MM = 120.0
WALL_THICKNESS_MM = 3.5
BOTTOM_THICKNESS_MM = WALL_THICKNESS_MM
INNER_RADIUS_MM = OUTER_RADIUS_MM - WALL_THICKNESS_MM
TEXT = "RA2026"
TEXT_HEIGHT_MM = 14.0
TEXT_DEPTH_MM = 1.8
TEXT_CENTER_Z_MM = HEIGHT_MM / 2.0
CYLINDER_SECTIONS = 192
RIM_WIRE_SEGMENTS = 32
RIM_WIRE_RADIUS_MM = 2.6
RIM_WIRE_OUTER_OVERHANG_MM = 1.2
PIXELS_PER_MM = 12
OUTPUT_FILE = "pen_holder_ra2026_100od_120h_3p5wall.3mf"
FONT_CANDIDATES = [
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/System/Library/Fonts/Supplemental/Verdana Bold.ttf",
    "/System/Library/Fonts/Supplemental/Tahoma Bold.ttf",
]


def find_font_path() -> Path:
    for candidate in FONT_CANDIDATES:
        path = Path(candidate)
        if path.exists():
            return path
    raise FileNotFoundError("No supported bold font was found on this system.")


def render_text_mask(text: str, font_path: Path) -> np.ndarray:
    font_px = int(TEXT_HEIGHT_MM * PIXELS_PER_MM * 1.9)
    font = ImageFont.truetype(str(font_path), size=font_px)
    dummy = Image.new("L", (1, 1), 0)
    dummy_draw = ImageDraw.Draw(dummy)
    left, top, right, bottom = dummy_draw.textbbox((0, 0), text, font=font)
    padding = int(font_px * 0.2)
    image = Image.new("L", (right - left + 2 * padding, bottom - top + 2 * padding), 0)
    draw = ImageDraw.Draw(image)
    draw.text((padding - left, padding - top), text, font=font, fill=255)

    bbox = image.getbbox()
    if bbox is None:
        raise ValueError("Rendered text is empty.")

    cropped = image.crop(bbox)
    return np.array(cropped) > 0


def mask_to_polygon(mask: np.ndarray) -> shapely.geometry.base.BaseGeometry:
    rectangles: list[shapely.geometry.Polygon] = []
    height, width = mask.shape
    for row_index in range(height):
        row = mask[row_index]
        start_index: int | None = None
        for column_index in range(width):
            filled = bool(row[column_index])
            if filled and start_index is None:
                start_index = column_index
            elif not filled and start_index is not None:
                rectangles.append(
                    shapely.geometry.box(
                        start_index,
                        height - row_index - 1,
                        column_index,
                        height - row_index,
                    )
                )
                start_index = None
        if start_index is not None:
            rectangles.append(
                shapely.geometry.box(
                    start_index, height - row_index - 1, width, height - row_index
                )
            )

    if not rectangles:
        raise ValueError("No foreground pixels were found in the rendered text.")

    return shapely.ops.unary_union(rectangles)


def build_text_polygon(text: str) -> shapely.geometry.base.BaseGeometry:
    mask = render_text_mask(text, find_font_path())
    polygon = mask_to_polygon(mask)
    min_x, min_y, max_x, max_y = polygon.bounds
    text_height_units = max_y - min_y
    scale_factor = TEXT_HEIGHT_MM / text_height_units
    polygon = shapely.affinity.scale(
        polygon, xfact=scale_factor, yfact=scale_factor, origin=(0.0, 0.0)
    )
    min_x, min_y, max_x, max_y = polygon.bounds
    polygon = shapely.affinity.translate(
        polygon,
        xoff=-((min_x + max_x) / 2.0),
        yoff=TEXT_CENTER_Z_MM - ((min_y + max_y) / 2.0),
    )
    return polygon


def build_holder_mesh() -> trimesh.Trimesh:
    rim_center_radius_mm = (
        OUTER_RADIUS_MM + RIM_WIRE_OUTER_OVERHANG_MM - RIM_WIRE_RADIUS_MM
    )
    rim_center_z_mm = HEIGHT_MM - RIM_WIRE_RADIUS_MM

    outer_dx_mm = OUTER_RADIUS_MM - rim_center_radius_mm
    inner_dx_mm = INNER_RADIUS_MM - rim_center_radius_mm
    outer_dy_mm = np.sqrt((RIM_WIRE_RADIUS_MM**2) - (outer_dx_mm**2))
    inner_dy_mm = np.sqrt((RIM_WIRE_RADIUS_MM**2) - (inner_dx_mm**2))

    outer_join = (OUTER_RADIUS_MM, rim_center_z_mm - outer_dy_mm)
    inner_join = (INNER_RADIUS_MM, rim_center_z_mm - inner_dy_mm)

    outer_theta = np.arctan2(outer_join[1] - rim_center_z_mm, outer_dx_mm)
    inner_theta = np.arctan2(inner_join[1] - rim_center_z_mm, inner_dx_mm)
    if inner_theta <= outer_theta:
        inner_theta += 2.0 * np.pi

    rim_thetas = np.linspace(outer_theta, inner_theta, RIM_WIRE_SEGMENTS + 1)
    rim_curve = np.column_stack(
        [
            rim_center_radius_mm + (RIM_WIRE_RADIUS_MM * np.cos(rim_thetas)),
            rim_center_z_mm + (RIM_WIRE_RADIUS_MM * np.sin(rim_thetas)),
        ]
    )

    profile = np.vstack(
        [
            [0.0, 0.0],
            [OUTER_RADIUS_MM, 0.0],
            [OUTER_RADIUS_MM, outer_join[1]],
            rim_curve[1:],
            [INNER_RADIUS_MM, BOTTOM_THICKNESS_MM],
            [0.0, BOTTOM_THICKNESS_MM],
            [0.0, 0.0],
        ]
    )

    holder = trimesh.creation.revolve(profile, sections=CYLINDER_SECTIONS)
    if holder is None or not holder.is_watertight:
        raise ValueError("Failed to build a watertight pen holder mesh.")
    return holder


def wrap_text_mesh_to_cylinder(text_mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    vertices = text_mesh.vertices.copy()
    wrapped_vertices = np.zeros_like(vertices)

    for index, (flat_x, vertical_z, depth) in enumerate(vertices):
        angle = flat_x / OUTER_RADIUS_MM
        radius = OUTER_RADIUS_MM + depth
        wrapped_vertices[index] = [
            radius * np.cos(angle),
            radius * np.sin(angle),
            vertical_z,
        ]

    wrapped = trimesh.Trimesh(
        vertices=wrapped_vertices, faces=text_mesh.faces.copy(), process=False
    )
    wrapped.merge_vertices()
    wrapped.remove_unreferenced_vertices()
    return wrapped


def build_text_mesh() -> trimesh.Trimesh:
    polygon = build_text_polygon(TEXT)
    polygon_parts = list(getattr(polygon, "geoms", [polygon]))
    flat_mesh = trimesh.util.concatenate(
        [
            trimesh.creation.extrude_polygon(
                part,
                height=TEXT_DEPTH_MM,
                engine="triangle",
            )
            for part in polygon_parts
        ]
    )
    wrapped_mesh = wrap_text_mesh_to_cylinder(flat_mesh)
    if not wrapped_mesh.is_watertight:
        raise ValueError("Wrapped text mesh is not watertight.")
    return wrapped_mesh


def main() -> None:
    holder_mesh = build_holder_mesh()
    text_mesh = build_text_mesh()
    final_mesh = trimesh.boolean.union([holder_mesh, text_mesh], engine="manifold")
    if final_mesh is None or not final_mesh.is_watertight:
        raise ValueError("Failed to create a watertight union for the pen holder.")

    output_path = Path(__file__).with_name(OUTPUT_FILE)
    final_mesh.export(output_path)

    print(output_path)
    print(f"vertices={len(final_mesh.vertices)} faces={len(final_mesh.faces)}")
    print(f"bounds={final_mesh.bounds.tolist()}")


if __name__ == "__main__":
    main()

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
TEXT_DEPTH_MM = 1.8
LAYOUT_CENTER_Z_MM = HEIGHT_MM / 2.0
CYLINDER_SECTIONS = 192
RIM_WIRE_SEGMENTS = 32
RIM_WIRE_RADIUS_MM = 2.6
RIM_WIRE_OUTER_OVERHANG_MM = 1.2
PIXELS_PER_MM = 6
TOP_ROW_ICON_HEIGHT_MM = 17.0
TOP_ROW_TEXT_HEIGHT_MM = 12.5
WORDMARK_LINE_HEIGHT_MM = 9.8
ROW_GAP_MM = 2.4
LINE_GAP_MM = 1.6
COLUMN_GAP_MM = 3.0
OUTPUT_FILE = "pen_holder_ra2026_rockwell_automation_100od_120h_3p5wall.3mf"
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


def mm_to_px(length_mm: float) -> int:
    return max(1, int(round(length_mm * PIXELS_PER_MM)))


def trim_mask(mask: np.ndarray) -> np.ndarray:
    ys, xs = np.where(mask)
    if len(xs) == 0 or len(ys) == 0:
        raise ValueError("Mask is empty.")
    return mask[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1]


def resize_mask(mask: np.ndarray, target_height_px: int) -> np.ndarray:
    source = Image.fromarray(mask.astype(np.uint8) * 255, mode="L")
    scale = target_height_px / source.height
    target_width_px = max(1, int(round(source.width * scale)))
    resized = source.resize(
        (target_width_px, target_height_px), Image.Resampling.NEAREST
    )
    return np.array(resized) > 0


def render_text_mask(text: str, font_path: Path, target_height_mm: float) -> np.ndarray:
    font_px = int(target_height_mm * PIXELS_PER_MM * 1.9)
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
    return resize_mask(np.array(cropped) > 0, mm_to_px(target_height_mm))


def paste_mask(canvas: np.ndarray, mask: np.ndarray, top: int, left: int) -> None:
    height, width = mask.shape
    canvas[top : top + height, left : left + width] |= mask


def build_ra_icon_mask(font_path: Path) -> np.ndarray:
    width_px = 520
    height_px = 520
    image = Image.new("L", (width_px, height_px), 0)
    draw = ImageDraw.Draw(image)

    octagon = [
        (0.24 * width_px, 0.0 * height_px),
        (0.76 * width_px, 0.0 * height_px),
        (1.0 * width_px, 0.24 * height_px),
        (1.0 * width_px, 0.76 * height_px),
        (0.76 * width_px, 1.0 * height_px),
        (0.24 * width_px, 1.0 * height_px),
        (0.0 * width_px, 0.76 * height_px),
        (0.0 * width_px, 0.24 * height_px),
    ]
    draw.polygon(octagon, fill=255)

    def carve_mask(mask: np.ndarray, left: int, top: int) -> None:
        glyph = Image.fromarray(mask.astype(np.uint8) * 255, mode="L")
        image.paste(0, (left, top), glyph)

    draw.polygon(
        [
            (0.46 * width_px, 0.08 * height_px),
            (0.84 * width_px, 0.08 * height_px),
            (0.72 * width_px, 0.22 * height_px),
            (0.34 * width_px, 0.22 * height_px),
        ],
        fill=0,
    )
    draw.polygon(
        [
            (0.10 * width_px, 0.70 * height_px),
            (0.52 * width_px, 0.70 * height_px),
            (0.70 * width_px, 0.86 * height_px),
            (0.28 * width_px, 0.86 * height_px),
        ],
        fill=0,
    )

    letter_ra_mask = render_text_mask("RA", font_path, 21.5)
    letter_ra_image = Image.fromarray(letter_ra_mask.astype(np.uint8) * 255, mode="L")
    letter_ra_image = letter_ra_image.resize(
        (int(letter_ra_image.width * 1.62), int(letter_ra_image.height * 1.46)),
        Image.Resampling.NEAREST,
    )
    letter_ra_left = int((0.49 * width_px) - (letter_ra_image.width / 2.0))
    letter_ra_top = int((0.43 * height_px) - (letter_ra_image.height / 2.0))
    carve_mask(
        np.array(letter_ra_image) > 0,
        letter_ra_left,
        letter_ra_top,
    )
    return trim_mask(np.array(image) > 0)


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


def build_layout_mask() -> np.ndarray:
    font_path = find_font_path()
    icon_mask = resize_mask(
        build_ra_icon_mask(font_path), mm_to_px(TOP_ROW_ICON_HEIGHT_MM)
    )
    year_mask = render_text_mask("2026", font_path, TOP_ROW_TEXT_HEIGHT_MM)
    rockwell_mask = render_text_mask("Rockwell", font_path, WORDMARK_LINE_HEIGHT_MM)
    automation_mask = render_text_mask("Automation", font_path, WORDMARK_LINE_HEIGHT_MM)

    column_gap_px = mm_to_px(COLUMN_GAP_MM)
    row_gap_px = mm_to_px(ROW_GAP_MM)
    line_gap_px = mm_to_px(LINE_GAP_MM)
    top_row_height = max(icon_mask.shape[0], year_mask.shape[0])
    top_row_width = icon_mask.shape[1] + column_gap_px + year_mask.shape[1]
    wordmark_width = max(rockwell_mask.shape[1], automation_mask.shape[1])
    wordmark_height = rockwell_mask.shape[0] + line_gap_px + automation_mask.shape[0]
    layout_width = max(top_row_width, wordmark_width)
    layout_height = top_row_height + row_gap_px + wordmark_height

    canvas = np.zeros((layout_height, layout_width), dtype=bool)
    top_row_left = (layout_width - top_row_width) // 2
    icon_top = (top_row_height - icon_mask.shape[0]) // 2
    year_top = (top_row_height - year_mask.shape[0]) // 2
    paste_mask(canvas, icon_mask, icon_top, top_row_left)
    paste_mask(
        canvas,
        year_mask,
        year_top,
        top_row_left + icon_mask.shape[1] + column_gap_px,
    )

    wordmark_top = top_row_height + row_gap_px
    rockwell_left = (layout_width - rockwell_mask.shape[1]) // 2
    automation_left = (layout_width - automation_mask.shape[1]) // 2
    paste_mask(canvas, rockwell_mask, wordmark_top, rockwell_left)
    paste_mask(
        canvas,
        automation_mask,
        wordmark_top + rockwell_mask.shape[0] + line_gap_px,
        automation_left,
    )
    return canvas


def build_text_polygon() -> shapely.geometry.base.BaseGeometry:
    mask = build_layout_mask()
    polygon = mask_to_polygon(mask)
    polygon = shapely.affinity.scale(
        polygon,
        xfact=1.0 / PIXELS_PER_MM,
        yfact=1.0 / PIXELS_PER_MM,
        origin=(0.0, 0.0),
    )
    min_x, min_y, max_x, max_y = polygon.bounds
    polygon = shapely.affinity.translate(
        polygon,
        xoff=-((min_x + max_x) / 2.0),
        yoff=LAYOUT_CENTER_Z_MM - ((min_y + max_y) / 2.0),
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


def split_polygon_for_extrusion(
    polygon: shapely.geometry.Polygon,
) -> list[shapely.geometry.Polygon]:
    pieces: list[shapely.geometry.Polygon] = [polygon]
    if not polygon.interiors:
        return pieces

    min_x, min_y, max_x, max_y = polygon.bounds
    for interior in polygon.interiors:
        centroid_x = shapely.geometry.Polygon(interior).centroid.x
        cutter = shapely.geometry.LineString(
            [(centroid_x, min_y - 1.0), (centroid_x, max_y + 1.0)]
        )
        next_pieces: list[shapely.geometry.Polygon] = []
        for piece in pieces:
            split_result = shapely.ops.split(piece, cutter)
            for geom in getattr(split_result, "geoms", [split_result]):
                if isinstance(geom, shapely.geometry.Polygon) and geom.area > 0.01:
                    next_pieces.append(geom)
        pieces = next_pieces or pieces

    return pieces


def build_text_meshes() -> list[trimesh.Trimesh]:
    polygon = build_text_polygon()
    polygon_parts = list(getattr(polygon, "geoms", [polygon]))
    wrapped_meshes: list[trimesh.Trimesh] = []
    for part in polygon_parts:
        extrusion_parts = [part]
        if isinstance(part, shapely.geometry.Polygon) and part.interiors:
            extrusion_parts = split_polygon_for_extrusion(part)

        for extrusion_part in extrusion_parts:
            flat_mesh = trimesh.creation.extrude_polygon(
                extrusion_part,
                height=TEXT_DEPTH_MM,
                engine="triangle",
            )
            wrapped_mesh = wrap_text_mesh_to_cylinder(flat_mesh)
            if not wrapped_mesh.is_watertight:
                raise ValueError("Wrapped text mesh is not watertight.")
            wrapped_meshes.append(wrapped_mesh)
    return wrapped_meshes


def main() -> None:
    holder_mesh = build_holder_mesh()
    text_meshes = build_text_meshes()
    final_mesh = trimesh.boolean.union([holder_mesh, *text_meshes], engine="manifold")
    if final_mesh is None or not final_mesh.is_watertight:
        raise ValueError("Failed to create a watertight union for the pen holder.")

    output_path = Path(__file__).with_name(OUTPUT_FILE)
    final_mesh.export(output_path)

    print(output_path)
    print(f"vertices={len(final_mesh.vertices)} faces={len(final_mesh.faces)}")
    print(f"bounds={final_mesh.bounds.tolist()}")


if __name__ == "__main__":
    main()

from __future__ import annotations

from pathlib import Path
import sys

import numpy as np
import trimesh
from scipy import ndimage

SOURCE_FILE = "库里南.3mf"
OUTPUT_FILE = "kulinanF.3mf"
TARGET_WIDTH_MM = 90.0
TARGET_LENGTH_MM = 160.0
TARGET_HEIGHT_MM = 90.0
WALL_THICKNESS_MM = 2.0
VOXEL_PITCH_MM = 0.5
BOTTOM_AREA_THRESHOLD = 0.32
BOTTOM_CLEARANCE_MM = 2.0
OPENING_COMPONENT_LAYERS = 3


def select_primary_body(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    components = mesh.split(only_watertight=False)
    if len(components) == 0:
        raise ValueError("The source model has no mesh components.")
    return max(components, key=lambda item: len(item.faces)).copy()


def scale_mesh_to_target(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    scaled = mesh.copy()
    current_width_mm, current_length_mm, current_height_mm = scaled.extents
    scale = np.array(
        [
            TARGET_WIDTH_MM / current_width_mm,
            TARGET_LENGTH_MM / current_length_mm,
            TARGET_HEIGHT_MM / current_height_mm,
        ],
        dtype=float,
    )
    transform = np.eye(4)
    transform[:3, :3] = np.diag(scale)
    scaled.apply_transform(transform)
    return scaled


def detect_bottom_cut_index(occupied: np.ndarray) -> int:
    layer_areas = occupied.sum(axis=(0, 1))
    nonzero_layers = np.flatnonzero(layer_areas)
    if len(nonzero_layers) == 0:
        raise ValueError("The voxelized model is empty.")

    threshold = layer_areas.max() * BOTTOM_AREA_THRESHOLD
    first_body_layer = next(
        (int(index) for index in nonzero_layers if layer_areas[index] >= threshold),
        int(nonzero_layers[0]),
    )
    clearance_layers = int(np.ceil(BOTTOM_CLEARANCE_MM / VOXEL_PITCH_MM))
    cut_index = max(int(nonzero_layers[0]) + 1, first_body_layer - clearance_layers)
    return min(cut_index, int(nonzero_layers[-1]))


def detect_opening_top_index(occupied: np.ndarray) -> int:
    layer_areas = occupied.sum(axis=(0, 1))
    nonzero_layers = np.flatnonzero(layer_areas)
    if len(nonzero_layers) == 0:
        raise ValueError("The voxelized model is empty.")

    consecutive_layers = 0

    for layer_index in nonzero_layers:
        if layer_areas[layer_index] == 0:
            consecutive_layers = 0
            continue

        component_count = ndimage.label(occupied[:, :, layer_index])[1]
        if component_count == 1:
            consecutive_layers += 1
            if consecutive_layers >= OPENING_COMPONENT_LAYERS:
                return int(layer_index - OPENING_COMPONENT_LAYERS + 1)
        else:
            consecutive_layers = 0

    return int(nonzero_layers[-1])


def detect_bottom_opening_box(
    occupied: np.ndarray,
    bottom_cut_index: int,
    opening_top_index: int,
    shell_layers: int,
) -> tuple[int, int, int, int, int]:
    protected_layers = min(occupied.shape[2], opening_top_index + shell_layers + 1)
    opening_x_min: int | None = None
    opening_x_max: int | None = None
    opening_y_min: int | None = None
    opening_y_max: int | None = None

    for layer_index in range(bottom_cut_index, protected_layers):
        layer = occupied[:, :, layer_index]
        if not layer.any():
            continue

        x_indices, y_indices = np.nonzero(layer)
        layer_x_min = int(x_indices.min()) + shell_layers
        layer_x_max = int(x_indices.max()) - shell_layers
        layer_y_min = int(y_indices.min()) + shell_layers
        layer_y_max = int(y_indices.max()) - shell_layers

        if layer_x_min >= layer_x_max or layer_y_min >= layer_y_max:
            continue

        if opening_x_min is None:
            opening_x_min = layer_x_min
            opening_x_max = layer_x_max
            opening_y_min = layer_y_min
            opening_y_max = layer_y_max
            continue

        opening_x_min = max(opening_x_min, layer_x_min)
        opening_x_max = min(opening_x_max, layer_x_max)
        opening_y_min = max(opening_y_min, layer_y_min)
        opening_y_max = min(opening_y_max, layer_y_max)

    if (
        opening_x_min is None
        or opening_x_max is None
        or opening_y_min is None
        or opening_y_max is None
        or opening_x_min >= opening_x_max
        or opening_y_min >= opening_y_max
    ):
        raise ValueError("Unable to determine a continuous bottom opening box.")

    return (
        opening_x_min,
        opening_x_max,
        opening_y_min,
        opening_y_max,
        protected_layers,
    )


def clear_bottom_obstructions(
    occupied: np.ndarray,
    bottom_cut_index: int,
    opening_top_index: int,
    shell_layers: int,
) -> np.ndarray:
    cleared = occupied.copy()
    protected_layers = min(cleared.shape[2], opening_top_index + shell_layers + 1)
    opening_x_min: int | None = None
    opening_x_max: int | None = None
    opening_y_min: int | None = None
    opening_y_max: int | None = None

    for layer_index in range(bottom_cut_index, protected_layers):
        layer = cleared[:, :, layer_index]
        if not layer.any():
            continue

        x_indices, y_indices = np.nonzero(layer)
        layer_x_min = int(x_indices.min()) + shell_layers
        layer_x_max = int(x_indices.max()) - shell_layers
        layer_y_min = int(y_indices.min()) + shell_layers
        layer_y_max = int(y_indices.max()) - shell_layers

        if layer_x_min >= layer_x_max or layer_y_min >= layer_y_max:
            continue

        if opening_x_min is None:
            opening_x_min = layer_x_min
            opening_x_max = layer_x_max
            opening_y_min = layer_y_min
            opening_y_max = layer_y_max
            continue

        opening_x_min = max(opening_x_min, layer_x_min)
        opening_x_max = min(opening_x_max, layer_x_max)
        opening_y_min = max(opening_y_min, layer_y_min)
        opening_y_max = min(opening_y_max, layer_y_max)

    if (
        opening_x_min is not None
        and opening_x_max is not None
        and opening_y_min is not None
        and opening_y_max is not None
        and opening_x_min < opening_x_max
        and opening_y_min < opening_y_max
    ):
        cleared[
            opening_x_min : opening_x_max + 1,
            opening_y_min : opening_y_max + 1,
            bottom_cut_index:protected_layers,
        ] = False

    return cleared


def build_open_shell(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    voxelized = mesh.voxelized(pitch=VOXEL_PITCH_MM).fill()
    occupied = voxelized.matrix.copy()

    bottom_cut_index = detect_bottom_cut_index(occupied)
    opening_top_index = detect_opening_top_index(occupied)
    opening_box = detect_bottom_opening_box(
        occupied,
        bottom_cut_index,
        opening_top_index,
        max(1, int(round(WALL_THICKNESS_MM / VOXEL_PITCH_MM))),
    )

    shell_layers = max(1, int(round(WALL_THICKNESS_MM / VOXEL_PITCH_MM)))
    eroded = ndimage.binary_erosion(
        occupied,
        structure=ndimage.generate_binary_structure(rank=3, connectivity=1),
        iterations=shell_layers,
        border_value=0,
    )
    shell = occupied & ~eroded

    shell_grid = trimesh.voxel.VoxelGrid(shell, transform=voxelized.transform)
    rebuilt = shell_grid.marching_cubes
    rebuilt.remove_unreferenced_vertices()
    rebuilt.merge_vertices()
    rebuilt.process(validate=True)
    rebuilt = select_primary_body(rebuilt)
    rebuilt.process(validate=True)

    opening_x_min, opening_x_max, opening_y_min, opening_y_max, opening_z_max = (
        opening_box
    )
    min_corner = np.array(
        [opening_x_min, opening_y_min, 0],
        dtype=float,
    )
    max_corner = np.array(
        [opening_x_max + 1, opening_y_max + 1, opening_z_max],
        dtype=float,
    )
    cutter_extents = max_corner - min_corner
    cutter_center = (min_corner + max_corner) / 2.0
    cutter_transform = np.eye(4)
    cutter_transform[:3, 3] = cutter_center
    cutter = trimesh.creation.box(extents=cutter_extents, transform=cutter_transform)

    opened = trimesh.boolean.difference([rebuilt, cutter], engine="manifold")
    if opened is None:
        raise ValueError("Boolean opening cut failed.")
    if isinstance(opened, list):
        opened = trimesh.util.concatenate(opened)

    opened.remove_unreferenced_vertices()
    opened.merge_vertices()
    opened.process(validate=True)
    return opened


def remove_bottom_cap(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    opened = mesh.copy()
    triangles = opened.triangles
    min_z = float(opened.bounds[0][2])
    bottom_faces = np.isclose(triangles[:, :, 2], min_z, atol=1e-6).all(axis=1)

    if not bottom_faces.any():
        return opened

    opened.update_faces(~bottom_faces)
    opened.remove_unreferenced_vertices()
    opened.merge_vertices()
    opened.process(validate=True)
    return opened


def fit_final_extents(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    fitted = mesh.copy()
    current_width_mm, current_length_mm, current_height_mm = fitted.extents
    scale = np.array(
        [
            TARGET_WIDTH_MM / current_width_mm,
            TARGET_LENGTH_MM / current_length_mm,
            TARGET_HEIGHT_MM / current_height_mm,
        ],
        dtype=float,
    )
    transform = np.eye(4)
    transform[:3, :3] = np.diag(scale)
    fitted.apply_transform(transform)
    fitted.apply_translation(-fitted.bounds[0])
    return fitted


def combine_components(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    components = mesh.split(only_watertight=False)
    if len(components) <= 1:
        combined = mesh.copy()
    else:
        combined = trimesh.util.concatenate(
            [component.copy() for component in components]
        )

    combined.remove_unreferenced_vertices()
    combined.merge_vertices()
    combined.process(validate=True)
    return combined


def export_model(mesh: trimesh.Trimesh, output_path: Path) -> None:
    exported = trimesh.exchange.threemf.export_3MF(mesh=mesh)
    temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    temp_path.write_bytes(exported)
    temp_path.replace(output_path)


def main() -> None:
    source_name = sys.argv[1] if len(sys.argv) > 1 else SOURCE_FILE
    output_name = sys.argv[2] if len(sys.argv) > 2 else OUTPUT_FILE

    source_path = Path(__file__).with_name(source_name)
    output_path = Path(__file__).with_name(output_name)

    mesh = trimesh.load(source_path, force="mesh")
    primary_body = select_primary_body(mesh)
    scaled = scale_mesh_to_target(primary_body)
    rebuilt = build_open_shell(scaled)
    fitted = fit_final_extents(rebuilt)
    cleaned = combine_components(fitted)
    export_model(cleaned, output_path)

    print(output_path)
    print("extents_mm", cleaned.extents.tolist())
    print("bounds_mm", cleaned.bounds.tolist())
    print("watertight", cleaned.is_watertight)


if __name__ == "__main__":
    main()

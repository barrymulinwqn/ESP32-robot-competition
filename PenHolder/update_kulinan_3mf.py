from __future__ import annotations

from pathlib import Path

import numpy as np
import trimesh
from scipy import ndimage

SOURCE_FILE = "库里南.3mf"
OUTPUT_FILE = SOURCE_FILE
TARGET_WIDTH_MM = 90.0
TARGET_LENGTH_MM = 160.0
TARGET_HEIGHT_MM = 90.0
WALL_THICKNESS_MM = 2.0
VOXEL_PITCH_MM = 1.0
BOTTOM_AREA_THRESHOLD = 0.32
BOTTOM_CLEARANCE_MM = 2.0


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


def build_open_shell(mesh: trimesh.Trimesh) -> trimesh.Trimesh:
    voxelized = mesh.voxelized(pitch=VOXEL_PITCH_MM).fill()
    occupied = voxelized.matrix.copy()

    bottom_cut_index = detect_bottom_cut_index(occupied)
    occupied[:, :, :bottom_cut_index] = False

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
    return rebuilt


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


def export_model(mesh: trimesh.Trimesh, output_path: Path) -> None:
    exported = trimesh.exchange.threemf.export_3MF(mesh=mesh)
    temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    temp_path.write_bytes(exported)
    temp_path.replace(output_path)


def main() -> None:
    source_path = Path(__file__).with_name(SOURCE_FILE)
    output_path = Path(__file__).with_name(OUTPUT_FILE)

    mesh = trimesh.load(source_path, force="mesh")
    primary_body = select_primary_body(mesh)
    scaled = scale_mesh_to_target(primary_body)
    rebuilt = build_open_shell(scaled)
    fitted = fit_final_extents(rebuilt)
    cleaned = select_primary_body(fitted)
    export_model(cleaned, output_path)

    print(output_path)
    print("extents_mm", cleaned.extents.tolist())
    print("bounds_mm", cleaned.bounds.tolist())
    print("watertight", cleaned.is_watertight)


if __name__ == "__main__":
    main()

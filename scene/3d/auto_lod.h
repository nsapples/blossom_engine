/**************************************************************************/
/*  auto_lod.h                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"

class Camera3D;
class MeshInstance3D;

class AutoLOD : public Node3D {
	GDCLASS(AutoLOD, Node3D);

public:
	enum MaterialLODLevel {
		MAT_LOD_FULL,     // All features enabled.
		MAT_LOD_REDUCED,  // Normal maps disabled, detail textures off.
		MAT_LOD_SIMPLE,   // Flat shading, no specular, no AO.
		MAT_LOD_CHEAPEST, // Unshaded / unlit.
	};

private:
	struct TrackedMesh {
		MeshInstance3D *instance = nullptr;
		Vector<Ref<Material>> original_materials;
		Vector<Vector<Ref<Material>>> lod_materials; // [surface][lod_level]
		MaterialLODLevel current_mat_lod = MAT_LOD_FULL;
		bool materials_generated = false;
	};

	Vector<TrackedMesh> tracked_meshes;
	bool auto_discover = true;
	bool discovered = false;

	// Distance thresholds for material LOD.
	real_t mat_distance_reduced = 15.0;
	real_t mat_distance_simple = 40.0;
	real_t mat_distance_cheapest = 80.0;

	// Texture streaming integration.
	bool integrate_texture_streaming = true;

	// Update throttle.
	int update_interval_frames = 2; // Update every N frames.
	int frame_counter = 0;

	void _discover_meshes();
	void _generate_material_lods(TrackedMesh &p_tracked);
	Ref<Material> _create_reduced_material(const Ref<Material> &p_source);
	Ref<Material> _create_simple_material(const Ref<Material> &p_source);
	Ref<Material> _create_cheapest_material(const Ref<Material> &p_source);
	MaterialLODLevel _distance_to_mat_lod(real_t p_distance) const;
	void _update_lods(Camera3D *p_camera);
	void _report_texture_distances(const TrackedMesh &p_tracked, real_t p_distance);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_auto_discover(bool p_enable);
	bool get_auto_discover() const;

	void set_mat_distance_reduced(real_t p_distance);
	real_t get_mat_distance_reduced() const;
	void set_mat_distance_simple(real_t p_distance);
	real_t get_mat_distance_simple() const;
	void set_mat_distance_cheapest(real_t p_distance);
	real_t get_mat_distance_cheapest() const;

	void set_integrate_texture_streaming(bool p_enable);
	bool get_integrate_texture_streaming() const;

	void set_update_interval_frames(int p_interval);
	int get_update_interval_frames() const;

	void add_mesh_instance(MeshInstance3D *p_instance);
	void remove_mesh_instance(MeshInstance3D *p_instance);

	int get_tracked_mesh_count() const;

	AutoLOD();
};

VARIANT_ENUM_CAST(AutoLOD::MaterialLODLevel);

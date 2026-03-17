/**************************************************************************/
/*  auto_lod.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "auto_lod.h"

#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/texture_streaming_manager.h"
#include "scene/main/viewport.h"

void AutoLOD::_discover_meshes() {
	tracked_meshes.clear();

	TypedArray<Node> nodes = get_tree()->get_current_scene()->find_children("*", "MeshInstance3D", true, false);
	for (int i = 0; i < nodes.size(); i++) {
		MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(nodes[i].operator Object *());
		if (mi && mi->get_mesh().is_valid()) {
			add_mesh_instance(mi);
		}
	}
	discovered = true;
}

void AutoLOD::add_mesh_instance(MeshInstance3D *p_instance) {
	// Check if already tracked.
	for (const TrackedMesh &tm : tracked_meshes) {
		if (tm.instance == p_instance) {
			return;
		}
	}

	TrackedMesh tm;
	tm.instance = p_instance;

	// Store original materials.
	Ref<Mesh> mesh = p_instance->get_mesh();
	if (mesh.is_valid()) {
		int surface_count = mesh->get_surface_count();
		tm.original_materials.resize(surface_count);
		for (int s = 0; s < surface_count; s++) {
			Ref<Material> override_mat = p_instance->get_surface_override_material(s);
			if (override_mat.is_valid()) {
				tm.original_materials.write[s] = override_mat;
			} else {
				tm.original_materials.write[s] = mesh->surface_get_material(s);
			}
		}
	}

	tracked_meshes.push_back(tm);
}

void AutoLOD::remove_mesh_instance(MeshInstance3D *p_instance) {
	for (int i = 0; i < tracked_meshes.size(); i++) {
		if (tracked_meshes[i].instance == p_instance) {
			// Restore original materials.
			TrackedMesh &tm = tracked_meshes.write[i];
			for (int s = 0; s < tm.original_materials.size(); s++) {
				tm.instance->set_surface_override_material(s, tm.original_materials[s]);
			}
			tracked_meshes.remove_at(i);
			return;
		}
	}
}

void AutoLOD::_generate_material_lods(TrackedMesh &p_tracked) {
	if (p_tracked.materials_generated) {
		return;
	}

	int surface_count = p_tracked.original_materials.size();
	p_tracked.lod_materials.resize(surface_count);

	for (int s = 0; s < surface_count; s++) {
		Ref<Material> orig = p_tracked.original_materials[s];
		p_tracked.lod_materials.write[s].resize(4);
		p_tracked.lod_materials.write[s].write[MAT_LOD_FULL] = orig;

		if (orig.is_valid()) {
			p_tracked.lod_materials.write[s].write[MAT_LOD_REDUCED] = _create_reduced_material(orig);
			p_tracked.lod_materials.write[s].write[MAT_LOD_SIMPLE] = _create_simple_material(orig);
			p_tracked.lod_materials.write[s].write[MAT_LOD_CHEAPEST] = _create_cheapest_material(orig);
		} else {
			p_tracked.lod_materials.write[s].write[MAT_LOD_REDUCED] = Ref<Material>();
			p_tracked.lod_materials.write[s].write[MAT_LOD_SIMPLE] = Ref<Material>();
			p_tracked.lod_materials.write[s].write[MAT_LOD_CHEAPEST] = Ref<Material>();
		}
	}

	p_tracked.materials_generated = true;
}

Ref<Material> AutoLOD::_create_reduced_material(const Ref<Material> &p_source) {
	Ref<StandardMaterial3D> src = p_source;
	if (src.is_null()) {
		return p_source; // Not a StandardMaterial3D, can't simplify.
	}

	Ref<StandardMaterial3D> mat = src->duplicate();

	// Disable normal map.
	mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, false);
	mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, Ref<Texture2D>());

	// Disable detail texture.
	mat->set_feature(BaseMaterial3D::FEATURE_DETAIL, false);
	mat->set_texture(BaseMaterial3D::TEXTURE_DETAIL_ALBEDO, Ref<Texture2D>());
	mat->set_texture(BaseMaterial3D::TEXTURE_DETAIL_NORMAL, Ref<Texture2D>());

	// Disable heightmap/parallax.
	mat->set_feature(BaseMaterial3D::FEATURE_HEIGHT_MAPPING, false);
	mat->set_texture(BaseMaterial3D::TEXTURE_HEIGHTMAP, Ref<Texture2D>());

	return mat;
}

Ref<Material> AutoLOD::_create_simple_material(const Ref<Material> &p_source) {
	Ref<StandardMaterial3D> src = p_source;
	if (src.is_null()) {
		return p_source;
	}

	Ref<StandardMaterial3D> mat = src->duplicate();

	// Everything from reduced.
	mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, false);
	mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, Ref<Texture2D>());
	mat->set_feature(BaseMaterial3D::FEATURE_DETAIL, false);
	mat->set_feature(BaseMaterial3D::FEATURE_HEIGHT_MAPPING, false);

	// Also disable AO.
	mat->set_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION, false);
	mat->set_texture(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, Ref<Texture2D>());

	// Disable emission.
	mat->set_feature(BaseMaterial3D::FEATURE_EMISSION, false);
	mat->set_texture(BaseMaterial3D::TEXTURE_EMISSION, Ref<Texture2D>());

	// Disable roughness texture (keep value).
	mat->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, Ref<Texture2D>());

	// Disable metallic texture (keep value).
	mat->set_texture(BaseMaterial3D::TEXTURE_METALLIC, Ref<Texture2D>());

	return mat;
}

Ref<Material> AutoLOD::_create_cheapest_material(const Ref<Material> &p_source) {
	Ref<StandardMaterial3D> src = p_source;
	if (src.is_null()) {
		return p_source;
	}

	Ref<StandardMaterial3D> mat;
	mat.instantiate();

	// Keep only albedo color and texture.
	mat->set_albedo(src->get_albedo());
	Ref<Texture2D> albedo_tex = src->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
	if (albedo_tex.is_valid()) {
		mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, albedo_tex);
	}

	// Unshaded for maximum performance.
	mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	// Preserve transparency mode.
	mat->set_transparency(src->get_transparency());
	if (src->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED) {
		mat->set_alpha_scissor_threshold(src->get_alpha_scissor_threshold());
	}

	// Disable all features.
	mat->set_cull_mode(src->get_cull_mode());

	return mat;
}

AutoLOD::MaterialLODLevel AutoLOD::_distance_to_mat_lod(real_t p_distance) const {
	if (p_distance <= mat_distance_reduced) {
		return MAT_LOD_FULL;
	} else if (p_distance <= mat_distance_simple) {
		return MAT_LOD_REDUCED;
	} else if (p_distance <= mat_distance_cheapest) {
		return MAT_LOD_SIMPLE;
	}
	return MAT_LOD_CHEAPEST;
}

void AutoLOD::_report_texture_distances(const TrackedMesh &p_tracked, real_t p_distance) {
	TextureStreamingManager *tsm = TextureStreamingManager::get_singleton();
	if (!tsm) {
		return;
	}

	for (int s = 0; s < p_tracked.original_materials.size(); s++) {
		Ref<StandardMaterial3D> mat = p_tracked.original_materials[s];
		if (mat.is_null()) {
			continue;
		}

		// Report distance for albedo texture.
		Ref<Texture2D> albedo = mat->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
		if (albedo.is_valid()) {
			tsm->report_texture_distance(albedo->get_rid(), p_distance);
		}

		// Report distance for normal map.
		Ref<Texture2D> normal = mat->get_texture(BaseMaterial3D::TEXTURE_NORMAL);
		if (normal.is_valid()) {
			tsm->report_texture_distance(normal->get_rid(), p_distance);
		}

		// Report distance for emission.
		Ref<Texture2D> emission = mat->get_texture(BaseMaterial3D::TEXTURE_EMISSION);
		if (emission.is_valid()) {
			tsm->report_texture_distance(emission->get_rid(), p_distance);
		}
	}
}

void AutoLOD::_update_lods(Camera3D *p_camera) {
	Vector3 cam_pos = p_camera->get_global_position();

	for (int i = 0; i < tracked_meshes.size(); i++) {
		TrackedMesh &tm = tracked_meshes.write[i];

		if (!tm.instance || !tm.instance->is_inside_tree()) {
			continue;
		}

		// Generate material LODs lazily on first use.
		if (!tm.materials_generated) {
			_generate_material_lods(tm);
		}

		real_t distance = cam_pos.distance_to(tm.instance->get_global_position());
		MaterialLODLevel desired = _distance_to_mat_lod(distance);

		// Swap materials if LOD level changed.
		if (desired != tm.current_mat_lod) {
			for (int s = 0; s < tm.lod_materials.size(); s++) {
				if (tm.lod_materials[s].size() > (int)desired) {
					tm.instance->set_surface_override_material(s, tm.lod_materials[s][desired]);
				}
			}
			tm.current_mat_lod = desired;
		}

		// Report texture distances.
		if (integrate_texture_streaming) {
			_report_texture_distances(tm, distance);
		}
	}
}

void AutoLOD::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (auto_discover) {
				callable_mp(this, &AutoLOD::_discover_meshes).call_deferred();
			}
		} break;

		case NOTIFICATION_PROCESS: {
			frame_counter++;
			if (frame_counter < update_interval_frames) {
				return;
			}
			frame_counter = 0;

			Camera3D *camera = get_viewport()->get_camera_3d();
			if (!camera) {
				return;
			}

			if (auto_discover && !discovered) {
				_discover_meshes();
			}

			_update_lods(camera);

			// Also tick the texture streaming manager.
			if (integrate_texture_streaming) {
				TextureStreamingManager *tsm = TextureStreamingManager::get_singleton();
				if (tsm) {
					tsm->process(get_process_delta_time());
				}
			}
		} break;
	}
}

// Properties.

void AutoLOD::set_auto_discover(bool p_enable) {
	auto_discover = p_enable;
}

bool AutoLOD::get_auto_discover() const {
	return auto_discover;
}

void AutoLOD::set_mat_distance_reduced(real_t p_distance) {
	mat_distance_reduced = p_distance;
}

real_t AutoLOD::get_mat_distance_reduced() const {
	return mat_distance_reduced;
}

void AutoLOD::set_mat_distance_simple(real_t p_distance) {
	mat_distance_simple = p_distance;
}

real_t AutoLOD::get_mat_distance_simple() const {
	return mat_distance_simple;
}

void AutoLOD::set_mat_distance_cheapest(real_t p_distance) {
	mat_distance_cheapest = p_distance;
}

real_t AutoLOD::get_mat_distance_cheapest() const {
	return mat_distance_cheapest;
}

void AutoLOD::set_integrate_texture_streaming(bool p_enable) {
	integrate_texture_streaming = p_enable;
}

bool AutoLOD::get_integrate_texture_streaming() const {
	return integrate_texture_streaming;
}

void AutoLOD::set_update_interval_frames(int p_interval) {
	update_interval_frames = MAX(1, p_interval);
}

int AutoLOD::get_update_interval_frames() const {
	return update_interval_frames;
}

int AutoLOD::get_tracked_mesh_count() const {
	return tracked_meshes.size();
}

void AutoLOD::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_auto_discover", "enable"), &AutoLOD::set_auto_discover);
	ClassDB::bind_method(D_METHOD("get_auto_discover"), &AutoLOD::get_auto_discover);

	ClassDB::bind_method(D_METHOD("set_mat_distance_reduced", "distance"), &AutoLOD::set_mat_distance_reduced);
	ClassDB::bind_method(D_METHOD("get_mat_distance_reduced"), &AutoLOD::get_mat_distance_reduced);
	ClassDB::bind_method(D_METHOD("set_mat_distance_simple", "distance"), &AutoLOD::set_mat_distance_simple);
	ClassDB::bind_method(D_METHOD("get_mat_distance_simple"), &AutoLOD::get_mat_distance_simple);
	ClassDB::bind_method(D_METHOD("set_mat_distance_cheapest", "distance"), &AutoLOD::set_mat_distance_cheapest);
	ClassDB::bind_method(D_METHOD("get_mat_distance_cheapest"), &AutoLOD::get_mat_distance_cheapest);

	ClassDB::bind_method(D_METHOD("set_integrate_texture_streaming", "enable"), &AutoLOD::set_integrate_texture_streaming);
	ClassDB::bind_method(D_METHOD("get_integrate_texture_streaming"), &AutoLOD::get_integrate_texture_streaming);

	ClassDB::bind_method(D_METHOD("set_update_interval_frames", "interval"), &AutoLOD::set_update_interval_frames);
	ClassDB::bind_method(D_METHOD("get_update_interval_frames"), &AutoLOD::get_update_interval_frames);

	ClassDB::bind_method(D_METHOD("add_mesh_instance", "instance"), &AutoLOD::add_mesh_instance);
	ClassDB::bind_method(D_METHOD("remove_mesh_instance", "instance"), &AutoLOD::remove_mesh_instance);
	ClassDB::bind_method(D_METHOD("get_tracked_mesh_count"), &AutoLOD::get_tracked_mesh_count);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_discover"), "set_auto_discover", "get_auto_discover");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "integrate_texture_streaming"), "set_integrate_texture_streaming", "get_integrate_texture_streaming");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "update_interval_frames", PROPERTY_HINT_RANGE, "1,30,1"), "set_update_interval_frames", "get_update_interval_frames");

	ADD_GROUP("Material LOD Distances", "mat_distance_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mat_distance_reduced", PROPERTY_HINT_RANGE, "1,100,0.5"), "set_mat_distance_reduced", "get_mat_distance_reduced");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mat_distance_simple", PROPERTY_HINT_RANGE, "5,200,1"), "set_mat_distance_simple", "get_mat_distance_simple");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mat_distance_cheapest", PROPERTY_HINT_RANGE, "10,500,1"), "set_mat_distance_cheapest", "get_mat_distance_cheapest");

	BIND_ENUM_CONSTANT(MAT_LOD_FULL);
	BIND_ENUM_CONSTANT(MAT_LOD_REDUCED);
	BIND_ENUM_CONSTANT(MAT_LOD_SIMPLE);
	BIND_ENUM_CONSTANT(MAT_LOD_CHEAPEST);
}

AutoLOD::AutoLOD() {
	set_process(true);
}

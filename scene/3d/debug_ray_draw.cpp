/**************************************************************************/
/*  debug_ray_draw.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "debug_ray_draw.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/material.h"

DebugRayDraw *DebugRayDraw::singleton = nullptr;

DebugRayDraw *DebugRayDraw::get_singleton() {
	return singleton;
}

void DebugRayDraw::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (!enabled) {
		clear();
	}
}

bool DebugRayDraw::is_enabled() const {
	return enabled;
}

void DebugRayDraw::add_ray(const Vector3 &p_from, const Vector3 &p_to, bool p_hit, const Color &p_color) {
	if (!enabled) {
		return;
	}
	if (pending_rays.size() >= (uint32_t)max_rays_per_frame) {
		return;
	}

	RayEntry entry;
	entry.from = p_from;
	entry.to = p_to;
	entry.hit = p_hit;
	entry.color = p_hit ? p_color : Color(1, 0, 0); // Red for miss, custom for hit.
	pending_rays.push_back(entry);
}

void DebugRayDraw::_ensure_draw_nodes(Node *p_parent) {
	if (draw_nodes_created || !p_parent) {
		return;
	}

	mesh = memnew(ImmediateMesh);

	mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("_DebugRayDraw");
	mesh_instance->set_mesh(mesh);
	mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);

	// Unshaded material.
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	mat->set_no_depth_test(true);
	mesh_instance->set_surface_override_material(0, mat);

	p_parent->add_child(mesh_instance);
	draw_nodes_created = true;
}

void DebugRayDraw::flush(Node *p_scene_root) {
	if (!enabled || pending_rays.is_empty()) {
		if (mesh) {
			mesh->clear_surfaces();
		}
		pending_rays.clear();
		return;
	}

	_ensure_draw_nodes(p_scene_root);

	if (!mesh) {
		pending_rays.clear();
		return;
	}

	mesh->clear_surfaces();
	mesh->surface_begin(Mesh::PRIMITIVE_LINES);

	for (const RayEntry &ray : pending_rays) {
		mesh->surface_set_color(ray.color);
		mesh->surface_add_vertex(ray.from);
		mesh->surface_set_color(ray.color);
		mesh->surface_add_vertex(ray.to);

		// Draw a small cross at hit point.
		if (ray.hit) {
			float s = 0.1;
			mesh->surface_set_color(Color(1, 1, 0)); // Yellow hit marker.
			mesh->surface_add_vertex(ray.to + Vector3(-s, 0, 0));
			mesh->surface_set_color(Color(1, 1, 0));
			mesh->surface_add_vertex(ray.to + Vector3(s, 0, 0));
			mesh->surface_set_color(Color(1, 1, 0));
			mesh->surface_add_vertex(ray.to + Vector3(0, -s, 0));
			mesh->surface_set_color(Color(1, 1, 0));
			mesh->surface_add_vertex(ray.to + Vector3(0, s, 0));
			mesh->surface_set_color(Color(1, 1, 0));
			mesh->surface_add_vertex(ray.to + Vector3(0, 0, -s));
			mesh->surface_set_color(Color(1, 1, 0));
			mesh->surface_add_vertex(ray.to + Vector3(0, 0, s));
		}
	}

	mesh->surface_end();
	pending_rays.clear();
}

void DebugRayDraw::clear() {
	pending_rays.clear();
	if (mesh) {
		mesh->clear_surfaces();
	}
}

void DebugRayDraw::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &DebugRayDraw::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &DebugRayDraw::is_enabled);
	ClassDB::bind_method(D_METHOD("add_ray", "from", "to", "hit", "color"), &DebugRayDraw::add_ray, DEFVAL(Color(0, 1, 0)));
	ClassDB::bind_method(D_METHOD("clear"), &DebugRayDraw::clear);
}

DebugRayDraw::DebugRayDraw() {
	singleton = this;
	enabled = GLOBAL_DEF("debug/physics/visualize_raycasts", false);
}

DebugRayDraw::~DebugRayDraw() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

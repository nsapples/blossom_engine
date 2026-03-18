/**************************************************************************/
/*  debug_ray_draw.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "core/math/vector3.h"
#include "core/math/color.h"
#include "core/object/object.h"
#include "core/templates/local_vector.h"

class ImmediateMesh;
class MeshInstance3D;
class Node;

class DebugRayDraw : public Object {
	GDCLASS(DebugRayDraw, Object);

public:
	struct RayEntry {
		Vector3 from;
		Vector3 to;
		Color color;
		bool hit;
	};

private:
	static DebugRayDraw *singleton;

	LocalVector<RayEntry> pending_rays;
	bool enabled = false;
	int max_rays_per_frame = 512;

	// Drawing.
	ImmediateMesh *mesh = nullptr;
	MeshInstance3D *mesh_instance = nullptr;
	bool draw_nodes_created = false;

	void _ensure_draw_nodes(Node *p_parent);

protected:
	static void _bind_methods();

public:
	static DebugRayDraw *get_singleton();

	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	// Call this from any system that does raycasts.
	void add_ray(const Vector3 &p_from, const Vector3 &p_to, bool p_hit, const Color &p_color = Color(0, 1, 0));

	// Called once per frame to flush and draw all rays.
	void flush(Node *p_scene_root);

	// Clear without drawing.
	void clear();

	DebugRayDraw();
	~DebugRayDraw();
};

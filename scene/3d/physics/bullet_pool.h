/**************************************************************************/
/*  bullet_pool.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "scene/3d/node_3d.h"

class BallisticBody3D;

class BulletPool : public Node3D {
	GDCLASS(BulletPool, Node3D);

private:
	int pool_size = 200;
	Vector<BallisticBody3D *> pool;
	Vector<BallisticBody3D *> active;
	bool pool_created = false;

	void _create_pool();
	BallisticBody3D *_get_bullet();
	void _return_bullet(BallisticBody3D *p_bullet);
	void _on_bullet_hit(const Vector3 &p_position, const Vector3 &p_normal, BallisticBody3D *p_bullet);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Fire a bullet from a position in a direction with speed.
	BallisticBody3D *fire(const Vector3 &p_position, const Vector3 &p_direction, real_t p_speed, real_t p_penetration_power = 1.0);

	// Return all active bullets to the pool.
	void clear_all();

	// Settings.
	void set_pool_size(int p_size);
	int get_pool_size() const;

	// Stats.
	int get_active_count() const;
	int get_available_count() const;

	BulletPool();
};

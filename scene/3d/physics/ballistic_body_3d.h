/**************************************************************************/
/*  ballistic_body_3d.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "scene/3d/physics/rigid_body_3d.h"

class BallisticBody3D : public RigidBody3D {
	GDCLASS(BallisticBody3D, RigidBody3D);

public:
	enum BallisticState {
		STATE_FLYING,
		STATE_HIT,
		STATE_PENETRATED,
		STATE_RICOCHETED,
	};

private:
	Vector3 previous_position;
	bool has_previous_position = false;
	BallisticState ballistic_state = STATE_FLYING;

	// Hit results.
	Vector3 hit_position;
	Vector3 hit_normal;
	ObjectID hit_collider_id;

	// Projectile properties.
	real_t penetration_power = 1.0;
	real_t raycast_margin = 0.01;

	void _perform_raycast(PhysicsDirectBodyState3D *p_state);
	void _handle_impact(PhysicsDirectBodyState3D *p_state, const Ref<PhysicsMaterial> &p_material);

protected:
	virtual void _body_state_changed(PhysicsDirectBodyState3D *p_state) override;
	static void _bind_methods();

public:
	void set_penetration_power(real_t p_power);
	real_t get_penetration_power() const;

	void set_raycast_margin(real_t p_margin);
	real_t get_raycast_margin() const;

	BallisticState get_ballistic_state() const;
	Vector3 get_hit_position() const;
	Vector3 get_hit_normal() const;

	BallisticBody3D();
};

VARIANT_ENUM_CAST(BallisticBody3D::BallisticState);

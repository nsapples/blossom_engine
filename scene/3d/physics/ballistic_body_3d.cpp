/**************************************************************************/
/*  ballistic_body_3d.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "ballistic_body_3d.h"

#include "core/object/class_db.h"
#include "scene/3d/debug_ray_draw.h"
#include "scene/3d/physics/physics_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/physics_material.h"
#include "servers/physics_3d/physics_server_3d.h"

void BallisticBody3D::_perform_raycast(PhysicsDirectBodyState3D *p_state) {
	if (ballistic_state != STATE_FLYING) {
		return;
	}

	Vector3 current_position = p_state->get_transform().origin;

	if (!has_previous_position) {
		previous_position = current_position;
		has_previous_position = true;
		return;
	}

	Vector3 velocity = p_state->get_linear_velocity();
	real_t speed = velocity.length();
	if (speed < CMP_EPSILON) {
		previous_position = current_position;
		return;
	}

	// Raycast from previous position to current position + margin along velocity.
	Vector3 ray_from = previous_position;
	Vector3 ray_to = current_position + velocity.normalized() * raycast_margin;

	PhysicsDirectSpaceState3D *space = p_state->get_space_state();

	PhysicsDirectSpaceState3D::RayParameters params;
	params.from = ray_from;
	params.to = ray_to;
	params.exclude.insert(get_rid());
	params.collision_mask = get_collision_mask();
	params.collide_with_bodies = true;
	params.collide_with_areas = false;

	PhysicsDirectSpaceState3D::RayResult result;
	bool col = space->intersect_ray(params, result);

	// Report to debug ray draw.
	if (DebugRayDraw::get_singleton() && DebugRayDraw::get_singleton()->is_enabled()) {
		DebugRayDraw::get_singleton()->add_ray(ray_from, col ? result.position : ray_to, col, Color(1.0, 0.3, 0.1));
	}

	if (col) {
		hit_position = result.position;
		hit_normal = result.normal;
		hit_collider_id = result.collider_id;

		// Try to get the PhysicsMaterial from the hit body.
		Ref<PhysicsMaterial> material;
		Object *collider = result.collider;
		if (collider) {
			StaticBody3D *static_body = Object::cast_to<StaticBody3D>(collider);
			if (static_body) {
				material = static_body->get_physics_material_override();
			} else {
				RigidBody3D *rigid_body = Object::cast_to<RigidBody3D>(collider);
				if (rigid_body) {
					material = rigid_body->get_physics_material_override();
				}
			}
		}

		_handle_impact(p_state, material);
	}

	previous_position = current_position;
}

void BallisticBody3D::_handle_impact(PhysicsDirectBodyState3D *p_state, const Ref<PhysicsMaterial> &p_material) {
	real_t resistance = 1.0;
	real_t reflection = 0.0;

	if (p_material.is_valid()) {
		resistance = p_material->get_penetration_resistance();
		reflection = p_material->get_reflection_factor();

		// Spawn impact scene at hit point.
		Ref<PackedScene> impact_scene = p_material->get_impact_scene();
		if (impact_scene.is_valid()) {
			Node *impact_instance = impact_scene->instantiate();
			if (impact_instance) {
				Node3D *impact_3d = Object::cast_to<Node3D>(impact_instance);
				if (impact_3d) {
					// Orient impact effect along hit normal.
					Transform3D t;
					t.origin = hit_position;
					// Align -Z with hit normal.
					Vector3 up = hit_normal.abs().is_equal_approx(Vector3(0, 1, 0)) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
					t.basis = Basis::looking_at(-hit_normal, up);
					impact_3d->set_global_transform(t);
				}
				get_tree()->get_current_scene()->add_child(impact_instance);
			}
		}
	}

	Vector3 velocity = p_state->get_linear_velocity();

	if (penetration_power > resistance) {
		// Penetrate — reduce speed proportionally and continue.
		ballistic_state = STATE_PENETRATED;
		real_t speed_factor = 1.0 - (resistance / penetration_power);
		p_state->set_linear_velocity(velocity * MAX(speed_factor, 0.1));
		penetration_power -= resistance;
		// Return to flying so it can hit more things.
		ballistic_state = STATE_FLYING;

		emit_signal("penetrated", hit_position, hit_normal);
	} else if (reflection > CMP_EPSILON) {
		// Ricochet — reflect velocity along hit normal.
		ballistic_state = STATE_RICOCHETED;
		Vector3 reflected = velocity.bounce(hit_normal) * reflection;
		p_state->set_linear_velocity(reflected);
		p_state->set_transform(Transform3D(p_state->get_transform().basis, hit_position + hit_normal * 0.01));

		emit_signal("ricocheted", hit_position, hit_normal);
		ballistic_state = STATE_FLYING;
	} else {
		// Full stop.
		ballistic_state = STATE_HIT;
		p_state->set_linear_velocity(Vector3());
		p_state->set_angular_velocity(Vector3());
		p_state->set_transform(Transform3D(p_state->get_transform().basis, hit_position));
		set_freeze_enabled(true);

		emit_signal("hit", hit_position, hit_normal);
	}
}

void BallisticBody3D::_body_state_changed(PhysicsDirectBodyState3D *p_state) {
	RigidBody3D::_body_state_changed(p_state);
	_perform_raycast(p_state);
}

void BallisticBody3D::set_penetration_power(real_t p_power) {
	penetration_power = p_power;
}

real_t BallisticBody3D::get_penetration_power() const {
	return penetration_power;
}

void BallisticBody3D::set_raycast_margin(real_t p_margin) {
	raycast_margin = p_margin;
}

real_t BallisticBody3D::get_raycast_margin() const {
	return raycast_margin;
}

BallisticBody3D::BallisticState BallisticBody3D::get_ballistic_state() const {
	return ballistic_state;
}

Vector3 BallisticBody3D::get_hit_position() const {
	return hit_position;
}

Vector3 BallisticBody3D::get_hit_normal() const {
	return hit_normal;
}

void BallisticBody3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_penetration_power", "power"), &BallisticBody3D::set_penetration_power);
	ClassDB::bind_method(D_METHOD("get_penetration_power"), &BallisticBody3D::get_penetration_power);

	ClassDB::bind_method(D_METHOD("set_raycast_margin", "margin"), &BallisticBody3D::set_raycast_margin);
	ClassDB::bind_method(D_METHOD("get_raycast_margin"), &BallisticBody3D::get_raycast_margin);

	ClassDB::bind_method(D_METHOD("get_ballistic_state"), &BallisticBody3D::get_ballistic_state);
	ClassDB::bind_method(D_METHOD("get_hit_position"), &BallisticBody3D::get_hit_position);
	ClassDB::bind_method(D_METHOD("get_hit_normal"), &BallisticBody3D::get_hit_normal);

	ADD_GROUP("Ballistics", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "penetration_power", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_penetration_power", "get_penetration_power");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "raycast_margin", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_raycast_margin", "get_raycast_margin");

	ADD_SIGNAL(MethodInfo("hit", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::VECTOR3, "normal")));
	ADD_SIGNAL(MethodInfo("penetrated", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::VECTOR3, "normal")));
	ADD_SIGNAL(MethodInfo("ricocheted", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::VECTOR3, "normal")));

	BIND_ENUM_CONSTANT(STATE_FLYING);
	BIND_ENUM_CONSTANT(STATE_HIT);
	BIND_ENUM_CONSTANT(STATE_PENETRATED);
	BIND_ENUM_CONSTANT(STATE_RICOCHETED);
}

BallisticBody3D::BallisticBody3D() {
}

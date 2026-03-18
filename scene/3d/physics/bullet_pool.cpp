/**************************************************************************/
/*  bullet_pool.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "bullet_pool.h"
#include "ballistic_body_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"

void BulletPool::_create_pool() {
	if (pool_created) {
		return;
	}

	for (int i = 0; i < pool_size; i++) {
		BallisticBody3D *bullet = memnew(BallisticBody3D);
		bullet->set_name(vformat("_Bullet_%d", i));

		// Small sphere collision shape.
		CollisionShape3D *col = memnew(CollisionShape3D);
		Ref<SphereShape3D> shape;
		shape.instantiate();
		shape->set_radius(0.02);
		col->set_shape(shape);
		bullet->add_child(col);

		// Start frozen and hidden.
		bullet->set_freeze_enabled(true);
		bullet->set_visible(false);
		bullet->set_process_mode(PROCESS_MODE_DISABLED);

		// Connect hit signal to return bullet to pool.
		bullet->connect("hit", callable_mp(this, &BulletPool::_on_bullet_hit).bind(bullet));

		add_child(bullet);
		pool.push_back(bullet);
	}

	pool_created = true;
}

BallisticBody3D *BulletPool::_get_bullet() {
	if (pool.is_empty()) {
		// Pool exhausted — recycle the oldest active bullet.
		if (active.is_empty()) {
			return nullptr;
		}
		BallisticBody3D *oldest = active[0];
		_return_bullet(oldest);
	}

	BallisticBody3D *bullet = pool[pool.size() - 1];
	pool.remove_at(pool.size() - 1);
	active.push_back(bullet);
	return bullet;
}

void BulletPool::_return_bullet(BallisticBody3D *p_bullet) {
	// Deactivate.
	p_bullet->set_freeze_enabled(true);
	p_bullet->set_visible(false);
	p_bullet->set_process_mode(PROCESS_MODE_DISABLED);
	p_bullet->set_linear_velocity(Vector3());
	p_bullet->set_angular_velocity(Vector3());

	// Move back to pool.
	for (int i = 0; i < active.size(); i++) {
		if (active[i] == p_bullet) {
			active.remove_at(i);
			break;
		}
	}
	pool.push_back(p_bullet);
}

void BulletPool::_on_bullet_hit(const Vector3 &p_position, const Vector3 &p_normal, BallisticBody3D *p_bullet) {
	// Return to pool after a short delay so impact effects can play.
	// For now, return immediately.
	_return_bullet(p_bullet);
}

BallisticBody3D *BulletPool::fire(const Vector3 &p_position, const Vector3 &p_direction, real_t p_speed, real_t p_penetration_power) {
	if (!pool_created) {
		_create_pool();
	}

	BallisticBody3D *bullet = _get_bullet();
	if (!bullet) {
		return nullptr;
	}

	// Reset state.
	bullet->set_freeze_enabled(false);
	bullet->set_visible(true);
	bullet->set_process_mode(PROCESS_MODE_INHERIT);

	// Position and launch.
	Transform3D t;
	t.origin = p_position;
	// Align -Z with direction.
	Vector3 dir = p_direction.normalized();
	Vector3 up = dir.abs().is_equal_approx(Vector3(0, 1, 0)) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
	t.basis = Basis::looking_at(dir, up);
	bullet->set_global_transform(t);

	bullet->set_linear_velocity(dir * p_speed);
	bullet->set_angular_velocity(Vector3());
	bullet->set_penetration_power(p_penetration_power);

	return bullet;
}

void BulletPool::clear_all() {
	while (!active.is_empty()) {
		_return_bullet(active[0]);
	}
}

void BulletPool::set_pool_size(int p_size) {
	pool_size = MAX(1, p_size);
}

int BulletPool::get_pool_size() const {
	return pool_size;
}

int BulletPool::get_active_count() const {
	return active.size();
}

int BulletPool::get_available_count() const {
	return pool.size();
}

void BulletPool::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_create_pool();
		} break;
	}
}

void BulletPool::_bind_methods() {
	ClassDB::bind_method(D_METHOD("fire", "position", "direction", "speed", "penetration_power"), &BulletPool::fire, DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("clear_all"), &BulletPool::clear_all);

	ClassDB::bind_method(D_METHOD("set_pool_size", "size"), &BulletPool::set_pool_size);
	ClassDB::bind_method(D_METHOD("get_pool_size"), &BulletPool::get_pool_size);

	ClassDB::bind_method(D_METHOD("get_active_count"), &BulletPool::get_active_count);
	ClassDB::bind_method(D_METHOD("get_available_count"), &BulletPool::get_available_count);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "pool_size", PROPERTY_HINT_RANGE, "1,1000,1"), "set_pool_size", "get_pool_size");
}

BulletPool::BulletPool() {
}

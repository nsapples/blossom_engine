/**************************************************************************/
/*  spatial_audio_player_3d.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "spatial_audio_player_3d.h"

#include "core/object/class_db.h"
#include "scene/3d/physics/area_3d.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/physics_material.h"
#include "servers/audio/audio_server.h"
#include "servers/audio/effects/audio_effect_filter.h"
#include "servers/audio/effects/audio_effect_reverb.h"
#include "servers/physics_3d/physics_server_3d.h"

// ===========================================================
//  Setup
// ===========================================================

SpatialAudioPlayer3D::SpatialAudioPlayer3D() {
	_generate_reverb_directions();
}

SpatialAudioPlayer3D::~SpatialAudioPlayer3D() {
	_cleanup_audio_bus();
}

void SpatialAudioPlayer3D::_generate_reverb_directions() {
	reverb_directions.clear();
	// Fibonacci sphere for even distribution.
	float golden_ratio = (1.0f + Math::sqrt(5.0f)) / 2.0f;
	for (int i = 0; i < reverb_ray_count; i++) {
		float theta = 2.0f * 3.14159265358979323846 * float(i) / golden_ratio;
		float phi = Math::acos(1.0f - 2.0f * (float(i) + 0.5f) / float(reverb_ray_count));
		Vector3 dir(
				Math::sin(phi) * Math::cos(theta),
				Math::sin(phi) * Math::sin(theta),
				Math::cos(phi));
		reverb_directions.push_back(dir.normalized());
	}
}

void SpatialAudioPlayer3D::_setup_audio_bus() {
	if (bus_created) {
		return;
	}
	// Create a dedicated audio bus for this player's effects.
	dedicated_bus_name = vformat("_SpatialAudio_%d", get_instance_id());
	AudioServer::get_singleton()->add_bus(-1);
	bus_index = AudioServer::get_singleton()->get_bus_count() - 1;
	AudioServer::get_singleton()->set_bus_name(bus_index, dedicated_bus_name);
	AudioServer::get_singleton()->set_bus_send(bus_index, "Master");

	// Add lowpass filter effect.
	Ref<AudioEffectLowPassFilter> lpf;
	lpf.instantiate();
	lpf->set_cutoff(20500.0);
	AudioServer::get_singleton()->add_bus_effect(bus_index, lpf);

	// Add reverb effect.
	Ref<AudioEffectReverb> reverb;
	reverb.instantiate();
	reverb->set_room_size(0.0);
	reverb->set_wet(0.0);
	reverb->set_damping(0.5);
	AudioServer::get_singleton()->add_bus_effect(bus_index, reverb);

	set_bus(dedicated_bus_name);
	bus_created = true;
}

void SpatialAudioPlayer3D::_cleanup_audio_bus() {
	if (!bus_created) {
		return;
	}
	int idx = AudioServer::get_singleton()->get_bus_index(dedicated_bus_name);
	if (idx >= 0) {
		AudioServer::get_singleton()->remove_bus(idx);
	}
	bus_created = false;
}

// ===========================================================
//  Physics material integration
// ===========================================================

real_t SpatialAudioPlayer3D::_get_material_transmission(const Ref<PhysicsMaterial> &p_material) const {
	if (p_material.is_null()) {
		return 0.1; // Default: blocks most sound.
	}
	// Use inverse of penetration_resistance as transmission.
	// High resistance = low transmission.
	return CLAMP(1.0 / MAX(p_material->get_penetration_resistance(), 0.1), 0.0, 1.0);
}

real_t SpatialAudioPlayer3D::_get_material_reflection(const Ref<PhysicsMaterial> &p_material) const {
	if (p_material.is_null()) {
		return 0.5; // Default: moderate reflection.
	}
	// reflection_factor directly maps to acoustic reflection.
	return CLAMP(p_material->get_reflection_factor(), 0.0, 1.0);
}

// ===========================================================
//  Occlusion
// ===========================================================

void SpatialAudioPlayer3D::_update_occlusion(PhysicsDirectSpaceState3D *p_space, const Vector3 &p_listener_pos, real_t p_delta) {
	if (!occlusion_enabled) {
		target_occlusion_factor = 0.0;
		current_wall_count = 0;
		return;
	}

	Vector3 emitter_pos = get_global_position();
	Vector3 direction = p_listener_pos - emitter_pos;
	real_t distance = direction.length();

	if (distance < 0.01) {
		target_occlusion_factor = 0.0;
		current_wall_count = 0;
		return;
	}

	// Multi-ray occlusion: cast rays from emitter to listener,
	// counting walls hit and accumulating transmission loss.
	int walls = 0;
	real_t total_transmission = 1.0;
	Vector3 ray_from = emitter_pos;

	HashSet<RID> exclude;

	for (int i = 0; i < max_occlusion_rays && total_transmission > 0.01; i++) {
		PhysicsDirectSpaceState3D::RayParameters params;
		params.from = ray_from;
		params.to = p_listener_pos;
		params.collision_mask = occlusion_mask;
		params.exclude = exclude;
		params.collide_with_bodies = true;
		params.collide_with_areas = false;

		PhysicsDirectSpaceState3D::RayResult result;
		bool hit = p_space->intersect_ray(params, result);

		if (!hit) {
			break;
		}

		walls++;
		exclude.insert(result.rid);

		// Get material from hit body.
		Ref<PhysicsMaterial> material;
		Object *collider = result.collider;
		if (collider) {
			StaticBody3D *sb = Object::cast_to<StaticBody3D>(collider);
			if (sb) {
				material = sb->get_physics_material_override();
			} else {
				RigidBody3D *rb = Object::cast_to<RigidBody3D>(collider);
				if (rb) {
					material = rb->get_physics_material_override();
				}
			}
		}

		real_t transmission = _get_material_transmission(material);
		total_transmission *= transmission;

		// Continue ray from just past the hit point.
		ray_from = result.position + direction.normalized() * 0.05;
	}

	current_wall_count = walls;
	target_occlusion_factor = 1.0 - total_transmission;
}

// ===========================================================
//  Reverb estimation
// ===========================================================

void SpatialAudioPlayer3D::_update_reverb(PhysicsDirectSpaceState3D *p_space, real_t p_delta) {
	if (!reverb_enabled) {
		target_room_size = 0.0;
		target_wetness = 0.0;
		target_damping = 0.5;
		return;
	}

	Vector3 origin = get_global_position();
	real_t total_distance = 0.0;
	int hits = 0;
	real_t total_reflection = 0.0;

	HashSet<RID> exclude;

	for (const Vector3 &dir : reverb_directions) {
		PhysicsDirectSpaceState3D::RayParameters params;
		params.from = origin;
		params.to = origin + dir * max_reverb_distance;
		params.collision_mask = occlusion_mask;
		params.exclude = exclude;
		params.collide_with_bodies = true;
		params.collide_with_areas = false;

		PhysicsDirectSpaceState3D::RayResult result;
		bool hit = p_space->intersect_ray(params, result);

		if (hit) {
			real_t dist = origin.distance_to(result.position);
			total_distance += dist;
			hits++;

			// Get reflection from material.
			Ref<PhysicsMaterial> material;
			Object *collider = result.collider;
			if (collider) {
				StaticBody3D *sb = Object::cast_to<StaticBody3D>(collider);
				if (sb) {
					material = sb->get_physics_material_override();
				} else {
					RigidBody3D *rb = Object::cast_to<RigidBody3D>(collider);
					if (rb) {
						material = rb->get_physics_material_override();
					}
				}
			}
			total_reflection += _get_material_reflection(material);
		} else {
			// Ray didn't hit anything — open space.
			total_distance += max_reverb_distance;
		}
	}

	int total_rays = reverb_directions.size();
	real_t avg_distance = total_rays > 0 ? total_distance / total_rays : max_reverb_distance;
	real_t enclosure = total_rays > 0 ? float(hits) / float(total_rays) : 0.0;
	real_t avg_reflection = hits > 0 ? total_reflection / hits : 0.5;

	// Room size: normalized average distance.
	target_room_size = CLAMP(avg_distance / max_reverb_distance, 0.0, 1.0);

	// Wetness: more enclosed = more reverb, scaled by reflection.
	target_wetness = CLAMP(enclosure * avg_reflection * max_reverb_wetness, 0.0, max_reverb_wetness);

	// Damping: inverse of reflection (absorbent surfaces = more damping).
	target_damping = CLAMP(1.0 - avg_reflection, 0.0, 1.0);
}

// ===========================================================
//  Air absorption
// ===========================================================

void SpatialAudioPlayer3D::_update_air_absorption(real_t p_distance) {
	if (!air_absorption_enabled) {
		return;
	}
	// High frequencies are absorbed by air over distance.
	// Linear interpolation from 20500 Hz to min cutoff over max distance.
	real_t factor = CLAMP(p_distance / air_absorption_max_distance, 0.0, 1.0);
	real_t air_cutoff = Math::lerp(20500.0, air_absorption_min_cutoff, factor);

	// This will be combined with occlusion lowpass in _apply_effects.
	current_lowpass = MIN(current_lowpass, air_cutoff);
}

// ===========================================================
//  Water / submersion detection
// ===========================================================

void SpatialAudioPlayer3D::_update_water_state(PhysicsDirectSpaceState3D *p_space, const Vector3 &p_listener_pos) {
	if (!water_muffle_enabled) {
		listener_submerged = false;
		emitter_submerged = false;
		return;
	}

	// Check if emitter or listener is inside a water volume (Area3D with buoyancy).
	// We do point-in-area checks using the physics server.
	Vector3 emitter_pos = get_global_position();

	PhysicsDirectSpaceState3D::PointParameters point_params;
	point_params.collide_with_areas = true;
	point_params.collide_with_bodies = false;
	point_params.collision_mask = UINT32_MAX;

	// Check emitter.
	point_params.position = emitter_pos;
	Vector<PhysicsDirectSpaceState3D::ShapeResult> emitter_results;
	emitter_results.resize(8);
	int emitter_count = p_space->intersect_point(point_params, emitter_results.ptrw(), 8);
	emitter_submerged = false;
	for (int i = 0; i < emitter_count; i++) {
		Area3D *area = Object::cast_to<Area3D>(emitter_results[i].collider);
		if (area && area->get_gravity_point_unit_distance() > 0.0) {
			// Heuristic: areas with buoyancy properties are water volumes.
			emitter_submerged = true;
			break;
		}
	}

	// Check listener.
	point_params.position = p_listener_pos;
	Vector<PhysicsDirectSpaceState3D::ShapeResult> listener_results;
	listener_results.resize(8);
	int listener_count = p_space->intersect_point(point_params, listener_results.ptrw(), 8);
	listener_submerged = false;
	for (int i = 0; i < listener_count; i++) {
		Area3D *area = Object::cast_to<Area3D>(listener_results[i].collider);
		if (area && area->get_gravity_point_unit_distance() > 0.0) {
			listener_submerged = true;
			break;
		}
	}
}

// ===========================================================
//  Apply combined effects
// ===========================================================

void SpatialAudioPlayer3D::_apply_effects(real_t p_delta) {
	if (!bus_created) {
		return;
	}

	int idx = AudioServer::get_singleton()->get_bus_index(dedicated_bus_name);
	if (idx < 0) {
		return;
	}

	// Smooth occlusion factor.
	current_occlusion_factor = Math::lerp(current_occlusion_factor, target_occlusion_factor, MIN(1.0, p_delta * occlusion_smoothing));

	// Calculate lowpass from occlusion.
	real_t occ_cutoff = Math::lerp(20500.0, occlusion_lowpass_min, current_occlusion_factor);
	real_t final_cutoff = occ_cutoff;

	// Apply air absorption (already computed in current_lowpass).
	final_cutoff = MIN(final_cutoff, current_lowpass);

	// Apply water muffling.
	if (listener_submerged || emitter_submerged) {
		final_cutoff = MIN(final_cutoff, water_lowpass_cutoff);
	}

	// Update lowpass filter.
	Ref<AudioEffect> lpf_effect = AudioServer::get_singleton()->get_bus_effect(idx, 0);
	Ref<AudioEffectLowPassFilter> lpf = lpf_effect;
	if (lpf.is_valid()) {
		lpf->set_cutoff(final_cutoff);
	}

	// Smooth reverb.
	current_room_size = Math::lerp(current_room_size, target_room_size, MIN(1.0, p_delta * reverb_smoothing));
	current_wetness = Math::lerp(current_wetness, target_wetness, MIN(1.0, p_delta * reverb_smoothing));
	current_damping = Math::lerp(current_damping, target_damping, MIN(1.0, p_delta * reverb_smoothing));

	// Update reverb.
	Ref<AudioEffect> rev_effect = AudioServer::get_singleton()->get_bus_effect(idx, 1);
	Ref<AudioEffectReverb> reverb = rev_effect;
	if (reverb.is_valid()) {
		reverb->set_room_size(current_room_size);
		reverb->set_wet(current_wetness);
		reverb->set_damping(current_damping);
	}

	// Volume reduction from occlusion and water.
	real_t vol_reduction = current_occlusion_factor * occlusion_volume_reduction;
	if (listener_submerged || emitter_submerged) {
		vol_reduction += water_volume_reduction;
	}

	// Apply volume offset via bus volume.
	AudioServer::get_singleton()->set_bus_volume_db(idx, -vol_reduction);
}

// ===========================================================
//  Main loop
// ===========================================================

void SpatialAudioPlayer3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_setup_audio_bus();
			set_physics_process(true);
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_cleanup_audio_bus();
		} break;

		case NOTIFICATION_PHYSICS_PROCESS: {
			update_frame++;
			if (update_frame < update_interval) {
				// Still apply smoothing every frame.
				_apply_effects(get_physics_process_delta_time());
				return;
			}
			update_frame = 0;

			Camera3D *camera = get_viewport()->get_camera_3d();
			if (!camera) {
				return;
			}

			Vector3 listener_pos = camera->get_global_position();
			Vector3 emitter_pos = get_global_position();
			real_t distance = listener_pos.distance_to(emitter_pos);
			real_t delta = get_physics_process_delta_time() * update_interval;

			// Get physics space.
			PhysicsDirectSpaceState3D *space = get_world_3d()->get_direct_space_state();
			if (!space) {
				return;
			}

			// Reset per-frame lowpass accumulator.
			current_lowpass = 20500.0;

			// Update all systems.
			_update_occlusion(space, listener_pos, delta);
			_update_reverb(space, delta);
			_update_air_absorption(distance);
			_update_water_state(space, listener_pos);
			_apply_effects(delta);
		} break;
	}
}

// ===========================================================
//  Properties
// ===========================================================

void SpatialAudioPlayer3D::set_occlusion_enabled(bool p_enabled) { occlusion_enabled = p_enabled; }
bool SpatialAudioPlayer3D::is_occlusion_enabled() const { return occlusion_enabled; }
void SpatialAudioPlayer3D::set_max_occlusion_rays(int p_rays) { max_occlusion_rays = CLAMP(p_rays, 1, 10); }
int SpatialAudioPlayer3D::get_max_occlusion_rays() const { return max_occlusion_rays; }
void SpatialAudioPlayer3D::set_occlusion_lowpass_min(real_t p_hz) { occlusion_lowpass_min = CLAMP(p_hz, 100.0, 5000.0); }
real_t SpatialAudioPlayer3D::get_occlusion_lowpass_min() const { return occlusion_lowpass_min; }
void SpatialAudioPlayer3D::set_occlusion_volume_reduction(real_t p_db) { occlusion_volume_reduction = CLAMP(p_db, 0.0, 40.0); }
real_t SpatialAudioPlayer3D::get_occlusion_volume_reduction() const { return occlusion_volume_reduction; }
void SpatialAudioPlayer3D::set_occlusion_mask(uint32_t p_mask) { occlusion_mask = p_mask; }
uint32_t SpatialAudioPlayer3D::get_occlusion_mask() const { return occlusion_mask; }

void SpatialAudioPlayer3D::set_reverb_enabled(bool p_enabled) { reverb_enabled = p_enabled; }
bool SpatialAudioPlayer3D::is_reverb_enabled() const { return reverb_enabled; }
void SpatialAudioPlayer3D::set_reverb_ray_count(int p_count) {
	reverb_ray_count = CLAMP(p_count, 4, 64);
	_generate_reverb_directions();
}
int SpatialAudioPlayer3D::get_reverb_ray_count() const { return reverb_ray_count; }
void SpatialAudioPlayer3D::set_max_reverb_distance(real_t p_distance) { max_reverb_distance = MAX(1.0, p_distance); }
real_t SpatialAudioPlayer3D::get_max_reverb_distance() const { return max_reverb_distance; }
void SpatialAudioPlayer3D::set_max_reverb_wetness(real_t p_wetness) { max_reverb_wetness = CLAMP(p_wetness, 0.0, 1.0); }
real_t SpatialAudioPlayer3D::get_max_reverb_wetness() const { return max_reverb_wetness; }

void SpatialAudioPlayer3D::set_air_absorption_enabled(bool p_enabled) { air_absorption_enabled = p_enabled; }
bool SpatialAudioPlayer3D::is_air_absorption_enabled() const { return air_absorption_enabled; }

void SpatialAudioPlayer3D::set_water_muffle_enabled(bool p_enabled) { water_muffle_enabled = p_enabled; }
bool SpatialAudioPlayer3D::is_water_muffle_enabled() const { return water_muffle_enabled; }
void SpatialAudioPlayer3D::set_water_lowpass_cutoff(real_t p_hz) { water_lowpass_cutoff = CLAMP(p_hz, 100.0, 5000.0); }
real_t SpatialAudioPlayer3D::get_water_lowpass_cutoff() const { return water_lowpass_cutoff; }

int SpatialAudioPlayer3D::get_current_wall_count() const { return current_wall_count; }
real_t SpatialAudioPlayer3D::get_current_room_size() const { return current_room_size; }
bool SpatialAudioPlayer3D::is_listener_submerged() const { return listener_submerged; }
bool SpatialAudioPlayer3D::is_emitter_submerged() const { return emitter_submerged; }

// ===========================================================
//  Binding
// ===========================================================

void SpatialAudioPlayer3D::_bind_methods() {
	// Occlusion.
	ClassDB::bind_method(D_METHOD("set_occlusion_enabled", "enabled"), &SpatialAudioPlayer3D::set_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("is_occlusion_enabled"), &SpatialAudioPlayer3D::is_occlusion_enabled);
	ClassDB::bind_method(D_METHOD("set_max_occlusion_rays", "rays"), &SpatialAudioPlayer3D::set_max_occlusion_rays);
	ClassDB::bind_method(D_METHOD("get_max_occlusion_rays"), &SpatialAudioPlayer3D::get_max_occlusion_rays);
	ClassDB::bind_method(D_METHOD("set_occlusion_lowpass_min", "hz"), &SpatialAudioPlayer3D::set_occlusion_lowpass_min);
	ClassDB::bind_method(D_METHOD("get_occlusion_lowpass_min"), &SpatialAudioPlayer3D::get_occlusion_lowpass_min);
	ClassDB::bind_method(D_METHOD("set_occlusion_volume_reduction", "db"), &SpatialAudioPlayer3D::set_occlusion_volume_reduction);
	ClassDB::bind_method(D_METHOD("get_occlusion_volume_reduction"), &SpatialAudioPlayer3D::get_occlusion_volume_reduction);
	ClassDB::bind_method(D_METHOD("set_occlusion_mask", "mask"), &SpatialAudioPlayer3D::set_occlusion_mask);
	ClassDB::bind_method(D_METHOD("get_occlusion_mask"), &SpatialAudioPlayer3D::get_occlusion_mask);

	// Reverb.
	ClassDB::bind_method(D_METHOD("set_reverb_enabled", "enabled"), &SpatialAudioPlayer3D::set_reverb_enabled);
	ClassDB::bind_method(D_METHOD("is_reverb_enabled"), &SpatialAudioPlayer3D::is_reverb_enabled);
	ClassDB::bind_method(D_METHOD("set_reverb_ray_count", "count"), &SpatialAudioPlayer3D::set_reverb_ray_count);
	ClassDB::bind_method(D_METHOD("get_reverb_ray_count"), &SpatialAudioPlayer3D::get_reverb_ray_count);
	ClassDB::bind_method(D_METHOD("set_max_reverb_distance", "distance"), &SpatialAudioPlayer3D::set_max_reverb_distance);
	ClassDB::bind_method(D_METHOD("get_max_reverb_distance"), &SpatialAudioPlayer3D::get_max_reverb_distance);
	ClassDB::bind_method(D_METHOD("set_max_reverb_wetness", "wetness"), &SpatialAudioPlayer3D::set_max_reverb_wetness);
	ClassDB::bind_method(D_METHOD("get_max_reverb_wetness"), &SpatialAudioPlayer3D::get_max_reverb_wetness);

	// Air absorption.
	ClassDB::bind_method(D_METHOD("set_air_absorption_enabled", "enabled"), &SpatialAudioPlayer3D::set_air_absorption_enabled);
	ClassDB::bind_method(D_METHOD("is_air_absorption_enabled"), &SpatialAudioPlayer3D::is_air_absorption_enabled);

	// Water.
	ClassDB::bind_method(D_METHOD("set_water_muffle_enabled", "enabled"), &SpatialAudioPlayer3D::set_water_muffle_enabled);
	ClassDB::bind_method(D_METHOD("is_water_muffle_enabled"), &SpatialAudioPlayer3D::is_water_muffle_enabled);
	ClassDB::bind_method(D_METHOD("set_water_lowpass_cutoff", "hz"), &SpatialAudioPlayer3D::set_water_lowpass_cutoff);
	ClassDB::bind_method(D_METHOD("get_water_lowpass_cutoff"), &SpatialAudioPlayer3D::get_water_lowpass_cutoff);

	// State queries.
	ClassDB::bind_method(D_METHOD("get_current_wall_count"), &SpatialAudioPlayer3D::get_current_wall_count);
	ClassDB::bind_method(D_METHOD("get_current_room_size"), &SpatialAudioPlayer3D::get_current_room_size);
	ClassDB::bind_method(D_METHOD("is_listener_submerged"), &SpatialAudioPlayer3D::is_listener_submerged);
	ClassDB::bind_method(D_METHOD("is_emitter_submerged"), &SpatialAudioPlayer3D::is_emitter_submerged);

	// Properties.
	ADD_GROUP("Occlusion", "occlusion_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "occlusion_enabled"), "set_occlusion_enabled", "is_occlusion_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "occlusion_max_rays", PROPERTY_HINT_RANGE, "1,10,1"), "set_max_occlusion_rays", "get_max_occlusion_rays");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_lowpass_min", PROPERTY_HINT_RANGE, "100,5000,10,suffix:Hz"), "set_occlusion_lowpass_min", "get_occlusion_lowpass_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_volume_reduction", PROPERTY_HINT_RANGE, "0,40,0.5,suffix:dB"), "set_occlusion_volume_reduction", "get_occlusion_volume_reduction");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "occlusion_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_occlusion_mask", "get_occlusion_mask");

	ADD_GROUP("Reverb", "reverb_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "reverb_enabled"), "set_reverb_enabled", "is_reverb_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reverb_ray_count", PROPERTY_HINT_RANGE, "4,64,1"), "set_reverb_ray_count", "get_reverb_ray_count");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_max_distance", PROPERTY_HINT_RANGE, "1,200,0.5,suffix:m"), "set_max_reverb_distance", "get_max_reverb_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_max_wetness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_max_reverb_wetness", "get_max_reverb_wetness");

	ADD_GROUP("Air Absorption", "air_absorption_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "air_absorption_enabled"), "set_air_absorption_enabled", "is_air_absorption_enabled");

	ADD_GROUP("Water", "water_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "water_muffle_enabled"), "set_water_muffle_enabled", "is_water_muffle_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "water_lowpass_cutoff", PROPERTY_HINT_RANGE, "100,5000,10,suffix:Hz"), "set_water_lowpass_cutoff", "get_water_lowpass_cutoff");

	BIND_ENUM_CONSTANT(ATTENUATION_LINEAR);
	BIND_ENUM_CONSTANT(ATTENUATION_LOGARITHMIC);
	BIND_ENUM_CONSTANT(ATTENUATION_INVERSE);
	BIND_ENUM_CONSTANT(ATTENUATION_NATURAL);
}

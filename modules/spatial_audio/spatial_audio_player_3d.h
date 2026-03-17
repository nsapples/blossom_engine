/**************************************************************************/
/*  spatial_audio_player_3d.h                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "scene/3d/audio_stream_player_3d.h"

class PhysicsMaterial;
class PhysicsDirectSpaceState3D;

class SpatialAudioPlayer3D : public AudioStreamPlayer3D {
	GDCLASS(SpatialAudioPlayer3D, AudioStreamPlayer3D);

public:
	enum AttenuationMode {
		ATTENUATION_LINEAR,
		ATTENUATION_LOGARITHMIC,
		ATTENUATION_INVERSE,
		ATTENUATION_NATURAL,
	};

private:
	// Occlusion.
	bool occlusion_enabled = true;
	int max_occlusion_rays = 3;
	real_t occlusion_lowpass_min = 800.0; // Hz cutoff when fully occluded.
	real_t occlusion_volume_reduction = 12.0; // dB reduction per wall.
	real_t occlusion_smoothing = 8.0;
	uint32_t occlusion_mask = 1;

	// Current occlusion state.
	int current_wall_count = 0;
	real_t current_occlusion_factor = 0.0; // 0 = clear, 1 = fully occluded.
	real_t target_occlusion_factor = 0.0;

	// Reverb estimation.
	bool reverb_enabled = true;
	int reverb_ray_count = 10;
	real_t max_reverb_distance = 50.0;
	real_t max_reverb_wetness = 0.5;
	real_t reverb_smoothing = 4.0;

	// Current reverb state.
	real_t current_room_size = 0.0;
	real_t current_wetness = 0.0;
	real_t current_damping = 0.0;
	real_t target_room_size = 0.0;
	real_t target_wetness = 0.0;
	real_t target_damping = 0.0;

	// Air absorption.
	bool air_absorption_enabled = true;
	real_t air_absorption_max_distance = 100.0;
	real_t air_absorption_min_cutoff = 2000.0; // Hz at max distance.

	// Water/submersion.
	bool water_muffle_enabled = true;
	real_t water_lowpass_cutoff = 400.0; // Hz when underwater.
	real_t water_volume_reduction = 8.0; // dB.

	// Internal state.
	real_t current_lowpass = 20500.0;
	real_t current_volume_offset = 0.0;
	bool listener_submerged = false;
	bool emitter_submerged = false;
	int update_frame = 0;
	int update_interval = 2; // Process every N physics frames.

	// Audio effect references (created internally).
	int bus_index = -1;
	String dedicated_bus_name;
	bool bus_created = false;

	// Fibonacci sphere directions for reverb rays.
	Vector<Vector3> reverb_directions;

	void _setup_audio_bus();
	void _cleanup_audio_bus();
	void _generate_reverb_directions();

	void _update_occlusion(PhysicsDirectSpaceState3D *p_space, const Vector3 &p_listener_pos, real_t p_delta);
	void _update_reverb(PhysicsDirectSpaceState3D *p_space, real_t p_delta);
	void _update_air_absorption(real_t p_distance);
	void _update_water_state(PhysicsDirectSpaceState3D *p_space, const Vector3 &p_listener_pos);
	void _apply_effects(real_t p_delta);

	real_t _get_material_transmission(const Ref<PhysicsMaterial> &p_material) const;
	real_t _get_material_reflection(const Ref<PhysicsMaterial> &p_material) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Occlusion.
	void set_occlusion_enabled(bool p_enabled);
	bool is_occlusion_enabled() const;
	void set_max_occlusion_rays(int p_rays);
	int get_max_occlusion_rays() const;
	void set_occlusion_lowpass_min(real_t p_hz);
	real_t get_occlusion_lowpass_min() const;
	void set_occlusion_volume_reduction(real_t p_db);
	real_t get_occlusion_volume_reduction() const;
	void set_occlusion_mask(uint32_t p_mask);
	uint32_t get_occlusion_mask() const;

	// Reverb.
	void set_reverb_enabled(bool p_enabled);
	bool is_reverb_enabled() const;
	void set_reverb_ray_count(int p_count);
	int get_reverb_ray_count() const;
	void set_max_reverb_distance(real_t p_distance);
	real_t get_max_reverb_distance() const;
	void set_max_reverb_wetness(real_t p_wetness);
	real_t get_max_reverb_wetness() const;

	// Air absorption.
	void set_air_absorption_enabled(bool p_enabled);
	bool is_air_absorption_enabled() const;

	// Water.
	void set_water_muffle_enabled(bool p_enabled);
	bool is_water_muffle_enabled() const;
	void set_water_lowpass_cutoff(real_t p_hz);
	real_t get_water_lowpass_cutoff() const;

	// State queries.
	int get_current_wall_count() const;
	real_t get_current_room_size() const;
	bool is_listener_submerged() const;
	bool is_emitter_submerged() const;

	SpatialAudioPlayer3D();
	~SpatialAudioPlayer3D();
};

VARIANT_ENUM_CAST(SpatialAudioPlayer3D::AttenuationMode);

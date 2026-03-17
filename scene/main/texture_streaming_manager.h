/**************************************************************************/
/*  texture_streaming_manager.h                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "scene/resources/texture.h"

class Camera3D;

class TextureStreamingManager : public Object {
	GDCLASS(TextureStreamingManager, Object);

public:
	enum StreamingQuality {
		QUALITY_LOWEST, // Smallest mip only.
		QUALITY_LOW,
		QUALITY_MEDIUM,
		QUALITY_HIGH,
		QUALITY_FULL, // Full resolution.
	};

private:
	static TextureStreamingManager *singleton;

	struct StreamedTexture {
		RID texture_rid;
		String source_path;
		int full_width = 0;
		int full_height = 0;
		int total_mipmaps = 0;
		Image::Format format = Image::FORMAT_L8;

		StreamingQuality current_quality = QUALITY_LOWEST;
		StreamingQuality desired_quality = QUALITY_LOWEST;
		real_t min_distance = INFINITY; // Closest distance to any camera this frame.
		bool loading = false;
		uint64_t last_used_frame = 0;
		uint64_t vram_usage = 0;
	};

	HashMap<RID, StreamedTexture> tracked_textures;
	Mutex mutex;

	// Settings.
	uint64_t vram_budget_bytes = 512 * 1024 * 1024; // 512 MB default.
	uint64_t current_vram_usage = 0;
	real_t distance_bias = 1.0;
	real_t hysteresis = 0.2; // Prevents rapid quality flipping.
	bool enabled = true;
	int max_loads_per_frame = 2;

	// Distance thresholds for quality levels.
	real_t distance_full = 10.0;
	real_t distance_high = 30.0;
	real_t distance_medium = 60.0;
	real_t distance_low = 120.0;

	uint64_t frame_count = 0;
	uint64_t eviction_age = 300; // Frames before an unseen texture is evicted.

	void _update_desired_qualities();
	void _process_load_queue();
	void _evict_textures();
	void _load_texture_quality(RID p_texture_rid, StreamingQuality p_quality);
	StreamingQuality _distance_to_quality(real_t p_distance) const;
	uint64_t _estimate_vram(int p_width, int p_height, Image::Format p_format, int p_mip_levels) const;
	int _quality_to_mip_skip(StreamingQuality p_quality, int p_total_mipmaps) const;

protected:
	static void _bind_methods();

public:
	static TextureStreamingManager *get_singleton();

	void register_texture(RID p_texture_rid, const String &p_source_path, int p_width, int p_height, int p_mipmaps, Image::Format p_format);
	void unregister_texture(RID p_texture_rid);
	void report_texture_distance(RID p_texture_rid, real_t p_distance);

	void process(real_t p_delta);

	// Settings.
	void set_vram_budget_mb(int p_mb);
	int get_vram_budget_mb() const;

	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	void set_distance_bias(real_t p_bias);
	real_t get_distance_bias() const;

	void set_distance_full(real_t p_distance);
	real_t get_distance_full() const;
	void set_distance_high(real_t p_distance);
	real_t get_distance_high() const;
	void set_distance_medium(real_t p_distance);
	real_t get_distance_medium() const;
	void set_distance_low(real_t p_distance);
	real_t get_distance_low() const;

	void set_max_loads_per_frame(int p_count);
	int get_max_loads_per_frame() const;

	// Debug info.
	int get_tracked_texture_count() const;
	uint64_t get_current_vram_usage() const;
	Dictionary get_streaming_stats() const;

	TextureStreamingManager();
	~TextureStreamingManager();
};

VARIANT_ENUM_CAST(TextureStreamingManager::StreamingQuality);

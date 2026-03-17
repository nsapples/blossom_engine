/**************************************************************************/
/*  texture_streaming_manager.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "texture_streaming_manager.h"

#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "scene/resources/compressed_texture.h"
#include "servers/rendering_server.h"

TextureStreamingManager *TextureStreamingManager::singleton = nullptr;

TextureStreamingManager *TextureStreamingManager::get_singleton() {
	return singleton;
}

void TextureStreamingManager::register_texture(RID p_texture_rid, const String &p_source_path, int p_width, int p_height, int p_mipmaps, Image::Format p_format) {
	MutexLock lock(mutex);

	if (tracked_textures.has(p_texture_rid)) {
		return;
	}

	StreamedTexture st;
	st.texture_rid = p_texture_rid;
	st.source_path = p_source_path;
	st.full_width = p_width;
	st.full_height = p_height;
	st.total_mipmaps = p_mipmaps;
	st.format = p_format;
	st.current_quality = QUALITY_FULL; // Starts loaded at full res.
	st.desired_quality = QUALITY_FULL;
	st.last_used_frame = frame_count;
	st.vram_usage = _estimate_vram(p_width, p_height, p_format, p_mipmaps);
	current_vram_usage += st.vram_usage;

	tracked_textures.insert(p_texture_rid, st);
}

void TextureStreamingManager::unregister_texture(RID p_texture_rid) {
	MutexLock lock(mutex);

	if (!tracked_textures.has(p_texture_rid)) {
		return;
	}

	current_vram_usage -= tracked_textures[p_texture_rid].vram_usage;
	tracked_textures.erase(p_texture_rid);
}

void TextureStreamingManager::report_texture_distance(RID p_texture_rid, real_t p_distance) {
	MutexLock lock(mutex);

	if (!tracked_textures.has(p_texture_rid)) {
		return;
	}

	StreamedTexture &st = tracked_textures[p_texture_rid];
	st.min_distance = MIN(st.min_distance, p_distance * distance_bias);
	st.last_used_frame = frame_count;
}

TextureStreamingManager::StreamingQuality TextureStreamingManager::_distance_to_quality(real_t p_distance) const {
	if (p_distance <= distance_full) {
		return QUALITY_FULL;
	} else if (p_distance <= distance_high) {
		return QUALITY_HIGH;
	} else if (p_distance <= distance_medium) {
		return QUALITY_MEDIUM;
	} else if (p_distance <= distance_low) {
		return QUALITY_LOW;
	}
	return QUALITY_LOWEST;
}

int TextureStreamingManager::_quality_to_mip_skip(StreamingQuality p_quality, int p_total_mipmaps) const {
	if (p_total_mipmaps <= 1) {
		return 0;
	}

	switch (p_quality) {
		case QUALITY_FULL:
			return 0;
		case QUALITY_HIGH:
			return MIN(1, p_total_mipmaps - 1);
		case QUALITY_MEDIUM:
			return MIN(2, p_total_mipmaps - 1);
		case QUALITY_LOW:
			return MIN(3, p_total_mipmaps - 1);
		case QUALITY_LOWEST:
			return MIN(4, p_total_mipmaps - 1);
		default:
			return 0;
	}
}

uint64_t TextureStreamingManager::_estimate_vram(int p_width, int p_height, Image::Format p_format, int p_mip_levels) const {
	uint64_t total = 0;
	int w = p_width;
	int h = p_height;
	int pixel_size = Image::get_format_pixel_size(p_format);
	bool is_compressed = Image::is_format_compressed(p_format);

	for (int i = 0; i < p_mip_levels; i++) {
		if (is_compressed) {
			int bw = MAX(1, (w + 3) / 4);
			int bh = MAX(1, (h + 3) / 4);
			total += bw * bh * pixel_size;
		} else {
			total += w * h * pixel_size;
		}
		w = MAX(1, w >> 1);
		h = MAX(1, h >> 1);
	}
	return total;
}

void TextureStreamingManager::_update_desired_qualities() {
	for (KeyValue<RID, StreamedTexture> &kv : tracked_textures) {
		StreamedTexture &st = kv.value;

		if (st.min_distance == Math_INF) {
			// Not visible this frame — mark for potential eviction but don't change quality yet.
			continue;
		}

		StreamingQuality desired = _distance_to_quality(st.min_distance);

		// Apply hysteresis to prevent rapid switching.
		if (desired < st.current_quality) {
			// Upgrading — apply immediately.
			st.desired_quality = desired;
		} else if (desired > st.current_quality) {
			// Downgrading — require the distance to exceed threshold by hysteresis factor.
			real_t threshold = st.min_distance * (1.0 - hysteresis);
			StreamingQuality conservative = _distance_to_quality(threshold);
			if (conservative > st.current_quality) {
				st.desired_quality = desired;
			}
		}

		// Reset distance for next frame.
		st.min_distance = Math_INF;
	}
}

void TextureStreamingManager::_process_load_queue() {
	int loads_this_frame = 0;

	// First pass: upgrade textures that need higher quality.
	for (KeyValue<RID, StreamedTexture> &kv : tracked_textures) {
		if (loads_this_frame >= max_loads_per_frame) {
			break;
		}

		StreamedTexture &st = kv.value;
		if (st.desired_quality < st.current_quality && !st.loading) {
			_load_texture_quality(kv.key, st.desired_quality);
			loads_this_frame++;
		}
	}

	// Second pass: downgrade textures to free VRAM if over budget.
	if (current_vram_usage > vram_budget_bytes) {
		for (KeyValue<RID, StreamedTexture> &kv : tracked_textures) {
			if (current_vram_usage <= vram_budget_bytes) {
				break;
			}

			StreamedTexture &st = kv.value;
			if (st.desired_quality > st.current_quality && !st.loading) {
				_load_texture_quality(kv.key, st.desired_quality);
				loads_this_frame++;
			}
		}
	}
}

void TextureStreamingManager::_evict_textures() {
	for (KeyValue<RID, StreamedTexture> &kv : tracked_textures) {
		StreamedTexture &st = kv.value;
		uint64_t age = frame_count - st.last_used_frame;

		if (age > eviction_age && st.current_quality != QUALITY_LOWEST) {
			st.desired_quality = QUALITY_LOWEST;
		}
	}
}

void TextureStreamingManager::_load_texture_quality(RID p_texture_rid, StreamingQuality p_quality) {
	if (!tracked_textures.has(p_texture_rid)) {
		return;
	}

	StreamedTexture &st = tracked_textures[p_texture_rid];
	if (st.source_path.is_empty()) {
		return;
	}

	st.loading = true;

	int mip_skip = _quality_to_mip_skip(p_quality, st.total_mipmaps);
	int target_width = MAX(1, st.full_width >> mip_skip);
	int target_height = MAX(1, st.full_height >> mip_skip);
	int target_mipmaps = MAX(1, st.total_mipmaps - mip_skip);

	// Load the image from file at the target size.
	Ref<Image> image;
	image.instantiate();

	Error err = image->load(st.source_path);
	if (err != OK) {
		st.loading = false;
		return;
	}

	// Resize if we need a smaller version.
	if (mip_skip > 0) {
		image->resize(target_width, target_height, Image::INTERPOLATE_LANCZOS);
	}

	if (image->get_mipmap_count() == 0 && target_mipmaps > 1) {
		image->generate_mipmaps();
	}

	// Update the GPU texture.
	RenderingServer::get_singleton()->texture_2d_update(p_texture_rid, image);

	// Update VRAM tracking.
	uint64_t old_usage = st.vram_usage;
	st.vram_usage = _estimate_vram(target_width, target_height, st.format, target_mipmaps);
	current_vram_usage = current_vram_usage - old_usage + st.vram_usage;

	st.current_quality = p_quality;
	st.loading = false;
}

void TextureStreamingManager::process(real_t p_delta) {
	if (!enabled) {
		return;
	}

	MutexLock lock(mutex);
	frame_count++;

	_update_desired_qualities();
	_evict_textures();
	_process_load_queue();
}

// Settings.

void TextureStreamingManager::set_vram_budget_mb(int p_mb) {
	vram_budget_bytes = (uint64_t)p_mb * 1024 * 1024;
}

int TextureStreamingManager::get_vram_budget_mb() const {
	return (int)(vram_budget_bytes / (1024 * 1024));
}

void TextureStreamingManager::set_enabled(bool p_enabled) {
	enabled = p_enabled;
}

bool TextureStreamingManager::is_enabled() const {
	return enabled;
}

void TextureStreamingManager::set_distance_bias(real_t p_bias) {
	distance_bias = p_bias;
}

real_t TextureStreamingManager::get_distance_bias() const {
	return distance_bias;
}

void TextureStreamingManager::set_distance_full(real_t p_distance) {
	distance_full = p_distance;
}

real_t TextureStreamingManager::get_distance_full() const {
	return distance_full;
}

void TextureStreamingManager::set_distance_high(real_t p_distance) {
	distance_high = p_distance;
}

real_t TextureStreamingManager::get_distance_high() const {
	return distance_high;
}

void TextureStreamingManager::set_distance_medium(real_t p_distance) {
	distance_medium = p_distance;
}

real_t TextureStreamingManager::get_distance_medium() const {
	return distance_medium;
}

void TextureStreamingManager::set_distance_low(real_t p_distance) {
	distance_low = p_distance;
}

real_t TextureStreamingManager::get_distance_low() const {
	return distance_low;
}

void TextureStreamingManager::set_max_loads_per_frame(int p_count) {
	max_loads_per_frame = MAX(1, p_count);
}

int TextureStreamingManager::get_max_loads_per_frame() const {
	return max_loads_per_frame;
}

// Debug info.

int TextureStreamingManager::get_tracked_texture_count() const {
	return tracked_textures.size();
}

uint64_t TextureStreamingManager::get_current_vram_usage() const {
	return current_vram_usage;
}

Dictionary TextureStreamingManager::get_streaming_stats() const {
	Dictionary stats;
	stats["tracked_textures"] = get_tracked_texture_count();
	stats["vram_usage_mb"] = (real_t)current_vram_usage / (1024.0 * 1024.0);
	stats["vram_budget_mb"] = get_vram_budget_mb();
	stats["enabled"] = enabled;

	int count_full = 0, count_high = 0, count_medium = 0, count_low = 0, count_lowest = 0;
	for (const KeyValue<RID, StreamedTexture> &kv : tracked_textures) {
		switch (kv.value.current_quality) {
			case QUALITY_FULL:
				count_full++;
				break;
			case QUALITY_HIGH:
				count_high++;
				break;
			case QUALITY_MEDIUM:
				count_medium++;
				break;
			case QUALITY_LOW:
				count_low++;
				break;
			case QUALITY_LOWEST:
				count_lowest++;
				break;
		}
	}
	stats["quality_full"] = count_full;
	stats["quality_high"] = count_high;
	stats["quality_medium"] = count_medium;
	stats["quality_low"] = count_low;
	stats["quality_lowest"] = count_lowest;

	return stats;
}

void TextureStreamingManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_texture", "texture_rid", "source_path", "width", "height", "mipmaps", "format"), &TextureStreamingManager::register_texture);
	ClassDB::bind_method(D_METHOD("unregister_texture", "texture_rid"), &TextureStreamingManager::unregister_texture);
	ClassDB::bind_method(D_METHOD("report_texture_distance", "texture_rid", "distance"), &TextureStreamingManager::report_texture_distance);
	ClassDB::bind_method(D_METHOD("process", "delta"), &TextureStreamingManager::process);

	ClassDB::bind_method(D_METHOD("set_vram_budget_mb", "mb"), &TextureStreamingManager::set_vram_budget_mb);
	ClassDB::bind_method(D_METHOD("get_vram_budget_mb"), &TextureStreamingManager::get_vram_budget_mb);

	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &TextureStreamingManager::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &TextureStreamingManager::is_enabled);

	ClassDB::bind_method(D_METHOD("set_distance_bias", "bias"), &TextureStreamingManager::set_distance_bias);
	ClassDB::bind_method(D_METHOD("get_distance_bias"), &TextureStreamingManager::get_distance_bias);

	ClassDB::bind_method(D_METHOD("set_distance_full", "distance"), &TextureStreamingManager::set_distance_full);
	ClassDB::bind_method(D_METHOD("get_distance_full"), &TextureStreamingManager::get_distance_full);
	ClassDB::bind_method(D_METHOD("set_distance_high", "distance"), &TextureStreamingManager::set_distance_high);
	ClassDB::bind_method(D_METHOD("get_distance_high"), &TextureStreamingManager::get_distance_high);
	ClassDB::bind_method(D_METHOD("set_distance_medium", "distance"), &TextureStreamingManager::set_distance_medium);
	ClassDB::bind_method(D_METHOD("get_distance_medium"), &TextureStreamingManager::get_distance_medium);
	ClassDB::bind_method(D_METHOD("set_distance_low", "distance"), &TextureStreamingManager::set_distance_low);
	ClassDB::bind_method(D_METHOD("get_distance_low"), &TextureStreamingManager::get_distance_low);

	ClassDB::bind_method(D_METHOD("set_max_loads_per_frame", "count"), &TextureStreamingManager::set_max_loads_per_frame);
	ClassDB::bind_method(D_METHOD("get_max_loads_per_frame"), &TextureStreamingManager::get_max_loads_per_frame);

	ClassDB::bind_method(D_METHOD("get_tracked_texture_count"), &TextureStreamingManager::get_tracked_texture_count);
	ClassDB::bind_method(D_METHOD("get_current_vram_usage"), &TextureStreamingManager::get_current_vram_usage);
	ClassDB::bind_method(D_METHOD("get_streaming_stats"), &TextureStreamingManager::get_streaming_stats);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "vram_budget_mb", PROPERTY_HINT_RANGE, "64,4096,64"), "set_vram_budget_mb", "get_vram_budget_mb");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_bias", PROPERTY_HINT_RANGE, "0.1,10,0.1"), "set_distance_bias", "get_distance_bias");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_loads_per_frame", PROPERTY_HINT_RANGE, "1,16,1"), "set_max_loads_per_frame", "get_max_loads_per_frame");

	ADD_GROUP("Distance Thresholds", "distance_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_full", PROPERTY_HINT_RANGE, "1,100,0.5"), "set_distance_full", "get_distance_full");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_high", PROPERTY_HINT_RANGE, "5,200,0.5"), "set_distance_high", "get_distance_high");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_medium", PROPERTY_HINT_RANGE, "10,500,1"), "set_distance_medium", "get_distance_medium");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_low", PROPERTY_HINT_RANGE, "20,1000,1"), "set_distance_low", "get_distance_low");

	BIND_ENUM_CONSTANT(QUALITY_LOWEST);
	BIND_ENUM_CONSTANT(QUALITY_LOW);
	BIND_ENUM_CONSTANT(QUALITY_MEDIUM);
	BIND_ENUM_CONSTANT(QUALITY_HIGH);
	BIND_ENUM_CONSTANT(QUALITY_FULL);
}

TextureStreamingManager::TextureStreamingManager() {
	singleton = this;
}

TextureStreamingManager::~TextureStreamingManager() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

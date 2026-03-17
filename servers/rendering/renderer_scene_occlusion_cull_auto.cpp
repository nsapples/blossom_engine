/**************************************************************************/
/*  renderer_scene_occlusion_cull_auto.cpp                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "renderer_scene_occlusion_cull_auto.h"

// AutoOcclusionCullHZBuffer

void AutoOcclusionCullHZBuffer::update_from_gpu_depth(const Vector<float> &p_depth_data, const Size2i &p_size) {
	if (p_depth_data.is_empty() || p_size.x <= 0 || p_size.y <= 0) {
		has_gpu_depth = false;
		return;
	}

	// Resize the HZBuffer if needed.
	if (occlusion_buffer_size != p_size) {
		resize(p_size);
	}

	// Copy depth data into the base mip level.
	int pixel_count = p_size.x * p_size.y;
	ERR_FAIL_COND(p_depth_data.size() < pixel_count);
	ERR_FAIL_COND(data.size() < (uint32_t)pixel_count);

	const float *src = p_depth_data.ptr();
	float *dst = data.ptr();
	memcpy(dst, src, pixel_count * sizeof(float));

	// Generate mip chain for hierarchical testing.
	update_mips();

	has_gpu_depth = true;
	occlusion_frame++;
}

// RendererSceneAutoOcclusionCull

RendererSceneAutoOcclusionCull *RendererSceneAutoOcclusionCull::singleton = nullptr;

RendererSceneAutoOcclusionCull *RendererSceneAutoOcclusionCull::get_singleton() {
	return singleton;
}

void RendererSceneAutoOcclusionCull::set_enabled(bool p_enabled) {
	enabled = p_enabled;
}

bool RendererSceneAutoOcclusionCull::is_enabled() const {
	return enabled;
}

void RendererSceneAutoOcclusionCull::submit_depth_buffer(const Vector<float> &p_depth_data, const Size2i &p_size) {
	if (!enabled) {
		return;
	}
	pending_depth_data = p_depth_data;
	pending_depth_size = p_size;
	pending_depth_available = true;
}

void RendererSceneAutoOcclusionCull::_on_depth_readback(const Vector<uint8_t> &p_data, const Size2i &p_size) {
	if (!enabled) {
		return;
	}

	int pixel_count = p_size.x * p_size.y;
	if (p_data.is_empty() || pixel_count == 0) {
		return;
	}

	// Convert raw depth bytes to float array.
	// Depth buffer is typically 32-bit float or 24-bit normalized.
	// Assume 32-bit float depth (most common with Vulkan).
	Vector<float> depth_floats;
	if (p_data.size() >= pixel_count * (int)sizeof(float)) {
		// 32-bit float depth.
		depth_floats.resize(pixel_count);
		memcpy(depth_floats.ptrw(), p_data.ptr(), pixel_count * sizeof(float));
	} else if (p_data.size() >= pixel_count * 4) {
		// RGBA8 or similar — interpret first byte as depth.
		depth_floats.resize(pixel_count);
		float *dst = depth_floats.ptrw();
		const uint8_t *src = p_data.ptr();
		for (int i = 0; i < pixel_count; i++) {
			dst[i] = float(src[i * 4]) / 255.0f;
		}
	} else {
		return;
	}

	// Convert from Vulkan reverse-Z (1=near, 0=far) to forward depth (0=near, far=big).
	// The HZBuffer expects: larger depth = further away.
	float *ptr = depth_floats.ptrw();
	for (int i = 0; i < pixel_count; i++) {
		float d = ptr[i];
		if (d <= 0.0001f) {
			ptr[i] = 1e10f; // Far plane, treat as very distant.
		} else {
			ptr[i] = 1.0f / d; // Linearize reverse-Z.
		}
	}

	submit_depth_buffer(depth_floats, p_size);
}

RendererSceneOcclusionCull::HZBuffer *RendererSceneAutoOcclusionCull::get_buffer() {
	if (!enabled || !pending_depth_available) {
		return nullptr;
	}

	// Apply the previous frame's depth data.
	auto_buffer.update_from_gpu_depth(pending_depth_data, pending_depth_size);
	pending_depth_available = false;

	return &auto_buffer;
}

RendererSceneAutoOcclusionCull::RendererSceneAutoOcclusionCull() {
	singleton = this;
}

RendererSceneAutoOcclusionCull::~RendererSceneAutoOcclusionCull() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

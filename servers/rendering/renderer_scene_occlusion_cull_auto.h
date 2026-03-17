/**************************************************************************/
/*  renderer_scene_occlusion_cull_auto.h                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "servers/rendering/renderer_scene_occlusion_cull.h"

// Extends the HZBuffer to accept depth data from the GPU depth pre-pass.
// This allows automatic occlusion culling without manually-placed occluders.
// Uses 1-frame-delayed depth from the previous frame's depth pre-pass.

class AutoOcclusionCullHZBuffer : public RendererSceneOcclusionCull::HZBuffer {
	bool has_gpu_depth = false;
	Size2i gpu_depth_size;

public:
	// Feed the GPU depth buffer into the HZBuffer.
	// Called after the depth pre-pass completes.
	// The depth data is linearized depth values (0 = near, increasing with distance).
	void update_from_gpu_depth(const Vector<float> &p_depth_data, const Size2i &p_size);

	// Check if GPU depth data is available.
	bool has_gpu_depth_data() const { return has_gpu_depth; }
};

// Singleton wrapper that provides automatic occlusion culling
// by feeding GPU depth data into the existing HZBuffer system.

class RendererSceneAutoOcclusionCull {
	static RendererSceneAutoOcclusionCull *singleton;

	bool enabled = false;
	AutoOcclusionCullHZBuffer auto_buffer;

	// Store depth readback from previous frame.
	Vector<float> pending_depth_data;
	Size2i pending_depth_size;
	bool pending_depth_available = false;

public:
	static RendererSceneAutoOcclusionCull *get_singleton();

	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	// Called by the renderer after depth pre-pass to queue depth data.
	void submit_depth_buffer(const Vector<float> &p_depth_data, const Size2i &p_size);

	// Called by async depth readback. Converts raw depth bytes to linear floats.
	void _on_depth_readback(const Vector<uint8_t> &p_data, const Size2i &p_size);

	// Called before culling to apply the previous frame's depth.
	// Returns the HZBuffer pointer if auto occlusion is active, otherwise nullptr.
	RendererSceneOcclusionCull::HZBuffer *get_buffer();

	RendererSceneAutoOcclusionCull();
	~RendererSceneAutoOcclusionCull();
};

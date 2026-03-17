/**************************************************************************/
/*  openxr_adaptive_rendering.h                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "../openxr_interface.h"
#include "core/templates/rid.h"
#include "openxr_extension_wrapper.h"

class OpenXRAdaptiveRendering : public OpenXRExtensionWrapper {
	GDCLASS(OpenXRAdaptiveRendering, OpenXRExtensionWrapper);

public:
	static OpenXRAdaptiveRendering *get_singleton();

	OpenXRAdaptiveRendering();
	virtual ~OpenXRAdaptiveRendering() override;

	virtual void on_pre_render() override;
	virtual void on_post_draw_viewport(RID p_render_target) override;

	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	void set_min_render_scale(float p_scale);
	float get_min_render_scale() const;

	void set_max_render_scale(float p_scale);
	float get_max_render_scale() const;

	void set_scale_step(float p_step);
	float get_scale_step() const;

	float get_current_render_scale() const;
	float get_current_frame_time_ms() const;
	float get_target_frame_time_ms() const;

protected:
	static void _bind_methods();

private:
	static OpenXRAdaptiveRendering *singleton;

	bool enabled = false;

	// Frame timing.
	uint64_t pre_render_time_usec = 0;
	float frame_time_ema_ms = 0.0f;
	float ema_smoothing = 0.15f;

	// Quality state.
	float current_render_scale = 1.0f;
	float min_render_scale = 0.6f;
	float max_render_scale = 1.0f;
	float scale_step = 0.05f;

	int current_foveation_level = 0; // 0-3.

	// Thresholds (as fraction of frame budget).
	float overshoot_threshold = 0.9f;  // Start reducing at 90% of budget.
	float headroom_threshold = 0.75f;  // Start increasing at 75% of budget.

	// Cooldown to prevent oscillation.
	int cooldown_frames = 0;
	int cooldown_duration = 30; // ~333ms at 90fps.

	// Ramp-up requires sustained headroom.
	int headroom_streak = 0;
	int headroom_streak_required = 45; // ~500ms of sustained headroom before increasing.

	void _adapt_quality(float p_frame_time_ms, float p_budget_ms);
	void _reduce_quality();
	void _increase_quality();
	void _apply_render_scale();
	void _apply_foveation();
};

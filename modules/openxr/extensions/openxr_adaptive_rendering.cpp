/**************************************************************************/
/*  openxr_adaptive_rendering.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#include "openxr_adaptive_rendering.h"

#include "../openxr_interface.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "servers/xr/xr_server.h"

OpenXRAdaptiveRendering *OpenXRAdaptiveRendering::singleton = nullptr;

OpenXRAdaptiveRendering *OpenXRAdaptiveRendering::get_singleton() {
	return singleton;
}

OpenXRAdaptiveRendering::OpenXRAdaptiveRendering() {
	singleton = this;
	enabled = GLOBAL_GET("xr/openxr/adaptive_rendering/enabled");
}

OpenXRAdaptiveRendering::~OpenXRAdaptiveRendering() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void OpenXRAdaptiveRendering::on_pre_render() {
	if (!enabled) {
		return;
	}
	pre_render_time_usec = OS::get_singleton()->get_ticks_usec();
}

void OpenXRAdaptiveRendering::on_post_draw_viewport(RID p_render_target) {
	if (!enabled || pre_render_time_usec == 0) {
		return;
	}

	uint64_t now = OS::get_singleton()->get_ticks_usec();
	float frame_time_ms = (float)(now - pre_render_time_usec) / 1000.0f;

	// Exponential moving average.
	if (frame_time_ema_ms <= 0.0f) {
		frame_time_ema_ms = frame_time_ms;
	} else {
		frame_time_ema_ms = frame_time_ema_ms * (1.0f - ema_smoothing) + frame_time_ms * ema_smoothing;
	}

	// Get target frame time from display refresh rate.
	float target_fps = 90.0f;
	Ref<OpenXRInterface> xr_interface = XRServer::get_singleton()->get_primary_interface();
	if (xr_interface.is_valid()) {
		float display_refresh = xr_interface->get_display_refresh_rate();
		if (display_refresh > 0.0f) {
			target_fps = display_refresh;
		}
	}
	float budget_ms = 1000.0f / target_fps;

	_adapt_quality(frame_time_ema_ms, budget_ms);
}

void OpenXRAdaptiveRendering::_adapt_quality(float p_frame_time_ms, float p_budget_ms) {
	if (cooldown_frames > 0) {
		cooldown_frames--;
		return;
	}

	float usage = p_frame_time_ms / p_budget_ms;

	if (usage > overshoot_threshold) {
		// Over budget — reduce quality immediately.
		headroom_streak = 0;
		_reduce_quality();
		cooldown_frames = cooldown_duration;
	} else if (usage < headroom_threshold) {
		// Under budget — increase only after sustained headroom.
		headroom_streak++;
		if (headroom_streak >= headroom_streak_required) {
			_increase_quality();
			headroom_streak = 0;
			cooldown_frames = cooldown_duration;
		}
	} else {
		// In the acceptable range — reset streak.
		headroom_streak = 0;
	}
}

void OpenXRAdaptiveRendering::_reduce_quality() {
	// Priority order: foveation first (cheapest visual impact), then render scale.

	// 1. Increase foveation level.
	if (current_foveation_level < 3) {
		current_foveation_level++;
		_apply_foveation();
		return;
	}

	// 2. Reduce render scale.
	if (current_render_scale > min_render_scale) {
		current_render_scale = MAX(min_render_scale, current_render_scale - scale_step);
		_apply_render_scale();
		return;
	}
}

void OpenXRAdaptiveRendering::_increase_quality() {
	// Reverse priority: render scale first (most visual impact), then foveation.

	// 1. Increase render scale.
	if (current_render_scale < max_render_scale) {
		current_render_scale = MIN(max_render_scale, current_render_scale + scale_step);
		_apply_render_scale();
		return;
	}

	// 2. Decrease foveation level.
	if (current_foveation_level > 0) {
		current_foveation_level--;
		_apply_foveation();
		return;
	}
}

void OpenXRAdaptiveRendering::_apply_render_scale() {
	Ref<OpenXRInterface> xr_interface = XRServer::get_singleton()->get_primary_interface();
	if (xr_interface.is_valid()) {
		xr_interface->set_render_target_size_multiplier(current_render_scale);
	}
}

void OpenXRAdaptiveRendering::_apply_foveation() {
	Ref<OpenXRInterface> openxr_interface = XRServer::get_singleton()->get_primary_interface();
	if (openxr_interface.is_valid()) {
		openxr_interface->set_foveation_level(current_foveation_level);
	}
}

void OpenXRAdaptiveRendering::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (!enabled) {
		// Reset to max quality.
		current_render_scale = max_render_scale;
		current_foveation_level = 0;
		_apply_render_scale();
		_apply_foveation();
	}
}

bool OpenXRAdaptiveRendering::is_enabled() const {
	return enabled;
}

void OpenXRAdaptiveRendering::set_min_render_scale(float p_scale) {
	min_render_scale = CLAMP(p_scale, 0.3f, 1.0f);
}

float OpenXRAdaptiveRendering::get_min_render_scale() const {
	return min_render_scale;
}

void OpenXRAdaptiveRendering::set_max_render_scale(float p_scale) {
	max_render_scale = CLAMP(p_scale, 0.5f, 2.0f);
}

float OpenXRAdaptiveRendering::get_max_render_scale() const {
	return max_render_scale;
}

void OpenXRAdaptiveRendering::set_scale_step(float p_step) {
	scale_step = CLAMP(p_step, 0.01f, 0.2f);
}

float OpenXRAdaptiveRendering::get_scale_step() const {
	return scale_step;
}

float OpenXRAdaptiveRendering::get_current_render_scale() const {
	return current_render_scale;
}

float OpenXRAdaptiveRendering::get_current_frame_time_ms() const {
	return frame_time_ema_ms;
}

float OpenXRAdaptiveRendering::get_target_frame_time_ms() const {
	float target_fps = 90.0f;
	Ref<OpenXRInterface> xr_interface = XRServer::get_singleton()->get_primary_interface();
	if (xr_interface.is_valid()) {
		float display_refresh = xr_interface->get_display_refresh_rate();
		if (display_refresh > 0.0f) {
			target_fps = display_refresh;
		}
	}
	return 1000.0f / target_fps;
}

void OpenXRAdaptiveRendering::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &OpenXRAdaptiveRendering::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &OpenXRAdaptiveRendering::is_enabled);

	ClassDB::bind_method(D_METHOD("set_min_render_scale", "scale"), &OpenXRAdaptiveRendering::set_min_render_scale);
	ClassDB::bind_method(D_METHOD("get_min_render_scale"), &OpenXRAdaptiveRendering::get_min_render_scale);

	ClassDB::bind_method(D_METHOD("set_max_render_scale", "scale"), &OpenXRAdaptiveRendering::set_max_render_scale);
	ClassDB::bind_method(D_METHOD("get_max_render_scale"), &OpenXRAdaptiveRendering::get_max_render_scale);

	ClassDB::bind_method(D_METHOD("set_scale_step", "step"), &OpenXRAdaptiveRendering::set_scale_step);
	ClassDB::bind_method(D_METHOD("get_scale_step"), &OpenXRAdaptiveRendering::get_scale_step);

	ClassDB::bind_method(D_METHOD("get_current_render_scale"), &OpenXRAdaptiveRendering::get_current_render_scale);
	ClassDB::bind_method(D_METHOD("get_current_frame_time_ms"), &OpenXRAdaptiveRendering::get_current_frame_time_ms);
	ClassDB::bind_method(D_METHOD("get_target_frame_time_ms"), &OpenXRAdaptiveRendering::get_target_frame_time_ms);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_render_scale", PROPERTY_HINT_RANGE, "0.3,1.0,0.05"), "set_min_render_scale", "get_min_render_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_render_scale", PROPERTY_HINT_RANGE, "0.5,2.0,0.05"), "set_max_render_scale", "get_max_render_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_step", PROPERTY_HINT_RANGE, "0.01,0.2,0.01"), "set_scale_step", "get_scale_step");
}

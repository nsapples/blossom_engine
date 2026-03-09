/**************************************************************************/
/*  physics_material.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#if !defined(PHYSICS_2D_DISABLED) || !defined(PHYSICS_3D_DISABLED)
#include "core/io/resource.h"
#include "scene/resources/packed_scene.h"

class PhysicsMaterial : public Resource {
	GDCLASS(PhysicsMaterial, Resource);
	OBJ_SAVE_TYPE(PhysicsMaterial);
	RES_BASE_EXTENSION("phymat");

	real_t friction = 1.0;
	bool rough = false;
	real_t bounce = 0.0;
	bool absorbent = false;

	// Impact properties.
	Ref<PackedScene> impact_scene;
	real_t damage_modifier = 1.0;
	real_t penetration_resistance = 1.0;
	real_t reflection_factor = 0.0;

	// Footstep properties.
	Ref<PackedScene> footstep_scene;

protected:
	static void _bind_methods();

public:
	void set_friction(real_t p_val);
	_FORCE_INLINE_ real_t get_friction() const { return friction; }

	void set_rough(bool p_val);
	_FORCE_INLINE_ bool is_rough() const { return rough; }

	_FORCE_INLINE_ real_t computed_friction() const {
		return rough ? -friction : friction;
	}

	void set_bounce(real_t p_val);
	_FORCE_INLINE_ real_t get_bounce() const { return bounce; }

	void set_absorbent(bool p_val);
	_FORCE_INLINE_ bool is_absorbent() const { return absorbent; }

	_FORCE_INLINE_ real_t computed_bounce() const {
		return absorbent ? -bounce : bounce;
	}

	// Impact properties.
	void set_impact_scene(const Ref<PackedScene> &p_scene);
	Ref<PackedScene> get_impact_scene() const;

	void set_damage_modifier(real_t p_val);
	_FORCE_INLINE_ real_t get_damage_modifier() const { return damage_modifier; }

	void set_penetration_resistance(real_t p_val);
	_FORCE_INLINE_ real_t get_penetration_resistance() const { return penetration_resistance; }

	void set_reflection_factor(real_t p_val);
	_FORCE_INLINE_ real_t get_reflection_factor() const { return reflection_factor; }

	// Footstep properties.
	void set_footstep_scene(const Ref<PackedScene> &p_scene);
	Ref<PackedScene> get_footstep_scene() const;
};
#endif // !defined(PHYSICS_2D_DISABLED) || !defined(PHYSICS_3D_DISABLED)

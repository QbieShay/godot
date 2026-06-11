/**************************************************************************/
/*  trail_3d.h                                                            */
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

#include "scene/3d/line_3d.h"

class Trail3D : public Line3D {
	GDCLASS(Trail3D, Line3D);

	// Lots of code for this node is inspired/ported from
	// https://codeberg.org/MajorMcDoom/cozy-cube-godot-addons/src/branch/main
	// thank you so much for putting it out there!

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	// Ribbon
	// Trail3D

	enum LimitMode {
		LIMIT_MODE_LIFETIME,
		LIMIT_MODE_MAX_LENGTH,
		LIMIT_MODE_MAX,
	};

	void set_limit_mode(LimitMode p_limit_mode);
	LimitMode get_limit_mode() const;

	void set_min_section_length(real_t p_min_section_length);
	real_t get_min_section_length() const;

	void set_lifetime(real_t p_lifetime);
	real_t get_lifetime() const;

	void set_emitting(bool p_emitting);
	bool is_emitting() const;

	void set_max_length(real_t p_max_length);
	real_t get_max_length() const;

	void set_pin_uv(bool p_pin_uv);
	bool get_pin_uv() const;

	void clear() override;
	real_t get_current_length() const;

private:
	//Trail3D
	bool emitting = true;
	PackedRealArray velocities;
	real_t lifetime = 0.2;
	real_t max_length = 5.0;
	LimitMode limit_mode = LIMIT_MODE_LIFETIME;
	bool pin_uv = false;
	real_t _last_section_speed = 0.0;
	real_t _last_pinned_u = 0.0;

	Trail3D();
};

VARIANT_ENUM_CAST(Trail3D::LimitMode)

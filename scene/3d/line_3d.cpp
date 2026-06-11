/**************************************************************************/
/*  line_3d.cpp                                                           */
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

#include "line_3d.h"

#include "core/object/class_db.h"

void Trail3D::init_shaders() {
	billboard_additive_shader.instantiate();
	billboard_additive_shader->set_code(R"(
shader_type spatial;
render_mode blend_add, depth_draw_never, unshaded, skip_vertex_transform, cull_disabled;

void vertex() {
	if (length(NORMAL) > 0.0) {
		vec3 p = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
		vec3 t = (MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
		VERTEX = p + UV.y * normalize(cross(p, t));
		NORMAL = (VIEW_MATRIX * vec4(0, 1, 0, 0)).xyz;
		UV.y = (sign(UV.y) + 1.0) / 2.0;
	} else {
		VERTEX = vec3(0.);
		NORMAL = vec3(0.);
	}
}

void fragment() {
	ALBEDO = COLOR.rgb;
	ALPHA = COLOR.a;
}
)");
	billboard_shader.instantiate();
	billboard_shader->set_code(R"(
shader_type spatial;
render_mode blend_mix, depth_draw_never, unshaded, skip_vertex_transform, cull_disabled;

void vertex() {
	if (length(NORMAL) > 0.0) {
		vec3 p = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
		vec3 t = (MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
		VERTEX = p + UV.y * normalize(cross(p, t));
		NORMAL = (VIEW_MATRIX * vec4(0, 1, 0, 0)).xyz;
		UV.y = (sign(UV.y) + 1.0) / 2.0;
	} else {
		VERTEX = vec3(0.);
		NORMAL = vec3(0.);
	}
}

void fragment() {
	ALBEDO = COLOR.rgb;
	ALPHA = COLOR.a;
}
)");
	local_additive_shader.instantiate();
	local_additive_shader->set_code(R"(
shader_type spatial;
render_mode blend_add, depth_draw_never, unshaded, cull_disabled;

void fragment() {
	ALBEDO = COLOR.rgb;
	ALPHA = COLOR.a;
}
)");
	local_shader.instantiate();
	local_shader->set_code(R"(
shader_type spatial;
render_mode blend_mix, depth_draw_never, unshaded, cull_disabled;

void fragment() {
	ALBEDO = COLOR.rgb;
	ALPHA = COLOR.a;
}
)");

	billboard_additive_material.instantiate();
	billboard_additive_material->set_shader(billboard_additive_shader);
	billboard_material.instantiate();
	billboard_material->set_shader(billboard_shader);
	local_additive_material.instantiate();
	local_additive_material->set_shader(local_additive_shader);
	local_material.instantiate();
	local_material->set_shader(local_shader);
}

void Trail3D::finish_shaders() {
	billboard_additive_material.unref();
	billboard_material.unref();
	local_additive_material.unref();
	local_material.unref();
	billboard_shader.unref();
	billboard_additive_shader.unref();
	local_shader.unref();
	local_additive_shader.unref();
}

void Trail3D::set_width(float p_width) {
	width = p_width;
	rebuild();
}

float Trail3D::get_width() const {
	return width;
}

void Trail3D::set_width_curve(Ref<Curve> p_width_curve) {
	if (p_width_curve == width_curve) {
		return;
	}
	if (width_curve.is_valid()) {
		width_curve->disconnect_changed(callable_mp(this, &Trail3D::rebuild));
	}
	width_curve = p_width_curve;
	if (width_curve.is_valid()) {
		width_curve->connect_changed(callable_mp(this, &Trail3D::rebuild));
	}
	rebuild();
}

Ref<Curve> Trail3D::get_width_curve() const {
	return width_curve;
}

void Trail3D::set_color(const Color &p_color) {
	if (p_color != color) {
		rebuild();
	}
	color = p_color;
}

Color Trail3D::get_color() const {
	return color;
}

void Trail3D::set_color_gradient(Ref<Gradient> p_color_gradient) {
	if (p_color_gradient == color_gradient) {
		return;
	}
	if (color_gradient.is_valid()) {
		color_gradient->disconnect_changed(callable_mp(this, &Trail3D::rebuild));
	}
	color_gradient = p_color_gradient;
	if (color_gradient.is_valid()) {
		color_gradient->connect_changed(callable_mp(this, &Trail3D::rebuild));
	}
	rebuild();
}

Ref<Gradient> Trail3D::get_color_gradient() const {
	return color_gradient;
}

void Trail3D::set_material_mode(MaterialMode p_material_mode) {
	if (material_mode == p_material_mode) {
		return;
	}
	material_mode = p_material_mode;
	notify_property_list_changed();
	_ensure_material();
}

Trail3D::MaterialMode Trail3D::get_material_mode() const {
	return material_mode;
}

void Trail3D::set_material(Ref<ShaderMaterial> p_material) {
	if (material_mode != MATERIAL_MODE_CUSTOM) {
		return;
	}
	material = p_material;
	_ensure_material();
}

Ref<ShaderMaterial> Trail3D::get_material() const {
	return material;
}

void Trail3D::set_mesh_alignment(MeshAlignment p_alignment) {
	if (p_alignment != alignment) {
		rebuild();
		alignment = p_alignment;
		_ensure_material();
	}
}

Trail3D::MeshAlignment Trail3D::get_mesh_alignment() const {
	return alignment;
}

void Trail3D::set_tiling_mode(TilingMode p_tiling_mode) {
	if (p_tiling_mode != tiling_mode) {
		rebuild();
	}
	tiling_mode = p_tiling_mode;
	notify_property_list_changed();
}

Trail3D::TilingMode Trail3D::get_tiling_mode() const {
	return tiling_mode;
}

void Trail3D::set_tiling_multiplier(float p_tiling_multiplier) {
	if (p_tiling_multiplier != tiling_multiplier) {
		rebuild();
	}
	tiling_multiplier = p_tiling_multiplier;
}

float Trail3D::get_tiling_multiplier() const {
	return tiling_multiplier;
}

void Trail3D::set_tiling_offset(float p_tiling_offset) {
	if (p_tiling_offset != tiling_offset) {
		rebuild();
	}
	tiling_offset = p_offset;
}

float Trail3D::get_tiling_offset() const {
	return tiling_offset;
}

void Trail3D::set_min_section_length(real_t p_min_section_length) {
	if (p_min_section_length != min_section_length) {
		rebuild();
	}
	min_section_length = p_min_section_length;
}

real_t Trail3D::get_min_section_length() const {
	return min_section_length;
}

void Trail3D::rebuild(bool p_force = false) {
	_do_rebuild();
}

real_t Trail3D::get_current_length() const {
	real_t length = 0.0;
	for (int i = 0; i < points.size() - 1; i++) {
		length += points.get(i).distance_to(points.get(i + 1));
	}

	return length;
}

void Trail3D::clear() {
	points.clear();
	normals.clear();
	tangents.clear();
	_last_vertex_count = 600;
	_init_clear_mesh();
	rebuild();
}

Line3D::Line3D() {
}

void Line3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_width", "width"), &Trail3D::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &Trail3D::get_width);
	ClassDB::bind_method(D_METHOD("set_width_curve", "curve"), &Trail3D::set_width_curve);
	ClassDB::bind_method(D_METHOD("get_width_curve"), &Trail3D::get_width_curve);

	ClassDB::bind_method(D_METHOD("set_color", "color"), &Trail3D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &Trail3D::get_color);
	ClassDB::bind_method(D_METHOD("set_color_gradient", "gradient"), &Trail3D::set_color_gradient);
	ClassDB::bind_method(D_METHOD("get_color_gradient"), &Trail3D::get_color_gradient);

	ClassDB::bind_method(D_METHOD("set_material_mode", "material_mode"), &Trail3D::set_material_mode);
	ClassDB::bind_method(D_METHOD("get_material_mode"), &Trail3D::get_material_mode);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &Trail3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &Trail3D::get_material);

	ClassDB::bind_method(D_METHOD("set_mesh_alignment", "alignment"), &Trail3D::set_mesh_alignment);
	ClassDB::bind_method(D_METHOD("get_mesh_alignment"), &Trail3D::get_mesh_alignment);

	ClassDB::bind_method(D_METHOD("set_tiling_multiplier", "tiling_multiplier"), &Trail3D::set_tiling_multiplier);
	ClassDB::bind_method(D_METHOD("get_tiling_multiplier"), &Trail3D::get_tiling_multiplier);
	ClassDB::bind_method(D_METHOD("set_tiling_offset", "tiling_offset"), &Trail3D::set_tiling_offset);
	ClassDB::bind_method(D_METHOD("get_tiling_offset"), &Trail3D::get_tiling_offset);
	ClassDB::bind_method(D_METHOD("set_tiling_mode", "tiling_mode"), &Trail3D::set_tiling_mode);
	ClassDB::bind_method(D_METHOD("get_tiling_mode"), &Trail3D::get_tiling_mode);

	ClassDB::bind_method(D_METHOD("rebuild"), &Trail3D::rebuild);
	ClassDB::bind_method(D_METHOD("clear"), &Trail3D::clear);

	ClassDB::bind_method(D_METHOD("get_current_length"), &Trail3D::get_current_length);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "width"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "width_curve", PROPERTY_HINT_RESOURCE_TYPE, Curve::get_class_static()), "set_width_curve", "get_width_curve");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color", PROPERTY_HINT_NONE), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "color_gradient", PROPERTY_HINT_RESOURCE_TYPE, Gradient::get_class_static()), "set_color_gradient", "get_color_gradient");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mesh_alignment", PROPERTY_HINT_ENUM, "Local,Billboard"), "set_mesh_alignment", "get_mesh_alignment");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_mode", PROPERTY_HINT_ENUM, "Default,Default Additive,Custom"), "set_material_mode", "get_material_mode");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, ShaderMaterial::get_class_static()), "set_material", "get_material");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tiling_mode", PROPERTY_HINT_ENUM, "Unit,Length"), "set_tiling_mode", "get_tiling_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tiling_multiplier"), "set_tiling_multiplier", "get_tiling_multiplier");

	BIND_ENUM_CONSTANT(MATERIAL_MODE_MIX);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_ADD);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_CUSTOM);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_MAX);

	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_LOCAL);
	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_BILLBOARD);
	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_MAX);

	BIND_ENUM_CONSTANT(TILING_MODE_UNIT);
	BIND_ENUM_CONSTANT(TILING_MODE_LENGTH);
	BIND_ENUM_CONSTANT(TILING_MAX);
}

/**************************************************************************/
/*  ribbon.cpp                                                            */
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

#include "ribbon.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "servers/rendering/rendering_server.h"

// Points calculation, line construction and shaders are taken
// and adapted from CozyCubeGames.

/**************************************************************************/
/* Copyright (c) 2024 CozyCubeGames                                       */
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

void Ribbon::init_shaders() {
	billboard_additive_shader.instantiate();
	billboard_additive_shader->set_code(R"(
shader_type spatial;
render_mode blend_add, depth_draw_never, unshaded, skip_vertex_transform, cull_disabled;

void vertex() {
	if (length(NORMAL) > 0.){
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
	if (length(NORMAL) > 0.){
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

void Ribbon::finish_shaders() {
	billboard_additive_material.unref();
	billboard_material.unref();
	local_additive_material.unref();
	local_material.unref();
	billboard_shader.unref();
	billboard_additive_shader.unref();
	local_shader.unref();
	local_additive_shader.unref();
}

void Ribbon::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (ribbon_mode == RIBBON_MODE_TRAIL) {
				_process_trail(get_process_delta_time());
			}

			if (_needs_rebuilding) {
				_do_rebuild();
			}
		} break;
		case NOTIFICATION_READY: {
			clear();
		}
	}
}

void Ribbon::set_width(float p_width) {
	width = p_width;
	_needs_rebuilding = true;
}

float Ribbon::get_width() const {
	return width;
}

void Ribbon::set_emitting(bool p_emitting) {
	if (emitting != p_emitting && p_emitting) {
		clear();
	}
	emitting = p_emitting;
}

bool Ribbon::is_emitting() const {
	return emitting;
}

void Ribbon::set_width_curve(Ref<Curve> p_width_curve) {
	if (p_width_curve == width_curve) {
		return;
	}
	if (width_curve.is_valid()) {
		width_curve->disconnect_changed(callable_mp(this, &Ribbon::rebuild));
	}
	width_curve = p_width_curve;
	if (width_curve.is_valid()) {
		width_curve->connect_changed(callable_mp(this, &Ribbon::rebuild));
	}
	_needs_rebuilding = true;
}

Ref<Curve> Ribbon::get_width_curve() const {
	return width_curve;
}

void Ribbon::set_color(const Color &p_color) {
	if (p_color != color) {
		_needs_rebuilding = true;
	}
	color = p_color;
}
Color Ribbon::get_color() const {
	return color;
}

void Ribbon::set_color_gradient(Ref<Gradient> p_color_gradient) {
	if (p_color_gradient == color_gradient) {
		return;
	}
	if (color_gradient.is_valid()) {
		color_gradient->disconnect_changed(callable_mp(this, &Ribbon::rebuild));
	}
	color_gradient = p_color_gradient;
	if (color_gradient.is_valid()) {
		color_gradient->connect_changed(callable_mp(this, &Ribbon::rebuild));
	}
	_needs_rebuilding = true;
}

Ref<Gradient> Ribbon::get_color_gradient() const {
	return color_gradient;
}

void Ribbon::set_material_mode(MaterialMode p_material_mode) {
	if (material_mode == p_material_mode) {
		return;
	}
	material_mode = p_material_mode;
	notify_property_list_changed();
	_ensure_material();
}
Ribbon::MaterialMode Ribbon::get_material_mode() const {
	return material_mode;
}

void Ribbon::set_material(Ref<ShaderMaterial> p_material) {
	if (material_mode != MATERIAL_MODE_CUSTOM) {
		return;
	}
	material = p_material;
	_ensure_material();
}
Ref<ShaderMaterial> Ribbon::get_material() const {
	return material;
}

void Ribbon::set_mesh_alignment(MeshAlignment p_alignment) {
	if (p_alignment != alignment) {
		_needs_rebuilding = true;
	}
	alignment = p_alignment;
}

Ribbon::MeshAlignment Ribbon::get_alignment() const {
	return alignment;
}

void Ribbon::set_tiling_mode(TilingMode p_tiling_mode) {
	if (p_tiling_mode != tiling_mode) {
		_needs_rebuilding = true;
	}
	tiling_mode = p_tiling_mode;
	notify_property_list_changed();
}
Ribbon::TilingMode Ribbon::get_tiling_mode() const {
	return tiling_mode;
}

void Ribbon::set_tiling_multiplier(float p_tiling_multiplier) {
	if (p_tiling_multiplier != tiling_multiplier) {
		_needs_rebuilding = true;
	}
	tiling_multiplier = p_tiling_multiplier;
}
float Ribbon::get_tiling_multiplier() const {
	return tiling_multiplier;
}

void Ribbon::set_ribbon_mode(RibbonMode p_ribbon_mode) {
	if (p_ribbon_mode == ribbon_mode) {
		return;
	}
	clear();
	ribbon_mode = p_ribbon_mode;
	notify_property_list_changed();
}

Ribbon::RibbonMode Ribbon::get_ribbon_mode() const {
	return ribbon_mode;
}

void Ribbon::rebuild() {
	_needs_rebuilding = true;
	_do_rebuild();
}

void Ribbon::clear() {
	points.clear();
	normals.clear();
	_times.clear();
	velocities.clear();
	if (ribbon_mode == RIBBON_MODE_TRAIL) {
		tiling_offset = 0.;
	}
	_last_vertex_count = 512;
	set_mesh(nullptr);
	rebuild();
}

// Beam
void Ribbon::set_beam_length(real_t p_beam_length) {
	beam_length = p_beam_length;
	_process_beam();
}

real_t Ribbon::get_beam_length() const {
	return beam_length;
}

// Trail3D

void Ribbon::set_limit_mode(LimitMode p_limit_mode) {
	limit_mode = p_limit_mode;
	clear();
	notify_property_list_changed();
}
Ribbon::LimitMode Ribbon::get_limit_mode() const {
	return limit_mode;
}

void Ribbon::set_min_section_length(real_t p_min_section_length) {
	if (p_min_section_length != min_section_length) {
		_needs_rebuilding = true;
	}
	min_section_length = p_min_section_length;
}

real_t Ribbon::get_min_section_length() const {
	return min_section_length;
}

void Ribbon::set_lifetime(real_t p_lifetime) {
	lifetime = p_lifetime;
}

real_t Ribbon::get_lifetime() const {
	return lifetime;
}

void Ribbon::set_max_length(real_t p_max_length) {
	max_length = p_max_length;
}
real_t Ribbon::get_max_length() const {
	return max_length;
}

void Ribbon::set_pin_uv(bool p_pin_uv) {
	if (p_pin_uv != pin_uv) {
		_needs_rebuilding = true;
	}
	pin_uv = p_pin_uv;
}

bool Ribbon::get_pin_uv() const {
	return pin_uv;
}

real_t Ribbon::_calc_current_length() const {
	real_t length = 0.0;
	switch (ribbon_mode) {
		case RIBBON_MODE_TRAIL: {
			for (int i = 0; i < points.size() - 1; i++) {
				length += points.get(i).distance_to(points.get(i + 1));
			}
		} break;
		case RIBBON_MODE_BEAM: {
			if (points.size() > 1) {
				length = points.get(0).distance_to(points.get(points.size() - 1));
			}
		} break;
		case RIBBON_MODE_MAX: {
		}
	}
	return length;
}

real_t Ribbon::get_current_length() const {
	return _calc_current_length();
}

void Ribbon::_do_rebuild() {
	if (!is_inside_tree() || !is_ready() || !_needs_rebuilding) {
		return;
	}

	int points_count = points.size();

	if (points.size() < 2) {
		return;
	}

	if (Object::cast_to<ArrayMesh>(mesh.ptr()) == nullptr || mesh->get_surface_count() == 0) {
		set_mesh(memnew(ArrayMesh));
		Ref<ArrayMesh> am = mesh;
		Array arrays;

		PackedVector3Array mesh_vertices;
		PackedVector3Array mesh_normals;
		PackedColorArray mesh_colors;
		PackedVector2Array mesh_uvs;
		PackedInt32Array mesh_indices;

		mesh_vertices.resize(_last_vertex_count);
		mesh_normals.resize(_last_vertex_count);
		mesh_colors.resize(_last_vertex_count);
		mesh_uvs.resize(_last_vertex_count);
		mesh_indices.resize((_last_vertex_count) * 3);

		arrays.resize(RSE::ARRAY_MAX);
		arrays[RSE::ARRAY_VERTEX] = mesh_vertices;
		arrays[RSE::ARRAY_NORMAL] = mesh_normals;
		arrays[RSE::ARRAY_TEX_UV] = mesh_uvs;
		arrays[RSE::ARRAY_COLOR] = mesh_colors;
		arrays[RSE::ARRAY_INDEX] = mesh_indices;

		RID mesh_rid = am->get_rid();
		RenderingServerTypes::SurfaceData sd;
		Error err = RS::get_singleton()->mesh_create_surface_data_from_arrays(&sd, RSE::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary());
		if (err != OK) {
			print_error("Trail3D failed mesh initialization. Please open a bug report");
		}

		mesh_surface_format = sd.format;
		vertex_buffer = sd.vertex_data.duplicate();
		attribute_buffer = sd.attribute_data.duplicate();
		index_buffer = sd.index_data.duplicate();

		RS::get_singleton()->mesh_surface_make_offsets_from_format(sd.format, sd.vertex_count, sd.index_count, mesh_surface_offsets, vertex_stride, normal_tangent_stride, attrib_stride, skin_stride);
		RS::get_singleton()->mesh_clear(mesh_rid);
		am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		//RS::get_singleton()->mesh_add_surface(mesh_rid, sd);

		_ensure_material();
	}

	Ref<ArrayMesh> am = mesh;

	_needs_rebuilding = false;

	PackedVector3Array mesh_vertices;
	PackedVector3Array mesh_normals;
	PackedColorArray mesh_colors;
	PackedVector2Array mesh_uvs;
	PackedInt32Array mesh_indices;

	int array_size = points_count * 3;
	mesh_vertices.resize(array_size);
	mesh_normals.resize(array_size);
	mesh_colors.resize(array_size);
	mesh_uvs.resize(array_size);
	mesh_indices.resize((points_count - 1) * 12);
	uint8_t *vertex_write_buffer = vertex_buffer.ptrw();
	uint8_t *attribute_write_buffer = attribute_buffer.ptrw();

	Vector3 *_vertices = mesh_vertices.ptrw();
	Vector3 *_normals = mesh_normals.ptrw();
	Color *_colors = mesh_colors.ptrw();
	Vector2 *_uvs = mesh_uvs.ptrw();
	int *_indices = mesh_indices.ptrw();

	for (int i = 0; i < points_count - 1; i++) {
		int j = i * 12;
		int k = i * 3;
		_indices[j] = k;
		_indices[j + 1] = k + 3;
		_indices[j + 2] = k + 1;
		_indices[j + 3] = k + 1;
		_indices[j + 4] = k + 3;
		_indices[j + 5] = k + 4;
		_indices[j + 6] = k + 1;
		_indices[j + 7] = k + 4;
		_indices[j + 8] = k + 2;
		_indices[j + 9] = k + 2;
		_indices[j + 10] = k + 4;
		_indices[j + 11] = k + 5;
	}

	Transform3D inv_global_tf = get_global_transform().inverse();

	real_t length = _calc_current_length();

	real_t dist = 0.0;
	Vector3 min_bounds = Vector3();
	Vector3 max_bounds = Vector3();
	Vector3 width_bounds = Vector3(width, width, width);

	for (int i = 0; i < points_count; i++) {
		int j0 = i * 3;
		int j1 = j0 + 1;
		int j2 = j0 + 2;

		Vector3 p = points[i];

		if (i > 0) {
			dist += points[i - 1].distance_to(p);
		}

		real_t ratio = length > 0.0 ? dist / length : 0.0;
		real_t length_uv = 0;

		switch (tiling_mode) {
			case Ribbon::TilingMode::TILING_MODE_LENGTH: {
				if (pin_uv) {
					length_uv = (dist + tiling_offset) * tiling_multiplier;
				} else {
					length_uv = dist * tiling_multiplier;
				}
			} break;
			case Ribbon::TilingMode::TILING_MODE_UNIT: {
				length_uv = ratio * tiling_multiplier;
			} break;
			default: {
			}
		}

		real_t half_width = width * 0.5;

		if (width_curve.is_valid()) {
			half_width *= width_curve->sample(ratio);
		}

		Vector3 tangent;

		if (i == 0) {
			tangent = (points[i + 1] - p);
		} else if (i == points_count - 1) {
			tangent = (p - points[i - 1]);
		} else {
			tangent = (points[i + 1] - points[i - 1]);
		}
		if (tangent.length() > 0) {
			tangent = tangent.normalized();
		} else {
			tangent = Vector3(0.0, 0.0, 1.0);
		}

		min_bounds = MIN(min_bounds, p - width_bounds);
		max_bounds = MAX(max_bounds, p + width_bounds);
		p = inv_global_tf.xform(p);
		tangent = inv_global_tf.basis.xform(tangent);

		if (alignment == Ribbon::MeshAlignment::MESH_ALIGNMENT_BILLBOARD) {
			_vertices[j0] = p;
			_vertices[j1] = p;
			_vertices[j2] = p;
			_encode_vertex(p, j0);
			_encode_vertex(p, j1);
			_encode_vertex(p, j2);

			_normals[j0] = tangent;
			_normals[j1] = tangent;
			_normals[j2] = tangent;
			_encode_normal(tangent, j0);
			_encode_normal(tangent, j1);
			_encode_normal(tangent, j2);

			_uvs[j0] = Vector2(length_uv, -half_width);
			_uvs[j1] = Vector2(length_uv, 0);
			_uvs[j2] = Vector2(length_uv, half_width);
			_encode_uv(_uvs[j0], j0);
			_encode_uv(_uvs[j1], j1);
			_encode_uv(_uvs[j2], j2);

		} else {
			Vector3 normal = normals[i];
			normal = inv_global_tf.basis.xform(normal);

			Vector3 curve_binormal = normal.cross(tangent);
			_vertices[j0] = p + half_width * curve_binormal;
			_vertices[j1] = p;
			_vertices[j2] = p - half_width * curve_binormal;
			_encode_vertex(_vertices[j0], j0);
			_encode_vertex(_vertices[j1], j1);
			_encode_vertex(_vertices[j2], j2);

			_normals[j0] = normal;
			_normals[j1] = normal;
			_normals[j2] = normal;
			_encode_normal(normal, j0);
			_encode_normal(normal, j1);
			_encode_normal(normal, j2);

			_uvs[j0] = Vector2(length_uv, 0);
			_uvs[j1] = Vector2(length_uv, 0.5);
			_uvs[j2] = Vector2(length_uv, 1);
			_encode_uv(_uvs[j0], j0);
			_encode_uv(_uvs[j1], j1);
			_encode_uv(_uvs[j2], j2);
		}
		Color c = color.srgb_to_linear();
		if (color_gradient.is_valid()) {
			c *= color_gradient->get_color_at_offset(ratio).srgb_to_linear();
		}

		_colors[j0] = c;
		_colors[j1] = c;
		_colors[j2] = c;
		_encode_color(c, j0);
		_encode_color(c, j1);
		_encode_color(c, j2);
	}

	/*
	for (int i = 0; i < _last_vertex_count - points_count * 3; i++ ){
		_vertices[points_count * 3 + i] = _vertices[points_count * 3 -1];
		_uvs[points_count * 3 + i] = _uvs[points_count * 3 -1];
		_normals[points_count * 3 + i] = Vector3(0.0, 0.0, 0.0);
		_colors[points_count * 3 + i] = Color(1.0,1.0,1.0,0.0);
	}
	int index_count = mesh_indices.size();
	for (int i = index_count; i < _last_vertex_count; i++) {
		_indices[i] = _indices[index_count - 1];
	}*/

	Array arrays;
	arrays.resize(RSE::ARRAY_MAX);
	arrays[RSE::ARRAY_VERTEX] = mesh_vertices;
	arrays[RSE::ARRAY_NORMAL] = mesh_normals;
	arrays[RSE::ARRAY_TEX_UV] = mesh_uvs;
	arrays[RSE::ARRAY_COLOR] = mesh_colors;
	arrays[RSE::ARRAY_INDEX] = mesh_indices;

	if (points_count * 3 > _last_vertex_count) {
		while (points_count * 3 > _last_vertex_count) {
			_last_vertex_count *= 2;
		}
		if (_last_vertex_count >= 65536) {
			WARN_PRINT("Vertex count for trail tried to exceed max vertex size.");
		}
		_last_vertex_count = MIN(_last_vertex_count, 65536);
		int index_count = mesh_indices.size();
		mesh_vertices.resize(_last_vertex_count);
		mesh_normals.resize(_last_vertex_count);
		mesh_uvs.resize(_last_vertex_count);
		mesh_colors.resize(_last_vertex_count);
		mesh_indices.resize(_last_vertex_count * 3);
		_vertices = mesh_vertices.ptrw();
		_normals = mesh_normals.ptrw();
		_indices = mesh_indices.ptrw();
		for (int i = points_count * 3; i < _last_vertex_count; i++) {
			_vertices[i] = Vector3();
			_normals[i] = Vector3();
		}
		for (int i = index_count; i < _last_vertex_count * 3; i++) {
			_indices[i] = 0;
		}

		RID mesh_rid = am->get_rid();
		RenderingServerTypes::SurfaceData sd;
		Error err = RS::get_singleton()->mesh_create_surface_data_from_arrays(&sd, RSE::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary());
		if (err != OK) {
			print_error("Trail3D failed mesh initialization. Please open a bug report");
		}
		mesh_surface_format = sd.format;
		vertex_buffer = sd.vertex_data.duplicate();
		attribute_buffer = sd.attribute_data.duplicate();
		index_buffer = sd.index_data.duplicate();

		RS::get_singleton()->mesh_surface_make_offsets_from_format(sd.format, sd.vertex_count, sd.index_count, mesh_surface_offsets, vertex_stride, normal_tangent_stride, attrib_stride, skin_stride);
		RS::get_singleton()->mesh_clear(mesh_rid);
		RS::get_singleton()->mesh_add_surface(mesh_rid, sd);
		_ensure_material();

	} else {
		RID rid = am->get_rid();
		uint8_t *writable_index = index_buffer.ptrw();
		for (int i = 0; i < mesh_indices.size(); i++) {
			uint16_t idx = (uint16_t)_indices[i];
			memcpy(&writable_index[i * 2], &idx, sizeof(uint16_t));
		}
		RS::get_singleton()->mesh_surface_update_vertex_region(rid, 0, 0, vertex_buffer);
		RS::get_singleton()->mesh_surface_update_attribute_region(rid, 0, 0, attribute_buffer);
		RS::get_singleton()->mesh_surface_update_index_region(rid, 0, 0, index_buffer);
	}
	am->set_custom_aabb(AABB(Vector3(), (max_bounds - min_bounds)));
}

void Ribbon::_process_beam() {
	if (!is_inside_tree()) {
		return;
	}

	Vector3 origin = get_global_transform().origin;
	int subdivisions = (int)(beam_length / min_section_length);
	points.resize(subdivisions + 1);
	normals.resize(subdivisions + 1);
	Vector3 *_points = points.ptrw();
	Vector3 *_normals = normals.ptrw();
	Vector3 beam_dir = get_global_transform().basis.get_column(2).normalized();
	Vector3 beam_normal = get_global_transform().basis.get_column(1).normalized();
	real_t remaining_len = 0.0;
	beam_dir.normalize();
	for (int i = 0; remaining_len < beam_length; i++) {
		_points[i] = origin + beam_dir * remaining_len;
		remaining_len += min_section_length;
		_normals[i] = beam_normal;
	}
	_points[subdivisions] = origin + beam_dir * beam_length;
	_normals[subdivisions] = beam_normal;
	_needs_rebuilding = true;
}

void Ribbon::_process_trail(real_t p_delta) {
	if (!is_inside_tree() || !is_ready()) {
		return;
	}

	Transform3D tf = get_global_transform();
	Vector3 pos = tf.origin;
	Vector3 up = tf.basis.get_column(1);
	_time += p_delta;

	if (points.size() < 2) {
		while (points.size() < 2) {
			points.insert(0, pos);
			normals.insert(0, up);
			if (limit_mode == LIMIT_MODE_LIFETIME) {
				_times.insert(0, _time);
				velocities.insert(0, 0.);
			}
		}
	} else if (emitting) {
		points.write[0] = pos;
		normals.write[0] = up;
		if (limit_mode == LIMIT_MODE_LIFETIME) {
			_times.write[0] = _time;
		}
	}

	Vector3 leading = points.get(1);
	Vector3 from_leading = pos - leading;
	real_t dist_from_leading = from_leading.length();

	if (dist_from_leading < CMP_EPSILON && limit_mode == LIMIT_MODE_LIFETIME) {
		_times.write[1] = _time;
	}

	if (pin_uv) {
		tiling_offset = -_last_pinned_u - min_section_length;
	} else {
		_last_pinned_u = -tiling_offset - min_section_length;
	}

	switch (limit_mode) {
		case LIMIT_MODE_MAX_LENGTH:
			if (dist_from_leading > min_section_length && emitting) {
				points.insert(1, pos);
				normals.insert(1, up);
				_last_pinned_u += dist_from_leading;
			}
			break;
		case LIMIT_MODE_LIFETIME:
			if (dist_from_leading > min_section_length && emitting) {
				points.insert(1, pos);
				normals.insert(1, up);
				_times.insert(1, _time);
				_last_pinned_u += dist_from_leading;
				velocities.insert(1, (points[1] - points[2]).length() / (_times[1] - _times[2]));
			}
			break;
		default:
			break;
	}

	if (pin_uv) {
		tiling_offset -= dist_from_leading;
	}

	if (limit_mode == Ribbon::LimitMode::LIMIT_MODE_MAX_LENGTH) {
		int last_index = points.size() - 1;
		real_t total_length = 0.0;
		for (int i = 0; i < last_index; i++) {
			total_length += points[i].distance_to(points[i + 1]);
		}

		real_t extra_length = total_length - max_length;

		while (extra_length > 0 && points.size() > 1) {
			Vector3 last_point = points[last_index];
			Vector3 second_last_point = points[last_index - 1];
			real_t last_section_length = last_point.distance_to(second_last_point);
			if (last_section_length > extra_length) {
				real_t shortened_section_length = last_section_length - extra_length;
				points.write[last_index] = second_last_point + (last_point - second_last_point) * (shortened_section_length / last_section_length);
			} else {
				points.remove_at(last_index);
				normals.remove_at(last_index);
				last_index -= 1;
			}
			extra_length -= last_section_length;
		}

	} else if (limit_mode == Ribbon::LimitMode::LIMIT_MODE_LIFETIME) {
		// ATTEMPTS: 5
		// Add +1 to this counter for every failed attempt to improve this code
		int last_index = _times.size() - 1;
		real_t second_last_time = _times.get(last_index - 1);
		while ((second_last_time < _time - lifetime) && points.size() > 2) {
			points.remove_at(last_index);
			normals.remove_at(last_index);
			_times.remove_at(last_index);
			velocities.remove_at(last_index);
			last_index -= 1;
			_last_section_speed = (points[last_index - 1] - points[last_index]).length() / (_times[last_index - 1] - _times[last_index]);
			second_last_time = _times.get(last_index - 1);
		}

		real_t last_time = _times[last_index];
		if (last_time <= _time - lifetime) {
			Vector3 dir = points[last_index - 1] - points[last_index];
			real_t distance = dir.length();

			if (distance > 0.) {
				real_t speed_distance = velocities[last_index] * p_delta;
				points.write[last_index] += dir / distance * MIN(distance, speed_distance);
			}
		}
	}
	_needs_rebuilding = true;
}

void Ribbon::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "mesh") {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_RESOURCE_NOT_PERSISTENT;
	}
	if (p_property.name == "material" && material_mode != MATERIAL_MODE_CUSTOM) {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
	if (p_property.name == "lifetime" && limit_mode == LIMIT_MODE_MAX_LENGTH) {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
	if (p_property.name == "max_length" && limit_mode == LIMIT_MODE_LIFETIME) {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
	switch (ribbon_mode) {
		case RIBBON_MODE_BEAM: {
			if (p_property.name == "points") {
				p_property.usage = PROPERTY_USAGE_NONE;
			} else if (p_property.name == "normals") {
				p_property.usage = PROPERTY_USAGE_NONE;
			} else if (p_property.name == "lifetime") {
				p_property.usage = PROPERTY_USAGE_NONE;
			} else if (p_property.name == "max_length") {
				p_property.usage = PROPERTY_USAGE_NONE;
			} else if (p_property.name == "limit_mode") {
				p_property.usage = PROPERTY_USAGE_NONE;
			} else if (p_property.name == "pin_uv") {
				p_property.usage = PROPERTY_USAGE_NONE;
			}
		} break;
		case RIBBON_MODE_TRAIL: {
			if (p_property.name == "points") {
				p_property.usage = PROPERTY_USAGE_NONE;
			} else if (p_property.name == "normals") {
				p_property.usage = PROPERTY_USAGE_NONE;
			} else if (p_property.name == "target_node") {
				p_property.usage = PROPERTY_USAGE_NONE;
			} else if (p_property.name == "beam_length") {
				p_property.usage = PROPERTY_USAGE_NONE;
			}
		} break;
		case RIBBON_MODE_MAX: {
		} break;
	}
}

void Ribbon::_ensure_material() {
	if (!mesh.is_valid()) {
		return;
	}
	switch (material_mode) {
		case Ribbon::MaterialMode::MATERIAL_MODE_ADD: {
			if (alignment == MeshAlignment::MESH_ALIGNMENT_BILLBOARD) {
				mesh->surface_set_material(0, billboard_additive_material);
			} else {
				mesh->surface_set_material(0, local_additive_material);
			}

		} break;
		case Ribbon::MaterialMode::MATERIAL_MODE_MIX: {
			if (alignment == MeshAlignment::MESH_ALIGNMENT_BILLBOARD) {
				mesh->surface_set_material(0, billboard_material);
			} else {
				mesh->surface_set_material(0, local_material);
			}
		} break;
		case Ribbon::MaterialMode::MATERIAL_MODE_CUSTOM: {
			mesh->surface_set_material(0, material);
		} break;
		case Ribbon::MaterialMode::MATERIAL_MODE_MAX: {
		} break;
	}
}

void Ribbon::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_width", "width"), &Ribbon::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &Ribbon::get_width);
	ClassDB::bind_method(D_METHOD("set_width_curve", "curve"), &Ribbon::set_width_curve);
	ClassDB::bind_method(D_METHOD("get_width_curve"), &Ribbon::get_width_curve);
	ClassDB::bind_method(D_METHOD("set_emitting", "emitting"), &Ribbon::set_emitting);
	ClassDB::bind_method(D_METHOD("is_emitting"), &Ribbon::is_emitting);

	ClassDB::bind_method(D_METHOD("set_color", "color"), &Ribbon::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &Ribbon::get_color);
	ClassDB::bind_method(D_METHOD("set_color_gradient", "gradient"), &Ribbon::set_color_gradient);
	ClassDB::bind_method(D_METHOD("get_color_gradient"), &Ribbon::get_color_gradient);

	ClassDB::bind_method(D_METHOD("set_material_mode", "material_mode"), &Ribbon::set_material_mode);
	ClassDB::bind_method(D_METHOD("get_material_mode"), &Ribbon::get_material_mode);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &Ribbon::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &Ribbon::get_material);

	ClassDB::bind_method(D_METHOD("set_mesh_alignment", "alignment"), &Ribbon::set_mesh_alignment);
	ClassDB::bind_method(D_METHOD("get_alignment"), &Ribbon::get_alignment);

	ClassDB::bind_method(D_METHOD("set_tiling_multiplier", "tiling_multiplier"), &Ribbon::set_tiling_multiplier);
	ClassDB::bind_method(D_METHOD("get_tiling_multiplier"), &Ribbon::get_tiling_multiplier);
	ClassDB::bind_method(D_METHOD("set_tiling_mode", "tiling_mode"), &Ribbon::set_tiling_mode);
	ClassDB::bind_method(D_METHOD("get_tiling_mode"), &Ribbon::get_tiling_mode);

	ClassDB::bind_method(D_METHOD("set_ribbon_mode", "ribbon_mode"), &Ribbon::set_ribbon_mode);
	ClassDB::bind_method(D_METHOD("get_ribbon_mode"), &Ribbon::get_ribbon_mode);

	ClassDB::bind_method(D_METHOD("rebuild"), &Ribbon::rebuild);
	ClassDB::bind_method(D_METHOD("clear"), &Ribbon::clear);

	// Beam and Trail
	ClassDB::bind_method(D_METHOD("set_min_section_length", "min_section_length"), &Ribbon::set_min_section_length);
	ClassDB::bind_method(D_METHOD("get_min_section_length"), &Ribbon::get_min_section_length);

	// Beam
	ClassDB::bind_method(D_METHOD("set_beam_length", "max_length"), &Ribbon::set_beam_length);
	ClassDB::bind_method(D_METHOD("get_beam_length"), &Ribbon::get_beam_length);

	// Trail

	ClassDB::bind_method(D_METHOD("set_limit_mode", "limit_mode"), &Ribbon::set_limit_mode);
	ClassDB::bind_method(D_METHOD("get_limit_mode"), &Ribbon::get_limit_mode);

	ClassDB::bind_method(D_METHOD("set_lifetime", "lifetime"), &Ribbon::set_lifetime);
	ClassDB::bind_method(D_METHOD("get_lifetime"), &Ribbon::get_lifetime);

	ClassDB::bind_method(D_METHOD("set_max_length", "max_length"), &Ribbon::set_max_length);
	ClassDB::bind_method(D_METHOD("get_max_length"), &Ribbon::get_max_length);

	ClassDB::bind_method(D_METHOD("set_pin_uv", "pin_uv"), &Ribbon::set_pin_uv);
	ClassDB::bind_method(D_METHOD("get_pin_uv"), &Ribbon::get_pin_uv);

	ClassDB::bind_method(D_METHOD("get_current_length"), &Ribbon::get_current_length);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "ribbon_mode", PROPERTY_HINT_ENUM, "Trail,Beam"), "set_ribbon_mode", "get_ribbon_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "beam_length"), "set_beam_length", "get_beam_length");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emitting"), "set_emitting", "is_emitting");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "limit_mode", PROPERTY_HINT_ENUM, "Time,Length"), "set_limit_mode", "get_limit_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lifetime", PROPERTY_HINT_RANGE, "0.0,10.0,0.04,or_greater"), "set_lifetime", "get_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_length"), "set_max_length", "get_max_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "width"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "width_curve", PROPERTY_HINT_RESOURCE_TYPE, Curve::get_class_static()), "set_width_curve", "get_width_curve");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color", PROPERTY_HINT_NONE), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "color_gradient", PROPERTY_HINT_RESOURCE_TYPE, Gradient::get_class_static()), "set_color_gradient", "get_color_gradient");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mesh_alignment", PROPERTY_HINT_ENUM, "Local,Billboard"), "set_mesh_alignment", "get_alignment");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_mode", PROPERTY_HINT_ENUM, "Default,Default Additive,Custom"), "set_material_mode", "get_material_mode");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, ShaderMaterial::get_class_static()), "set_material", "get_material");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tiling_mode", PROPERTY_HINT_ENUM, "Unit,Length"), "set_tiling_mode", "get_tiling_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tiling_multiplier"), "set_tiling_multiplier", "get_tiling_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pin_uv"), "set_pin_uv", "get_pin_uv");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_section_length", PROPERTY_HINT_RANGE, "0.05,1.0,0.01"), "set_min_section_length", "get_min_section_length");

	BIND_ENUM_CONSTANT(RIBBON_MODE_TRAIL);
	BIND_ENUM_CONSTANT(RIBBON_MODE_BEAM);
	BIND_ENUM_CONSTANT(RIBBON_MODE_MAX);

	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_LOCAL);
	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_BILLBOARD);
	BIND_ENUM_CONSTANT(MESH_ALIGNMENT_MAX);

	BIND_ENUM_CONSTANT(TILING_MODE_UNIT);
	BIND_ENUM_CONSTANT(TILING_MODE_LENGTH);
	BIND_ENUM_CONSTANT(TILING_MAX);

	BIND_ENUM_CONSTANT(LIMIT_MODE_LIFETIME);
	BIND_ENUM_CONSTANT(LIMIT_MODE_MAX_LENGTH);
	BIND_ENUM_CONSTANT(LIMIT_MODE_MAX);

	BIND_ENUM_CONSTANT(MATERIAL_MODE_MIX);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_ADD);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_CUSTOM);
	BIND_ENUM_CONSTANT(MATERIAL_MODE_MAX);
}

Ribbon::Ribbon() {
	set_process_internal(true);
	points = PackedVector3Array();
	normals = PackedVector3Array();
	velocities = PackedRealArray();
	_times = PackedRealArray();
	vertex_buffer = PackedByteArray();
	attribute_buffer = PackedByteArray();
	index_buffer = Vector<uint8_t>();
	set_mesh(nullptr);
}

Ribbon::~Ribbon() {
}

void Ribbon::_encode_vertex(Vector3 vertex, int index) {
	float v_vertex[3] = { (float)vertex.x, (float)vertex.y, (float)vertex.z };
	memcpy(&(vertex_buffer.ptrw())[index * vertex_stride + mesh_surface_offsets[RSE::ARRAY_VERTEX]], &v_vertex, sizeof(float) * 3);
}

void Ribbon::_encode_normal(Vector3 normal, int index) {
	uint32_t v_normal = 0;
	Vector2 res = normal.octahedron_encode();
	v_normal |= (uint16_t)CLAMP(res.x * 65535, 0, 65535);
	v_normal |= (uint16_t)CLAMP(res.y * 65535, 0, 65535) << 16;

	memcpy(&(vertex_buffer.ptrw())[index * normal_tangent_stride + mesh_surface_offsets[RSE::ARRAY_NORMAL]], &v_normal, 4);
}

void Ribbon::_encode_uv(Vector2 p_uv, int index) {
	float v_uv[2] = { (float)p_uv.x, (float)p_uv.y };
	memcpy(&(attribute_buffer.ptrw())[index * attrib_stride + mesh_surface_offsets[RSE::ARRAY_TEX_UV]], v_uv, 8);
}

void Ribbon::_encode_color(Color p_color, int index) {
	uint8_t v_color[4] = {
		uint8_t(CLAMP(p_color.r * 255.0, 0.0, 255.0)),
		uint8_t(CLAMP(p_color.g * 255.0, 0.0, 255.0)),
		uint8_t(CLAMP(p_color.b * 255.0, 0.0, 255.0)),
		uint8_t(CLAMP(p_color.a * 255.0, 0.0, 255.0))
	};
	memcpy(&(attribute_buffer.ptrw())[index * attrib_stride + mesh_surface_offsets[RSE::ARRAY_COLOR]], v_color, 4);
}

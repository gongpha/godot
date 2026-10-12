/**************************************************************************/
/*  nav_polygon_bvh_3d.cpp                                                */
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

#include "nav_polygon_bvh_3d.h"

#include "core/templates/sort_array.h"

using namespace Nav3D;

namespace {

struct NavPolygonCenterComparator {
	const Vector3 *centers = nullptr;
	int axis = 0;

	_FORCE_INLINE_ bool operator()(uint32_t p_a, uint32_t p_b) const {
		return centers[p_a][axis] < centers[p_b][axis];
	}
};

struct NavPolygonBuildRange {
	uint32_t node = 0;
	uint32_t begin = 0;
	uint32_t end = 0;
};

} // namespace

void NavPolygonBVH3D::clear() {
	nodes.clear();
	items.clear();
	always.clear();
	built = false;
}

void NavPolygonBVH3D::build(const LocalVector<Polygon> &p_polygons) {
	clear();

	const uint32_t polygon_count = p_polygons.size();
	if (polygon_count == 0) {
		return;
	}
	built = true;

	LocalVector<AABB> boxes;
	boxes.resize(polygon_count);
	LocalVector<Vector3> centers;
	centers.resize(polygon_count);
	items.reserve(polygon_count);

	for (uint32_t i = 0; i < polygon_count; i++) {
		const LocalVector<Vector3> &vertices = p_polygons[i].vertices;
		if (vertices.size() < 3) {
			continue;
		}

		bool finite = true;
		AABB box(vertices[0], Vector3());
		for (const Vector3 &vertex : vertices) {
			finite = finite && vertex.is_finite();
			box.expand_to(vertex);
		}

		const Vector3 edge_a = vertices[1] - vertices[0];
		const Vector3 edge_b = vertices[2] - vertices[0];
		const Vector3 plane_normal = edge_a.cross(edge_b);
		const bool well_formed = plane_normal.length_squared() > real_t(1e-6) * edge_a.length_squared() * edge_b.length_squared();
		if (!finite || !well_formed) {
			always.push_back(i);
			continue;
		}

		const Vector3 plane_normalized = plane_normal.normalized();
		for (const Vector3 &vertex : vertices) {
			box.expand_to(vertex - plane_normalized * plane_normalized.dot(vertex - vertices[0]));
		}

		const Vector3 end = box.position + box.size;
		const real_t max_abs = MAX(MAX(MAX(Math::abs(box.position.x), Math::abs(box.position.y)), MAX(Math::abs(box.position.z), Math::abs(end.x))), MAX(Math::abs(end.y), Math::abs(end.z)));
		box = box.grow(real_t(0.05) + real_t(1e-4) * max_abs);

		boxes[i] = box;
		centers[i] = box.get_center();
		items.push_back(i);
	}

	if (items.is_empty()) {
		return;
	}

	nodes.reserve(2 * (items.size() / (LEAF_SIZE / 2) + 1));
	nodes.push_back(Node());

	LocalVector<NavPolygonBuildRange> todo;
	todo.push_back({ 0, 0, items.size() });

	SortArray<uint32_t, NavPolygonCenterComparator> sorter;
	sorter.compare.centers = centers.ptr();

	while (!todo.is_empty()) {
		const NavPolygonBuildRange range = todo[todo.size() - 1];
		todo.resize(todo.size() - 1);

		AABB bounds = boxes[items[range.begin]];
		AABB center_bounds(centers[items[range.begin]], Vector3());
		for (uint32_t k = range.begin + 1; k < range.end; k++) {
			bounds.merge_with(boxes[items[k]]);
			center_bounds.expand_to(centers[items[k]]);
		}
		nodes[range.node].aabb = bounds;

		const uint32_t count = range.end - range.begin;
		if (count <= LEAF_SIZE) {
			nodes[range.node].first = range.begin;
			nodes[range.node].count = count;
			continue;
		}

		const uint32_t mid = range.begin + count / 2;
		sorter.compare.axis = center_bounds.get_longest_axis_index();
		sorter.nth_element(range.begin, range.end, mid, items.ptr());

		const uint32_t child = nodes.size();
		nodes.push_back(Node());
		nodes.push_back(Node());
		nodes[range.node].first = child;
		nodes[range.node].count = 0;

		todo.push_back({ child, range.begin, mid });
		todo.push_back({ child + 1, mid, range.end });
	}
}

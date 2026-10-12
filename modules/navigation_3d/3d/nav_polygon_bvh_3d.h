/**************************************************************************/
/*  nav_polygon_bvh_3d.h                                                  */
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

#include "../nav_utils_3d.h"

#include "core/math/aabb.h"
#include "core/templates/local_vector.h"

// static bvh for one region iteration's polygons
// built once, read-only forever. no locks
// just filters candidates; the surviving polygons still go through the og brute-force code in the og order
//
// Each polygon's box covers every possible closest point: vertices, edges, faces,
// and vertex projections onto the plane of the first three vertices (the "inside" case)
// plus some padding because floating-point math likes to gaslight us
// degenerate planes or non-finite vertices? straight to `always`
// those polygons get checked every query because we don't trust them
class NavPolygonBVH3D {
public:
	struct Node {
		AABB aabb;
		uint32_t first = 0;
		uint32_t count = 0;
	};

private:
	LocalVector<Node> nodes;
	LocalVector<uint32_t> items;
	LocalVector<uint32_t> always;
	bool built = false;

	static constexpr uint32_t LEAF_SIZE = 6;
	static constexpr uint32_t STACK_SIZE = 96;

public:
	void build(const LocalVector<Nav3D::Polygon> &p_polygons);
	void clear();

	bool is_built() const { return built; }
	bool has_tree() const { return !nodes.is_empty(); }
	const AABB &get_root_aabb() const { return nodes[0].aabb; }
	const LocalVector<uint32_t> &get_always() const { return always; }

	static _FORCE_INLINE_ real_t aabb_distance_squared(const AABB &p_aabb, const Vector3 &p_point) {
		const Vector3 end = p_aabb.position + p_aabb.size;
		real_t d = 0.0;
		for (int axis = 0; axis < 3; axis++) {
			real_t v = 0.0;
			if (p_point[axis] < p_aabb.position[axis]) {
				v = p_aabb.position[axis] - p_point[axis];
			} else if (p_point[axis] > end[axis]) {
				v = p_point[axis] - end[axis];
			}
			d += v * v;
		}
		return d;
	}

	template <typename PruneFn, typename VisitFn>
	void walk(const Vector3 &p_point, PruneFn p_prune, VisitFn p_visit) const {
		if (nodes.is_empty()) {
			return;
		}
		uint32_t stack[STACK_SIZE];
		real_t stack_bound[STACK_SIZE];
		uint32_t stack_size = 0;

		stack[0] = 0;
		stack_bound[0] = aabb_distance_squared(nodes[0].aabb, p_point);
		stack_size = 1;

		while (stack_size > 0) {
			stack_size--;
			const uint32_t node_index = stack[stack_size];
			if (p_prune(stack_bound[stack_size])) {
				continue;
			}
			const Node &node = nodes[node_index];
			if (node.count > 0) {
				for (uint32_t i = node.first; i < node.first + node.count; i++) {
					p_visit(items[i]);
				}
				continue;
			}
			const real_t bound_a = aabb_distance_squared(nodes[node.first].aabb, p_point);
			const real_t bound_b = aabb_distance_squared(nodes[node.first + 1].aabb, p_point);
			if (stack_size + 2 > STACK_SIZE) {
				_visit_subtree(node_index, p_visit);
				continue;
			}
			if (bound_a <= bound_b) {
				stack[stack_size] = node.first + 1;
				stack_bound[stack_size] = bound_b;
				stack[stack_size + 1] = node.first;
				stack_bound[stack_size + 1] = bound_a;
			} else {
				stack[stack_size] = node.first;
				stack_bound[stack_size] = bound_a;
				stack[stack_size + 1] = node.first + 1;
				stack_bound[stack_size + 1] = bound_b;
			}
			stack_size += 2;
		}
	}

private:
	template <typename VisitFn>
	void _visit_subtree(uint32_t p_node_index, VisitFn &p_visit) const {
		LocalVector<uint32_t> pending;
		pending.push_back(p_node_index);
		while (!pending.is_empty()) {
			const uint32_t index = pending[pending.size() - 1];
			pending.resize(pending.size() - 1);
			const Node &node = nodes[index];
			if (node.count > 0) {
				for (uint32_t i = node.first; i < node.first + node.count; i++) {
					p_visit(items[i]);
				}
			} else {
				pending.push_back(node.first);
				pending.push_back(node.first + 1);
			}
		}
	}
};

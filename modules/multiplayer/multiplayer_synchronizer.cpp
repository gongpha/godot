/**************************************************************************/
/*  multiplayer_synchronizer.cpp                                          */
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

#include "multiplayer_synchronizer.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h" // 66
#include "scene/main/multiplayer_api.h"
// 66 begin
#include "scene_multiplayer.h"
// 66 end

Object *MultiplayerSynchronizer::_get_prop_target(Object *p_obj, const NodePath &p_path) {
	if (p_path.get_name_count() == 0) {
		return p_obj;
	}
	Node *node = Object::cast_to<Node>(p_obj);
	ERR_FAIL_COND_V_MSG(!node || !node->has_node(p_path), nullptr, vformat("Node '%s' not found.", p_path));
	return node->get_node(p_path);
}

void MultiplayerSynchronizer::_stop() {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
#endif
	root_node_cache = ObjectID();
	Node *node = is_inside_tree() ? get_node_or_null(root_path) : nullptr;
	if (node) {
		get_multiplayer()->object_configuration_remove(node, this);
	}
	reset();
}

void MultiplayerSynchronizer::_start() {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
#endif
	root_node_cache = ObjectID();
	reset();
	Node *node = is_inside_tree() ? get_node_or_null(root_path) : nullptr;
	if (node) {
		root_node_cache = node->get_instance_id();
		get_multiplayer()->object_configuration_add(node, this);
		_update_process();
	}
}

void MultiplayerSynchronizer::_update_process() {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
#endif
	Node *node = is_inside_tree() ? get_node_or_null(root_path) : nullptr;
	if (!node) {
		return;
	}
	set_process_internal(false);
	set_physics_process_internal(false);

	// 66 begin
	if (visibility_filters.size()) {
		switch (visibility_update_mode) {
			case VISIBILITY_PROCESS_IDLE:
				set_process_internal(true);
				break;
			case VISIBILITY_PROCESS_PHYSICS:
				set_physics_process_internal(true);
				break;
			case VISIBILITY_PROCESS_NONE:
				break;
		}
	}

	if (_has_interpolate_properties() && !is_multiplayer_authority()) {
		set_process_internal(true);
	}
	// 66 end
}

Node *MultiplayerSynchronizer::get_root_node() {
	return root_node_cache.is_valid() ? ObjectDB::get_instance<Node>(root_node_cache) : nullptr;
}

void MultiplayerSynchronizer::reset() {
	net_id = 0;
	last_sync_usec = 0;
	last_inbound_sync = 0;
	last_watch_usec = 0;
	sync_started = false;
	watchers.clear();
	// 66 begin
	interpolation_states.clear();
	interpolation_snapshots.clear();
	interpolation_fraction = 0.0;
	last_sync_receive_usec = 0;
	sync_receive_interval_usec = 0;
	interpolation_render_usec = 0;
	interpolation_clock_usec = 0;
	interpolation_slew = 0.0;
	interpolation_skip_count = 0;
	interpolation_skipped_usec = 0;
	// 66 end
}

uint32_t MultiplayerSynchronizer::get_net_id() const {
	return net_id;
}

void MultiplayerSynchronizer::set_net_id(uint32_t p_net_id) {
	net_id = p_net_id;
}

bool MultiplayerSynchronizer::update_outbound_sync_time(uint64_t p_usec) {
	if (last_sync_usec == p_usec) {
		// last_sync_usec has been updated in this frame.
		return true;
	}
	if (p_usec < last_sync_usec + sync_interval_usec) {
		// Too soon, should skip this synchronization frame.
		return false;
	}
	last_sync_usec = p_usec;
	return true;
}

bool MultiplayerSynchronizer::update_inbound_sync_time(uint16_t p_network_time) {
	if (!sync_started) {
		sync_started = true;
	} else if (p_network_time <= last_inbound_sync && last_inbound_sync - p_network_time < 32767) {
		return false;
	}
	last_inbound_sync = p_network_time;
	return true;
}

PackedStringArray MultiplayerSynchronizer::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();

	if (root_path.is_empty() || !has_node(root_path)) {
		warnings.push_back(RTR("A valid NodePath must be set in the \"Root Path\" property in order for MultiplayerSynchronizer to be able to synchronize properties."));
	}

	return warnings;
}

Error MultiplayerSynchronizer::get_state(const List<NodePath> &p_properties, Object *p_obj, Vector<Variant> &r_variant, Vector<const Variant *> &r_variant_ptrs) {
	ERR_FAIL_NULL_V(p_obj, ERR_INVALID_PARAMETER);
	r_variant.resize(p_properties.size());
	r_variant_ptrs.resize(r_variant.size());
	int i = 0;
	for (const NodePath &prop : p_properties) {
		bool valid = false;
		const Object *obj = _get_prop_target(p_obj, prop);
		ERR_FAIL_NULL_V(obj, FAILED);
		r_variant.write[i] = obj->get_indexed(prop.get_subnames(), &valid);
		r_variant_ptrs.write[i] = &r_variant[i];
		ERR_FAIL_COND_V_MSG(!valid, ERR_INVALID_DATA, vformat("Property '%s' not found.", prop));
		i++;
	}
	return OK;
}

Error MultiplayerSynchronizer::set_state(const List<NodePath> &p_properties, Object *p_obj, const Vector<Variant> &p_state) {
	ERR_FAIL_NULL_V(p_obj, ERR_INVALID_PARAMETER);
	int i = 0;
	for (const NodePath &prop : p_properties) {
		Object *obj = _get_prop_target(p_obj, prop);
		ERR_FAIL_NULL_V(obj, FAILED);
		obj->set_indexed(prop.get_subnames(), p_state[i]);
		i += 1;
	}
	return OK;
}

bool MultiplayerSynchronizer::is_visibility_public() const {
	return peer_visibility.has(0);
}

void MultiplayerSynchronizer::set_visibility_public(bool p_visible) {
	set_visibility_for(0, p_visible);
}

bool MultiplayerSynchronizer::is_visible_to(int p_peer) {
	if (visibility_filters.size()) {
		Variant arg = p_peer;
		const Variant *argv[1] = { &arg };
		for (Callable filter : visibility_filters) {
			Variant ret;
			Callable::CallError err;
			filter.callp(argv, 1, ret, err);
			ERR_FAIL_COND_V(err.error != Callable::CallError::CALL_OK || ret.get_type() != Variant::BOOL, false);
			if (!ret.operator bool()) {
				return false;
			}
		}
	}
	return peer_visibility.has(0) || peer_visibility.has(p_peer);
}

void MultiplayerSynchronizer::add_visibility_filter(Callable p_callback) {
	visibility_filters.insert(p_callback);
	_update_process();
}

void MultiplayerSynchronizer::remove_visibility_filter(Callable p_callback) {
	visibility_filters.erase(p_callback);
	_update_process();
}

void MultiplayerSynchronizer::set_visibility_for(int p_peer, bool p_visible) {
	if (peer_visibility.has(p_peer) == p_visible) {
		return;
	}
	if (p_visible) {
		peer_visibility.insert(p_peer);

		// 66 begin
		reset_interpolation();
		// 66 end
	} else {
		peer_visibility.erase(p_peer);
	}
	update_visibility(p_peer);
}

bool MultiplayerSynchronizer::get_visibility_for(int p_peer) const {
	return peer_visibility.has(p_peer);
}

void MultiplayerSynchronizer::set_visibility_update_mode(VisibilityUpdateMode p_mode) {
	visibility_update_mode = p_mode;
	_update_process();
}

MultiplayerSynchronizer::VisibilityUpdateMode MultiplayerSynchronizer::get_visibility_update_mode() const {
	return visibility_update_mode;
}

void MultiplayerSynchronizer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_root_path", "path"), &MultiplayerSynchronizer::set_root_path);
	ClassDB::bind_method(D_METHOD("get_root_path"), &MultiplayerSynchronizer::get_root_path);

	ClassDB::bind_method(D_METHOD("set_replication_interval", "milliseconds"), &MultiplayerSynchronizer::set_replication_interval);
	ClassDB::bind_method(D_METHOD("get_replication_interval"), &MultiplayerSynchronizer::get_replication_interval);

	ClassDB::bind_method(D_METHOD("set_delta_interval", "milliseconds"), &MultiplayerSynchronizer::set_delta_interval);
	ClassDB::bind_method(D_METHOD("get_delta_interval"), &MultiplayerSynchronizer::get_delta_interval);

	ClassDB::bind_method(D_METHOD("set_replication_config", "config"), &MultiplayerSynchronizer::set_replication_config);
	ClassDB::bind_method(D_METHOD("get_replication_config"), &MultiplayerSynchronizer::get_replication_config);

	ClassDB::bind_method(D_METHOD("set_visibility_update_mode", "mode"), &MultiplayerSynchronizer::set_visibility_update_mode);
	ClassDB::bind_method(D_METHOD("get_visibility_update_mode"), &MultiplayerSynchronizer::get_visibility_update_mode);
	ClassDB::bind_method(D_METHOD("update_visibility", "for_peer"), &MultiplayerSynchronizer::update_visibility, DEFVAL(0));

	ClassDB::bind_method(D_METHOD("set_visibility_public", "visible"), &MultiplayerSynchronizer::set_visibility_public);
	ClassDB::bind_method(D_METHOD("is_visibility_public"), &MultiplayerSynchronizer::is_visibility_public);

	ClassDB::bind_method(D_METHOD("add_visibility_filter", "filter"), &MultiplayerSynchronizer::add_visibility_filter);
	ClassDB::bind_method(D_METHOD("remove_visibility_filter", "filter"), &MultiplayerSynchronizer::remove_visibility_filter);
	ClassDB::bind_method(D_METHOD("set_visibility_for", "peer", "visible"), &MultiplayerSynchronizer::set_visibility_for);
	ClassDB::bind_method(D_METHOD("get_visibility_for", "peer"), &MultiplayerSynchronizer::get_visibility_for);

	// 66 begin
	ClassDB::bind_method(D_METHOD("get_interpolation_fraction"), &MultiplayerSynchronizer::get_interpolation_fraction);
	ClassDB::bind_method(D_METHOD("notify_sync_receive"), &MultiplayerSynchronizer::notify_sync_receive);
	ClassDB::bind_method(D_METHOD("reset_interpolation"), &MultiplayerSynchronizer::reset_interpolation);
	ClassDB::bind_method(D_METHOD("request_full_state_update"), &MultiplayerSynchronizer::request_full_state_update);
	// 66 end

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "root_path"), "set_root_path", "get_root_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "replication_interval", PROPERTY_HINT_RANGE, "0,5,0.001,suffix:s"), "set_replication_interval", "get_replication_interval");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "delta_interval", PROPERTY_HINT_RANGE, "0,5,0.001,suffix:s"), "set_delta_interval", "get_delta_interval");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "replication_config", PROPERTY_HINT_RESOURCE_TYPE, SceneReplicationConfig::get_class_static(), PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_EDITOR_INSTANTIATE_OBJECT), "set_replication_config", "get_replication_config");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_update_mode", PROPERTY_HINT_ENUM, "Idle,Physics,None"), "set_visibility_update_mode", "get_visibility_update_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "public_visibility"), "set_visibility_public", "is_visibility_public");

	BIND_ENUM_CONSTANT(VISIBILITY_PROCESS_IDLE);
	BIND_ENUM_CONSTANT(VISIBILITY_PROCESS_PHYSICS);
	BIND_ENUM_CONSTANT(VISIBILITY_PROCESS_NONE);

	ADD_SIGNAL(MethodInfo("synchronized"));
	ADD_SIGNAL(MethodInfo("delta_synchronized"));
	ADD_SIGNAL(MethodInfo("visibility_changed", PropertyInfo(Variant::INT, "for_peer")));
}

void MultiplayerSynchronizer::_notification(int p_what) {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
#endif
	if (root_path.is_empty()) {
		return;
	}

	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_start();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_stop();
		} break;

		// 66 begin
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (visibility_filters.size() && visibility_update_mode == VISIBILITY_PROCESS_IDLE) {
				update_visibility(0);
			}
			if (_has_interpolate_properties() && !is_multiplayer_authority()) {
				_apply_interpolation(get_process_delta_time());
			}
		} break;
			// 66 end

		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			// 66 begin
			if (visibility_filters.size() && visibility_update_mode == VISIBILITY_PROCESS_PHYSICS) {
				update_visibility(0);
			}
			// 66 end
		} break;
	}
}

void MultiplayerSynchronizer::set_replication_interval(double p_interval) {
	ERR_FAIL_COND_MSG(p_interval < 0, "Interval must be greater or equal to 0 (where 0 means default)");
	sync_interval_usec = uint64_t(p_interval * 1000 * 1000);
}

double MultiplayerSynchronizer::get_replication_interval() const {
	return double(sync_interval_usec) / 1000.0 / 1000.0;
}

void MultiplayerSynchronizer::set_delta_interval(double p_interval) {
	ERR_FAIL_COND_MSG(p_interval < 0, "Interval must be greater or equal to 0 (where 0 means default)");
	delta_interval_usec = uint64_t(p_interval * 1000 * 1000);
}

double MultiplayerSynchronizer::get_delta_interval() const {
	return double(delta_interval_usec) / 1000.0 / 1000.0;
}

void MultiplayerSynchronizer::set_replication_config(Ref<SceneReplicationConfig> p_config) {
	replication_config = p_config;
}

Ref<SceneReplicationConfig> MultiplayerSynchronizer::get_replication_config() {
	return replication_config;
}

void MultiplayerSynchronizer::update_visibility(int p_for_peer) {
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
#endif
	Node *node = is_inside_tree() ? get_node_or_null(root_path) : nullptr;
	if (node && get_multiplayer()->has_multiplayer_peer() && is_multiplayer_authority()) {
		emit_signal(SceneStringName(visibility_changed), p_for_peer);
	}
}

void MultiplayerSynchronizer::set_root_path(const NodePath &p_path) {
	if (p_path == root_path) {
		return;
	}
	_stop();
	root_path = p_path;
	_start();
	update_configuration_warnings();
}

NodePath MultiplayerSynchronizer::get_root_path() const {
	return root_path;
}

void MultiplayerSynchronizer::set_multiplayer_authority(int p_peer_id, bool p_recursive) {
	if (get_multiplayer_authority() == p_peer_id) {
		return;
	}
	_stop();
	Node::set_multiplayer_authority(p_peer_id, p_recursive);
	_start();
}

Error MultiplayerSynchronizer::_watch_changes(uint64_t p_usec) {
	ERR_FAIL_COND_V(replication_config.is_null(), FAILED);
	const List<NodePath> props(replication_config->get_watch_properties());
	if (props.size() != watchers.size()) {
		watchers.resize(props.size());
	}
	if (props.is_empty()) {
		return OK;
	}
	Node *node = get_root_node();
	ERR_FAIL_NULL_V(node, FAILED);
	int idx = -1;
	Watcher *ptr = watchers.ptrw();
	for (const NodePath &prop : props) {
		idx++;
		bool valid = false;
		const Object *obj = _get_prop_target(node, prop);
		ERR_CONTINUE_MSG(!obj, vformat("Node not found for property '%s'.", prop));
		Variant v = obj->get_indexed(prop.get_subnames(), &valid);
		ERR_CONTINUE_MSG(!valid, vformat("Property '%s' not found.", prop));
		Watcher &w = ptr[idx];
		if (w.prop != prop) {
			w.prop = prop;
			w.value = v.duplicate(true);
			w.last_change_usec = p_usec;
		} else if (!w.value.hash_compare(v)) {
			w.value = v.duplicate(true);
			w.last_change_usec = p_usec;
		}
	}
	return OK;
}

List<Variant> MultiplayerSynchronizer::get_delta_state(uint64_t p_cur_usec, uint64_t p_last_usec, uint64_t &r_indexes) {
	r_indexes = 0;
	List<Variant> out;

	// 66 begin
	const bool send_all = (p_last_usec == 0);
	// 66 end

	if (last_watch_usec == p_cur_usec) {
		// We already watched for changes in this frame.
		// 66 begin
		if (watchers.is_empty()) {
			_watch_changes(p_cur_usec);
		}
		// 66 end
	} else if (!send_all && p_cur_usec < p_last_usec + delta_interval_usec) { // 66
		// Too soon skip delta synchronization.
		// 66
		return out;

	} else {
		// Watch for changes.
		Error err = _watch_changes(p_cur_usec);
		ERR_FAIL_COND_V(err != OK, out);
		last_watch_usec = p_cur_usec;
	}

	const Watcher *ptr = watchers.size() ? watchers.ptr() : nullptr;
	for (int i = 0; i < watchers.size(); i++) {
		const Watcher &w = ptr[i];
		if (!send_all && w.last_change_usec <= p_last_usec) { // 66
			continue;
		}
		out.push_back(w.value);
		r_indexes |= 1ULL << i;
	}
	return out;
}

List<NodePath> MultiplayerSynchronizer::get_delta_properties(uint64_t p_indexes) {
	List<NodePath> out;
	ERR_FAIL_COND_V(replication_config.is_null(), out);
	const List<NodePath> watch_props(replication_config->get_watch_properties());
	int idx = 0;
	for (const NodePath &prop : watch_props) {
		if ((p_indexes & (1ULL << idx++)) == 0) {
			continue;
		}
		out.push_back(prop);
	}
	return out;
}

SceneReplicationConfig *MultiplayerSynchronizer::get_replication_config_ptr() const {
	return replication_config.ptr();
}

// 66 begin
Variant MultiplayerSynchronizer::_interpolate_variant(const Variant &p_from, const Variant &p_to, double p_fraction) {
	switch (p_from.get_type()) {
		case Variant::INT: {
			int64_t from = p_from;
			int64_t to = p_to;
			return (int64_t)Math::round(Math::lerp((double)from, (double)to, p_fraction));
		}
		case Variant::FLOAT: {
			double from = p_from;
			double to = p_to;
			return Math::lerp(from, to, p_fraction);
		}
		case Variant::VECTOR2: {
			Vector2 from = p_from;
			Vector2 to = p_to;
			return from.lerp(to, p_fraction);
		}
		case Variant::VECTOR2I: {
			Vector2i from = p_from;
			Vector2i to = p_to;
			return Vector2i(
					(int32_t)Math::round(Math::lerp((double)from.x, (double)to.x, p_fraction)),
					(int32_t)Math::round(Math::lerp((double)from.y, (double)to.y, p_fraction)));
		}
		case Variant::VECTOR3: {
			Vector3 from = p_from;
			Vector3 to = p_to;
			return from.lerp(to, p_fraction);
		}
		case Variant::VECTOR3I: {
			Vector3i from = p_from;
			Vector3i to = p_to;
			return Vector3i(
					(int32_t)Math::round(Math::lerp((double)from.x, (double)to.x, p_fraction)),
					(int32_t)Math::round(Math::lerp((double)from.y, (double)to.y, p_fraction)),
					(int32_t)Math::round(Math::lerp((double)from.z, (double)to.z, p_fraction)));
		}
		case Variant::VECTOR4: {
			Vector4 from = p_from;
			Vector4 to = p_to;
			return from.lerp(to, p_fraction);
		}
		case Variant::VECTOR4I: {
			Vector4i from = p_from;
			Vector4i to = p_to;
			return Vector4i(
					(int32_t)Math::round(Math::lerp((double)from.x, (double)to.x, p_fraction)),
					(int32_t)Math::round(Math::lerp((double)from.y, (double)to.y, p_fraction)),
					(int32_t)Math::round(Math::lerp((double)from.z, (double)to.z, p_fraction)),
					(int32_t)Math::round(Math::lerp((double)from.w, (double)to.w, p_fraction)));
		}
		case Variant::QUATERNION: {
			Quaternion from = p_from;
			Quaternion to = p_to;
			return from.slerp(to, p_fraction);
		}
		case Variant::BASIS: {
			Basis from = p_from;
			Basis to = p_to;
			return from.slerp(to, p_fraction);
		}
		case Variant::TRANSFORM2D: {
			Transform2D from = p_from;
			Transform2D to = p_to;
			return from.interpolate_with(to, p_fraction);
		}
		case Variant::TRANSFORM3D: {
			Transform3D from = p_from;
			Transform3D to = p_to;
			return from.interpolate_with(to, p_fraction);
		}
		case Variant::COLOR: {
			Color from = p_from;
			Color to = p_to;
			return from.lerp(to, p_fraction);
		}
		case Variant::RECT2: {
			Rect2 from = p_from;
			Rect2 to = p_to;
			return Rect2(
					from.position.lerp(to.position, p_fraction),
					from.size.lerp(to.size, p_fraction));
		}
		case Variant::RECT2I: {
			Rect2i from = p_from;
			Rect2i to = p_to;
			return Rect2i(
					Vector2i(
							(int32_t)Math::round(Math::lerp((double)from.position.x, (double)to.position.x, p_fraction)),
							(int32_t)Math::round(Math::lerp((double)from.position.y, (double)to.position.y, p_fraction))),
					Vector2i(
							(int32_t)Math::round(Math::lerp((double)from.size.x, (double)to.size.x, p_fraction)),
							(int32_t)Math::round(Math::lerp((double)from.size.y, (double)to.size.y, p_fraction))));
		}
		case Variant::AABB: {
			AABB from = p_from;
			AABB to = p_to;
			return AABB(
					from.position.lerp(to.position, p_fraction),
					from.size.lerp(to.size, p_fraction));
		}
		case Variant::PLANE: {
			Plane from = p_from;
			Plane to = p_to;
			return Plane(
					from.normal.lerp(to.normal, p_fraction).normalized(),
					Math::lerp(from.d, to.d, (real_t)p_fraction));
		}
		default:
			return p_fraction >= 0.5 ? p_to : p_from;
	}
}

static constexpr uint64_t INTERP_MIN_INTERVAL_USEC = 8000;
static constexpr uint64_t INTERP_DEFAULT_INTERVAL_USEC = 50000;
static constexpr uint64_t INTERP_BURST_USEC = 4000;
static constexpr uint64_t INTERP_QUIET_MIN_USEC = 100000;
static constexpr uint64_t INTERP_MAX_INTERVAL_USEC = 200000;
static constexpr uint64_t INTERP_DELAY_INTERVALS = 2;
static constexpr int INTERP_MAX_SNAPSHOTS = 16;
static constexpr double INTERP_MAX_SLEW = 0.1;
static constexpr double INTERP_STEER_USEC = 500000.0;
static constexpr int64_t INTERP_SNAP_INTERVALS = 4;

void MultiplayerSynchronizer::_capture_interpolation_snapshot(Node *p_node, uint64_t p_stamp_usec, bool p_reuse_unchanged) {
	const List<NodePath> &interp_props = replication_config->get_interpolate_properties();
	if (interpolation_states.size() != interp_props.size()) {
		interpolation_states.resize(interp_props.size());
	}
	const InterpolationSnapshot *prev = interpolation_snapshots.is_empty() ? nullptr : &interpolation_snapshots[interpolation_snapshots.size() - 1];
	InterpolationSnapshot snap;
	snap.stamp_usec = p_stamp_usec;
	snap.values.resize(interp_props.size());
	int idx = 0;
	for (const NodePath &prop : interp_props) {
		InterpolationState &state = interpolation_states.write[idx];
		state.prop = prop;
		Object *obj = _get_prop_target(p_node, prop);
		bool valid = false;
		Variant value = obj ? obj->get_indexed(prop.get_subnames(), &valid) : Variant();
		state.valid = valid;
		if (valid) {
			if (p_reuse_unchanged && prev && state.written && value == state.last_written && idx < prev->values.size()) {
				value = prev->values[idx];
			}
			snap.values.write[idx] = value;
		}
		idx++;
	}
	interpolation_snapshots.push_back(snap);
	while (interpolation_snapshots.size() > INTERP_MAX_SNAPSHOTS) {
		interpolation_snapshots.remove_at(0);
	}
}

void MultiplayerSynchronizer::_write_interpolated(Node *p_node, const InterpolationSnapshot &p_from, const InterpolationSnapshot &p_to, double p_fraction) {
	for (int i = 0; i < interpolation_states.size(); i++) {
		InterpolationState &state = interpolation_states.write[i];
		if (!state.valid || i >= p_to.values.size()) {
			continue;
		}
		Object *obj = _get_prop_target(p_node, state.prop);
		if (!obj) {
			continue;
		}
		const Variant &to = p_to.values[i];
		const Variant &from = (i < p_from.values.size() && p_from.values[i].get_type() == to.get_type()) ? p_from.values[i] : to;
		Variant value = _interpolate_variant(from, to, p_fraction);
		obj->set_indexed(state.prop.get_subnames(), value);
		state.last_written = value;
		state.written = true;
	}
}

void MultiplayerSynchronizer::_apply_interpolation(double p_delta) {
	if (!_has_interpolate_properties() || interpolation_snapshots.is_empty() || last_sync_receive_usec == 0) {
		return;
	}

	Node *node = get_root_node();
	if (!node) {
		return;
	}

	uint64_t now = OS::get_singleton()->get_ticks_usec();
	_advance_interpolation_clock(now);
	uint64_t render = interpolation_render_usec;

	while (interpolation_snapshots.size() > 2 && interpolation_snapshots[1].stamp_usec <= render) {
		interpolation_snapshots.remove_at(0);
	}

	const InterpolationSnapshot &a = interpolation_snapshots[0];
	if (interpolation_snapshots.size() == 1) {
		interpolation_fraction = 1.0;
		_write_interpolated(node, a, a, 1.0);
		return;
	}
	const InterpolationSnapshot &b = interpolation_snapshots[1];
	if (render <= a.stamp_usec) {
		interpolation_fraction = 0.0;
	} else if (render >= b.stamp_usec) {
		interpolation_fraction = 1.0;
	} else {
		interpolation_fraction = double(render - a.stamp_usec) / double(b.stamp_usec - a.stamp_usec);
	}
	_write_interpolated(node, a, b, interpolation_fraction);
}

void MultiplayerSynchronizer::_advance_interpolation_clock(uint64_t p_now) {
	if (interpolation_clock_usec == 0 || p_now <= interpolation_clock_usec) {
		interpolation_clock_usec = MAX(interpolation_clock_usec, p_now);
		return;
	}
	double elapsed = double(p_now - interpolation_clock_usec);
	interpolation_clock_usec = p_now;
	interpolation_render_usec += uint64_t(elapsed * (1.0 + interpolation_slew));
}

bool MultiplayerSynchronizer::_has_interpolate_properties() const {
	if (replication_config.is_null()) {
		return false;
	}
	return replication_config->get_interpolate_properties().size() > 0;
}

double MultiplayerSynchronizer::get_interpolation_fraction() const {
	return interpolation_fraction;
}

void MultiplayerSynchronizer::notify_sync_receive() {
	if (!_has_interpolate_properties()) {
		return;
	}

	Node *node = get_root_node();
	if (!node) {
		return;
	}

	uint64_t current_usec = OS::get_singleton()->get_ticks_usec();

	// 66
	if (interpolation_skip_count > 0) {
		interpolation_skip_count--;
		if (!interpolation_snapshots.is_empty()) {
			const InterpolationSnapshot &held = interpolation_snapshots[interpolation_snapshots.size() - 1];
			_write_interpolated(node, held, held, 1.0);
		}
		last_sync_receive_usec = current_usec;
		interpolation_skipped_usec = current_usec;
		return;
	}

	uint64_t gap = last_sync_receive_usec > 0 ? current_usec - last_sync_receive_usec : 0;
	bool burst = last_sync_receive_usec > 0 && gap < INTERP_BURST_USEC;
	uint64_t interval = sync_receive_interval_usec > 0 ? sync_receive_interval_usec : INTERP_DEFAULT_INTERVAL_USEC;
	uint64_t quiet_usec = MAX(interval * INTERP_SNAP_INTERVALS, INTERP_QUIET_MIN_USEC);
	bool quiet = last_sync_receive_usec == 0 || gap >= quiet_usec;
	if (!burst && !quiet) {
		if (sync_receive_interval_usec == 0) {
			sync_receive_interval_usec = CLAMP(gap, INTERP_MIN_INTERVAL_USEC, INTERP_MAX_INTERVAL_USEC);
		} else {
			int64_t est = int64_t(sync_receive_interval_usec) + (int64_t(gap) - int64_t(sync_receive_interval_usec)) / 8;
			sync_receive_interval_usec = CLAMP(uint64_t(MAX(est, int64_t(0))), INTERP_MIN_INTERVAL_USEC, INTERP_MAX_INTERVAL_USEC);
		}
		interval = sync_receive_interval_usec;
	}
	last_sync_receive_usec = current_usec;

	uint64_t stamp = current_usec;
	if (!interpolation_snapshots.is_empty()) {
		InterpolationSnapshot held = interpolation_snapshots[interpolation_snapshots.size() - 1];
		if (stamp >= held.stamp_usec + quiet_usec) {
			quiet = true;
		}
		if (quiet) {
			bool skipped_starts_move = interpolation_skipped_usec > 0 && interpolation_skipped_usec < stamp && stamp - interpolation_skipped_usec < quiet_usec;
			uint64_t move_start = skipped_starts_move ? interpolation_skipped_usec : stamp;
			if (move_start > interval && held.stamp_usec < move_start - interval) {
				held.stamp_usec = move_start - interval;
				interpolation_snapshots.push_back(held);
			}
		}
		if (stamp <= held.stamp_usec) {
			stamp = held.stamp_usec + 1;
		}
	}
	interpolation_skipped_usec = 0;

	_advance_interpolation_clock(current_usec);
	uint64_t delay = interval * INTERP_DELAY_INTERVALS;
	int64_t target = int64_t(stamp) - int64_t(delay);
	int64_t err = target - int64_t(interpolation_render_usec);
	if (quiet || interpolation_render_usec == 0 || (err < 0 ? -err : err) > int64_t(interval * INTERP_SNAP_INTERVALS)) {
		interpolation_render_usec = uint64_t(MAX(target, int64_t(0)));
		interpolation_clock_usec = current_usec;
		interpolation_slew = 0.0;
	} else {
		interpolation_slew = CLAMP(double(err) / INTERP_STEER_USEC, -INTERP_MAX_SLEW, INTERP_MAX_SLEW);
	}
	_capture_interpolation_snapshot(node, stamp, true);
	_apply_interpolation(0.0);
}

void MultiplayerSynchronizer::reset_interpolation() {
	// 66
	Node *node = get_root_node();
	interpolation_snapshots.clear();
	interpolation_skipped_usec = 0;
	if (node && _has_interpolate_properties()) {
		// 66
		uint64_t now = OS::get_singleton()->get_ticks_usec();
		_capture_interpolation_snapshot(node, now, false);
		const InterpolationSnapshot &held = interpolation_snapshots[0];
		for (int i = 0; i < interpolation_states.size(); i++) {
			InterpolationState &state = interpolation_states.write[i];
			if (state.valid) {
				state.last_written = held.values[i];
				state.written = true;
			}
		}
		interpolation_skip_count = 1;
	} else {
		interpolation_states.clear();
		interpolation_skip_count = 0;
	}

	interpolation_fraction = 1.0;
}

void MultiplayerSynchronizer::request_full_state_update() {
	if (replication_config.is_null()) {
		return;
	}

	uint64_t current_usec = OS::get_singleton()->get_ticks_usec();

	if (watchers.is_empty()) {
		_watch_changes(current_usec);
	}

	for (int i = 0; i < watchers.size(); i++) {
		watchers.write[i].last_change_usec = current_usec;
	}

	last_watch_usec = 0;

	// 66
	Ref<MultiplayerAPI> api = get_multiplayer();
	if (api.is_valid()) {
		SceneMultiplayer *sm = Object::cast_to<SceneMultiplayer>(api.ptr());
		if (sm && sm->get_replicator().is_valid()) {
			sm->get_replicator()->reset_sync_tracking(this);
		}
	}
}
// 66 end

// 66
MultiplayerSynchronizer::MultiplayerSynchronizer() {
	peer_visibility.insert(0);
}

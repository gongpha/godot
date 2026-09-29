/**************************************************************************/
/*  scene_cache_interface.h                                               */
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

#include "core/object/ref_counted.h"

class Node;
class SceneMultiplayer;

class SceneCacheInterface : public RefCounted {
	GDCLASS(SceneCacheInterface, RefCounted);

private:
	SceneMultiplayer *multiplayer = nullptr;

	//path sent caches
	struct NodeCache {
		int cache_id = 0;
		HashMap<int, int> recv_ids; // peer id, remote cache id
		HashMap<int, bool> confirmed_peers; // peer id, confirmed
	};

	struct RecvNode {
		ObjectID oid;
		NodePath path;

		RecvNode(const ObjectID &p_oid, const NodePath &p_path) {
			oid = p_oid;
			path = p_path;
		}
	};

	// 66 begin
	// SIMPLIFY_PATH involving nodes that do not yet exist
	// (channel 100 can overtake the channel 0 RPC responsible for creating said nodes)
	struct PendingPath {
		NodePath path;
		String methods_md5;
		uint64_t received_usec = 0;
	};
	// 66 end

	struct PeerInfo {
		HashMap<int, RecvNode> recv_nodes; // remote cache id, (ObjectID, NodePath)
		HashSet<ObjectID> sent_nodes;
		HashMap<int, PendingPath> pending_paths; // 66: remote cache id, pending path
	};

	HashMap<ObjectID, NodeCache> nodes_cache;
	HashMap<int, ObjectID> assigned_ids;
	HashMap<int, PeerInfo> peers_info;
	int last_send_cache_id = 1;
	// 66 begin
	static constexpr uint64_t PENDING_PATH_TIMEOUT_USEC = 10000000;
	int pending_path_count = 0; // Sum of all pending_paths, so an idle poll costs nothing.

	void _finish_simplify_path(int p_from, int p_id, const NodePath &p_path, const String &p_methods_md5, Node *p_node);
	// 66 end

	void _remove_node_cache(ObjectID p_oid);
	NodeCache &_track(Node *p_node);

protected:
	Error _send_confirm_path(Node *p_node, NodeCache &p_cache, const List<int> &p_peers);

public:
	void clear();
	void on_peer_change(int p_id, bool p_connected);
	void process_simplify_path(int p_from, const uint8_t *p_packet, int p_packet_len);
	void process_pending_paths(); // 66
	void process_confirm_path(int p_from, const uint8_t *p_packet, int p_packet_len);

	// Returns true if all peers have cached path.
	bool send_object_cache(Object *p_obj, int p_target, int &p_id);
	int make_object_cache(Object *p_obj);
	Object *get_cached_object(int p_from, uint32_t p_cache_id);
	bool is_cache_confirmed(Node *p_path, int p_peer);

	SceneCacheInterface(SceneMultiplayer *p_multiplayer) { multiplayer = p_multiplayer; }
};

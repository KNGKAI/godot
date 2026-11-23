/**************************************************************************/
/*  test_sandbox_security.h                                               */
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

#include "../command_bus.h"
#include "../sandbox_clock.h"
#include "../sandbox_debug_server.h"
#include "../sandbox_serializer.h"
#include "../world_state.h"

#include "core/io/marshalls.h"
#include "core/io/stream_peer.h"
#include "tests/test_macros.h"

namespace TestSandboxSecurity {

static bool g_pre_save_flushed = false;
static void _mock_pre_save_flush() {
	g_pre_save_flushed = true;
}

TEST_CASE("[SandboxCore][Gate5] Save Deserialization RCE Rejection & Schema v2 Round-Trip") {
	SandboxSerializer *ser = SandboxSerializer::get_singleton();
	WorldState *ws = WorldState::get_singleton();
	SandboxClock *clock = SandboxClock::get_singleton();
	REQUIRE(ser != nullptr);
	REQUIRE(ws != nullptr);
	REQUIRE(clock != nullptr);

	// 1. Attempt to deserialize a raw Variant buffer containing an encoded Object instance.
	Ref<StreamPeerBuffer> malicious_obj_buf;
	malicious_obj_buf.instantiate();
	malicious_obj_buf->put_var(ws, /*p_full_objects=*/true);
	Variant out_var;
	CHECK(SandboxSerializer::deserialize_variant_safe(malicious_obj_buf->get_data_array(), out_var) == ERR_INVALID_DATA);

	// 2. Attempt to load a crafted .sbsav binary file containing an embedded Object payload inside the envelope dictionary.
	Ref<StreamPeerBuffer> crafted_sbsav;
	crafted_sbsav.instantiate();
	const uint8_t magic[8] = { 'S', 'B', 'S', 'A', 'V', 'v', '0', '2' };
	crafted_sbsav->put_data(magic, 8);
	crafted_sbsav->put_u32(2);
	Dictionary malicious_env;
	malicious_env["schema_version"] = 2;
	Dictionary malicious_state;
	malicious_state["exploit"] = ws; // Embedded Object pointer
	malicious_env["world_state"] = malicious_state;
	crafted_sbsav->put_var(malicious_env, /*p_full_objects=*/true);
	CHECK(ser->load_from_binary(crafted_sbsav->get_data_array()) == ERR_INVALID_DATA);

	// 3. Attempt to load a crafted JSON save containing embedded GDScript / Object markers.
	String malicious_json = "{\"schema_version\": 2, \"world_state\": {\"rce\": \"Object(GDScript,script_source=OS.execute)\"}}";
	CHECK(ser->load_from_json(malicious_json) == ERR_INVALID_DATA);

	// 4. Verify ResourceSaver extensions (.tres, .res, .scn) are banned for save/load.
	CHECK(ser->save_file("user://bad_save.tres") == ERR_INVALID_DATA);
	CHECK(ser->load_file("user://bad_save.res") == ERR_INVALID_DATA);

	// 5. Verify valid Schema v2 binary (.sbsav) and JSON (.json) round-trip + pre-save hydration flush hook.
	ws->clear();
	ws->set_world_origin_64(4096.0, 0.0, -2048.0);
	ws->set_float(SNAME("weather.rain_intensity"), 0.42);
	ws->set_float(SNAME("power.sector_a_load"), 88.5);
	clock->set_sim_clock_state(14820, 7410.0);

	PackedByteArray econ_blob;
	econ_blob.push_back(0xAA);
	econ_blob.push_back(0xBB);
	ser->set_sim_module_blob(SNAME("economy"), econ_blob);

	PackedByteArray diff_blob;
	diff_blob.push_back(0x0F);
	ser->set_streaming_cell_diff(SNAME("meso_8_-4"), diff_blob);

	g_pre_save_flushed = false;
	ser->set_pre_save_flush_callback(&_mock_pre_save_flush);

	PackedByteArray bin_save = ser->save_to_binary();
	CHECK(g_pre_save_flushed == true);
	CHECK(!bin_save.is_empty());

	String json_save = ser->save_to_json();
	CHECK(json_save.contains("\"schema_version\": 2"));

	// Mutate state and restore from binary snapshot
	ws->clear();
	clock->reset();
	ser->set_sim_module_blob(SNAME("economy"), PackedByteArray());
	CHECK(ser->load_from_binary(bin_save) == OK);
	CHECK(clock->get_sim_tick_index() == 14820);
	CHECK(Math::is_equal_approx(ws->get_float(SNAME("weather.rain_intensity")), 0.42));
	CHECK(Math::is_equal_approx(ws->get_float(SNAME("power.sector_a_load")), 88.5));
	Array restored_origin = ws->get_world_origin_64_array();
	REQUIRE(restored_origin.size() == 3);
	CHECK(Math::is_equal_approx((double)restored_origin[0], 4096.0));
	CHECK(Math::is_equal_approx((double)restored_origin[2], -2048.0));
	CHECK(ser->get_sim_module_blob(SNAME("economy")) == econ_blob);
	CHECK(ser->get_streaming_cell_diff(SNAME("meso_8_-4")) == diff_blob);

	// The original v2 reader accepted a raw dictionary after the 12-byte
	// magic/version header. Both encodings must restore the same snapshot.
	PackedByteArray unframed = bin_save.slice(0, 12);
	unframed.append_array(bin_save.slice(16));
	clock->reset();
	CHECK(ser->load_from_binary(unframed) == OK);
	CHECK(clock->get_sim_tick_index() == 14820);
	PackedByteArray truncated = bin_save.slice(0, bin_save.size() - 1);
	CHECK(ser->load_from_binary(truncated) == ERR_INVALID_DATA);

	// Mutate state and restore from JSON snapshot
	ws->clear();
	clock->reset();
	ser->set_sim_module_blob(SNAME("economy"), PackedByteArray());
	CHECK(ser->load_from_json(json_save) == OK);
	CHECK(clock->get_sim_tick_index() == 14820);
	CHECK(Math::is_equal_approx(ws->get_float(SNAME("power.sector_a_load")), 88.5));
	CHECK(ser->get_sim_module_blob(SNAME("economy")) == econ_blob);

	ser->set_pre_save_flush_callback(nullptr);
	ws->clear();
	clock->reset();
}

TEST_CASE("[SandboxCore][Gate5] Debug WebSocket Handshake 403 Forbidden (CSWSH & DNS Rebinding)") {
	SandboxDebugServer *dbg = SandboxDebugServer::get_singleton();
	REQUIRE(dbg != nullptr);

	PackedStringArray allowed_origins;
	allowed_origins.push_back("http://127.0.0.1:8080");
	dbg->set_allowed_origins(allowed_origins);

	String resp;

	// 1. Unauthenticated request (missing Authorization header) -> 403 Forbidden
	String unauth_req = "GET / HTTP/1.1\r\n"
						"Host: 127.0.0.1:9090\r\n"
						"Upgrade: websocket\r\n\r\n";
	CHECK(dbg->evaluate_http_handshake(unauth_req, resp) == 403);
	CHECK(resp.begins_with("HTTP/1.1 403 Forbidden"));

	// 2. Mismatched Origin header (Cross-Site WebSocket Hijacking attempt) -> 403 Forbidden
	String token = dbg->get_session_bearer_token();
	String cswsh_req = "GET / HTTP/1.1\r\n"
					   "Host: 127.0.0.1:9090\r\n"
					   "Origin: https://malicious-site.example\r\n"
					   "Authorization: Bearer " +
			token + "\r\n\r\n";
	CHECK(dbg->evaluate_http_handshake(cswsh_req, resp) == 403);
	CHECK(resp.begins_with("HTTP/1.1 403 Forbidden"));

	// 3. DNS Rebinding & 0.0.0.0/8 Host header bypass -> 403 Forbidden
	String zero_host_req = "GET / HTTP/1.1\r\n"
						   "Host: 0.0.0.0:9090\r\n"
						   "Authorization: Bearer " +
			token + "\r\n\r\n";
	CHECK(dbg->evaluate_http_handshake(zero_host_req, resp) == 403);
	CHECK(resp.begins_with("HTTP/1.1 403 Forbidden"));

#ifdef DEBUG_ENABLED
	// 4. Valid loopback Host, allowlisted Origin, and matching session Bearer token -> 101 Switching Protocols
	String valid_req = "GET / HTTP/1.1\r\n"
					   "Host: 127.0.0.1:9090\r\n"
					   "Origin: http://127.0.0.1:8080\r\n"
					   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
					   "Authorization: Bearer " +
			token + "\r\n\r\n";
	CHECK(dbg->evaluate_http_handshake(valid_req, resp) == 101);
	CHECK(resp.begins_with("HTTP/1.1 101 Switching Protocols"));
#endif
}

TEST_CASE("[SandboxCore][Gate5] CommandBus Permission Tiers, Namespace Whitelist, & 512-Byte Cap") {
	CommandBus *cb = CommandBus::get_singleton();
	REQUIRE(cb != nullptr);
	cb->clear_rate_limit_state();
	cb->clear_audit_log();

	// 1. 512-byte buffer cap rejection
	String oversized = String("state.get ") + String("a").repeat(520);
	Dictionary res_oversized = cb->execute(oversized, CommandBus::PERMISSION_ADMIN);
	CHECK((bool)res_oversized["ok"] == false);
	CHECK((int)res_oversized["error_code"] == (int)ERR_INVALID_DATA);

	// 2. Permission tier enforcement (GUEST cannot execute ADMIN command sim.pause)
	Dictionary res_guest = cb->execute("sim.pause", CommandBus::PERMISSION_GUEST);
	CHECK((bool)res_guest["ok"] == false);
	CHECK((int)res_guest["error_code"] == (int)ERR_UNAUTHORIZED);

	// 3. Namespace whitelist enforcement
	TypedArray<StringName> power_only;
	power_only.push_back(SNAME("power"));
	Dictionary res_ns = cb->execute("sim.pause", CommandBus::PERMISSION_ADMIN, SNAME("term_01"), power_only);
	CHECK((bool)res_ns["ok"] == false);
	CHECK((int)res_ns["error_code"] == (int)ERR_UNAUTHORIZED);

	// 4. Argument signature validation (sim.speed expects FLOAT)
	Dictionary res_bad_arg = cb->execute("sim.speed invalid_float", CommandBus::PERMISSION_ADMIN);
	CHECK((bool)res_bad_arg["ok"] == false);
	CHECK((int)res_bad_arg["error_code"] == (int)ERR_INVALID_PARAMETER);

	// 5. Valid ADMIN execution
	Dictionary res_ok = cb->execute("sim.speed 25.0", CommandBus::PERMISSION_ADMIN);
	CHECK((bool)res_ok["ok"] == true);
	CHECK(Math::is_equal_approx(SandboxClock::get_singleton()->get_sim_time_scale(), 25.0));
	SandboxClock::get_singleton()->reset();
}

} // namespace TestSandboxSecurity

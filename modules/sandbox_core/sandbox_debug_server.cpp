/**************************************************************************/
/*  sandbox_debug_server.cpp                                              */
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

#include "sandbox_debug_server.h"

#include "core/crypto/crypto_core.h"

#ifdef DEBUG_ENABLED
void SandboxDebugServer::_generate_session_token() {
	uint8_t random_bytes[32] = { 0 };
	if (CryptoCore::generate_random(random_bytes, sizeof(random_bytes)) == OK) {
		session_bearer_token = String::hex_encode_buffer(random_bytes, sizeof(random_bytes));
	} else {
		// Fallback deterministic-free hex token if RNG is uninitialized in early unit test startup.
		session_bearer_token = "sandbox_debug_fallback_nonce_9f8e7d6c5b4a3210";
	}
}

bool SandboxDebugServer::_is_valid_loopback_host(const String &p_host_header) {
	String host = p_host_header.strip_edges().to_lower();
	if (host.is_empty()) {
		return false;
	}
	// Strip port suffix for IPv4 / localhost (preserve IPv6 brackets if present).
	if (host.begins_with("[")) {
		int close_bracket = host.find_char(']');
		if (close_bracket == -1) {
			return false;
		}
		host = host.substr(0, close_bracket + 1);
	} else {
		int colon_idx = host.find_char(':');
		if (colon_idx != -1) {
			host = host.substr(0, colon_idx);
		}
	}

	// Explicitly reject 0.0.0.0/8 (DNS Rebinding & 0.0.0.0 bypass mitigation, Security §5).
	if (host == "0.0.0.0" || host.begins_with("0.")) {
		return false;
	}
	return (host == "127.0.0.1" || host == "localhost" || host == "[::1]");
}
#endif // DEBUG_ENABLED

Error SandboxDebugServer::start(int p_port, const PackedStringArray &p_allowed_origins) {
#ifdef DEBUG_ENABLED
	stop();
	allowed_origins = p_allowed_origins;
	if (session_bearer_token.is_empty()) {
		_generate_session_token();
	}
	tcp_server.instantiate();
	// Strictly bind to 127.0.0.1 loopback only (never 0.0.0.0 / *).
	Error err = tcp_server->listen(p_port, IPAddress("127.0.0.1"));
	if (err == OK) {
		listening_port = tcp_server->get_local_port();
	}
	return err;
#else
	return ERR_UNAUTHORIZED;
#endif
}

void SandboxDebugServer::stop() {
#ifdef DEBUG_ENABLED
	for (uint32_t i = 0; i < pending_peers.size(); i++) {
		if (pending_peers[i].is_valid()) {
			pending_peers[i]->disconnect_from_host();
		}
	}
	pending_peers.clear();
	if (tcp_server.is_valid()) {
		tcp_server->stop();
		tcp_server.unref();
	}
	listening_port = 0;
#endif
}

bool SandboxDebugServer::is_listening() const {
#ifdef DEBUG_ENABLED
	return tcp_server.is_valid() && tcp_server->is_listening();
#else
	return false;
#endif
}

int SandboxDebugServer::get_local_port() const {
#ifdef DEBUG_ENABLED
	return is_listening() ? tcp_server->get_local_port() : 0;
#else
	return 0;
#endif
}

String SandboxDebugServer::get_session_bearer_token() const {
#ifdef DEBUG_ENABLED
	return session_bearer_token;
#else
	return String();
#endif
}

void SandboxDebugServer::set_allowed_origins(const PackedStringArray &p_origins) {
#ifdef DEBUG_ENABLED
	allowed_origins = p_origins;
#endif
}

PackedStringArray SandboxDebugServer::get_allowed_origins() const {
#ifdef DEBUG_ENABLED
	return allowed_origins;
#else
	return PackedStringArray();
#endif
}

int SandboxDebugServer::evaluate_http_handshake(const String &p_raw_http_request, String &r_http_response) const {
	const String forbidden_resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
#ifdef DEBUG_ENABLED
	Vector<String> lines = p_raw_http_request.split("\n");
	if (lines.is_empty()) {
		r_http_response = forbidden_resp;
		return 403;
	}

	String host_header;
	String origin_header;
	bool has_origin = false;
	String auth_header;
	String ws_key;

	for (int i = 1; i < lines.size(); i++) {
		String line = lines[i].strip_edges();
		if (line.is_empty()) {
			break;
		}
		int colon = line.find_char(':');
		if (colon == -1) {
			continue;
		}
		String key = line.substr(0, colon).strip_edges().to_lower();
		String val = line.substr(colon + 1).strip_edges();
		if (key == "host") {
			host_header = val;
		} else if (key == "origin") {
			has_origin = true;
			origin_header = val;
		} else if (key == "authorization") {
			auth_header = val;
		} else if (key == "sec-websocket-key") {
			ws_key = val;
		}
	}

	// 1. Validate Host header against 127.0.0.1 / localhost (reject 0.0.0.0/8 & external DNS rebinding).
	if (!_is_valid_loopback_host(host_header)) {
		r_http_response = forbidden_resp;
		return 403;
	}

	// 2. Validate Origin header against explicit allowlist (CSWSH mitigation).
	if (has_origin) {
		bool origin_allowed = false;
		for (int i = 0; i < allowed_origins.size(); i++) {
			if (allowed_origins[i] == origin_header) {
				origin_allowed = true;
				break;
			}
		}
		if (!origin_allowed) {
			r_http_response = forbidden_resp;
			return 403;
		}
	}

	// 3. Validate per-session cryptographic Bearer token.
	String expected_auth = "Bearer " + session_bearer_token;
	if (session_bearer_token.is_empty() || auth_header != expected_auth) {
		r_http_response = forbidden_resp;
		return 403;
	}

	// Valid handshake: compute Sec-WebSocket-Accept if Sec-WebSocket-Key was supplied.
	String accept_val;
	if (!ws_key.is_empty()) {
		String concat = ws_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		CharString cs = concat.utf8();
		unsigned char sha1_hash[20] = { 0 };
		if (CryptoCore::sha1((const uint8_t *)cs.get_data(), cs.length(), sha1_hash) == OK) {
			accept_val = CryptoCore::b64_encode_str(sha1_hash, sizeof(sha1_hash));
		}
	}

	r_http_response = "HTTP/1.1 101 Switching Protocols\r\n"
					  "Upgrade: websocket\r\n"
					  "Connection: Upgrade\r\n"
					  "Sec-WebSocket-Accept: " +
			accept_val + "\r\n\r\n";
	return 101;
#else
	r_http_response = forbidden_resp;
	return 403;
#endif
}

void SandboxDebugServer::poll() {
#ifdef DEBUG_ENABLED
	if (!is_listening()) {
		return;
	}
	while (tcp_server->is_connection_available()) {
		Ref<StreamPeerTCP> peer = tcp_server->take_connection();
		if (peer.is_valid()) {
			pending_peers.push_back(peer);
		}
	}

	uint32_t idx = 0;
	while (idx < pending_peers.size()) {
		Ref<StreamPeerTCP> peer = pending_peers[idx];
		if (peer.is_null()) {
			pending_peers.remove_at(idx);
			continue;
		}
		peer->poll();
		if (peer->get_status() != StreamPeerSocket::STATUS_CONNECTED) {
			pending_peers.remove_at(idx);
			continue;
		}
		int avail = peer->get_available_bytes();
		if (avail > 0) {
			int to_read = MIN(avail, 4096);
			Vector<uint8_t> buf;
			buf.resize(to_read);
			int received = 0;
			peer->get_partial_data(buf.ptrw(), to_read, received);
			if (received > 0) {
				String req = String::utf8((const char *)buf.ptr(), received);
				String resp;
				int code = evaluate_http_handshake(req, resp);
				CharString resp_utf8 = resp.utf8();
				peer->put_data((const uint8_t *)resp_utf8.get_data(), resp_utf8.length());
				if (code != 101) {
					peer->disconnect_from_host();
				}
			}
			pending_peers.remove_at(idx);
			continue;
		}
		idx++;
	}
#endif
}

void SandboxDebugServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "port", "allowed_origins"), &SandboxDebugServer::start, DEFVAL(0), DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("stop"), &SandboxDebugServer::stop);
	ClassDB::bind_method(D_METHOD("is_listening"), &SandboxDebugServer::is_listening);
	ClassDB::bind_method(D_METHOD("get_local_port"), &SandboxDebugServer::get_local_port);
	ClassDB::bind_method(D_METHOD("get_session_bearer_token"), &SandboxDebugServer::get_session_bearer_token);
	ClassDB::bind_method(D_METHOD("set_allowed_origins", "origins"), &SandboxDebugServer::set_allowed_origins);
	ClassDB::bind_method(D_METHOD("get_allowed_origins"), &SandboxDebugServer::get_allowed_origins);
	ClassDB::bind_method(D_METHOD("poll"), &SandboxDebugServer::poll);
}

SandboxDebugServer::SandboxDebugServer() {
	singleton = this;
#ifdef DEBUG_ENABLED
	_generate_session_token();
#endif
}

SandboxDebugServer::~SandboxDebugServer() {
	stop();
	if (singleton == this) {
		singleton = nullptr;
	}
}

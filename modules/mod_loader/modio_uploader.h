/**************************************************************************/
/*  modio_uploader.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/object.h"
#include "scene/main/http_request.h"

class ModIOUploader : public Object {
	GDCLASS(ModIOUploader, Object);

public:
	enum UploadStatus {
		STATUS_IDLE,
		STATUS_AUTHENTICATING,
		STATUS_VALIDATING,
		STATUS_PACKAGING,
		STATUS_UPLOADING,
		STATUS_DONE,
		STATUS_ERROR,
	};

private:
	static ModIOUploader *singleton;

	static constexpr int BLOSSOM_GAME_ID = 11342;
	static constexpr const char *BLOSSOM_CLIENT_ID = "54590466";
	static constexpr int OAUTH_CALLBACK_PORT = 34567;
	String api_base_url = "https://api.mod.io/v1";
	String access_token;
	UploadStatus status = STATUS_IDLE;
	String status_message;
	String last_error;

	// Upload state.
	String pending_mod_path;
	String pending_mod_name;
	String pending_mod_summary;
	String pending_pck_path;

	// OAuth state.
	String oauth_code_verifier;
	String oauth_state;

	HTTPRequest *http = nullptr;

	enum RequestType {
		REQ_NONE,
		REQ_OAUTH_TOKEN,
		REQ_CREATE_MOD,
		REQ_UPLOAD_FILE,
	};
	RequestType current_request = REQ_NONE;
	int pending_mod_id = 0;

	String _generate_code_verifier() const;
	String _generate_code_challenge(const String &p_verifier) const;
	String _generate_state() const;

	void _http_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _on_oauth_token_complete(int p_response_code, const String &p_body);
	void _on_create_mod_complete(int p_response_code, const String &p_body);
	void _on_upload_file_complete(int p_response_code, const String &p_body);

protected:
	static void _bind_methods();

public:
	static ModIOUploader *get_singleton();

	// Auth flow: OAuth browser login.
	void login_with_browser();
	void handle_oauth_callback(const String &p_code);
	bool is_authenticated() const;
	void set_access_token(const String &p_token);
	String get_access_token() const;

	// Game ID (hardcoded).
	int get_game_id() const { return BLOSSOM_GAME_ID; }

	// Upload flow.
	void upload_mod(const String &p_mod_path, const String &p_name, const String &p_summary);

	// Status.
	UploadStatus get_status() const;
	String get_status_message() const;
	String get_last_error() const;

	void set_http_node(HTTPRequest *p_http);

	ModIOUploader();
	~ModIOUploader();
};

VARIANT_ENUM_CAST(ModIOUploader::UploadStatus);

#endif // TOOLS_ENABLED

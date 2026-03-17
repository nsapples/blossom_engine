/**************************************************************************/
/*  mod_validator.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           BLOSSOM ENGINE                               */
/*                 https://github.com/nsapples/blossom_engine             */
/**************************************************************************/

#pragma once

#include "core/object/object.h"
#include "core/string/ustring.h"

class ModValidator : public Object {
	GDCLASS(ModValidator, Object);

public:
	struct ValidationResult {
		bool passed = true;
		PackedStringArray errors;
		PackedStringArray warnings;
	};

private:
	static ModValidator *singleton;

	// Blocked API patterns.
	static const char *BLOCKED_CALLS[];
	static const char *BLOCKED_CLASSES[];

	ValidationResult _validate_script(const String &p_path, const String &p_content);
	void _scan_line(const String &p_line, int p_line_num, const String &p_file, ValidationResult &r_result);

protected:
	static void _bind_methods();

public:
	static ModValidator *get_singleton();

	// Validate a single script file.
	Dictionary validate_script_file(const String &p_path);

	// Validate an entire mod directory.
	Dictionary validate_mod(const String &p_mod_path);

	// Check if a mod is safe to upload.
	bool is_mod_safe(const String &p_mod_path);

	ModValidator();
	~ModValidator();
};

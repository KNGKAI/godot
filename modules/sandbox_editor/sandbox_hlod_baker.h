/**************************************************************************/
/*  sandbox_hlod_baker.h                                                  */
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

#include "scene/gui/box_container.h"

class Button;
class EditorFileDialog;
class Label;
class LineEdit;
class OptionButton;
class SpinBox;

class SandboxHLODBaker : public VBoxContainer {
	GDCLASS(SandboxHLODBaker, VBoxContainer);

	LineEdit *output_directory = nullptr;
	Button *browse_directory = nullptr;
	EditorFileDialog *dir_dialog = nullptr;
	OptionButton *grid_layer = nullptr;
	SpinBox *tile_size = nullptr;
	Label *result = nullptr;

	void _browse_output_directory();
	void _select_output_directory(const String &p_dir);
	void _bake_edited_scene();

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	// Each bake publishes a self-contained generation beneath the output directory.
	static Error bake_scene(Node *p_root, const String &p_directory, const StringName &p_layer, int p_tile_size, String &r_manifest_path, String &r_message);
	SandboxHLODBaker();
};

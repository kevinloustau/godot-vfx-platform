#pragma once

#include <godot_cpp/core/class_db.hpp>

void initialize_ocio_module(godot::ModuleInitializationLevel p_level);
void uninitialize_ocio_module(godot::ModuleInitializationLevel p_level);

extern "C" {
GDExtensionBool GDE_EXPORT ocio_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization);
}

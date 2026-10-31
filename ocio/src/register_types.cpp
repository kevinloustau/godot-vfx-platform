#include "register_types.h"

#include "ocio_transform.h"

#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_ocio_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	// godot-cpp registers engine-class bindings from unreferenced inline
	// statics; the Windows (COFF, LTO + --gc-sections) link drops them, so
	// every Image crossing the API failed with "Cannot find instance binding
	// callbacks for class 'Image'". Register the one we use explicitly.
	internal::register_engine_class(Image::get_class_static(), &Image::_gde_binding_callbacks);
	GDREGISTER_CLASS(OcioTransform);
}

void uninitialize_ocio_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT ocio_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_ocio_module);
	init_obj.register_terminator(uninitialize_ocio_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}

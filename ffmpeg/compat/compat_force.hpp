/**************************************************************************/
/*  compat_force.hpp                                                      */
/**************************************************************************/
/* godot-cpp master moved Mutex into namespace godot::CoreBind (Semaphore */
/* stayed in godot); EIRTeam.FFmpeg's sources (built against godot-cpp    */
/* 4.2) use godot::Mutex / core_bind::Mutex. Force-included before every  */
/* TU (CCFLAGS -include / MSVC /FI) so the upstream submodule stays       */
/* unpatched. Revisit this if the godot-cpp submodule pin moves.         */
/**************************************************************************/

#ifndef GODOT_VFX_COMPAT_FORCE_HPP
#define GODOT_VFX_COMPAT_FORCE_HPP

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/semaphore.hpp>

namespace godot {
using CoreBind::Mutex;
// Semaphore is already godot::Semaphore with the pinned godot-cpp.
} // namespace godot

#endif // GODOT_VFX_COMPAT_FORCE_HPP

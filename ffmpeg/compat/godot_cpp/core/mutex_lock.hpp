/**************************************************************************/
/*  mutex_lock.hpp (compat)                                               */
/**************************************************************************/
/* godot-cpp 4.2 shipped this RAII helper; it was removed afterwards but */
/* EIRTeam.FFmpeg's sources still include it. Provided here so the        */
/* upstream submodule stays unpatched.                                    */
/**************************************************************************/

#ifndef GODOT_MUTEX_LOCK_HPP_COMPAT
#define GODOT_MUTEX_LOCK_HPP_COMPAT

#include <godot_cpp/classes/mutex.hpp>

namespace godot {

class MutexLock {
	const Mutex &mutex;

public:
	_ALWAYS_INLINE_ explicit MutexLock(const Mutex &p_mutex) :
			mutex(p_mutex) {
		const_cast<Mutex *>(&mutex)->lock();
	}

	_ALWAYS_INLINE_ ~MutexLock() {
		const_cast<Mutex *>(&mutex)->unlock();
	}

	MutexLock(const MutexLock &) = delete;
	MutexLock &operator=(const MutexLock &) = delete;
};

} // namespace godot

#endif // GODOT_MUTEX_LOCK_HPP_COMPAT

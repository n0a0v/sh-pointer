/*	BSD 3-Clause License

	Copyright (c) 2025, Paul Varga
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice, this
	   list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright notice,
	   this list of conditions and the following disclaimer in the documentation
	   and/or other materials provided with the distribution.

	3. Neither the name of the copyright holder nor the names of its
	   contributors may be used to endorse or promote products derived from
	   this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef INC_SH__ATOMIC_SHARED_PTR_HPP
#define INC_SH__ATOMIC_SHARED_PTR_HPP

#include <atomic>
#include <cstdint>
#include <thread>

#if defined(__has_include)
	#if __has_include(<immintrin.h>)
		#include <immintrin.h>
	#endif // __has_include(<immintrin.h>)
#endif // __has_include

#include "shared_ptr.hpp"

namespace sh::pointer
{
	/**	Encapsulation of a very simple wait mechanism for use in spin lock-style loops.
	 */
	class atomic_control_spin_waiter final
	{
	public:
		void wait() noexcept
		{
#if defined(__has_builtin)
	#if __has_builtin(_mm_pause)
			// Prefer PAUSE for the first pause_count iterations:
			if (m_counter++ < pause_count)
			{
				_mm_pause();
				return;
			}
	#endif // __has_builtin(_mm_pause)
#endif // __has_builtin

			// Fallback to yield for unusually long stalls.
			std::this_thread::yield();
		}

	private:
		using counter_type = std::uint32_t;

		/**	A constant for which the first this-many waits will use a less expensive yield (e.g., pause).
		 *	As this waiter expects to spin very few times, this constant should be relatively small. Exhausting
		 *	this count indicates relative excessive contention and a proper yield may be required.
		 */
		static constexpr counter_type pause_count{ 100u };

		/**	Counter for number of types wait has been called to allow variable wait behavior.
		 */
		counter_type m_counter{ 0 };
	};

	/**	Namespace-like type to pass as Policy to atomic_control_and_value to inform regarding what type of increment & decrement should be done.
	 *	@detail This variety performs shared increment & decrement.
	 */
	struct shared_policy final
	{
		static void increment(control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->shared_inc();
			}
		}
		static void decrement(control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->shared_dec();
			}
		}
	};

	/**	Namespace-like type to pass as Policy to atomic_control_and_value to inform regarding what type of increment & decrement should be done.
	 *	@detail This variety performs weak (control) increment & decrement.
	 */
	struct weak_policy final
	{
		static void increment(control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->weak_inc();
			}
		}
		static void decrement(control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->weak_dec();
			}
		}
	};

	/**	Base implementation of an atomic convertible_control for use in atomic shared_ptr and weak_ptr.
	 *	@tparam Policy CRTP type for implementor class with increment & decrement static member functions.
	 *	@detail Holds a pointer to a pointer::convertible_control structure. This structure is aligned such that an
	 *		offset returns a pointer to a value in memory, making storage of the value pointer unnecessary. Access to
	 *		the pointer to pointer::convertible_control is locked by spinning to set the least significant bit to one.
	 *		To support wait and notify, both are done upon the single contained atomic, holding the pointer to
	 *		pointer::convertible_control.
	 */
	template <typename Policy>
	class atomic_convertible_control
	{
	public:
		/**	Constructor for nullptr.
		 */
		constexpr atomic_convertible_control(std::nullptr_t) noexcept
			: m_ctrl{ nullptr }
		{ }
		/**	Constructor allowing non-null control.
		 *	@param ctrl_with_one_inc If non-null, a pointer to a control block from which a (Policy-style) increment is inherited.
		 */
		constexpr explicit atomic_convertible_control(convertible_control* const ctrl_with_one_inc) noexcept
			: m_ctrl{ ctrl_with_one_inc }
		{ }
		/**	Destructor which will release one (Policy-style) reference count via decrement.
		 */
		~atomic_convertible_control()
		{
			convertible_control* const ctrl = this->m_ctrl.load(std::memory_order_acquire);
			Policy::decrement(ctrl);
		}
		constexpr atomic_convertible_control() noexcept = delete;
		atomic_convertible_control(const atomic_convertible_control&) = delete;
		atomic_convertible_control& operator=(const atomic_convertible_control&) = delete;

		static constexpr bool is_always_lock_free = false;
		constexpr bool is_lock_free() const noexcept
		{
			return false;
		}

		/**	Assign the pointer::convertible_control.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed.
		 *	@param order The memory synchronization ordering for the write operation.
		 */
		void store(convertible_control* const&& desired_with_one_inc, const std::memory_order order) noexcept
		{
			// Lock:
			convertible_control* const previous_ctrl = this->lock_load(order, std::memory_order_acquire);
			// Unlock & inherit increment from desired_with_one_inc:
			this->unlock_store(desired_with_one_inc, order);
			// Decrement previous m_ctrl outside of lock:
			Policy::decrement(previous_ctrl);
		}
		/**	Return the pointer::convertible_control pointer.
		 *	@param order The memory synchronization ordering for the read operation.
		 *	@return The value of m_ctrl with an incremented reference count.
		 */
		[[nodiscard]] convertible_control* load(const std::memory_order order) const noexcept
		{
			// Lock:
			convertible_control* const ctrl_with_one_inc = this->lock_load(order, std::memory_order_acquire);
			// Increment m_ctrl under lock in order to hand out via return:
			Policy::increment(ctrl_with_one_inc);
			// Unlock using seq_cst to prevent increment from reordering after this:
			this->unlock_store(ctrl_with_one_inc, std::memory_order_seq_cst);
			// Return retains increment made within lock above:
			return ctrl_with_one_inc;
		}
		/**	Exchange the pointer::convertible_control pointer.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed.
		 *	@param order The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@return The previous value of m_ctrl retaining its reference count.
		 */
		[[nodiscard]] convertible_control* exchange(convertible_control* const&& desired_with_one_inc, const std::memory_order order) noexcept
		{
			// Exchange (locks then unlocks), setting desired & inheriting increment already on desired_with_one_inc:
			convertible_control* const ctrl_with_one_inc = this->lock_exchange(desired_with_one_inc, order, std::memory_order_acquire);
			// Return retains increment made previously:
			return ctrl_with_one_inc;
		}
		/**	Compare and exchange the pointer::convertible_control pointer.
		 *	@param expected_with_one_inc The value expected to find in m_ctrl. Reference count not modified upon success. Reference count will be decremented upon failure, value changed to refer to value that was found in m_ctrl, and an increment applied to that new value.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed upon success. Reference count will be decremented upon failure.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_failure The memory synchronization ordering for the load operation upon comparison failure.
		 *	@return True if the compare and exchange succeeded. False otherwise.
		 */
		[[nodiscard]] bool compare_exchange_strong(
			convertible_control*& expected_with_one_inc,
			convertible_control* const&& desired_with_one_inc,
			const std::memory_order order_success,
			const std::memory_order order_failure) noexcept
		{
			// Lock:
			convertible_control* const ctrl_with_one_inc = this->lock_load_expected(expected_with_one_inc, order_success, order_failure);
			// Check ctrl equal to expected?
			const bool as_expected = ctrl_with_one_inc == expected_with_one_inc;
			if (as_expected)
			{
				// Unlock & inherit increment from desired:
				this->unlock_store(desired_with_one_inc, order_success);
				// Decrement previous m_ctrl outside of lock:
				Policy::decrement(ctrl_with_one_inc);
				// Leave expected alone, retaining its increment.
			}
			else
			{
				// Increment m_ctrl under lock in order to hand out via expected:
				Policy::increment(ctrl_with_one_inc);
				// Unlock using seq_cst to prevent increment from reordering after this:
				this->unlock_store(ctrl_with_one_inc, std::memory_order_seq_cst);
				// Decrement previous expected:
				Policy::decrement(expected_with_one_inc);
				// Report witnessed value of ctrl into expected:
				expected_with_one_inc = ctrl_with_one_inc;
				// Decrement desired that went unused:
				Policy::decrement(desired_with_one_inc);
			}
			return as_expected;
		}
		/**	Compare and exchange the pointer::convertible_control pointer.
		 *	@param expected_with_one_inc The value expected to find in m_ctrl. Reference count not modified upon success. Reference count will be decremented upon failure, value changed to refer to value that was found in m_ctrl, and an increment applied to that new value.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed upon success. Reference count will be decremented upon failure.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_failure The memory synchronization ordering for the load operation upon comparison failure.
		 *	@return True if the compare and exchange succeeded. False otherwise.
		 */
		[[nodiscard]] bool compare_exchange_weak(
			convertible_control*& expected_with_one_inc,
			convertible_control*&& desired_with_one_inc,
			const std::memory_order order_success,
			const std::memory_order order_failure) noexcept
		{
			return this->compare_exchange_strong(expected_with_one_inc, std::move(desired_with_one_inc), order_success, order_failure);
		}
		/**	Compare and exchange the pointer::convertible_control pointer.
		 *	@param expected_with_one_inc The value expected to find in m_ctrl. Reference count not modified upon success. Reference count will be decremented upon failure, value changed to refer to value that was found in m_ctrl, and an increment applied to that new value.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed upon success. Reference count will be decremented upon failure.
		 *	@param order The desired memory ordering of the compare-and-exchange operation.
		 *	@return True if the compare and exchange succeeded. False otherwise.
		 */
		[[nodiscard]] bool compare_exchange_strong(
			convertible_control*& expected_with_one_inc,
			convertible_control*&& desired_with_one_inc,
			const std::memory_order order) noexcept
		{
			const std::memory_order order_success = order;
			std::memory_order order_failure = order;
			switch (order)
			{
			case std::memory_order_acq_rel:
				order_failure = std::memory_order_acquire;
				break;
			case std::memory_order_release:
				order_failure = std::memory_order_relaxed;
				break;
			default:
				break;
			}
			return this->compare_exchange_strong(expected_with_one_inc, std::move(desired_with_one_inc), order_success, order_failure);
		}
		/**	Compare and exchange the pointer::convertible_control pointer.
		 *	@param expected_with_one_inc The value expected to find in m_ctrl. Reference count not modified upon success. Reference count will be decremented upon failure, value changed to refer to value that was found in m_ctrl, and an increment applied to that new value.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed upon success. Reference count will be decremented upon failure.
		 *	@param order The desired memory ordering of the compare-and-exchange operation.
		 *	@return True if the compare and exchange succeeded. False otherwise.
		 */
		[[nodiscard]] bool compare_exchange_weak(
			convertible_control*& expected_with_one_inc,
			convertible_control*&& desired_with_one_inc,
			const std::memory_order order) noexcept
		{
			return this->compare_exchange_strong(expected_with_one_inc, std::move(desired_with_one_inc), order);
		}

		/**	Wait until contained convertible_control does not match the argument given.
		 *	@param old The pointer::convertible_control address to await mismatch.
		 *	@param order The desired memory ordering of the wait operation. One of: memory_order_relaxed, memory_order_acquire, memory_order_seq_cst, or memory_order_consume.
		 */
		void wait(convertible_control* const old, const std::memory_order order) const noexcept
		{
			SH_POINTER_ASSERT(
				order == std::memory_order_relaxed
				|| order == std::memory_order_consume
				|| order == std::memory_order_acquire
				|| order == std::memory_order_seq_cst,
				"std::atomic::wait doesn't expect release order");
			this->m_ctrl.wait(old, order);
		}
		/**	Notify one thread waiting on m_ctrl via wait.
		 */
		void notify_one() noexcept
		{
			this->m_ctrl.notify_one();
		}
		/**	Notify all threads waiting on m_ctrl via wait.
		 */
		void notify_all() noexcept
		{
			this->m_ctrl.notify_all();
		}

	private:
		static_assert(alignof(convertible_control) >= 2, "Alignment of control block must be at least 2-bytes to leave zeroed bits to hold bit_locked.");
		/**	Bit set within m_ctrl when this atomic_convertible_control & its contents are locked.
		 */
		static constexpr std::uintptr_t bit_locked{ 0b01 };

		/**	Exchange m_ctrl.
		 *	@param desired The desired value to exchange into m_ctrl. If bit_locked set, will achieve a lock upon return.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_load_and_failure The memory synchronization ordering for the initial load operation and the load operation upon comparison failure.
		 *	@return The previous value that was replaced, without any meta data bits, dereferencable as a pointer.
		 */
		[[nodiscard]] convertible_control* lock_exchange(
			convertible_control* const desired,
			const std::memory_order order_success,
			const std::memory_order order_load_and_failure) const noexcept
		{
			SH_POINTER_ASSERT((reinterpret_cast<std::uintptr_t>(static_cast<void*>(desired)) & bit_locked) == 0,
				"Didn't expect to exchange to desired value with bit_locked set.");
			SH_POINTER_ASSERT(
				order_load_and_failure != std::memory_order_release
				&& order_load_and_failure != std::memory_order_acq_rel,
				"std::atomic::load doesn't expect release order");
			atomic_control_spin_waiter waiter;
			convertible_control* expected = this->m_ctrl.load(order_load_and_failure);
			for (;;)
			{
				// Control-with-meta will have bit_locked unset.
				const std::uintptr_t ctrl_meta{ reinterpret_cast<std::uintptr_t>(static_cast<void*>(expected)) & ~bit_locked };

				// No exchange should be done if m_ctrl is presently locked
				// (bit_locked is set). Ensure expected has bit_locked unset.
				expected = static_cast<convertible_control*>(reinterpret_cast<void*>(ctrl_meta));

				if (this->m_ctrl.compare_exchange_weak(
						expected,
						desired,
						order_success,
						order_load_and_failure
					)
				)
				{
					// Success.
					break;
				}

				// Failure, wait for m_ctrl to be unlocked.
				waiter.wait();
			}
			// Expected already must be missing bit_locked per the compare and
			// exchange above. Unset that to return a usable (dereferencable)
			// convertible_control pointer to the caller:
			return expected;
		}
		/**	Lock and return the locked value of m_ctrl.
		 *	@param expected The expected initial value of m_ctrl.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_load The memory synchronization ordering for the load operation upon comparison failure.
		 *	@return The value of m_ctrl at the time of locking, without any meta data bits, dereferencable as a pointer.
		 */
		[[nodiscard]] convertible_control* lock_load_expected(
			convertible_control* expected,
			const std::memory_order order_success,
			const std::memory_order order_load) const noexcept
		{
			SH_POINTER_ASSERT(
				order_load != std::memory_order_release
				&& order_load != std::memory_order_acq_rel,
				"std::atomic::load doesn't expect release order");
			atomic_control_spin_waiter waiter;
			for (;;)
			{
				// Control-with-meta will have bit_locked unset.
				const std::uintptr_t ctrl_meta{ reinterpret_cast<std::uintptr_t>(static_cast<void*>(expected)) & ~bit_locked };

				// No exchange should be done if m_ctrl is presently locked
				// (bit_locked is set). Ensure expected has bit_locked unset.
				expected = static_cast<convertible_control*>(reinterpret_cast<void*>(ctrl_meta));

				if (this->m_ctrl.compare_exchange_weak(
						expected,
						static_cast<convertible_control*>(reinterpret_cast<void*>(ctrl_meta | bit_locked)),
						order_success,
						order_load
					)
				)
				{
					// Success.
					break;
				}

				// Failure, wait for m_ctrl to be unlocked.
				waiter.wait();
			}
			// Expected already must be missing bit_locked per the compare and
			// exchange above. Unset that to return a usable (dereferencable)
			// convertible_control pointer to the caller:
			return expected;
		}
		/**	Lock and return the locked value of m_ctrl.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_load_and_failure The memory synchronization ordering for the initial load operation and the load operation upon comparison failure.
		 *	@return The value of m_ctrl at the time of locking, without any meta data bits, dereferencable as a pointer.
		 */
		[[nodiscard]] convertible_control* lock_load(const std::memory_order order_success, const std::memory_order order_load_and_failure) const noexcept
		{
			return this->lock_load_expected(this->m_ctrl.load(order_load_and_failure), order_success, order_load_and_failure);
		}
		/**	Store a value into m_ctrl with the intention of unlocking.
		 *	@param ctrl The exact value to store into m_ctrl. Any meta data bits will be retained. Shouldn't have bit_locked set in order to unlock.
		 *	@param order The memory ordering of the store. One of memory_order_relaxed, memory_order_release, memory_order_seq_cst, memory_order_consume.
		 */
		void unlock_store(convertible_control* const ctrl, const std::memory_order order) const noexcept
		{
			SH_POINTER_ASSERT(
				order == std::memory_order_relaxed
				|| order == std::memory_order_consume
				|| order == std::memory_order_release
				|| order == std::memory_order_seq_cst,
				"std::atomic::store expected release order");
			SH_POINTER_ASSERT((reinterpret_cast<std::uintptr_t>(static_cast<void*>(ctrl)) & bit_locked) == 0,
				"Didn't expect to store ctrl value with bit_locked set.");
			this->m_ctrl.store(ctrl, order);
		}

		/**	Pointer to a pointer::convertible_control structure. Maybe be nullptr.
		 *	@note This pointer may be manipulated by having bit_locked set. If m_ctrl could possibly be locked, that bit must be cleared before dereferencing.
		 */
		mutable std::atomic<convertible_control*> m_ctrl;
	};

} // namespace sh::pointer

template <typename T>
struct std::atomic<sh::shared_ptr<T>> : private sh::pointer::atomic_convertible_control<sh::pointer::shared_policy>
{
	using atomic_control = sh::pointer::atomic_convertible_control<sh::pointer::shared_policy>;

public:
	using value_type = sh::shared_ptr<T>;

	constexpr atomic() noexcept
		: atomic_control{ nullptr }
	{ }
	constexpr atomic(std::nullptr_t) noexcept
		: atomic_control{ nullptr }
	{ }
	constexpr atomic(sh::shared_ptr<T> desired) noexcept
		: atomic_control{ sh::pointer::convert_value_to_control(std::exchange(desired.m_value, nullptr)) }
	{ }
	~atomic() = default;
	atomic(const atomic&) = delete;
	atomic& operator=(const atomic<T>&) = delete;

	atomic& operator=(sh::shared_ptr<T> desired) noexcept
	{
		store(std::move(desired));
		return *this;
	}
	operator sh::shared_ptr<T>() const noexcept
	{
		return load();
	}

	void store(sh::shared_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		this->atomic_control::store(
			sh::pointer::convert_value_to_control(
				std::exchange(desired.m_value, nullptr)
			),
			order
		);
	}
	sh::shared_ptr<T> load(const std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		return sh::shared_ptr<T>{ sh::pointer::convert_control_to_value<element_type*>(this->atomic_control::load(order)) };
	}
	sh::shared_ptr<T> exchange(sh::shared_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		desired.m_value = sh::pointer::convert_control_to_value<element_type*>(
			this->atomic_control::exchange(
				sh::pointer::convert_value_to_control(desired.get()),
				order)
			);
		return desired;
	}
	bool compare_exchange_strong(sh::shared_ptr<T>& expected, sh::shared_ptr<T> desired, const std::memory_order order_success, const std::memory_order order_failure) noexcept
	{
		sh::pointer::convertible_control* expected_ctrl = sh::pointer::convert_value_to_control(expected.get());
		const bool success = this->atomic_control::compare_exchange_strong(
			expected_ctrl,
			sh::pointer::convert_value_to_control(std::exchange(desired.m_value, nullptr)),
			order_success,
			order_failure);
		// If success: this is a no-op, expected is being set back to the same value & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_value = sh::pointer::convert_control_to_value<element_type*>(expected_ctrl);
		return success;
	}
	bool compare_exchange_weak(sh::shared_ptr<T>& expected, sh::shared_ptr<T> desired, const std::memory_order order_success, const std::memory_order order_failure) noexcept
	{
		sh::pointer::convertible_control* expected_ctrl = sh::pointer::convert_value_to_control(expected.get());
		const bool success = this->atomic_control::compare_exchange_weak(
			expected_ctrl,
			sh::pointer::convert_value_to_control(std::exchange(desired.m_value, nullptr)),
			order_success,
			order_failure);
		// If success: this is a no-op, expected is being set back to the same value & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_value = sh::pointer::convert_control_to_value<element_type*>(expected_ctrl);
		return success;
	}
	bool compare_exchange_strong(sh::shared_ptr<T>& expected, sh::shared_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		sh::pointer::convertible_control* expected_ctrl = sh::pointer::convert_value_to_control(expected.get());
		const bool success = this->atomic_control::compare_exchange_strong(
			expected_ctrl,
			sh::pointer::convert_value_to_control(std::exchange(desired.m_value, nullptr)),
			order);
		// If success: this is a no-op, expected is being set back to the same value & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_value = sh::pointer::convert_control_to_value<element_type*>(expected_ctrl);
		return success;
	}
	bool compare_exchange_weak(sh::shared_ptr<T>& expected, sh::shared_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		sh::pointer::convertible_control* expected_ctrl = sh::pointer::convert_value_to_control(expected.get());
		const bool success = this->atomic_control::compare_exchange_weak(
			expected_ctrl,
			sh::pointer::convert_value_to_control(std::exchange(desired.m_value, nullptr)),
			order);
		// If success: this is a no-op, expected is being set back to the same value & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_value = sh::pointer::convert_control_to_value<element_type*>(expected_ctrl);
		return success;
	}

	void wait(const sh::shared_ptr<T> old, const std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		this->atomic_control::wait(sh::pointer::convert_value_to_control(old.get()), order);
	}
	using atomic_control::notify_one;
	using atomic_control::notify_all;
	using atomic_control::is_always_lock_free;
	using atomic_control::is_lock_free;

private:
	using element_type = typename value_type::element_type;
};

template <typename T>
struct std::atomic<sh::weak_ptr<T>> : private sh::pointer::atomic_convertible_control<sh::pointer::weak_policy>
{
	using atomic_control = sh::pointer::atomic_convertible_control<sh::pointer::weak_policy>;

public:
	using value_type = sh::weak_ptr<T>;

	constexpr atomic() noexcept
		: atomic_control{ nullptr }
	{ }
	constexpr atomic(std::nullptr_t) noexcept
		: atomic_control{ nullptr }
	{ }
	constexpr atomic(sh::weak_ptr<T> desired) noexcept
		: atomic_control{ std::exchange(desired.m_ctrl, nullptr) }
	{ }
	~atomic() = default;
	atomic(const atomic&) = delete;
	atomic& operator=(const atomic<T>&) = delete;

	atomic& operator=(sh::weak_ptr<T> desired) noexcept
	{
		store(std::move(desired));
		return *this;
	}
	operator sh::weak_ptr<T>() const noexcept
	{
		return load();
	}

	void store(sh::weak_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		this->atomic_control::store(std::exchange(desired.m_ctrl, nullptr), order);
	}
	sh::weak_ptr<T> load(const std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		return sh::weak_ptr<T>{ this->atomic_control::load(order) };
	}
	sh::weak_ptr<T> exchange(sh::weak_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		// Use static_cast to imply that desired.m_ctrl is an rvalue, as it
		// effectively (kind of) is. We're to replace its value upon exchange's
		// return.
		desired.m_ctrl = this->atomic_control::exchange(static_cast<sh::pointer::convertible_control*&&>(desired.m_ctrl), order);
		return desired;
	}
	bool compare_exchange_strong(sh::weak_ptr<T>& expected, sh::weak_ptr<T> desired, const std::memory_order order_success, const std::memory_order order_failure) noexcept
	{
		return this->atomic_control::compare_exchange_strong(expected.m_ctrl, std::exchange(desired.m_ctrl, nullptr), order_success, order_failure);
	}
	bool compare_exchange_weak(sh::weak_ptr<T>& expected, sh::weak_ptr<T> desired, const std::memory_order order_success, const std::memory_order order_failure) noexcept
	{
		return this->atomic_control::compare_exchange_weak(expected.m_ctrl, std::exchange(desired.m_ctrl, nullptr), order_success, order_failure);
	}
	bool compare_exchange_strong(sh::weak_ptr<T>& expected, sh::weak_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		return this->atomic_control::compare_exchange_strong(expected.m_ctrl, std::exchange(desired.m_ctrl, nullptr), order);
	}
	bool compare_exchange_weak(sh::weak_ptr<T>& expected, sh::weak_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		return this->atomic_control::compare_exchange_weak(expected.m_ctrl, std::exchange(desired.m_ctrl, nullptr), order);
	}

	void wait(const sh::weak_ptr<T> old, const std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		this->atomic_control::wait(old.m_ctrl, order);
	}
	using atomic_control::notify_one;
	using atomic_control::notify_all;
	using atomic_control::is_always_lock_free;
	using atomic_control::is_lock_free;
};

namespace sh
{
	template <typename T>
	using atomic_shared_ptr = ::std::atomic<::sh::shared_ptr<T>>;
	template <typename T>
	using atomic_weak_ptr = ::std::atomic<::sh::weak_ptr<T>>;
} // namespace sh

#endif

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

#ifndef INC_SH__ATOMIC_WIDE_SHARED_PTR_HPP
#define INC_SH__ATOMIC_WIDE_SHARED_PTR_HPP

#include <atomic>

#include "atomic_shared_ptr.hpp"
#include "wide_shared_ptr.hpp"

namespace sh::pointer
{
	/**	Base implementation of an atomic control & value pair for use in atomic wide_shared_ptr and wide_weak_ptr.
	 *	@tparam Policy CRTP type for implementor class with increment & decrement static member functions.
	 *	@detail Holds a pointer to a pointer::control structure and a void pointer to data. Accesses to either are
	 *		locked by spinning to set the least significant bit in the pointer to pointer::control to one. To support
	 *		wait and notify, both are done upon the atomic holding the pointer to pointer::control. To handle the
	 *		(rare) case of same-control but different-value, the next-to-least significant bit in this pointer is
	 *		toggled.
	 */
	template <typename Policy>
	class atomic_control_and_value
	{
	public:
		/**	Type alias for value types that have been type erased inside atomic_control_and_value.
		 */
		using erased_t = void;

		/**	Constructor for nullptr (neither control nor value).
		 */
		atomic_control_and_value(std::nullptr_t, std::nullptr_t) noexcept
			: m_ctrl{ nullptr }
			, m_value{ nullptr }
		{ }
		/**	Constructor allowing non-null control & value.
		 *	@param ctrl_with_one_inc If non-null, a pointer to a control block from which a (Policy-style) increment is inherited.
		 *	@param value A value pointer associated with the given control block.
		 */
		atomic_control_and_value(control* const ctrl_with_one_inc, erased_t* const value) noexcept
			: m_ctrl{ ctrl_with_one_inc }
			, m_value{ value }
		{ }
		/**	Destructor which will release one (Policy-style) reference count via decrement.
		 */
		~atomic_control_and_value()
		{
			control* const ctrl = this->m_ctrl.load(std::memory_order_acquire);
			// m_ctrl shouldn't be locked (bit_locked), but may be decorated with bit_notify.
			const std::uintptr_t ctrl_sans_notify{ reinterpret_cast<std::uintptr_t>(static_cast<void*>(ctrl)) & ~bit_notify };
			Policy::decrement(static_cast<control*>(reinterpret_cast<void*>(ctrl_sans_notify)));
		}
		// Disable default & copy construction, copy assignment.
		constexpr atomic_control_and_value() noexcept = delete;
		atomic_control_and_value(const atomic_control_and_value&) = delete;
		atomic_control_and_value& operator=(const atomic_control_and_value&) = delete;

		// This implementation uses a spin lock within the control block pointer and does not qualify as lock free:
		static constexpr bool is_always_lock_free = false;
		constexpr bool is_lock_free() const noexcept
		{
			return false;
		}

		/**	Assign the pointer::control and value pointers.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed.
		 *	@param desired_value The value to exchange into m_value.
		 *	@param order The memory synchronization ordering for the write operation.
		 */
		void store(control* const&& desired_ctrl_with_one_inc, erased_t* const desired_value, const std::memory_order order) noexcept
		{
			// Lock:
			std::uintptr_t previous_ctrl_meta;
			control* const previous_ctrl = this->lock_load(previous_ctrl_meta, order, std::memory_order_acquire);
			// Under the lock, check if we're changing value while retaining
			// the same ctrl so that we can cause waiters to wake:
			const bool toggle_notify = previous_ctrl == desired_ctrl_with_one_inc
				&& this->m_value != desired_value;
			// Replace m_value under lock:
			this->m_value = desired_value;
			// Unlock & inherit increment from desired_ctrl_with_one_inc:
			if (toggle_notify)
			{
				SH_POINTER_ASSERT(reinterpret_cast<std::uintptr_t>(static_cast<void*>(desired_ctrl_with_one_inc))
					== (previous_ctrl_meta & ~bit_notify),
					"Expected previous_ctrl_meta and desired_ctrl_with_one_inc to point to the same pointer::control.");
				// Unlock using toggled bit_notify, effectively inheriting
				// increment from desired_ctrl_with_one_inc:
				this->unlock_store(previous_ctrl_meta ^ bit_notify, order);
			}
			else
			{
				// Unlock & inherit increment from desired_ctrl_with_one_inc:
				this->unlock_store(desired_ctrl_with_one_inc, order);
			}
			// Decrement previous m_ctrl outside of lock:
			Policy::decrement(previous_ctrl);
		}
		/**	Return the pointer::control and value pointers.
		 *	@param order The memory synchronization ordering for the read operation.
		 *	@return The value of m_ctrl (with an incremented reference count) and m_value.
		 */
		[[nodiscard]] std::pair<control*, erased_t*> load(const std::memory_order order) const noexcept
		{
			// Lock:
			std::uintptr_t ctrl_meta;
			control* const ctrl_with_one_inc = this->lock_load(ctrl_meta, order, std::memory_order_acquire);
			// Increment m_ctrl under lock in order to hand out via return:
			Policy::increment(ctrl_with_one_inc);
			// Copy value while under lock:
			erased_t* const value = this->m_value;
			// Unlock retaining meta bits:
			this->unlock_store(ctrl_meta, std::memory_order_release);
			// Return retains increment made within lock above:
			return { ctrl_with_one_inc, value };
		}
		/**	Exchange the pointer::control and value pointers with those given.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed.
		 *	@param desired_value The value to exchange into m_value.
		 *	@param order The memory synchronization ordering for the read-modify-write operation.
		 *	@return The previous values of m_ctrl (retaining its reference count) and m_value.
		 */
		[[nodiscard]] std::pair<control*, erased_t*> exchange(
			control* const&& desired_ctrl_with_one_inc,
			erased_t* const desired_value,
			const std::memory_order order) noexcept
		{
			// Lock:
			std::uintptr_t ctrl_meta;
			control* const ctrl_with_one_inc = this->lock_load(ctrl_meta, order, std::memory_order_acquire);
			// Under the lock, check if we're changing value while retaining
			// the same ctrl so that notify can cause waiters to wake:
			const bool toggle_notify = ctrl_with_one_inc == desired_ctrl_with_one_inc
				&& this->m_value != desired_value;
			// Exchange value while under lock:
			erased_t* const value = std::exchange(this->m_value, desired_value);
			// Unlock & inherit increment from desired_ctrl_with_one_inc:
			if (toggle_notify)
			{
				SH_POINTER_ASSERT((ctrl_meta & ~bit_notify) == reinterpret_cast<std::uintptr_t>(static_cast<void*>(desired_ctrl_with_one_inc)),
					"Expected ctrl_meta and desired_ctrl_with_one_inc to point to the same pointer::control.");
				// Unlock using toggled bit_notify, effectively inheriting
				// increment from desired_ctrl_with_one_inc:
				this->unlock_store(ctrl_meta ^ bit_notify, order);
			}
			else
			{
				// Unlock & inherit increment from desired_ctrl_with_one_inc:
				this->unlock_store(desired_ctrl_with_one_inc, order);
			}
			// Return retains increment made previously:
			return { ctrl_with_one_inc, value };
		}

		/**	Compare and exchange the pointer::control and value pointers.
		 *	@param expected_with_one_inc The value expected to find in m_ctrl. Reference count not modified upon success. Reference count will be decremented upon failure, value changed to refer to value that was found in m_ctrl, and an increment applied to that new value.
		 *	@param expected_value The value expected to find in m_value. Upon failure, will be assigned to refer to the value that was found in m_value.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed upon success. Reference count will be decremented upon failure.
		 *	@param desired_value The value to exchange into m_value.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_failure The memory synchronization ordering for the load operation upon comparison failure.
		 *	@return True if the compare and exchange succeeded. False otherwise.
		 */
		[[nodiscard]] bool compare_exchange_strong(
			control*& expected_ctrl_with_one_inc, erased_t*& expected_value,
			control* const&& desired_ctrl_with_one_inc, erased_t* const&& desired_value,
			const std::memory_order order_success,
			const std::memory_order order_failure) noexcept
		{
			// Lock:
			std::uintptr_t ctrl_meta;
			control* const ctrl_with_one_inc = this->lock_load_expected(expected_ctrl_with_one_inc, ctrl_meta, order_success, order_failure);
			// Check ctrl equal to expected?
			const bool as_expected = ctrl_with_one_inc == expected_ctrl_with_one_inc
				&& this->m_value == expected_value;
			if (as_expected)
			{
				// Under the lock, check if we're changing value while retaining
				// the same ctrl so that notify can cause waiters to wake:
				const bool toggle_notify = ctrl_with_one_inc == desired_ctrl_with_one_inc
					&& this->m_value != desired_value;
				// Assign value while under lock:
				this->m_value = desired_value;
				// Unlock & inherit increment from desired_ctrl_with_one_inc:
				if (toggle_notify)
				{
					// Unlock using toggled bit_notify, effectively inheriting
					// increment from desired_ctrl_with_one_inc:
					this->unlock_store(ctrl_meta ^ bit_notify, order_success);
				}
				else
				{
					// Unlock & inherit increment from desired_ctrl_with_one_inc:
					this->unlock_store(desired_ctrl_with_one_inc, order_success);
				}
				// Decrement previous m_ctrl outside of lock:
				Policy::decrement(ctrl_with_one_inc);
				// Leave expected alone, retaining its increment.
			}
			else
			{
				// Report witnessed value into expected:
				expected_value = this->m_value;
				// Increment m_ctrl under lock in order to hand out via expected:
				Policy::increment(ctrl_with_one_inc);
				// Unlock retaining meta bits:
				this->unlock_store(ctrl_meta, std::memory_order_release);
				// Decrement previous expected:
				Policy::decrement(expected_ctrl_with_one_inc);
				// Report witnessed value of ctrl into expected:
				expected_ctrl_with_one_inc = ctrl_with_one_inc;
				// Decrement desired that went unused:
				Policy::decrement(desired_ctrl_with_one_inc);
			}
			return as_expected;
		}
		/**	Compare and exchange the pointer::control and value pointers.
		 *	@param expected_with_one_inc The value expected to find in m_ctrl. Reference count not modified upon success. Reference count will be decremented upon failure, value changed to refer to value that was found in m_ctrl, and an increment applied to that new value.
		 *	@param expected_value The value expected to find in m_value. Upon failure, will be assigned to refer to the value that was found in m_value.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed upon success. Reference count will be decremented upon failure.
		 *	@param desired_value The value to exchange into m_value.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_failure The memory synchronization ordering for the load operation upon comparison failure.
		 *	@return True if the compare and exchange succeeded. False otherwise.
		 */
		[[nodiscard]] bool compare_exchange_weak(
			control*& expected_ctrl_with_one_inc, erased_t*& expected_value,
			control*&& desired_ctrl_with_one_inc, erased_t*&& desired_value,
			const std::memory_order order_success,
			const std::memory_order order_failure) noexcept
		{
			return this->compare_exchange_strong(
				expected_ctrl_with_one_inc, expected_value,
				std::move(desired_ctrl_with_one_inc), std::move(desired_value),
				order_success, order_failure);
		}
		/**	Compare and exchange the pointer::control and value pointers.
		 *	@param expected_with_one_inc The value expected to find in m_ctrl. Reference count not modified upon success. Reference count will be decremented upon failure, value changed to refer to value that was found in m_ctrl, and an increment applied to that new value.
		 *	@param expected_value The value expected to find in m_value. Upon failure, will be assigned to refer to the value that was found in m_value.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed upon success. Reference count will be decremented upon failure.
		 *	@param desired_value The value to exchange into m_value.
		 *	@param order The desired memory ordering of the compare-and-exchange operation.
		 *	@return True if the compare and exchange succeeded. False otherwise.
		 */
		[[nodiscard]] bool compare_exchange_strong(
			control*& expected_ctrl_with_one_inc, erased_t*& expected_value,
			control*&& desired_ctrl_with_one_inc, erased_t*&& desired_value,
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
			return this->compare_exchange_strong(
				expected_ctrl_with_one_inc, expected_value,
				std::move(desired_ctrl_with_one_inc), std::move(desired_value),
				order_success, order_failure);
		}
		/**	Compare and exchange the pointer::control and value pointers.
		 *	@param expected_with_one_inc The value expected to find in m_ctrl. Reference count not modified upon success. Reference count will be decremented upon failure, value changed to refer to value that was found in m_ctrl, and an increment applied to that new value.
		 *	@param expected_value The value expected to find in m_value. Upon failure, will be assigned to refer to the value that was found in m_value.
		 *	@param desired_with_one_inc The value to exchange into m_ctrl. Reference count will be assumed upon success. Reference count will be decremented upon failure.
		 *	@param desired_value The value to exchange into m_value.
		 *	@param order The desired memory ordering of the compare-and-exchange operation.
		 *	@return True if the compare and exchange succeeded. False otherwise.
		 */
		[[nodiscard]] bool compare_exchange_weak(
			control*& expected_ctrl_with_one_inc, erased_t*& expected_value,
			control*&& desired_ctrl_with_one_inc, erased_t*&& desired_value,
			const std::memory_order order) noexcept
		{
			return this->compare_exchange_strong(
				expected_ctrl_with_one_inc, expected_value,
				std::move(desired_ctrl_with_one_inc), std::move(desired_value),
				order);
		}

		/**	Wait until either contained control or value pointer does not match the respective arguments given.
		 *	@param old_ctrl The pointer::control address to await mismatch.
		 *	@param old_value The data value pointer to await mismatch.
		 *	@param order The desired memory ordering of the wait operation. One of: memory_order_relaxed, memory_order_acquire, memory_order_seq_cst, or memory_order_consume.
		 */
		void wait(control* const old_ctrl, const erased_t* const old_value, const std::memory_order order) const noexcept
		{
			SH_POINTER_ASSERT(
				order != std::memory_order_release
				|| order == std::memory_order_acq_rel,
				"std::atomic::wait doesn't expect release order");
			for (;;)
			{
				// Lock:
				std::uintptr_t ctrl_meta;
				control* const ctrl = this->lock_load(ctrl_meta, order, order);
				// Copy value while under lock:
				erased_t* const value = this->m_value;
				// Unlock retaining meta bits:
				this->unlock_store(ctrl_meta, std::memory_order_release);
				// Compare ctrl & value:
				if (ctrl != old_ctrl || value != old_value)
				{
					// Break out of loop as either ctrl and/or value is different:
					break;
				}
				// Wait until notify & m_ctrl has been changed to try again.
				// The bit_notify bit within m_ctrl will be toggled if m_value is
				// changed but the same control value kept:
				this->m_ctrl.wait(static_cast<control*>(reinterpret_cast<void*>(ctrl_meta)), order);
			}
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
		static_assert(alignof(control) >= 4, "Alignment of control block must be at least 4-bytes to leave zeroed bits to hold bit_locked & bit_notify.");
		/**	Bit set within m_ctrl when this atomic_control_and_value & its contents are locked.
		 */
		static constexpr std::uintptr_t bit_locked{ 0b01 };
		/**	Bit toggled within m_ctrl when an assignment is made that changes m_value without altering m_value.
		 */
		static constexpr std::uintptr_t bit_notify{ 0b10 };

		/**	Lock and return the locked value of m_ctrl.
		 *	@param expected The expected initial value of m_ctrl.
		 *	@param ctrl_meta Is assigned the exact value (including any meta data bits) of m_ctrl at the time of locking.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_failure The memory synchronization ordering for the load operation upon comparison failure.
		 *	@return The value of m_ctrl at the time of locking, without any meta data bits, dereferencable as a pointer.
		 */
		[[nodiscard]] control* lock_load_expected(
			control* expected,
			std::uintptr_t& ctrl_meta,
			const std::memory_order order_success,
			const std::memory_order order_failure) const noexcept
		{
			SH_POINTER_ASSERT(
				order_failure != std::memory_order_release
				&& order_failure != std::memory_order_acq_rel,
				"std::atomic::load doesn't expect release order");
			atomic_control_spin_waiter waiter;
			for (;;)
			{
				// Control-with-meta will have, upon return:
				// 1. bit_locked unset.
				// 2. bit_notify set or unset, accurately:
				ctrl_meta = reinterpret_cast<std::uintptr_t>(static_cast<void*>(expected)) & ~bit_locked;

				// No exchange should be done if m_ctrl is presently locked
				// (bit_locked is set). Ensure expected has bit_locked unset.
				expected = static_cast<control*>(reinterpret_cast<void*>(ctrl_meta));

				if (this->m_ctrl.compare_exchange_weak(
						expected,
						static_cast<control*>(reinterpret_cast<void*>(ctrl_meta | bit_locked)),
						order_success,
						order_failure
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
			// exchange above, but may have retained the notify bit. Unset that
			// to return a usable (dereferencable) control pointer to the caller:
			const std::uintptr_t expected_sans_meta{ ctrl_meta & ~bit_notify };
			return static_cast<control*>(reinterpret_cast<void*>(expected_sans_meta));
		}
		/**	Lock and return the locked value of m_ctrl.
		 *	@param ctrl_meta Is assigned the exact value (including any meta data bits) of m_ctrl at the time of locking.
		 *	@param order_success The memory synchronization ordering for the read-modify-write operation upon success.
		 *	@param order_load_and_failure The memory synchronization ordering for the initial load operation and the load operation upon comparison failure.
		 *	@return The value of m_ctrl at the time of locking, without any meta data bits, dereferencable as a pointer.
		 */
		[[nodiscard]] control* lock_load(std::uintptr_t& ctrl_meta, const std::memory_order order_success, const std::memory_order order_load_and_failure) const noexcept
		{
			return this->lock_load_expected(this->m_ctrl.load(order_load_and_failure), ctrl_meta, order_success, order_load_and_failure);
		}
		/**	Store a value into m_ctrl with the intention of unlocking.
		 *	@param ctrl The exact value to store into m_ctrl. Any meta data bits will be retained. Shouldn't have bit_locked set in order to unlock.
		 *	@param order The memory ordering of the store. One of memory_order_relaxed, memory_order_release, memory_order_seq_cst, memory_order_consume.
		 */
		void unlock_store(control* const ctrl, const std::memory_order order) const noexcept
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
		/**	Store a value into m_ctrl with the intention of unlocking.
		 *	@param ctrl_meta An exact value to store into m_ctrl. Any meta data bits will be retained. Shouldn't have bit_locked set in order to unlock.
		 *	@param order The memory ordering of the store. One of memory_order_relaxed, memory_order_release, memory_order_seq_cst.
		 */
		void unlock_store(const std::uintptr_t ctrl_meta, const std::memory_order order) const noexcept
		{
			this->unlock_store(static_cast<control*>(reinterpret_cast<void*>(ctrl_meta)), order);
		}

		/**	Pointer to a pointer::control structure. Maybe be nullptr.
		 *	@note This pointer may be manipulated by having bit_locked or bit_notify set. Those must be cleared before dereferencing.
		 */
		mutable std::atomic<control*> m_ctrl;
		/**	Pointer to a data value. Maybe be nullptr.
		 *	@note Access is locked by setting bit_locked into m_ctrl.
		 */
		erased_t* m_value;
	};

} // namespace sh::pointer

template <typename T>
struct std::atomic<sh::wide_shared_ptr<T>> : private sh::pointer::atomic_control_and_value<sh::pointer::shared_policy>
{
	using atomic_control_and_value = sh::pointer::atomic_control_and_value<sh::pointer::shared_policy>;

public:
	using value_type = sh::wide_shared_ptr<T>;

	constexpr atomic() noexcept
		: atomic_control_and_value{ nullptr, nullptr }
	{ }
	constexpr atomic(std::nullptr_t) noexcept
		: atomic_control_and_value{ nullptr, nullptr }
	{ }
	constexpr atomic(sh::wide_shared_ptr<T> desired) noexcept
		: atomic_control_and_value
		{
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<std::remove_const_t<element_type>*>(std::exchange(desired.m_value, nullptr))
		}
	{ }
	~atomic() = default;
	atomic(const atomic&) = delete;
	atomic& operator=(const atomic<T>&) = delete;

	atomic& operator=(sh::wide_shared_ptr<T> desired) noexcept
	{
		store(std::move(desired));
		return *this;
	}
	operator sh::wide_shared_ptr<T>() const noexcept
	{
		return load();
	}

	void store(sh::wide_shared_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		this->atomic_control_and_value::store(
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<std::remove_const_t<element_type>*>(std::exchange(desired.m_value, nullptr)),
			order
		);
	}
	sh::wide_shared_ptr<T> load(const std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		const auto [ctrl_with_one_inc, value] = this->atomic_control_and_value::load(order);
		return sh::wide_shared_ptr<T>{ ctrl_with_one_inc, static_cast<element_type*>(value) };
	}
	sh::wide_shared_ptr<T> exchange(sh::wide_shared_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		const auto [ctrl_with_one_inc, value] = this->atomic_control_and_value::exchange(
			// Move instead of exchange as will be replaced just below:
			std::move(desired.m_ctrl),
			const_cast<std::remove_const_t<element_type>*>(desired.m_value),
			order);
		desired.m_ctrl = ctrl_with_one_inc;
		desired.m_value = static_cast<element_type*>(value);
		return desired;
	}
	bool compare_exchange_strong(sh::wide_shared_ptr<T>& expected, sh::wide_shared_ptr<T> desired, const std::memory_order order_success, const std::memory_order order_failure) noexcept
	{
		sh::pointer::control* expected_ctrl = expected.m_ctrl;
		atomic_control_and_value::erased_t* expected_value = const_cast<element_type*>(expected.m_value);
		const bool success = this->atomic_control_and_value::compare_exchange_strong(
			expected_ctrl, expected_value,
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<element_type*>(std::exchange(desired.m_value, nullptr)),
			order_success,
			order_failure);
		// If success: these are no-ops, expected is being set back to the same values & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_ctrl = expected_ctrl;
		expected.m_value = static_cast<element_type*>(expected_value);
		return success;
	}
	bool compare_exchange_weak(sh::wide_shared_ptr<T>& expected, sh::wide_shared_ptr<T> desired, const std::memory_order order_success, const std::memory_order order_failure) noexcept
	{
		sh::pointer::control* expected_ctrl = expected.m_ctrl;
		atomic_control_and_value::erased_t* expected_value = const_cast<element_type*>(expected.m_value);
		const bool success = this->atomic_control_and_value::compare_exchange_weak(
			expected_ctrl, expected_value,
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<element_type*>(std::exchange(desired.m_value, nullptr)),
			order_success,
			order_failure);
		// If success: these are no-ops, expected is being set back to the same values & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_ctrl = expected_ctrl;
		expected.m_value = static_cast<element_type*>(expected_value);
		return success;
	}
	bool compare_exchange_strong(sh::wide_shared_ptr<T>& expected, sh::wide_shared_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		sh::pointer::control* expected_ctrl = expected.m_ctrl;
		atomic_control_and_value::erased_t* expected_value = const_cast<element_type*>(expected.m_value);
		const bool success = this->atomic_control_and_value::compare_exchange_strong(
			expected_ctrl,
			expected_value,
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<element_type*>(std::exchange(desired.m_value, nullptr)),
			order);
		// If success: these are no-ops, expected is being set back to the same values & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_ctrl = expected_ctrl;
		expected.m_value = static_cast<element_type*>(expected_value);
		return success;
	}
	bool compare_exchange_weak(sh::wide_shared_ptr<T>& expected, sh::wide_shared_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		sh::pointer::control* expected_ctrl = expected.m_ctrl;
		atomic_control_and_value::erased_t* expected_value = const_cast<element_type*>(expected.m_value);
		const bool success = this->atomic_control_and_value::compare_exchange_weak(
			expected_ctrl,
			expected_value,
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<element_type*>(std::exchange(desired.m_value, nullptr)),
			order);
		// If success: these are no-ops, expected is being set back to the same values & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_ctrl = expected_ctrl;
		expected.m_value = static_cast<element_type*>(expected_value);
		return success;
	}

	void wait(const sh::wide_shared_ptr<T> old, const std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		this->atomic_control_and_value::wait(old.m_ctrl, old.m_value, order);
	}
	using atomic_control_and_value::notify_one;
	using atomic_control_and_value::notify_all;
	using atomic_control_and_value::is_always_lock_free;
	using atomic_control_and_value::is_lock_free;

private:
	using element_type = typename value_type::element_type;
};

template <typename T>
struct std::atomic<sh::wide_weak_ptr<T>> : private sh::pointer::atomic_control_and_value<sh::pointer::weak_policy>
{
	using atomic_control_and_value = sh::pointer::atomic_control_and_value<sh::pointer::weak_policy>;

public:
	using value_type = sh::wide_weak_ptr<T>;

	constexpr atomic() noexcept
		: atomic_control_and_value{ nullptr, nullptr }
	{ }
	constexpr atomic(std::nullptr_t) noexcept
		: atomic_control_and_value{ nullptr, nullptr }
	{ }
	constexpr atomic(sh::wide_weak_ptr<T> desired) noexcept
		: atomic_control_and_value
		{
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<std::remove_const_t<element_type>*>(std::exchange(desired.m_value, nullptr))
		}
	{ }
	~atomic() = default;
	atomic(const atomic&) = delete;
	atomic& operator=(const atomic<T>&) = delete;

	atomic& operator=(sh::wide_weak_ptr<T> desired) noexcept
	{
		store(std::move(desired));
		return *this;
	}
	operator sh::wide_weak_ptr<T>() const noexcept
	{
		return load();
	}

	void store(sh::wide_weak_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		this->atomic_control_and_value::store(
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<std::remove_const_t<element_type>*>(std::exchange(desired.m_value, nullptr)),
			order
		);
	}
	sh::wide_weak_ptr<T> load(const std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		const auto [ctrl_with_one_inc, value] = this->atomic_control_and_value::load(order);
		return sh::wide_weak_ptr<T>{ ctrl_with_one_inc, static_cast<element_type*>(value) };
	}
	sh::wide_weak_ptr<T> exchange(sh::wide_weak_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		const auto [ctrl_with_one_inc, value] = this->atomic_control_and_value::exchange(
			// Move instead of exchange as will be replaced just below:
			std::move(desired.m_ctrl),
			const_cast<std::remove_const_t<element_type>*>(desired.m_value),
			order);
		desired.m_ctrl = ctrl_with_one_inc;
		desired.m_value = static_cast<element_type*>(value);
		return desired;
	}
	bool compare_exchange_strong(sh::wide_weak_ptr<T>& expected, sh::wide_weak_ptr<T> desired, const std::memory_order order_success, const std::memory_order order_failure) noexcept
	{
		sh::pointer::control* expected_ctrl = expected.m_ctrl;
		atomic_control_and_value::erased_t* expected_value = const_cast<element_type*>(expected.m_value);
		const bool success = this->atomic_control_and_value::compare_exchange_strong(
			expected_ctrl, expected_value,
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<element_type*>(std::exchange(desired.m_value, nullptr)),
			order_success,
			order_failure);
		// If success: these are no-ops, expected is being set back to the same values & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_ctrl = expected_ctrl;
		expected.m_value = static_cast<element_type*>(expected_value);
		return success;
	}
	bool compare_exchange_weak(sh::wide_weak_ptr<T>& expected, sh::wide_weak_ptr<T> desired, const std::memory_order order_success, const std::memory_order order_failure) noexcept
	{
		sh::pointer::control* expected_ctrl = expected.m_ctrl;
		atomic_control_and_value::erased_t* expected_value = const_cast<element_type*>(expected.m_value);
		const bool success = this->atomic_control_and_value::compare_exchange_weak(
			expected_ctrl, expected_value,
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<element_type*>(std::exchange(desired.m_value, nullptr)),
			order_success,
			order_failure);
		// If success: these are no-ops, expected is being set back to the same values & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_ctrl = expected_ctrl;
		expected.m_value = static_cast<element_type*>(expected_value);
		return success;
	}
	bool compare_exchange_strong(sh::wide_weak_ptr<T>& expected, sh::wide_weak_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		sh::pointer::control* expected_ctrl = expected.m_ctrl;
		atomic_control_and_value::erased_t* expected_value = const_cast<element_type*>(expected.m_value);
		const bool success = this->atomic_control_and_value::compare_exchange_strong(
			expected_ctrl,
			expected_value,
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<element_type*>(std::exchange(desired.m_value, nullptr)),
			order);
		// If success: these are no-ops, expected is being set back to the same values & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_ctrl = expected_ctrl;
		expected.m_value = static_cast<element_type*>(expected_value);
		return success;
	}
	bool compare_exchange_weak(sh::wide_weak_ptr<T>& expected, sh::wide_weak_ptr<T> desired, const std::memory_order order = std::memory_order_seq_cst) noexcept
	{
		sh::pointer::control* expected_ctrl = expected.m_ctrl;
		atomic_control_and_value::erased_t* expected_value = const_cast<element_type*>(expected.m_value);
		const bool success = this->atomic_control_and_value::compare_exchange_weak(
			expected_ctrl,
			expected_value,
			std::exchange(desired.m_ctrl, nullptr),
			const_cast<element_type*>(std::exchange(desired.m_value, nullptr)),
			order);
		// If success: these are no-ops, expected is being set back to the same values & retained its reference.
		// If failure: expected is a new value, with a new increment. The prior value was already decremented.
		expected.m_ctrl = expected_ctrl;
		expected.m_value = static_cast<element_type*>(expected_value);
		return success;
	}

	void wait(const sh::wide_weak_ptr<T> old, const std::memory_order order = std::memory_order_seq_cst) const noexcept
	{
		this->atomic_control_and_value::wait(old.m_ctrl, old.m_value, order);
	}
	using atomic_control_and_value::notify_one;
	using atomic_control_and_value::notify_all;
	using atomic_control_and_value::is_always_lock_free;
	using atomic_control_and_value::is_lock_free;

private:
	using element_type = typename value_type::element_type;
};

namespace sh
{
	template <typename T>
	using atomic_wide_shared_ptr = ::std::atomic<::sh::wide_shared_ptr<T>>;
	template <typename T>
	using atomic_wide_weak_ptr = ::std::atomic<::sh::wide_weak_ptr<T>>;
} // namespace sh

#endif

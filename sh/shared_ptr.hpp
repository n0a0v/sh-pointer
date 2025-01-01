/*	BSD 3-Clause License

	Copyright (c) 2024, Paul Varga
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

#ifndef INC_SH__SHARED_PTR_HPP
#define INC_SH__SHARED_PTR_HPP

/**	@file
 *	This file declares sh::shared_ptr, sh::weak_ptr, and related functions
 *	mirroring those in <memory>:
 *		* allocate_shared
 *		* allocate_shared_for_overwrite
 *		* const_pointer_cast
 *		* dynamic_pointer_cast
 *		* get_deleter
 *		* make_shared
 *		* make_shared_for_overwrite
 *		* owner_less
 *		* reinterpret_pointer_cast
 *		* static_pointer_cast
 *		* std::hash<sh::shared_ptr>
 *
 *	Where most implementation of std::shared_ptr and std::weak_ptr are two
 *	pointers in size, sh::shared_ptr and sh::weak_ptr are only one. To achieve
 *	this, some limitations are incurred:
 *		1. Construction is much more limited.
 *			a. Only sh::make_shared and similar can construct a non-null
 *			   sh::shared_ptr.
 *			b. No construction from raw pointers is supported.
 *		2. Some types of polymorphic casts are unavailable.
 *			a. The assumptions regarding memory layout require the value
 *			   pointer to contain the same address.
 *			b. Conversion to a sh::wide_shared_ptr is required in these cases.
 *		3. Aliased construction is unavailable.
 *			a. See 2a & 2b.
 *		4. Types with extended alignment may not be supported (see
 *		   sh::pointer::max_alignment).
 *			a. You can increase the supported alignment by altering
 *			   sh::pointer::max_alignment at the expense of increased memory used
 *			   for padding.
 */

#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <iosfwd>

#include "pointer_traits.hpp"
#include "pointer.hpp"

/**	If SH_POINTER_DEBUG_SHARED_PTR is defined as non-zero, extra paranoid validation will be performed at runtime.
 */
#if !defined(SH_POINTER_DEBUG_SHARED_PTR) && !defined(NDEBUG)
	#define SH_POINTER_DEBUG_SHARED_PTR 1
#endif // !SH_POINTER_DEBUG_SHARED_PTR && !NDEBUG

#if SH_POINTER_DEBUG_SHARED_PTR
	#include <typeinfo>
#endif // SH_POINTER_DEBUG_SHARED_PTR

/**	Define SH_POINTER_NO_UNIQUE_ADDRESS to alias C++20's [[no_unique_address]] or a compiler specific variant.
 */
#if !defined(SH_POINTER_NO_UNIQUE_ADDRESS)
	#if defined(_MSC_VER)
		#if _MCS_VER < 1929
			#error "MSVC v14x ABI doesn't support no_unique_address. See: https://devblogs.microsoft.com/cppblog/msvc-cpp20-and-the-std-cpp20-switch/"
		#endif // _MCS_VER < 1929
		#define SH_POINTER_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
	#else // !_MSC_VER
		#define SH_POINTER_NO_UNIQUE_ADDRESS [[no_unique_address]]
	#endif // !_MSC_VER
#endif // !SH_POINTER_NO_UNIQUE_ADDRESS

namespace sh::pointer
{
	/**	The maximum alignment to be supported by sh::shared_ptr.
	 */
	constexpr std::size_t max_alignment{ alignof(std::max_align_t) };

	// Cast tag types:
	struct const_cast_tag {};
	struct dynamic_cast_tag {};
	struct reinterpret_cast_tag {};
	struct static_cast_tag {};

	/**	Value ("zero") vs default construction method enumeration.
	 */
	enum class construct_method
	{
		value_ctor,
		default_ctor
	};

	/**	Operation functions associated with a control block.
	 */
	struct control_operations final
	{
		using destruct_type = void(*)(class control*) noexcept;
		using deallocate_type = void(*)(class control*) noexcept;
		using get_deleter_type = void*(*)(class control*) noexcept;

		/**	Called with the control block to destruct associated value(s).
		 */
		destruct_type m_destruct;

		/**	Called with the control block to deallocate the control block & associated value(s).
		 */
		deallocate_type m_deallocate;

		/**	Called with the control block to return a pointer to a deleter.
		 */
		get_deleter_type m_get_deleter{ nullptr };

#if SH_POINTER_DEBUG_SHARED_PTR
		using get_element_count_type = std::size_t(*)(const class control*) noexcept;

		/**	Called with the control block to return the number of elements controlled.
		 */
		get_element_count_type m_get_element_count{ nullptr };
#endif // SH_POINTER_DEBUG_SHARED_PTR
	};

	using use_count_t = std::uint32_t;

	/**	A control block containing shared & weak reference counts and access to destruction & deallocation operations.
	 */
	class control
	{
	public:
		/**	The counter type used for combined shared (value + control) & weak (control) reference counts.
		 */
		using counter_t = std::uint_fast64_t;
		/**	Equal to a single counter_t reference on a control block.
		 */
		static constexpr counter_t control_one = 1ull << (sizeof(counter_t) * CHAR_BIT >> 1);
		/**	Equal to a single counter_t reference on an associated value.
		 */
		static constexpr counter_t value_one = 1ull;
		/**	Equal to a single counter_t reference for a weak_ptr.
		 */
		static constexpr counter_t weak_one{ control_one };
		/**	Equal to a single counter_t reference for a shared_ptr.
		 */
		static constexpr counter_t shared_one{ control_one | value_one };

		/**	Return the number of shared_one references in a given counter value.
		 *	@param counter The initial counter value.
		 *	@param operations The control operations to which a pointer is stored.
		 */
		control(const counter_t counter, const control_operations& operations) noexcept
			: m_counter{ counter }
			, m_operations{ &operations }
		{ }
		control() = delete;
		control(const control&) = delete;
		control& operator=(const control&) = delete;

		/**	Return the number of value_one references in a given counter value.
		 *	@param counter The counter from which to extract a shared_one reference count.
		 *	@return The number of value_one references in the given counter.
		 */
		static constexpr std::uint32_t to_value_count(const counter_t counter) noexcept
		{
			// Truncate to remove control bits and retain value bits.
			return std::uint32_t(counter);
		}
		/**	Return the number of shared_one references.
		 *	@detail Used by shared_ptr::use_count, weak_ptr::use_count, and weak_ptr::expired.
		 *	@note Stored count may change immediately after returning
		 *	@return The number of shared_one references.
		 */
		use_count_t get_shared_count() const noexcept
		{
			// Each value count can only be from a shared count.
			return use_count_t{ to_value_count(m_counter.load(std::memory_order_relaxed)) };
		}
		/**	Increment counter by shared_one.
		 *	@detail Used by shared_ptr.
		 */
		void shared_inc() noexcept
		{
			m_counter.fetch_add(shared_one, std::memory_order_relaxed);
		}
		/**	Decrement counter by shared_one. Calls destruct & deallocate if this was the last reference. Calls destruct if the last shared_one reference.
		 *	@detail Used by shared_ptr.
		 */
		void shared_dec() noexcept
		{
			const counter_t previous{ m_counter.fetch_sub(shared_one, std::memory_order_release) };
			if (previous == shared_one)
			{
				// Acquire if last reference control + value reference.
				std::atomic_thread_fence(std::memory_order_acquire);

				// If this was the last control reference.
				get_operations().m_destruct(this);
				get_operations().m_deallocate(this);
			}
			else if (to_value_count(previous) == 1u)
			{
				// Acquire if last reference value reference.
				std::atomic_thread_fence(std::memory_order_acquire);

				// If this was only the last value reference.
				get_operations().m_destruct(this);
			}
		}

		/**	Enumeration of return values from shared_inc_if_nonzero.
		 */
		enum class shared_inc_if_nonzero_result : std::int8_t
		{
			/**	No change was made to control.
			 */
			no_inc,
			/**	An increment of shared_one was added to control and the associated value is valid.
			 */
			added_shared_inc
		};

		/**	Try to increment counter by shared_one. Will only succeed if counter contains at least one increment of value_one.
		 *	@detail Used by weak_ptr::lock.
		 *	@return If a the counter was incremented by shared_one, added_shared_inc. If no increment was performed, no_inc.
		 */
		shared_inc_if_nonzero_result shared_inc_if_nonzero() noexcept
		{
			counter_t counter{ m_counter.load() };
			// Can't increment value if it's zero, it's already been destructed.
			while (to_value_count(counter) > 0)
			{
				if (m_counter.compare_exchange_weak(counter, counter + shared_one))
				{
					return shared_inc_if_nonzero_result::added_shared_inc;
				}
			}
			return shared_inc_if_nonzero_result::no_inc;
		}
		/**	Decrement counter by value_one & call destruct if this was the last value reference.
		 *	@detail Used by wide_weak_ptr when it must lock to cast to demote the wide_shared_ptr's reference from shared_one to control_one.
		 */
		void value_dec_for_shared_to_weak() noexcept
		{
			const counter_t previous{ m_counter.fetch_sub(value_one, std::memory_order_release) };
			if (to_value_count(previous) == 1u)
			{
				// Acquire if last value reference.
				std::atomic_thread_fence(std::memory_order_acquire);

				// If this was only the last value reference.
				get_operations().m_destruct(this);
			}
		}
		/**	Increment counter by weak_one.
		 *	@detail Used by weak_ptr.
		 */
		void weak_inc() noexcept
		{
			m_counter.fetch_add(weak_one, std::memory_order_relaxed);
		}
		/**	Decrement counter by weak_one & call deallocate if this was the last reference.
		 *	@detail Used by weak_ptr.
		 */
		void weak_dec() noexcept
		{
			// Before bothering with a store, check if we're the last
			// reference. If so, no other weak or shared pointers could
			// possibly be referencing this:
			if (m_counter.load(std::memory_order_acquire) == weak_one)
			{
				// If this was the last control reference. Value destruction
				// has already occurred, but deallocation is required.
				get_operations().m_deallocate(this);
			}
			else
			{
				// No luck, do the decrement:
				const counter_t previous{ m_counter.fetch_sub(weak_one, std::memory_order_release) };
				if (previous == weak_one)
				{
					// Acquire if last control reference.
					std::atomic_thread_fence(std::memory_order_acquire);

					// If this was the last control reference. Value
					// destruction has already occurred, but deallocation is
					// required.
					get_operations().m_deallocate(this);
				}
			}
		}
		const control_operations& get_operations() noexcept
		{
			return *m_operations;
		}

	private:
		/**	The atomic counter value.
		 */
		std::atomic<counter_t> m_counter;

		/**	Pointer to static table of operations accessible from control.
		 */
		const control_operations* const m_operations;

#if SH_POINTER_DEBUG_SHARED_PTR
	public:
		/**	In debug validation, checks that a control came from the same origin.
		 *	@param origin The origin to check against the origin set in validate_set_origin.
		 */
		void validate(const char* const origin) const noexcept
		{
			SH_POINTER_ASSERT(m_origin == origin,
				"Pointer control block origin isn't as expected.");
		}
		/**	In debug validation, check the a control will properly destruct a value.
		 *	@param origin The origin to check against the origin set in validate_set_origin.
		 */
		void validate_destruct(const char* const origin) noexcept
		{
			validate(origin);
			SH_POINTER_ASSERT(m_destructed == false,
				"Control block destructing has already been destructed.");
			SH_POINTER_ASSERT(m_deallocated == false,
				"Control block destructing has already been deallocated.");
			m_destructed = true;
		}
		/**	In debug validation, check the a control will properly deallocate (either storage or value).
		 *	@param origin The origin to check against the origin set in validate_set_origin.
		 */
		void validate_deallocate(const char* const origin) noexcept
		{
			validate(origin);
			SH_POINTER_ASSERT(m_destructed == true,
				"Control block deallocating hasn't been destructed yet.");
			SH_POINTER_ASSERT(m_deallocated == false,
				"Control block deallocating has already been deallocated");
			m_deallocated = true;
		}
		/**	In debug validation, assign an origin to be checked by other validate functions.
		 *	@param origin The origin to later check against.
		 */
		void validate_set_origin(const char* const origin) noexcept
		{
			SH_POINTER_ASSERT(m_origin == nullptr,
				"Changing control block origin a second time.");
			m_origin = origin;
		}

	private:
		/**	For debug validation, a pointer to a static string identifying where this control originated.
		 */
		const char* m_origin = nullptr;
		/**	For debug validation, a flag to show if the associated data has been destructed.
		 */
		bool m_destructed{ false };
		/**	For debug validation, a flag to show if the associated data has been deallocated.
		 */
		bool m_deallocated{ false };
#endif // SH_POINTER_DEBUG_SHARED_PTR
	};

	/**	An aligned control block. Intended to be convert to & from value(s) with convert_control_to_value and convert_value_to_control.
	 *	@note Used by sh::shared_ptr (via convert_value_to_control) and sh::weak_ptr.
	 */
	class alignas(max_alignment) convertible_control : public control
	{
		using control::control;
	};

	/**	Counterpart to std::integral_constant that is merely runtime constant.
	 */
	template <typename T>
	struct integral
	{
		const T m_value;
		/**	Return the integral value.
		 *	@return The integral value.
		 */
		constexpr T operator()() const noexcept
		{
			return m_value;
		}
	};

	/**	Offset a pointer earlier in memory by a given number of bytes and reinterpret_cast it to the specified type.
	 *	@tparam To The resulting type.
	 *	@tparam From The input type.
	 *	@tparam Offset The type of the given offset, the operator() of which will result in the numeric offset in bytes.
	 *	@param from The input pointer.
	 *	@param offset The number of bytes to offset.
	 *	@return The pointer \p from at \p offset bytes earlier as a \p To.
	 */
	template <
		typename To,
		typename From,
		typename Offset
	>
		requires std::is_pointer_v<To>
	To backward_offset_cast(From* const from, const Offset& offset) noexcept
	{
		using void_type = std::conditional_t<std::is_const_v<From>, const void, void>;
		using byte_type = std::conditional_t<std::is_const_v<From>, const std::byte, std::byte>;
		return reinterpret_cast<To>(static_cast<byte_type*>(static_cast<void_type*>(from)) - offset());
	}

	/**	Offset a pointer later in memory by a given number of bytes and reinterpret_cast it to the specified type.
	 *	@tparam To The resulting type.
	 *	@tparam From The input type.
	 *	@tparam Offset The type of the given offset, the operator() of which will result in the numeric offset in bytes.
	 *	@param from The input pointer.
	 *	@param offset The number of bytes to offset.
	 *	@return The pointer \p from at \p offset bytes later as a \p To.
	 */
	template <
		typename To,
		typename From,
		typename Offset
	>
		requires std::is_pointer_v<To>
	To forward_offset_cast(From* const from, const Offset& offset) noexcept
	{
		using void_type = std::conditional_t<std::is_const_v<From>, const void, void>;
		using byte_type = std::conditional_t<std::is_const_v<From>, const std::byte, std::byte>;
		return reinterpret_cast<To>(static_cast<byte_type*>(static_cast<void_type*>(from)) + offset());
	}

	/**	Offset a pointer earlier in memory by zero bytes and reinterpret_cast it to the specified type.
	 *	@tparam To The resulting type.
	 *	@tparam From The input type.
	 *	@tparam OffsetType The integral type of the given offset.
	 *	@param from The input pointer.
	 *	@return The pointer \p from reinterpretted as type To.
	 */
	template <
		typename To,
		typename From,
		typename OffsetType
	>
		requires std::is_pointer_v<To>
	To backward_offset_cast(From* const from, const std::integral_constant<OffsetType, 0>&) noexcept
	{
		return reinterpret_cast<To>(from);
	}

	/**	Offset a pointer later in memory by zero bytes and reinterpret_cast it to the specified type.
	 *	@tparam To The resulting type.
	 *	@tparam From The input type.
	 *	@tparam OffsetType The integral type of the given offset.
	 *	@param from The input pointer.
	 *	@return The pointer \p from reinterpretted as type To.
	 */
	template <
		typename To,
		typename From,
		typename OffsetType
	>
		requires std::is_pointer_v<To>
	To forward_offset_cast(From* const from, const std::integral_constant<OffsetType, 0>&) noexcept
	{
		return reinterpret_cast<To>(from);
	}

	/**	Offset a convertible_control to a value \p T that follows in memory.
	 *	@tparam T A reference to the type of value.
	 *	@param ctrl A reference to a convertible_control.
	 *	@return A reference to a value T that follows convertible_control.
	 */
	template <typename T>
		requires std::is_reference_v<T>
	constexpr T convert_control_to_value(convertible_control& ctrl) noexcept
	{
		using element_type = std::remove_reference_t<T>;
		return *forward_offset_cast<element_type*>(
			&ctrl,
			std::integral_constant<std::size_t, sizeof(convertible_control)>{});
	}
	/**	Offset a convertible_control to a value \p T that follows in memory.
	 *	@tparam T A pointer to the type of value.
	 *	@param ctrl A pointer to a convertible_control.
	 *	@return A pointer to a value T that follows convertible_control or nullptr.
	 */
	template <typename T>
		requires std::is_pointer_v<T>
	constexpr T convert_control_to_value(convertible_control* const ctrl) noexcept
	{
		return ctrl
			? forward_offset_cast<T>(
				ctrl,
				std::integral_constant<std::size_t, sizeof(convertible_control)>{})
			: nullptr;
	}

	/**	Offset a reference or pointer to a value to the convertible_control preceding it in memory.
	 *	@tparam T The type of the given value.
	 *	@param value A reference or pointer (may be equal to null) to a value known to have a convertible_control preceding it.
	 *	@return A reference to the convertible_control preceding value if given an reference to a value or array. If given a pointer to a value, a pointer to the convertible_control instead.
	 */
	template <typename T>
	constexpr decltype(auto) convert_value_to_control(T&& value) noexcept
	{
		using offset_type = std::integral_constant<std::size_t, sizeof(convertible_control)>;
		if constexpr (std::is_same_v<T, std::nullptr_t>)
		{
			// no return
		}
		else if constexpr (std::is_reference_v<T> && std::is_array_v<std::remove_reference_t<T>>)
		{
			// T: element_type (&value)[]:
			return convert_value_to_control(value[0]);
		}
		else if constexpr (std::is_pointer_v<std::remove_reference_t<T>>)
		{
			// T: element_type* or element_type*&:
			using element_type = std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<T>>>;
			return value
				? backward_offset_cast<convertible_control*>(
					const_cast<element_type*>(value),
					offset_type{})
				: nullptr;
		}
		else if constexpr (std::is_reference_v<T>)
		{
			// T: element_type&
			using element_type = std::remove_const_t<std::remove_reference_t<T>>;
			return *backward_offset_cast<convertible_control*>(
				std::addressof(const_cast<element_type&>(value)),
				offset_type{});
		}
		// no return
	}

	/**	Allocate a control block associated with a value of type T using a given allocator.
	 *	@tparam T The value type.
	 *	@tparam Alloc The allocator type.
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (false == std::is_array_v<T>)
	class value_convertible_to_control final
	{
	private:
		using element_type = T;
		using allocator_traits = std::allocator_traits<Alloc>;
		using value_allocator_traits = typename allocator_traits::template rebind_traits<element_type>;
		using value_allocator = typename value_allocator_traits::allocator_type;

		/**	A convertible control block with an allocator and storage space for an associate value.
		 */
		struct storage_type final
		{
			/**	Construct storage for an element_type.
			 *	@param alloc The allocator to be used for constructing and destroying the value.
			 */
			explicit storage_type(value_allocator&& alloc) noexcept
				: m_ctrl{ control::shared_one, value_convertible_to_control::operations() }
				, m_alloc{ alloc }
			{
				static_assert(std::is_nothrow_move_constructible_v<value_allocator>,
					"Exceptions from value_allocator move contructor aren't expected.");
				static_assert(std::is_nothrow_constructible_v<
						convertible_control,
						decltype(control::shared_one),
						decltype(value_convertible_to_control::operations())>,
					"Exceptions from convertible_control constructor aren't expected.");
				static_assert(is_pointer_interconvertible_with_class(&std::remove_pointer_t<decltype(this)>::m_ctrl),
					"reinterpret_cast from convertible_control to storage_type must be valid.");
				static_assert(offsetof(storage_type, m_value) - offsetof(storage_type, m_ctrl) == sizeof(convertible_control),
					"convert_value_to_control only valid if m_ctrl to m_value offset is sizeof(convertible_control).");
			}

			/**	Control block convertible to and from m_value.
			 *	@note Must be first member of storage_type as reinterpret_cast is used to convert from convertible_control.
			 */
			convertible_control m_ctrl;

			/**	Storage space to be used for the construction of element_type.
			 *	@note Must immediately follow and be similarly aligned to convertible_control for conversion to work.
			 */
			alignas(convertible_control) std::byte m_value[sizeof(element_type)];

			/**	Allocator used for constructing and destroying value.
			 */
			SH_POINTER_NO_UNIQUE_ADDRESS value_allocator m_alloc;
		};

		using storage_allocator_traits = typename allocator_traits::template rebind_traits<storage_type>;
		using storage_allocator = typename storage_allocator_traits::allocator_type;

#if SH_POINTER_DEBUG_SHARED_PTR
		/**	For debug validation, return a pointer to a static string identifying this class.
		 *	@return A pointer to a static string identifying this class.
		 */
		static const char* origin() noexcept
		{
			static const char* const instance = typeid(value_convertible_to_control).name();
			return instance;
		}
#endif // SH_POINTER_DEBUG_SHARED_PTR

		/**	Return a reference to a static control_operations structure.
		 *	@return A reference to a static control_operations structure.
		 */
		static const control_operations& operations() noexcept
		{
			static const control_operations instance{
#ifdef __cpp_designated_initializers
				.m_destruct =
#endif // __cpp_designated_initializers
				/* destruct */
				[](control* const ctrl) noexcept -> void
				{
#if SH_POINTER_DEBUG_SHARED_PTR
					ctrl->validate_destruct(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR

					storage_type& storage = reinterpret_cast<storage_type&>(static_cast<convertible_control&>(*ctrl));
					element_type& value = convert_control_to_value<element_type&>(storage.m_ctrl);
					value_allocator_traits::destroy(storage.m_alloc, std::addressof(value));
				},
#ifdef __cpp_designated_initializers
				.m_deallocate =
#endif // __cpp_designated_initializers
				/* deallocate */
				[](control* const ctrl) noexcept -> void
				{
#if SH_POINTER_DEBUG_SHARED_PTR
					ctrl->validate_deallocate(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR

					storage_type* const storage = reinterpret_cast<storage_type*>(static_cast<convertible_control*>(ctrl));

					// Move this allocator out of storage before destroying & deleting it.
					storage_allocator storage_alloc{ std::move(storage->m_alloc) };
					storage_allocator_traits::destroy(storage_alloc, storage);
					storage_allocator_traits::deallocate(storage_alloc, storage, 1);
				},
#ifdef __cpp_designated_initializers
				.m_get_deleter =
#endif // __cpp_designated_initializers
				/* get_deleter */ nullptr,
#if SH_POINTER_DEBUG_SHARED_PTR
#ifdef __cpp_designated_initializers
				.m_get_element_count =
#endif // __cpp_designated_initializers
				/* get_element_count */
				[](const control* const ctrl) noexcept -> std::size_t
				{
					ctrl->validate(origin());
					return 1;
				},
#endif // SH_POINTER_DEBUG_SHARED_PTR
			};
			return instance;
		}

	public:
		/**	Allocate a control block associated with a value using a given allocator, the latter constructed with the given arguments.
		 *	@throw May throw std::bad_alloc or other exceptions during allocation & construction.
		 *	@tparam Construct If no arguments are given and this is default_ctor, value may be default constructed. Otherwise uses value construction.
		 *	@tparam Args The argument types to pass to value's constructor T::T.
		 *	@param alloc The allocator.
		 *	@param args The arguments to pass to value's constructor T::T.
		 *	@return The pointer to the control value. Use convert_value_to_control to access the associated control block.
		 */
		template <construct_method Construct, typename... Args>
		static element_type* allocate(const Alloc& alloc, Args&&... args)
		{
			storage_allocator storage_alloc{ alloc };

			constexpr std::size_t storage_element_count{ 1 };
			storage_type* const storage = storage_allocator_traits::allocate(storage_alloc, storage_element_count);
			storage_allocator_traits::construct(storage_alloc, storage, value_allocator{ alloc });

			element_type* const value = reinterpret_cast<element_type*>(&storage->m_value);

			const auto construct_value = [&args...](value_allocator& alloc, element_type* const value)
				noexcept(
					(Construct == construct_method::value_ctor && std::is_nothrow_constructible_v<element_type, Args...>)
					|| (Construct == construct_method::default_ctor && std::is_nothrow_default_constructible_v<element_type>)
				) -> void
			{
				if constexpr (Construct == construct_method::value_ctor)
				{
					value_allocator_traits::construct(alloc, value, std::forward<Args>(args)...);
				}
				else
				{
					static_assert(sizeof...(args) == 0, "Default construction and arguments are mutually exclusive.");
					constexpr std::size_t element_count{ 1 };
					std::uninitialized_default_construct_n(value, element_count);
				}
			};
			if constexpr (noexcept(construct_value(storage->m_alloc, value)))
			{
				construct_value(storage->m_alloc, value);
			}
			else
			{
				try
				{
					construct_value(storage->m_alloc, value);
				}
				catch (...)
				{
					storage_allocator_traits::destroy(storage_alloc, storage);
					storage_allocator_traits::deallocate(storage_alloc, storage, storage_element_count);
					throw;
				}
			}
#if SH_POINTER_DEBUG_SHARED_PTR
			storage->m_ctrl.validate_set_origin(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR
			return value;
		}
	};

	/**	Allocate a control block associated with a value of type T using a given allocator.
	 *	@tparam Alloc The allocator type.
	 *	@tparam T The value type. Must be an array type (e.g., T[]).
	 */
	template <
		typename T,
		typename Alloc,
		typename Count
	>
	class array_of_values_convertible_to_control final
	{
	private:
		using element_type = std::remove_extent_t<T>;
		static_assert(alignof(element_type) <= max_alignment,
			"element_type has extended alignment, beyond that which sh::shared_ptr expects. See sh::pointer::max_alignment.");

		using allocator_traits = std::allocator_traits<Alloc>;
		using count_type = Count;

		using value_allocator_traits = typename allocator_traits::template rebind_traits<element_type>;
		using value_allocator = typename value_allocator_traits::allocator_type;

		/**	A convertible control block with an allocator and element count.
		 */
		struct storage_type final
		{
			/**	Construct storage for an element_type.
			 *	@param alloc The allocator to be used for constructing and destroying the value.
			 */
			storage_type(value_allocator&& alloc, count_type element_count) noexcept
				: m_alloc{ std::move(alloc) }
				, m_element_count{ std::move(element_count) }
				, m_ctrl{ control::shared_one, array_of_values_convertible_to_control::operations() }
			{
				static_assert(std::is_nothrow_move_constructible_v<value_allocator>,
					"Exceptions from value_allocator move contructor aren't expected.");
				static_assert(std::is_nothrow_move_constructible_v<count_type>,
					"Exceptions from count_type move contructor aren't expected.");
				static_assert(std::is_nothrow_constructible_v<
						convertible_control,
						decltype(control::shared_one),
						decltype(array_of_values_convertible_to_control::operations())>,
					"Exceptions from convertible_control constructor aren't expected.");
				static_assert(offsetof(storage_type, m_ctrl) + sizeof(convertible_control) == sizeof(storage_type),
					"convert_value_to_control only valid if m_ctrl to values offset (following storage_type) is sizeof(convertible_control).");
			}

			/**	Allocator used for constructing and destroying value.
			 */
			SH_POINTER_NO_UNIQUE_ADDRESS value_allocator m_alloc;

			/**	The number of elements of element_type that follow storage_type in memory.
			 */
			SH_POINTER_NO_UNIQUE_ADDRESS const count_type m_element_count;

			/**	Control block convertible to and from m_value.
			 *	@note Must be first member of storage_type as reinterpret_cast is used to convert from convertible_control.
			 */
			convertible_control m_ctrl;
		};

		using storage_allocator_traits = typename allocator_traits::template rebind_traits<storage_type>;
		using storage_allocator = typename storage_allocator_traits::allocator_type;

		/**	Trivial data type sized & aligned as storage_type (aligned as max_alignment).
		 */
		struct alignas(storage_type) aligned_bytes final
		{
			std::byte m_bytes[sizeof(storage_type)];

			/**	Return the number of aligned_byte elements required to hold storage_type + the given count of element_type elements.
			 *	@param element_count The number of elements of element_type to allocate in the array.
			 *	@return The number of aligned_byte elements that will provide sufficient memory.
			 */
			static constexpr auto element_count(const count_type& element_count) noexcept
			{
				return
					/* storage_type[1]: */ 1u +
					/* element_type[element_count]: */ (
						((element_count() * sizeof(element_type)) // sizeof(element_type[element_count])
							+ (sizeof(aligned_bytes) - 1u)) // 1 minus sizeof(aligned_bytes) to cause ceil during truncating division.
						/ sizeof(aligned_bytes) // convert from sizeof bytes to element count of aligned_bytes.
					);
			}
		};
		using aligned_bytes_allocator_traits = typename allocator_traits::template rebind_traits<aligned_bytes>;
		using aligned_bytes_allocator = typename aligned_bytes_allocator_traits::allocator_type;

#if SH_POINTER_DEBUG_SHARED_PTR
		/**	For debug validation, return a pointer to a static string identifying this class.
		 *	@return A pointer to a static string identifying this class.
		 */
		static const char* origin() noexcept
		{
			static const char* const instance = typeid(array_of_values_convertible_to_control).name();
			return instance;
		}
#endif // SH_POINTER_DEBUG_SHARED_PTR

		/**	Return a reference to a static control_operations structure.
		 *	@return A reference to a static control_operations structure.
		 */
		static const control_operations& operations() noexcept
		{
			static const control_operations instance{
#ifdef __cpp_designated_initializers
				.m_destruct =
#endif // __cpp_designated_initializers
				/* destruct */
				[](control* const ctrl) noexcept -> void
				{
#if SH_POINTER_DEBUG_SHARED_PTR
					ctrl->validate_destruct(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR
					storage_type* const storage = backward_offset_cast<storage_type*>(
						static_cast<convertible_control*>(ctrl),
						std::integral_constant<std::size_t, offsetof(storage_type, m_ctrl)>{});

					element_type* const values = std::addressof(convert_control_to_value<element_type&>(storage->m_ctrl));

					for (element_type* cur = values + storage->m_element_count(); cur != values; )
					{
						--cur;
						value_allocator_traits::destroy(storage->m_alloc, cur);
					}
				},
#ifdef __cpp_designated_initializers
				.m_deallocate =
#endif // __cpp_designated_initializers
				/* deallocate */
				[](control* const ctrl) noexcept -> void
				{
#if SH_POINTER_DEBUG_SHARED_PTR
					ctrl->validate_deallocate(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR
					storage_type* const storage = backward_offset_cast<storage_type*>(
						static_cast<convertible_control*>(ctrl),
						std::integral_constant<std::size_t, offsetof(storage_type, m_ctrl)>{});
					aligned_bytes* const bytes = reinterpret_cast<aligned_bytes*>(storage);

					// Move this allocator, element_count out of storage before destroying & deleting it.
					storage_allocator storage_alloc{ std::move(storage->m_alloc) };
					const count_type element_count{ std::move(storage->m_element_count) };
					storage_allocator_traits::destroy(storage_alloc, storage);

					aligned_bytes_allocator aligned_bytes_alloc{ std::move(storage_alloc) };
					aligned_bytes_allocator_traits::deallocate(
						aligned_bytes_alloc,
						bytes,
						aligned_bytes::element_count(element_count));
				},
#ifdef __cpp_designated_initializers
				.m_get_deleter =
#endif // __cpp_designated_initializers
				/* get_deleter */ nullptr,
#if SH_POINTER_DEBUG_SHARED_PTR
#ifdef __cpp_designated_initializers
				.m_get_element_count =
#endif // __cpp_designated_initializers
				/* get_element_count */
				[](const control* const ctrl) noexcept -> std::size_t
				{
					ctrl->validate(origin());
					const storage_type* const storage = backward_offset_cast<const storage_type*>(
						static_cast<const convertible_control*>(ctrl),
						std::integral_constant<std::size_t, offsetof(storage_type, m_ctrl)>{});
					return storage->m_element_count();
				},
#endif // SH_POINTER_DEBUG_SHARED_PTR
			};
			return instance;
		}

	public:
		/**	Allocate a control block associated with an array of values using a given allocator.
		 *	@throw May throw std::bad_alloc or other exceptions during allocation & construction.
		 *	@tparam Construct If no arguments are given and this is default_ctor, value may be default constructed. Otherwise uses value construction.
		 *	@tparam Args The argument types to pass to value's constructor T::T.
		 *	@param alloc The value allocator.
		 *	@param element_count The number of elements of element_type to allocate in the array.
		 *	@param args The arguments to pass to value's constructor (element_type::element_type).
		 *	@return The pointer to the control value. Use convert_value_to_control to access the associated control block.
		 */
		template <construct_method Construct, typename... Args>
		static element_type* allocate_array(const Alloc& alloc, const count_type& element_count, Args&&... args)
		{
			static_assert(sizeof(storage_type) == sizeof(aligned_bytes));
			aligned_bytes_allocator aligned_bytes_alloc{ alloc };
			const std::size_t aligned_byte_element_count = aligned_bytes::element_count(element_count);

			aligned_bytes* const bytes = aligned_bytes_allocator_traits::allocate(aligned_bytes_alloc, aligned_byte_element_count);

			storage_type* const storage = reinterpret_cast<storage_type*>(bytes);
			storage_allocator storage_alloc{ alloc };
			storage_allocator_traits::construct(storage_alloc, storage,
				value_allocator{ alloc },
				element_count
			);

			element_type* const values = std::addressof(convert_control_to_value<element_type&>(storage->m_ctrl));

			decltype(element_count()) construct_index{ 0u };
			const auto construct_values = [&element_count, &args..., &construct_index](value_allocator& alloc, element_type* const values)
				noexcept(
					(Construct == construct_method::value_ctor && std::is_nothrow_constructible_v<element_type, Args...>)
					|| (Construct == construct_method::default_ctor && std::is_nothrow_default_constructible_v<element_type>)
				) -> void
			{
				if (element_count() > 0)
				{
					const decltype(element_count()) construct_count{ element_count() };
					const decltype(element_count()) construct_copy_count{ construct_count - 1 };
					// Construct [0, construct_count - 1) from left-to-right by copying args.
					while (construct_index < construct_copy_count)
					{
						if constexpr (Construct == construct_method::value_ctor)
						{
							value_allocator_traits::construct(alloc, values + construct_index, args...);
						}
						else
						{
							static_assert(sizeof...(args) == 0, "Default construction and arguments are mutually exclusive.");
							std::uninitialized_default_construct_n(values + construct_index, 1u);
						}
						++construct_index;
					}
					// Construct [construct_count - 1] by forwarding args.
					if constexpr (Construct == construct_method::value_ctor)
					{
						value_allocator_traits::construct(alloc, values + construct_index, std::forward<Args>(args)...);
					}
					else
					{
						static_assert(sizeof...(args) == 0, "Default construction and arguments are mutually exclusive.");
						std::uninitialized_default_construct_n(values + construct_index, 1u);
					}
				}
			};
			if constexpr (noexcept(construct_values(storage->m_alloc, values)))
			{
				construct_values(storage->m_alloc, values);
			}
			else
			{
				try
				{
					construct_values(storage->m_alloc, values);
				}
				catch (...)
				{
					// Destroy [0, construct_index) from right-to-left.
					for (auto destroy_index{ construct_index }; destroy_index > 0; )
					{
						--destroy_index;
						value_allocator_traits::destroy(storage->m_alloc, values + destroy_index);
					}
					storage_allocator_traits::destroy(storage_alloc, storage);
					aligned_bytes_allocator_traits::deallocate(aligned_bytes_alloc, bytes, aligned_byte_element_count);
					throw;
				}
			}
#if SH_POINTER_DEBUG_SHARED_PTR
			storage->m_ctrl.validate_set_origin(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR
			return values;
		}
	};

	/**	A wrapper around std::allocator for use by sh::make_shared.
	 */
	template <typename T>
	struct default_allocator : private std::allocator<T>
	{
		using allocator_traits = std::allocator_traits<std::allocator<T>>;
		using value_type = typename allocator_traits::value_type;
		using size_type = typename allocator_traits::size_type;
		using difference_type = typename allocator_traits::difference_type;

		template <typename U>
		struct rebind
		{
			using other = default_allocator<U>;
		};

		template <typename U>
		friend struct default_allocator;

		default_allocator() = default;
		default_allocator(const default_allocator& other) = default;
		default_allocator(default_allocator&& other) noexcept = default;
		default_allocator& operator=(const default_allocator& other) = default;
		default_allocator& operator=(default_allocator&& other) noexcept = default;

		template <typename U>
		constexpr explicit default_allocator(const default_allocator<U>& other)
			noexcept(std::is_nothrow_copy_constructible_v<std::allocator<T>>)
			: std::allocator<T>{ other }
		{ }
		template <typename U>
		constexpr explicit default_allocator(default_allocator<U>&& other)
			noexcept(std::is_nothrow_move_constructible_v<std::allocator<T>>)
			: std::allocator<T>{ std::move(other) }
		{ }

		[[nodiscard]] constexpr T* allocate(const std::size_t n)
			noexcept(noexcept(allocator_traits::allocate(static_cast<std::allocator<T>&>(*this), n)))
		{
			return allocator_traits::allocate(static_cast<std::allocator<T>&>(*this), n);
		}
		constexpr void deallocate(T* const p, const std::size_t n)
			noexcept(noexcept(allocator_traits::deallocate(static_cast<std::allocator<T>&>(*this), p, n)))
		{
			allocator_traits::deallocate(static_cast<std::allocator<T>&>(*this), p, n);
		}
		template <typename U, typename... Args>
		constexpr void construct(U* const p, Args&&... args)
			noexcept(noexcept(allocator_traits::construct(static_cast<std::allocator<T>&>(*this), p, args...)))
		{
#if !defined(__GLIBCXX__)
			// At present, only libstdc++ std::construct_at (used during
			// std::allocator::construct) supports multidimensional allocation.
			if constexpr (std::is_array_v<U>)
			{
				static_assert(sizeof...(args) == 0, "Arguments are unexpected when constructing array.");
				default_allocator<std::remove_extent_t<U>> element_alloc{ *this };
				using element_allocator_traits = std::allocator_traits<decltype(element_alloc)>;
				for (std::size_t index = 0; index < std::rank_v<T>; ++index)
				{
					element_allocator_traits::construct(element_alloc, p + index);
				}
			}
			else
#endif // !__GLIBCXX__
			{
				allocator_traits::construct(static_cast<std::allocator<T>&>(*this), p, std::forward<Args>(args)...);
			}
		}
		template <typename U>
		constexpr void destroy(U* const p)
			noexcept(noexcept(allocator_traits::destroy(static_cast<std::allocator<T>&>(*this), p)))
		{
#if !defined(__GLIBCXX__)
			if constexpr (std::is_array_v<U>)
			{
				default_allocator<std::remove_extent_t<U>> element_alloc{ *this };
				using element_allocator_traits = std::allocator_traits<decltype(element_alloc)>;
				// Destroy right-to-left:
				for (std::size_t index = std::rank_v<T>; index > 0; )
				{
					--index;
					element_allocator_traits::destroy(element_alloc, p + index);
				}
			}
			else
#endif // !__GLIBCXX__
			{
				allocator_traits::destroy(static_cast<std::allocator<T>&>(*this), p);
			}
		}
	};

} // namespace sh::pointer

namespace sh
{
	template <typename T> class wide_shared_ptr;
	template <typename T> class wide_weak_ptr;
	template <typename T> class shared_ptr;
	template <typename T> class weak_ptr;

	/**	A reference counting owner of allocated data similar to a more limited std::shared_ptr that is only a single pointer wide.
	 */
	template <typename T>
	class shared_ptr
	{
	public:
		using element_type = std::remove_extent_t<T>;
		using weak_type = shared_ptr<T>;

		constexpr shared_ptr() noexcept
			: m_value{ nullptr }
		{ }
		constexpr shared_ptr(std::nullptr_t) noexcept
			: m_value{ nullptr }
		{ }
		shared_ptr(const shared_ptr<T>& other) noexcept
			: m_value{ other.m_value }
		{
			increment(m_value);
		}
		shared_ptr(shared_ptr<T>&& other) noexcept
			: m_value{ std::exchange(other.m_value, nullptr) }
		{ }
		~shared_ptr()
		{
			decrement(m_value);
		}

		shared_ptr& operator=(const shared_ptr<T>& other) noexcept
		{
			increment(other.m_value);
			decrement(m_value);
			m_value = other.m_value;
			return *this;
		}
		shared_ptr& operator=(shared_ptr<T>&& other) noexcept
		{
			if (this != &other)
			{
				element_type* const value = std::exchange(other.m_value, nullptr);
				this->decrement(m_value);
				m_value = value;
			}
			return *this;
		}

		void reset() noexcept
		{
			decrement(std::exchange(m_value, nullptr));
		}
		void swap(shared_ptr& other) noexcept
		{
			std::swap(m_value, other.m_value);
		}

		element_type* get() const noexcept
		{
			return m_value;
		}
		element_type& operator*() const noexcept
		{
			SH_POINTER_ASSERT(m_value != nullptr, "Dereferencing nullptr shared_ptr.");
			return *m_value;
		}
		element_type* operator->() const noexcept
		{
			SH_POINTER_ASSERT(m_value != nullptr, "Dereferencing nullptr shared_ptr.");
			return m_value;
		}
		element_type& operator[](const std::ptrdiff_t idx) const noexcept
		{
			SH_POINTER_ASSERT(idx >= 0, "Negative index given to shared_ptr::operator[] has undefined results.");
			if constexpr (std::is_array_v<T>)
			{
				SH_POINTER_ASSERT(
					m_value != nullptr
					&& (pointer::convert_value_to_control(*m_value).get_operations().m_get_element_count == nullptr
						|| std::size_t(idx) < pointer::convert_value_to_control(*m_value).get_operations().m_get_element_count(
							&pointer::convert_value_to_control(*m_value)
						))
					, "Index given to shared_ptr::operator[] is out of bounds."
				);
			}
			else
			{
				SH_POINTER_ASSERT(idx == 0, "Index given to shared_ptr::operator[] is out of bounds.");
				SH_POINTER_ASSERT(m_value != nullptr, "Dereferencing nullptr shared_ptr in operator[].");
			}
			return m_value[idx];
		}
		pointer::use_count_t use_count() const noexcept
		{
			return m_value ? pointer::convert_value_to_control(*m_value).get_shared_count() : pointer::use_count_t{ 0 };
		}
		explicit constexpr operator bool() const noexcept
		{
			return m_value != nullptr;
		}
		template <typename Y>
		bool owner_before(const shared_ptr<Y>& other) const noexcept
		{
			// As m_ctrl and other.m_ctrl are both offsets of m_value and
			// other.m_value, we can determine owner ordering by comparing
			// values.
			return m_value < other.m_value;
		}
		template <typename Y>
		bool owner_before(const weak_ptr<Y>& other) const noexcept
		{
			return pointer::convert_value_to_control(m_value) < other.m_ctrl;
		}
		template <typename Y>
		bool owner_before(const wide_shared_ptr<Y>& other) const noexcept
		{
			return pointer::convert_value_to_control(m_value) < other.m_ctrl;
		}
		template <typename Y>
		bool owner_before(const wide_weak_ptr<Y>& other) const noexcept
		{
			return pointer::convert_value_to_control(m_value) < other.m_ctrl;
		}

		// implicit conversion
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		shared_ptr(const shared_ptr<U>& other) noexcept
			: m_value{ other.get() }
		{
			increment(m_value);
		}
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		shared_ptr(shared_ptr<U>&& other) noexcept
			: m_value{ std::exchange(other.m_value, nullptr) }
		{ }
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		shared_ptr& operator=(const shared_ptr<U>& other) noexcept
		{
			increment(other.get());
			decrement(m_value);
			m_value = other.get();
			SH_POINTER_ASSERT(static_cast<const void*>(m_value) == static_cast<const void*>(other.get()),
				"sh::shared_ptr doesn't support offset value pointers.");
			return *this;
		}
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		shared_ptr& operator=(shared_ptr<U>&& other) noexcept
		{
			auto* const value = std::exchange(other.m_value, nullptr);
			this->decrement(m_value);
			m_value = value;
			SH_POINTER_ASSERT(static_cast<const void*>(m_value) == static_cast<const void*>(value),
				"sh::shared_ptr doesn't support offset value pointers.");
			return *this;
		}

		// const_cast
		template <typename U>
			requires (std::is_convertible_v<const U*, const T*>
				&& is_pointer_interconvertible_v<std::remove_const_t<U>, std::remove_const_t<T>>)
		shared_ptr(const pointer::const_cast_tag&, const shared_ptr<U>& other) noexcept
			: m_value{ const_cast<element_type*>(other.get()) }
		{
			increment(m_value);
		}
		template <typename U>
			requires (std::is_convertible_v<const U*, const T*>
				&& is_pointer_interconvertible_v<std::remove_const_t<U>, std::remove_const_t<T>>)
		shared_ptr(const pointer::const_cast_tag&, shared_ptr<U>&& other) noexcept
			: m_value{ const_cast<element_type*>(std::exchange(other.m_value, nullptr)) }
		{ }

		// dynamic_cast
		template <typename U>
			requires is_pointer_interconvertible_v<U, T>
		shared_ptr(const pointer::dynamic_cast_tag&, const shared_ptr<U>& other) noexcept
			: m_value{ dynamic_cast<element_type*>(other.get()) }
		{
			increment(m_value);
		}
		template <typename U>
			requires is_pointer_interconvertible_v<U, T>
		shared_ptr(const pointer::dynamic_cast_tag&, shared_ptr<U>&& other) noexcept
			: m_value{ dynamic_cast<element_type*>(std::exchange(other.m_value, nullptr)) }
		{ }

		// static_cast
		template <typename U>
			requires is_pointer_interconvertible_v<U, T>
		shared_ptr(const pointer::static_cast_tag&, const shared_ptr<U>& other) noexcept
			: m_value{ static_cast<element_type*>(other.get()) }
		{
			increment(m_value);
		}
		template <typename U>
			requires is_pointer_interconvertible_v<U, T>
		shared_ptr(const pointer::static_cast_tag&, shared_ptr<U>&& other) noexcept
			: m_value{ static_cast<element_type*>(std::exchange(other.m_value, nullptr)) }
		{ }

		// reinterpret_cast
		template <typename U>
		shared_ptr(const pointer::reinterpret_cast_tag&, const shared_ptr<U>& other) noexcept
			: m_value{ reinterpret_cast<element_type*>(other.get()) }
		{
			increment(m_value);
		}
		template <typename U>
		shared_ptr(const pointer::reinterpret_cast_tag&, shared_ptr<U>&& other) noexcept
			: m_value{ reinterpret_cast<element_type*>(std::exchange(other.m_value, nullptr)) }
		{ }

	private:
		template <typename U> friend class wide_shared_ptr;
		template <typename U> friend class shared_ptr;
		template <typename U> friend class weak_ptr;

		template <typename U, typename Alloc, typename... Args>
			requires (false == std::is_array_v<U>
				&& alignof(U) <= pointer::max_alignment)
		friend shared_ptr<U> allocate_shared(const Alloc& alloc, Args&&... args);

		template <typename U, typename Alloc>
			requires (false == std::is_array_v<U>
				&& alignof(U) <= pointer::max_alignment)
		friend shared_ptr<U> allocate_shared_for_overwrite(const Alloc& alloc);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> == 0
				&& alignof(U) <= pointer::max_alignment)
		friend shared_ptr<U> allocate_shared(const Alloc& alloc, std::size_t element_count);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> > 0
				&& alignof(U) <= pointer::max_alignment)
		friend shared_ptr<U> allocate_shared(const Alloc& alloc);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> == 0
				&& alignof(U) <= pointer::max_alignment)
		friend shared_ptr<U> allocate_shared(const Alloc& alloc, std::size_t element_count, const std::remove_extent_t<U>& init_value);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> > 0
				&& alignof(U) <= pointer::max_alignment)
		friend shared_ptr<U> allocate_shared(const Alloc& alloc, const std::remove_extent_t<U>& init_value);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> == 0
				&& alignof(U) <= pointer::max_alignment)
		friend shared_ptr<U> allocate_shared_for_overwrite(const Alloc& alloc, std::size_t element_count);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> > 0
				&& alignof(U) <= pointer::max_alignment)
		friend shared_ptr<U> allocate_shared_for_overwrite(const Alloc& alloc);

		static void increment(element_type* const value) noexcept
		{
			if (value)
			{
				pointer::convert_value_to_control(*value).shared_inc();
			}
		}
		static void decrement(element_type* const value) noexcept
		{
			if (value)
			{
				pointer::convertible_control& ctrl = pointer::convert_value_to_control(*value);
				ctrl.shared_dec();
			}
		}

		explicit shared_ptr(element_type* const value_with_one_ref) noexcept
			: m_value{ value_with_one_ref }
		{ }

		element_type* m_value;
	};

	/**	A reference counting weak owner of allocated data similar to a more limited std::weak_ptr that is only a single pointer wide.
	 */
	template <typename T>
	class weak_ptr
	{
	public:
		using element_type = std::remove_extent_t<T>;

		constexpr weak_ptr() noexcept
			: m_ctrl{ nullptr }
		{ }
		weak_ptr(const weak_ptr<T>& other) noexcept
			: m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		weak_ptr(weak_ptr<T>&& other) noexcept
			: m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{ }
		weak_ptr(const shared_ptr<T>& other) noexcept
			: m_ctrl{ pointer::convert_value_to_control(other.get()) }
		{
			increment(m_ctrl);
		}
		~weak_ptr()
		{
			decrement(m_ctrl);
		}

		weak_ptr& operator=(const weak_ptr<T>& other) noexcept
		{
			increment(other.m_ctrl);
			decrement(m_ctrl);
			m_ctrl = other.m_ctrl;
			return *this;
		}
		weak_ptr& operator=(weak_ptr<T>&& other) noexcept
		{
			if (this != &other)
			{
				pointer::convertible_control* const ctrl = std::exchange(other.m_ctrl, nullptr);
				this->decrement(m_ctrl);
				m_ctrl = ctrl;
			}
			return *this;
		}
		weak_ptr& operator=(const shared_ptr<T>& other) noexcept
		{
			pointer::convertible_control* const ctrl = pointer::convert_value_to_control(other.get());
			increment(ctrl);
			decrement(m_ctrl);
			m_ctrl = ctrl;
			return *this;
		}

		void reset() noexcept
		{
			decrement(std::exchange(m_ctrl, nullptr));
		}
		void swap(weak_ptr& other) noexcept
		{
			using std::swap;
			swap(m_ctrl, other.m_ctrl);
		}
		pointer::use_count_t use_count() const noexcept
		{
			return m_ctrl ? m_ctrl->get_shared_count() : pointer::use_count_t{ 0 };
		}
		shared_ptr<T> lock() const noexcept
		{
			return m_ctrl
				&& m_ctrl->shared_inc_if_nonzero() == pointer::control::shared_inc_if_nonzero_result::added_shared_inc
				? shared_ptr<T>{ std::addressof(pointer::convert_control_to_value<element_type&>(*m_ctrl)) }
				: shared_ptr<T>{ nullptr };
		}
		bool expired() const noexcept
		{
			return m_ctrl == nullptr || m_ctrl->get_shared_count() == 0;
		}
		template <typename Y>
		bool owner_before(const shared_ptr<Y>& other) const noexcept
		{
			return m_ctrl < pointer::convert_value_to_control(other.m_value);
		}
		template <typename Y>
		bool owner_before(const weak_ptr<Y>& other) const noexcept
		{
			return m_ctrl < other.m_ctrl;
		}
		template <typename Y>
		bool owner_before(const wide_shared_ptr<Y>& other) const noexcept
		{
			return m_ctrl < other.m_ctrl;
		}
		template <typename Y>
		bool owner_before(const wide_weak_ptr<Y>& other) const noexcept
		{
			return m_ctrl < other.m_ctrl;
		}

		// implicit conversion from weak_ptr
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		weak_ptr(const weak_ptr<U>& other) noexcept
			: m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		weak_ptr(weak_ptr<U>&& other) noexcept
			: m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{ }
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		weak_ptr& operator=(const weak_ptr<U>& other) noexcept
		{
			increment(other.m_ctrl);
			decrement(m_ctrl);
			m_ctrl = other.m_ctrl;
			return *this;
		}
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		weak_ptr& operator=(weak_ptr<U>&& other) noexcept
		{
			pointer::convertible_control* const ctrl = std::exchange(other.m_ctrl, nullptr);
			this->decrement(m_ctrl);
			m_ctrl = ctrl;
			return *this;
		}

		// implicit conversion from shared_ptr
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		weak_ptr(const shared_ptr<U>& other) noexcept
			: m_ctrl{ pointer::convert_value_to_control(other.get()) }
		{
			increment(m_ctrl);
		}
		template <typename U>
			requires (std::is_convertible_v<U*, T*>
				&& is_pointer_interconvertible_v<U, T>)
		weak_ptr& operator=(const shared_ptr<U>& other) noexcept
		{
			pointer::convertible_control* const ctrl = pointer::convert_value_to_control(other.get());
			increment(ctrl);
			decrement(m_ctrl);
			m_ctrl = ctrl;
			return *this;
		}

	private:
		template <typename U> friend class wide_shared_ptr;
		template <typename U> friend class wide_weak_ptr;
		template <typename U> friend class shared_ptr;
		template <typename U> friend class weak_ptr;

		static void increment(pointer::convertible_control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->weak_inc();
			}
		}
		static void decrement(pointer::convertible_control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->weak_dec();
			}
		}

		pointer::convertible_control* m_ctrl;
	};

	/**	Constructs a sh::shared_ptr to own a (value initialized) element T using the supplied allocator.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@tparam Args The types of arguments passed to T's constructor.
	 *	@param alloc The allocator to use.
	 *	@param args The arguments passed to the constructor of T.
	 *	@return A non-null sh::shared_ptr owning the element T.
	 */
	template <
		typename T,
		typename Alloc,
		typename... Args
	>
		requires (false == std::is_array_v<T>
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> allocate_shared(const Alloc& alloc, Args&&... args)
	{
		using origin_type = pointer::value_convertible_to_control<
			T,
			Alloc
		>;
		return shared_ptr<T>{
			origin_type::template allocate<pointer::construct_method::value_ctor>(
				alloc,
				std::forward<Args>(args)...
			)
		};
	}
	/**	Constructs a sh::shared_ptr to own a (default initialized) element T using the supplied allocator.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@return A non-null sh::shared_ptr owning the element T.
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (false == std::is_array_v<T>
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> allocate_shared_for_overwrite(const Alloc& alloc)
	{
		using origin_type = pointer::value_convertible_to_control<
			T,
			Alloc
		>;
		return shared_ptr<T>{
			origin_type::template allocate<pointer::construct_method::default_ctor>(
				alloc
			)
		};
	}

	/**	Constructs via a supplied allocator a sh::shared_ptr to own an array of \p element_count (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::shared_ptr owning the elements T[\p element_count].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> allocate_shared(const Alloc& alloc, const std::size_t element_count)
	{
		using origin_type = pointer::array_of_values_convertible_to_control<
			T,
			Alloc,
			pointer::integral<std::size_t>
		>;
		return shared_ptr<T>{
			origin_type::template allocate_array<pointer::construct_method::value_ctor>(
				alloc,
				pointer::integral<std::size_t>{ element_count }
			)
		};
	}
	/**	Constructs via a supplied allocator a sh::shared_ptr to own an array of (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@return A non-null sh::shared_ptr owning the elements T[N].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> allocate_shared(const Alloc& alloc)
	{
		using origin_type = pointer::array_of_values_convertible_to_control<
			T,
			Alloc,
			std::integral_constant<std::size_t, std::extent_v<T>>
		>;
		return shared_ptr<T>{
			origin_type::template allocate_array<pointer::construct_method::value_ctor>(
				alloc,
				std::integral_constant<std::size_t, std::extent_v<T>>{}
			)
		};
	}
	/**	Constructs via a supplied allocator a sh::shared_ptr to own an array of \p element_count (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@param init_value The value to copy into each allocated element.
	 *	@return A non-null sh::shared_ptr owning the elements T[\p element_count].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> allocate_shared(const Alloc& alloc, const std::size_t element_count, const std::remove_extent_t<T>& init_value)
	{
		using origin_type = pointer::array_of_values_convertible_to_control<
			T,
			Alloc,
			pointer::integral<std::size_t>
		>;
		return shared_ptr<T>{
			origin_type::template allocate_array<pointer::construct_method::value_ctor>(
				alloc,
				pointer::integral<std::size_t>{ element_count },
				init_value
			)
		};
	}
	/**	Constructs via a supplied allocator a sh::shared_ptr to own an array of (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@param init_value The value to copy into each allocated element.
	 *	@return A non-null sh::shared_ptr owning the elements T[N].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> allocate_shared(const Alloc& alloc, const std::remove_extent_t<T>& init_value)
	{
		using origin_type = pointer::array_of_values_convertible_to_control<
			T,
			Alloc,
			std::integral_constant<std::size_t, std::extent_v<T>>
		>;
		return shared_ptr<T>{
			origin_type::template allocate_array<pointer::construct_method::value_ctor>(
				alloc,
				std::integral_constant<std::size_t, std::extent_v<T>>{},
				init_value
			)
		};
	}

	/**	Constructs via a supplied allocator a sh::shared_ptr to own an array of \p element_count (default initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::shared_ptr owning the elements T[\p element_count].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> allocate_shared_for_overwrite(const Alloc& alloc, const std::size_t element_count)
	{
		using origin_type = pointer::array_of_values_convertible_to_control<
			T,
			Alloc,
			pointer::integral<std::size_t>
		>;
		return shared_ptr<T>{
			origin_type::template allocate_array<pointer::construct_method::default_ctor>(
				alloc,
				pointer::integral<std::size_t>{ element_count }
			)
		};
	}
	/**	Constructs via a supplied allocator a sh::shared_ptr to own an array of (default initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@return A non-null sh::shared_ptr owning the elements of T[N].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> allocate_shared_for_overwrite(const Alloc& alloc)
	{
		using origin_type = pointer::array_of_values_convertible_to_control<
			T,
			Alloc,
			std::integral_constant<std::size_t, std::extent_v<T>>
		>;
		return shared_ptr<T>{
			origin_type::template allocate_array<pointer::construct_method::default_ctor>(
				alloc,
				std::integral_constant<std::size_t, std::extent_v<T>>{}
			)
		};
	}

	/**	Constructs a sh::shared_ptr to own a (value initialized) element T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Args The types of arguments passed to T's constructor.
	 *	@param args The arguments passed to the constructor of T.
	 *	@return A non-null sh::shared_ptr owning the element T.
	 */
	template <
		typename T,
		typename... Args
	>
		requires (false == std::is_array_v<T>
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> make_shared(Args&&... args)
	{
		return sh::allocate_shared<T>(pointer::default_allocator<T>{}, std::forward<Args>(args)...);
	}
	/**	Constructs a sh::shared_ptr to own a (default initialized) element T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@return A non-null sh::shared_ptr owning the element T.
	 */
	template <typename T>
		requires (false == std::is_array_v<T>
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> make_shared_for_overwrite()
	{
		return sh::allocate_shared_for_overwrite<T>(pointer::default_allocator<T>{});
	}

	/**	Constructs a sh::shared_ptr to own an array of \p element_count (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::shared_ptr owning the elements T[\p element_count].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> make_shared(const std::size_t element_count)
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared<T>(pointer::default_allocator<element_type>{}, element_count);
	}
	/**	Constructs a sh::shared_ptr to own an array of \p element_count (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@param init_value The value to copy into each allocated element.
	 *	@return A non-null sh::shared_ptr owning the elements T[\p element_count].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> make_shared(const std::size_t element_count, const std::remove_extent_t<T>& init_value)
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared<T>(pointer::default_allocator<element_type>{}, element_count, init_value);
	}
	/**	Constructs a sh::shared_ptr to own an array of (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@return A non-null sh::shared_ptr owning the elements T[N].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> make_shared()
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared<T>(pointer::default_allocator<element_type>{});
	}
	/**	Constructs a sh::shared_ptr to own an array of (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param init_value The value to copy into each allocated element.
	 *	@return A non-null sh::shared_ptr owning the elements T[N].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> make_shared(const std::remove_extent_t<T>& init_value)
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared<T>(pointer::default_allocator<element_type>{}, init_value);
	}

	/**	Constructs a sh::shared_ptr to own an array of \p element_count (default initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::shared_ptr owning the elements T[\p element_count].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> make_shared_for_overwrite(const std::size_t element_count)
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared_for_overwrite<T>(pointer::default_allocator<element_type>{}, element_count);
	}
	/**	Constructs a sh::shared_ptr to own an array of (default initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@return A non-null sh::shared_ptr owning the elements T[N].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) <= pointer::max_alignment)
	shared_ptr<T> make_shared_for_overwrite()
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared_for_overwrite<T>(pointer::default_allocator<element_type>{});
	}

	// shared_ptr -> shared_ptr casts:
	template <
		typename T,
		typename U
	>
		requires is_pointer_interconvertible_v<std::remove_const_t<U>, std::remove_const_t<T>>
	shared_ptr<T> const_pointer_cast(const shared_ptr<U>& from)
	{
		return shared_ptr<T>{ pointer::const_cast_tag{}, from };
	}
	template <
		typename T,
		typename U
	>
		requires is_pointer_interconvertible_v<std::remove_const_t<U>, std::remove_const_t<T>>
	shared_ptr<T> const_pointer_cast(shared_ptr<U>&& from)
	{
		return shared_ptr<T>{ pointer::const_cast_tag{}, std::move(from) };
	}
	template <
		typename T,
		typename U
	>
		requires is_pointer_interconvertible_v<U, T>
	shared_ptr<T> dynamic_pointer_cast(const shared_ptr<U>& from)
	{
		return shared_ptr<T>{ pointer::dynamic_cast_tag{}, from };
	}
	template <
		typename T,
		typename U
	>
		requires is_pointer_interconvertible_v<U, T>
	shared_ptr<T> dynamic_pointer_cast(shared_ptr<U>&& from)
	{
		return shared_ptr<T>{ pointer::dynamic_cast_tag{}, std::move(from) };
	}
	template <
		typename T,
		typename U
	>
		requires is_pointer_interconvertible_v<U, T>
	shared_ptr<T> static_pointer_cast(const shared_ptr<U>& from)
	{
		return shared_ptr<T>{ pointer::static_cast_tag{}, from };
	}
	template <
		typename T,
		typename U
	>
		requires is_pointer_interconvertible_v<U, T>
	shared_ptr<T> static_pointer_cast(shared_ptr<U>&& from)
	{
		return shared_ptr<T>{ pointer::static_cast_tag{}, std::move(from) };
	}
	template <typename T, typename U>
	shared_ptr<T> reinterpret_pointer_cast(const shared_ptr<U>& from)
	{
		return shared_ptr<T>{ pointer::reinterpret_cast_tag{}, from };
	}
	template <typename T, typename U>
	shared_ptr<T> reinterpret_pointer_cast(shared_ptr<U>&& from)
	{
		return shared_ptr<T>{ pointer::reinterpret_cast_tag{}, std::move(from) };
	}

	template <typename Deleter, typename T>
	constexpr Deleter* get_deleter(const shared_ptr<T>& ptr) noexcept
	{
		pointer::convertible_control* const ctrl = pointer::convert_value_to_control(ptr.get());
		return ctrl != nullptr && ctrl->get_operations().m_get_deleter != nullptr
			? static_cast<Deleter*>(ctrl->get_operations().m_get_deleter(ctrl))
			: nullptr;
	}

	template <typename T, typename U>
	bool operator==(const shared_ptr<T>& lhs, const shared_ptr<U>& rhs) noexcept
	{
		return lhs.get() == rhs.get();
	}
	template <typename T>
	bool operator==(const shared_ptr<T>& lhs, const std::nullptr_t) noexcept
	{
		return lhs.get() == nullptr;
	}
	template <typename U>
	bool operator==(const std::nullptr_t, const shared_ptr<U>& rhs) noexcept
	{
		return nullptr == rhs.get();
	}
	template <typename T, typename U>
	std::strong_ordering operator<=>(const shared_ptr<T>& lhs, const shared_ptr<U>& rhs) noexcept
	{
		return lhs.get() <=> rhs.get();
	}
	template <typename T>
	std::strong_ordering operator<=>(const shared_ptr<T>& lhs, const std::nullptr_t) noexcept
	{
		return lhs.get() <=> nullptr;
	}
	template <typename U>
	std::strong_ordering operator<=>(const std::nullptr_t, const shared_ptr<U>& rhs) noexcept
	{
		return nullptr <=> rhs.get();
	}
	template <typename T, typename U, typename V>
	std::basic_ostream<U, V>& operator<<(std::basic_ostream<U, V>& ostr, const shared_ptr<T>& ptr)
	{
		ostr << ptr.get();
		return ostr;
	}
	template <typename T>
	void swap(shared_ptr<T>& lhs, shared_ptr<T>& rhs) noexcept
	{
		lhs.swap(rhs);
	}
	template <typename T>
	void swap(weak_ptr<T>& lhs, weak_ptr<T>& rhs) noexcept
	{
		lhs.swap(rhs);
	}

	template <typename T = void>
	struct owner_less;

	template <typename T>
	struct owner_less<shared_ptr<T>>
	{
		bool operator()(const shared_ptr<T>& lhs, const shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const shared_ptr<T>& lhs, const weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const weak_ptr<T>& lhs, const shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const shared_ptr<T>& lhs, const wide_shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_shared_ptr<T>& lhs, const shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const shared_ptr<T>& lhs, const wide_weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_weak_ptr<T>& lhs, const shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
	};
	template <typename T>
	struct owner_less<weak_ptr<T>>
	{
		bool operator()(const weak_ptr<T>& lhs, const weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const weak_ptr<T>& lhs, const shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const shared_ptr<T>& lhs, const weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const weak_ptr<T>& lhs, const wide_shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_shared_ptr<T>& lhs, const weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const weak_ptr<T>& lhs, const wide_weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_weak_ptr<T>& lhs, const weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
	};
	template <>
	struct owner_less<void>
	{
		template <typename T, typename U>
		bool operator()(const T& lhs, const U& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
	};

} // namespace sh

namespace std
{
	template <typename T>
	struct hash<sh::shared_ptr<T>> : std::hash<std::remove_extent_t<T>*>
	{
		constexpr decltype(auto) operator()(const sh::shared_ptr<T>& ptr)
			noexcept(noexcept(std::hash<std::remove_extent_t<T>*>::operator()(ptr.get())))
		{
			return this->std::hash<std::remove_extent_t<T>*>::operator()(ptr.get());
		}
	};
} // namespace std

#endif

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

#ifndef INC_SH__WIDE_SHARED_PTR_HPP
#define INC_SH__WIDE_SHARED_PTR_HPP

/**	@file
 *	This file declares sh::wide_shared_ptr, sh::wide_weak_ptr, and related
 *	functions mirroring those in <memory>:
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
 *	These "wide" varieties of sh::shared_ptr/weak_ptr are more fully featured
 *	and aim to be comparable to std::shared_ptr and std::weak_ptr. To do so,
 *	they give up the advantage of being only a single pointer in width.
 */

#include "shared_ptr.hpp"
// pointer_traits.hpp & pointer.hpp included by shared_ptr.hpp

namespace sh::pointer
{
	struct unknown_count final : std::integral_constant<std::size_t, ~std::size_t{ 0 }>
	{ };

	template <typename T>
	struct external_value_deleter final
	{
		using element_type = std::remove_extent_t<T>;

		template <typename Count, typename Alloc>
		void operator()(element_type* const ptr, const Count& element_count, Alloc& alloc) const noexcept
		{
			using allocator_traits = std::allocator_traits<Alloc>;
			// Destroy from right-to-left:
			for (auto destroy_index = element_count(); destroy_index > 0; )
			{
				--destroy_index;
				allocator_traits::destroy(alloc, ptr + destroy_index);
			}
			allocator_traits::deallocate(alloc, ptr, element_count());
		}
	};

	/**	Allocate a control block associated with an separately allocated value (or array of values).
	 *	@tparam T The value type.
	 *	@tparam Count The element count type.
	 *	@tparam Deleter The deleter type.
	 *	@tparam Alloc The allocator type (use for the control block).
	 */
	template <
		typename T,
		typename Count,
		typename Deleter,
		typename Alloc
	>
	class external_value_control final
	{
	private:
		using element_type = std::remove_extent_t<T>;
		using count_type = Count;
		using deleter_type = Deleter;
		using allocator_traits = std::allocator_traits<Alloc>;
		using value_allocator_traits = typename allocator_traits::template rebind_traits<element_type>;
		using value_allocator = typename value_allocator_traits::allocator_type;

		struct storage_type final : public control
		{
			constexpr storage_type(element_type* const value, const count_type& element_count, deleter_type&& deleter, const Alloc& allocator) noexcept
				: control{ control::shared_one, external_value_control::operations() }
				, m_value{ value }
				, m_element_count{ element_count }
				, m_deleter{ std::move(deleter) }
				, m_alloc{ allocator }
			{
				static_assert(std::is_nothrow_constructible_v<
						control,
						decltype(control::shared_one),
						decltype(external_value_control::operations())>,
					"Exceptions from control constructor aren't expected.");
				static_assert(std::is_nothrow_move_constructible_v<count_type>,
					"Exceptions from count_type constructor aren't expected.");
				static_assert(std::is_nothrow_move_constructible_v<deleter_type>,
					"Exceptions from delete_type constructor aren't expected.");
				static_assert(std::is_nothrow_constructible_v<value_allocator, const Alloc&>,
					"Exceptions from value_allocator contructor aren't expected.");
			}

			/**	The value to pass to deleter_type::operator(). May not match the value in a given wide_shared_ptr due to casting or aliasing. May be nullptr.
			 */
			element_type* const m_value;

			/**	The element count. May be unknown_count.
			 */
			SH_POINTER_NO_UNIQUE_ADDRESS const count_type m_element_count;

			/**	Storage for deleter. Callable with element_type* to destroy & deallocate a value.
			 */
			SH_POINTER_NO_UNIQUE_ADDRESS deleter_type m_deleter;

			/**	Allocator used for constructing and destroying value.
			 */
			SH_POINTER_NO_UNIQUE_ADDRESS value_allocator m_alloc;
		};

		using storage_allocator_traits = typename std::allocator_traits<Alloc>::template rebind_traits<storage_type>;
		using storage_allocator = typename storage_allocator_traits::allocator_type;

#if SH_POINTER_DEBUG_SHARED_PTR
		/**	For debug validation, return a pointer to a static string identifying this class.
		 *	@return A pointer to a static string identifying this class.
		 */
		static const char* origin() noexcept
		{
			static const char* const instance = typeid(external_value_control).name();
			return instance;
		}
#endif // SH_POINTER_DEBUG_SHARED_PTR

		/**	Return a reference to a static control_operations structure.
		 *	@return A reference to a static control_operations structure.
		 */
		static const control_operations& operations() noexcept
		{
			const static control_operations instance{
#ifdef __cpp_designated_initializers
				.m_destruct =
#endif // __cpp_designated_initializers
				/* destruct */
				[](control* const ctrl) noexcept -> void
				{
#if SH_POINTER_DEBUG_SHARED_PTR
					ctrl->validate_destruct(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR
					storage_type* const storage = static_cast<storage_type*>(ctrl);
					if constexpr (std::is_invocable_v<deleter_type, element_type*, count_type, value_allocator&>)
					{
						// allocate_shared et al need extra data that we already store. Pass it along to
						// what's presumably external_value_deleter so that it doesn't need to be stored
						// redundantly.
						storage->m_deleter(storage->m_value, storage->m_element_count, storage->m_alloc);
					}
					else
					{
						storage->m_deleter(storage->m_value);
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
					storage_type* const storage = static_cast<storage_type*>(ctrl);
					storage_allocator storage_allocator{ std::move(storage->m_alloc) };
					storage_allocator_traits::destroy(storage_allocator, storage);
					storage_allocator_traits::deallocate(storage_allocator, storage, 1);
				},
#ifdef __cpp_designated_initializers
				.m_get_deleter =
#endif // __cpp_designated_initializers
				/* get_deleter */
				[](control* const ctrl) noexcept -> void*
				{
#if SH_POINTER_DEBUG_SHARED_PTR
					ctrl->validate(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR
					storage_type* const storage = static_cast<storage_type*>(ctrl);
					return &storage->m_deleter;
				},
#if SH_POINTER_DEBUG_SHARED_PTR
#ifdef __cpp_designated_initializers
				.m_get_element_count =
#endif // __cpp_designated_initializers
				/* get_element_count */
				[](const control* const ctrl) noexcept -> std::size_t
				{
					ctrl->validate(origin());
					const storage_type* const storage = static_cast<const storage_type*>(ctrl);
					return storage->m_element_count();
				},
#endif // SH_POINTER_DEBUG_SHARED_PTR
			};
			return instance;
		}

	public:
		/**	Allocate a control block to associate with a value (or array of values) of type element_type addressed by a given pointer.
		 *	@throw May throw on allocate or construct failure.
		 *	@param value The pointer of which to assume control.
		 *	@param element_count The number of elements pointed to by value. May be unknown_count.
		 *	@param deleter The deleter to store and to later pass value to operator().
		 *	@param alloc The allocator which will allocate and deallocate the control block.
		 *	@return The pointer to the control.
		 */
		static control* allocate_control(element_type* const value, const count_type& element_count, Deleter&& deleter, const Alloc& alloc)
		{
			storage_allocator storage_alloc{ alloc };
			storage_type* const storage = storage_allocator_traits::allocate(storage_alloc, 1);

			static_assert(std::is_nothrow_constructible_v<storage_type, element_type*, count_type, Deleter&&, Alloc&&>,
				"storage_type constructor expected to be noexcept.");
			storage_allocator_traits::construct(storage_alloc, storage, value, element_count, std::move(deleter), alloc);
#if SH_POINTER_DEBUG_SHARED_PTR
			storage->validate_set_origin(origin());
#endif // SH_POINTER_DEBUG_SHARED_PTR
			return storage;
		}

		/**	Allocate a control block to associate with a value of type element_type addressed by a given pointer.
		 *	@throw May throw on allocate or construct failure.
		 *	@param deleter The deleter to store and to later pass value to operator().
		 *	@param alloc The allocator which will allocate and deallocate the control block.
		 *	@param args The arguments to pass to value's constructor (element_type::element_type).
		 *	@return The pointer to the control.
		 */
		template <
			construct_method Construct,
			typename... Args
		>
			requires (false == std::is_array_v<T>)
		static std::pair<control*, element_type*> allocate(Deleter&& deleter, const Alloc& alloc, Args&&... args)
		{
			static constexpr std::integral_constant<std::size_t, 1> element_count;
			value_allocator value_alloc{ alloc };
			element_type* const value = value_allocator_traits::allocate(value_alloc, element_count());

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
					std::uninitialized_default_construct_n(value, element_count());
				}
			};

			if constexpr (noexcept(construct_value(value_alloc, value)))
			{
				construct_value(value_alloc, value);
			}
			else
			{
				try
				{
					construct_value(value_alloc, value);
				}
				catch (...)
				{
					value_allocator_traits::deallocate(value_alloc, value, element_count());
					throw;
				}
			}

			try
			{
				return std::make_pair(
					allocate_control(value, element_count, std::move(deleter), alloc),
					value
				);
			}
			catch (...)
			{
				value_allocator_traits::destroy(value_alloc, value);
				value_allocator_traits::deallocate(value_alloc, value, element_count());
				throw;
			}
		}

		/**	Allocate a control block to associate with an array of values of type element_type addressed by a given pointer.
		 *	@throw May throw on allocate or construct failure.
		 *	@param element_count The number of elements pointed to by value. May be unknown_count.
		 *	@param deleter The deleter to store and to later pass value to operator().
		 *	@param alloc The allocator which will allocate and deallocate the control block.
		 *	@param args The arguments to pass to value's constructor (element_type::element_type).
		 *	@return The pointer to the control.
		 */
		template <
			construct_method Construct,
			typename... Args
		>
			requires std::is_array_v<T>
		static std::pair<control*, element_type*> allocate_array(const count_type& element_count, Deleter&& deleter, const Alloc& alloc, Args&&... args)
		{
			value_allocator value_alloc{ alloc };
			element_type* const values = value_allocator_traits::allocate(value_alloc, element_count());

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
			const auto deallocate_values = [&element_count, &construct_index](value_allocator& alloc, element_type* const values)
				noexcept -> void
			{
				// Destroy [0, construct_index) from right-to-left.
				for (auto destroy_index{ construct_index }; destroy_index > 0; )
				{
					--destroy_index;
					value_allocator_traits::destroy(alloc, values + destroy_index);
				}
				value_allocator_traits::deallocate(alloc, values, element_count());
			};
			if constexpr (noexcept(construct_values(value_alloc, values)))
			{
				construct_values(value_alloc, values);
			}
			else
			{
				try
				{
					construct_values(value_alloc, values);
				}
				catch (...)
				{
					deallocate_values(value_alloc, values);
					throw;
				}
			}

			try
			{
				return std::make_pair(
					allocate_control(values, element_count, std::move(deleter), alloc),
					values
				);
			}
			catch (...)
			{
				deallocate_values(value_alloc, values);
				throw;
			}
		}
	};

	/**	Concept for deleters acceptable to sh::wide_shared_ptr.
	 *	@tparam Deleter The deleter type.
	 *	@tparam ElementType The element type of the sh::wide_shared_ptr.
	 *	@detail Deleter(element_type*) shouldn't throw exceptions, but doesn't need to be marked noexcept.
	 */
	template <typename Deleter, typename ElementType>
	concept is_deleter = std::is_invocable_v<Deleter, ElementType*>;

} // namespace sh::pointer

namespace sh
{
	/**	Exception thrown by sh::wide_shared_ptr::collapse if the pointer isn't suitable for sh::shared_ptr.
	 */
	class bad_collapse : public std::exception
	{
		using std::exception::exception;
	};

	template <typename T> class wide_weak_ptr;

	/**	A reference counting owner of allocated data similar to std::shared_ptr, compatible with sh::shared_ptr.
	 */
	template <typename T>
	class wide_shared_ptr
	{
	public:
		using element_type = std::remove_extent_t<T>;
		using weak_type = wide_weak_ptr<T>;

		constexpr wide_shared_ptr() noexcept
			: m_value{ nullptr }
			, m_ctrl{ nullptr }
		{ }
		constexpr wide_shared_ptr(std::nullptr_t) noexcept
			: m_value{ nullptr }
			, m_ctrl{ nullptr }
		{ }
		explicit wide_shared_ptr(element_type* const ptr) noexcept
			: m_value{ ptr }
			, m_ctrl{
				pointer::external_value_control<
					T,
					pointer::unknown_count,
					std::default_delete<T>,
					std::allocator<void>
				>::allocate_control(
					ptr,
					pointer::unknown_count{},
					std::default_delete<T>{},
					std::allocator<void>{}
				)
			}
		{
			// Seems silly to allocate_control a control block when ptr == nullptr, but
			// that's what libstdc++ does here.
		}
		template <typename Deleter>
			requires pointer::is_deleter<Deleter, element_type>
		wide_shared_ptr(element_type* const ptr, Deleter d) noexcept
			: m_value{ ptr }
			, m_ctrl{
				pointer::external_value_control<
					T,
					pointer::unknown_count,
					Deleter,
					std::allocator<void>
				>::allocate_control(
					ptr,
					pointer::unknown_count{},
					std::move(d),
					std::allocator<void>{}
				)
			}
		{ }
		template <typename Deleter>
			requires pointer::is_deleter<Deleter, element_type>
		wide_shared_ptr(const std::nullptr_t, Deleter d) noexcept
			: m_value{ nullptr }
			, m_ctrl{
				pointer::external_value_control<
					T,
					pointer::unknown_count,
					Deleter,
					std::allocator<void>
				>::allocate_control(
					nullptr,
					pointer::unknown_count{},
					std::move(d),
					std::allocator<void>{}
				)
			}
		{ }
		template <typename Deleter, typename Alloc>
			requires pointer::is_deleter<Deleter, element_type>
		wide_shared_ptr(element_type* const ptr, Deleter d, Alloc alloc) noexcept
			: m_value{ ptr }
			, m_ctrl{
				pointer::external_value_control<
					T,
					pointer::unknown_count,
					Deleter,
					Alloc
				>::allocate_control(
					ptr,
					pointer::unknown_count{},
					std::move(d),
					std::move(alloc)
				)
			}
		{ }
		template <typename Deleter, typename Alloc>
			requires pointer::is_deleter<Deleter, element_type>
		wide_shared_ptr(const std::nullptr_t, Deleter d, Alloc alloc) noexcept
			: m_value{ nullptr }
			, m_ctrl{
				pointer::external_value_control<
					T,
					pointer::unknown_count,
					Deleter,
					Alloc
				>::allocate_control(
					nullptr,
					pointer::unknown_count{},
					std::move(d),
					std::move(alloc)
				)
			}
		{ }
		template <typename Y>
		wide_shared_ptr(const wide_shared_ptr<Y>& other, element_type* const ptr) noexcept
			: m_value{ ptr }
			, m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		template <typename Y>
		wide_shared_ptr(wide_shared_ptr<Y>&& other, element_type* const ptr) noexcept
			: m_value{ ptr }
			, m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{
			other.m_value = nullptr;
		}
		template <typename Y>
		wide_shared_ptr(const shared_ptr<Y>& other, element_type* const ptr) noexcept
			: m_value{ ptr }
			, m_ctrl{ pointer::convert_value_to_control(other.m_value) }
		{
			increment(m_ctrl);
		}
		template <typename Y>
		wide_shared_ptr(shared_ptr<Y>&& other, element_type* const ptr) noexcept
			: m_value{ ptr }
			, m_ctrl{ pointer::convert_value_to_control(std::exchange(other.m_value, nullptr)) }
		{ }
		wide_shared_ptr(const wide_shared_ptr<T>& other) noexcept
			: m_value{ other.m_value }
			, m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		wide_shared_ptr(wide_shared_ptr<T>&& other) noexcept
			: m_value{ std::exchange(other.m_value, nullptr) }
			, m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{ }
		wide_shared_ptr(const shared_ptr<T>& other) noexcept
			: m_value{ other.m_value }
			, m_ctrl{ pointer::convert_value_to_control(other.m_value) }
		{
			increment(m_ctrl);
		}
		wide_shared_ptr(shared_ptr<T>&& other) noexcept
			: m_value{ other.m_value }
			, m_ctrl{ pointer::convert_value_to_control(std::exchange(other.m_value, nullptr)) }
		{ }
		~wide_shared_ptr()
		{
			decrement(m_ctrl);
		}

		wide_shared_ptr& operator=(const wide_shared_ptr<T>& other) noexcept
		{
			increment(other.m_ctrl);
			decrement(m_ctrl);
			m_value = other.m_value;
			m_ctrl = other.m_ctrl;
			return *this;
		}
		wide_shared_ptr& operator=(wide_shared_ptr<T>&& other) noexcept
		{
			if (this != &other)
			{
				pointer::control* const ctrl = std::exchange(other.m_ctrl, nullptr);
				element_type* value = std::exchange(other.m_value, nullptr);
				this->decrement(m_ctrl);
				m_value = value;
				m_ctrl = ctrl;
			}
			return *this;
		}

		void reset() noexcept
		{
			m_value = nullptr;
			decrement(std::exchange(m_ctrl, nullptr));
		}
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		void reset(U* const ptr) noexcept
		{
			decrement(m_ctrl);
			m_value = ptr;
			m_ctrl = pointer::external_value_control<
				T,
				pointer::unknown_count,
				std::default_delete<T>,
				std::allocator<void>
			>::allocate_control(
				ptr,
				pointer::unknown_count{},
				std::default_delete<T>{},
				std::allocator<void>{}
			);
		}
		template <
			typename U,
			typename Deleter
		>
			requires (std::is_convertible_v<U*, T*> && pointer::is_deleter<Deleter, element_type>)
		void reset(U* const ptr, Deleter d) noexcept
		{
			decrement(m_ctrl);
			m_value = ptr;
			m_ctrl = pointer::external_value_control<
				T,
				pointer::unknown_count,
				Deleter,
				std::allocator<void>
			>::allocate_control(
				ptr,
				pointer::unknown_count{},
				std::move(d),
				std::allocator<void>{}
			);
		}
		template <
			typename U,
			typename Deleter,
			typename Alloc
		>
			requires (std::is_convertible_v<U*, T*> && pointer::is_deleter<Deleter, element_type>)
		void reset(U* const ptr, Deleter d, Alloc alloc) noexcept
		{
			decrement(m_ctrl);
			m_value = ptr;
			m_ctrl = pointer::external_value_control<
				T,
				pointer::unknown_count,
				Deleter,
				Alloc
			>::allocate_control(
				ptr,
				pointer::unknown_count{},
				std::move(d),
				std::move(alloc)
			);
		}
		void swap(wide_shared_ptr& other) noexcept
		{
			using std::swap;
			swap(m_value, other.m_value);
			swap(m_ctrl, other.m_ctrl);
		}

		element_type* get() const noexcept
		{
			return m_value;
		}
		element_type& operator*() const noexcept
		{
			SH_POINTER_ASSERT(m_value != nullptr, "Dereferencing nullptr wide_shared_ptr.");
			return *m_value;
		}
		element_type* operator->() const noexcept
		{
			SH_POINTER_ASSERT(m_value != nullptr, "Dereferencing nullptr wide_shared_ptr.");
			return m_value;
		}
		element_type& operator[](const std::ptrdiff_t idx) const noexcept
		{
			SH_POINTER_ASSERT(idx >= 0, "Negative index given to wide_shared_ptr::operator[] has undefined results.");
			SH_POINTER_ASSERT(m_value != nullptr, "Dereferencing nullptr wide_shared_ptr in operator[].");
			if constexpr (std::is_array_v<T>)
			{
				SH_POINTER_ASSERT(m_ctrl == nullptr
					|| m_ctrl->get_operations().m_get_element_count == nullptr
					|| std::size_t(idx) < m_ctrl->get_operations().m_get_element_count(m_ctrl)
					, "Index given to wide_shared_ptr::operator[] is out of bounds."
				);
			}
			else
			{
				SH_POINTER_ASSERT(idx == 0, "Index given to wide_shared_ptr::operator[] is out of bounds.");
			}
			return m_value[idx];
		}
		long use_count() const noexcept
		{
			return m_ctrl ? long{ m_ctrl->get_shared_count() } : 0L;
		}
		explicit constexpr operator bool() const noexcept
		{
			return m_value != nullptr;
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

		// collapse back into shared_ptr:
		shared_ptr<T> collapse() const &
		{
			if (pointer::convert_value_to_control(m_value) != m_ctrl)
			{
				throw bad_collapse{};
			}
			// Increment shared_one reference to give to shared_ptr:
			increment(m_ctrl);
			return shared_ptr<T>{ m_value };
		}
		shared_ptr<T> collapse() &&
		{
			if (pointer::convert_value_to_control(m_value) != m_ctrl)
			{
				throw bad_collapse{};
			}
			m_ctrl = nullptr;
			// Result will inherit our shared_one reference:
			return shared_ptr<T>{ std::exchange(m_value, nullptr) };
		}

		// implicit conversion
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_shared_ptr(const wide_shared_ptr<U>& other) noexcept
			: m_value{ other.m_value }
			, m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_shared_ptr(wide_shared_ptr<U>&& other) noexcept
			: m_value{ std::exchange(other.m_value, nullptr) }
			, m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{ }
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_shared_ptr(const shared_ptr<U>& other) noexcept
			: m_value{ other.m_value }
			, m_ctrl{ pointer::convert_value_to_control(other.m_value) }
		{
			increment(m_ctrl);
		}
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_shared_ptr(shared_ptr<U>&& other) noexcept
			: m_value{ other.m_value }
			, m_ctrl{ pointer::convert_value_to_control(std::exchange(other.m_value, nullptr)) }
		{ }

		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_shared_ptr& operator=(const wide_shared_ptr<U>& other) noexcept
		{
			increment(other.m_ctrl);
			decrement(m_ctrl);
			m_value = other.m_value;
			m_ctrl = other.m_ctrl;
			return *this;
		}
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_shared_ptr& operator=(wide_shared_ptr<U>&& other) noexcept
		{
			pointer::control* const ctrl = std::exchange(other.m_ctrl, nullptr);
			element_type* const value = std::exchange(other.m_value, nullptr);
			this->decrement(m_ctrl);
			m_value = value;
			m_ctrl = ctrl;
			return *this;
		}

		// const_cast
		template <typename U>
			requires std::is_convertible_v<const U*, const T*>
		wide_shared_ptr(const pointer::const_cast_tag&, const wide_shared_ptr<U>& other) noexcept
			: m_value{ const_cast<element_type*>(other.get()) }
			, m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		template <typename U>
			requires std::is_convertible_v<const U*, const T*>
		wide_shared_ptr(const pointer::const_cast_tag&, wide_shared_ptr<U>&& other) noexcept
			: m_value{ const_cast<element_type*>(std::exchange(other.m_value, nullptr)) }
			, m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{ }

		// dynamic_cast
		template <typename U>
		wide_shared_ptr(const pointer::dynamic_cast_tag&, const wide_shared_ptr<U>& other) noexcept
			: m_value{ dynamic_cast<element_type*>(other.get()) }
			, m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		template <typename U>
		wide_shared_ptr(const pointer::dynamic_cast_tag&, wide_shared_ptr<U>&& other) noexcept
			: m_value{ dynamic_cast<element_type*>(std::exchange(other.m_value, nullptr)) }
			, m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{ }
		template <typename U>
		wide_shared_ptr(const pointer::dynamic_cast_tag&, const shared_ptr<U>& other) noexcept
			: m_value{ dynamic_cast<element_type*>(other.get()) }
			, m_ctrl{ pointer::convert_value_to_control(other.get()) }
		{
			increment(m_ctrl);
		}
		template <typename U>
		wide_shared_ptr(const pointer::dynamic_cast_tag&, shared_ptr<U>&& other) noexcept
			: m_value{ dynamic_cast<element_type*>(other.m_value) }
			, m_ctrl{ pointer::convert_value_to_control(std::exchange(other.m_value, nullptr)) }
		{ }

		// static_cast
		template <typename U>
		wide_shared_ptr(const pointer::static_cast_tag&, const wide_shared_ptr<U>& other) noexcept
			: m_value{ static_cast<element_type*>(other.get()) }
			, m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		template <typename U>
		wide_shared_ptr(const pointer::static_cast_tag&, wide_shared_ptr<U>&& other) noexcept
			: m_value{ static_cast<element_type*>(std::exchange(other.m_value, nullptr)) }
			, m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{ }
		template <typename U>
		wide_shared_ptr(const pointer::static_cast_tag&, const shared_ptr<U>& other) noexcept
			: m_value{ static_cast<element_type*>(other.get()) }
			, m_ctrl{ pointer::convert_value_to_control(other.get()) }
		{
			increment(m_ctrl);
		}
		template <typename U>
		wide_shared_ptr(const pointer::static_cast_tag&, shared_ptr<U>&& other) noexcept
			: m_value{ static_cast<element_type*>(other.m_value) }
			, m_ctrl{ pointer::convert_value_to_control(std::exchange(other.m_value, nullptr)) }
		{ }

		// reinterpret_cast
		template <typename U>
		wide_shared_ptr(const pointer::reinterpret_cast_tag&, const wide_shared_ptr<U>& other) noexcept
			: m_value{ reinterpret_cast<element_type*>(other.get()) }
			, m_ctrl{ other.m_ctrl }
		{
			increment(m_ctrl);
		}
		template <typename U>
		wide_shared_ptr(const pointer::reinterpret_cast_tag&, wide_shared_ptr<U>&& other) noexcept
			: m_value{ reinterpret_cast<element_type*>(std::exchange(other.m_value, nullptr)) }
			, m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
		{ }

	private:
		template <typename U> friend class wide_shared_ptr;
		template <typename U> friend class wide_weak_ptr;
		template <typename U> friend class shared_ptr;
		template <typename U> friend class weak_ptr;

		template <typename U, typename Alloc, typename... Args>
			requires (false == std::is_array_v<U>
				&& alignof(U) > pointer::max_alignment)
		friend wide_shared_ptr<U> allocate_shared(const Alloc& alloc, Args&&... args);

		template <typename U, typename Alloc>
			requires (false == std::is_array_v<U>
				&& alignof(U) > pointer::max_alignment)
		friend wide_shared_ptr<U> allocate_shared_for_overwrite(const Alloc& alloc);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> == 0
				&& alignof(U) > pointer::max_alignment)
		friend wide_shared_ptr<U> allocate_shared(const Alloc& alloc, std::size_t element_count);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> > 0
				&& alignof(U) > pointer::max_alignment)
		friend wide_shared_ptr<U> allocate_shared(const Alloc& alloc);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> == 0
				&& alignof(U) > pointer::max_alignment)
		friend wide_shared_ptr<U> allocate_shared(const Alloc& alloc, std::size_t element_count, const std::remove_extent_t<U>& init_value);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> > 0
				&& alignof(U) > pointer::max_alignment)
		friend wide_shared_ptr<U> allocate_shared(const Alloc& alloc, const std::remove_extent_t<U>& init_value);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> == 0
				&& alignof(U) > pointer::max_alignment)
		friend wide_shared_ptr<U> allocate_shared_for_overwrite(const Alloc& alloc, std::size_t element_count);

		template <typename U, typename Alloc>
			requires (std::is_array_v<U>
				&& std::extent_v<U> > 0
				&& alignof(U) > pointer::max_alignment)
		friend wide_shared_ptr<U> allocate_shared_for_overwrite(const Alloc& alloc);

		template <typename Deleter, typename U>
		friend Deleter* get_deleter(const wide_shared_ptr<U>& ptr) noexcept;

		static void increment(pointer::control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->shared_inc();
			}
		}
		static void decrement(pointer::control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->shared_dec();
			}
		}

		wide_shared_ptr(pointer::control* const control_with_one_ref, element_type* const value) noexcept
			: m_value{ value }
			, m_ctrl{ control_with_one_ref }
		{ }

		/** Pointer to the owned value or nullptr. May be nullptr if empty or explicitly constructed from a pointer equal to nullptr.
		 */
		element_type* m_value;
		/** Pointer to the control block or nullptr. May be nullptr if uncontrolled (aliasing an empty shared_ptr) or m_value is nullptr.
		 */
		pointer::control* m_ctrl;
	};

	/**	A reference counting weak owner of allocated data similar to std::weak_ptr, compatible with sh::wide_shared_ptr.
	 */
	template <typename T>
	class wide_weak_ptr
	{
	public:
		using element_type = std::remove_extent_t<T>;

		constexpr wide_weak_ptr() noexcept
			: m_ctrl{ nullptr }
			, m_value{ nullptr }
		{ }
		wide_weak_ptr(const wide_weak_ptr<T>& other) noexcept
			: m_ctrl{ other.m_ctrl }
			, m_value{ other.m_value }
		{
			increment(m_ctrl);
		}
		wide_weak_ptr(wide_weak_ptr<T>&& other) noexcept
			: m_ctrl{ std::exchange(other.m_ctrl, nullptr) }
			, m_value{ std::exchange(other.m_value, nullptr) }
		{ }
		wide_weak_ptr(const wide_shared_ptr<T>& other) noexcept
			: m_ctrl{ other.m_ctrl }
			, m_value{ other.m_value }
		{
			increment(m_ctrl);
		}
		~wide_weak_ptr()
		{
			decrement(m_ctrl);
		}

		wide_weak_ptr& operator=(const wide_weak_ptr<T>& other) noexcept
		{
			increment(other.m_ctrl);
			decrement(m_ctrl);
			m_ctrl = other.m_ctrl;
			m_value = other.m_value;
			return *this;
		}
		wide_weak_ptr& operator=(wide_weak_ptr<T>&& other) noexcept
		{
			pointer::control* const ctrl = std::exchange(other.m_ctrl, nullptr);
			element_type* const value = std::exchange(other.m_value, nullptr);
			this->decrement(m_ctrl);
			m_ctrl = ctrl;
			m_value = value;
			return *this;
		}
		wide_weak_ptr& operator=(const wide_shared_ptr<T>& other) noexcept
		{
			pointer::control* const ctrl = other.m_ctrl;
			element_type* const value = other.m_value;
			increment(ctrl);
			decrement(m_ctrl);
			m_ctrl = ctrl;
			m_value = value;
			return *this;
		}

		void reset() noexcept
		{
			decrement(std::exchange(m_ctrl, nullptr));
		}
		void swap(wide_weak_ptr& other) noexcept
		{
			using std::swap;
			swap(m_ctrl, other.m_ctrl);
			swap(m_value, other.m_value);
		}
		long use_count() const noexcept
		{
			return m_ctrl ? long{ m_ctrl->get_shared_count() } : 0L;
		}
		bool expired() const noexcept
		{
			return m_ctrl == nullptr || m_ctrl->get_shared_count() == 0;
		}
		wide_shared_ptr<T> lock() const noexcept
		{
			return m_ctrl
				&& m_ctrl->shared_inc_if_nonzero() == pointer::control::shared_inc_if_nonzero_result::added_shared_inc
				? wide_shared_ptr<T>{ m_ctrl, m_value }
				: wide_shared_ptr<T>{ nullptr };
		}
		template <typename Y>
		bool owner_before(const shared_ptr<Y>& other) const noexcept
		{
			return m_ctrl < pointer::convert_value_to_control(other.get());
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

		// implicit conversion from wide_weak_ptr
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_weak_ptr(const wide_weak_ptr<U>& other) noexcept
		{
			if constexpr (is_static_cast_inert_v<element_type*, std::remove_extent_t<U>*>)
			{
				m_ctrl = other.m_ctrl;
				m_value = other.m_value;
				increment(m_ctrl);
			}
			else
			{
				wide_shared_ptr<U> locked = other.lock();
				SH_POINTER_ASSERT(locked.m_ctrl == nullptr || locked.m_ctrl == other.m_ctrl,
					"Result of wide_weak_ptr::lock points to unexpected control block.");
				m_ctrl = std::exchange(locked.m_ctrl, nullptr);
				m_value = std::exchange(locked.m_value, nullptr);
				if (m_ctrl)
				{
					m_ctrl->value_dec_for_shared_to_weak();
				}
			}
		}
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_weak_ptr(wide_weak_ptr<U>&& other) noexcept
		{
			if constexpr (is_static_cast_inert_v<element_type*, std::remove_extent_t<U>*>)
			{
				m_ctrl = std::exchange(other.m_ctrl, nullptr);
				U* const value = std::exchange(other.m_value, nullptr);
				m_value = value;
			}
			else
			{
				const wide_shared_ptr<U> locked = other.lock();
				SH_POINTER_ASSERT(locked.m_ctrl == nullptr || locked.m_ctrl == other.m_ctrl,
					"Result of wide_weak_ptr::lock points to unexpected control block.");
				m_ctrl = locked.m_ctrl;
				m_value = locked.m_value;
				if (m_ctrl)
				{
					// Take over reference from other.
					other.m_ctrl = nullptr;
					other.m_value = nullptr;
				}
				else
				{
					// Reset other to drop its reference as one may expect from "move".
					other.reset();
				}
			}
		}
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_weak_ptr& operator=(const wide_weak_ptr<U>& other) noexcept
		{
			if constexpr (is_static_cast_inert_v<element_type*, std::remove_extent_t<U>*>)
			{
				// Increment before decrement in case this & other reference the same control.
				increment(other.m_ctrl);
				decrement(m_ctrl);
				m_ctrl = other.m_ctrl;
				m_value = other.m_value;
			}
			else
			{
				// Decrement this.
				decrement(m_ctrl);
				// Possibly increment other.
				wide_shared_ptr<U> locked = other.lock();
				SH_POINTER_ASSERT(locked.m_ctrl == nullptr || locked.m_ctrl == other.m_ctrl,
					"Result of wide_weak_ptr::lock points to unexpected control block.");
				m_ctrl = std::exchange(locked.m_ctrl, nullptr);
				m_value = std::exchange(locked.m_value, nullptr);
				// Possibly accept reference from other via locked.
				if (m_ctrl)
				{
					m_ctrl->value_dec_for_shared_to_weak();
				}
			}
			return *this;
		}
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_weak_ptr& operator=(wide_weak_ptr<U>&& other) noexcept
		{
			if constexpr (is_static_cast_inert_v<element_type*, std::remove_extent_t<U>*>)
			{
				pointer::control* const ctrl = std::exchange(other.m_ctrl, nullptr);
				element_type* const value = std::exchange(other.m_value, nullptr);
				this->decrement(m_ctrl);
				m_ctrl = ctrl;
				m_value = value;
			}
			else
			{
				const wide_shared_ptr<U> locked = other.lock();
				SH_POINTER_ASSERT(locked.m_ctrl == nullptr || locked.m_ctrl == other.m_ctrl,
					"Result of wide_weak_ptr::lock points to unexpected control block.");
				this->decrement(m_ctrl);
				m_ctrl = locked.m_ctrl;
				m_value = locked.m_value;
				if (m_ctrl)
				{
					// Take over reference from other.
					other.m_ctrl = nullptr;
					other.m_value = nullptr;
				}
				else
				{
					// Reset other to drop its reference as one may expect from "move".
					other.reset();
				}
			}
			return *this;
		}

		// implicit conversion from wide_shared_ptr
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_weak_ptr(const wide_shared_ptr<U>& other) noexcept
			: m_ctrl{ other.m_ctrl }
			, m_value{ other.m_value }
		{
			increment(m_ctrl);
		}
		template <typename U>
			requires std::is_convertible_v<U*, T*>
		wide_weak_ptr& operator=(const wide_shared_ptr<U>& other) noexcept
		{
			increment(other.m_ctrl);
			decrement(m_ctrl);
			m_ctrl = other.m_ctrl;
			m_value = other.m_value;
			return *this;
		}

	private:
		template <typename U> friend class shared_ptr;
		template <typename U> friend class weak_ptr;
		template <typename U> friend class wide_shared_ptr;
		template <typename U> friend class wide_weak_ptr;

		static void increment(pointer::control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->weak_inc();
			}
		}
		static void decrement(pointer::control* const ctrl) noexcept
		{
			if (ctrl)
			{
				ctrl->weak_dec();
			}
		}

		/** Pointer to the control block or nullptr. May be nullptr if uncontrolled (aliasing an empty shared_ptr) or m_value is nullptr.
		 */
		pointer::control* m_ctrl;
		/** Pointer to the owned value or nullptr. May be nullptr if empty or explicitly constructed from a pointer equal to nullptr. May be invalid to dereference.
		 */
		element_type* m_value;
	};

	/**	Constructs a sh::wide_shared_ptr to own a (value initialized) element T using the supplied allocator.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@tparam Args The types of arguments passed to T's constructor.
	 *	@param alloc The allocator to use.
	 *	@param args The arguments passed to the constructor of T.
	 *	@return A non-null sh::wide_shared_ptr owning the element T.
	 */
	template <
		typename T,
		typename Alloc,
		typename... Args
	>
		requires (false == std::is_array_v<T>
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> allocate_shared(const Alloc& alloc, Args&&... args)
	{
		static constexpr std::integral_constant<std::size_t, 1> element_count;
		auto [control, value] = pointer::external_value_control<
				T,
				decltype(element_count),
				pointer::external_value_deleter<T>,
				Alloc
			>::template allocate<
				pointer::construct_method::value_ctor
			>(pointer::external_value_deleter<T>{}, alloc, std::forward<Args>(args)...);
		return wide_shared_ptr<T>{ control, value };
	}
	/**	Constructs a sh::wide_shared_ptr to own a (default initialized) element T using the supplied allocator.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@return A non-null sh::wide_shared_ptr owning the element T.
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (false == std::is_array_v<T>
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> allocate_shared_for_overwrite(const Alloc& alloc)
	{
		static constexpr std::integral_constant<std::size_t, 1> element_count;
		auto [control, value] = pointer::external_value_control<
				T,
				decltype(element_count),
				pointer::external_value_deleter<T>,
				Alloc
			>::template allocate<
				pointer::construct_method::default_ctor
			>(pointer::external_value_deleter<T>{}, alloc);
		return wide_shared_ptr<T>{ control, value };
	}

	/**	Constructs via a supplied allocator a sh::wide_shared_ptr to own an array of \p element_count (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[\p element_count].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> allocate_shared(const Alloc& alloc, const std::size_t element_count)
	{
		auto [control, value] = pointer::external_value_control<
				T,
				pointer::integral<std::size_t>,
				pointer::external_value_deleter<T>,
				Alloc
			>::template allocate_array<
				pointer::construct_method::value_ctor
			>(pointer::integral<std::size_t>{ element_count }, pointer::external_value_deleter<T>{}, alloc);
		return wide_shared_ptr<T>{ control, value };
	}
	/**	Constructs via a supplied allocator a sh::wide_shared_ptr to own an array of (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[N].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> allocate_shared(const Alloc& alloc)
	{
		auto [control, value] = pointer::external_value_control<
				T,
				std::integral_constant<std::size_t, std::extent_v<T>>,
				pointer::external_value_deleter<T>,
				Alloc
			>::template allocate_array<
				pointer::construct_method::value_ctor
			>(std::integral_constant<std::size_t, std::extent_v<T>>{}, pointer::external_value_deleter<T>{}, alloc);
		return wide_shared_ptr<T>{ control, value };
	}
	/**	Constructs via a supplied allocator a sh::wide_shared_ptr to own an array of \p element_count (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@param init_value The value to copy into each allocated element.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[\p element_count].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> allocate_shared(const Alloc& alloc, const std::size_t element_count, const std::remove_extent_t<T>& init_value)
	{
		auto [control, value] = pointer::external_value_control<
				T,
				pointer::integral<std::size_t>,
				pointer::external_value_deleter<T>,
				Alloc
			>::template allocate_array<
				pointer::construct_method::value_ctor
			>(pointer::integral<std::size_t>{ element_count }, pointer::external_value_deleter<T>{}, alloc, init_value);
		return wide_shared_ptr<T>{ control, value };
	}
	/**	Constructs via a supplied allocator a sh::wide_shared_ptr to own an array of (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@param init_value The value to copy into each allocated element.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[N].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> allocate_shared(const Alloc& alloc, const std::remove_extent_t<T>& init_value)
	{
		auto [control, value] = pointer::external_value_control<
				T,
				std::integral_constant<std::size_t, std::extent_v<T>>,
				pointer::external_value_deleter<T>,
				Alloc
			>::template allocate_array<
				pointer::construct_method::value_ctor
			>(std::integral_constant<std::size_t, std::extent_v<T>>{}, pointer::external_value_deleter<T>{}, alloc, init_value);
		return wide_shared_ptr<T>{ control, value };
	}
	/**	Constructs via a supplied allocator a sh::wide_shared_ptr to own an array of \p element_count (default initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[\p element_count].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> allocate_shared_for_overwrite(const Alloc& alloc, const std::size_t element_count)
	{
		auto [control, value] = pointer::external_value_control<
				T,
				pointer::integral<std::size_t>,
				pointer::external_value_deleter<T>,
				Alloc
			>::template allocate_array<
				pointer::construct_method::default_ctor
			>(pointer::integral<std::size_t>{ element_count }, pointer::external_value_deleter<T>{}, alloc);
		return wide_shared_ptr<T>{ control, value };
	}
	/**	Constructs via a supplied allocator a sh::wide_shared_ptr to own an array of (default initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Alloc The allocator type to use for construction and destruction.
	 *	@param alloc The allocator to use.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[N].
	 */
	template <
		typename T,
		typename Alloc
	>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> allocate_shared_for_overwrite(const Alloc& alloc)
	{
		auto [control, value] = pointer::external_value_control<
				T,
				std::integral_constant<std::size_t, std::extent_v<T>>,
				pointer::external_value_deleter<T>,
				Alloc
			>::template allocate_array<
				pointer::construct_method::default_ctor
			>(std::integral_constant<std::size_t, std::extent_v<T>>{}, pointer::external_value_deleter<T>{}, alloc);
		return wide_shared_ptr<T>{ control, value };
	}

	/**	Constructs a sh::wide_shared_ptr to own a (value initialized) element T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@tparam Args The types of arguments passed to T's constructor.
	 *	@param args The arguments passed to the constructor of T.
	 *	@return A non-null sh::wide_shared_ptr owning the element T.
	 */
	template <
		typename T,
		typename... Args
	>
		requires (false == std::is_array_v<T>
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> make_shared(Args&&... args)
	{
		return sh::allocate_shared<T>(std::allocator<T>{}, std::forward<Args>(args)...);
	}
	/**	Constructs a sh::wide_shared_ptr to own a (default initialized) element T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@return A non-null sh::wide_shared_ptr owning the element T.
	 */
	template <typename T>
		requires (false == std::is_array_v<T>
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> make_shared_for_overwrite()
	{
		return sh::allocate_shared_for_overwrite<T>(std::allocator<T>{});
	}

	/**	Constructs a sh::wide_shared_ptr to own an array of \p element_count (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[\p element_count].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> make_shared(const std::size_t element_count)
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared<T>(std::allocator<element_type>{}, element_count);
	}
	/**	Constructs a sh::wide_shared_ptr to own an array of (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[N].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> make_shared()
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared<T>(std::allocator<element_type>{});
	}
	/**	Constructs a sh::wide_shared_ptr to own an array of \p element_count (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@param init_value The value to copy into each allocated element.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[\p element_count].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> make_shared(const std::size_t element_count, const std::remove_extent_t<T>& init_value)
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared<T>(std::allocator<element_type>{}, element_count, init_value);
	}
	/**	Constructs a sh::wide_shared_ptr to own an array of (value initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param init_value The value to copy into each allocated element.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[N].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> make_shared(const std::remove_extent_t<T>& init_value)
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared<T>(std::allocator<element_type>{}, init_value);
	}
	/**	Constructs a sh::wide_shared_ptr to own an array of \p element_count (default initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[\p element_count].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> == 0
			&& alignof(T) > pointer::max_alignment)
	wide_shared_ptr<T> make_shared_for_overwrite(const std::size_t element_count)
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared_for_overwrite<T>(std::allocator<element_type>{}, element_count);
	}
	/**	Constructs a sh::wide_shared_ptr to own an array of (default initialized) elements of T.
	 *	@throw May throw std::bad_alloc or other exceptions from T's constructor.
	 *	@tparam T The type of element to construct.
	 *	@param element_count The number of elements to allocate & construct.
	 *	@return A non-null sh::wide_shared_ptr owning the elements T[N].
	 */
	template <typename T>
		requires (std::is_array_v<T>
			&& std::extent_v<T> > 0
			&& alignof(T) > pointer::max_alignment)
// TEST
	wide_shared_ptr<T> make_shared_for_overwrite()
	{
		using element_type = std::remove_extent_t<T>;
		return sh::allocate_shared_for_overwrite<T>(std::allocator<element_type>{});
	}

	// wide_shared_ptr -> wide_shared_ptr casts:
	template <typename T, typename U>
	wide_shared_ptr<T> const_pointer_cast(const wide_shared_ptr<U>& from)
	{
		return wide_shared_ptr<T>{ pointer::const_cast_tag{}, from };
	}
	template <typename T, typename U>
	wide_shared_ptr<T> const_pointer_cast(wide_shared_ptr<U>&& from)
	{
		return wide_shared_ptr<T>{ pointer::const_cast_tag{}, std::move(from) };
	}
	template <typename T, typename U>
	wide_shared_ptr<T> dynamic_pointer_cast(const wide_shared_ptr<U>& from)
	{
		return wide_shared_ptr<T>{ pointer::dynamic_cast_tag{}, from };
	}
	template <typename T, typename U>
	wide_shared_ptr<T> dynamic_pointer_cast(wide_shared_ptr<U>&& from)
	{
		return wide_shared_ptr<T>{ pointer::dynamic_cast_tag{}, std::move(from) };
	}
	template <typename T, typename U>
	wide_shared_ptr<T> static_pointer_cast(const wide_shared_ptr<U>& from)
	{
		return wide_shared_ptr<T>{ pointer::static_cast_tag{}, from };
	}
	template <typename T, typename U>
	wide_shared_ptr<T> static_pointer_cast(wide_shared_ptr<U>&& from)
	{
		return wide_shared_ptr<T>{ pointer::static_cast_tag{}, std::move(from) };
	}
	template <typename T, typename U>
	wide_shared_ptr<T> reinterpret_pointer_cast(const wide_shared_ptr<U>& from)
	{
		return wide_shared_ptr<T>{ pointer::reinterpret_cast_tag{}, from };
	}
	template <typename T, typename U>
	wide_shared_ptr<T> reinterpret_pointer_cast(wide_shared_ptr<U>&& from)
	{
		return wide_shared_ptr<T>{ pointer::reinterpret_cast_tag{}, std::move(from) };
	}

	// shared_ptr -> wide_shared_ptr casts:
	template <
		typename T,
		typename U
	>
		requires (false == is_pointer_interconvertible_v<U, T>)
	wide_shared_ptr<T> dynamic_pointer_cast(const shared_ptr<U>& from)
	{
		return wide_shared_ptr<T>{ pointer::dynamic_cast_tag{}, from };
	}
	template <
		typename T,
		typename U
	>
		requires (false == is_pointer_interconvertible_v<U, T>)
	wide_shared_ptr<T> dynamic_pointer_cast(shared_ptr<U>&& from)
	{
		return wide_shared_ptr<T>{ pointer::dynamic_cast_tag{}, std::move(from) };
	}
	template <
		typename T,
		typename U
	>
		requires (false == is_pointer_interconvertible_v<U, T>)
	wide_shared_ptr<T> static_pointer_cast(const shared_ptr<U>& from)
	{
		return wide_shared_ptr<T>{ pointer::static_cast_tag{}, from };
	}
	template <
		typename T,
		typename U
	>
		requires (false == is_pointer_interconvertible_v<U, T>)
	wide_shared_ptr<T> static_pointer_cast(shared_ptr<U>&& from)
	{
		return wide_shared_ptr<T>{ pointer::static_cast_tag{}, std::move(from) };
	}

	template <typename Deleter, typename T>
	Deleter* get_deleter(const wide_shared_ptr<T>& ptr) noexcept
	{
		return ptr.m_ctrl != nullptr && ptr.m_ctrl->get_operations().m_get_deleter != nullptr
			? static_cast<Deleter*>(ptr.m_ctrl->get_operations().m_get_deleter(ptr.m_ctrl))
			: nullptr;
	}

	template <typename T, typename U>
	bool operator==(const wide_shared_ptr<T>& lhs, const wide_shared_ptr<U>& rhs) noexcept
	{
		return lhs.get() == rhs.get();
	}
	template <typename T>
	bool operator==(const wide_shared_ptr<T>& lhs, const std::nullptr_t) noexcept
	{
		return lhs.get() == nullptr;
	}
	template <typename U>
	bool operator==(const std::nullptr_t, const wide_shared_ptr<U>& rhs) noexcept
	{
		return nullptr == rhs.get();
	}
	template <typename T, typename U>
	std::strong_ordering operator<=>(const wide_shared_ptr<T>& lhs, const wide_shared_ptr<U>& rhs) noexcept
	{
		return lhs.get() <=> rhs.get();
	}
	template <typename T>
	std::strong_ordering operator<=>(const wide_shared_ptr<T>& lhs, const std::nullptr_t) noexcept
	{
		return lhs.get() <=> nullptr;
	}
	template <typename U>
	std::strong_ordering operator<=>(const std::nullptr_t, const wide_shared_ptr<U>& rhs) noexcept
	{
		return nullptr <=> rhs.get();
	}
	template <typename T, typename U, typename V>
	std::basic_ostream<U, V>& operator<<(std::basic_ostream<U, V>& ostr, const wide_shared_ptr<T>& ptr)
	{
		ostr << ptr.get();
		return ostr;
	}
	template <typename T>
	void swap(wide_shared_ptr<T>& lhs, wide_shared_ptr<T>& rhs) noexcept
	{
		lhs.swap(rhs);
	}
	template <typename T>
	void swap(wide_weak_ptr<T>& lhs, wide_weak_ptr<T>& rhs) noexcept
	{
		lhs.swap(rhs);
	}

	template <typename T>
	struct owner_less<wide_shared_ptr<T>>
	{
		bool operator()(const wide_shared_ptr<T>& lhs, const wide_shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_shared_ptr<T>& lhs, const shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const shared_ptr<T>& lhs, const wide_shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_shared_ptr<T>& lhs, const weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const weak_ptr<T>& lhs, const wide_shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_shared_ptr<T>& lhs, const wide_weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_weak_ptr<T>& lhs, const wide_shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
	};
	template <typename T>
	struct owner_less<wide_weak_ptr<T>>
	{
		bool operator()(const wide_weak_ptr<T>& lhs, const wide_weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_weak_ptr<T>& lhs, const shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const shared_ptr<T>& lhs, const wide_weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_weak_ptr<T>& lhs, const weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const weak_ptr<T>& lhs, const wide_weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_weak_ptr<T>& lhs, const wide_shared_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
		bool operator()(const wide_shared_ptr<T>& lhs, const wide_weak_ptr<T>& rhs) const noexcept
		{
			return lhs.owner_before(rhs);
		}
	};

} // namespace sh

namespace std
{
	template <typename T>
	struct hash<sh::wide_shared_ptr<T>> : std::hash<std::remove_extent_t<T>*>
	{
		constexpr decltype(auto) operator()(const sh::wide_shared_ptr<T>& ptr)
			noexcept(noexcept(std::hash<std::remove_extent_t<T>*>::operator()(ptr.get())))
		{
			return this->std::hash<std::remove_extent_t<T>*>::operator()(ptr.get());
		}
	};
} // namespace std

#endif

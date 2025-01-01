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

#ifndef INC_SH__POINTER_TRAITS_HPP
#define INC_SH__POINTER_TRAITS_HPP

#include <type_traits>

/**	@file
 *	This file declares sh::is_static_cast_inert. This checks for a given
 *	static_cast, that the memory addressed (pointed-at by a pointer or
 *	referenced by a reference) will never accessed during the cast.
 *
 *	That such can happen sounds odd, but fetching the vtable pointer during a
 *	static_cast upcast (derived to base) with virtual inheritance is a known
 *	side effect.
 *
 *	To detect casts to or through a virtual base, C++26-like
 *	sh::is_virtual_base_of and sh::is_virtual_base_of_v are defined. These are
 *	used by sh::is_static_cast_inert and sh::is_static_cast_inert_v to test for
 *	the inertness of a given static_cast.
 *
 *	Additionally provided is sh::is_pointer_interconvertible which aims to
 *	broaden C++20's std::is_pointer_interconvertible_base_of to additional cases.
 *
 *	Lastly, sh::is_pointer_interconvertible_with_class either aliases or
 *	implements C++20's std::is_pointer_interconvertible_with_class. C++20
 *	support is required for this to work (as written), but not all compilers
 *	provide this function at the time of writing.
 */

namespace sh::pointer
{
	/**	A union that intentionally doesn't initialize a instance of the given type.
	 *	@note More powerful std::declval, basically.
	 */
	template <typename T>
	union addressable_val_t final
	{
		constexpr addressable_val_t() noexcept
			: m_constructed{}
		{ }
		/**	Trivial constexpr destructor (C++20):
		 */
		constexpr ~addressable_val_t()
		{ }
		/**	Something inconsequential to construct.
		 */
		char m_constructed;
		/**	Secondary member that doesn't construct.
		 */
		struct constexpr_dtor_type final
		{
			/* For MSVC, must wrap T with another type with an explicitly constexpr marked destructor because
			 * MSVC incorrectly checks that all union member destructors are constexpr even if not called.
			 *
			 * See: https://developercommunity.visualstudio.com/t/Constexpr-union-destructor-cannot-result/10486017
			 */
			constexpr ~constexpr_dtor_type() = default;

			T m_instance;
		} m_wrapper;
	};

	/**	Returns the address of a addressable_val_t's object instance.
	 *	@param val An addressable_val_t.
	 *	@return The address of an addressable_val_t's object instance.
	 */
	template <typename T>
	constexpr const T* addressable_val(const addressable_val_t<T>& val) noexcept
	{
		return std::addressof(val.m_wrapper.m_instance);
	}

	/**	Check if a pointer-to-member resides at the same address as its objects:
	 *	@tparam S The object (e.g., struct or class) type.
	 *	@tparam M The type of the pointed-to member.
	 *	@param mem_ptr The pointer-to-member to inspect.
	 *	@return True if the pointer-to-member is interconvertible with its owning object.
	 */
	template <typename S, typename M>
	constexpr bool is_pointer_interconvertible_with_class(M S::* const mem_ptr) noexcept
	{
		if constexpr (std::is_standard_layout_v<M> == false)
		{
			// std::is_standard_layout is false, meaning that the first member
			// is not reinterpret_cast-able to & from its class. The object
			// type may have a vtable or inherit other classes.
			return false;
		}
		else
		{
			constexpr pointer::addressable_val_t<S> instance;
			// Check if the given pointer-to-member is in fact the first:
			return static_cast<const void*>(addressable_val(instance))
				== static_cast<const void*>(std::addressof(addressable_val(instance)->*mem_ptr));
		}
	}
} // namespace sh::pointer

namespace sh
{
	/**	If Base is a virtual base of Derived, provide a member constant value equal to true. Otherwise value is false.
	 *	@tparam Base The potential base type.
	 *	@tparam Derived The potential derived type.
	 */
	template <typename Base, typename Derived, typename = void>
	struct is_virtual_base_of
		// Inherit true from std::is_base_of if Base is a virtual or non-virtual
		// base of Derived. The specialization below will override for non-virtual
		// base cases:
		: std::is_base_of<Base, Derived>
	{ };

	/**	Specialization of is_virtual_base_of that handles cases where Base and be static_cast to Derived.
	 *	@tparam Base The potential base type.
	 *	@tparam Derived The potential derived type.
	 */
	template <typename Base, typename Derived>
	struct is_virtual_base_of <
		Base,
		Derived,
		// If static_cast from Base => Derived is possible, Base cannot be a
		// virtual base of Derived:
		std::void_t<decltype(
			static_cast<Derived*>(
				std::declval<std::remove_cv_t<Base>*>()
			)
		)>>
		: std::false_type
	{ };

	/**	If Base is a virtual base of Derived, is equal to true. Otherwise is equal to false.
	 *	@tparam Base The potential base type.
	 *	@tparam Derived The potential derived type.
	 */
	template <typename Base, typename Derived>
	constexpr bool is_virtual_base_of_v = is_virtual_base_of<Base, Derived>::value;

	/**	Checks if static_cast<To>(From{}) will never access memory address by the value of From{}.
	 *	@tparam To The input's type.
	 *	@tparam From The output type.
	 */
	template <typename To, typename From>
	struct is_static_cast_inert
		: /* if */ std::conditional_t<std::is_pointer_v<From>,
			/* then */ std::bool_constant<false == is_virtual_base_of_v<std::remove_pointer_t<To>, std::remove_pointer_t<From>>>,
		/* else if */ std::conditional_t<std::is_reference_v<From>,
			/* then */ std::bool_constant<false == is_virtual_base_of_v<std::remove_reference_t<To>, std::remove_reference_t<From>>>,
		/* else */ std::true_type>>
	{ };

	/**	Checks if static_cast<To>(From{}) will never access memory address by the value of From{}.
	 *	@tparam To The input's type.
	 *	@tparam From The output type.
	 */
	template <typename To, typename From>
	constexpr bool is_static_cast_inert_v = is_static_cast_inert<To, From>::value;

	/**	Test if To* can be static_cast to From* and will never alter the underlying address (that is, reinterpret_cast would serve as well in place of static_cast). Otherwise value is false.
	 *	@tparam From The pointed-to type of value given to static_cast.
	 *	@tparam To The pointed-to type requested as the output value from static_cast.
	 */
	template <typename From, typename To, typename = void>
	struct is_pointer_interconvertible
		: std::false_type
	{ };

	template <typename From, typename To>
	struct is_pointer_interconvertible
		<From
		, To
		, std::void_t<
			std::enable_if_t<
				// MSVC requires checking this or certain virtual upcasts will
				// result in an internal compiler error:
				false == is_virtual_base_of_v<To, From>,
				// Ensure static cast from From to To is possible. This will
				// fail unrelated types, incompatible arrays, and more:
				decltype(
					static_cast<std::add_pointer_t<To>>(
						std::declval<std::add_pointer_t<From>>()
					)
				)>
			>
		>
	{
	private:
		// Ignore const, volatile, extents (already checked by static_cast above):
		using from_type = std::remove_cv_t<std::remove_all_extents_t<From>>;
		using to_type = std::remove_cv_t<std::remove_all_extents_t<To>>;

		// If one type is the derived type, instantiate that one:
		using instance_type = std::conditional_t<
			std::is_base_of_v<from_type, to_type>,
			to_type,
			from_type>;

		static constexpr pointer::addressable_val_t<instance_type> m_instance{};
		static constexpr const to_type* to_pointer{
			static_cast<std::add_pointer_t<std::add_const_t<to_type>>>(pointer::addressable_val(m_instance))
		};
		static constexpr const from_type* from_pointer{
			static_cast<std::add_pointer_t<std::add_const_t<from_type>>>(pointer::addressable_val(m_instance))
		};

	public:
		static constexpr bool value = static_cast<const void*>(to_pointer) == static_cast<const void*>(from_pointer);
	};

	/**	If To can be static_cast to From and never alter the underlying address (as would be done by reinterpret_cast), is equal to true. Otherwise is equal to false.
	 *	@tparam From The type of value given to static_cast.
	 *	@tparam To The type requested as output from static_cast.
	 */
	template <typename From, typename To>
	constexpr bool is_pointer_interconvertible_v = is_pointer_interconvertible<From, To>::value;

#if __cpp_lib_is_pointer_interconvertible
	using std::is_pointer_interconvertible_with_class;
#else // !__cpp_lib_is_pointer_interconvertible
	using pointer::is_pointer_interconvertible_with_class;
#endif // !__cpp_lib_is_pointer_interconvertible

} // namespace sh

#endif

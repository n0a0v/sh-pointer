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

	/**	Test if To* can be static_cast to/from From* and never alter the underlying address (that is, reinterpret_cast would serve as well in place of static_cast), provide a member constant value equal to true. Otherwise value is false.
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
			// Ensure static cast from From to To is possible. This will fail on virtual upcasting:
			decltype(
				static_cast<std::remove_cv_t<To>*>(
					std::declval<std::remove_cv_t<From>*>()
				)
			)>
		>
	{
	private:
		using from_type = std::remove_cv_t<From>;
		using to_type = std::remove_cv_t<To>;

		/**	A union used to avoid constructing from_type or to_type.
		 */
		static constexpr union object_type
		{
			// Trivial constexpr destructor (C++20):
			constexpr ~object_type() {}
			// Something inconsequential to construct:
			char m_constructed;
			// If one type is the derived type, instantiate that one:
			std::conditional_t<std::is_base_of_v<from_type, to_type>, to_type, from_type> m_instance;
		} object{};
		static constexpr const to_type* to_pointer = static_cast<const to_type*>(&object.m_instance);
		static constexpr const from_type* from_pointer = static_cast<const from_type*>(&object.m_instance);

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
			// Use a union so that we don't need to construct S.
			constexpr union object_type
			{
				// Trivial constexpr destructor (C++20):
				constexpr ~object_type() {}
				// Something inconsequential to construct:
				char m_constructed;
				// Instance of class/struct type to use:
				S m_instance;
			} object{};
			// Check if the given pointer-to-member is in fact the first:
			return static_cast<const void*>(&object.m_instance)
				== static_cast<const void*>(&(object.m_instance.*mem_ptr));
		}
	}
#endif // !__cpp_lib_is_pointer_interconvertible

} // namespace sh::pointer

#endif

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

#ifndef INC_SH__NEVER_NULL_HPP
#define INC_SH__NEVER_NULL_HPP

/**	@file
 *	This file declares sh::never_null, a pointer-like type wrapper that
 *	disallows construction or assignment from nullptr. At runtime, if
 *	sh::never_null receives a nullptr value, the exception null_error is
 *	thrown.
 *
 *	Unlike sh::not_null, sh::never_null does not allow the contained pointer to
 *	be moved-from, resulting in a potential nullptr value. This may make
 *	sh::not_null a necessary choice for members of classes that may be
 *	moved-from.
 *
 *	Both comparison against nullptr and operator bool are available, but are
 *	evaluated at compile time.
 */

#include <memory>
#include <utility>

#include "pointer.hpp"

namespace sh
{

/**	A wrapper for pointer-like types that disallows construction and assignment
 *	from nullptr and also disallows move-from operations that may result in the
 *	container pointer becoming nullptr.
 *	@tparam T The pointer-like type to contain.
 */
template <typename T>
class never_null
{
public:
	/**	The pointer-like type contained.
	 */
	using pointer_type = T;

	never_null() = delete;
	never_null(std::nullptr_t) = delete;
	never_null& operator=(std::nullptr_t) = delete;

	/**	Constructor from a pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param pointer The value from which the pointer is to be constructed.
	 *	@throw If construction from pointer results in \p this being nullptr, null_error is thrown.
	 */
	template <
		typename U,
		typename IsConvertible = std::enable_if_t<std::is_constructible_v<pointer_type, U>>
	>
	constexpr explicit never_null(U&& pointer)
		: m_pointer{ std::forward<U>(pointer) }
	{
		if (m_pointer == nullptr)
		{
			throw null_error{ "never_null constructed as nullptr" };
		}
	}
	/**	Constructor from a never_null wrapped pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param other The never_null wrapper from which the pointer is to be constructed.
	 */
	template <
		typename U,
		typename IsConvertible = std::enable_if_t<std::is_constructible_v<pointer_type, U>>
	>
	constexpr never_null(const never_null<U>& other)
		noexcept(std::is_nothrow_constructible_v<pointer_type, U>)
		: m_pointer{ other.get() }
	{
		SH_POINTER_ASSERT(m_pointer != nullptr,
			"never_null copy constructor doesn't expect incoming nullptr.");
	}

	/**	Assignment from a pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param pointer The pointer value to be assigned.
	 *	@return A reference to this never_null.
	 *	@throw If assignment from the pointer in \p other would result in this being nullptr, null_error is thrown.
	 */
	template <
		typename U,
		typename IsConvertible = std::enable_if_t<
			(std::is_same_v<pointer_type, U>
				&& std::is_assignable_v<pointer_type&, U>)
			|| (false == std::is_same_v<pointer_type, U>
				&& std::is_constructible_v<pointer_type, U>)
		>
	>
	constexpr never_null& operator=(U&& pointer)
	{
		if constexpr (std::is_same_v<pointer_type, U>)
		{
			// No temporary used for same-type.
			if (pointer == nullptr)
			{
				throw null_error{ "never_null assigned nullptr" };
			}
			// Assign only after null_error would have been thrown.
			m_pointer = std::forward<U>(pointer);
		}
		else
		{
			// Convert U to pointer_type before evaluating for null/non-null.
			pointer_type p{ std::forward<U>(pointer) };
			if (p == nullptr)
			{
				throw null_error{ "never_null assigned nullptr" };
			}
			// Assign only after null_error would have been thrown.
			m_pointer = std::move(p);
		}
		SH_POINTER_ASSERT(m_pointer != nullptr,
			"never_null assignment operator resulted in nullptr.");
		return *this;
	}
	/**	Assignment from a never_null wrapped pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param other The never_null wrapper from which the pointer is to be assigned.
	 *	@return A reference to this never_null.
	 */
	template <
		typename U,
		typename IsConvertible = std::enable_if_t<
			(std::is_same_v<pointer_type, U>
				&& std::is_assignable_v<pointer_type&, U>)
			|| (false == std::is_same_v<pointer_type, U>
				&& std::is_constructible_v<pointer_type, U>)
		>
	>
	constexpr never_null& operator=(const never_null<U>& other)
		noexcept(std::is_nothrow_assignable_v<pointer_type, U>)
	{
		m_pointer = other.m_pointer;
		return *this;
	}

	/**	Return a reference to the contained pointer.
	 *	@return A reference to the contained pointer.
	 */
	[[nodiscard]] constexpr const pointer_type& get() const noexcept
	{
		SH_POINTER_ASSERT(m_pointer != nullptr,
			"never_null::get should never return nullptr.");
		return m_pointer;
	}
	/**	Return the result of dereferencing the contained pointer.
	 *	@return The result of dereferencing the contained pointer.
	 */
	[[nodiscard]] constexpr decltype(auto) operator*() const
		noexcept(noexcept(*std::declval<const pointer_type&>()))
	{
		SH_POINTER_ASSERT(m_pointer != nullptr,
			"never_null::operator* should never dereference nullptr.");
		return *m_pointer;
	}
	/**	Return a reference to the contained pointer.
	 *	@return A reference to the contained pointer.
	 */
	constexpr const pointer_type& operator->() const noexcept
	{
		SH_POINTER_ASSERT(m_pointer != nullptr,
			"never_null::operator-> should never return nullptr.");
		return m_pointer;
	}
	/**	Return a reference to the contained pointer.
	 *	@return A reference to the contained pointer.
	 */
	constexpr operator const pointer_type&() const noexcept
	{
		return m_pointer;
	}
	/**	Return if the contained pointer is not nullptr.
	 *	@return Always true.
	 */
	constexpr explicit operator bool() const noexcept
	{
		SH_POINTER_ASSERT(m_pointer != nullptr,
			"never_null::operator bool should never have to handle nullptr.");
		return true;
	}
	/**	Swap pointer values between \p this and another never_null \p other.
	 *	@param other The never_null with which to swap pointer values.
	 */
	constexpr void swap(never_null& other)
		noexcept(std::is_nothrow_swappable_v<pointer_type>)
	{
		using std::swap;
		swap(m_pointer, other.m_pointer);
	}
	/**	Swap pointer values.
	 *	@param lhs A never_null that will swap pointer values with \p rhs.
	 *	@param rhs A never_null that will swap pointer values with \p lhs.
	 */
	friend constexpr void swap(never_null& lhs, never_null& rhs)
		noexcept(std::is_nothrow_swappable_v<pointer_type>)
	{
		lhs.swap(rhs);
	}

private:
	/**	The wrapped pointer value.
	 */
	pointer_type m_pointer;
};

// never_null vs never_null
template <typename L, typename R>
constexpr bool operator==(const never_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() == std::declval<const R&>()))
{
	return lhs.get() == rhs.get();
}
template <typename L, typename R>
constexpr bool operator!=(const never_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() != std::declval<const R&>()))
{
	return lhs.get() != rhs.get();
}
template <typename L, typename R>
constexpr bool operator<(const never_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() < std::declval<const R&>()))
{
	return lhs.get() < rhs.get();
}
template <typename L, typename R>
constexpr bool operator<=(const never_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() <= std::declval<const R&>()))
{
	return lhs.get() <= rhs.get();
}
template <typename L, typename R>
constexpr bool operator>(const never_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() > std::declval<const R&>()))
{
	return lhs.get() > rhs.get();
}
template <typename L, typename R>
constexpr bool operator>=(const never_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() >= std::declval<const R&>()))
{
	return lhs.get() >= rhs.get();
}

// never_null vs std::nullptr_t
template <typename U>
constexpr bool operator==(const never_null<U>& lhs, std::nullptr_t) noexcept
{
	SH_POINTER_ASSERT(lhs.get() != nullptr,
		"operator== shouldn't receive nullptr never_null.");
	return false;
}
template <typename U>
constexpr bool operator==(std::nullptr_t, const never_null<U>& rhs) noexcept
{
	SH_POINTER_ASSERT(rhs.get() != nullptr,
		"operator== shouldn't receive nullptr never_null.");
	return false;
}
template <typename U>
constexpr bool operator!=(const never_null<U>& lhs, std::nullptr_t) noexcept
{
	SH_POINTER_ASSERT(lhs.get() != nullptr,
		"operator!= shouldn't receive nullptr never_null.");
	return true;
}
template <typename U>
constexpr bool operator!=(std::nullptr_t, const never_null<U>& rhs) noexcept
{
	SH_POINTER_ASSERT(rhs.get() != nullptr,
		"operator!= shouldn't receive nullptr never_null.");
	return true;
}
template <typename U>
constexpr bool operator<(const never_null<U>& lhs, std::nullptr_t) noexcept
{
	SH_POINTER_ASSERT(lhs.get() != nullptr,
		"operator< shouldn't receive nullptr never_null.");
	return false;
}
template <typename U>
constexpr bool operator<(std::nullptr_t, const never_null<U>& rhs) noexcept
{
	SH_POINTER_ASSERT(rhs.get() != nullptr,
		"operator< shouldn't receive nullptr never_null.");
	return true;
}
template <typename U>
constexpr bool operator<=(const never_null<U>& lhs, std::nullptr_t) noexcept
{
	SH_POINTER_ASSERT(lhs.get() != nullptr,
		"operator<= shouldn't receive nullptr never_null.");
	return false;
}
template <typename U>
constexpr bool operator<=(std::nullptr_t, const never_null<U>& rhs) noexcept
{
	SH_POINTER_ASSERT(rhs.get() != nullptr,
		"operator<= shouldn't receive nullptr never_null.");
	return true;
}
template <typename U>
constexpr bool operator>(const never_null<U>& lhs, std::nullptr_t) noexcept
{
	SH_POINTER_ASSERT(lhs.get() != nullptr,
		"operator> shouldn't receive nullptr never_null.");
	return true;
}
template <typename U>
constexpr bool operator>(std::nullptr_t, const never_null<U>& rhs) noexcept
{
	SH_POINTER_ASSERT(rhs.get() != nullptr,
		"operator> shouldn't receive nullptr never_null.");
	return false;
}
template <typename U>
constexpr bool operator>=(const never_null<U>& lhs, std::nullptr_t) noexcept
{
	SH_POINTER_ASSERT(lhs.get() != nullptr,
		"operator>= shouldn't receive nullptr never_null.");
	return true;
}
template <typename U>
constexpr bool operator>=(std::nullptr_t, const never_null<U>& rhs) noexcept
{
	SH_POINTER_ASSERT(rhs.get() != nullptr,
		"operator>= shouldn't receive nullptr never_null.");
	return false;
}

// never_null<U> vs pointer
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator==(const never_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() == std::declval<const P&>()))
{
	return lhs.get() == rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator==(const P& lhs, const never_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() == std::declval<const U&>()))
{
	return lhs == rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator!=(const never_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() != std::declval<const P&>()))
{
	return lhs.get() != rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator!=(const P& lhs, const never_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() != std::declval<const U&>()))
{
	return lhs != rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator<(const never_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() < std::declval<const P&>()))
{
	return lhs.get() < rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator<(const P& lhs, const never_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() < std::declval<const U&>()))
{
	return lhs < rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator<=(const never_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() <= std::declval<const P&>()))
{
	return lhs.get() <= rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator<=(const P& lhs, const never_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() <= std::declval<const U&>()))
{
	return lhs <= rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator>(const never_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() > std::declval<const P&>()))
{
	return lhs.get() > rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator>(const P& lhs, const never_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() > std::declval<const U&>()))
{
	return lhs > rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator>=(const never_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() >= std::declval<const P&>()))
{
	return lhs.get() >= rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator>=(const P& lhs, const never_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() >= std::declval<const U&>()))
{
	return lhs >= rhs.get();
}

} // namespace sh

#endif

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

#ifndef INC_SH__NOT_NULL_HPP
#define INC_SH__NOT_NULL_HPP

/**	@file
 *	This file declares sh::not_null, a pointer-like type wrapper that disallows
 *	construction or assignment from nullptr. At runtime, if sh::not_null
 *	receives a nullptr value, the exception sh::null_error is thrown.
 *
 *	It is possible for sh::not_null to contain a pointer value that is equal to
 *	nullptr if it has been moved-from. This may occur with pointer-like types
 *	such as std::unique_ptr or std::shared_ptr. Testing for non-nullptr can be
 *	done with operator== and operator!= against nullptr or operator bool.
 *
 *	The intended use is for parameters or members that cannot be a reference
 *	because they must hold ownership such as std::unique_ptr or a
 *	std::shared_ptr's reference count. Members that cannot be moved-from may be
 *	better served by sh::never_null.
 */

#include <memory>
#include <utility>

#include "pointer.hpp"

namespace sh
{

template <typename T> class never_null;

/**	A wrapper for pointer-like types that disallows construction and assignment from nullptr.
 *	@tparam T The pointer-like type to contain.
 */
template <typename T>
class not_null
{
public:
	/**	The pointer-like type contained.
	 */
	using pointer_type = T;

	not_null() = delete;
	not_null(std::nullptr_t) = delete;
	not_null& operator=(std::nullptr_t) = delete;

	/**	Constructor from a pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param pointer The value from which the pointer is to be constructed.
	 *	@throw If construction from pointer results in \p this being nullptr, null_error is thrown.
	 */
	template <
		typename U,
		typename IsConvertible = std::enable_if_t<std::is_constructible_v<pointer_type, U>>
	>
	constexpr explicit not_null(U&& pointer)
		: m_pointer{ std::forward<U>(pointer) }
	{
		if (m_pointer == nullptr)
		{
			throw null_error{ "not_null constructed as nullptr" };
		}
	}
	/**	Constructor from a non_null wrapped pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param other The non_null wrapper from which the pointer is to be constructed.
	 *	@throw If construction from the pointer in \p other results in this being nullptr, null_error is thrown.
	 */
	template <
		typename U,
		typename IsConvertible = std::enable_if_t<std::is_constructible_v<pointer_type, U>>
	>
	constexpr not_null(const not_null<U>& other)
		// Note: delegated constructor will non-nullptr check other.get():
		: not_null{ other.get() }
	{ }
	/**	Constructor from a non_null wrapped pointer rvalue with type convertible to pointer_type.
	 *	@note Will not throw an exception if \p other or the pointer resulting within \p this is nullptr.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param other The non_null wrapper from which the pointer is to be move constructed.
	 */
	template <
		typename U,
		typename IsConvertible = std::enable_if_t<std::is_constructible_v<pointer_type, U&&>>
	>
	constexpr not_null(not_null<U>&& other)
		noexcept(std::is_nothrow_constructible_v<pointer_type, U&&>)
		: m_pointer{ std::move(other).get() }
	{ }
	/**	Constructor from a never_null wrapped pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param other The never_null wrapper from which the pointer is to be constructed.
	 */
	template <
		typename U,
		typename IsConvertible = std::enable_if_t<std::is_constructible_v<pointer_type, U>>
	>
	constexpr not_null(const never_null<U>& other)
		noexcept(std::is_nothrow_constructible_v<pointer_type, U>)
		: m_pointer{ other.get() }
	{
		SH_POINTER_ASSERT(m_pointer != nullptr);
	}

	/**	Assignment from a pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param pointer The pointer value to be assigned.
	 *	@return A reference to this non_null.
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
	constexpr not_null& operator=(U&& pointer)
	{
		if constexpr (std::is_same_v<pointer_type, U>)
		{
			// No temporary used for same-type.
			if (pointer == nullptr)
			{
				throw null_error{ "not_null assigned nullptr" };
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
				throw null_error{ "not_null assigned nullptr" };
			}
			// Assign only after null_error would have been thrown.
			m_pointer = std::move(p);
		}
		SH_POINTER_ASSERT(m_pointer != nullptr);
		return *this;
	}
	/**	Assignment from a non_null wrapped pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param other The non_null wrapper from which the pointer is to be assigned.
	 *	@return A reference to this non_null.
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
	constexpr not_null& operator=(const not_null<U>& other)
	{
		// Note: delegated operator= will non-nullptr check other.get():
		return this->operator=(other.get());
	}
	/**	Assignment from a non_null wrapped pointer rvalue with type convertible to pointer_type.
	 *	@note Will not throw an exception if \p other or the pointer resulting within \p this is nullptr.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param other The non_null wrapper from which the pointer is to be move assigned.
	 *	@return A reference to this non_null.
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
	constexpr not_null& operator=(not_null<U>&& other)
		noexcept(std::is_nothrow_assignable_v<pointer_type&, U&&>)
	{
		m_pointer = std::move(other).get();
		return *this;
	}
	/**	Assignment from a never_null wrapped pointer with type convertible to pointer_type.
	 *	@tparam U The type of pointer that is to be assigned.
	 *	@param other The never_null wrapper from which the pointer is to be assigned.
	 *	@return A reference to this non_null.
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
	constexpr not_null& operator=(const never_null<U>& other)
		noexcept(std::is_nothrow_assignable_v<pointer_type&, U>)
	{
		m_pointer = other.get();
		SH_POINTER_ASSERT(m_pointer != nullptr);
		return *this;
	}

	/**	Return a reference to the contained pointer.
	 *	@return A reference to the contained pointer. May be nullptr if \p this has been moved-from.
	 */
	[[nodiscard]] constexpr const pointer_type& get() const &
	{
		return m_pointer;
	}
	/**	Return an rvalue reference to the contained pointer.
	 *	@return An rvalue reference to the contained pointer. May be nullptr if \p this has been moved-from.
	 */
	[[nodiscard]] constexpr pointer_type&& get() &&
	{
		return std::move(m_pointer);
	}
	/**	Return the result of dereferencing the contained pointer.
	 *	@return The result of dereferencing the contained pointer.
	 *	@throw If the pointer in \p this is nullptr, null_error is thrown.
	 */
	[[nodiscard]] constexpr decltype(auto) operator*() const
	{
		if (m_pointer == nullptr)
		{
			throw null_error{ "not_null dereferenced via operator* with nullptr" };
		}
		return *m_pointer;
	}
	/**	Return a reference to the contained pointer.
	 *	@return A reference to the contained pointer.
	 *	@throw If the pointer in \p this is nullptr, null_error is thrown.
	 */
	constexpr const pointer_type& operator->() const
	{
		if (m_pointer == nullptr)
		{
			throw null_error{ "not_null dereferenced via operator-> with nullptr" };
		}
		return m_pointer;
	}
	/**	Return a reference to the contained pointer.
	 *	@return A reference to the contained pointer. May be nullptr if \p this has been moved-from.
	 */
	constexpr operator const pointer_type&() const
	{
		return get();
	}
	/**	Return if the contained pointer is not nullptr.
	 *	@return True if the contained pointer is not nullptr, false otherwise (in instances that this has been moved-from).
	 */
	constexpr explicit operator bool() const noexcept
	{
		return m_pointer != nullptr;
	}
	/**	Swap pointer values between \p this and another non_null \p other.
	 *	@param other The not_null with which to swap pointer values.
	 */
	constexpr void swap(not_null& other)
		noexcept(std::is_nothrow_swappable_v<pointer_type>)
	{
		using std::swap;
		swap(m_pointer, other.m_pointer);
	}
	/**	Swap pointer values.
	 *	@param lhs A not_null that will swap pointer values with \p rhs.
	 *	@param rhs A not_null that will swap pointer values with \p lhs.
	 */
	friend constexpr void swap(not_null& lhs, not_null& rhs)
		noexcept(std::is_nothrow_swappable_v<pointer_type>)
	{
		lhs.swap(rhs);
	}

private:
	/**	The wrapped pointer value.
	 */
	pointer_type m_pointer;
};

// not_null vs not_null
template <typename L, typename R>
constexpr bool operator==(const not_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() == std::declval<const R&>()))
{
	return lhs.get() == rhs.get();
}
template <typename L, typename R>
constexpr bool operator!=(const not_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() != std::declval<const R&>()))
{
	return lhs.get() != rhs.get();
}
template <typename L, typename R>
constexpr bool operator<(const not_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() < std::declval<const R&>()))
{
	return lhs.get() < rhs.get();
}
template <typename L, typename R>
constexpr bool operator<=(const not_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() <= std::declval<const R&>()))
{
	return lhs.get() <= rhs.get();
}
template <typename L, typename R>
constexpr bool operator>(const not_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() > std::declval<const R&>()))
{
	return lhs.get() > rhs.get();
}
template <typename L, typename R>
constexpr bool operator>=(const not_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() >= std::declval<const R&>()))
{
	return lhs.get() >= rhs.get();
}

// not_null vs never_null
template <typename L, typename R>
constexpr bool operator==(const not_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() == std::declval<const R&>()))
{
	return lhs.get() == rhs.get();
}
template <typename L, typename R>
constexpr bool operator==(const never_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() == std::declval<const R&>()))
{
	return lhs.get() == rhs.get();
}
template <typename L, typename R>
constexpr bool operator!=(const not_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() != std::declval<const R&>()))
{
	return lhs.get() != rhs.get();
}
template <typename L, typename R>
constexpr bool operator!=(const never_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() != std::declval<const R&>()))
{
	return lhs.get() != rhs.get();
}
template <typename L, typename R>
constexpr bool operator<(const not_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() < std::declval<const R&>()))
{
	return lhs.get() < rhs.get();
}
template <typename L, typename R>
constexpr bool operator<(const never_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() < std::declval<const R&>()))
{
	return lhs.get() < rhs.get();
}
template <typename L, typename R>
constexpr bool operator<=(const not_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() <= std::declval<const R&>()))
{
	return lhs.get() <= rhs.get();
}
template <typename L, typename R>
constexpr bool operator<=(const never_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() <= std::declval<const R&>()))
{
	return lhs.get() <= rhs.get();
}
template <typename L, typename R>
constexpr bool operator>(const not_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() > std::declval<const R&>()))
{
	return lhs.get() > rhs.get();
}
template <typename L, typename R>
constexpr bool operator>(const never_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() > std::declval<const R&>()))
{
	return lhs.get() > rhs.get();
}
template <typename L, typename R>
constexpr bool operator>=(const not_null<L>& lhs, const never_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() >= std::declval<const R&>()))
{
	return lhs.get() >= rhs.get();
}
template <typename L, typename R>
constexpr bool operator>=(const never_null<L>& lhs, const not_null<R>& rhs)
	noexcept(noexcept(std::declval<const L&>() >= std::declval<const R&>()))
{
	return lhs.get() >= rhs.get();
}

// not_null vs std::nullptr_t
template <typename U>
constexpr bool operator==(const not_null<U>& lhs, std::nullptr_t) noexcept
{
	return lhs.get() == nullptr;
}
template <typename U>
constexpr bool operator==(std::nullptr_t, const not_null<U>& rhs) noexcept
{
	return rhs.get() == nullptr;
}
template <typename U>
constexpr bool operator!=(const not_null<U>& lhs, std::nullptr_t) noexcept
{
	return lhs.get() != nullptr;
}
template <typename U>
constexpr bool operator!=(std::nullptr_t, const not_null<U>& rhs) noexcept
{
	return rhs.get() != nullptr;
}
template <typename U>
constexpr bool operator<(const not_null<U>& lhs, std::nullptr_t) noexcept
{
	return false;
}
template <typename U>
constexpr bool operator<(std::nullptr_t, const not_null<U>& rhs) noexcept
{
	return rhs.get() != nullptr;
}
template <typename U>
constexpr bool operator<=(const not_null<U>& lhs, std::nullptr_t) noexcept
{
	return lhs.get() == nullptr;
}
template <typename U>
constexpr bool operator<=(std::nullptr_t, const not_null<U>& rhs) noexcept
{
	return true;
}
template <typename U>
constexpr bool operator>(const not_null<U>& lhs, std::nullptr_t) noexcept
{
	return lhs.get() != nullptr;
}
template <typename U>
constexpr bool operator>(std::nullptr_t, const not_null<U>& rhs) noexcept
{
	return false;
}
template <typename U>
constexpr bool operator>=(const not_null<U>& lhs, std::nullptr_t) noexcept
{
	return true;
}
template <typename U>
constexpr bool operator>=(std::nullptr_t, const not_null<U>& rhs) noexcept
{
	return rhs.get() == nullptr;
}

// not_null<U> vs pointer
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator==(const not_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() == std::declval<const P&>()))
{
	return lhs.get() == rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator==(const P& lhs, const not_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() == std::declval<const U&>()))
{
	return lhs == rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator!=(const not_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() != std::declval<const P&>()))
{
	return lhs.get() != rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator!=(const P& lhs, const not_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() != std::declval<const U&>()))
{
	return lhs != rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator<(const not_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() < std::declval<const P&>()))
{
	return lhs.get() < rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator<(const P& lhs, const not_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() < std::declval<const U&>()))
{
	return lhs < rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator<=(const not_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() <= std::declval<const P&>()))
{
	return lhs.get() <= rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator<=(const P& lhs, const not_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() <= std::declval<const U&>()))
{
	return lhs <= rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator>(const not_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() > std::declval<const P&>()))
{
	return lhs.get() > rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator>(const P& lhs, const not_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() > std::declval<const U&>()))
{
	return lhs > rhs.get();
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator>=(const not_null<U>& lhs, const P& rhs)
	noexcept(noexcept(std::declval<const U&>() >= std::declval<const P&>()))
{
	return lhs.get() >= rhs;
}
template <
	typename U,
	typename P,
	typename IsConvertible = std::enable_if_t<std::is_convertible_v<U, P> || std::is_convertible_v<P, U>>
>
constexpr bool operator>=(const P& lhs, const not_null<U>& rhs)
	noexcept(noexcept(std::declval<const P&>() >= std::declval<const U&>()))
{
	return lhs >= rhs.get();
}

} // namespace sh

#endif

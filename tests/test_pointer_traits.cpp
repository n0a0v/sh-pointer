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

#include <gtest/gtest.h>

#include <sh/pointer_traits.hpp>

using sh::pointer::is_pointer_interconvertible_v;
using sh::pointer::is_pointer_interconvertible_with_class;
using sh::pointer::is_virtual_base_of_v;

namespace
{
	namespace Layout
	{
		struct Std
		{
			int m_first;
			int m_last;
		};
		struct NonStdInherits : private Std
		{
			int m_first;
		};
		struct NonStdVirtual
		{
			virtual ~NonStdVirtual() = default;

			int m_first;
		};
	} // Layout

	namespace SimpleInheritance
	{
		struct Base
		{
			int m_base{ 123 };
		};
		struct Derived : Base
		{
			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Derived).
			int m_derived[128] = { 456 };
		};
	} // SimpleInheritance

	namespace SingleInheritance
	{
		struct Base
		{
			virtual ~Base() = default;

			int m_base{ 123 };
		};
		struct Derived : Base
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Derived).
			int m_derived[128] = { 456 };
		};
	} // SingleInheritance

	namespace MultipleInheritance
	{
		struct Base1
		{
			virtual ~Base1() = default;

			int m_base1{ 123 };
		};
		struct Base2
		{
			virtual ~Base2() = default;

			int m_base2{ 123 };
		};
		struct Derived : Base1, Base2
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Base1) to
			// sizeof(Derived).
			int m_derived[128] = { 456 };
		};
	} // MultipleInheritance

	namespace VirtualInheritance
	{
		struct Base
		{
			virtual ~Base() = default;

			int m_base{ 123 };
		};
		struct Mid1 : virtual Base
		{
			~Mid1() override = default;

			int m_mid1{ 456 };
		};
		struct Mid2 : virtual Base
		{
			~Mid2() override = default;

			int m_mid2{ 789 };
		};
		struct Derived : Mid1, Mid2
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Derived).
			int m_derived[128] = { 1000 };
		};
	} // VirtualInheritance

} // anonymous namespace

TEST(sh_pointer_traits, is_virtual_base_of)
{
	{
		using namespace MultipleInheritance;
		static_assert(false == is_virtual_base_of_v<Base1, Base1>);
		static_assert(false == is_virtual_base_of_v<Base1, Base2>);
		static_assert(false == is_virtual_base_of_v<Base1, Derived>);
		static_assert(false == is_virtual_base_of_v<Base2, Derived>);
	}
	{
		using namespace VirtualInheritance;
		static_assert(false == is_virtual_base_of_v<Base, Base>);
		static_assert(is_virtual_base_of_v<Base, Mid1>);
		static_assert(is_virtual_base_of_v<Base, Mid2>);
		static_assert(is_virtual_base_of_v<Base, Derived>);
	}
}
TEST(sh_pointer_traits, is_pointer_interconvertible_array_pointer)
{
	static_assert(is_pointer_interconvertible_v<int, int>);
	static_assert(is_pointer_interconvertible_v<int[10], int[10]>);
	static_assert(is_pointer_interconvertible_v<int, const int>);
	static_assert(is_pointer_interconvertible_v<const int, int>);
	static_assert(is_pointer_interconvertible_v<const int, const int>);
	// Rank must match:
	static_assert(false == is_pointer_interconvertible_v<int[4], int>);
	static_assert(false == is_pointer_interconvertible_v<int, int[4]>);
	// Extent must match:
	static_assert(false == is_pointer_interconvertible_v<int[10], int[9]>);
	// Levels of pointer must match:
	static_assert(false == is_pointer_interconvertible_v<int*, int>);
	static_assert(false == is_pointer_interconvertible_v<int, int*>);
}
TEST(sh_pointer_traits, is_pointer_interconvertible_simple_inheritance)
{
	using namespace SimpleInheritance;
	Derived d;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	Derived* const d1 = &d;
	Base* const b = d1;
	EXPECT_EQ(d1, b);
	EXPECT_EQ(
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(d1)),
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(b))
	);
	static_assert(is_pointer_interconvertible_v<Base, Derived>);
	Derived* const d2 = static_cast<Derived*>(b);
	EXPECT_EQ(d2, b);
	EXPECT_EQ(
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(d2)),
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(b))
	);
}
TEST(sh_pointer_traits, is_pointer_interconvertible_single_inheritance)
{
	using namespace SingleInheritance;
	Derived d;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	Derived* const d1 = &d;
	Base* const b = d1;
	EXPECT_EQ(d1, b);
	EXPECT_EQ(
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(d1)),
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(b))
	);
	static_assert(is_pointer_interconvertible_v<Base, Derived>);
	Derived* const d2 = static_cast<Derived*>(b);
	EXPECT_EQ(d2, b);
	EXPECT_EQ(
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(d2)),
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(b))
	);
}
TEST(sh_pointer_traits, is_pointer_interconvertible_multiple_inheritance)
{
	using namespace MultipleInheritance;
	Derived d;
	static_assert(is_pointer_interconvertible_v<Derived, Base1>);
	Derived* const d1 = &d;
	Base1* const b = d1;
	EXPECT_EQ(d1, b);
	EXPECT_EQ(
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(d1)),
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(b))
	);
	static_assert(is_pointer_interconvertible_v<Base1, Derived>);
	Derived* const d2 = static_cast<Derived*>(b);
	EXPECT_EQ(d2, b);
	EXPECT_EQ(
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(d2)),
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(b))
	);
	static_assert(is_pointer_interconvertible_v<Derived, Base2> == false);
}
TEST(sh_pointer_traits, is_pointer_interconvertible_with_class)
{
	using namespace Layout;
	static_assert(is_pointer_interconvertible_with_class(&Std::m_first));
	static_assert(false == is_pointer_interconvertible_with_class(&Std::m_last));
	static_assert(false == is_pointer_interconvertible_with_class(&NonStdInherits::m_first));
	static_assert(false == is_pointer_interconvertible_with_class(&NonStdVirtual::m_first));
}

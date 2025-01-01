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

#include <cstring>
#include <iostream>
#include <sh/wide_shared_ptr.hpp>

using sh::bad_collapse;
using sh::const_pointer_cast;
using sh::dynamic_pointer_cast;
using sh::get_deleter;
using sh::make_shared;
using sh::make_shared_for_overwrite;
using sh::wide_shared_ptr;
using sh::wide_weak_ptr;
using sh::reinterpret_pointer_cast;
using sh::shared_ptr;
using sh::static_pointer_cast;
using sh::weak_ptr;
using sh::is_static_cast_inert_v;

namespace
{
	struct allocations
	{
		std::size_t m_current{ 0 };
		std::size_t m_peak{ 0 };
		std::size_t m_allocate_calls{ 0 };
		std::size_t m_deallocate_calls{ 0 };
		std::size_t m_construct_calls{ 0 };
		std::size_t m_construct_default{ 0 };
		std::size_t m_destroy_calls{ 0 };

		friend std::ostream& operator<<(std::ostream& ostr, const allocations& all)
		{
			return ostr << "current " << all.m_current << " bytes"
				", peak " << all.m_peak << " bytes"
				", allocate calls " << all.m_allocate_calls <<
				", deallocate calls " << all.m_deallocate_calls <<
				", construct calls " << all.m_construct_calls <<
				", construct default " << all.m_construct_default <<
				", destroy calls " << all.m_destroy_calls;
		}
	};

	template <typename T>
	struct typed_allocations : allocations
	{
		static allocations& get()
		{
			static allocations instance;
			return instance;
		}
	};

	using general_allocations = typed_allocations<void>;

	template <typename T, typename Allocator = std::allocator<T>>
	class counted_allocator : private Allocator
	{
	public:
		using allocator_traits = std::allocator_traits<Allocator>;
		using value_type = typename allocator_traits::value_type;
		using size_type = typename allocator_traits::size_type;
		using difference_type = typename allocator_traits::difference_type;

		template <typename U>
		struct rebind
		{
			using other = counted_allocator<U>;
		};

		counted_allocator() = default;
		counted_allocator(const counted_allocator& other) = default;
		counted_allocator(counted_allocator&& other) noexcept = default;
		counted_allocator& operator=(const counted_allocator& other) = default;
		counted_allocator& operator=(counted_allocator&& other) noexcept = default;

		template <typename U>
		explicit counted_allocator(const counted_allocator<U>& other) noexcept
		{ }
		template <typename U>
		explicit counted_allocator(counted_allocator<U>&& other) noexcept
		{ }

		[[nodiscard]] constexpr T* allocate(const std::size_t n)
		{
			general_allocations::get().m_current += sizeof(T) * n;
			general_allocations::get().m_allocate_calls += 1;
			general_allocations::get().m_peak = std::max(general_allocations::get().m_peak, general_allocations::get().m_current);

			typed_allocations<T>::get().m_current += n;
			typed_allocations<T>::get().m_allocate_calls += 1;
			typed_allocations<T>::get().m_peak = std::max(typed_allocations<T>::get().m_peak, typed_allocations<T>::get().m_current);
			return allocator_traits::allocate(*this, n);
		}
		constexpr void deallocate(T* const p, const std::size_t n)
		{
			general_allocations::get().m_current -= sizeof(T) * n;
			general_allocations::get().m_deallocate_calls += 1;
			typed_allocations<T>::get().m_current -= n;
			typed_allocations<T>::get().m_deallocate_calls += 1;
			return allocator_traits::deallocate(*this, p, n);
		}
		template <typename U, typename... Args>
		void construct(U* const p, Args&&... args)
		{
			allocator_traits::construct(*this, p, std::forward<Args>(args)...);
			general_allocations::get().m_construct_calls += 1;
			typed_allocations<T>::get().m_construct_calls += 1;
		}
		template <typename U>
		void destroy(U* const p)
		{
			general_allocations::get().m_destroy_calls += 1;
			typed_allocations<T>::get().m_destroy_calls += 1;
			allocator_traits::destroy(*this, p);
		}
	};

	template <typename T, int C, typename Allocator = counted_allocator<T>>
	class prefill_allocator : private Allocator
	{
	public:
		using allocator_traits = std::allocator_traits<Allocator>;
		using value_type = typename allocator_traits::value_type;
		using size_type = typename allocator_traits::size_type;
		using difference_type = typename allocator_traits::difference_type;

		template <typename U>
		struct rebind
		{
			using other = prefill_allocator<U, C, typename Allocator::template rebind<U>::other>;
		};

		prefill_allocator() = default;
		prefill_allocator(const prefill_allocator& other) = default;
		prefill_allocator(prefill_allocator&& other) noexcept = default;
		prefill_allocator& operator=(const prefill_allocator& other) = default;
		prefill_allocator& operator=(prefill_allocator&& other) noexcept = default;

		template <typename U, int D>
		explicit prefill_allocator(const prefill_allocator<U, D>& other) noexcept
		{ }
		template <typename U, int D>
		explicit prefill_allocator(prefill_allocator<U, D>&& other) noexcept
		{ }

		[[nodiscard]] constexpr T* allocate(const std::size_t n)
		{
			T* const result = allocator_traits::allocate(*this, n);
			std::memset(static_cast<void*>(result), C, sizeof(T) * n);
			return result;
		}
		using Allocator::deallocate;
		template <typename U, typename... Args>
		void construct(U* const p, Args&&... args)
		{
			allocator_traits::construct(*this, p, std::forward<Args>(args)...);
		}
		template <typename U>
		void destroy(U* const p)
		{
			allocator_traits::destroy(*this, p);
		}
	};

	template <typename T, typename Allocator = counted_allocator<T>>
	class stateful_allocator : public Allocator
	{
	public:
		template <typename U>
		struct rebind
		{
			using other = stateful_allocator<U, typename Allocator::template rebind<U>::other>;
		};

		explicit stateful_allocator(const int state = 123) noexcept
			: m_state{ state }
		{ }
		stateful_allocator(const stateful_allocator& other) = default;
		stateful_allocator(stateful_allocator&& other) noexcept = default;
		stateful_allocator& operator=(const stateful_allocator& other) = default;
		stateful_allocator& operator=(stateful_allocator&& other) noexcept = default;

		template <typename U>
		explicit stateful_allocator(const stateful_allocator<U>& other) noexcept
			: m_state{ other.m_state }
		{ }
		template <typename U>
		explicit stateful_allocator(stateful_allocator<U>&& other) noexcept
			: m_state{ other.m_state }
		{ }

		int m_state;
	};

	namespace SingleInheritance
	{
		struct Base
		{
			int m_base{ 123 };

			virtual ~Base() = default;
		};
		struct Derived : Base
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Derived).
			int m_derived[128] = { 456 };
		};
		struct DifferentDerived : Base
		{
			~DifferentDerived() override = default;

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(DifferentDerived).
			int m_different[128] = { 456 };
		};
	} // SingleInheritance

	namespace MultipleInheritance
	{
		struct First
		{
			virtual ~First() = default;

			int m_first{ 123 };
		};
		struct Base
		{
			virtual ~Base() = default;

			int m_base{ 456 };
		};
		struct Derived : First, Base
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Derived).
			int m_derived[128] = { 789 };
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

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Mid1).
			int m_derived1[128] = { 456 };
		};
		struct Mid2 : virtual Base
		{
			~Mid2() override = default;

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Mid2).
			int m_derived2[128] = { 456 };
		};
		struct Derived : Mid1, Mid2
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Derived).
			int m_derived[128] = { 456 };
		};
	} // VirtualInheritance

	struct alignas(sh::pointer::max_alignment * 2) extended_alignment
	{
		int m_value;
	};

	struct configurable_exception
	{ };

	struct throws_on_counter : public extended_alignment
	{
		static int throw_counter;
		static int current_counter;

		throws_on_counter()
			: extended_alignment{ current_counter++ }
		{
			if (throw_counter >= 0 && m_value == throw_counter)
			{
				throw configurable_exception{};
			}
		}
	};

	int throws_on_counter::throw_counter = -1;
	int throws_on_counter::current_counter = 0;

	class sh_wide_shared_ptr : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			general_allocations::get() = allocations{};
			ASSERT_EQ(0u, general_allocations::get().m_current);
			ASSERT_EQ(0u, general_allocations::get().m_allocate_calls);
			ASSERT_EQ(0u, general_allocations::get().m_deallocate_calls);
			ASSERT_EQ(0u, general_allocations::get().m_construct_calls);
			ASSERT_EQ(0u, general_allocations::get().m_construct_default);
			ASSERT_EQ(0u, general_allocations::get().m_destroy_calls);

			throws_on_counter::throw_counter = -1;
			throws_on_counter::current_counter = 0;
		}
		void TearDown() override
		{
			const auto& gen = general_allocations::get();
			EXPECT_EQ(0u, gen.m_current) << gen;
			EXPECT_EQ(gen.m_allocate_calls, gen.m_deallocate_calls) << gen;
			EXPECT_EQ(gen.m_construct_calls + gen.m_construct_default, gen.m_destroy_calls) << gen;
		}
	};

} // anonymous namespace

TEST_F(sh_wide_shared_ptr, wide_shared_ctor)
{
	const wide_shared_ptr<int> x;
	EXPECT_FALSE(bool(x));
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_nullptr)
{
	const wide_shared_ptr<int> x{ nullptr };
	EXPECT_FALSE(bool(x));
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_ptr)
{
	int* const p = new int{ 123 };
	const wide_shared_ptr<int> x{ p };
	EXPECT_TRUE(bool(x));
	EXPECT_EQ(1u, x.use_count());
	EXPECT_EQ(p, x.get());
	EXPECT_THROW(x.collapse(), bad_collapse);
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_ptr_deleter)
{
	int* const p = new int{ 123 };
	const wide_shared_ptr<int> x{ p, std::default_delete<int>{} };
	EXPECT_TRUE(bool(x));
	EXPECT_EQ(1u, x.use_count());
	EXPECT_EQ(p, x.get());
	EXPECT_THROW(x.collapse(), bad_collapse);
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_nullptr_deleter)
{
	const wide_shared_ptr<int> x{ nullptr, std::default_delete<int>{} };
	EXPECT_FALSE(bool(x));
	EXPECT_EQ(1u, x.use_count());
	EXPECT_EQ(nullptr, x.get());
	EXPECT_THROW(x.collapse(), bad_collapse);
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_ptr_deleter_alloc)
{
	int* const p = new int{ 123 };
	const wide_shared_ptr<int> x{ p, std::default_delete<int>{}, counted_allocator<wide_shared_ptr<int>>{} };
	EXPECT_TRUE(bool(x));
	EXPECT_EQ(1u, x.use_count());
	EXPECT_EQ(p, x.get());
	EXPECT_THROW(x.collapse(), bad_collapse);
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_nullptr_deleter_alloc)
{
	const wide_shared_ptr<int> x{ nullptr, std::default_delete<int>{}, counted_allocator<wide_shared_ptr<int>>{} };
	EXPECT_FALSE(bool(x));
	EXPECT_EQ(1u, x.use_count());
	EXPECT_EQ(nullptr, x.get());
	EXPECT_THROW(x.collapse(), bad_collapse);
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_alias_copy)
{
	{
		using namespace SingleInheritance;
		const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		ASSERT_TRUE(bool(x));
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.get());

		const wide_shared_ptr<int> y{ x, &x->m_derived[0] };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(2u, y.use_count());
		EXPECT_EQ(&x->m_derived[0], y.get());
	}
	{
		const wide_shared_ptr<double> x{};

		int i{ 123 };
		wide_shared_ptr<int> y{ x, &i };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(&i, y.get());

		wide_shared_ptr<int> z{ y };
		EXPECT_TRUE(bool(z));
		EXPECT_EQ(0u, z.use_count());
		EXPECT_EQ(&i, z.get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_alias_move)
{
	{
		using namespace SingleInheritance;
		wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		ASSERT_TRUE(bool(x));
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.get());

		int* const p = &x->m_derived[0];
		const wide_shared_ptr<int> y{ std::move(x), &x->m_derived[0] };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(1u, y.use_count());
		EXPECT_EQ(p, y.get());
	}
	{
		int i{ 123 };
		wide_shared_ptr<int> x{ wide_shared_ptr<double>{}, &i };
		EXPECT_TRUE(bool(x));
		EXPECT_EQ(0u, x.use_count());
		EXPECT_EQ(&i, x.get());

		wide_shared_ptr<int> y{ x };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(&i, y.get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_alias_shared_ptr_copy)
{
	{
		using namespace SingleInheritance;
		const shared_ptr<Derived> x{ make_shared<Derived>() };
		ASSERT_TRUE(bool(x));
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.get());

		const wide_shared_ptr<int> y{ x, &x->m_derived[0] };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(2u, y.use_count());
		EXPECT_EQ(&x->m_derived[0], y.get());
	}
	{
		const shared_ptr<double> x{};

		int i{ 123 };
		wide_shared_ptr<int> y{ x, &i };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(&i, y.get());

		wide_shared_ptr<int> z{ y };
		EXPECT_TRUE(bool(z));
		EXPECT_EQ(0u, z.use_count());
		EXPECT_EQ(&i, z.get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_alias_shared_ptr_move)
{
	{
		using namespace SingleInheritance;
		shared_ptr<Derived> x{ make_shared<Derived>() };
		ASSERT_TRUE(bool(x));
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.get());

		int* const p = &x->m_derived[0];
		const wide_shared_ptr<int> y{ std::move(x), &x->m_derived[0] };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(1u, y.use_count());
		EXPECT_EQ(p, y.get());
	}
	{
		int i{ 123 };
		wide_shared_ptr<int> x{ shared_ptr<double>{}, &i };
		EXPECT_TRUE(bool(x));
		EXPECT_EQ(0u, x.use_count());
		EXPECT_EQ(&i, x.get());

		wide_shared_ptr<int> y{ x };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(&i, y.get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_copy)
{
	const wide_shared_ptr<int> x{ make_shared<int>() };
	ASSERT_TRUE(bool(x));
	ASSERT_EQ(1u, x.use_count());
	ASSERT_NE(nullptr, x.get());

	const wide_shared_ptr<int> y{ x };
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(2u, y.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(x.get(), y.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_move)
{
	wide_shared_ptr<int> x{ make_shared<int>() };
	int* const p = x.get();
	ASSERT_TRUE(bool(x));
	ASSERT_EQ(1u, x.use_count());
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(p, x.get());

	wide_shared_ptr<int> y{ std::move(x) };
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(1u, y.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(p, y.get());

	EXPECT_FALSE(bool(x));
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_copy_implicit)
{
	{
		using namespace MultipleInheritance;
		const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		const wide_shared_ptr<const Base> y{ x };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(2u, y.use_count());
		EXPECT_NE(nullptr, y.get());
		EXPECT_EQ(x.get(), y.get());
	}
	{
		using namespace VirtualInheritance;
		const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		const wide_shared_ptr<const Base> y{ x };
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(2u, y.use_count());
		EXPECT_NE(nullptr, y.get());
		EXPECT_EQ(x.get(), y.get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_ctor_move_implicit)
{
	{
		using namespace MultipleInheritance;
		const wide_shared_ptr<const Base> x{ make_shared<Derived>() };
		EXPECT_TRUE(bool(x));
		EXPECT_EQ(1u, x.use_count());
		EXPECT_NE(nullptr, x.get());
	}
	{
		using namespace VirtualInheritance;
		const wide_shared_ptr<const Base> x{ make_shared<Derived>() };
		EXPECT_TRUE(bool(x));
		EXPECT_EQ(1u, x.use_count());
		EXPECT_NE(nullptr, x.get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_assign_copy)
{
	const wide_shared_ptr<int> x{ make_shared<int>() };
	ASSERT_TRUE(bool(x));
	ASSERT_EQ(1u, x.use_count());
	ASSERT_NE(nullptr, x.get());

	wide_shared_ptr<int> y;
	ASSERT_FALSE(bool(y));
	ASSERT_EQ(0u, y.use_count());
	ASSERT_EQ(nullptr, y.get());

	const wide_shared_ptr<int> z;
	ASSERT_FALSE(bool(z));
	ASSERT_EQ(0u, z.use_count());
	ASSERT_EQ(nullptr, z.get());

	y = x;
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(2u, y.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(x.get(), y.get());

	y = z;
	EXPECT_FALSE(bool(y));
	EXPECT_EQ(0u, y.use_count());
	EXPECT_EQ(nullptr, y.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_assign_move)
{
	wide_shared_ptr<int> x{ make_shared<int>() };
	int* const p = x.get();
	ASSERT_TRUE(bool(x));
	ASSERT_EQ(1u, x.use_count());
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(p, x.get());

	wide_shared_ptr<int> y;
	ASSERT_FALSE(bool(y));
	ASSERT_EQ(0u, y.use_count());
	ASSERT_EQ(nullptr, y.get());

	wide_shared_ptr<int> z;
	ASSERT_FALSE(bool(z));
	ASSERT_EQ(0u, z.use_count());
	ASSERT_EQ(nullptr, z.get());

	y = std::move(x);
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(1u, y.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(p, y.get());

	EXPECT_FALSE(bool(x));
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.get());

	y = std::move(z);
	EXPECT_FALSE(bool(y));
	EXPECT_EQ(0u, y.use_count());
	EXPECT_EQ(nullptr, y.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_assign_copy_implicit)
{
	using namespace MultipleInheritance;
	const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
	wide_shared_ptr<const Base> y;
	y = x;
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(2u, x.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(x.get(), y.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_assign_move_implicit)
{
	using namespace MultipleInheritance;
	wide_shared_ptr<Derived> x{ make_shared<Derived>() };
	wide_shared_ptr<const Base> y;
	y = std::move(x);
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(1u, y.use_count());
	EXPECT_NE(nullptr, y.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_operator_arrow)
{
	struct Value
	{
		int m_value{ 123 };
	};
	const wide_shared_ptr<Value> x = make_shared<Value>();
	EXPECT_EQ(123, x->m_value);
}
TEST_F(sh_wide_shared_ptr, wide_shared_operator_deref)
{
	const wide_shared_ptr<int> x = make_shared<int>(123);
	EXPECT_EQ(123, *x);
}
TEST_F(sh_wide_shared_ptr, wide_shared_operator_index)
{
	const wide_shared_ptr<int[]> x = make_shared<int[]>(2);
	EXPECT_EQ(x[0], int{});
	EXPECT_EQ(x[1], int{});
}
TEST_F(sh_wide_shared_ptr, wide_shared_operator_bool)
{
	EXPECT_FALSE(bool(wide_shared_ptr<int>{}));
	EXPECT_TRUE(bool(make_shared<int>(123)));
}
TEST_F(sh_wide_shared_ptr, wide_shared_use_count)
{
	wide_shared_ptr<int> x;
	ASSERT_EQ(0u, x.use_count());

	x = make_shared<int>(123);
	ASSERT_EQ(1u, x.use_count());

	wide_shared_ptr<int> y{ x };
	EXPECT_EQ(2u, x.use_count());
	EXPECT_EQ(2u, y.use_count());

	x = nullptr;
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(1u, y.use_count());
}
TEST_F(sh_wide_shared_ptr, wide_shared_get)
{
	wide_shared_ptr<int> x;
	ASSERT_EQ(nullptr, x.get());

	x = make_shared<int>(123);
	ASSERT_NE(nullptr, x.get());

	wide_shared_ptr<int> y{ x };
	EXPECT_NE(nullptr, x.get());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(x.get(), y.get());

	x = nullptr;
	EXPECT_EQ(nullptr, x.get());
	EXPECT_NE(nullptr, y.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_reset)
{
	wide_shared_ptr<int> x{ make_shared<int>(123) };
	ASSERT_TRUE(bool(x));
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(1u, x.use_count());

	x.reset();
	ASSERT_FALSE(bool(x));
	ASSERT_EQ(nullptr, x.get());
	ASSERT_EQ(0u, x.use_count());
}
TEST_F(sh_wide_shared_ptr, wide_shared_reset_ptr)
{
	using namespace SingleInheritance;
	const wide_shared_ptr<Base> x{ make_shared<Base>() };
	ASSERT_TRUE(bool(x));
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(1u, x.use_count());

	wide_shared_ptr<Base> y{ x };
	ASSERT_TRUE(bool(y));
	ASSERT_NE(nullptr, y.get());
	ASSERT_EQ(2u, y.use_count());

	y.reset(new Derived{});
	ASSERT_TRUE(bool(x));
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(1u, x.use_count());
}
TEST_F(sh_wide_shared_ptr, wide_shared_reset_ptr_deleter)
{
	using namespace SingleInheritance;
	const wide_shared_ptr<Base> x{ make_shared<Base>() };
	ASSERT_TRUE(bool(x));
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(1u, x.use_count());

	wide_shared_ptr<Base> y{ x };
	ASSERT_TRUE(bool(y));
	ASSERT_NE(nullptr, y.get());
	ASSERT_EQ(2u, y.use_count());

	y.reset(new Derived{}, std::default_delete<Base>{});
	ASSERT_TRUE(bool(x));
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(1u, x.use_count());
}
TEST_F(sh_wide_shared_ptr, wide_shared_reset_ptr_deleter_alloc)
{
	using namespace SingleInheritance;
	const wide_shared_ptr<Base> x{ make_shared<Base>() };
	ASSERT_TRUE(bool(x));
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(1u, x.use_count());

	wide_shared_ptr<Base> y{ x };
	ASSERT_TRUE(bool(y));
	ASSERT_NE(nullptr, y.get());
	ASSERT_EQ(2u, y.use_count());

	y.reset(new Derived{}, std::default_delete<Base>{}, counted_allocator<wide_shared_ptr<Base>>{});
	ASSERT_TRUE(bool(x));
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(1u, x.use_count());
}
TEST_F(sh_wide_shared_ptr, wide_shared_swap)
{
	const wide_shared_ptr<int> a{ make_shared<int>(123) };
	const wide_shared_ptr<int> b{ make_shared<int>(456) };
	{
		wide_shared_ptr<int> x{ a };
		wide_shared_ptr<int> y{ b };
		ASSERT_EQ(x.get(), a.get());
		ASSERT_EQ(y.get(), b.get());

		x.swap(y);
		ASSERT_EQ(x.get(), b.get());
		ASSERT_EQ(y.get(), a.get());
	}
	{
		wide_shared_ptr<int> x{ a };
		wide_shared_ptr<int> y;
		ASSERT_EQ(x.get(), a.get());
		ASSERT_EQ(y.get(), nullptr);

		x.swap(y);
		ASSERT_EQ(x.get(), nullptr);
		ASSERT_EQ(y.get(), a.get());
	}
	{
		wide_shared_ptr<int> x;
		wide_shared_ptr<int> y{ b };
		ASSERT_EQ(x.get(), nullptr);
		ASSERT_EQ(y.get(), b.get());

		x.swap(y);
		ASSERT_EQ(x.get(), b.get());
		ASSERT_EQ(y.get(), nullptr);
	}
	{
		wide_shared_ptr<int> x;
		wide_shared_ptr<int> y;
		ASSERT_EQ(x.get(), nullptr);
		ASSERT_EQ(y.get(), nullptr);

		x.swap(y);
		ASSERT_EQ(x.get(), nullptr);
		ASSERT_EQ(y.get(), nullptr);
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_owner_before)
{
	const shared_ptr<int> x = make_shared<int>(123);
	const shared_ptr<int> y = make_shared<int>(456);
	const wide_shared_ptr<int> xo{ x };
	const wide_shared_ptr<int> yo{ y };
	EXPECT_FALSE(xo.owner_before(x));
	EXPECT_FALSE(xo.owner_before(xo));
	EXPECT_FALSE(yo.owner_before(y));
	EXPECT_FALSE(yo.owner_before(yo));
	EXPECT_NE(xo.owner_before(y), yo.owner_before(xo));
	EXPECT_NE(xo.owner_before(yo), yo.owner_before(x));
	EXPECT_NE(xo.owner_before(yo), yo.owner_before(xo));

	const weak_ptr<int> z{ x };
	EXPECT_FALSE(xo.owner_before(z));
	EXPECT_EQ(yo.owner_before(z), yo.owner_before(x));

	const wide_weak_ptr<int> zo{ xo };
	EXPECT_FALSE(xo.owner_before(zo));
	EXPECT_EQ(yo.owner_before(zo), yo.owner_before(xo));
}
TEST_F(sh_wide_shared_ptr, wide_shared_collapse_lvalue_valid)
{
	using namespace MultipleInheritance;
	const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
	const shared_ptr<Derived> y = x.collapse();
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(x.get(), y.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_collapse_lvalue_invalid)
{
	using namespace MultipleInheritance;
	const wide_shared_ptr<Base> x{ make_shared<Derived>() };
	EXPECT_THROW(x.collapse(), bad_collapse);
}
TEST_F(sh_wide_shared_ptr, wide_shared_collapse_rvalue_valid)
{
	using namespace MultipleInheritance;
	const wide_shared_ptr<Base> x{ make_shared<Derived>() };
	const shared_ptr<Derived> y = static_pointer_cast<Derived>(x).collapse();
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(x.get(), y.get());
}
TEST_F(sh_wide_shared_ptr, wide_shared_collapse_rvalue_invalid)
{
	using namespace MultipleInheritance;
	wide_shared_ptr<Base> x{ make_shared<Derived>() };
	EXPECT_THROW(std::move(x).collapse(), bad_collapse);
}

TEST_F(sh_wide_shared_ptr, wide_weak_ctor)
{
	const wide_weak_ptr<int> x;
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.lock().get());
}
TEST_F(sh_wide_shared_ptr, wide_weak_ctor_copy)
{
	const wide_shared_ptr<int> x{ make_shared<int>(123) };
	const wide_weak_ptr<int> y{ x };
	EXPECT_EQ(1u, y.use_count());
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_NE(nullptr, y.lock().get());

	const wide_weak_ptr<int> z{ y };
	EXPECT_EQ(1u, z.use_count());
	EXPECT_EQ(x.get(), z.lock().get());
	EXPECT_NE(nullptr, z.lock().get());
}
TEST_F(sh_wide_shared_ptr, wide_weak_ctor_move)
{
	const wide_shared_ptr<int> x{ make_shared<int>(123) };
	wide_weak_ptr<int> y{ x };
	EXPECT_EQ(1u, y.use_count());
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_NE(nullptr, y.lock().get());

	const wide_weak_ptr<int> z{ std::move(y) };
	EXPECT_EQ(1u, z.use_count());
	EXPECT_EQ(x.get(), z.lock().get());
	EXPECT_NE(nullptr, z.lock().get());

	EXPECT_EQ(0u, y.use_count());
	EXPECT_EQ(nullptr, y.lock().get());
}
TEST_F(sh_wide_shared_ptr, wide_weak_ctor_shared_ptr)
{
	const wide_shared_ptr<int> x{ make_shared<int>(123) };
	const wide_weak_ptr<int> y{ x };
	EXPECT_EQ(1u, y.use_count());
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_NE(nullptr, y.lock().get());
}
TEST_F(sh_wide_shared_ptr, wide_weak_ctor_copy_wide_implicit)
{
	{
		using namespace MultipleInheritance;
		const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		const wide_weak_ptr<const Base> y{ x };
		EXPECT_EQ(1u, y.use_count());
		EXPECT_NE(nullptr, y.lock().get());
		EXPECT_EQ(x.get(), y.lock().get());
	}
	{
		using namespace VirtualInheritance;
		const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		const wide_weak_ptr<const Base> y{ x };
		EXPECT_EQ(1u, y.use_count());
		EXPECT_NE(nullptr, y.lock().get());
		EXPECT_EQ(x.get(), y.lock().get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_ctor_copy_weak_ptr_implicit)
{
	{
		using namespace MultipleInheritance;
		const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		const wide_weak_ptr<Derived> y{ x };
		const wide_weak_ptr<const Base> z{ y };
		EXPECT_EQ(1u, z.use_count());
		EXPECT_NE(nullptr, z.lock().get());
		EXPECT_EQ(x.get(), z.lock().get());
	}
	{
		// Force wide_weak_ptr's copy constructor to lock.
		using namespace VirtualInheritance;
		wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		const wide_weak_ptr<Derived> y{ x };
		{
			const wide_weak_ptr<const Base> z{ y };
			EXPECT_EQ(1u, z.use_count());
			EXPECT_NE(nullptr, z.lock().get());
			EXPECT_EQ(x.get(), z.lock().get());
		}
		x.reset();
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());
		{
			const wide_weak_ptr<const Base> z{ y };
			EXPECT_EQ(0u, z.use_count());
			EXPECT_EQ(nullptr, z.lock().get());
		}
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_ctor_move_implicit)
{
	{
		using namespace MultipleInheritance;
		const wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		wide_weak_ptr<Derived> y{ x };
		const wide_weak_ptr<const Base> z{ std::move(y) };
		EXPECT_EQ(1u, z.use_count());
		EXPECT_NE(nullptr, z.lock().get());
	}
	{
		// Force wide_weak_ptr's copy constructor to lock.
		using namespace VirtualInheritance;
		wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		{
			wide_weak_ptr<Derived> y{ x };
			const wide_weak_ptr<const Base> z{ std::move(y) };
			EXPECT_EQ(1u, z.use_count());
			EXPECT_NE(nullptr, z.lock().get());
		}
		{
			wide_weak_ptr<Derived> y{ x };
			x.reset();
			ASSERT_EQ(0u, y.use_count());
			ASSERT_EQ(nullptr, y.lock().get());

			const wide_weak_ptr<const Base> z{ std::move(y) };
			EXPECT_EQ(0u, z.use_count());
			EXPECT_EQ(nullptr, z.lock().get());
		}
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_assign_copy)
{
	{
		const wide_shared_ptr<int> w{ make_shared<int>() };
		const wide_weak_ptr<int> x{ w };
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.lock().get());

		wide_weak_ptr<int> y;
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());

		const wide_weak_ptr<int> z;
		ASSERT_EQ(0u, z.use_count());
		ASSERT_EQ(nullptr, z.lock().get());

		y = x;
		EXPECT_EQ(1u, y.use_count());
		EXPECT_NE(nullptr, y.lock().get());
		EXPECT_EQ(x.lock().get(), y.lock().get());

		y = z;
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(nullptr, y.lock().get());
	}
	{
		wide_shared_ptr<int> w{ make_shared<int>() };
		const wide_weak_ptr<int> x{ w };
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.lock().get());

		wide_weak_ptr<int> y;
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());

		const wide_weak_ptr<int> z;
		ASSERT_EQ(0u, z.use_count());
		ASSERT_EQ(nullptr, z.lock().get());

		w.reset();
		y = x;
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(nullptr, y.lock().get());

		y = z;
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(nullptr, y.lock().get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_assign_move)
{
	{
		const wide_shared_ptr<int> w{ make_shared<int>() };
		wide_weak_ptr<int> x{ w };
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.lock().get());
		ASSERT_EQ(w.get(), x.lock().get());

		wide_weak_ptr<int> y;
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());

		wide_weak_ptr<int> z;
		ASSERT_EQ(0u, z.use_count());
		ASSERT_EQ(nullptr, z.lock().get());

		y = std::move(x);
		EXPECT_EQ(1u, y.use_count());
		EXPECT_NE(nullptr, y.lock().get());
		EXPECT_EQ(w.get(), y.lock().get());

		EXPECT_EQ(0u, x.use_count());
		EXPECT_EQ(nullptr, x.lock().get());

		y = std::move(z);
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(nullptr, y.lock().get());
	}
	{
		wide_shared_ptr<int> w{ make_shared<int>() };
		wide_weak_ptr<int> x{ w };
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.lock().get());
		ASSERT_EQ(w.get(), x.lock().get());

		wide_weak_ptr<int> y;
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());

		wide_weak_ptr<int> z;
		ASSERT_EQ(0u, z.use_count());
		ASSERT_EQ(nullptr, z.lock().get());

		w.reset();

		y = std::move(x);
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(nullptr, y.lock().get());

		EXPECT_EQ(0u, x.use_count());
		EXPECT_EQ(nullptr, x.lock().get());

		y = std::move(z);
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(nullptr, y.lock().get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_assign_copy_wide_implicit)
{
	using namespace MultipleInheritance;
	{
		wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		wide_weak_ptr<const Base> y;
		y = x;
		EXPECT_EQ(1u, y.use_count());
		EXPECT_NE(nullptr, y.lock().get());
		EXPECT_EQ(x.get(), y.lock().get());
	}
	{
		wide_shared_ptr<Derived> x{ make_shared<Derived>() };
		x.reset();
		wide_weak_ptr<const Base> y;
		y = x;
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(nullptr, y.lock().get());
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_assign_copy_weak_ptr_implicit)
{
	{
		using namespace MultipleInheritance;
		{
			wide_shared_ptr<Derived> x{ make_shared<Derived>() };
			wide_weak_ptr<Derived> y{ x };
			wide_weak_ptr<const Base> z;
			z = y;
			EXPECT_EQ(1u, z.use_count());
			EXPECT_NE(nullptr, z.lock().get());
			EXPECT_EQ(x.get(), z.lock().get());
		}
		{
			wide_shared_ptr<Derived> x{ make_shared<Derived>() };
			wide_weak_ptr<Derived> y{ x };
			x.reset();
			wide_weak_ptr<const Base> z;
			z = y;
			EXPECT_EQ(0u, z.use_count());
			EXPECT_EQ(nullptr, z.lock().get());
		}
	}
	{
		using namespace VirtualInheritance;
		static_assert(false == is_static_cast_inert_v<Base*, Derived*>);
		{
			wide_shared_ptr<Derived> x{ make_shared<Derived>() };
			wide_weak_ptr<Derived> y{ x };
			wide_weak_ptr<const Base> z;
			z = y;
			EXPECT_EQ(1u, z.use_count());
			EXPECT_NE(nullptr, z.lock().get());
			EXPECT_EQ(x.get(), z.lock().get());
		}
		{
			wide_shared_ptr<Derived> x{ make_shared<Derived>() };
			wide_weak_ptr<Derived> y{ x };
			x.reset();
			wide_weak_ptr<const Base> z;
			z = y;
			EXPECT_EQ(0u, z.use_count());
			EXPECT_EQ(nullptr, z.lock().get());
		}
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_assign_move_implicit)
{
	{
		using namespace MultipleInheritance;
		{
			wide_shared_ptr<Derived> x{ make_shared<Derived>() };
			wide_weak_ptr<Derived> y{ x };
			wide_weak_ptr<const Base> z;
			z = std::move(y);
			EXPECT_EQ(1u, z.use_count());
			EXPECT_NE(nullptr, z.lock().get());
			EXPECT_EQ(x.get(), z.lock().get());
		}
		{
			wide_shared_ptr<Derived> x{ make_shared<Derived>() };
			wide_weak_ptr<Derived> y{ x };
			x.reset();
			wide_weak_ptr<const Base> z;
			z = std::move(y);
			EXPECT_EQ(0u, z.use_count());
			EXPECT_EQ(nullptr, z.lock().get());
		}
	}
	{
		using namespace VirtualInheritance;
		static_assert(false == is_static_cast_inert_v<Base*, Derived*>);
		{
			wide_shared_ptr<Derived> x{ make_shared<Derived>() };
			wide_weak_ptr<Derived> y{ x };
			wide_weak_ptr<const Base> z;
			z = std::move(y);
			EXPECT_EQ(1u, z.use_count());
			EXPECT_NE(nullptr, z.lock().get());
			EXPECT_EQ(x.get(), z.lock().get());
		}
		{
			wide_shared_ptr<Derived> x{ make_shared<Derived>() };
			wide_weak_ptr<Derived> y{ x };
			x.reset();
			wide_weak_ptr<const Base> z;
			z = std::move(y);
			EXPECT_EQ(0u, z.use_count());
			EXPECT_EQ(nullptr, z.lock().get());
		}
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_use_count)
{
	wide_shared_ptr<int> x{ make_shared<int>(123) };
	wide_shared_ptr<int> y{ x };
	wide_weak_ptr<int> z{ x };
	ASSERT_EQ(2u, z.use_count());

	x.reset();
	ASSERT_EQ(1u, z.use_count());

	{
		wide_weak_ptr<int> w{ z };
		ASSERT_EQ(1u, z.use_count());
	}

	y.reset();
	ASSERT_EQ(0u, z.use_count());
}
TEST_F(sh_wide_shared_ptr, wide_weak_expired)
{
	wide_shared_ptr<int> x{ make_shared<int>(123) };
	wide_weak_ptr<int> y{ x };
	ASSERT_EQ(x.get(), y.lock().get());
	ASSERT_NE(nullptr, y.lock().get());
	ASSERT_FALSE(y.expired());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	ASSERT_TRUE(y.expired());
}
TEST_F(sh_wide_shared_ptr, wide_weak_lock)
{
	wide_shared_ptr<int> x{ make_shared<int>(123) };
	wide_weak_ptr<int> y{ x };
	ASSERT_EQ(x.get(), y.lock().get());
	ASSERT_NE(nullptr, y.lock().get());
	ASSERT_EQ(1u, y.use_count());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	EXPECT_EQ(0u, y.use_count());
}
TEST_F(sh_wide_shared_ptr, wide_weak_reset)
{
	{
		wide_shared_ptr<int> x{ make_shared<int>(123) };
		wide_weak_ptr<int> y{ x };
		ASSERT_NE(nullptr, y.lock().get());
		ASSERT_EQ(1u, y.use_count());

		y.reset();
		EXPECT_EQ(nullptr, y.lock().get());
		EXPECT_EQ(0u, y.use_count());
	}
	{
		wide_shared_ptr<int> x{ make_shared<int>(123) };
		wide_weak_ptr<int> y{ x };
		ASSERT_NE(nullptr, y.lock().get());
		ASSERT_EQ(1u, y.use_count());

		x.reset();
		ASSERT_EQ(nullptr, y.lock().get());
		ASSERT_EQ(0u, y.use_count());

		y.reset();
		EXPECT_EQ(nullptr, y.lock().get());
		EXPECT_EQ(0u, y.use_count());
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_swap)
{
	{
		const wide_shared_ptr<int> a{ make_shared<int>(123) };
		const wide_shared_ptr<int> b{ make_shared<int>(456) };
		{
			wide_weak_ptr<int> x{ a };
			wide_weak_ptr<int> y{ b };
			ASSERT_EQ(x.lock().get(), a.get());
			ASSERT_EQ(y.lock().get(), b.get());

			x.swap(y);
			ASSERT_EQ(x.lock().get(), b.get());
			ASSERT_EQ(y.lock().get(), a.get());
		}
		{
			wide_weak_ptr<int> x{ a };
			wide_weak_ptr<int> y;
			ASSERT_EQ(x.lock().get(), a.get());
			ASSERT_EQ(y.lock().get(), nullptr);

			x.swap(y);
			ASSERT_EQ(x.lock().get(), nullptr);
			ASSERT_EQ(y.lock().get(), a.get());
		}
		{
			wide_weak_ptr<int> x;
			wide_weak_ptr<int> y{ b };
			ASSERT_EQ(x.lock().get(), nullptr);
			ASSERT_EQ(y.lock().get(), b.get());

			x.swap(y);
			ASSERT_EQ(x.lock().get(), b.get());
			ASSERT_EQ(y.lock().get(), nullptr);
		}
	}
	{
		wide_shared_ptr<int> a{ make_shared<int>(123) };
		wide_shared_ptr<int> b{ make_shared<int>(456) };
		wide_weak_ptr<int> x{ a };
		wide_weak_ptr<int> y{ b };
		ASSERT_EQ(x.lock().get(), a.get());
		ASSERT_EQ(y.lock().get(), b.get());
		a.reset();
		b.reset();

		x.swap(y);
		ASSERT_EQ(x.lock().get(), b.get());
		ASSERT_EQ(y.lock().get(), a.get());
	}
	{
		wide_shared_ptr<int> a{ make_shared<int>(123) };
		wide_weak_ptr<int> x{ a };
		wide_weak_ptr<int> y;
		ASSERT_EQ(x.lock().get(), a.get());
		ASSERT_EQ(y.lock().get(), nullptr);
		a.reset();

		x.swap(y);
		ASSERT_EQ(x.lock().get(), nullptr);
		ASSERT_EQ(y.lock().get(), a.get());
	}
	{
		wide_shared_ptr<int> b{ make_shared<int>(456) };
		wide_weak_ptr<int> x;
		wide_weak_ptr<int> y{ b };
		ASSERT_EQ(x.lock().get(), nullptr);
		ASSERT_EQ(y.lock().get(), b.get());
		b.reset();

		x.swap(y);
		ASSERT_EQ(x.lock().get(), b.get());
		ASSERT_EQ(y.lock().get(), nullptr);
	}
	{
		wide_weak_ptr<int> x;
		wide_weak_ptr<int> y;
		ASSERT_EQ(x.lock().get(), nullptr);
		ASSERT_EQ(y.lock().get(), nullptr);

		x.swap(y);
		ASSERT_EQ(x.lock().get(), nullptr);
		ASSERT_EQ(y.lock().get(), nullptr);
	}
}
TEST_F(sh_wide_shared_ptr, wide_weak_owner_before)
{
	const shared_ptr<int> x = make_shared<int>(123);
	const shared_ptr<int> y = make_shared<int>(456);
	const weak_ptr<int> z{ x };
	const weak_ptr<int> w{ y };
	const wide_shared_ptr<int> xo{ x };
	const wide_shared_ptr<int> yo{ y };
	const wide_weak_ptr<int> zo{ x };
	const wide_weak_ptr<int> wo{ y };

	EXPECT_FALSE(zo.owner_before(x));
	EXPECT_FALSE(zo.owner_before(xo));
	EXPECT_FALSE(zo.owner_before(z));
	EXPECT_FALSE(zo.owner_before(zo));
	EXPECT_EQ(zo.owner_before(y), x.owner_before(y));
	EXPECT_EQ(zo.owner_before(yo), x.owner_before(yo));
	EXPECT_EQ(zo.owner_before(wo), x.owner_before(wo));
}

TEST_F(sh_wide_shared_ptr, make_shared)
{
	make_shared<extended_alignment>();
	make_shared<extended_alignment>(123);
}
TEST_F(sh_wide_shared_ptr, make_shared_throw)
{
	throws_on_counter::throw_counter = 0;
	EXPECT_THROW(make_shared<throws_on_counter>(), configurable_exception);
}
TEST_F(sh_wide_shared_ptr, make_shared_for_overwrite)
{
	make_shared_for_overwrite<extended_alignment>();
}
TEST_F(sh_wide_shared_ptr, make_shared_for_overwrite_throw)
{
	throws_on_counter::throw_counter = 0;
	EXPECT_THROW(make_shared_for_overwrite<throws_on_counter>(), configurable_exception);
}
TEST_F(sh_wide_shared_ptr, make_shared_array)
{
	make_shared<extended_alignment[]>(2);
	make_shared<extended_alignment[]>(2, extended_alignment{ 123 });
	make_shared<extended_alignment[2]>();
	make_shared<extended_alignment[2]>(extended_alignment{ 123 });
	make_shared<extended_alignment[][2]>(2);
	make_shared<extended_alignment[2][2]>();
}
TEST_F(sh_wide_shared_ptr, make_shared_array_throw)
{
	throws_on_counter::throw_counter = 1;
	EXPECT_THROW(make_shared<throws_on_counter[]>(3), configurable_exception);
}
TEST_F(sh_wide_shared_ptr, make_shared_for_overwrite_array)
{
	make_shared_for_overwrite<extended_alignment[]>(3);
	make_shared_for_overwrite<extended_alignment[3]>();
}
TEST_F(sh_wide_shared_ptr, make_shared_for_overwrite_array_throw)
{
	throws_on_counter::throw_counter = 1;
	EXPECT_THROW(make_shared_for_overwrite<throws_on_counter[]>(3), configurable_exception);
}
TEST_F(sh_wide_shared_ptr, allocate_shared)
{
	using namespace MultipleInheritance;
	{
		counted_allocator<Derived> alloc;
		wide_shared_ptr<Base> x{ sh::allocate_shared<Derived>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());

		x.reset();
		x = sh::allocate_shared<Derived>(alloc);
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());

		wide_weak_ptr<Base> y{ x };
		EXPECT_EQ(x.get(), y.lock().get());
		EXPECT_EQ(1u, y.use_count());

		x.reset();
		EXPECT_EQ(nullptr, y.lock().get());
		EXPECT_EQ(0u, y.use_count());
	}
	{
		stateful_allocator<Derived> alloc{ 123 };
		wide_shared_ptr<Base> x{ sh::allocate_shared<Derived>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());

		x.reset();
		x = sh::allocate_shared<Derived>(alloc);
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());

		wide_weak_ptr<Base> y{ x };
		EXPECT_EQ(x.get(), y.lock().get());
		EXPECT_EQ(1u, y.use_count());

		x.reset();
		EXPECT_EQ(nullptr, y.lock().get());
		EXPECT_EQ(0u, y.use_count());
	}
	{
		counted_allocator<extended_alignment> alloc;
		wide_shared_ptr<extended_alignment> x{ sh::allocate_shared<extended_alignment>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		stateful_allocator<extended_alignment> alloc{ 123 };
		wide_shared_ptr<extended_alignment> x{ sh::allocate_shared<extended_alignment>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
}
TEST_F(sh_wide_shared_ptr, allocate_shared_array)
{
	using namespace MultipleInheritance;
	{
		counted_allocator<Derived> alloc;
		wide_shared_ptr<Derived[]> x{ sh::allocate_shared<Derived[]>(alloc, 2u) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		counted_allocator<Derived> alloc;
		wide_shared_ptr<Derived[]> x{ sh::allocate_shared<Derived[2]>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		counted_allocator<Derived> alloc;
		wide_shared_ptr<Derived[2]> x{ sh::allocate_shared<Derived[2]>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		constexpr extended_alignment value{ 123 };
		counted_allocator<extended_alignment> alloc;
		wide_shared_ptr<extended_alignment[]> x{ sh::allocate_shared<extended_alignment[]>(alloc, 2, value) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
		EXPECT_EQ(x[0].m_value, value.m_value);
		EXPECT_EQ(x[1].m_value, value.m_value);
	}
	{
		constexpr extended_alignment value{ 123 };
		counted_allocator<extended_alignment> alloc;
		wide_shared_ptr<extended_alignment[2]> x{ sh::allocate_shared<extended_alignment[2]>(alloc, value) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
		EXPECT_EQ(x[0].m_value, value.m_value);
		EXPECT_EQ(x[1].m_value, value.m_value);
	}
	{
		stateful_allocator<Derived> alloc;
		wide_shared_ptr<Derived[]> x{ sh::allocate_shared<Derived[]>(alloc, 2u) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
}
TEST_F(sh_wide_shared_ptr, allocate_shared_for_overwrite)
{
	constexpr char prefill = '\xff';
	prefill_allocator<char, prefill> alloc;
	wide_shared_ptr<char> x{ sh::allocate_shared_for_overwrite<char>(alloc) };
	general_allocations::get().m_construct_default += 1;
	ASSERT_TRUE(bool(x));
	EXPECT_EQ(prefill, *x);

	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	x.reset();
	x = sh::allocate_shared_for_overwrite<char>(alloc);
	general_allocations::get().m_construct_default += 1;
	ASSERT_TRUE(bool(x));
	EXPECT_EQ(prefill, *x);

	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	wide_weak_ptr<char> y{ x };
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_EQ(1u, y.use_count());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	EXPECT_EQ(0u, y.use_count());
}
TEST_F(sh_wide_shared_ptr, allocate_shared_for_overwrite_extended_alignment)
{
	constexpr char prefill = '\xff';
	prefill_allocator<char, prefill> alloc;
	wide_shared_ptr<extended_alignment> x{ sh::allocate_shared_for_overwrite<extended_alignment>(alloc) };
	general_allocations::get().m_construct_default += 1;
	ASSERT_TRUE(bool(x));
	EXPECT_EQ(prefill, x->m_value);

	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	x.reset();
	x = sh::allocate_shared_for_overwrite<extended_alignment>(alloc);
	general_allocations::get().m_construct_default += 1;
	ASSERT_TRUE(bool(x));
	EXPECT_EQ(prefill, x->m_value);

	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	wide_weak_ptr<extended_alignment> y{ x };
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_EQ(1u, y.use_count());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	EXPECT_EQ(0u, y.use_count());
}
TEST_F(sh_wide_shared_ptr, allocate_shared_for_overwrite_array)
{
	constexpr char prefill = '\xff';
	prefill_allocator<char, prefill> alloc;
	wide_shared_ptr<char[]> x{ sh::allocate_shared_for_overwrite<char[]>(alloc, 4) };
	general_allocations::get().m_construct_default += 4;
	ASSERT_TRUE(bool(x));
	EXPECT_EQ(prefill, x[0]);
	EXPECT_EQ(prefill, x[1]);
	EXPECT_EQ(prefill, x[2]);
	EXPECT_EQ(prefill, x[3]);

	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	x.reset();
	x = sh::allocate_shared_for_overwrite<char[4]>(alloc);
	general_allocations::get().m_construct_default += 4;
	ASSERT_TRUE(bool(x));
	EXPECT_EQ(prefill, x[0]);
	EXPECT_EQ(prefill, x[1]);
	EXPECT_EQ(prefill, x[2]);
	EXPECT_EQ(prefill, x[3]);

	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	wide_weak_ptr<char[]> y{ x };
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_EQ(1u, y.use_count());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	EXPECT_EQ(0u, y.use_count());
}
TEST_F(sh_wide_shared_ptr, allocate_shared_for_overwrite_array_extended_alignment)
{
	constexpr char prefill = '\xff';
	prefill_allocator<char, prefill> alloc;
	wide_shared_ptr<extended_alignment[]> x{ sh::allocate_shared_for_overwrite<extended_alignment[]>(alloc, 4) };
	general_allocations::get().m_construct_default += 4;
	ASSERT_TRUE(bool(x));
	EXPECT_EQ(prefill, x[0].m_value);
	EXPECT_EQ(prefill, x[1].m_value);
	EXPECT_EQ(prefill, x[2].m_value);
	EXPECT_EQ(prefill, x[3].m_value);

	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	x.reset();
	x = sh::allocate_shared_for_overwrite<extended_alignment[4]>(alloc);
	general_allocations::get().m_construct_default += 4;
	ASSERT_TRUE(bool(x));
	EXPECT_EQ(prefill, x[0].m_value);
	EXPECT_EQ(prefill, x[1].m_value);
	EXPECT_EQ(prefill, x[2].m_value);
	EXPECT_EQ(prefill, x[3].m_value);

	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	wide_weak_ptr<extended_alignment[]> y{ x };
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_EQ(1u, y.use_count());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	EXPECT_EQ(0u, y.use_count());
}

TEST_F(sh_wide_shared_ptr, wide_shared_const_cast)
{
	{
		const wide_shared_ptr<int> x{ make_shared<int>(123) };
		const wide_shared_ptr<const int> y{ const_pointer_cast<const int>(x)  };
		const wide_shared_ptr<int> z{ const_pointer_cast<int>(y)  };
		EXPECT_EQ(x.get(), y.get());
		EXPECT_EQ(x.get(), z.get());
	}
	{
		using namespace MultipleInheritance;
		shared_ptr<Derived> x{ make_shared<Derived>() };
		{
			const shared_ptr<const Derived> y = const_pointer_cast<const Derived>(x);
			EXPECT_TRUE(bool(y));
			EXPECT_EQ(x.get(), y.get());
		}
		{
			Derived* const p = x.get();
			const shared_ptr<const Derived> y = const_pointer_cast<const Derived>(std::move(x));
			EXPECT_TRUE(bool(y));
			EXPECT_EQ(p, y.get());
		}
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_dynamic_cast)
{
	{
		using namespace SingleInheritance;
		const wide_shared_ptr<Base> x{ make_shared<Derived>() };
		const wide_shared_ptr<Derived> y{ dynamic_pointer_cast<Derived>(x) };
		EXPECT_EQ(x.get(), y.get());
		EXPECT_NE(nullptr, y.get());
		const wide_shared_ptr<DifferentDerived> z{ dynamic_pointer_cast<DifferentDerived>(x) };
		EXPECT_EQ(nullptr, z.get());
	}
	{
		using namespace MultipleInheritance;
		const shared_ptr<Derived> x{ make_shared<Derived>() };
		wide_shared_ptr<Base> y = dynamic_pointer_cast<Base>(x);
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(x.get(), y.get());
		{
			const wide_shared_ptr<Derived> z = dynamic_pointer_cast<Derived>(y);
			EXPECT_TRUE(bool(z));
			EXPECT_EQ(y.get(), z.get());
			EXPECT_EQ(x.get(), z.get());
		}
		{
			const wide_shared_ptr<Derived> z = dynamic_pointer_cast<Derived>(std::move(y));
			EXPECT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
		}
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_static_cast)
{
	{
		using namespace SingleInheritance;
		const wide_shared_ptr<Base> x{ make_shared<Derived>() };
		const wide_shared_ptr<Derived> y{ static_pointer_cast<Derived>(x) };
		EXPECT_EQ(x.get(), y.get());
	}
	{
		using namespace MultipleInheritance;
		const shared_ptr<Derived> x{ make_shared<Derived>() };
		wide_shared_ptr<Base> y = static_pointer_cast<Base>(x);
		EXPECT_TRUE(bool(y));
		EXPECT_EQ(x.get(), y.get());
		{
			const wide_shared_ptr<Derived> z = static_pointer_cast<Derived>(y);
			EXPECT_TRUE(bool(z));
			EXPECT_EQ(y.get(), z.get());
			EXPECT_EQ(x.get(), z.get());
		}
		{
			wide_shared_ptr<Derived> z = static_pointer_cast<Derived>(std::move(y));
			EXPECT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
		}
	}
}
TEST_F(sh_wide_shared_ptr, wide_shared_reinterpret_cast)
{
	const wide_shared_ptr<int> x{ make_shared<int>() };
	const wide_shared_ptr<float> y{ reinterpret_pointer_cast<float>(x) };
	EXPECT_EQ(
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(x.get())),
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(y.get()))
	);
}

TEST_F(sh_wide_shared_ptr, wide_shared_get_deleter)
{
	{
		wide_shared_ptr<int> x{ make_shared<int>() };
		void* const del = get_deleter<void>(x);
		EXPECT_EQ(nullptr, del);
	}
	{
		int i{ 123 };
		wide_shared_ptr<int> x{ shared_ptr<float>{}, &i };
		void* const del = get_deleter<void>(x);
		EXPECT_EQ(nullptr, del);
	}
	{
		int deleter_called{ 0 };
		const auto deleter = [&deleter_called](int* const ptr) -> void
		{
			++deleter_called;
			delete ptr;
		};
		{
			wide_shared_ptr<int> x{ new int{ 123 }, deleter };
			decltype(deleter)* del = get_deleter<decltype(deleter)>(x);
			ASSERT_NE(nullptr, del);
			ASSERT_EQ(0, deleter_called);

			(*del)(nullptr);
			EXPECT_EQ(1, deleter_called);
		}
		EXPECT_EQ(2, deleter_called);
	}
	{
		const auto deleter = [](int* const ptr) -> void
		{
			delete ptr;
		};
		void (*func)(int*) = deleter;

		wide_shared_ptr<int> x{ new int{ 123 }, func };
		void (**func2)(int*) = get_deleter<decltype(func)>(x);
		ASSERT_NE(nullptr, func2);
		EXPECT_EQ(func, *func2);
	}
}

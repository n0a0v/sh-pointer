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
#include <sh/shared_ptr.hpp>

using sh::const_pointer_cast;
using sh::dynamic_pointer_cast;
using sh::get_deleter;
using sh::is_pointer_interconvertible_v;
using sh::make_shared;
using sh::reinterpret_pointer_cast;
using sh::shared_ptr;
using sh::static_pointer_cast;
using sh::weak_ptr;

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

	namespace SimpleInheritance
	{
		struct Base
		{
			std::unique_ptr<int> m_base = std::make_unique<int>(123);
		};
		struct Derived : Base
		{ };
	} // SimpleInheritance

	namespace SingleInheritance
	{
		struct Base
		{
			std::unique_ptr<int> m_base = std::make_unique<int>(123);

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

	struct configurable_exception
	{ };

	struct throws_on_counter
	{
		static int throw_counter;
		static int current_counter;

		throws_on_counter()
			: m_value{ current_counter++ }
		{
			if (throw_counter >= 0 && m_value == throw_counter)
			{
				throw configurable_exception{};
			}
		}
		throws_on_counter(const throws_on_counter&)
			: m_value{ current_counter++ }
		{
			if (throw_counter >= 0 && m_value == throw_counter)
			{
				throw configurable_exception{};
			}
		}

		int m_value;
	};

	int throws_on_counter::throw_counter = -1;
	int throws_on_counter::current_counter = 0;

	class sh_shared_ptr : public ::testing::Test
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

			throws_on_counter::current_counter = 0;
			throws_on_counter::throw_counter = -1;
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

TEST_F(sh_shared_ptr, shared_ptr_ctor)
{
	const shared_ptr<int> x;
	EXPECT_FALSE(bool(x));
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.get());
}
TEST_F(sh_shared_ptr, shared_ptr_ctor_nullptr)
{
	const shared_ptr<int> x{ nullptr };
	EXPECT_FALSE(bool(x));
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.get());
}
TEST_F(sh_shared_ptr, shared_ptr_ctor_copy)
{
	const shared_ptr<int> x{ make_shared<int>() };
	ASSERT_TRUE(bool(x));
	ASSERT_EQ(1u, x.use_count());
	ASSERT_NE(nullptr, x.get());

	const shared_ptr<int> y{ x };
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(2u, y.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(x.get(), y.get());
}
TEST_F(sh_shared_ptr, shared_ptr_ctor_move)
{
	shared_ptr<int> x{ make_shared<int>() };
	int* const p = x.get();
	ASSERT_TRUE(bool(x));
	ASSERT_EQ(1u, x.use_count());
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(p, x.get());

	shared_ptr<int> y{ std::move(x) };
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(1u, y.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(p, y.get());

	EXPECT_FALSE(bool(x));
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.get());
}
TEST_F(sh_shared_ptr, shared_ptr_ctor_copy_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	const shared_ptr<Derived> x{ make_shared<Derived>() };
	const shared_ptr<const Base> y{ x };
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(2u, y.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(x.get(), y.get());
}
TEST_F(sh_shared_ptr, shared_ptr_ctor_move_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	const shared_ptr<const Base> x{ make_shared<Derived>() };
	EXPECT_TRUE(bool(x));
	EXPECT_EQ(1u, x.use_count());
	EXPECT_NE(nullptr, x.get());
}
TEST_F(sh_shared_ptr, shared_ptr_assign_copy)
{
	const shared_ptr<int> x{ make_shared<int>() };
	ASSERT_TRUE(bool(x));
	ASSERT_EQ(1u, x.use_count());
	ASSERT_NE(nullptr, x.get());

	shared_ptr<int> y;
	ASSERT_FALSE(bool(y));
	ASSERT_EQ(0u, y.use_count());
	ASSERT_EQ(nullptr, y.get());

	const shared_ptr<int> z;
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
TEST_F(sh_shared_ptr, shared_ptr_assign_move)
{
	shared_ptr<int> x{ make_shared<int>() };
	int* const p = x.get();
	ASSERT_TRUE(bool(x));
	ASSERT_EQ(1u, x.use_count());
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(p, x.get());

	shared_ptr<int> y;
	ASSERT_FALSE(bool(y));
	ASSERT_EQ(0u, y.use_count());
	ASSERT_EQ(nullptr, y.get());

	shared_ptr<int> z;
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
TEST_F(sh_shared_ptr, shared_ptr_assign_copy_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	const shared_ptr<Derived> x{ make_shared<Derived>() };
	shared_ptr<const Base> y;
	y = x;
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(2u, x.use_count());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(x.get(), y.get());
}
TEST_F(sh_shared_ptr, shared_ptr_assign_move_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	shared_ptr<Derived> x{ make_shared<Derived>() };
	shared_ptr<const Base> y;
	y = std::move(x);
	EXPECT_TRUE(bool(y));
	EXPECT_EQ(1u, y.use_count());
	EXPECT_NE(nullptr, y.get());
}
TEST_F(sh_shared_ptr, shared_ptr_operator_arrow)
{
	struct Value
	{
		int m_value{ 123 };
	};
	const shared_ptr<Value> x = make_shared<Value>();
	EXPECT_EQ(123, x->m_value);
}
TEST_F(sh_shared_ptr, shared_ptr_operator_deref)
{
	const shared_ptr<int> x = make_shared<int>(123);
	EXPECT_EQ(123, *x);
}
TEST_F(sh_shared_ptr, shared_ptr_operator_index)
{
	const shared_ptr<int[]> x = make_shared<int[]>(2);
	EXPECT_EQ(x[0], int{});
	EXPECT_EQ(x[1], int{});
}
TEST_F(sh_shared_ptr, shared_ptr_operator_bool)
{
	EXPECT_FALSE(bool(shared_ptr<int>{}));
	EXPECT_TRUE(bool(make_shared<int>(123)));
}
TEST_F(sh_shared_ptr, shared_ptr_use_count)
{
	shared_ptr<int> x;
	ASSERT_EQ(0u, x.use_count());

	x = make_shared<int>(123);
	ASSERT_EQ(1u, x.use_count());

	shared_ptr<int> y{ x };
	EXPECT_EQ(2u, x.use_count());
	EXPECT_EQ(2u, y.use_count());

	x = nullptr;
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(1u, y.use_count());
}
TEST_F(sh_shared_ptr, shared_ptr_get)
{
	shared_ptr<int> x;
	ASSERT_EQ(nullptr, x.get());

	x = make_shared<int>(123);
	ASSERT_NE(nullptr, x.get());

	shared_ptr<int> y{ x };
	EXPECT_NE(nullptr, x.get());
	EXPECT_NE(nullptr, y.get());
	EXPECT_EQ(x.get(), y.get());

	x = nullptr;
	EXPECT_EQ(nullptr, x.get());
	EXPECT_NE(nullptr, y.get());
}
TEST_F(sh_shared_ptr, shared_ptr_reset)
{
	shared_ptr<int> x{ make_shared<int>(123) };
	ASSERT_TRUE(bool(x));
	ASSERT_NE(nullptr, x.get());
	ASSERT_EQ(1u, x.use_count());

	x.reset();
	ASSERT_FALSE(bool(x));
	ASSERT_EQ(nullptr, x.get());
	ASSERT_EQ(0u, x.use_count());
}
TEST_F(sh_shared_ptr, shared_ptr_swap)
{
	const shared_ptr<int> a{ make_shared<int>(123) };
	const shared_ptr<int> b{ make_shared<int>(456) };
	{
		shared_ptr<int> x{ a };
		shared_ptr<int> y{ b };
		ASSERT_EQ(x.get(), a.get());
		ASSERT_EQ(y.get(), b.get());

		x.swap(y);
		ASSERT_EQ(x.get(), b.get());
		ASSERT_EQ(y.get(), a.get());
	}
	{
		shared_ptr<int> x{ a };
		shared_ptr<int> y;
		ASSERT_EQ(x.get(), a.get());
		ASSERT_EQ(y.get(), nullptr);

		x.swap(y);
		ASSERT_EQ(x.get(), nullptr);
		ASSERT_EQ(y.get(), a.get());
	}
	{
		shared_ptr<int> x;
		shared_ptr<int> y{ b };
		ASSERT_EQ(x.get(), nullptr);
		ASSERT_EQ(y.get(), b.get());

		x.swap(y);
		ASSERT_EQ(x.get(), b.get());
		ASSERT_EQ(y.get(), nullptr);
	}
	{
		shared_ptr<int> x;
		shared_ptr<int> y;
		ASSERT_EQ(x.get(), nullptr);
		ASSERT_EQ(y.get(), nullptr);

		x.swap(y);
		ASSERT_EQ(x.get(), nullptr);
		ASSERT_EQ(y.get(), nullptr);
	}
}
TEST_F(sh_shared_ptr, shared_ptr_owner_before)
{
	const shared_ptr<int> x = make_shared<int>(123);
	const shared_ptr<int> y = make_shared<int>(456);
	EXPECT_FALSE(x.owner_before(x));
	EXPECT_FALSE(y.owner_before(y));
	EXPECT_NE(x.owner_before(y), y.owner_before(x));

	const weak_ptr<int> z{ x };
	EXPECT_FALSE(x.owner_before(z));
	EXPECT_EQ(y.owner_before(z), y.owner_before(x));
}

TEST_F(sh_shared_ptr, weak_ptr_ctor)
{
	const weak_ptr<int> x;
	EXPECT_EQ(0u, x.use_count());
	EXPECT_EQ(nullptr, x.lock().get());
}
TEST_F(sh_shared_ptr, weak_ptr_ctor_copy)
{
	const shared_ptr<int> x{ make_shared<int>(123) };
	const weak_ptr<int> y{ x };
	EXPECT_EQ(1u, y.use_count());
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_NE(nullptr, y.lock().get());

	const weak_ptr<int> z{ y };
	EXPECT_EQ(1u, z.use_count());
	EXPECT_EQ(x.get(), z.lock().get());
	EXPECT_NE(nullptr, z.lock().get());
}
TEST_F(sh_shared_ptr, weak_ptr_ctor_move)
{
	const shared_ptr<int> x{ make_shared<int>(123) };
	weak_ptr<int> y{ x };
	EXPECT_EQ(1u, y.use_count());
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_NE(nullptr, y.lock().get());

	const weak_ptr<int> z{ std::move(y) };
	EXPECT_EQ(1u, z.use_count());
	EXPECT_EQ(x.get(), z.lock().get());
	EXPECT_NE(nullptr, z.lock().get());

	EXPECT_EQ(0u, y.use_count());
	EXPECT_EQ(nullptr, y.lock().get());
}
TEST_F(sh_shared_ptr, weak_ptr_ctor_shared_ptr)
{
	const shared_ptr<int> x{ make_shared<int>(123) };
	const weak_ptr<int> y{ x };
	EXPECT_EQ(1u, y.use_count());
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_NE(nullptr, y.lock().get());
}
TEST_F(sh_shared_ptr, weak_ptr_ctor_copy_shared_ptr_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	const shared_ptr<Derived> x{ make_shared<Derived>() };
	const weak_ptr<const Base> y{ x };
	EXPECT_EQ(1u, y.use_count());
	EXPECT_NE(nullptr, y.lock().get());
	EXPECT_EQ(x.get(), y.lock().get());
}
TEST_F(sh_shared_ptr, weak_ptr_ctor_copy_weak_ptr_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	const shared_ptr<Derived> x{ make_shared<Derived>() };
	const weak_ptr<Derived> y{ x };
	const weak_ptr<const Base> z{ y };
	EXPECT_EQ(1u, z.use_count());
	EXPECT_NE(nullptr, z.lock().get());
	EXPECT_EQ(x.get(), z.lock().get());
}
TEST_F(sh_shared_ptr, weak_ptr_ctor_move_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	const shared_ptr<Derived> x{ make_shared<Derived>() };
	weak_ptr<Derived> y{ x };
	const weak_ptr<const Base> z{ std::move(y) };
	EXPECT_EQ(1u, z.use_count());
	EXPECT_NE(nullptr, z.lock().get());
}
TEST_F(sh_shared_ptr, weak_ptr_assign_copy)
{
	{
		const shared_ptr<int> w{ make_shared<int>() };
		const weak_ptr<int> x{ w };
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.lock().get());

		weak_ptr<int> y;
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());

		const weak_ptr<int> z;
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
		shared_ptr<int> w{ make_shared<int>() };
		const weak_ptr<int> x{ w };
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.lock().get());

		weak_ptr<int> y;
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());

		const weak_ptr<int> z;
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
TEST_F(sh_shared_ptr, weak_ptr_assign_move)
{
	{
		const shared_ptr<int> w{ make_shared<int>() };
		weak_ptr<int> x{ w };
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.lock().get());
		ASSERT_EQ(w.get(), x.lock().get());

		weak_ptr<int> y;
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());

		weak_ptr<int> z;
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
		shared_ptr<int> w{ make_shared<int>() };
		weak_ptr<int> x{ w };
		ASSERT_EQ(1u, x.use_count());
		ASSERT_NE(nullptr, x.lock().get());
		ASSERT_EQ(w.get(), x.lock().get());

		weak_ptr<int> y;
		ASSERT_EQ(0u, y.use_count());
		ASSERT_EQ(nullptr, y.lock().get());

		weak_ptr<int> z;
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
TEST_F(sh_shared_ptr, weak_ptr_assign_copy_shared_ptr_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	{
		shared_ptr<Derived> x{ make_shared<Derived>() };
		weak_ptr<const Base> y;
		y = x;
		EXPECT_EQ(1u, y.use_count());
		EXPECT_NE(nullptr, y.lock().get());
		EXPECT_EQ(x.get(), y.lock().get());
	}
	{
		shared_ptr<Derived> x{ make_shared<Derived>() };
		x.reset();
		weak_ptr<const Base> y;
		y = x;
		EXPECT_EQ(0u, y.use_count());
		EXPECT_EQ(nullptr, y.lock().get());
	}
}
TEST_F(sh_shared_ptr, weak_ptr_assign_copy_weak_ptr_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	{
		shared_ptr<Derived> x{ make_shared<Derived>() };
		weak_ptr<Derived> y{ x };
		weak_ptr<const Base> z;
		z = y;
		EXPECT_EQ(1u, z.use_count());
		EXPECT_NE(nullptr, z.lock().get());
		EXPECT_EQ(x.get(), z.lock().get());
	}
	{
		shared_ptr<Derived> x{ make_shared<Derived>() };
		weak_ptr<Derived> y{ x };
		x.reset();
		weak_ptr<const Base> z;
		z = y;
		EXPECT_EQ(0u, z.use_count());
		EXPECT_EQ(nullptr, z.lock().get());
	}
}
TEST_F(sh_shared_ptr, weak_ptr_assign_move_implicit)
{
	using namespace SimpleInheritance;
	static_assert(is_pointer_interconvertible_v<Derived, Base>);
	{
		shared_ptr<Derived> x{ make_shared<Derived>() };
		weak_ptr<Derived> y{ x };
		weak_ptr<const Base> z;
		z = std::move(y);
		EXPECT_EQ(1u, z.use_count());
		EXPECT_NE(nullptr, z.lock().get());
		EXPECT_EQ(x.get(), z.lock().get());
	}
	{
		shared_ptr<Derived> x{ make_shared<Derived>() };
		weak_ptr<Derived> y{ x };
		x.reset();
		weak_ptr<const Base> z;
		z = std::move(y);
		EXPECT_EQ(0u, z.use_count());
		EXPECT_EQ(nullptr, z.lock().get());
	}
}
TEST_F(sh_shared_ptr, weak_ptr_use_count)
{
	shared_ptr<int> x{ make_shared<int>(123) };
	shared_ptr<int> y{ x };
	weak_ptr<int> z{ x };
	ASSERT_EQ(2u, z.use_count());

	x.reset();
	ASSERT_EQ(1u, z.use_count());

	{
		weak_ptr<int> w{ z };
		ASSERT_EQ(1u, z.use_count());
	}

	y.reset();
	ASSERT_EQ(0u, z.use_count());
}
TEST_F(sh_shared_ptr, weak_ptr_expired)
{
	shared_ptr<int> x{ make_shared<int>(123) };
	weak_ptr<int> y{ x };
	ASSERT_EQ(x.get(), y.lock().get());
	ASSERT_NE(nullptr, y.lock().get());
	ASSERT_FALSE(y.expired());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	ASSERT_TRUE(y.expired());
}
TEST_F(sh_shared_ptr, weak_ptr_lock)
{
	shared_ptr<int> x{ make_shared<int>(123) };
	weak_ptr<int> y{ x };
	ASSERT_EQ(x.get(), y.lock().get());
	ASSERT_NE(nullptr, y.lock().get());
	ASSERT_EQ(1u, y.use_count());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	EXPECT_EQ(0u, y.use_count());
}
TEST_F(sh_shared_ptr, weak_ptr_reset)
{
	{
		shared_ptr<int> x{ make_shared<int>(123) };
		weak_ptr<int> y{ x };
		ASSERT_NE(nullptr, y.lock().get());
		ASSERT_EQ(1u, y.use_count());

		y.reset();
		EXPECT_EQ(nullptr, y.lock().get());
		EXPECT_EQ(0u, y.use_count());
	}
	{
		shared_ptr<int> x{ make_shared<int>(123) };
		weak_ptr<int> y{ x };
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
TEST_F(sh_shared_ptr, weak_ptr_swap)
{
	{
		const shared_ptr<int> a{ make_shared<int>(123) };
		const shared_ptr<int> b{ make_shared<int>(456) };
		{
			weak_ptr<int> x{ a };
			weak_ptr<int> y{ b };
			ASSERT_EQ(x.lock().get(), a.get());
			ASSERT_EQ(y.lock().get(), b.get());

			x.swap(y);
			ASSERT_EQ(x.lock().get(), b.get());
			ASSERT_EQ(y.lock().get(), a.get());
		}
		{
			weak_ptr<int> x{ a };
			weak_ptr<int> y;
			ASSERT_EQ(x.lock().get(), a.get());
			ASSERT_EQ(y.lock().get(), nullptr);

			x.swap(y);
			ASSERT_EQ(x.lock().get(), nullptr);
			ASSERT_EQ(y.lock().get(), a.get());
		}
		{
			weak_ptr<int> x;
			weak_ptr<int> y{ b };
			ASSERT_EQ(x.lock().get(), nullptr);
			ASSERT_EQ(y.lock().get(), b.get());

			x.swap(y);
			ASSERT_EQ(x.lock().get(), b.get());
			ASSERT_EQ(y.lock().get(), nullptr);
		}
	}
	{
		shared_ptr<int> a{ make_shared<int>(123) };
		shared_ptr<int> b{ make_shared<int>(456) };
		weak_ptr<int> x{ a };
		weak_ptr<int> y{ b };
		ASSERT_EQ(x.lock().get(), a.get());
		ASSERT_EQ(y.lock().get(), b.get());
		a.reset();
		b.reset();

		x.swap(y);
		ASSERT_EQ(x.lock().get(), b.get());
		ASSERT_EQ(y.lock().get(), a.get());
	}
	{
		shared_ptr<int> a{ make_shared<int>(123) };
		weak_ptr<int> x{ a };
		weak_ptr<int> y;
		ASSERT_EQ(x.lock().get(), a.get());
		ASSERT_EQ(y.lock().get(), nullptr);
		a.reset();

		x.swap(y);
		ASSERT_EQ(x.lock().get(), nullptr);
		ASSERT_EQ(y.lock().get(), a.get());
	}
	{
		shared_ptr<int> b{ make_shared<int>(456) };
		weak_ptr<int> x;
		weak_ptr<int> y{ b };
		ASSERT_EQ(x.lock().get(), nullptr);
		ASSERT_EQ(y.lock().get(), b.get());
		b.reset();

		x.swap(y);
		ASSERT_EQ(x.lock().get(), b.get());
		ASSERT_EQ(y.lock().get(), nullptr);
	}
	{
		weak_ptr<int> x;
		weak_ptr<int> y;
		ASSERT_EQ(x.lock().get(), nullptr);
		ASSERT_EQ(y.lock().get(), nullptr);

		x.swap(y);
		ASSERT_EQ(x.lock().get(), nullptr);
		ASSERT_EQ(y.lock().get(), nullptr);
	}
}
TEST_F(sh_shared_ptr, weak_ptr_owner_before)
{
	const shared_ptr<int> x = make_shared<int>(123);
	const shared_ptr<int> y = make_shared<int>(456);
	const weak_ptr<int> z{ x };
	const weak_ptr<int> w{ y };

	EXPECT_FALSE(z.owner_before(x));
	EXPECT_FALSE(z.owner_before(z));
	EXPECT_EQ(z.owner_before(y), x.owner_before(y));
}

TEST_F(sh_shared_ptr, make_shared)
{
	using namespace SingleInheritance;
	shared_ptr<Base> x{ make_shared<Derived>() };
	EXPECT_TRUE(bool(x));
	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	x.reset();
	x = make_shared<Derived>();
	EXPECT_TRUE(bool(x));
	EXPECT_NE(nullptr, x.get());
	EXPECT_EQ(1u, x.use_count());

	weak_ptr<Base> y{ x };
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_EQ(1u, y.use_count());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	EXPECT_EQ(0u, y.use_count());
}
TEST_F(sh_shared_ptr, make_shared_throw)
{
	throws_on_counter::throw_counter = 0;
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(make_shared<throws_on_counter>(), configurable_exception);
}
TEST_F(sh_shared_ptr, make_shared_array)
{
	using namespace SingleInheritance;
	{
		shared_ptr<Derived[]> x{ make_shared<Derived[]>(2) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		shared_ptr<Derived[]> x{ make_shared<Derived[2]>() };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		shared_ptr<Derived[2]> x{ make_shared<Derived[2]>() };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		constexpr int value{ 123 };
		shared_ptr<int[]> x{ make_shared<int[]>(2, value) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
		EXPECT_EQ(value, x[0]);
		EXPECT_EQ(value, x[1]);
	}
	{
		constexpr int value{ 123 };
		shared_ptr<int[2]> x{ make_shared<int[2]>(value) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
		EXPECT_EQ(value, x[0]);
		EXPECT_EQ(value, x[1]);
	}
	{
		shared_ptr<int[][2]> x{ make_shared<int[][2]>(2) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		shared_ptr<Derived[][2]> x{ make_shared<Derived[][2]>(2) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
}
TEST_F(sh_shared_ptr, make_shared_array_throw)
{
	throws_on_counter::throw_counter = 1;
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(make_shared<throws_on_counter[]>(3), configurable_exception);
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(make_shared<throws_on_counter[4]>(), configurable_exception);
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(make_shared<throws_on_counter[]>(3, throws_on_counter{}), configurable_exception);
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(make_shared<throws_on_counter[4]>(throws_on_counter{}), configurable_exception);
}
TEST_F(sh_shared_ptr, allocate_shared)
{
	using namespace SingleInheritance;
	{
		counted_allocator<Derived> alloc;
		shared_ptr<Base> x{ sh::allocate_shared<Derived>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());

		x.reset();
		x = sh::allocate_shared<Derived>(alloc);
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());

		weak_ptr<Base> y{ x };
		EXPECT_EQ(x.get(), y.lock().get());
		EXPECT_EQ(1u, y.use_count());

		x.reset();
		EXPECT_EQ(nullptr, y.lock().get());
		EXPECT_EQ(0u, y.use_count());
	}
	{
		stateful_allocator<Derived> alloc{ 123 };
		shared_ptr<Base> x{ sh::allocate_shared<Derived>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());

		x.reset();
		x = sh::allocate_shared<Derived>(alloc);
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());

		weak_ptr<Base> y{ x };
		EXPECT_EQ(x.get(), y.lock().get());
		EXPECT_EQ(1u, y.use_count());

		x.reset();
		EXPECT_EQ(nullptr, y.lock().get());
		EXPECT_EQ(0u, y.use_count());
	}
}
TEST_F(sh_shared_ptr, allocate_shared_throw)
{
	throws_on_counter::throw_counter = 0;
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(sh::allocate_shared<throws_on_counter>(counted_allocator<throws_on_counter>{}), configurable_exception);
}
TEST_F(sh_shared_ptr, allocate_shared_array)
{
	using namespace SingleInheritance;
	{
		counted_allocator<Derived> alloc;
		shared_ptr<Derived[]> x{ sh::allocate_shared<Derived[]>(alloc, 2u) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		counted_allocator<Derived> alloc;
		shared_ptr<Derived[]> x{ sh::allocate_shared<Derived[2]>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		counted_allocator<Derived> alloc;
		shared_ptr<Derived[2]> x{ sh::allocate_shared<Derived[2]>(alloc) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
	{
		constexpr int value{ 123 };
		counted_allocator<int> alloc;
		shared_ptr<int[]> x{ sh::allocate_shared<int[]>(alloc, 2, value) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
		EXPECT_EQ(x[0], value);
		EXPECT_EQ(x[1], value);
	}
	{
		constexpr int value{ 123 };
		counted_allocator<int> alloc;
		shared_ptr<int[2]> x{ sh::allocate_shared<int[2]>(alloc, value) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
		EXPECT_EQ(x[0], value);
		EXPECT_EQ(x[1], value);
	}
	{
		stateful_allocator<Derived> alloc;
		shared_ptr<Derived[]> x{ sh::allocate_shared<Derived[]>(alloc, 2u) };
		EXPECT_TRUE(bool(x));
		EXPECT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
	}
}
TEST_F(sh_shared_ptr, allocate_shared_array_throw)
{
	throws_on_counter::throw_counter = 1;
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(sh::allocate_shared<throws_on_counter[]>(counted_allocator<throws_on_counter>{}, 3), configurable_exception);
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(sh::allocate_shared<throws_on_counter[4]>(counted_allocator<throws_on_counter>{}), configurable_exception);

	throws_on_counter::throw_counter = 3;
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(sh::allocate_shared<throws_on_counter[]>(counted_allocator<throws_on_counter>{}, 3, throws_on_counter{}), configurable_exception);
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(sh::allocate_shared<throws_on_counter[4]>(counted_allocator<throws_on_counter>{}, throws_on_counter{}), configurable_exception);
}
TEST_F(sh_shared_ptr, allocate_shared_for_overwrite)
{
	constexpr char prefill = '\xff';
	prefill_allocator<char, prefill> alloc;
	shared_ptr<char> x{ sh::allocate_shared_for_overwrite<char>(alloc) };
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

	weak_ptr<char> y{ x };
	EXPECT_EQ(x.get(), y.lock().get());
	EXPECT_EQ(1u, y.use_count());

	x.reset();
	EXPECT_EQ(nullptr, y.lock().get());
	EXPECT_EQ(0u, y.use_count());
}
TEST_F(sh_shared_ptr, allocate_shared_for_overwrite_throw)
{
	throws_on_counter::throw_counter = 0;
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(sh::allocate_shared_for_overwrite<throws_on_counter>(counted_allocator<throws_on_counter>{}), configurable_exception);
}
TEST_F(sh_shared_ptr, allocate_shared_for_overwrite_array)
{
	constexpr char prefill = '\xff';
	prefill_allocator<char, prefill> alloc;
	{
		shared_ptr<char[]> x{ sh::allocate_shared_for_overwrite<char[]>(alloc, 4) };
		general_allocations::get().m_construct_default += 4;
		ASSERT_TRUE(bool(x));
		ASSERT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
		EXPECT_EQ(prefill, x[0]);
		EXPECT_EQ(prefill, x[1]);
		EXPECT_EQ(prefill, x[2]);
		EXPECT_EQ(prefill, x[3]);
	}
	{
		shared_ptr<char[]> x{ sh::allocate_shared_for_overwrite<char[4]>(alloc) };
		general_allocations::get().m_construct_default += 4;
		ASSERT_TRUE(bool(x));
		ASSERT_NE(nullptr, x.get());
		EXPECT_EQ(1u, x.use_count());
		EXPECT_EQ(prefill, x[0]);
		EXPECT_EQ(prefill, x[1]);
		EXPECT_EQ(prefill, x[2]);
		EXPECT_EQ(prefill, x[3]);
	}
}
TEST_F(sh_shared_ptr, allocate_shared_for_overwrite_array_throw)
{
	throws_on_counter::throw_counter = 1;
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(sh::allocate_shared_for_overwrite<throws_on_counter[]>(counted_allocator<throws_on_counter>{}, 3), configurable_exception);
	general_allocations::get().m_construct_default += 1;
	throws_on_counter::current_counter = 0;
	EXPECT_THROW(sh::allocate_shared_for_overwrite<throws_on_counter[4]>(counted_allocator<throws_on_counter>{}), configurable_exception);
	general_allocations::get().m_construct_default += 1;
}

TEST_F(sh_shared_ptr, shared_ptr_const_cast)
{
	const shared_ptr<int> x{ make_shared<int>(123) };
	const shared_ptr<const int> y{ const_pointer_cast<const int>(x)  };
	const shared_ptr<int> z{ const_pointer_cast<int>(y)  };
	EXPECT_EQ(x.get(), y.get());
	EXPECT_EQ(x.get(), z.get());
}
TEST_F(sh_shared_ptr, shared_ptr_dynamic_cast)
{
	using namespace SingleInheritance;
	const shared_ptr<Base> x{ make_shared<Derived>() };
	const shared_ptr<Derived> y{ dynamic_pointer_cast<Derived>(x) };
	EXPECT_EQ(x.get(), y.get());
	EXPECT_NE(nullptr, y.get());
	const shared_ptr<DifferentDerived> z{ dynamic_pointer_cast<DifferentDerived>(x) };
	EXPECT_EQ(nullptr, z.get());
}
TEST_F(sh_shared_ptr, shared_ptr_static_cast)
{
	using namespace SimpleInheritance;
	const shared_ptr<Base> x{ make_shared<Derived>() };
	const shared_ptr<Derived> y{ static_pointer_cast<Derived>(x) };
	EXPECT_EQ(x.get(), y.get());
}
TEST_F(sh_shared_ptr, shared_ptr_reinterpret_cast)
{
	const shared_ptr<int> x{ make_shared<int>() };
	const shared_ptr<float> y{ reinterpret_pointer_cast<float>(x) };
	EXPECT_EQ(
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(x.get())),
		reinterpret_cast<std::uintptr_t>(static_cast<void*>(y.get()))
	);
}

TEST_F(sh_shared_ptr, shared_get_deleter)
{
	const shared_ptr<int> x{ make_shared<int>() };
	void* const del = get_deleter<void>(x);
	EXPECT_EQ(nullptr, del);
}

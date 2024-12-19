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

#include <sh/never_null.hpp>

using sh::never_null;

namespace
{
	struct Base
	{
		virtual ~Base() = default;
	};
	struct Derived final : Base
	{
		explicit Derived(const int value)
			: m_value{ value }
		{ }
		~Derived() override = default;

		int m_value;
	};
} // anonymous namespace

TEST(sh_never_null, ctor_ptr_valid)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(*nn, i);
}
TEST(sh_never_null, ctor_ptr_invalid)
{
	const int* const p = nullptr;
	EXPECT_THROW(never_null<const int*>{ p }, sh::null_error);
}
TEST(sh_never_null, ctor_convertible_ptr_valid)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	never_null<const Base*> nn{ &s };
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(static_cast<const Derived&>(*nn).m_value, i);
}
TEST(sh_never_null, ctor_convertible_ptr_invalid)
{
	const Derived* const p = nullptr;
	EXPECT_THROW(never_null<const Base*>{ p }, sh::null_error);
}
TEST(sh_never_null, ctor_copy_lvalue_ptr)
{
	const int i{ 123 };
	const never_null<const int*> nn{ &i };
	never_null<const int*> nn2{ nn };
	EXPECT_EQ(nn, nn2);
}
TEST(sh_never_null, ctor_copy_rvalue_ptr)
{
	const int i{ 123 };
	never_null<const int*> nn{ never_null<const int*>{ &i } };
	EXPECT_EQ(nn.get(), &i);
}
TEST(sh_never_null, ctor_copy_convertible_lvalue_ptr)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	const never_null<const Derived*> nn{ &s };
	never_null<const Base*> nn2{ nn };
	EXPECT_EQ(nn2.get(), &s);
}
TEST(sh_never_null, ctor_copy_convertible_rvalue_ptr)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	never_null<const Base*> nn{ never_null<const Derived*>{ &s } };
	EXPECT_EQ(nn.get(), &s);
}
TEST(sh_never_null, ctor_shared_ptr_valid)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p = std::make_shared<int>(i);
	never_null<std::shared_ptr<int>> nn{ p };
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(*nn, i);
}
TEST(sh_never_null, ctor_shared_ptr_invalid)
{
	EXPECT_THROW(never_null<std::shared_ptr<int>>{ std::shared_ptr<int>{} }, sh::null_error);
}
TEST(sh_never_null, ctor_convertible_shared_ptr_valid)
{
	constexpr int i{ 123 };
	const std::shared_ptr<Derived> p{ std::make_shared<Derived>(i) };
	never_null<std::shared_ptr<Base>> nn{ p };
	EXPECT_EQ(nn.get(), p);
}
TEST(sh_never_null, ctor_convertible_shared_ptr_invalid)
{
	EXPECT_THROW(never_null<std::shared_ptr<Base>>{ std::shared_ptr<Derived>{} }, sh::null_error);
}
TEST(sh_never_null, ctor_copy_lvalue_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p{ std::make_shared<int>(i) };
	const never_null<std::shared_ptr<int>> nn{ p };
	const never_null<std::shared_ptr<int>> nn2{ nn };
	EXPECT_EQ(nn, nn2);
}
TEST(sh_never_null, ctor_copy_rvalue_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p{ std::make_shared<int>(i) };
	const never_null<std::shared_ptr<int>> nn{ never_null<std::shared_ptr<int>>{ p } };
	EXPECT_EQ(nn.get(), p);
}
TEST(sh_never_null, ctor_copy_convertible_lvalue_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<Derived> p{ std::make_shared<Derived>(i) };
	const never_null<std::shared_ptr<Derived>> nn{ p };
	const never_null<std::shared_ptr<Base>> nn2{ nn };
	EXPECT_EQ(nn, nn2);
}
TEST(sh_never_null, ctor_copy_convertible_rvalue_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<Derived> p{ std::make_shared<Derived>(i) };
	never_null<std::shared_ptr<Base>> nn{ never_null<std::shared_ptr<Derived>>{ p } };
	EXPECT_EQ(nn.get(), p);
}
TEST(sh_never_null, ctor_unique_ptr_valid)
{
	const int i{ 123 };
	std::unique_ptr<int> p = std::make_unique<int>(i);
	never_null<std::unique_ptr<int>> nn{ std::move(p) };
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(*nn, i);
}
TEST(sh_never_null, ctor_unique_ptr_invalid)
{
	EXPECT_THROW(never_null<std::unique_ptr<int>>{ std::unique_ptr<int>{} }, sh::null_error);
}
TEST(sh_never_null, ctor_convertible_unique_ptr_valid)
{
	constexpr int i{ 123 };
	std::unique_ptr<Derived> p{ std::make_unique<Derived>(i) };
	Derived* const p2 = p.get();
	never_null<std::unique_ptr<Base>> nn{ std::move(p) };
	EXPECT_EQ(nn.get().get(), p2);
}
TEST(sh_never_null, ctor_convertible_unique_ptr_invalid)
{
	EXPECT_THROW(never_null<std::unique_ptr<Base>>{ std::unique_ptr<Derived>{} }, sh::null_error);
}
TEST(sh_never_null, assign_copy_lvalue)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	const int j{ 456 };
	never_null<const int*> nn2{ &j };
	nn = nn2;
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(*nn, j);
	ASSERT_TRUE(bool(nn2));
	EXPECT_EQ(*nn2, j);
}
TEST(sh_never_null, assign_copy_rvalue)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	const int j{ 456 };
	nn = never_null<const int*>{ &j };
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(*nn, j);
}
TEST(sh_never_null, assign_copy_convertible_lvalue)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	never_null<const Base*> nn{ &s };
	constexpr int j{ 456 };
	const Derived s2{ j };
	never_null<const Derived*> nn2{ &s2 };
	nn = nn2;
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(static_cast<const Derived&>(*nn).m_value, j);
	ASSERT_TRUE(bool(nn2));
	EXPECT_EQ(nn2->m_value, j);
}
TEST(sh_never_null, assign_copy_convertible_rvalue)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	never_null<const Base*> nn{ &s };
	constexpr int j{ 456 };
	const Derived s2{ j };
	nn = never_null<const Derived*>{ &s2 };
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(static_cast<const Derived&>(*nn).m_value, j);
}
TEST(sh_never_null, assign_ptr_valid)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	const int j{ 456 };
	nn = &j;
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(*nn, j);
}
TEST(sh_never_null, assign_ptr_invalid)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	const int* const p = nullptr;
	EXPECT_THROW(nn = p, sh::null_error);
}
TEST(sh_never_null, assign_convertible_ptr_valid)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	never_null<const Base*> nn{ &s };
	constexpr int j{ 456 };
	const Derived s2{ j };
	nn = &s2;
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(static_cast<const Derived&>(*nn).m_value, j);
}
TEST(sh_never_null, assign_convertible_ptr_invalid)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	never_null<const Base*> nn{ &s };
	const Derived* const p = nullptr;
	EXPECT_THROW(nn = p, sh::null_error);
}
TEST(sh_never_null, assign_shared_ptr_valid)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p1 = std::make_shared<int>(i);
	never_null<std::shared_ptr<int>> nn{ p1 };
	constexpr int j{ 456 };
	const std::shared_ptr<int> p2 = std::make_shared<int>(j);
	nn = p2;
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(*nn, j);
}
TEST(sh_never_null, assign_shared_ptr_invalid)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p1 = std::make_shared<int>(i);
	never_null<std::shared_ptr<int>> nn{ p1 };
	const std::shared_ptr<int> p2;
	EXPECT_THROW(nn = p2, sh::null_error);
}
TEST(sh_never_null, assign_unique_ptr_valid)
{
	constexpr int i{ 123 };
	never_null<std::unique_ptr<int>> nn{ std::make_unique<int>(i) };
	const int j{ 456 };
	nn = std::make_unique<int>(j);
	ASSERT_TRUE(bool(nn));
	EXPECT_EQ(*nn, j);
}
TEST(sh_never_null, assign_unique_ptr_invalid)
{
	constexpr int i{ 123 };
	never_null<std::unique_ptr<int>> nn{ std::make_unique<int>(i) };
	EXPECT_THROW(nn = std::unique_ptr<int>{}, sh::null_error);
}
TEST(sh_never_null, get_ptr)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	EXPECT_EQ(nn.get(), &i);
}
TEST(sh_never_null, get_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p{ std::make_shared<int>(i) };
	never_null<std::shared_ptr<int>> nn{ p };
	EXPECT_EQ(nn.get(), p);
}
TEST(sh_never_null, get_unique_ptr)
{
	constexpr int i{ 123 };
	std::unique_ptr<int> p{ std::make_unique<int>(i) };
	int* const p2 = p.get();
	never_null<std::unique_ptr<int>> nn{ std::move(p) };
	EXPECT_EQ(nn.get().get(), p2);
}
TEST(sh_never_null, operator_deref_ptr)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	EXPECT_EQ(std::addressof(*nn), std::addressof(i));
}
TEST(sh_never_null, operator_deref_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p{ std::make_shared<int>(i) };
	never_null<std::shared_ptr<int>> nn{ p };
	EXPECT_EQ(std::addressof(*nn), p.get());
}
TEST(sh_never_null, operator_deref_unique_ptr)
{
	constexpr int i{ 123 };
	std::unique_ptr<int> p{ std::make_unique<int>(i) };
	int* const p2 = p.get();
	never_null<std::unique_ptr<int>> nn{ std::move(p) };
	EXPECT_EQ(std::addressof(*nn), p2);
}
TEST(sh_never_null, operator_arrow_ptr)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	never_null<const Derived*> nn{ &s };
	EXPECT_EQ(nn.operator->(), std::addressof(s));
	EXPECT_EQ(std::addressof(nn->m_value), std::addressof(s.m_value));
}
TEST(sh_never_null, operator_arrow_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<Derived> p{ std::make_shared<Derived>(i) };
	never_null<std::shared_ptr<Derived>> nn{ p };
	EXPECT_EQ(nn.operator->(), p);
	EXPECT_EQ(std::addressof(nn->m_value), std::addressof(p->m_value));
}
TEST(sh_never_null, operator_arrow_unique_ptr)
{
	constexpr int i{ 123 };
	std::unique_ptr<Derived> p{ std::make_unique<Derived>(i) };
	Derived* const p2 = p.get();
	never_null<std::unique_ptr<Derived>> nn{ std::move(p) };
	EXPECT_EQ(nn.operator->().get(), p2);
	EXPECT_EQ(std::addressof(nn->m_value), std::addressof(p2->m_value));
}
TEST(sh_never_null, operator_pointer_type_ptr)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	const auto l = [&i](const int* const arg)
	{
		EXPECT_EQ(std::addressof(i), arg);
	};
	l(nn);
}
TEST(sh_never_null, operator_pointer_type_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p{ std::make_shared<int>(i) };
	never_null<std::shared_ptr<int>> nn{ p };
	const auto l = [&p](const std::shared_ptr<int>& arg)
	{
		EXPECT_EQ(p, arg);
	};
	l(nn);
}
TEST(sh_never_null, operator_pointer_type_unique_ptr)
{
	constexpr int i{ 123 };
	std::unique_ptr<int> p{ std::make_unique<int>(i) };
	int* const p2 = p.get();
	never_null<std::unique_ptr<int>> nn{ std::move(p) };
	const auto l = [p2](const std::unique_ptr<int>& arg)
	{
		EXPECT_EQ(p2, arg.get());
	};
	l(nn);
}
TEST(sh_never_null, operator_bool_ptr)
{
	const int i{ 123 };
	never_null<const int*> nn{ &i };
	EXPECT_TRUE(bool(nn));
}
TEST(sh_never_null, swap_ptr)
{
	const int i{ 123 };
	const int j{ 456 };
	never_null<const int*> nn{ &i };
	never_null<const int*> nn2{ &j };
	nn.swap(nn2);
	EXPECT_EQ(nn.get(), &j);
	EXPECT_EQ(nn2.get(), &i);
	using std::swap;
	swap(nn, nn2);
	EXPECT_EQ(nn.get(), &i);
	EXPECT_EQ(nn2.get(), &j);
}
TEST(sh_never_null, compare_eq_ptr)
{
	const int* const p = nullptr;
	const int i{ 123 };
	EXPECT_FALSE(never_null<const int*>{ &i } == p);
	EXPECT_FALSE(p == never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } == nullptr);
	EXPECT_FALSE(nullptr == never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } == &i);
	EXPECT_TRUE(&i == never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } == never_null<const int*>{ &i });
	const int j{ 123 };
	EXPECT_FALSE(never_null<const int*>{ &i } == never_null<const int*>{ &j });
	EXPECT_FALSE(never_null<const int*>{ &j } == never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } == &j);
	EXPECT_FALSE(&j == never_null<const int*>{ &i });
}
TEST(sh_never_null, compare_ne_ptr)
{
	const int* const p = nullptr;
	const int i{ 123 };
	EXPECT_TRUE(never_null<const int*>{ &i } != p);
	EXPECT_TRUE(p != never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } != nullptr);
	EXPECT_TRUE(nullptr != never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } != never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } != &i);
	EXPECT_FALSE(&i != never_null<const int*>{ &i });
	const int j{ 123 };
	EXPECT_TRUE(never_null<const int*>{ &i } != never_null<const int*>{ &j });
	EXPECT_TRUE(never_null<const int*>{ &j } != never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } != &j);
	EXPECT_TRUE(&j != never_null<const int*>{ &i });
}
TEST(sh_never_null, compare_lt_ptr)
{
	const int* const p = nullptr;
	const int i{ 123 };
	EXPECT_FALSE(never_null<const int*>{ &i } < nullptr);
	EXPECT_TRUE(nullptr < never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } < p);
	EXPECT_TRUE(p < never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } < never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } < &i);
	EXPECT_FALSE(&i < never_null<const int*>{ &i });
	const int j{ 123 };
	EXPECT_EQ(&i < &j, never_null<const int*>{ &i } < never_null<const int*>{ &j });
	EXPECT_EQ(&j < &i, never_null<const int*>{ &j } < never_null<const int*>{ &i });
	EXPECT_EQ(&i < &j, never_null<const int*>{ &i } < &j);
	EXPECT_EQ(&j < &i, &j < never_null<const int*>{ &i });
}
TEST(sh_never_null, compare_le_ptr)
{
	const int* const p = nullptr;
	const int i{ 123 };
	EXPECT_FALSE(never_null<const int*>{ &i } <= nullptr);
	EXPECT_TRUE(nullptr <= never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } <= p);
	EXPECT_TRUE(p <= never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } <= never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } <= &i);
	EXPECT_TRUE(&i <= never_null<const int*>{ &i });
	const int j{ 123 };
	EXPECT_EQ(&i <= &j, never_null<const int*>{ &i } <= never_null<const int*>{ &j });
	EXPECT_EQ(&j <= &i, never_null<const int*>{ &j } <= never_null<const int*>{ &i });
	EXPECT_EQ(&i <= &j, never_null<const int*>{ &i } <= &j);
	EXPECT_EQ(&j <= &i, &j <= never_null<const int*>{ &i });
}
TEST(sh_never_null, compare_gt_ptr)
{
	const int* const p = nullptr;
	const int i{ 123 };
	EXPECT_TRUE(never_null<const int*>{ &i } > nullptr);
	EXPECT_FALSE(nullptr > never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } > p);
	EXPECT_FALSE(p > never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } > never_null<const int*>{ &i });
	EXPECT_FALSE(never_null<const int*>{ &i } > &i);
	EXPECT_FALSE(&i > never_null<const int*>{ &i });
	const int j{ 123 };
	EXPECT_EQ(&i > &j, never_null<const int*>{ &i } > never_null<const int*>{ &j });
	EXPECT_EQ(&j > &i, never_null<const int*>{ &j } > never_null<const int*>{ &i });
	EXPECT_EQ(&i > &j, never_null<const int*>{ &i } > &j);
	EXPECT_EQ(&j > &i, &j > never_null<const int*>{ &i });
}
TEST(sh_never_null, compare_ge_ptr)
{
	const int* const p = nullptr;
	const int i{ 123 };
	EXPECT_TRUE(never_null<const int*>{ &i } >= nullptr);
	EXPECT_FALSE(nullptr >= never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } >= p);
	EXPECT_FALSE(p >= never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } >= never_null<const int*>{ &i });
	EXPECT_TRUE(never_null<const int*>{ &i } >= &i);
	EXPECT_TRUE(&i >= never_null<const int*>{ &i });
	const int j{ 123 };
	EXPECT_EQ(&i >= &j, never_null<const int*>{ &i } >= never_null<const int*>{ &j });
	EXPECT_EQ(&j >= &i, never_null<const int*>{ &j } >= never_null<const int*>{ &i });
	EXPECT_EQ(&i >= &j, never_null<const int*>{ &i } >= &j);
	EXPECT_EQ(&j >= &i, &j >= never_null<const int*>{ &i });
}
TEST(sh_never_null, compare_eq_convertible_ptr)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	const Base* const p = &s;
	EXPECT_TRUE(never_null<const Base*>{ &s } == &s);
	EXPECT_TRUE(&s == never_null<const Base*>{ &s });
	EXPECT_TRUE(never_null<const Derived*>{ &s } == p);
	EXPECT_TRUE(p == never_null<const Derived*>{ &s });
	EXPECT_TRUE(never_null<const Base*>{ &s } == never_null<const Derived*>{ &s });
	EXPECT_TRUE(never_null<const Derived*>{ &s } == never_null<const Base*>{ &s });
	constexpr int j{ 123 };
	const Derived s2{ j };
	const Base* const p2 = &s2;
	EXPECT_FALSE(never_null<const Base*>{ &s } == &s2);
	EXPECT_FALSE(&s2 == never_null<const Base*>{ &s });
	EXPECT_FALSE(never_null<const Derived*>{ &s } == p2);
	EXPECT_FALSE(p2 == never_null<const Derived*>{ &s });
	EXPECT_FALSE(never_null<const Base*>{ &s } == never_null<const Derived*>{ &s2 });
	EXPECT_FALSE(never_null<const Derived*>{ &s2 } == never_null<const Base*>{ &s });
}
TEST(sh_never_null, compare_ne_convertible_ptr)
{
	constexpr int i{ 123 };
	const Derived s{ i };
	const Base* const p = &s;
	EXPECT_FALSE(never_null<const Base*>{ &s } != &s);
	EXPECT_FALSE(&s != never_null<const Base*>{ &s });
	EXPECT_FALSE(never_null<const Derived*>{ &s } != p);
	EXPECT_FALSE(p != never_null<const Derived*>{ &s });
	EXPECT_FALSE(never_null<const Base*>{ &s } != never_null<const Derived*>{ &s });
	EXPECT_FALSE(never_null<const Derived*>{ &s } != never_null<const Base*>{ &s });
	constexpr int j{ 123 };
	const Derived s2{ j };
	const Base* const p2 = &s2;
	EXPECT_TRUE(never_null<const Base*>{ &s } != &s2);
	EXPECT_TRUE(&s2 != never_null<const Base*>{ &s });
	EXPECT_TRUE(never_null<const Derived*>{ &s } != p2);
	EXPECT_TRUE(p2 != never_null<const Derived*>{ &s });
	EXPECT_TRUE(never_null<const Base*>{ &s } != never_null<const Derived*>{ &s2 });
	EXPECT_TRUE(never_null<const Derived*>{ &s2 } != never_null<const Base*>{ &s });
}
TEST(sh_never_null, compare_eq_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p{ std::make_shared<int>(i) };
	EXPECT_FALSE(never_null<std::shared_ptr<int>>{ p } == std::shared_ptr<int>{});
	EXPECT_FALSE(std::shared_ptr<int>{} == never_null<std::shared_ptr<int>>{ p });
	EXPECT_FALSE(never_null<std::shared_ptr<int>>{ p } == nullptr);
	EXPECT_FALSE(nullptr == never_null<std::shared_ptr<int>>{ p });
	EXPECT_TRUE(never_null<std::shared_ptr<int>>{ p } == p);
	EXPECT_TRUE(p == never_null<std::shared_ptr<int>>{ p });
	EXPECT_TRUE(never_null<std::shared_ptr<int>>{ p } == never_null<std::shared_ptr<int>>{ p });
	const std::shared_ptr<int> p2{ std::make_shared<int>(i) };
	EXPECT_FALSE(never_null<std::shared_ptr<int>>{ p } == never_null<std::shared_ptr<int>>{ p2 });
	EXPECT_FALSE(never_null<std::shared_ptr<int>>{ p2 } == never_null<std::shared_ptr<int>>{ p });
}
TEST(sh_never_null, compare_ne_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<int> p{ std::make_shared<int>(i) };
	EXPECT_TRUE(never_null<std::shared_ptr<int>>{ p } != std::shared_ptr<int>{});
	EXPECT_TRUE(std::shared_ptr<int>{} != never_null<std::shared_ptr<int>>{ p });
	EXPECT_TRUE(never_null<std::shared_ptr<int>>{ p } != nullptr);
	EXPECT_TRUE(nullptr != never_null<std::shared_ptr<int>>{ p });
	EXPECT_FALSE(never_null<std::shared_ptr<int>>{ p } != p);
	EXPECT_FALSE(p != never_null<std::shared_ptr<int>>{ p });
	EXPECT_FALSE(never_null<std::shared_ptr<int>>{ p } != never_null<std::shared_ptr<int>>{ p });
	const std::shared_ptr<int> p2{ std::make_shared<int>(i) };
	EXPECT_TRUE(never_null<std::shared_ptr<int>>{ p } != never_null<std::shared_ptr<int>>{ p2 });
	EXPECT_TRUE(never_null<std::shared_ptr<int>>{ p2 } != never_null<std::shared_ptr<int>>{ p });
}
TEST(sh_never_null, compare_eq_convertible_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<Derived> p{ std::make_shared<Derived>(i) };
	const std::shared_ptr<Base> p2{ p };
	EXPECT_TRUE(never_null<std::shared_ptr<Base>>{ p } == p);
	EXPECT_TRUE(p == never_null<std::shared_ptr<Base>>{ p });
	EXPECT_TRUE(never_null<std::shared_ptr<Derived>>{ p } == p2);
	EXPECT_TRUE(p2 == never_null<std::shared_ptr<Derived>>{ p });
	EXPECT_TRUE(never_null<std::shared_ptr<Derived>>{ p } == never_null<std::shared_ptr<Base>>{ p });
	EXPECT_TRUE(never_null<std::shared_ptr<Base>>{ p } == never_null<std::shared_ptr<Derived>>{ p });
	const std::shared_ptr<Derived> p3{ std::make_shared<Derived>(i) };
	const std::shared_ptr<Base> p4{ p3 };
	EXPECT_FALSE(never_null<std::shared_ptr<Base>>{ p } == p3);
	EXPECT_FALSE(p3 == never_null<std::shared_ptr<Base>>{ p });
	EXPECT_FALSE(never_null<std::shared_ptr<Derived>>{ p } == p4);
	EXPECT_FALSE(p4 == never_null<std::shared_ptr<Derived>>{ p });
	EXPECT_FALSE(never_null<std::shared_ptr<Derived>>{ p } == never_null<std::shared_ptr<Base>>{ p3 });
	EXPECT_FALSE(never_null<std::shared_ptr<Base>>{ p } == never_null<std::shared_ptr<Derived>>{ p3 });
}
TEST(sh_never_null, compare_ne_convertible_shared_ptr)
{
	constexpr int i{ 123 };
	const std::shared_ptr<Derived> p{ std::make_shared<Derived>(i) };
	const std::shared_ptr<Base> p2{ p };
	EXPECT_FALSE(never_null<std::shared_ptr<Base>>{ p } != p);
	EXPECT_FALSE(p != never_null<std::shared_ptr<Base>>{ p });
	EXPECT_FALSE(never_null<std::shared_ptr<Derived>>{ p } != p2);
	EXPECT_FALSE(p2 != never_null<std::shared_ptr<Derived>>{ p });
	EXPECT_FALSE(never_null<std::shared_ptr<Derived>>{ p } != never_null<std::shared_ptr<Base>>{ p });
	EXPECT_FALSE(never_null<std::shared_ptr<Base>>{ p } != never_null<std::shared_ptr<Derived>>{ p });
	const std::shared_ptr<Derived> p3{ std::make_shared<Derived>(i) };
	const std::shared_ptr<Base> p4{ p3 };
	EXPECT_TRUE(never_null<std::shared_ptr<Base>>{ p } != p3);
	EXPECT_TRUE(p3 != never_null<std::shared_ptr<Base>>{ p });
	EXPECT_TRUE(never_null<std::shared_ptr<Derived>>{ p } != p4);
	EXPECT_TRUE(p4 != never_null<std::shared_ptr<Derived>>{ p });
	EXPECT_TRUE(never_null<std::shared_ptr<Derived>>{ p } != never_null<std::shared_ptr<Base>>{ p3 });
	EXPECT_TRUE(never_null<std::shared_ptr<Base>>{ p } != never_null<std::shared_ptr<Derived>>{ p3 });
}

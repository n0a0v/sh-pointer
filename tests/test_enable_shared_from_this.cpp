/*	BSD 3-Clause License

	Copyright (c) 2025, Paul Varga
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

#include <sh/shared_ptr.hpp>
#include <sh/wide_shared_ptr.hpp>

using sh::enable_shared_from_this;
using sh::make_shared;
using sh::shared_ptr;
using sh::weak_ptr;
using sh::wide_shared_ptr;
using sh::wide_weak_ptr;

namespace
{
	namespace SingleInheritanceOnBase
	{
		struct Base : public enable_shared_from_this<Base>
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
	} // SingleInheritanceOnBase

	namespace SingleInheritanceOnDerived
	{
		struct Base
		{
			int m_base{ 123 };

			virtual ~Base() = default;
		};
		struct Derived : public enable_shared_from_this<Derived>, Base
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Base) to
			// sizeof(Derived).
			int m_derived[128] = { 456 };
		};
	} // SingleInheritanceOnDerived

	namespace MultipleInheritanceOnFirst
	{
		struct First : public enable_shared_from_this<First>
		{
			virtual ~First() = default;
			int m_first{ 123 };
		};
		struct Second
		{
			virtual ~Second() = default;

			int m_base{ 456 };
		};
		struct Derived : First, Second
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Second) to
			// sizeof(Derived).
			int m_derived[128] = { 789 };
		};
	} // MultipleInheritanceOnFirst

	namespace MultipleInheritanceOnSecond
	{
		struct First
		{
			virtual ~First() = default;
			int m_first{ 123 };
		};
		struct Second : public enable_shared_from_this<Second>
		{
			virtual ~Second() = default;

			int m_base{ 456 };
		};
		struct Derived : First, Second
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Second) to
			// sizeof(Derived).
			int m_derived[128] = { 789 };
		};
	} // MultipleInheritanceOnSecond

	namespace MultipleInheritanceOnDerived
	{
		struct First
		{
			virtual ~First() = default;
			int m_first{ 123 };
		};
		struct Second
		{
			virtual ~Second() = default;

			int m_base{ 456 };
		};
		struct Derived : public enable_shared_from_this<Derived>, First, Second
		{
			~Derived() override = default;

			// Make this big enough where alignment won't pad sizeof(Second) to
			// sizeof(Derived).
			int m_derived[128] = { 789 };
		};
	} // MultipleInheritanceOnDerived

	namespace VirtualInheritanceOnBase
	{
		struct Base : public enable_shared_from_this<Base>
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
	} // VirtualInheritanceOnBase

} // anonymous namespace

TEST(sh_enable_shared_from_this, shared_from_this_base)
{
	using namespace SingleInheritanceOnBase;
	{
		shared_ptr<Base> x = make_shared<Base>();
		{
			auto& from = *x;
			wide_shared_ptr<Base> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_shared_ptr<const Base> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
	}
	{
		Base x;
		EXPECT_THROW({
			x.shared_from_this();
		}, std::bad_weak_ptr);

		const Base y;
		EXPECT_THROW({
			y.shared_from_this();
		}, std::bad_weak_ptr);
	}
}
TEST(sh_enable_shared_from_this, weak_from_this_base)
{
	using namespace SingleInheritanceOnBase;
	{
		shared_ptr<Base> x = make_shared<Base>();
		{
			auto& from = *x;
			wide_weak_ptr<Base> y = from.weak_from_this();
			wide_shared_ptr<Base> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_weak_ptr<const Base> y = from.weak_from_this();
			wide_shared_ptr<const Base> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
	}
	{
		Base x;
		{
			auto& from = x;
			wide_weak_ptr<Base> y = from.weak_from_this();
			wide_shared_ptr<Base> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
		{
			const auto& from = x;
			wide_weak_ptr<const Base> y = from.weak_from_this();
			wide_shared_ptr<const Base> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
	}
}
TEST(sh_enable_shared_from_this, shared_from_this_single_inheritance_on_base)
{
	using namespace SingleInheritanceOnBase;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_shared_ptr<Base> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_shared_ptr<const Base> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
	}
	{
		Derived x;
		EXPECT_THROW({
			x.shared_from_this();
		}, std::bad_weak_ptr);

		const Derived y;
		EXPECT_THROW({
			y.shared_from_this();
		}, std::bad_weak_ptr);
	}
}
TEST(sh_enable_shared_from_this, weak_from_this_single_inheritance_on_base)
{
	using namespace SingleInheritanceOnBase;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_weak_ptr<Base> y = from.weak_from_this();
			wide_shared_ptr<Base> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_weak_ptr<const Base> y = from.weak_from_this();
			wide_shared_ptr<const Base> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
	}
	{
		Derived x;
		{
			auto& from = x;
			wide_weak_ptr<Base> y = from.weak_from_this();
			wide_shared_ptr<Base> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
		{
			const auto& from = x;
			wide_weak_ptr<const Base> y = from.weak_from_this();
			wide_shared_ptr<const Base> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
	}
}
TEST(sh_enable_shared_from_this, shared_from_this_single_inheritance_on_derived)
{
	using namespace SingleInheritanceOnDerived;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_shared_ptr<Derived> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_shared_ptr<const Derived> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
	}
	{
		Derived x;
		EXPECT_THROW({
			x.shared_from_this();
		}, std::bad_weak_ptr);

		const Derived y;
		EXPECT_THROW({
			y.shared_from_this();
		}, std::bad_weak_ptr);
	}
}
TEST(sh_enable_shared_from_this, weak_from_this_single_inheritance_on_derived)
{
	using namespace SingleInheritanceOnDerived;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_weak_ptr<Derived> y = from.weak_from_this();
			wide_shared_ptr<Derived> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_weak_ptr<const Derived> y = from.weak_from_this();
			wide_shared_ptr<const Derived> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
	}
	{
		Derived x;
		{
			auto& from = x;
			wide_weak_ptr<Derived> y = from.weak_from_this();
			wide_shared_ptr<Derived> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
		{
			const auto& from = x;
			wide_weak_ptr<const Derived> y = from.weak_from_this();
			wide_shared_ptr<const Derived> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
	}
}
TEST(sh_enable_shared_from_this, shared_from_this_multiple_inheritance_on_first)
{
	using namespace MultipleInheritanceOnFirst;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_shared_ptr<First> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_shared_ptr<const First> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
	}
	{
		Derived x;
		EXPECT_THROW({
			x.shared_from_this();
		}, std::bad_weak_ptr);

		const Derived y;
		EXPECT_THROW({
			y.shared_from_this();
		}, std::bad_weak_ptr);
	}
}
TEST(sh_enable_shared_from_this, weak_from_this_multiple_inheritance_on_first)
{
	using namespace MultipleInheritanceOnFirst;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_weak_ptr<First> y = from.weak_from_this();
			wide_shared_ptr<First> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_weak_ptr<const First> y = from.weak_from_this();
			wide_shared_ptr<const First> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
	}
	{
		Derived x;
		{
			auto& from = x;
			wide_weak_ptr<First> y = from.weak_from_this();
			wide_shared_ptr<First> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
		{
			const auto& from = x;
			wide_weak_ptr<const First> y = from.weak_from_this();
			wide_shared_ptr<const First> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
	}
}
TEST(sh_enable_shared_from_this, shared_from_this_multiple_inheritance_on_second)
{
	using namespace MultipleInheritanceOnSecond;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_shared_ptr<Second> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_shared_ptr<const Second> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
	}
	{
		Derived x;
		EXPECT_THROW({
			x.shared_from_this();
		}, std::bad_weak_ptr);

		const Derived y;
		EXPECT_THROW({
			y.shared_from_this();
		}, std::bad_weak_ptr);
	}
}
TEST(sh_enable_shared_from_this, weak_from_this_multiple_inheritance_on_second)
{
	using namespace MultipleInheritanceOnSecond;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_weak_ptr<Second> y = from.weak_from_this();
			wide_shared_ptr<Second> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_weak_ptr<const Second> y = from.weak_from_this();
			wide_shared_ptr<const Second> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
	}
	{
		Derived x;
		{
			auto& from = x;
			wide_weak_ptr<Second> y = from.weak_from_this();
			wide_shared_ptr<Second> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
		{
			const auto& from = x;
			wide_weak_ptr<const Second> y = from.weak_from_this();
			wide_shared_ptr<const Second> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
	}
}
TEST(sh_enable_shared_from_this, shared_from_this_multiple_inheritance_on_derived)
{
	using namespace MultipleInheritanceOnDerived;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_shared_ptr<Derived> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_shared_ptr<const Derived> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
	}
	{
		Derived x;
		EXPECT_THROW({
			x.shared_from_this();
		}, std::bad_weak_ptr);

		const Derived y;
		EXPECT_THROW({
			y.shared_from_this();
		}, std::bad_weak_ptr);
	}

}
TEST(sh_enable_shared_from_this, weak_from_this_multiple_inheritance_on_derived)
{
	using namespace MultipleInheritanceOnDerived;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_weak_ptr<Derived> y = from.weak_from_this();
			wide_shared_ptr<Derived> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_weak_ptr<const Derived> y = from.weak_from_this();
			wide_shared_ptr<const Derived> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
	}
	{
		Derived x;
		{
			auto& from = x;
			wide_weak_ptr<Derived> y = from.weak_from_this();
			wide_shared_ptr<Derived> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
		{
			const auto& from = x;
			wide_weak_ptr<const Derived> y = from.weak_from_this();
			wide_shared_ptr<const Derived> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
	}
}
TEST(sh_enable_shared_from_this, shared_from_this_virtual_inheritance_on_base)
{
	using namespace VirtualInheritanceOnBase;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_shared_ptr<Base> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_shared_ptr<const Base> y = from.shared_from_this();

			EXPECT_EQ(x.get(), y.get());
			EXPECT_FALSE(x.owner_before(y));
			EXPECT_FALSE(y.owner_before(x));
		}
	}
	{
		Derived x;
		EXPECT_THROW({
			x.shared_from_this();
		}, std::bad_weak_ptr);

		const Derived y;
		EXPECT_THROW({
			y.shared_from_this();
		}, std::bad_weak_ptr);
	}
}
TEST(sh_enable_shared_from_this, weak_from_this_virtual_inheritance_on_base)
{
	using namespace VirtualInheritanceOnBase;
	{
		shared_ptr<Derived> x = make_shared<Derived>();
		{
			auto& from = *x;
			wide_weak_ptr<Base> y = from.weak_from_this();
			wide_shared_ptr<Base> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
		{
			const auto& from = *x;
			wide_weak_ptr<const Base> y = from.weak_from_this();
			wide_shared_ptr<const Base> z = y.lock();

			ASSERT_TRUE(bool(z));
			EXPECT_EQ(x.get(), z.get());
			EXPECT_FALSE(x.owner_before(z));
			EXPECT_FALSE(z.owner_before(x));
		}
	}
	{
		Derived x;
		{
			auto& from = x;
			wide_weak_ptr<Base> y = from.weak_from_this();
			wide_shared_ptr<Base> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
		{
			const auto& from = x;
			wide_weak_ptr<const Base> y = from.weak_from_this();
			wide_shared_ptr<const Base> z = y.lock();
			EXPECT_FALSE(bool(z));
		}
	}
}
TEST(sh_enable_shared_from_this, array_from_this_base)
{
	using namespace SingleInheritanceOnBase;
	shared_ptr<Base[]> x = make_shared<Base[]>(2);
	{
		auto& from = x[0];
		EXPECT_THROW({
			from.shared_from_this();
		}, std::bad_weak_ptr);
	}
	{
		const auto& from = x[0];
		EXPECT_THROW({
			from.shared_from_this();
		}, std::bad_weak_ptr);
	}
}
TEST(sh_enable_shared_from_this, member_from_this_base)
{
	using namespace SingleInheritanceOnBase;
	struct Composite : public enable_shared_from_this<Composite>
	{
		Base m_base;
	};
	shared_ptr<Composite> x = make_shared<Composite>();
	{
		auto& from = x->m_base;
		EXPECT_THROW({
			from.shared_from_this();
		}, std::bad_weak_ptr);
	}
	{
		const auto& from = x->m_base;
		EXPECT_THROW({
			from.shared_from_this();
		}, std::bad_weak_ptr);
	}
}

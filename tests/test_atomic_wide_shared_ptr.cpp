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

#include <sh/atomic_wide_shared_ptr.hpp>
#include <thread>

using sh::atomic_wide_shared_ptr;
using sh::atomic_wide_weak_ptr;
using sh::wide_shared_ptr;
using sh::wide_weak_ptr;

TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_ctor_default)
{
	atomic_wide_shared_ptr<int> x;
	ASSERT_FALSE(bool(x.load()));
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_ctor_nullptr)
{
	atomic_wide_shared_ptr<int> x{ nullptr };
	ASSERT_FALSE(bool(x.load()));
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_ctor_shared_ptr)
{
	atomic_wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	ASSERT_TRUE(bool(x.load()));
	EXPECT_EQ(x.load().use_count(), 2u);
	EXPECT_EQ(*x.load(), 123);
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_store)
{
	{
		atomic_wide_shared_ptr<int> x;
		x.store(sh::make_shared<int>(123));
		ASSERT_TRUE(bool(x.load()));
		EXPECT_EQ(x.load().use_count(), 2u);
		EXPECT_EQ(*x.load(), 123);
	}
	{
		atomic_wide_shared_ptr<const int> x;
		x.store(sh::make_shared<int>(123));
		ASSERT_TRUE(bool(x.load()));
		EXPECT_EQ(x.load().use_count(), 2u);
		EXPECT_EQ(*x.load(), 123);
	}
	{
		atomic_wide_shared_ptr<const int> x;
		x.store(sh::make_shared<const int>(123));
		ASSERT_TRUE(bool(x.load()));
		EXPECT_EQ(x.load().use_count(), 2u);
		EXPECT_EQ(*x.load(), 123);
	}
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_load)
{
	atomic_wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	sh::wide_shared_ptr<int> y = x.load();
	ASSERT_TRUE(bool(y));
	EXPECT_EQ(y.use_count(), 2u);
	EXPECT_EQ(*y, 123);
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_exchange)
{
	{
		atomic_wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
		sh::wide_shared_ptr<int> y = x.exchange(sh::make_shared<int>(456));
		ASSERT_TRUE(bool(y));
		EXPECT_EQ(y.use_count(), 1u);
		EXPECT_EQ(*y, 123);
		ASSERT_TRUE(bool(x.load()));
		EXPECT_EQ(x.load().use_count(), 2u);
		EXPECT_EQ(*x.load(), 456);
	}
	{
		atomic_wide_shared_ptr<const int> x{ sh::make_shared<const int>(123) };
		sh::wide_shared_ptr<const int> y = x.exchange(sh::make_shared<const int>(456));
		ASSERT_TRUE(bool(y));
		EXPECT_EQ(y.use_count(), 1u);
		EXPECT_EQ(*y, 123);
		ASSERT_TRUE(bool(x.load()));
		EXPECT_EQ(x.load().use_count(), 2u);
		EXPECT_EQ(*x.load(), 456);
	}
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_compare_exchange_strong)
{
	const sh::wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_shared_ptr<int> y{ sh::make_shared<int>(456) };
	atomic_wide_shared_ptr<int> z{ x };
	{
		sh::wide_shared_ptr<int> expected = y;
		const bool result = z.compare_exchange_strong(expected, y);
		EXPECT_FALSE(result);
		EXPECT_EQ(expected.get(), x.get());
	}
	{
		sh::wide_shared_ptr<int> expected = x;
		const bool result = z.compare_exchange_strong(expected, y);
		EXPECT_TRUE(result);
		EXPECT_EQ(expected.get(), x.get());
		ASSERT_TRUE(bool(z.load()));
		EXPECT_EQ(z.load().use_count(), 3u);
		EXPECT_EQ(*z.load(), 456);
	}
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_compare_exchange_weak)
{
	const sh::wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_shared_ptr<int> y{ sh::make_shared<int>(456) };
	atomic_wide_shared_ptr<int> z{ x };
	{
		sh::wide_shared_ptr<int> expected = y;
		const bool result = z.compare_exchange_weak(expected, y);
		EXPECT_FALSE(result);
		EXPECT_EQ(expected.get(), x.get());
	}
	{
		sh::wide_shared_ptr<int> expected = x;
		const bool result = z.compare_exchange_weak(expected, y);
		EXPECT_TRUE(result);
		EXPECT_EQ(expected.get(), x.get());
		ASSERT_TRUE(bool(z.load()));
		EXPECT_EQ(z.load().use_count(), 3u);
		EXPECT_EQ(*z.load(), 456);
	}
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_wait)
{
	const sh::wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_shared_ptr<int> y{ sh::make_shared<int>(456) };
	atomic_wide_shared_ptr<int> z{ x };
	{
		z.wait(y);
	}
	{
		std::thread x_waiter{
			[&x, &z]()
			{
				z.wait(x);
			}
		};

		z.store(y);
		z.notify_one();

		x_waiter.join();
	}
	{
		std::thread y_waiter{
			[&y, &z]()
			{
				z.wait(y);
			}
		};

		z.store(x);
		z.notify_all();

		y_waiter.join();
	}
	{
		std::atomic_flag waiting = ATOMIC_FLAG_INIT;
		std::thread x_waiter{
			[&x, &z, &waiting]()
			{
				waiting.test_and_set();
				waiting.notify_one();
				z.wait(x);
			}
		};

		waiting.wait(false);
		z.store(y);
		z.notify_one();

		x_waiter.join();
	}
	{
		std::atomic_flag waiting = ATOMIC_FLAG_INIT;
		std::thread y_waiter{
			[&y, &z, &waiting]()
			{
				waiting.test_and_set();
				waiting.notify_one();
				z.wait(y);
			}
		};

		waiting.wait(false);
		z.store(x);
		z.notify_all();

		y_waiter.join();
	}
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_wait_alias)
{
	struct TwoInts
	{
		int m_a{ 123 };
		int m_b{ 456 };
	};
	sh::wide_shared_ptr<TwoInts> w{ sh::make_shared<TwoInts>() };
	const sh::wide_shared_ptr<int> x{ w, &w->m_a };
	const sh::wide_shared_ptr<int> y{ w, &w->m_b };
	atomic_wide_shared_ptr<int> z{ x };
	{
		z.wait(y);
	}
	{
		std::thread x_waiter{
			[&x, &z]()
			{
				z.wait(x);
			}
		};

		z.store(y);
		z.notify_one();

		x_waiter.join();
	}
	{
		std::thread y_waiter{
			[&y, &z]()
			{
				z.wait(y);
			}
		};

		z.store(x);
		z.notify_one();

		y_waiter.join();
	}
	{
		std::atomic_flag waiting = ATOMIC_FLAG_INIT;
		std::thread x_waiter{
			[&x, &z, &waiting]()
			{
				waiting.test_and_set();
				waiting.notify_one();
				z.wait(x);
			}
		};

		waiting.wait(false);
		z.store(y);
		z.notify_one();

		x_waiter.join();
	}
	{
		std::atomic_flag waiting = ATOMIC_FLAG_INIT;
		std::thread y_waiter{
			[&y, &z, &waiting]()
			{
				waiting.test_and_set();
				waiting.notify_one();
				z.wait(y);
			}
		};

		waiting.wait(false);
		z.store(x);
		z.notify_one();

		y_waiter.join();
	}
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_notify_one)
{
	const sh::wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_shared_ptr<int> y{ sh::make_shared<int>(456) };
	atomic_wide_shared_ptr<int> z{ x };
	std::atomic<int> waiting{ 2 };

	std::thread t1{
		[&x, &z, &waiting]() { z.wait(x); waiting.fetch_sub(1); }
	};
	std::thread t2{
		[&x, &z, &waiting]() { z.wait(x); waiting.fetch_sub(1); }
	};

	z.store(y);
	z.notify_one();
	while (waiting.load() == 2)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	z.notify_one();

	t1.join();
	t2.join();

	EXPECT_EQ(waiting.load(), 0);
}
TEST(sh_atomic_wide_shared_ptr, atomic_wide_shared_ptr_notify_all)
{
	const sh::wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_shared_ptr<int> y{ sh::make_shared<int>(456) };
	atomic_wide_shared_ptr<int> z{ x };

	std::thread t1{
		[&x, &z]() { z.wait(x); }
	};
	std::thread t2{
		[&x, &z]() { z.wait(x); }
	};

	z.store(y);
	z.notify_all();

	t1.join();
	t2.join();
}

TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_ctor_default)
{
	sh::atomic_wide_weak_ptr<int> x;
	ASSERT_FALSE(bool(x.load().lock()));
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_ctor_nullptr)
{
	sh::atomic_wide_weak_ptr<int> x{ nullptr };
	ASSERT_FALSE(bool(x.load().lock()));
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_ctor_weak_ptr)
{
	wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	sh::atomic_wide_weak_ptr<int> y{ x };
	ASSERT_TRUE(bool(y.load().lock()));
	EXPECT_EQ(y.load().lock().use_count(), 2u);
	EXPECT_EQ(y.load().lock().get(), x.get());
	EXPECT_EQ(*y.load().lock(), 123);

	x.reset();
	EXPECT_FALSE(bool(y.load().lock()));
	EXPECT_EQ(y.load().lock().use_count(), 0u);
	EXPECT_EQ(y.load().lock().get(), nullptr);
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_store)
{
	{
		wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
		sh::atomic_wide_weak_ptr<int> y;
		y.store(x);
		ASSERT_TRUE(bool(y.load().lock()));
		EXPECT_EQ(y.load().lock().use_count(), 2u);
		EXPECT_EQ(y.load().lock().get(), x.get());
		EXPECT_EQ(*y.load().lock(), 123);

		y.store(sh::wide_weak_ptr<int>{});
		EXPECT_FALSE(bool(y.load().lock()));
		EXPECT_EQ(y.load().lock().use_count(), 0u);
		EXPECT_EQ(y.load().lock().get(), nullptr);

		y.store(x);
		x.reset();
		EXPECT_FALSE(bool(y.load().lock()));
		EXPECT_EQ(y.load().lock().use_count(), 0u);
		EXPECT_EQ(y.load().lock().get(), nullptr);
	}
	{
		wide_shared_ptr<const int> x{ sh::make_shared<const int>(123) };
		sh::atomic_wide_weak_ptr<const int> y;
		y.store(x);
		ASSERT_TRUE(bool(y.load().lock()));
		EXPECT_EQ(y.load().lock().use_count(), 2u);
		EXPECT_EQ(y.load().lock().get(), x.get());
		EXPECT_EQ(*y.load().lock(), 123);

		y.store(sh::wide_weak_ptr<const int>{});
		EXPECT_FALSE(bool(y.load().lock()));
		EXPECT_EQ(y.load().lock().use_count(), 0u);
		EXPECT_EQ(y.load().lock().get(), nullptr);

		y.store(x);
		x.reset();
		EXPECT_FALSE(bool(y.load().lock()));
		EXPECT_EQ(y.load().lock().use_count(), 0u);
		EXPECT_EQ(y.load().lock().get(), nullptr);
	}
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_load)
{
	wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	sh::atomic_wide_weak_ptr<int> y{ x };
	ASSERT_TRUE(bool(y.load().lock()));
	EXPECT_EQ(y.load().lock().use_count(), 2u);
	EXPECT_EQ(y.load().lock().get(), x.get());
	EXPECT_EQ(*y.load().lock(), 123);
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_exchange)
{
	{
		wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
		wide_shared_ptr<int> y{ sh::make_shared<int>(456) };
		sh::atomic_wide_weak_ptr<int> z{ x };
		z.exchange(y);
		ASSERT_TRUE(bool(z.load().lock()));
		EXPECT_EQ(z.load().lock().use_count(), 2u);
		EXPECT_EQ(z.load().lock().get(), y.get());
		EXPECT_EQ(*z.load().lock(), 456);
	}
	{
		wide_shared_ptr<const int> x{ sh::make_shared<const int>(123) };
		wide_shared_ptr<const int> y{ sh::make_shared<const int>(456) };
		sh::atomic_wide_weak_ptr<const int> z{ x };
		z.exchange(y);
		ASSERT_TRUE(bool(z.load().lock()));
		EXPECT_EQ(z.load().lock().use_count(), 2u);
		EXPECT_EQ(z.load().lock().get(), y.get());
		EXPECT_EQ(*z.load().lock(), 456);
	}
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_compare_exchange_strong)
{
	const sh::wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_shared_ptr<int> y{ sh::make_shared<int>(456) };
	sh::atomic_wide_weak_ptr<int> z{ x };
	{
		sh::wide_weak_ptr<int> expected{ y };
		const bool result = z.compare_exchange_strong(expected, y);
		EXPECT_FALSE(result);
		EXPECT_EQ(expected.lock().get(), x.get());
	}
	{
		sh::wide_weak_ptr<int> expected{ x };
		const bool result = z.compare_exchange_strong(expected, y);
		EXPECT_TRUE(result);
		EXPECT_EQ(expected.lock().get(), x.get());
		ASSERT_TRUE(bool(z.load().lock()));
		EXPECT_EQ(z.load().lock().use_count(), 2u);
		EXPECT_EQ(*z.load().lock(), 456);
	}
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_compare_exchange_weak)
{
	const sh::wide_shared_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_shared_ptr<int> y{ sh::make_shared<int>(456) };
	sh::atomic_wide_weak_ptr<int> z{ x };
	{
		sh::wide_weak_ptr<int> expected{ y };
		const bool result = z.compare_exchange_weak(expected, y);
		EXPECT_FALSE(result);
		EXPECT_EQ(expected.lock().get(), x.get());
	}
	{
		sh::wide_weak_ptr<int> expected{ x };
		const bool result = z.compare_exchange_weak(expected, y);
		EXPECT_TRUE(result);
		EXPECT_EQ(expected.lock().get(), x.get());
		ASSERT_TRUE(bool(z.load().lock()));
		EXPECT_EQ(z.load().lock().use_count(), 2u);
		EXPECT_EQ(*z.load().lock(), 456);
	}
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_wait)
{
	const sh::wide_weak_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_weak_ptr<int> y{ sh::make_shared<int>(456) };
	sh::atomic_wide_weak_ptr<int> z{ x };
	{
		z.wait(y);
	}
	{
		std::thread x_waiter{
			[&x, &z]() { z.wait(x); }
		};

		z.store(y);
		z.notify_one();

		x_waiter.join();
	}
	{
		std::thread y_waiter{
			[&y, &z]() { z.wait(y); }
		};

		z.store(x);
		z.notify_all();

		y_waiter.join();
	}
	{
		std::atomic_flag waiting = ATOMIC_FLAG_INIT;
		std::thread x_waiter{
			[&x, &z, &waiting]()
			{
				waiting.test_and_set();
				waiting.notify_one();
				z.wait(x);
			}
		};

		waiting.wait(false);
		z.store(y);
		z.notify_one();

		x_waiter.join();
	}
	{
		std::atomic_flag waiting = ATOMIC_FLAG_INIT;
		std::thread y_waiter{
			[&y, &z, &waiting]()
			{
				waiting.test_and_set();
				waiting.notify_one();
				z.wait(y);
			}
		};

		waiting.wait(false);
		z.store(x);
		z.notify_one();

		y_waiter.join();
	}
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_wait_alias)
{
	struct TwoInts
	{
		int m_a{ 123 };
		int m_b{ 456 };
	};
	sh::wide_shared_ptr<TwoInts> w{ sh::make_shared<TwoInts>() };
	const sh::wide_weak_ptr<int> x{ sh::wide_shared_ptr<int>{ w, &w->m_a } };
	const sh::wide_weak_ptr<int> y{ sh::wide_shared_ptr<int>{ w, &w->m_b } };
	atomic_wide_weak_ptr<int> z{ x };
	{
		z.wait(y);
	}
	{
		std::thread x_waiter{
			[&x, &z]()
			{
				z.wait(x);
			}
		};

		z.store(y);
		z.notify_one();

		x_waiter.join();
	}
	{
		std::thread y_waiter{
			[&y, &z]()
			{
				z.wait(y);
			}
		};

		z.store(x);
		z.notify_one();

		y_waiter.join();
	}
	{
		std::atomic_flag waiting = ATOMIC_FLAG_INIT;
		std::thread x_waiter{
			[&x, &z, &waiting]()
			{
				waiting.test_and_set();
				waiting.notify_one();
				z.wait(x);
			}
		};

		waiting.wait(false);
		z.store(y);
		z.notify_one();

		x_waiter.join();
	}
	{
		std::atomic_flag waiting = ATOMIC_FLAG_INIT;
		std::thread y_waiter{
			[&y, &z, &waiting]()
			{
				waiting.test_and_set();
				waiting.notify_one();
				z.wait(y);
			}
		};

		waiting.wait(false);
		z.store(x);
		z.notify_one();

		y_waiter.join();
	}
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_notify_one)
{
	const sh::wide_weak_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_weak_ptr<int> y{ sh::make_shared<int>(456) };
	sh::atomic_wide_weak_ptr<int> z{ x };
	std::atomic<int> waiting{ 2 };

	std::thread t1{
		[&x, &z, &waiting]() { z.wait(x); waiting.fetch_sub(1); }
	};
	std::thread t2{
		[&x, &z, &waiting]() { z.wait(x); waiting.fetch_sub(1); }
	};

	z.store(y);
	z.notify_one();
	while (waiting.load() == 2)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	z.notify_one();

	t1.join();
	t2.join();

	EXPECT_EQ(waiting.load(), 0);
}
TEST(sh_atomic_wide_weak_ptr, atomic_wide_weak_ptr_notify_all)
{
	const sh::wide_weak_ptr<int> x{ sh::make_shared<int>(123) };
	const sh::wide_weak_ptr<int> y{ sh::make_shared<int>(456) };
	sh::atomic_wide_weak_ptr<int> z{ x };

	std::thread t1{
		[&x, &z]() { z.wait(x); }
	};
	std::thread t2{
		[&x, &z]() { z.wait(x); }
	};

	z.store(y);
	z.notify_all();

	t1.join();
	t2.join();
}

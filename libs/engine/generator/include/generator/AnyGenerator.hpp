#pragma once
#include <memory>


namespace generator {


/// A type erasing container that can store any generator that generates type T.
/// @tparam T Type returned by the generate() -function.
template <typename T>
class AnyGenerator
{
public:

	template <typename Generator>
	AnyGenerator(Generator generator) :
		m_base{new Derived<Generator>{std::move(generator)}}
	{ }

	AnyGenerator(const AnyGenerator& that) : m_base{that.m_base->Clone()} { }

	AnyGenerator(AnyGenerator&&) = default;

	AnyGenerator& operator=(const AnyGenerator& that) {
		m_base = that.m_base->Clone();
		return *this;
	}

	AnyGenerator& operator=(AnyGenerator&&) = default;

	T Generate() const { return m_base->Generate(); }

	bool Done() const noexcept { return m_base->Done(); }

	void Next() { m_base->Next(); }

private:

	class Base {
	public:
		virtual ~Base() { }
		virtual std::unique_ptr<Base> Clone() const = 0;
		virtual T Generate() const = 0;
		virtual bool Done() const noexcept = 0;
		virtual void Next() = 0;
	};

	template <typename Generator>
	class Derived : public Base {
	public:

		Derived(Generator generator) : m_generator{std::move(generator)} { }

		virtual std::unique_ptr<Base> Clone() const override {
			return std::unique_ptr<Base>(new Derived{m_generator});
		}

		virtual T Generate() const override {
			return m_generator.Generate();
		}

		virtual bool Done() const noexcept override {
			return m_generator.Done();
		}

		virtual void Next() override {
			m_generator.Next();
		}

	private:

		Generator m_generator;

	};

	std::unique_ptr<Base> m_base;

};

}



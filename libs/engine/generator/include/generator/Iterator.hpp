#pragma once
#include <cassert>
#include <stdexcept>


#include "utils.hpp"


namespace generator {




/// An iterator that can be used to "drive" a generator.
/// Note that iterator mutates the generator.
template <typename Generator>
class Iterator
{
private:

	Generator* m_generator;

	typename GeneratedType<Generator>::Type m_value;

public:

	using iterator_category = std::input_iterator_tag;

	using value_type = typename GeneratedType<Generator>::Type;

	using difference_type = std::ptrdiff_t;

	using pointer = value_type*;

	using reference = value_type&;

	/// Creates a dummy end iterator.
	Iterator() noexcept :
		m_generator{nullptr},
		m_value{}
	{ }

	/// Iterator to the given generator.
	Iterator(Generator& generator) noexcept :
		m_generator{&generator},
		m_value{}
	{
		if (m_generator->Done()) m_generator = nullptr;
		else m_value = m_generator->Generate();
	}

	/// Advance the iterator.
	/// Might make the iterator "out of range".
	/// Précondition : l'itérateur n'est pas épuisé (vérifiée par assertion).
	Iterator& operator++() {
		assert(!(!m_generator) && "générateur épuisé");
		m_generator->Next();
		if (m_generator->Done()) m_generator = nullptr;
		else m_value = m_generator->Generate();
		return *this;
	}

	/// Get reference to the current generated value.
	/// Précondition : l'itérateur n'est pas épuisé (vérifiée par assertion).
	const typename Iterator::value_type& operator*() const {
		assert(!(!m_generator) && "générateur épuisé");
		return m_value;
	}

	/// Get pointer to the current generated value.
	/// Précondition : l'itérateur n'est pas épuisé (vérifiée par assertion).
	const typename Iterator::value_type* operator->() const {
		assert(!(!m_generator) && "générateur épuisé");
		return &m_value;
	}

	/// Iterators are equal if both are out of range or both iterate the same
	/// generator.
	bool operator==(const Iterator& that) const noexcept {
		return m_generator == that.m_generator;
	}

	bool operator!=(const Iterator& that) const noexcept {
		return m_generator != that.m_generator;
	}

};


/// Will return an iterator to the generator.
template <typename Generator>
Iterator<Generator> begin(Generator& generator) noexcept {
	return Iterator<Generator>{generator};
}


/// Returns a dummy end iterator
template <typename Generator>
Iterator<Generator> end(const Generator&) noexcept {
	return Iterator<Generator>{};
}


}



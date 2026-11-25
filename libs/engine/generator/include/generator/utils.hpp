#pragma once
namespace generator {


/// Will have a type named "Type" that has same type as value returned by method
/// generate() of type Generator.
template <typename Generator>
class GeneratedType {
public:

	using Type =  decltype(static_cast<const Generator*>(nullptr)->Generate());

};


/// Will have a type named "Type" that has same type as value returned by method
/// edges() for type Primitive.
template <typename Primitive>
class EdgeGeneratorType {
public:

	using Type =  decltype(static_cast<const Primitive*>(nullptr)->Edges());

};


/// Will have a type named "Type" that has same type as value returned by method
/// triangles() for type Primitive.
template <typename Primitive>
class TriangleGeneratorType {
public:

	using Type =  decltype(static_cast<const Primitive*>(nullptr)->Triangles());

};


/// Will have a type named "Type" that has same type as value returned by method
/// vertices() for type Primitive.
template <typename Primitive>
class VertexGeneratorType {
public:

	using Type =  decltype(static_cast<const Primitive*>(nullptr)->Vertices());

};


/// Counts the number of steps left in the generator.
template <typename Generator>
int Count(const Generator& generator) noexcept {
	Generator temp{generator};
	int c = 0;
	while (!temp.Done()) {
		++c;
		temp.Next();
	}
	return c;
}


}


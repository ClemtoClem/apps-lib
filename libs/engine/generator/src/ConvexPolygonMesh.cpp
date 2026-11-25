// Copyright 2016 Markus Ilmola
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1f of the License, or (at your option) any later version.

#include <cassert>
#include "generator/ConvexPolygonMesh.hpp"

#include "generator/CircleShape.hpp"
#include "generator/Iterator.hpp"

using namespace generator;



ConvexPolygonMesh::TriangleGenerator::TriangleGenerator(const ConvexPolygonMesh& mesh) noexcept :
	m_mMesh{&mesh},
	m_mOdd{false},
	m_mSegmentIndex{0},
	m_mSideIndex{0},
	m_mRingIndex{0}
{

}


bool ConvexPolygonMesh::TriangleGenerator::Done() const noexcept {
	return
		m_mMesh->m_mSegments == 0 || m_mMesh->m_mVertices.size() < 3 ||
		m_mRingIndex == m_mMesh->m_mRings;
}


Triangle ConvexPolygonMesh::TriangleGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	Triangle triangle{};

	const int verticesPerRing = m_mMesh->m_mSegments * static_cast<int>(m_mMesh->m_mVertices.size());
	const int delta = m_mRingIndex * verticesPerRing + 1;

	const int n1 = m_mSideIndex * m_mMesh->m_mSegments + m_mSegmentIndex;
	const int n2 = (n1 + 1) % verticesPerRing;

	if (m_mRingIndex == m_mMesh->m_mRings - 1) {
		triangle.vertices[0] = 0;
		triangle.vertices[1] = n1 + delta;
		triangle.vertices[2] = n2 + delta;
	}
	else {
		if (!m_mOdd) {
			triangle.vertices[0] = n1 + delta;
			triangle.vertices[1] = n2 + delta;
			triangle.vertices[2] = n1 + verticesPerRing + delta;
		}
		else {
			triangle.vertices[0] = n2 + delta;
			triangle.vertices[1] = n2 + verticesPerRing + delta;
			triangle.vertices[2] = n1 + verticesPerRing + delta;
		}
	}

	return triangle;
}




void ConvexPolygonMesh::TriangleGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	if (m_mRingIndex == m_mMesh->m_mRings - 1) {
		++m_mSegmentIndex;

		if (m_mSegmentIndex == m_mMesh->m_mSegments) {
			m_mSegmentIndex = 0;

			++m_mSideIndex;

			if (m_mSideIndex == static_cast<int>(m_mMesh->m_mVertices.size())) {
				++m_mRingIndex;
			}
		}
	}
	else {

		m_mOdd = !m_mOdd;

		if (!m_mOdd) {

			++m_mSegmentIndex;

			if (m_mSegmentIndex == m_mMesh->m_mSegments) {
				m_mSegmentIndex = 0;

				++m_mSideIndex;

				if (m_mSideIndex == static_cast<int>(m_mMesh->m_mVertices.size())) {
					m_mSideIndex = 0;
					m_mOdd = false;

					++m_mRingIndex;
				}
			}

		}
	}
}



ConvexPolygonMesh::VertexGenerator::VertexGenerator(const ConvexPolygonMesh& mesh) noexcept :
	m_mMesh{&mesh},
	m_mCenterDone{false},
	m_mSegmentIndex{0},
	m_mSideIndex{0},
	m_mRingIndex{0}
{
}


bool ConvexPolygonMesh::VertexGenerator::Done() const noexcept {
	return
		m_mMesh->m_mSegments == 0 || m_mMesh->m_mRings == 0 ||
		m_mMesh->m_mVertices.size() < 3 || m_mRingIndex == m_mMesh->m_mRings;
}




MeshVertex ConvexPolygonMesh::VertexGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	MeshVertex vertex{};

	if (m_mCenterDone) {

		const float ringDelta = static_cast<float>(m_mRingIndex) / static_cast<float>(m_mMesh->m_mRings);
		const float segmentDelta = static_cast<float>(m_mSegmentIndex) / static_cast<float>(m_mMesh->m_mSegments);

		const size_t nextSide = (static_cast<size_t>(m_mSideIndex) + 1) % m_mMesh->m_mVertices.size();
		const math::FVector3 a = math::Lerp(m_mMesh->m_mVertices.at(static_cast<size_t>(m_mSideIndex)), m_mMesh->m_mCenter, ringDelta);
		const math::FVector3 b = math::Lerp(m_mMesh->m_mVertices.at(nextSide), m_mMesh->m_mCenter, ringDelta);

		vertex.position = math::Lerp(a, b, segmentDelta);
	}
	else {
		vertex.position = m_mMesh->m_mCenter;
	}

	vertex.normal = m_mMesh->m_mNormal;

	const math::FVector3 delta = vertex.position - m_mMesh->m_mCenter;

	vertex.texCoord[0] = math::Dot(m_mMesh->m_mTangent, delta);
	vertex.texCoord[1] = math::Dot(m_mMesh->m_mBitangent, delta);

	vertex.texCoord -= m_mMesh->m_mTexDelta;

	return vertex;
}




void ConvexPolygonMesh::VertexGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	if (!m_mCenterDone) {
		m_mCenterDone = true;
	}
	else {

		++m_mSegmentIndex;

		if (m_mSegmentIndex == m_mMesh->m_mSegments) {
			m_mSegmentIndex = 0;

			++m_mSideIndex;

			if (m_mSideIndex == static_cast<int>(m_mMesh->m_mVertices.size())) {

				m_mSideIndex = 0;

				++m_mRingIndex;
			}
		}
	}
}


namespace {


std::vector<math::FVector3> MakeVertices(float radius, int sides) noexcept {
	std::vector<math::FVector3> result{};

	CircleShape circle{radius, sides};
	for (const auto& v : circle.Vertices()) {
		result.push_back(math::FVector3{v.position[0], v.position[1], 0.0f});
	}
	result.pop_back(); // The last one is a dublicate with the first one

	return result;
}



std::vector<math::FVector3> ConvertVertices(const std::vector<math::FVector2>& vertices) noexcept {
	std::vector<math::FVector3> result(vertices.size());

	for (std::size_t i = 0; i < vertices.size(); ++i) {
		result.at(i) = math::FVector3{vertices.at(i)[0], vertices.at(i)[1], 0.0f};
	}

	return result;
}


math::FVector3 CalcCenter(const std::vector<math::FVector3>& vertices) noexcept {
	math::FVector3 result{};
	for (const auto& v : vertices) {
		result += v;
	}
	return result / static_cast<float>(vertices.size());
}


math::FVector3 CalcNormal(const math::FVector3& center, const std::vector<math::FVector3>& vertices) {
	math::FVector3 normal{};
	for (size_t i = 0; i < vertices.size(); ++i) {
		normal += math::TriangleNormal(center, vertices[i], vertices[(i + 1) % vertices.size()]);
	}
	return math::Normalize(normal);
}


}


ConvexPolygonMesh::ConvexPolygonMesh(
	float radius, int sides, int segments, int rings
) noexcept :
	ConvexPolygonMesh{MakeVertices(radius, sides), segments, rings}
{ }


ConvexPolygonMesh::ConvexPolygonMesh(
	const std::vector<math::FVector2>& vertices, int segments, int rings
) noexcept :
	ConvexPolygonMesh{ConvertVertices(vertices), segments, rings}
{ }


ConvexPolygonMesh::ConvexPolygonMesh(
	std::vector<math::FVector3> vertices, int segments, int rings
) noexcept :
	m_mVertices{std::move(vertices)},
	m_mSegments{segments},
	m_mRings{rings},
	m_mCenter{CalcCenter(m_mVertices)},
	m_mNormal{CalcNormal(m_mCenter, m_mVertices)},
	m_mTangent{},
	m_mBitangent{},
	m_mTexDelta{}
{

	if (m_mVertices.size() > 0) {
		m_mTangent = math::Normalize(m_mVertices.at(0) - m_mCenter);
		m_mBitangent = math::Cross(m_mNormal, m_mTangent);

		math::FVector2 texMax{};

		for (const math::FVector3& vertex : m_mVertices) {
			math::FVector3 delta = vertex - m_mCenter;

			math::FVector2 uv{math::Dot(m_mTangent, delta), math::Dot(m_mBitangent, delta)};

			m_mTexDelta = math::Min(m_mTexDelta, uv);
			texMax = math::Max(texMax, uv);
		}

		math::FVector2 size = texMax - m_mTexDelta;

		m_mTangent /= size[0];
		m_mBitangent /= size[1];

		m_mTexDelta /= size;

	}
}


ConvexPolygonMesh::TriangleGenerator ConvexPolygonMesh::Triangles() const noexcept {
	return TriangleGenerator{*this};
}


ConvexPolygonMesh::VertexGenerator ConvexPolygonMesh::Vertices() const noexcept {
	return VertexGenerator{*this};
}



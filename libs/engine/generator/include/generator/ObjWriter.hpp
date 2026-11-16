#pragma once
#include <sstream>

#include "MeshVertex.hpp"
#include "Triangle.hpp"


namespace generator {


/// A class for generating obj files for preview and debug purposes.
class ObjWriter {
public:

	ObjWriter();

	template <typename Mesh>
	void WriteMesh(const Mesh& mesh) {
		int newBase = m_base;

		for (const MeshVertex& vertex : mesh.Vertices()) {
			++newBase;

			m_ss
			<< "v "
			<< vertex.position[0] << " "
			<< vertex.position[1] << " "
			<< vertex.position[2] << "\n";

			m_ss
			<< "vn "
			<< vertex.normal[0] << " "
			<< vertex.normal[1] << " "
			<< vertex.normal[2] << "\n";

			m_ss
			<< "vt "
			<< vertex.texCoord[0] << " "
			<< vertex.texCoord[1] << "\n";
		}

		for (const Triangle& triangle : mesh.Triangles()) {
			auto t = triangle.vertices + m_base;
			m_ss
			<< "f "
			<< t[0] << "/" << t[0] << "/" << t[0] << " "
			<< t[1] << "/" << t[1] << "/" << t[1] << " "
			<< t[2] << "/" << t[2] << "/" << t[2] << "\n";
		}

		m_base = newBase;
	}

	std::string Str() { return m_ss.str(); }

private:

	int m_base;

	std::stringstream m_ss;

};

}



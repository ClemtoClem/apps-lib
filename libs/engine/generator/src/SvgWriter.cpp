

#include <algorithm>
#include <sstream>
#include <memory>
#include <map>



#include "generator/SvgWriter.hpp"


using namespace generator;


namespace {

std::string ToColor(const math::FVector3& c) {
	std::stringstream ss;
	auto fn = [] (float channel) {
		channel = std::clamp(channel, 0.0f, 1.0f);
		return static_cast<int>(255*channel);
	};

	ss << "rgb(" << fn(c[0]) << "," << fn(c[1]) << "," << fn(c[2]) << ")";

	return ss.str();
}

}




SvgWriter::BaseElem::BaseElem(float depth, const math::FVector3& tint) :
	z{depth},
	color{tint}
{ }


SvgWriter::BaseElem::~BaseElem() { }


SvgWriter::VertexElem::VertexElem(const math::FVector3& point, const math::FVector3& tint) :
	BaseElem{point[2], tint},
	p{point}
{ }


void SvgWriter::VertexElem::Stream(std::ostream& os) const {
	os
	<< "<circle "
	<< "cx=\"" << p[0] << "\" cy=\"" << p[1] << "\" "
	<< "r=\"3\" style=\"fill: " << ToColor(color) << "\" />\n";
}



SvgWriter::LineElem::LineElem(
	const math::FVector3& a, const math::FVector3& b, const math::FVector3& tint
) :
	BaseElem{(a[2]+b[2])/2.0f, tint},
	p1(a),
	p2(b)
{ }


void SvgWriter::LineElem::Stream(std::ostream& os) const {
	os
	<< "<line "
	<< "x1=\""<< p1[0]  << "\" y1=\""<< p1[1] << "\" "
	<< "x2=\""<< p2[0]  << "\" y2=\""<< p2[1]  << "\" "
	<< "style=\"stroke: " << ToColor(color) << ";\" />\n";
}




SvgWriter::TriangleElem::TriangleElem(
	const math::FVector3& p1, const math::FVector3& p2, const math::FVector3& p3,
	const math::FVector3& tint
) :
	BaseElem{(p1[2]+p2[2]+p3[2])/3.0f, tint},
	p{{ p1, p2, p3 }}
{ }


void SvgWriter::TriangleElem::Stream(std::ostream& os) const {
	os
	<< "<polygon " << "points=\"";
	for (const math::FVector3& q : p) {
		os << q[0] << "," << q[1] << " ";
	}
	os << "\" ";
	os << "style=\"fill: " << ToColor(color)  << ";\" />\n";
}




math::FVector3 SvgWriter::Project(const math::FVector3& p) const {
	// Fenêtre SVG : y vers le bas, comme ProjectToViewport.
	return math::ProjectToViewport(p, m_viewProjMatrix, m_viewportOrigin.ToFloat(), m_viewportSize.ToFloat());
}

math::FVector3 SvgWriter::NormalToColor(const math::FVector3& normal) const {
	float d = math::Dot(normal, m_lightDir);
	d = std::clamp(d, 0.0f, 1.0f);
	d = 0.1f + 0.8f * d;
	return {d, d, d};
}






SvgWriter::SvgWriter(int width, int height) :
	m_size{width, height},
	m_viewMatrix{math::FMatrix4::Identity()},
	m_projMatrix{math::FMatrix4::Identity()},
	m_viewProjMatrix{math::FMatrix4::Identity()},
	m_viewportOrigin{0, 0},
	m_viewportSize{width, height},
	m_lightDir{1.0f, 2.0f, 3.0f},
	m_cullface{true},
	m_elems{}
{
	m_lightDir = math::Normalize(m_lightDir);
}


void SvgWriter::ModelView(const math::FMatrix4& matrix) {
	m_viewMatrix = matrix;
	m_viewProjMatrix = m_projMatrix * m_viewMatrix;
}


void SvgWriter::Perspective(float fovy, float aspect, float zNear, float zFar) {
	m_projMatrix = math::FMatrix4::Perspective(fovy, aspect, zNear, zFar);
	m_viewProjMatrix = m_projMatrix * m_viewMatrix;
}


void SvgWriter::Ortho(float left, float right, float bottom, float top) {
	m_projMatrix = math::FMatrix4::Ortho(left, right, bottom, top, -1.0f, 1.0f);
	m_viewProjMatrix = m_projMatrix * m_viewMatrix;
}


void SvgWriter::Viewport(int x, int y, int width, int height) {
	m_viewportOrigin = math::IVec2{x, y};
	m_viewportSize = math::IVec2{width, height};
}


void SvgWriter::Cullface(bool cullface) {
	m_cullface =  cullface;
}


void SvgWriter::WritePoint(const math::FVector3& p, const math::FVector3& color) {
	m_elems.push_back(std::unique_ptr<BaseElem>{new VertexElem{Project(p), color}});
}


void SvgWriter::WriteLine(
	const math::FVector3& p1, const math::FVector3& p2, const math::FVector3& color
) {
	if (p1 == p2) return;
	m_elems.push_back(
		std::unique_ptr<BaseElem>{new LineElem{Project(p1), Project(p2), color}}
	);
}


void SvgWriter::WriteTriangle(
	const math::FVector3& p1, const math::FVector3& p2, const math::FVector3& p3,
	const math::FVector3& color
) {
	if (p1 == p2 || p2 == p3 || p1 == p3) return;

	auto pp1 = Project(p1);
	auto pp2 = Project(p2);
	auto pp3 = Project(p3);

	if (m_cullface && math::TriangleNormal(pp1, pp2, pp3)[2] > 0.0f) return;

	m_elems.push_back(std::unique_ptr<BaseElem>{new TriangleElem(
		pp1, pp2, pp3, color
	)});
}


void SvgWriter::WriteTriangle(
	const math::FVector3& p1, const math::FVector3& p2, const math::FVector3& p3
) {
	WriteTriangle(p1, p2, p3, NormalToColor(math::TriangleNormal(p1, p2, p3)));
}


std::string SvgWriter::Str() const {
	std::stringstream ss;

	ss
	<< "<svg "
	<< "width=\"" << m_size[0] << "\" height=\"" << m_size[1] << "\" "
	<< "version=\"1.1f\" "
	<< "xmlns=\"http://www.w3.org/2000/svg\""
	<< ">\n";

	ss
	<< "<rect "
	<< "width=\"" << m_size[0] << "\" height=\"" << m_size[1] << "\" "
	<< "style=\"fill:white\""
	<< "/>\n";

	std::stable_sort(
		m_elems.begin(), m_elems.end(),
		[] (
			const std::unique_ptr<BaseElem>& e1,
			const std::unique_ptr<BaseElem>& e2
		) {
			return e1->z > e2->z;
		}
	);

	for (const auto &elem : m_elems) elem->Stream(ss);

	ss << "</svg>\n";

	return ss.str();
}


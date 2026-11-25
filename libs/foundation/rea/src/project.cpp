// Projet REA : sérialisation JSON (fondation.dataio).
#include "rea/project.hpp"

#include <dataio/json.hpp>

namespace rea {

Result<Project, String> Project::Load(const std::filesystem::path &path) {
	const auto json = dataio::LoadJsonFile(path.string());
	if (json.IsError())
		return Err(String(json.Error().Format()));

	const dataio::NodePtr &root = json.Value();
	Project p;
	p.binaryPath = std::string((*root)["binary"]->AsString());
	if (const auto a = ArchFromName((*root)["arch"]->AsString()); a)
		p.arch = *a;
	for (const dataio::NodePtr &ann : (*root)["annotations"]->AsArray()) {
		Annotation a;
		a.address = uint64_t((*ann)["address"]->AsInt());
		a.comment = std::string((*ann)["comment"]->AsString());
		a.name = std::string((*ann)["name"]->AsString());
		p.annotations.push_back(std::move(a));
	}
	return Ok(std::move(p));
}

bool Project::Save(const std::filesystem::path &path) const {
	dataio::NodePtr root = dataio::Node::MakeObject();
	root->Set("format", dataio::Node::MakeString("rea-project"));
	root->Set("version", dataio::Node::MakeInt(1));
	root->Set("binary", dataio::Node::MakeString(binaryPath));
	root->Set("arch", dataio::Node::MakeString(ArchName(arch)));

	dataio::NodePtr anns = dataio::Node::MakeArray();
	for (const Annotation &a : annotations) {
		dataio::NodePtr j = dataio::Node::MakeObject();
		j->Set("address", dataio::Node::MakeInt(int64_t(a.address)));
		j->Set("comment", dataio::Node::MakeString(a.comment));
		j->Set("name", dataio::Node::MakeString(a.name));
		anns->Push(std::move(j));
	}
	root->Set("annotations", std::move(anns));
	return dataio::SaveJsonFile(path.string(), root, 2);
}

const Annotation *Project::Find(uint64_t address) const noexcept {
	for (const Annotation &a : annotations)
		if (a.address == address)
			return &a;
	return nullptr;
}

void Project::Comment(uint64_t address, std::string text) {
	for (Annotation &a : annotations)
		if (a.address == address) {
			a.comment = std::move(text);
			return;
		}
	annotations.push_back({address, std::move(text), {}});
}

void Project::Rename(uint64_t address, std::string name) {
	for (Annotation &a : annotations)
		if (a.address == address) {
			a.name = std::move(name);
			return;
		}
	annotations.push_back({address, {}, std::move(name)});
}

} // namespace rea

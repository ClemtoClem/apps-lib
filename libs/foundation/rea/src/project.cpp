// Projet REA : sérialisation JSON (fondation.dataio).
#include "rea/project.hpp"

#include <dataio/json.hpp>

namespace rea {

Result<Project, String> Project::Load(const std::filesystem::path &path) {
	const auto json = dataio::LoadJsonFile(path.string());
	if (json.IsError())
		return Err(String(json.Error().Format()));

	const dataio::Json &root = json.Value();
	Project p;
	p.binaryPath = std::string(root["binary"].AsString());
	if (const auto a = ArchFromName(root["arch"].AsString()); a)
		p.arch = *a;
	for (const dataio::Json &ann : root["annotations"].AsArray()) {
		Annotation a;
		a.address = uint64_t(ann["address"].AsInt());
		a.comment = std::string(ann["comment"].AsString());
		a.name = std::string(ann["name"].AsString());
		p.annotations.push_back(std::move(a));
	}
	return Ok(std::move(p));
}

bool Project::Save(const std::filesystem::path &path) const {
	dataio::Json root = dataio::Json::MakeObject();
	root.Set("format", "rea-project");
	root.Set("version", 1);
	root.Set("binary", binaryPath);
	root.Set("arch", ArchName(arch));

	dataio::Json anns = dataio::Json::MakeArray();
	for (const Annotation &a : annotations) {
		dataio::Json j = dataio::Json::MakeObject();
		j.Set("address", uint64_t(a.address));
		j.Set("comment", a.comment);
		j.Set("name", a.name);
		anns.Push(std::move(j));
	}
	root.Set("annotations", std::move(anns));
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

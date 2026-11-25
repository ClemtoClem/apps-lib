#include "sandbox/document/project.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <dataio/json.hpp>

namespace sandbox {

namespace {
constexpr std::string_view kProjectFormat = "sandbox.project";
}

Result<Project, String> Project::Load(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return Err(String::Format("impossible de lire {}", path.string()));
	std::stringstream text;
	text << in.rdbuf();
	auto parsed = dataio::ParseJson(text.str());
	if (!parsed)
		return Err(String("projet : JSON invalide (" + parsed.Error().Format() + ")"));
	const dataio::NodePtr &j = parsed.Value();
	if ((*j)["format"]->AsString() != kProjectFormat)
		return Err(String::Format("projet : format « {} » attendu", kProjectFormat));
	Project p;
	p.m_file = path;
	p.name = String((*j)["name"]->AsString());
	if (p.name.IsEmpty())
		p.name = String(path.stem().string());
	p.activeScene = String((*j)["active_scene"]->AsString());
	const auto list = [&](std::string_view key, std::vector<String> &out) {
		for (const dataio::NodePtr &e : (*j)[key]->AsArray())
			if (e->IsString())
				out.push_back(String(e->AsString()));
	};
	list("scenes", p.scenes);
	list("objects", p.objects);
	list("models", p.models);
	for (const dataio::NodePtr &s : (*j)["scripts"]->AsArray())
		p.scripts.push_back({String((*s)["name"]->AsString()), String((*s)["description"]->AsString()), String((*s)["file"]->AsString())});
	return Ok(std::move(p));
}

Project Project::Create(const std::filesystem::path &file, String name) {
	Project p;
	p.m_file = file;
	p.name = std::move(name);
	return p;
}

Option<String> Project::Save() const {
	dataio::NodePtr j = dataio::Node::MakeObject();
	j->Set("format", dataio::Node::MakeString(std::string(kProjectFormat)));
	j->Set("version", dataio::Node::MakeInt(int64_t(1)));
	j->Set("name", dataio::Node::MakeString(std::string(name)));
	j->Set("active_scene", dataio::Node::MakeString(std::string(activeScene)));
	const auto list = [&](std::string_view key, const std::vector<String> &items) {
		dataio::NodePtr a = dataio::Node::MakeArray();
		for (const String &s : items)
			a->Push(dataio::Node::MakeString(std::string(s)));
		j->Set(key, std::move(a));
	};
	list("scenes", scenes);
	list("objects", objects);
	list("models", models);
	dataio::NodePtr scriptList = dataio::Node::MakeArray();
	for (const ProjectScript &s : scripts) {
		dataio::NodePtr o = dataio::Node::MakeObject();
		o->Set("name", dataio::Node::MakeString(std::string(s.name)));
		o->Set("description", dataio::Node::MakeString(std::string(s.description)));
		o->Set("file", dataio::Node::MakeString(std::string(s.file)));
		scriptList->Push(std::move(o));
	}
	j->Set("scripts", std::move(scriptList));
	std::error_code ec;
	std::filesystem::create_directories(Folder(), ec);
	std::ofstream out(m_file, std::ios::binary);
	if (!out)
		return Some(String::Format("impossible d'écrire {}", m_file.string()));
	const std::string text = dataio::WriteJson(j, 2);
	out.write(text.data(), std::streamsize(text.size()));
	return out ? Option<String>(NONE) : Some(String::Format("écriture incomplète de {}", m_file.string()));
}

String Project::Relative(const std::filesystem::path &file) const {
	std::error_code ec;
	const std::filesystem::path rel = std::filesystem::relative(file, Folder(), ec);
	return String((ec || rel.empty() ? file : rel).generic_string());
}

std::optional<std::filesystem::path> Project::ActiveScene() const {
	for (const String &s : scenes)
		if (std::filesystem::path(s.c_str()).stem().string() == std::string_view(activeScene))
			return Resolve(s);
	if (!scenes.empty())
		return Resolve(scenes.front());
	return std::nullopt;
}

std::optional<std::filesystem::path> Project::ScriptFile(std::string_view module) const {
	for (const ProjectScript &s : scripts)
		if (std::string_view(s.name) == module && !s.file.IsEmpty())
			return Resolve(s.file);
	const std::filesystem::path fallback = Folder() / "scripts" / (std::string(module) + ".script");
	std::error_code ec;
	if (std::filesystem::exists(fallback, ec))
		return fallback;
	return std::nullopt;
}

void Project::AddUnique(std::vector<String> &list, const String &relative) {
	if (std::ranges::find(list, relative) == list.end())
		list.push_back(relative);
}

} // namespace sandbox

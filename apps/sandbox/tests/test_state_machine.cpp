#include <cpp-maker/test.hpp>

#include <string>

#include <sandbox/game/state_machine.hpp>

using namespace sandbox::game;

TEST(StateMachine_DefaultStateAndBlend) {
	StateMachine m;
	m.AddState("repos", {0.f, 0.f});
	m.AddState("marche", {1.f, 1.f});
	// Le premier état ajouté devient courant.
	CHECK_EQ(std::string(m.Current()), "repos");
	CHECK_EQ(m.Pose().size(), size_t(2));
	CHECK_NEAR(m.Pose()[0], 0.f, 1e-6f);
	// Transition déclenchée : fondu sur 0,4 s.
	m.AddTransition("repos", "marche", "avance", 0.4f);
	m.SetParameter("avance", true);
	m.Tick(0.2f); // à mi-fondu
	CHECK(m.Blending());
	CHECK_EQ(std::string(m.Current()), "repos"); // encore en transition
	CHECK_NEAR(m.Pose()[0], 0.5f, 1e-4f);
	CHECK_NEAR(m.Pose()[1], 0.5f, 1e-4f);
	m.Tick(0.2f); // fondu terminé
	CHECK_FALSE(m.Blending());
	CHECK_EQ(std::string(m.Current()), "marche");
	CHECK_NEAR(m.Pose()[0], 1.f, 1e-6f);
}

TEST(StateMachine_NoTransitionWithoutCondition) {
	StateMachine m;
	m.AddState("repos", {0.f});
	m.AddState("marche", {1.f});
	m.AddTransition("repos", "marche", "avance", 0.4f);
	m.Tick(1.f); // « avance » n'est pas vrai : rien ne change
	CHECK_EQ(std::string(m.Current()), "repos");
	CHECK_NEAR(m.Pose()[0], 0.f, 1e-6f);
}

TEST(StateMachine_ForceStateSkipsBlend) {
	StateMachine m;
	m.AddState("repos", {0.f});
	m.AddState("marche", {1.f});
	m.SetState("marche"); // immédiat, sans fondu
	CHECK_EQ(std::string(m.Current()), "marche");
	CHECK_NEAR(m.Pose()[0], 1.f, 1e-6f);
}

TEST(StateMachine_ChainOfStates) {
	StateMachine m;
	m.AddState("repos", {0.f});
	m.AddState("marche", {1.f});
	m.AddState("course", {2.f});
	m.AddTransition("repos", "marche", "avance", 0.2f);
	m.AddTransition("marche", "course", "court", 0.2f);
	m.SetParameter("avance", true);
	m.Tick(0.2f); // → marche
	CHECK_EQ(std::string(m.Current()), "marche");
	m.SetParameter("court", true);
	m.Tick(0.2f); // → course
	CHECK_EQ(std::string(m.Current()), "course");
	CHECK_NEAR(m.Pose()[0], 2.f, 1e-6f);
}

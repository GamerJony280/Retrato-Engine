#include "ember/config.hpp"
#include "ember/runtime.hpp"
#include "nova/compiler.hpp"
#include "nova/support.hpp"
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void rejects(const std::function<void()> &fn, const std::string &fragment) {
    try {
        fn();
    } catch (const std::exception &e) {
        if (std::string(e.what()).find(fragment) != std::string::npos)
            return;
        throw std::runtime_error("Wrong diagnostic: " + std::string(e.what()));
    }
    throw std::runtime_error("Expected rejection: " + fragment);
}
void bad(const std::string &source, const std::string &message) {
    rejects([&] { nova::compile(source, "player.nova"); }, message);
}
} // namespace
int main(int argc, char **argv) {
    try {
        auto tokens = nova::lex("// comment\nvar a: float = 1.25e-2\n/* multi\nline */ print(\"a\\n\\\"b\")",
                                "lexer.nova");
        check(tokens[1].text == "var" && tokens[1].location.line == 2 && tokens[1].location.column == 1,
              "lexer source position");
        bool number = false, string = false;
        for (auto &t : tokens) {
            number |= t.kind == nova::TokenKind::Float && t.text == "1.25e-2";
            string |= t.kind == nova::TokenKind::String && t.text == "a\n\"b";
        }
        check(number && string, "lexer literals");
        bad("var x: int = @", "unexpected character");
        bad("/* broken", "unterminated comment");
        bad("print(\"bad)", "unterminated string");
        bad("print(\"\\q\")", "unknown string escape");
        bad("var x: float = 1e+", "invalid exponent");
        bad("var x: int = 99999999999999999999999999", "numeric literal out of range");
        bad("var x: float = 1e999", "numeric literal out of range");
        auto ast = nova::parse(nova::lex(
            "var x: int = 1 + 2 * 3\nfunc f(a: int) -> int { if a > 0 { return a } else { return 0 } }",
            "ast.nova"));
        check(ast.startup[0].expr->text == "+" && ast.startup[0].expr->args[1]->text == "*", "precedence");
        nova::analyze(ast);
        bad("var x int = 0", "expected ':'");
        bad("func f() {", "expected '}'");
        bad("var x: int =", "expected expression");
        bad("var x: int = 0 var y: int = 1", "expected newline");
        bad("var x: int = speeed", "player.nova:1:14: error: unknown variable 'speeed'");
        bad("var x: int = 1\nvar x: int = 2", "duplicate variable");
        bad("var x: int = true", "expected int, got bool");
        bad("var x: int = 1.5", "expected int, got float");
        bad("func f(x: int) {}\nfunc start() { f() }", "wrong function argument count");
        bad("func f(x: int) {}\nfunc start() { f(false) }", "expected int, got bool");
        bad("func f() { return 1 }", "void function cannot return");
        bad("func f() -> int { return }", "return value required");
        bad("func f() -> int { if true { return 1 } }", "not all paths return");
        bad("func f() -> int { return false }", "expected int, got bool");
        bad("return 1", "return outside function");
        bad("func f() { { var x: int = 2 } print(x) }", "unknown variable");
        bad("func update(d: int) {}", "invalid lifecycle signature");
        bad("func f(a: int,a: int) {}", "duplicate variable");
        bad("func f() {}\nfunc f() {}", "duplicate or reserved function");
        bad("var x: int = 0\n1 = x", "writable");
        bad("var x: Vec2 = Vec2(1,2)\nx.z = 2", "unknown property");
        bad("Vec3(1,2,3).x = 2", "writable");
        bad("print(1,2)", "print expects");
        bad("if 1 {}", "expected bool");
        bad("while 1 {}", "expected bool");
        bad("var x: bool = true + false", "invalid operator");
        bad("var x: bool = print(1) == print(2)", "invalid operator");
        bad("var x: float = 1.0 % 2", "invalid operator");
        bad("var x: int = !1", "invalid unary");
        bad("var x: Entity = self\nx.method()", "method/indirect calls");
        bad("func f() -> int { return 1 }\nvar x: int = f()", "global initialization");
        std::string deep = "var x: int = 0";
        for (int i = 0; i < 200; ++i)
            deep += " + 1";
        bad(deep, "exceeds 128");
        std::string cpp = nova::compile(
            "var speed: float = 2.0\nfunc update(delta: float) { transform.position.x += speed * delta }",
            "move.nova");
        check(cpp.find("host_.on_update") != std::string::npos &&
                  cpp.find("nova::fmul") != std::string::npos &&
                  cpp.find("NOVA_PROGRAM_NAME") != std::string::npos &&
                  cpp.find("#line 2 \"move.nova\"") != std::string::npos,
              "code generation and mapping");
        check(cpp == nova::compile("var speed: float = 2.0\nfunc update(delta: float) { transform.position.x "
                                   "+= speed * delta }",
                                   "move.nova"),
              "deterministic code generation");
        check(cpp.find("auto &lhs_ =") == std::string::npos &&
                  cpp.find("auto rhs_ =") == std::string::npos &&
                  cpp.find("[[maybe_unused]] double") != std::string::npos,
              "generated temporaries have unique names and unused parameters are annotated");
        check(cpp.find("nv_speed") != std::string::npos, "Readable native variable names");
        auto names = nova::compile("var _x: int = 1\nvar nh_5f78: int = 2\nvar a__b: int = 3", "names.nova");
        check(names.find("nh_5f78") != std::string::npos && names.find("nv_nh_5f78") != std::string::npos, "Reserved identifier encoding and disjoint namespace");
        nova::compile("var class: int = 2\nfunc template() -> int { return class }", "keywords.nova");
        nova::compile("var a: int = 1\nvar b: int = 2\na = b = 3\nvar c: bool = a < b || a >= 0 && a != 9",
                      "ops.nova");
        nova::compile("print(\"if\")\nprint(\")\")", "strings.nova");
        nova::compile("func start() {\nvar e: Entity = create(\"temporary\")\n"
                      "if alive(e) { destroy(e) }\nvar mouse: Vec2 = mouse_position() + mouse_delta()\n"
                      "var pressed: bool = key_down(1) || mouse_down(1) || gamepad_down(0,1)\n"
                      "var axis: float = gamepad_axis(0,0)\n"
                      "save_set_int(\"health\", 100)\nif save_has(\"health\") { "
                      "var health: int = save_get_int(\"health\") }\n}",
                      "game_api.nova");
        auto physics_cpp = nova::compile(
            "func start() {\nvar e: Entity = create(\"body\")\nphysics_box(e, Vec3(0.5,0.5,0.5), true)\n"
            "physics_set_velocity(e, Vec3(1,0,0))\nphysics_impulse(e, Vec3(0,1,0))\n}",
            "physics_api.nova");
        check(physics_cpp.find("BodyShape::box") != std::string::npos &&
                  physics_cpp.find("host_.add_impulse") != std::string::npos,
              "Nova advanced physics bindings generate through Host");
        rejects([] { nova::iadd(INT64_MAX, 1); }, "overflow");
        rejects([] { nova::isub(INT64_MIN, 1); }, "overflow");
        rejects([] { nova::imul(INT64_MIN, -1); }, "overflow");
        rejects([] { nova::idiv(INT64_MIN, -1); }, "overflow");
        rejects([] { nova::idiv(1, 0); }, "zero");
        rejects([] { nova::fdiv(1, 0); }, "zero");
        check(nova::imul(-3, -2) == 6 && nova::imod(INT64_MIN, -1) == 0, "checked arithmetic");
        rejects([] { nova::located("fault.nova", 3, 8, [] { nova::idiv(1, 0); }); },
                "fault.nova:3:8: runtime error");
        auto array_cpp=nova::compile("var a: Vec3[]=array_vec3(3,Vec3(0,0,0))\nfunc read(a: Vec3[]) -> float { return a[0].x }\nfunc start() { a[1].x+=2; print(len(a)) }", "arrays.nova");
        check(array_cpp.find("nova::at")!=std::string::npos && array_cpp.find("const nova::Array<ember::Vec3> &")!=std::string::npos,"array code generation uses checked indexing and borrowed params");
        bad("var a: int[]=array_int(1,0)\na[true]=1","expected int");
        bad("var a: int[]=array_int(1,0)\na[0]=false","cannot assign bool to int");
        bad("func f(a: int[]) { a[0]=1 }","writable");
        bad("var a: int[]=array_int(1,0)\na=array_int(2,0)","writable");
        bad("func f() -> int[] { return array_int(1,0) }","array");
        auto native_cpp=nova::compile("native func service(n: int) -> int\nfunc query() -> int { return service(2) }","native.nova");
        check(native_cpp.find("nova_native::fn_service(host_")!=std::string::npos && native_cpp.find("call_query(")!=std::string::npos,"typed native seam and C++ exports");
        bad("native func update(d: float)","lifecycle");
        bad("native func f(a: int,a: int)","duplicate");
        nova::Array<bool> bits(2,false);nova::at(bits,1)=true;check(nova::at(bits,1),"real bool lvalue");
        rejects([&]{nova::at(bits,-1);},"index");rejects([]{nova::Array<int64_t> huge(8193,0);},"8192");
        rejects([]{nova::native_int(INT64_MAX);},"range");
        auto root=std::filesystem::path("nova-import-tests");std::filesystem::create_directories(root);
        auto write=[&](const char *name,const char *text){std::ofstream(root/name)<<text;};
        write("shared.nova","var score: int=1\n");write("main.nova","import \"shared.nova\"\nimport \"shared.nova\"\nfunc score_value() -> int { return score }\n");
        std::vector<std::string> deps;nova::compile_file((root/"main.nova").string(),&deps);check(deps.size()==2,"duplicate imports loaded once");
        write("shared.nova","import \"main.nova\"\n");rejects([&]{nova::compile_file((root/"main.nova").string());},"cyclic import");
        write("shared.nova","import \"../escape.nova\"\n");rejects([&]{nova::compile_file((root/"main.nova").string());},"escapes source root");
        write("shared.nova","var score: int=unknown\n");rejects([&]{nova::compile_file((root/"main.nova").string());},"shared.nova:1");
        std::filesystem::remove_all(root);
        nova::Budget budget;
        budget.remaining = 0;
        rejects([&] { budget.step(); }, "budget");

        ember::Config config;
        config.backend = "cpu";
        config.worker_threads = 0;
        ember::game::Host host(config), foreign(config);
        auto entity = host.create("player");
        rejects([&] { host.create("player"); }, "duplicate entity");
        rejects([&] { host.entity("missing"); }, "Unknown entity");
        rejects([&] { foreign.position(entity); }, "foreign entity");
        rejects([&] { host.position({}); }, "Invalid");
        for (int i = 0; i < 200; ++i)
            host.create("box" + std::to_string(i));
        host.set_position(entity, {1, 2, 3});
        check(host.position(entity).x == 1, "handle survives vector growth");
        rejects([&] { host.set_position(entity, {INFINITY, 0, 0}); }, "Non-finite");
        int input_events = 0;
        auto input = host.on_input([&](const ember::game::InputEvent &event) {
            if (input_events == 0)
                check(event.type == ember::game::InputEvent::Type::MouseMove && event.dx == 3 && event.dy == -2,
                      "typed input payload");
            ++input_events;
        });
        host.dispatch_input({ember::game::InputEvent::Type::MouseMove, 0, 0, 0, 12, 8, 3, -2});
        check(input_events == 1 && host.mouse_position().x == 12 && host.mouse_delta().x == 3,
              "input delivery and mouse state");
        host.dispatch_input({ember::game::InputEvent::Type::KeyDown, 0, 65});
        host.dispatch_input({ember::game::InputEvent::Type::GamepadButtonDown, 0, 2, 7});
        host.dispatch_input({ember::game::InputEvent::Type::GamepadAxis, 0, 1, 7, -.5f});
        check(host.key_down(65) && host.gamepad_down(7, 2) && host.gamepad_axis(7, 1) == -.5f,
              "keyboard and gamepad state");
        rejects(
            [&] {
                host.dispatch_input(
                    {ember::game::InputEvent::Type::GamepadAxis, 0, 1, 2, INFINITY, 0, 0, 0});
            },
            "Non-finite input");
        host.destroy(entity);
        check(!host.alive(entity), "destroy invalidates entity");
        rejects([&] { host.position(entity); }, "Invalid");
        rejects([&] { host.destroy(entity); }, "Invalid");
        auto replacement = host.create("player");
        check(host.alive(replacement) && !host.alive(entity), "reused slot does not revive stale handle");
        host.set_position(replacement, {7, 0, 0});
        check(host.position(replacement).x == 7, "destroyed name can be reused");
        int updates = 0, signals = 0;
        auto c = host.on_update([&](float) { ++updates; });
        auto s = host.on_signal([&](int64_t code) {
            check(code == 42, "signal code");
            ++signals;
            if (signals == 1)
                host.emit(42);
        });
        host.emit(42);
        host.tick(1.0 / 60);
        check(updates == 1 && signals == 1, "first signal tick");
        host.tick(1.0 / 60);
        check(signals == 2, "signal deferral");
        c.disconnect();
        host.tick(1.0 / 60);
        check(updates == 2, "disconnection");
        ember::game::Connection later;
        int called = 0;
        auto first = host.on_update([&](float) { later.disconnect(); });
        later = host.on_update([&](float) { ++called; });
        host.tick(1.0 / 60);
        check(called == 0, "disconnect during dispatch");
        auto recursive = host.on_update([&](float) { host.tick(0); });
        rejects([&] { host.tick(1.0 / 60); }, "Recursive");
        recursive.disconnect();
        host.tick(1.0 / 60);
        ember::game::Host resilient(config);
        int after_failure = 0, signal_after_failure = 0;
        auto throws = resilient.on_update([](float) { throw std::runtime_error("listener failure"); });
        auto follows = resilient.on_update([&](float) { ++after_failure; });
        auto still_signals = resilient.on_signal([&](int64_t) { ++signal_after_failure; });
        resilient.emit(9);
        rejects([&] { resilient.tick(1.0 / 60); }, "listener failure");
        check(after_failure == 1 && signal_after_failure == 1,
              "listener failure does not skip peers or queued signals");
        ember::game::Connection survivor;
        {
            ember::game::Host temporary(config);
            survivor = temporary.on_update([](float) {});
        }
        survivor.disconnect();
        if (argc > 1) {
            auto a = host.load_asset(std::string(argv[1]) + "/assets/robot.embasset");
            rejects([&] { foreign.spawn(a, "bad"); }, "foreign asset");
            auto model = host.spawn(a, "robot");
            host.set_position(model, {3, 0, 0});
            check(host.position(model).x == 3, "asset handles");
        }
        config.fixed_hz = 0;
        rejects([&] { ember::Runtime runtime(config); }, "fixed_hz");
        config.fixed_hz = 241;
        rejects([&] { ember::Runtime too_fast(config); }, "fixed_hz");
        config.fixed_hz = 60;
        ember::Runtime tiny(config);
        tiny.tick(1e-20);
        tiny.tick(1.0 / 60.0);
        check(std::abs(tiny.time() - 1.0 / 60.0) < 1e-9, "tiny timestep does not poison accumulator");
        std::cout << "Nova lexer/parser/semantic/codegen and Host checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

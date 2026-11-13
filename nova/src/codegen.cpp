#include "nova/compiler.hpp"
#include "catalog.hpp"
#include <iomanip>
#include <map>
#include <sstream>
namespace nova {
namespace {
std::string quote(const std::string &s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            o << '\\' << char(c);
        else if (c >= 32 && c < 127)
            o << char(c);
        else
            o << '\\' << std::oct << std::setw(3) << std::setfill('0') << int(c) << std::dec;
    }
    return o.str() + '"';
}
std::string name(const std::string &s) {
    if (s == "transform")
        return "transform_";
    if (s == "self")
        return "self_";
    // The nv_ namespace keeps ordinary names readable. Reserved spellings
    // use a disjoint hex namespace, avoiding C++ reserved identifiers.
    if (!s.empty() && s.front() != '_' && s.find("__") == std::string::npos)
        return "nv_" + s;
    const char *hex = "0123456789abcdef";
    std::string n = "nh_";
    for (unsigned char c : s) {
        n += hex[c >> 4];
        n += hex[c & 15];
    }
    return n;
}
std::string arithmetic(std::string op, Type t, std::string a, std::string b) {
    const std::map<std::string, std::string> ops{
        {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "div"}, {"%", "mod"}};
    auto it = ops.find(op);
    if (it != ops.end() && t != Type::String) {
        std::string prefix = t == Type::Int ? "i" : (t == Type::Vec2 || t == Type::Vec3) ? "v" : "f";
        return "nova::" + prefix + it->second + "(" + a + ", " + b + ")";
    }
    return "(" + a + " " + op + " " + b + ")";
}
class Generator {
    const Module &m;
    std::ostringstream out;
    int unique = 0;
    std::string temporary(const char *prefix) { return prefix + std::to_string(unique++); }
    std::string expr(const Expr &e) {
        const auto &a = e.args;
        switch (e.kind) {
        case Expr::Kind::Literal:
            if (e.type == Type::String)
                return "std::string(" + quote(e.text) + ")";
            if (e.type == Type::Int) {
                if (e.text == "-9223372036854775808")
                    return "(-std::int64_t{9223372036854775807} - 1)";
                return "std::int64_t{" + std::to_string(std::stoll(e.text)) + "}";
            }
            if (e.type == Type::Float)
                return "double(" + e.text + ")";
            return e.text;
        case Expr::Kind::Name:
            return name(e.text);
        case Expr::Kind::Index: {
            const auto base = temporary("base_"), index = temporary("index_");
            const auto base_expr = expr(*a[0]);
            const auto index_expr = expr(*a[1]);
            return "([&]() -> decltype(auto) { auto &" + base + " = " + base_expr +
                   "; auto " + index + " = " + index_expr + "; return nova::at(" + base + ", " + index + "); }())";
        }
        case Expr::Kind::Member:
            return "(" + expr(*a[0]) + ")." + e.text;
        case Expr::Kind::Unary:
            if (e.text == "-" && e.type == Type::Int)
                return "nova::isub(0, " + expr(*a[0]) + ")";
            return "(" + e.text + expr(*a[0]) + ")";
        case Expr::Kind::Call: {
            auto fn = a[0]->text;
            const auto *builtin = detail::builtin(fn);
            std::string target = builtin ? builtin->cpp_target : name(fn);
            bool native = false;
            for (const auto &f : m.functions) if (f.name == fn && f.native) native = true;
            if (native) target = "nova_native::fn_" + fn;
            std::string code = "([&]() -> " + std::string(detail::cpp_type(e.type)) + " { ", args;
            for (size_t i = 1; i < a.size(); ++i) {
                auto n = temporary("arg_");
                code += (detail::array_element(a[i]->type) == Type::Unknown ? "auto " : "const auto &") + n + " = " + expr(*a[i]) + "; ";
                if (i > 1)
                    args += ", ";
                args += n;
            }
            if (native) args = "host_" + (args.empty() ? "" : ", " + args);
            return code + "return " + target + "(" + args + "); }())";
        }
        case Expr::Kind::Binary: {
            auto op = e.text;
            bool assignment = op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=";
            if (op == "&&" || op == "||") {
                const auto left = expr(*a[0]);
                const auto right = expr(*a[1]);
                return "(" + left + " " + op + " " + right + ")";
            }
            const auto lhs = temporary("lhs_"), rhs = temporary("rhs_"), old = temporary("old_");
            std::string code = "([&]() -> " + std::string(detail::cpp_type(e.type)) + " { auto ";
            code += (assignment ? "&" : "") + lhs + " = ";
            code += expr(*a[0]) + "; ";
            if (assignment && op != "=")
                code += "auto " + old + " = " + lhs + "; ";
            code += "auto " + rhs + " = " + expr(*a[1]) + "; return ";
            if (assignment)
                code += "nova::store(" + lhs + ", " +
                        (op == "=" ? rhs : arithmetic(op.substr(0, 1), e.type, old, rhs)) + ")";
            else
                code += arithmetic(op, e.type, lhs, rhs);
            return code + "; }())";
        }
        }
        throw Error(e.location, "invalid expression in generator");
    }
    std::string located(const Location &p, const std::string &body) {
        return "nova::located(" + quote(p.file) + ", " + std::to_string(p.line) + ", " +
               std::to_string(p.column) + ", [&]() { " + body + " })";
    }
    std::string value(const Expr &e) { return located(e.location, "return " + expr(e) + ";"); }
    void mapping(const Location &p) { out << "#line " << p.line << ' ' << quote(p.file) << '\n'; }
    void statements(const std::vector<Stmt> &body, bool global = false) {
        for (const auto &s : body) {
            mapping(s.location);
            switch (s.kind) {
            case Stmt::Kind::Var:
                if (global)
                    out << name(s.name) << " = " << value(*s.expr) << ";\n";
                else {
                    auto temp = "init_" + std::to_string(unique++);
                    out << "auto " << temp << " = " << value(*s.expr) << ";\n"
                        << "[[maybe_unused]] " << detail::cpp_type(s.type) << ' ' << name(s.name) << " = " << "std::move(" << temp << ");\n";
                }
                break;
            case Stmt::Kind::Expression:
                out << value(*s.expr) << ";\n";
                break;
            case Stmt::Kind::Return:
                out << "return" << (s.expr ? " " + value(*s.expr) : "") << ";\n";
                break;
            case Stmt::Kind::If:
                out << "if (" << value(*s.expr) << ") {\n";
                statements(s.body);
                out << "}\n";
                if (!s.alternate.empty()) {
                    out << "else {\n";
                    statements(s.alternate);
                    out << "}\n";
                }
                break;
            case Stmt::Kind::While:
                out << "while (" << located(s.location, "budget_.step(); return " + expr(*s.expr) + ";")
                    << ") {\n";
                statements(s.body);
                out << "}\n";
                break;
            case Stmt::Kind::Block:
                out << "{\n";
                statements(s.body);
                out << "}\n";
                break;
            }
        }
    }
    bool has(const std::string &n) const {
        for (auto &f : m.functions)
            if (f.name == n)
                return true;
        return false;
    }

  public:
    explicit Generator(const Module &mod) : m(mod) {}
    std::string run() {
        out << "// Generated by Nova MVP. Do not edit.\n#include <nova/support.hpp>\n"
               "#ifdef NOVA_NATIVE_HEADER\n#include NOVA_NATIVE_HEADER\n#endif\n"
               "#ifndef NOVA_PROGRAM_NAME\n#define NOVA_PROGRAM_NAME Program\n#define NOVA_UNDEF_PROGRAM_NAME\n#endif\n"
               "namespace nova_generated {\nclass NOVA_PROGRAM_NAME {\nember::game::Host "
               "host_;\nember::game::Entity self_;\nember::game::Transform transform_;\nember::SaveData save_;\nnova::Budget "
               "budget_;\nbool executing_ = false;\nember::game::Connection update_connection_, signal_connection_;\n";
        for (const auto &s : m.startup)
            if (s.kind == Stmt::Kind::Var)
                out << detail::cpp_type(s.type) << ' ' << name(s.name) << "{};\n";
        for (const auto &f : m.functions) {
            if (f.native) continue;
            mapping(f.location);
            out << detail::cpp_type(f.result) << ' ' << name(f.name) << '(';
            for (size_t i = 0; i < f.parameters.size(); ++i) {
                if (i)
                    out << ", ";
                auto t = f.parameters[i].type;
                out << "[[maybe_unused]] ";
                if (detail::array_element(t) != Type::Unknown) out << "const ";
                out << detail::cpp_type(t) << (detail::array_element(t) == Type::Unknown ? " " : " &") << name(f.parameters[i].name);
            }
            out << ") {\n";
            out << "nova::CallDepth depth_(budget_);\n";
            statements(f.body);
            out << "}\n";
        }
        out << "std::uint32_t mask_(std::int64_t n) { if(n<0 || n>UINT32_MAX) throw std::runtime_error(\"Invalid collision mask\"); return std::uint32_t(n); }\n"
               "bool mouse_down_(std::int64_t n) { return host_.mouse_down(nova::native_int(n)); }\n"
               "bool gamepad_down_(std::int64_t d, std::int64_t b) { return host_.gamepad_down(nova::native_int(d),nova::native_int(b)); }\n"
               "double gamepad_axis_(std::int64_t d, std::int64_t a) { return host_.gamepad_axis(nova::native_int(d),nova::native_int(a)); }\n"
               "bool key_down_(std::int64_t n) { return host_.key_down(nova::native_int(n)); }\n"
               "bool key_pressed_(std::int64_t n) { return host_.key_pressed(nova::native_int(n)); }\n"
               "bool key_released_(std::int64_t n) { return host_.key_released(nova::native_int(n)); }\n"
               "void light_(std::int64_t n, ember::Vec3 p, ember::Vec3 c, double r) { host_.set_light(nova::native_int(n),p,c,nova::component(r)); }\n"
               "void collider_box_(ember::game::Entity e, ember::Vec3 h, std::int64_t m) { host_.add_box_collider(e,h,mask_(m)); }\n"
               "ember::game::Sweep sweep_(ember::Vec3 p, ember::Vec3 d, double r, std::int64_t m) { return host_.sweep(p,d,nova::component(r),mask_(m)); }\n"
               "ember::game::Slide slide_(ember::Vec3 p, ember::Vec3 d, double r, std::int64_t m) { return host_.slide(p,d,nova::component(r),mask_(m)); }\n";
        out << "#line 1 \"<nova-host>\"\nvoid destroy_(ember::game::Entity e) { host_.destroy(e); }\n"
               "void physics_box_(ember::game::Entity e, ember::Vec3 half, bool dynamic) { "
               "ember::BodyDesc d; d.shape = ember::BodyShape::box(half); d.motion = dynamic ? "
               "ember::BodyMotion::Dynamic : ember::BodyMotion::Static; d.allow_rotation = false; "
               "host_.add_rigid_body(e, d); }\n"
               "void physics_sphere_(ember::game::Entity e, double radius, bool dynamic) { "
               "ember::BodyDesc d; d.shape = ember::BodyShape::sphere(nova::component(radius)); d.motion = dynamic ? "
               "ember::BodyMotion::Dynamic : ember::BodyMotion::Static; d.allow_rotation = false; "
               "host_.add_rigid_body(e, d); }\n"
               "void load_save_(const std::string& path) { save_ = ember::SaveData::read(path); }\n"
               "ember::Vec3 get_position_(ember::game::Entity e) { return e == "
               "self_ ? transform_.position : host_.position(e); }\nvoid set_position_(ember::game::Entity "
               "e, ember::Vec3 p) { if(e == self_) transform_.position = p; else host_.set_position(e,p); "
               "}\npublic:\nNOVA_PROGRAM_NAME(const NOVA_PROGRAM_NAME&) = delete;\nNOVA_PROGRAM_NAME& operator=(const NOVA_PROGRAM_NAME&) = "
               "delete;\nNOVA_PROGRAM_NAME(ember::game::Host host, ember::game::Entity self) : host_(host), "
               "self_(self), transform_(host.transform(self)) {\n";
        statements(m.startup, true);
        if (has("start"))
            out << name("start") << "();\n";
        out << "if (host_.alive(self_)) host_.set_transform(self_,transform_);\n";
        if (has("update"))
            out << "if (host_.alive(self_)) update_connection_ = host_.on_update([this](float dt) { "
                   "if (!host_.alive(self_)) { update_connection_.disconnect(); return; } nova::EntryGuard entry_(executing_); budget_.reset(); "
                   "transform_ = host_.transform(self_); "
                << name("update")
                << "(dt); if (host_.alive(self_)) host_.set_transform(self_,transform_); else "
                   "update_connection_.disconnect(); });\n";
        if (has("on_signal"))
            out << "if (host_.alive(self_)) signal_connection_ = host_.on_signal([this](std::int64_t code) { "
                   "if (!host_.alive(self_)) { signal_connection_.disconnect(); return; } nova::EntryGuard entry_(executing_); budget_.reset(); "
                   "transform_ = host_.transform(self_); "
                << name("on_signal")
                << "(code); if (host_.alive(self_)) host_.set_transform(self_,transform_); else "
                   "signal_connection_.disconnect(); });\n";
        out << "}\n";
        for (const auto &f : m.functions) {
            if (f.native || detail::lifecycle(f.name)) continue;
            mapping(f.location);
            out << detail::cpp_type(f.result) << " call_" << f.name << "(";
            std::string args;
            for (size_t i=0; i<f.parameters.size(); ++i) {
                const auto &p=f.parameters[i];
                if(i) { out << ", "; args += ", "; }
                if(detail::array_element(p.type)!=Type::Unknown) out << "const ";
                out << detail::cpp_type(p.type) << (detail::array_element(p.type)==Type::Unknown ? " " : " &") << name(p.name);
                args += name(p.name);
            }
            out << ") { if(!host_.alive(self_)) throw std::runtime_error(\"Expired Nova self\"); "
                   "nova::EntryGuard entry_(executing_); budget_.reset(); transform_=host_.transform(self_); ";
            if(f.result!=Type::Void) out << "auto result_ = ";
            out << located(f.location, "return " + name(f.name) + "(" + args + ");") << "; "
                   "if(host_.alive(self_)) host_.set_transform(self_,transform_); ";
            if(f.result!=Type::Void) out << "return result_; ";
            out << "}\n";
        }
        out << "};\n}\n#ifdef NOVA_UNDEF_PROGRAM_NAME\n#undef NOVA_UNDEF_PROGRAM_NAME\n#undef NOVA_PROGRAM_NAME\n#endif\n";
        return out.str();
    }
};
} // namespace
std::string generate(const Module &m) {
    return Generator(m).run();
}
} // namespace nova

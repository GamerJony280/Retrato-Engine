#include "nova/compiler.hpp"
#include "catalog.hpp"
#include <cmath>
#include <map>
namespace nova {
namespace {
bool numeric(Type t) {
    return t == Type::Int || t == Type::Float;
}
bool compatible(Type dst, Type src) {
    return dst == src || (dst == Type::Float && src == Type::Int);
}
struct Symbol {
    Type type;
    bool writable;
};
class Analyzer {
    Module &module;
    std::vector<std::map<std::string, Symbol>> scopes{1};
    std::map<std::string, Function *> functions;
    Type result = Type::Void;
    bool in_function = false;
    void declare(const Location &p, const std::string &name, Type t, bool writable = true) {
        if (name == "transform" || name == "self" || (detail::builtin(name) && name != "delta") ||
            functions.count(name))
            throw Error(p, "reserved name '" + name + "'");
        if (!scopes.back().emplace(name, Symbol{t, writable}).second)
            throw Error(p, "duplicate variable '" + name + "'");
    }
    void require(const Expr &e, Type t) {
        if (!compatible(t, e.type))
            throw Error(e.location, "expected " + type_name(t) + ", got " + type_name(e.type));
    }
    void arguments(Expr &e, const std::vector<Type> &types) {
        if (e.args.size() != types.size() + 1)
            throw Error(e.location, "wrong function argument count");
        for (size_t i = 0; i < types.size(); ++i)
            require(*e.args[i + 1], types[i]);
    }
    Type binary(const std::string &op, Type a, Type b, Location loc) {
        if (op == "&&" || op == "||") {
            if (a == Type::Bool && b == Type::Bool)
                return Type::Bool;
        } else if (op == "==" || op == "!=") {
            if ((numeric(a) && numeric(b)) || (a == b && (a == Type::Bool || a == Type::String || a == Type::Entity)))
                return Type::Bool;
        } else if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            if (numeric(a) && numeric(b))
                return Type::Bool;
        } else if (op == "%") {
            if (a == Type::Int && b == Type::Int)
                return Type::Int;
        } else if (op == "+" && a == Type::String && b == Type::String)
            return Type::String;
        else if (numeric(a) && numeric(b))
            return a == Type::Float || b == Type::Float ? Type::Float : Type::Int;
        else if ((a == Type::Vec2 || a == Type::Vec3) && a == b && (op == "+" || op == "-"))
            return a;
        else if ((a == Type::Vec2 || a == Type::Vec3) && numeric(b) && (op == "*" || op == "/"))
            return a;
        throw Error(loc, "invalid operator '" + op + "' for " + type_name(a) + " and " + type_name(b));
    }
    Type expression(Expr &e, int depth = 0) {
        if (depth > 128)
            throw Error(e.location, "expression nesting exceeds 128");
        auto &a = e.args;
        switch (e.kind) {
        case Expr::Kind::Literal:
            try {
                if (e.type == Type::Int) {
                    size_t n;
                    std::stoll(e.text, &n);
                    if (n != e.text.size())
                        throw std::invalid_argument("int");
                }
                if (e.type == Type::Float && !std::isfinite(std::stod(e.text)))
                    throw std::out_of_range("float");
            } catch (const std::exception &) {
                throw Error(e.location, "numeric literal out of range");
            }
            return e.type;
        case Expr::Kind::Name:
            for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                auto s = it->find(e.text);
                if (s != it->end()) {
                    e.type = s->second.type;
                    e.writable = s->second.writable;
                    return e.type;
                }
            }
            throw Error(e.location, "unknown variable '" + e.text + "'");
        case Expr::Kind::Index: {
            auto base = expression(*a[0], depth + 1);
            expression(*a[1], depth + 1); require(*a[1], Type::Int);
            auto element = detail::array_element(base);
            if (element == Type::Unknown) throw Error(e.location, "index requires an array");
            if (!a[0]->writable && a[0]->kind != Expr::Kind::Name)
                throw Error(e.location, "index a stored array, not a temporary");
            e.writable = a[0]->writable; return e.type = element;
        }
        case Expr::Kind::Member: {
            Type base = expression(*a[0], depth + 1);
            if ((base == Type::Slide || base == Type::Sweep) && (e.text == "position" || e.text == "normal" ||
                (base == Type::Slide && e.text == "remaining"))) e.type = Type::Vec3;
            else if (base == Type::Slide && e.text == "contacts") e.type = Type::Int;
            else if (base == Type::Sweep && e.text == "hit") e.type = Type::Bool;
            else if (base == Type::Sweep && e.text == "fraction") e.type = Type::Float;
            else if (base == Type::Sweep && e.text == "entity") e.type = Type::Entity;
            else if (base == Type::Transform && e.text == "position")
                e.type = Type::Vec3;
            else if (base == Type::Transform && e.text == "yaw")
                e.type = Type::Float;
            else if ((base == Type::Vec2 || base == Type::Vec3) &&
                     (e.text == "x" || e.text == "y" || (base == Type::Vec3 && e.text == "z")))
                e.type = Type::Float;
            else
                throw Error(e.location, "unknown property '" + e.text + "' on " + type_name(base));
            e.writable = a[0]->writable;
            return e.type;
        }
        case Expr::Kind::Call: {
            if (a[0]->kind != Expr::Kind::Name)
                throw Error(e.location, "method/indirect calls are not in Nova MVP; use runtime functions");
            auto name = a[0]->text;
            for (size_t i = 1; i < a.size(); ++i)
                expression(*a[i], depth + 1);
            const auto *builtin = detail::builtin(name);
            if (builtin && builtin->kind == detail::BuiltinKind::Length) {
                if (a.size() != 2 || detail::array_element(a[1]->type) == Type::Unknown)
                    throw Error(e.location, "len expects one array");
                return e.type = Type::Int;
            }
            if (builtin && builtin->kind == detail::BuiltinKind::Text) {
                if (a.size() != 2 || (a[1]->type != Type::String && a[1]->type != Type::Bool && !numeric(a[1]->type)))
                    throw Error(e.location, "str expects one scalar");
                return e.type = Type::String;
            }
            if (builtin && builtin->kind == detail::BuiltinKind::Print) {
                if (a.size() != 2 ||
                    (a[1]->type != Type::String && a[1]->type != Type::Bool && !numeric(a[1]->type) &&
                     a[1]->type != Type::Vec2 && a[1]->type != Type::Vec3))
                    throw Error(e.location, "print expects one scalar or vector");
                return e.type = Type::Void;
            }
            if (builtin) {
                std::vector<Type> parameters(builtin->parameters.begin(),
                                             builtin->parameters.begin() + builtin->arity);
                arguments(e, parameters);
                return e.type = builtin->result;
            }
            auto fn = functions.find(name);
            if (fn == functions.end())
                throw Error(e.location, "unknown function '" + name + "'");
            if (!in_function)
                throw Error(e.location, "call user functions from start(), not global initialization");
            std::vector<Type> types;
            for (auto &p : fn->second->parameters)
                types.push_back(p.type);
            arguments(e, types);
            return e.type = fn->second->result;
        }
        case Expr::Kind::Unary: {
            if (e.text == "-" && a[0]->kind == Expr::Kind::Literal && a[0]->type == Type::Int &&
                a[0]->text == "9223372036854775808") {
                e.kind = Expr::Kind::Literal;
                e.text = "-9223372036854775808";
                e.type = Type::Int;
                a.clear();
                return e.type;
            }
            auto t = expression(*a[0], depth + 1);
            if (e.text == "!" && t == Type::Bool)
                return e.type = Type::Bool;
            if (e.text != "!" && numeric(t))
                return e.type = t;
            throw Error(e.location, "invalid unary operator");
        }
        case Expr::Kind::Binary: {
            auto l = expression(*a[0], depth + 1), r = expression(*a[1], depth + 1);
            if (e.text == "=" || e.text == "+=" || e.text == "-=" || e.text == "*=" || e.text == "/=") {
                if (!a[0]->writable || l == Type::Transform || detail::array_element(l) != Type::Unknown)
                    throw Error(e.location, "assignment requires a writable variable or property");
                auto t = e.text == "=" ? r : binary(e.text.substr(0, 1), l, r, e.location);
                if (!compatible(l, t))
                    throw Error(e.location, "cannot assign " + type_name(t) + " to " + type_name(l));
                return e.type = l;
            }
            return e.type = binary(e.text, l, r, e.location);
        }
        }
        throw Error(e.location, "invalid AST");
    }
    bool block(std::vector<Stmt> &stmts, bool scope = true) {
        if (scope)
            scopes.emplace_back();
        bool returns = false;
        for (auto &s : stmts) {
            if (s.expr)
                expression(*s.expr);
            switch (s.kind) {
            case Stmt::Kind::Var:
                require(*s.expr, s.type);
                declare(s.location, s.name, s.type);
                break;
            case Stmt::Kind::Return:
                if (!in_function)
                    throw Error(s.location, "return outside function");
                if (result == Type::Void && s.expr)
                    throw Error(s.location, "void function cannot return a value");
                if (result != Type::Void && !s.expr)
                    throw Error(s.location, "return value required");
                if (s.expr)
                    require(*s.expr, result);
                returns = true;
                break;
            case Stmt::Kind::If: {
                require(*s.expr, Type::Bool);
                bool a = block(s.body), b = block(s.alternate);
                returns = returns || (a && b);
                break;
            }
            case Stmt::Kind::While:
                require(*s.expr, Type::Bool);
                block(s.body);
                break;
            case Stmt::Kind::Block:
                returns = block(s.body) || returns;
                break;
            case Stmt::Kind::Expression:
                break;
            }
        }
        if (scope)
            scopes.pop_back();
        return returns;
    }

  public:
    explicit Analyzer(Module &m) : module(m) {}
    void run() {
        scopes[0]["transform"] = {Type::Transform, true};
        scopes[0]["self"] = {Type::Entity, false};
        for (auto &f : module.functions) {
            if (f.name == "transform" || f.name == "self" || detail::builtin(f.name) ||
                !functions.emplace(f.name, &f).second)
                throw Error(f.location, "duplicate or reserved function '" + f.name + "'");
            if (const auto *lifecycle = detail::lifecycle(f.name)) {
                if (f.native) throw Error(f.location, "lifecycle functions cannot be native declarations");
                if (f.result != Type::Void || f.parameters.size() != lifecycle->arity ||
                    (lifecycle->arity && f.parameters[0].type != lifecycle->parameter))
                    throw Error(f.location, "invalid lifecycle signature for '" + f.name + "'");
            }
        }
        block(module.startup, false);
        in_function = true;
        for (auto &f : module.functions) {
            if (f.native) {
                if (detail::array_element(f.result) != Type::Unknown) throw Error(f.location, "native array returns are unsupported");
                std::map<std::string,bool> seen;
                for (auto &p : f.parameters) {
                    if (!seen.emplace(p.name,true).second) throw Error(p.location, "duplicate native parameter");
                    if (detail::array_element(p.type) != Type::Unknown) throw Error(p.location,"native array arguments are unsupported");
                }
                continue;
            }
            if (detail::array_element(f.result) != Type::Unknown)
                throw Error(f.location, "array returns are not supported; initialize stored arrays");
            result = f.result;
            scopes.emplace_back();
            for (auto &p : f.parameters)
                declare(p.location, p.name, p.type, detail::array_element(p.type) == Type::Unknown);
            bool returns = block(f.body, false);
            if (result != Type::Void && !returns)
                throw Error(f.location, "not all paths return a value");
            scopes.pop_back();
        }
    }
};
} // namespace
void analyze(Module &m) {
    Analyzer(m).run();
}
std::string compile(const std::string &source, const std::string &file) {
    auto m = parse(lex(source, file));
    if (!m.imports.empty()) throw Error(m.imports[0].first,"use compile_file to resolve imports");
    analyze(m);
    return generate(m);
}
} // namespace nova

#include "nova/compiler.hpp"
#include "catalog.hpp"
namespace nova {
namespace {
class Parser {
    const std::vector<Token> &tokens;
    size_t index = 0;
    int depth = 0;
    struct Depth {
        Parser &p;
        explicit Depth(Parser &parser) : p(parser) {
            if (++p.depth > 128)
                throw Error(p.current().location, "nesting exceeds 128");
        }
        ~Depth() { --p.depth; }
    };
    const Token &current() const { return tokens.at(index); }
    bool is(const std::string &s) const { return current().text == s && current().kind != TokenKind::String; }
    Token take() { return tokens.at(index++); }
    bool accept(const std::string &s) {
        if (is(s)) {
            take();
            return true;
        }
        return false;
    }
    Token expect(const std::string &s) {
        if (!is(s))
            throw Error(current().location, "expected '" + s + "'");
        return take();
    }
    Token identifier() {
        if (current().kind != TokenKind::Identifier)
            throw Error(current().location, "expected identifier");
        return take();
    }
    void lines() {
        while (current().kind == TokenKind::Newline)
            take();
    }
    void separators() {
        while (current().kind == TokenKind::Newline || is(";"))
            take();
    }
    void end_statement() {
        if (current().kind == TokenKind::Newline || is(";"))
            separators();
        else if (!is("}") && current().kind != TokenKind::End)
            throw Error(current().location, "expected newline or ';'");
    }
    Type type() {
        auto t = identifier();
        auto result = detail::source_type(t.text);
        if (result == Type::Unknown)
            throw Error(t.location, "unknown type '" + t.text + "'");
        if (accept("[")) {
            expect("]"); result = detail::array_type(result);
            if (result == Type::Unknown) throw Error(t.location, "this type cannot be an array element");
        }
        return result;
    }
    int precedence() const {
        if (current().kind != TokenKind::Symbol)
            return 0;
        const auto &s = current().text;
        if (s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=")
            return 1;
        if (s == "||")
            return 2;
        if (s == "&&")
            return 3;
        if (s == "==" || s == "!=")
            return 4;
        if (s == "<" || s == ">" || s == "<=" || s == ">=")
            return 5;
        if (s == "+" || s == "-")
            return 6;
        if (s == "*" || s == "/" || s == "%")
            return 7;
        return 0;
    }
    void height(Expr &e) {
        e.height = 1;
        for (const auto &a : e.args)
            if (a->height + 1 > e.height)
                e.height = a->height + 1;
        if (e.height > 128)
            throw Error(e.location, "expression nesting exceeds 128");
    }
    std::unique_ptr<Expr> expression(int minimum = 1) {
        Depth guard(*this);
        auto token = take();
        auto e = std::make_unique<Expr>();
        e->location = token.location;
        e->text = token.text;
        if (token.kind == TokenKind::Integer || token.kind == TokenKind::Float ||
            token.kind == TokenKind::String || token.text == "true" || token.text == "false") {
            e->kind = Expr::Kind::Literal;
            e->type = token.kind == TokenKind::Integer  ? Type::Int
                      : token.kind == TokenKind::Float  ? Type::Float
                      : token.kind == TokenKind::String ? Type::String
                                                        : Type::Bool;
        } else if (token.kind == TokenKind::Identifier)
            e->kind = Expr::Kind::Name;
        else if (token.text == "!" || token.text == "-" || token.text == "+") {
            e->kind = Expr::Kind::Unary;
            e->args.push_back(expression(8));
        } else if (token.text == "(") {
            lines();
            e = expression();
            lines();
            expect(")");
        } else
            throw Error(token.location, "expected expression");
        height(*e);
        int chain = 0;
        while (true) {
            if (++chain > 128)
                throw Error(e->location, "expression chain exceeds 128");
            if (accept("[")) {
                auto n = std::make_unique<Expr>();
                n->kind = Expr::Kind::Index; n->location = e->location;
                n->args.push_back(std::move(e)); lines(); n->args.push_back(expression()); lines(); expect("]");
                height(*n); e = std::move(n); continue;
            }
            if (is(".")) {
                take();
                auto member = identifier();
                auto n = std::make_unique<Expr>();
                n->kind = Expr::Kind::Member;
                n->location = member.location;
                n->text = member.text;
                n->args.push_back(std::move(e));
                height(*n);
                e = std::move(n);
                continue;
            }
            if (is("(")) {
                take();
                auto n = std::make_unique<Expr>();
                n->kind = Expr::Kind::Call;
                n->location = e->location;
                n->args.push_back(std::move(e));
                lines();
                if (!is(")"))
                    do {
                        lines();
                        n->args.push_back(expression());
                        lines();
                    } while (accept(","));
                expect(")");
                height(*n);
                e = std::move(n);
                continue;
            }
            int p = precedence();
            if (p < minimum)
                break;
            auto op = take();
            lines();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Kind::Binary;
            n->location = op.location;
            n->text = op.text;
            n->args.push_back(std::move(e));
            n->args.push_back(expression(p == 1 ? p : p + 1));
            height(*n);
            e = std::move(n);
        }
        return e;
    }
    std::vector<Stmt> block() {
        Depth guard(*this);
        lines();
        expect("{");
        separators();
        std::vector<Stmt> b;
        while (!is("}")) {
            if (current().kind == TokenKind::End)
                throw Error(current().location, "expected '}'");
            b.push_back(statement());
            separators();
        }
        expect("}");
        return b;
    }
    Stmt statement() {
        Depth guard(*this);
        Stmt s;
        s.location = current().location;
        if (accept("var")) {
            s.kind = Stmt::Kind::Var;
            s.name = identifier().text;
            expect(":");
            s.type = type();
            expect("=");
            s.expr = expression();
            end_statement();
        } else if (accept("return")) {
            s.kind = Stmt::Kind::Return;
            if (current().kind != TokenKind::Newline && current().kind != TokenKind::End && !is(";") &&
                !is("}"))
                s.expr = expression();
            end_statement();
        } else if (accept("if")) {
            s.kind = Stmt::Kind::If;
            s.expr = expression();
            s.body = block();
            lines();
            if (accept("else")) {
                lines();
                if (is("if"))
                    s.alternate.push_back(statement());
                else
                    s.alternate = block();
            }
        } else if (accept("while")) {
            s.kind = Stmt::Kind::While;
            s.expr = expression();
            s.body = block();
        } else if (is("{")) {
            s.kind = Stmt::Kind::Block;
            s.body = block();
        } else {
            s.kind = Stmt::Kind::Expression;
            s.expr = expression();
            end_statement();
        }
        return s;
    }

  public:
    explicit Parser(const std::vector<Token> &t) : tokens(t) {}
    Module run() {
        Module m;
        m.file = current().location.file;
        separators();
        while (current().kind != TokenKind::End) {
            if (accept("import")) {
                if(current().kind != TokenKind::String) throw Error(current().location,"import requires a string path");
                auto t=take(); m.imports.push_back({t.location,t.text}); end_statement();
            } else if (is("func") || is("native")) {
                Function f;
                f.location = current().location;
                f.native = accept("native");
                expect("func");
                f.name = identifier().text;
                expect("(");
                lines();
                if (!is(")"))
                    do {
                        lines();
                        auto p = identifier();
                        expect(":");
                        auto t = type();
                        f.parameters.push_back({p.location, p.text, t});
                        lines();
                    } while (accept(","));
                expect(")");
                if (accept("->"))
                    f.result = type();
                if (f.native) end_statement(); else f.body = block();
                m.functions.push_back(std::move(f));
            } else
                m.startup.push_back(statement());
            separators();
        }
        return m;
    }
};
} // namespace
Module parse(const std::vector<Token> &t) {
    if (t.empty() || t.back().kind != TokenKind::End)
        throw Error({"<input>", 1, 1}, "missing token terminator");
    return Parser(t).run();
}
} // namespace nova

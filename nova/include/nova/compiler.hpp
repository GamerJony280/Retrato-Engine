#pragma once
#include <memory>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

namespace nova {
struct Location {
    std::string file;
    int line = 1, column = 1;
};
class Error : public std::runtime_error {
  public:
    Location location;
    Error(Location, const std::string &);
};
enum class TokenKind { Identifier, Integer, Float, String, Keyword, Symbol, Newline, End };
struct Token {
    TokenKind kind;
    std::string text;
    Location location;
};
enum class Type { Unknown, Void, Int, Float, Bool, String, Vec2, Vec3, Entity, Asset, Transform, Slide, Sweep, IntArray, FloatArray, BoolArray, StringArray, Vec2Array, Vec3Array, EntityArray, AssetArray };
std::string type_name(Type);
struct Expr {
    enum class Kind { Literal, Name, Unary, Binary, Call, Member, Index } kind;
    Location location;
    std::string text;
    Type type = Type::Unknown;
    bool writable = false;
    int height = 1;
    std::vector<std::unique_ptr<Expr>> args;
};
struct Stmt {
    enum class Kind { Var, Expression, Return, If, While, Block } kind;
    Location location;
    std::string name;
    Type type = Type::Unknown;
    std::unique_ptr<Expr> expr;
    std::vector<Stmt> body, alternate;
};
struct Parameter {
    Location location;
    std::string name;
    Type type;
};
struct Function {
    Location location;
    std::string name;
    std::vector<Parameter> parameters;
    Type result = Type::Void;
    bool native = false;
    std::vector<Stmt> body;
};
struct Module {
    std::string file;
    std::vector<std::pair<Location, std::string>> imports;
    std::vector<Stmt> startup;
    std::vector<Function> functions;
};
std::vector<Token> lex(const std::string &source, const std::string &file);
Module parse(const std::vector<Token> &);
void analyze(Module &);
std::string generate(const Module &); // requires successful analyze
std::string compile(const std::string &source, const std::string &file);
Module load_module(const std::string &file, std::vector<std::string> *dependencies = nullptr);
std::string compile_file(const std::string &file, std::vector<std::string> *dependencies = nullptr);
} // namespace nova

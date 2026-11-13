#include "nova/compiler.hpp"
#include <cctype>
#include <set>
namespace nova {
Error::Error(Location p, const std::string &message)
    : std::runtime_error(p.file + ":" + std::to_string(p.line) + ":" + std::to_string(p.column) +
                         ": error: " + message),
      location(std::move(p)) {}
std::vector<Token> lex(const std::string &source, const std::string &file) {
    if (source.size() > 1024 * 1024)
        throw Error({file, 1, 1}, "source exceeds 1 MiB limit");
    std::vector<Token> tokens;
    size_t i = 0;
    Location pos{file, 1, 1};
    auto peek = [&](size_t n = 0) { return i + n < source.size() ? source[i + n] : '\0'; };
    auto take = [&]() {
        char c = source[i++];
        if (c == '\n') {
            ++pos.line;
            pos.column = 1;
        } else
            ++pos.column;
        return c;
    };
    const std::set<std::string> keywords{"var", "func", "native", "import", "return", "if", "else", "while", "true", "false"};
    while (i < source.size()) {
        char c = peek();
        Location start = pos;
        if (c == ' ' || c == '\t' || c == '\r') {
            take();
            continue;
        }
        if (c == '\n') {
            take();
            tokens.push_back({TokenKind::Newline, "\n", start});
            continue;
        }
        if (c == '/' && peek(1) == '/') {
            while (i < source.size() && peek() != '\n')
                take();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            take();
            take();
            while (i < source.size() && !(peek() == '*' && peek(1) == '/')) {
                if (peek() == '\n')
                    tokens.push_back({TokenKind::Newline, "\n", pos});
                take();
            }
            if (i == source.size())
                throw Error(start, "unterminated comment");
            take();
            take();
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string s;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
                s += take();
            tokens.push_back({keywords.count(s) ? TokenKind::Keyword : TokenKind::Identifier, s, start});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string s;
            bool floating = false;
            while (std::isdigit(static_cast<unsigned char>(peek())))
                s += take();
            if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
                floating = true;
                s += take();
                while (std::isdigit(static_cast<unsigned char>(peek())))
                    s += take();
            }
            if (peek() == 'e' || peek() == 'E') {
                floating = true;
                s += take();
                if (peek() == '+' || peek() == '-')
                    s += take();
                if (!std::isdigit(static_cast<unsigned char>(peek())))
                    throw Error(start, "invalid exponent");
                while (std::isdigit(static_cast<unsigned char>(peek())))
                    s += take();
            }
            tokens.push_back({floating ? TokenKind::Float : TokenKind::Integer, s, start});
            continue;
        }
        if (c == '"') {
            take();
            std::string s;
            while (i < source.size() && peek() != '"') {
                if (peek() == '\n' || peek() == '\r' || peek() == '\0')
                    throw Error(start, "unterminated string");
                char v = take();
                if (v == '\\') {
                    if (i == source.size())
                        throw Error(start, "unterminated string escape");
                    v = take();
                    switch (v) {
                    case 'n':
                        v = '\n';
                        break;
                    case 't':
                        v = '\t';
                        break;
                    case 'r':
                        v = '\r';
                        break;
                    case '"':
                    case '\\':
                        break;
                    default:
                        throw Error(start, "unknown string escape");
                    }
                }
                s += v;
            }
            if (i == source.size())
                throw Error(start, "unterminated string");
            take();
            tokens.push_back({TokenKind::String, s, start});
            continue;
        }
        std::string s(1, take());
        std::string pair = s + peek();
        if (pair == "->" || pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" ||
            pair == "||" || pair == "+=" || pair == "-=" || pair == "*=" || pair == "/=")
            s += take();
        else if (std::string("+-*/%!=<>(){}[]:;.,").find(c) == std::string::npos)
            throw Error(start, "unexpected character");
        tokens.push_back({TokenKind::Symbol, s, start});
    }
    tokens.push_back({TokenKind::End, "", pos});
    return tokens;
}
} // namespace nova

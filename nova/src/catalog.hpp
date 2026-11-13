#pragma once
#include "nova/compiler.hpp"
#include <array>
#include <cstddef>
#include <string>

namespace nova::detail {
enum class BuiltinKind { Regular, Print, Length, Text };
struct BuiltinSpec {
    const char *name;
    Type result;
    std::array<Type, 6> parameters;
    size_t arity;
    const char *cpp_target;
    BuiltinKind kind = BuiltinKind::Regular;
};
struct LifecycleSpec {
    const char *name;
    Type parameter;
    size_t arity;
};
const BuiltinSpec *builtin(const std::string &);
const LifecycleSpec *lifecycle(const std::string &);
Type source_type(const std::string &);
const char *cpp_type(Type);
Type array_type(Type);
Type array_element(Type);
} // namespace nova::detail

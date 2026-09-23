#pragma once
#include <cstddef>
#include <string>

namespace calculator {
constexpr std::size_t kMaxExpression = 192;
struct EvalResult {
    bool ok = false;
    double value = 0.0;
    std::string error_message;
};
std::string format_value(double value);
EvalResult evaluate_expression(const std::string &expression);
}

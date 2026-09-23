// Adapted from CardputerZero/Calculator/main/src/calculator_engine.cpp.
#include "engine.hpp"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace calculator {
namespace {
class Parser {
public:
    explicit Parser(const std::string &text) : text_(text) {}
    bool parse(double &value) {
        return expression(value) && position_ == text_.size();
    }
    std::string error = "syntax";
private:
    bool finite(double value) {
        if (std::isfinite(value)) return true;
        error = "range";
        return false;
    }
    bool expression(double &value) {
        if (!term(value)) return false;
        while (position_ < text_.size()) {
            const char op = text_[position_];
            if (op != '+' && op != '-') break;
            ++position_;
            double right = 0;
            if (!term(right)) return false;
            value = op == '+' ? value + right : value - right;
            if (!finite(value)) return false;
        }
        return true;
    }
    bool term(double &value) {
        if (!factor(value)) return false;
        while (position_ < text_.size()) {
            const char op = text_[position_];
            if (op != '*' && op != '/') break;
            ++position_;
            double right = 0;
            if (!factor(right)) return false;
            if (op == '/' && right == 0) {
                error = "division_by_zero";
                return false;
            }
            value = op == '*' ? value * right : value / right;
            if (!finite(value)) return false;
        }
        return true;
    }
    bool factor(double &value) {
        // Guard recursive parentheses, unary minus and powers on the small device.
        if (++depth_ > 64) { error = "too_long"; --depth_; return false; }
        const bool ok = factor_body(value);
        --depth_;
        return ok;
    }
    bool factor_body(double &value) {
        if (position_ < text_.size() && text_[position_] == '-') {
            ++position_;
            if (!factor(value)) return false;
            value = -value;
            return true;
        }
        if (!primary(value)) return false;
        if (position_ < text_.size() && text_[position_] == '^') {
            ++position_;
            double exponent = 0;
            if (!factor(exponent)) return false;
            value = std::pow(value, exponent);
            if (!finite(value)) return false;
        }
        return true;
    }
    bool primary(double &value) {
        if (position_ < text_.size() && text_[position_] == '(') {
            ++position_;
            if (!expression(value)) return false;
            if (position_ >= text_.size() || text_[position_] != ')') return false;
            ++position_;
            return true;
        }
        const auto start = position_;
        bool digit = false, dot = false;
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            if (ch >= '0' && ch <= '9') digit = true;
            else if (ch == '.' && !dot) dot = true;
            else break;
            ++position_;
        }
        if (!digit) return false;
        // Result formatting may produce scientific notation; continued arithmetic
        // must accept it instead of silently changing tiny or large values.
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            const auto exponent_start = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            if (exponent_start == position_) return false;
        }
        std::istringstream stream(text_.substr(start, position_ - start));
        stream.imbue(std::locale::classic());
        stream >> value;
        if (stream.fail()) { error = "range"; return false; }
        return stream.eof() && finite(value);
    }
    const std::string &text_;
    std::size_t position_ = 0;
    unsigned depth_ = 0;
};
}

std::string format_value(double value) {
    if (!std::isfinite(value)) return "error";
    if (value == 0) return "0";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(12) << value;
    return stream.str();
}

EvalResult evaluate_expression(const std::string &expression) {
    EvalResult result;
    if (expression.empty()) { result.error_message = "empty"; return result; }
    if (expression.size() > kMaxExpression) { result.error_message = "too_long"; return result; }
    Parser parser(expression);
    if (!parser.parse(result.value)) { result.error_message = parser.error; return result; }
    result.ok = true;
    return result;
}
}

#include "model.hpp"

namespace calculator {
namespace {
bool digit(char ch) { return ch >= '0' && ch <= '9'; }
bool op(char ch) { return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '^'; }
bool unary(const std::string &s, std::size_t pos) {
    return s[pos] == '-' && (pos == 0 || op(s[pos-1]) || s[pos-1] == '(');
}
int balance(const std::string &s) {
    int result = 0;
    for (char ch : s) { if (ch == '(') ++result; else if (ch == ')') --result; }
    return result;
}
}

void Model::changed() { error_.clear(); evaluated_ = false; }
void Model::clear() { expression_.clear(); history_.clear(); changed(); }
void Model::input(char ch) {
    if (!digit(ch) && !op(ch) && ch != '.' && ch != '(' && ch != ')') return;
    // A digit after a result/error starts fresh; an operator continues a result.
    if ((!error_.empty() || evaluated_) && (digit(ch) || ch == '.' || ch == '(')) clear();
    else if (!error_.empty()) return;
    if (expression_.size() >= kMaxExpression) { error_ = "too_long"; return; }
    const char last = expression_.empty() ? '\0' : expression_.back();
    if (digit(ch)) {
        if (last == ')') return;
        expression_ += ch;
    } else if (ch == '.') {
        if (last == ')' || last == '.') return;
        std::size_t start = expression_.size();
        while (start > 0 && (digit(expression_[start-1]) || expression_[start-1] == '.')) --start;
        if (expression_.find('.', start) != std::string::npos) return;
        if (!digit(last)) {
            if (expression_.size() + 2 > kMaxExpression) { error_ = "too_long"; return; }
            expression_ += '0';
        }
        expression_ += '.';
    } else if (ch == '(') {
        if (digit(last) || last == '.' || last == ')') return;
        expression_ += ch;
    } else if (ch == ')') {
        if (balance(expression_) <= 0 || (!digit(last) && last != '.' && last != ')')) return;
        expression_ += ch;
    } else if (ch == '-') {
        // Keep 3*-2 and 3--2 intact: minus can start the next operand.
        if (last == '-' && unary(expression_, expression_.size()-1)) return;
        expression_ += ch;
    } else {
        if (expression_.empty() || last == '(') return;
        if (op(last)) {
            if (unary(expression_, expression_.size()-1)) expression_.pop_back();
            if (expression_.empty() || expression_.back() == '(') return;
            if (op(expression_.back())) expression_.pop_back();
        }
        expression_ += ch;
    }
    changed();
}

void Model::evaluate() {
    if (expression_.empty() || evaluated_) return;
    const auto result = evaluate_expression(expression_);
    if (!result.ok) { error_ = result.error_message; return; }
    history_ = expression_ + " =";
    expression_ = format_value(result.value);
    error_.clear();
    evaluated_ = true;
}
void Model::backspace() {
    if (!expression_.empty()) expression_.pop_back();
    history_.clear();
    changed();
}
void Model::toggle_sign() {
    if (!error_.empty()) return;
    if (expression_.empty()) { expression_ = "-"; changed(); return; }
    std::size_t start = expression_.size();
    if (expression_.back() == ')') {
        int depth = 0;
        while (start > 0) {
            const char ch = expression_[--start];
            if (ch == ')') ++depth;
            else if (ch == '(' && --depth == 0) break;
        }
    } else if (digit(expression_.back()) || expression_.back() == '.') {
        while (start > 0) {
            const char ch = expression_[start-1];
            if (digit(ch) || ch == '.' || ch == 'e' || ch == 'E') --start;
            else if ((ch == '-' || ch == '+') && start >= 2 &&
                     (expression_[start-2] == 'e' || expression_[start-2] == 'E')) --start;
            else break;
        }
    } else if (expression_.back() == '-' && unary(expression_, expression_.size()-1)) {
        expression_.pop_back(); changed(); return;
    }
    if (start > 0 && unary(expression_, start-1)) expression_.erase(start-1, 1);
    else if (expression_.size() < kMaxExpression) expression_.insert(start, 1, '-');
    else { error_ = "too_long"; return; }
    changed();
}
}

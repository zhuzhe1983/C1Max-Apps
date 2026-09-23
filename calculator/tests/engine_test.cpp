#include "engine.hpp"
#include "model.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string &message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void value(const std::string &expression, double expected, double tolerance = 1e-10) {
    const auto result = calculator::evaluate_expression(expression);
    check(result.ok && std::abs(result.value - expected) <= tolerance,
          expression + " => " + calculator::format_value(result.value));
}
void error(const std::string &expression, const std::string &expected = "") {
    const auto result = calculator::evaluate_expression(expression);
    check(!result.ok && (expected.empty() || result.error_message == expected), "reject " + expression);
}
void type(calculator::Model &m, const std::string &text) { for (char ch : text) m.input(ch); }
}

int main() {
    // Original CardputerZero parser behavior and source regression cases.
    value("1+2", 3); value("3*2", 6); value("1/8", .125);
    value("2^10", 1024); value("2^3^2", 512); value("(1+2)*3", 9);
    value("-2^2", -4); value("2^-3", .125); value(".5*4", 2); value("5.*2", 10);
    value("2+3*4-8/2", 10); value("3*-2", -6); value("3--2", 5);
    value("1e-15*2", 2e-15, 1e-28); value("2.5E+2", 250);
    value("1/0.000000000001", 1e12, .001);
    error("1/0", "division_by_zero"); error("0/0", "division_by_zero");
    for (const char *expr : {"", "1+", "1++2", "(1+2", "1.2.3", "1e", "NaN", "Inf", "0x10", "2(3)", ")2("}) error(expr);
    error("10^309", "range"); error("(-1)^0.5", "range");
    error(std::string(193, '1'), "too_long");
    error(std::string(65, '(') + "1" + std::string(65, ')'), "too_long");
    check(calculator::format_value(2.0/3.0) == "0.666666666667", "twelve significant digits");
    check(calculator::format_value(-0.0) == "0", "negative zero");
    check(calculator::format_value(1e-15) == "1e-15", "small results do not become zero");
    check(calculator::format_value(std::numeric_limits<double>::infinity()) == "error", "nonfinite formatting");

    calculator::Model m;
    type(m, "1+2"); m.evaluate(); check(m.display() == "3" && m.history() == "1+2 =", "calculate and history");
    type(m, "*2"); m.evaluate(); check(m.display() == "6", "continue result");
    type(m, "4"); check(m.display() == "4" && m.history().empty(), "new digits reset result");
    m.clear(); type(m, "1/0"); m.evaluate(); check(m.error() == "division_by_zero", "visible divide-by-zero error");
    m.backspace(); type(m, "2"); m.evaluate(); check(m.display() == "0.5" && m.error().empty(), "correct error via backspace");
    m.clear(); type(m, "1/0"); m.evaluate(); type(m, "7"); check(m.display() == "7" && m.error().empty(), "fresh input after error");
    m.clear(); type(m, "..5.2"); check(m.display() == "0.52", "ignore extra decimal points");
    m.toggle_sign(); check(m.display() == "-0.52", "negative decimal"); m.toggle_sign(); check(m.display() == "0.52", "remove negative sign");
    m.clear(); type(m, "3*"); m.toggle_sign(); type(m, "2"); m.evaluate(); check(m.display() == "-6", "signed operand");
    m.clear(); type(m, "3*-2"); m.evaluate(); check(m.display() == "-6", "minus key retains multiplication");
    m.clear(); type(m, "1-2"); m.toggle_sign(); check(m.display() == "1--2", "toggle subtracted operand"); m.evaluate(); check(m.display() == "3", "double minus calculation");
    m.clear(); type(m, "2*(3+4)"); m.toggle_sign(); check(m.display() == "2*-(3+4)", "toggle parenthesized operand"); m.evaluate(); check(m.display() == "-14", "negative parenthesized operand");
    m.clear(); type(m, "2*(3+4)"); m.toggle_sign(); m.toggle_sign(); check(m.display() == "2*(3+4)", "toggle group twice");
    m.clear(); type(m, "1+"); m.input('*'); type(m, "3"); m.evaluate(); check(m.display() == "3", "replace pending operator");
    m.clear(); type(m, "1/1000000000000000"); m.evaluate(); check(m.display() == "1e-15", "small computed result");
    m.toggle_sign(); check(m.display() == "-1e-15", "negate scientific result"); type(m, "*2"); m.evaluate(); check(m.display() == "-2e-15", "continue scientific result");
    m.clear(); m.backspace(); m.toggle_sign(); m.toggle_sign(); check(m.display() == "0", "empty editor operations");
    type(m, std::string(192, '1')); m.input('2'); check(m.error() == "too_long" && m.expression().size() == 192, "bounded touch input");
    m.backspace(); check(m.expression().size() == 191 && m.error().empty(), "recover from length error");
    m.clear(); check(m.display() == "0" && m.error().empty() && m.history().empty(), "clear all");
    if (!failures) std::cout << "Calculator engine and editing tests passed\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

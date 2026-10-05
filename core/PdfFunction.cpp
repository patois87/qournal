/*
 * Qournal
 *
 * @license GNU GPLv2 or later
 */

#include "PdfFunction.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Pdf {

namespace {

constexpr int MAX_DEPTH = 8;
constexpr int MAX_STACK = 100;  ///< as the specification limits the calculator
constexpr qint64 MAX_SAMPLES = 1 << 22;

std::vector<double> numbers(const Reader& reader, const Value& value) {
    std::vector<double> result;
    // Kept in a variable: the loop must not go over a part of a temporary value
    const Value array = reader.resolve(value);
    for (const Value& item: array.toArray()) {
        result.push_back(reader.resolve(item).toNumber());
    }
    return result;
}

std::vector<double> entry(const Reader& reader, const Dict& dict, const char* key) {
    const Value* value = dict.find(key);
    return value ? numbers(reader, *value) : std::vector<double>();
}

double interpolate(double x, double xmin, double xmax, double ymin, double ymax) {
    return xmax == xmin ? ymin : ymin + (x - xmin) * (ymax - ymin) / (xmax - xmin);
}

/// Type 0: a table of samples, interpolated linearly between them
class Sampled: public Function {
public:
    bool init(const Reader& reader, const Dict& dict, const QByteArray& data) {
        m_size.clear();
        for (double size: entry(reader, dict, "Size")) {
            m_size.push_back(static_cast<int>(size));
        }
        const Value* bitsValue = dict.find("BitsPerSample");
        m_bits = bitsValue ? reader.resolve(*bitsValue).toInt() : 0;
        const int m = inputs();
        const int n = outputs();
        if (m < 1 || m > 8 || n < 1 || static_cast<int>(m_size.size()) != m ||
            !(m_bits == 1 || m_bits == 2 || m_bits == 4 || m_bits == 8 || m_bits == 12 || m_bits == 16 ||
              m_bits == 24 || m_bits == 32)) {
            return false;
        }
        qint64 count = n;
        for (int size: m_size) {
            if (size < 1) {
                return false;
            }
            count *= size;
            if (count > MAX_SAMPLES) {
                return false;
            }
        }
        if (data.size() * 8 < count * m_bits) {
            return false;
        }
        m_encode = entry(reader, dict, "Encode");
        if (m_encode.size() != static_cast<size_t>(2 * m)) {
            m_encode.clear();
            for (int size: m_size) {
                m_encode.push_back(0);
                m_encode.push_back(size - 1);
            }
        }
        m_decode = entry(reader, dict, "Decode");
        if (m_decode.size() != static_cast<size_t>(2 * n)) {
            m_decode = m_range;
        }
        // The samples as numbers from 0 to 1
        m_samples.resize(static_cast<size_t>(count));
        const double max = std::pow(2.0, m_bits) - 1;
        qint64 bit = 0;
        for (double& sample: m_samples) {
            quint64 value = 0;
            for (int i = 0; i < m_bits; ++i, ++bit) {
                value = (value << 1) | ((static_cast<uchar>(data[bit / 8]) >> (7 - bit % 8)) & 1);
            }
            sample = static_cast<double>(value) / max;
        }
        return true;
    }

protected:
    std::vector<double> evaluate(const std::vector<double>& in) const override {
        const int m = inputs();
        const int n = outputs();
        // The place in the table, and the two samples around it in each dimension
        std::vector<int> low(static_cast<size_t>(m));
        std::vector<double> fraction(static_cast<size_t>(m));
        for (int i = 0; i < m; ++i) {
            const size_t k = static_cast<size_t>(i);
            double e = interpolate(in[k], m_domain[2 * k], m_domain[2 * k + 1], m_encode[2 * k], m_encode[2 * k + 1]);
            e = std::clamp(e, 0.0, static_cast<double>(m_size[k] - 1));
            low[k] = std::min(static_cast<int>(std::floor(e)), std::max(m_size[k] - 2, 0));
            fraction[k] = m_size[k] > 1 ? e - low[k] : 0;
        }
        std::vector<double> out(static_cast<size_t>(n), 0.0);
        // The corners of the cell, weighted
        for (int corner = 0; corner < (1 << m); ++corner) {
            double weight = 1;
            qint64 index = 0;
            qint64 stride = 1;
            for (int i = 0; i < m; ++i) {
                const size_t k = static_cast<size_t>(i);
                const bool high = (corner >> i) & 1;
                weight *= high ? fraction[k] : 1 - fraction[k];
                const int position = std::min(low[k] + (high ? 1 : 0), m_size[k] - 1);
                index += position * stride;
                stride *= m_size[k];
            }
            if (weight == 0) {
                continue;
            }
            for (int j = 0; j < n; ++j) {
                out[static_cast<size_t>(j)] += weight * m_samples[static_cast<size_t>(index * n + j)];
            }
        }
        for (int j = 0; j < n; ++j) {
            const size_t k = static_cast<size_t>(j);
            out[k] = interpolate(out[k], 0, 1, m_decode[2 * k], m_decode[2 * k + 1]);
        }
        return out;
    }

private:
    friend class Function;
    std::vector<int> m_size;
    int m_bits = 0;
    std::vector<double> m_encode;
    std::vector<double> m_decode;
    std::vector<double> m_samples;
};

/// Type 2: C0 + x^N * (C1 - C0)
class Exponential: public Function {
public:
    bool init(const Reader& reader, const Dict& dict) {
        m_c0 = entry(reader, dict, "C0");
        m_c1 = entry(reader, dict, "C1");
        if (m_c0.empty()) {
            m_c0 = {0};
        }
        if (m_c1.empty()) {
            m_c1 = {1};
        }
        const Value* nValue = dict.find("N");
        m_n = nValue ? reader.resolve(*nValue).toNumber() : 1;
        return m_c0.size() == m_c1.size() && inputs() == 1;
    }

protected:
    std::vector<double> evaluate(const std::vector<double>& in) const override {
        const double x = in[0];
        const double power = m_n == 1 ? x : std::pow(x, m_n);
        std::vector<double> out(m_c0.size());
        for (size_t i = 0; i < out.size(); ++i) {
            out[i] = m_c0[i] + power * (m_c1[i] - m_c0[i]);
        }
        return out;
    }

private:
    std::vector<double> m_c0;
    std::vector<double> m_c1;
    double m_n = 1;
};

/// Type 3: functions for parts of the domain, one after the other
class Stitching: public Function {
public:
    bool init(const Reader& reader, const Dict& dict, int depth) {
        const Value* functions = dict.find("Functions");
        if (!functions) {
            return false;
        }
        const Value list = reader.resolve(*functions);
        for (const Value& item: list.toArray()) {
            auto function = Function::parse(reader, item, depth + 1);
            if (!function || function->inputs() != 1) {
                return false;
            }
            m_functions.push_back(std::move(function));
        }
        m_bounds = entry(reader, dict, "Bounds");
        m_encode = entry(reader, dict, "Encode");
        const size_t k = m_functions.size();
        return k > 0 && inputs() == 1 && m_bounds.size() == k - 1 && m_encode.size() == 2 * k;
    }

protected:
    std::vector<double> evaluate(const std::vector<double>& in) const override {
        const double x = in[0];
        size_t i = 0;
        while (i < m_bounds.size() && x >= m_bounds[i]) {
            ++i;
        }
        const double low = i == 0 ? m_domain[0] : m_bounds[i - 1];
        const double high = i == m_bounds.size() ? m_domain[1] : m_bounds[i];
        return (*m_functions[i])({interpolate(x, low, high, m_encode[2 * i], m_encode[2 * i + 1])});
    }

public:
    void jumps(std::vector<double>& places, int depth) const override {
        if (depth > MAX_DEPTH) {
            return;
        }
        for (size_t i = 0; i < m_functions.size(); ++i) {
            const double low = i == 0 ? m_domain[0] : m_bounds[i - 1];
            const double high = i == m_bounds.size() ? m_domain[1] : m_bounds[i];
            if (i > 0) {
                places.push_back(low);
            }
            // Those of the parts, from their domain back into this one
            std::vector<double> inner;
            m_functions[i]->jumps(inner, depth + 1);
            for (double place: inner) {
                places.push_back(interpolate(place, m_encode[2 * i], m_encode[2 * i + 1], low, high));
            }
        }
    }

private:
    std::vector<std::shared_ptr<Function>> m_functions;
    std::vector<double> m_bounds;
    std::vector<double> m_encode;
};

/// Type 4: a program in a small part of PostScript
class Calculator: public Function {
public:
    bool init(const QByteArray& code) {
        qsizetype at = 0;
        // The program is one block in braces
        while (at < code.size() && code[at] != '{') {
            ++at;
        }
        if (at >= code.size()) {
            return false;
        }
        ++at;
        return parseBlock(code, at, m_program, 0) && !m_range.empty();
    }

protected:
    std::vector<double> evaluate(const std::vector<double>& in) const override {
        std::vector<double> stack(in);
        if (!run(m_program, stack)) {
            return std::vector<double>(static_cast<size_t>(outputs()), 0.0);
        }
        const size_t n = static_cast<size_t>(outputs());
        std::vector<double> out(n, 0.0);
        // The results are the topmost values
        for (size_t i = 0; i < n && i < stack.size(); ++i) {
            out[n - 1 - i] = stack[stack.size() - 1 - i];
        }
        return out;
    }

private:
    struct Op {
        enum class Kind { Number, Operator, If, IfElse };
        Kind kind = Kind::Number;
        double number = 0;
        QByteArray name;
        std::vector<Op> first;   ///< of if and ifelse
        std::vector<Op> second;  ///< of ifelse
    };

    static bool parseBlock(const QByteArray& code, qsizetype& at, std::vector<Op>& block, int depth) {
        if (depth > 16) {
            return false;
        }
        std::vector<std::vector<Op>> pending;  // blocks before "if" or "ifelse"
        while (at < code.size()) {
            const char c = code[at];
            if (std::isspace(static_cast<uchar>(c))) {
                ++at;
            } else if (c == '{') {
                ++at;
                std::vector<Op> inner;
                if (!parseBlock(code, at, inner, depth + 1)) {
                    return false;
                }
                pending.push_back(std::move(inner));
            } else if (c == '}') {
                ++at;
                return pending.empty();
            } else {
                const qsizetype start = at;
                while (at < code.size() && !std::isspace(static_cast<uchar>(code[at])) && code[at] != '{' &&
                       code[at] != '}') {
                    ++at;
                }
                const QByteArray token = code.mid(start, at - start);
                Op op;
                bool isNumber = false;
                op.number = token.toDouble(&isNumber);
                if (isNumber) {
                    op.kind = Op::Kind::Number;
                } else if (token == "if" && pending.size() == 1) {
                    op.kind = Op::Kind::If;
                    op.first = std::move(pending[0]);
                    pending.clear();
                } else if (token == "ifelse" && pending.size() == 2) {
                    op.kind = Op::Kind::IfElse;
                    op.first = std::move(pending[0]);
                    op.second = std::move(pending[1]);
                    pending.clear();
                } else if (!pending.empty()) {
                    return false;
                } else {
                    op.kind = Op::Kind::Operator;
                    op.name = token;
                }
                block.push_back(std::move(op));
            }
        }
        return false;  // no closing brace
    }

    static bool run(const std::vector<Op>& program, std::vector<double>& s) {
        auto pop = [&s](double& value) {
            if (s.empty()) {
                return false;
            }
            value = s.back();
            s.pop_back();
            return true;
        };
        for (const Op& op: program) {
            if (s.size() > MAX_STACK) {
                return false;
            }
            if (op.kind == Op::Kind::Number) {
                s.push_back(op.number);
                continue;
            }
            if (op.kind == Op::Kind::If || op.kind == Op::Kind::IfElse) {
                double condition = 0;
                if (!pop(condition)) {
                    return false;
                }
                const std::vector<Op>& chosen = condition != 0 ? op.first : op.second;
                if ((op.kind == Op::Kind::IfElse || condition != 0) && !run(chosen, s)) {
                    return false;
                }
                continue;
            }
            const QByteArray& n = op.name;
            double a = 0;
            double b = 0;
            if (n == "true" || n == "false") {
                s.push_back(n == "true" ? 1 : 0);
            } else if (n == "dup") {
                if (!pop(a)) {
                    return false;
                }
                s.push_back(a);
                s.push_back(a);
            } else if (n == "pop") {
                if (!pop(a)) {
                    return false;
                }
            } else if (n == "exch") {
                if (!pop(b) || !pop(a)) {
                    return false;
                }
                s.push_back(b);
                s.push_back(a);
            } else if (n == "copy") {
                if (!pop(a)) {
                    return false;
                }
                const qsizetype count = static_cast<qsizetype>(a);
                if (count < 0 || count > static_cast<qsizetype>(s.size())) {
                    return false;
                }
                s.insert(s.end(), s.end() - count, s.end());
            } else if (n == "index") {
                if (!pop(a)) {
                    return false;
                }
                const qsizetype i = static_cast<qsizetype>(a);
                if (i < 0 || i >= static_cast<qsizetype>(s.size())) {
                    return false;
                }
                s.push_back(s[s.size() - 1 - static_cast<size_t>(i)]);
            } else if (n == "roll") {
                if (!pop(b) || !pop(a)) {
                    return false;
                }
                const qsizetype count = static_cast<qsizetype>(a);
                if (count < 0 || count > static_cast<qsizetype>(s.size())) {
                    return false;
                }
                if (count > 0) {
                    qsizetype shift = static_cast<qsizetype>(b) % count;
                    if (shift < 0) {
                        shift += count;
                    }
                    std::rotate(s.end() - count, s.end() - shift, s.end());
                }
            } else {
                // One operand
                static const QByteArray UNARY[] = {"abs", "neg", "ceiling", "floor", "round", "truncate", "sqrt",
                                                   "sin", "cos", "ln",      "log",   "cvi",   "cvr",      "not"};
                if (std::find(std::begin(UNARY), std::end(UNARY), n) != std::end(UNARY)) {
                    if (!pop(a)) {
                        return false;
                    }
                    double v = a;
                    if (n == "abs") {
                        v = std::abs(a);
                    } else if (n == "neg") {
                        v = -a;
                    } else if (n == "ceiling") {
                        v = std::ceil(a);
                    } else if (n == "floor") {
                        v = std::floor(a);
                    } else if (n == "round") {
                        v = std::floor(a + 0.5);
                    } else if (n == "truncate" || n == "cvi") {
                        v = std::trunc(a);
                    } else if (n == "sqrt") {
                        v = std::sqrt(std::max(a, 0.0));
                    } else if (n == "sin") {
                        v = std::sin(a * M_PI / 180);
                    } else if (n == "cos") {
                        v = std::cos(a * M_PI / 180);
                    } else if (n == "ln") {
                        v = a > 0 ? std::log(a) : 0;
                    } else if (n == "log") {
                        v = a > 0 ? std::log10(a) : 0;
                    } else if (n == "not") {
                        // Booleans are 1 and 0; integers are inverted bitwise
                        v = (a == 0 || a == 1) ? 1 - a : static_cast<double>(~static_cast<qint64>(a));
                    }
                    s.push_back(v);
                    continue;
                }
                // Two operands
                if (!pop(b) || !pop(a)) {
                    return false;
                }
                double v = 0;
                if (n == "add") {
                    v = a + b;
                } else if (n == "sub") {
                    v = a - b;
                } else if (n == "mul") {
                    v = a * b;
                } else if (n == "div") {
                    v = b != 0 ? a / b : 0;
                } else if (n == "idiv") {
                    v = static_cast<qint64>(b) != 0 ?
                                static_cast<double>(static_cast<qint64>(a) / static_cast<qint64>(b)) :
                                0;
                } else if (n == "mod") {
                    v = static_cast<qint64>(b) != 0 ?
                                static_cast<double>(static_cast<qint64>(a) % static_cast<qint64>(b)) :
                                0;
                } else if (n == "exp") {
                    v = std::pow(a, b);
                } else if (n == "atan") {
                    v = std::atan2(a, b) * 180 / M_PI;
                    if (v < 0) {
                        v += 360;
                    }
                } else if (n == "eq") {
                    v = a == b;
                } else if (n == "ne") {
                    v = a != b;
                } else if (n == "gt") {
                    v = a > b;
                } else if (n == "ge") {
                    v = a >= b;
                } else if (n == "lt") {
                    v = a < b;
                } else if (n == "le") {
                    v = a <= b;
                } else if (n == "and") {
                    v = static_cast<double>(static_cast<qint64>(a) & static_cast<qint64>(b));
                } else if (n == "or") {
                    v = static_cast<double>(static_cast<qint64>(a) | static_cast<qint64>(b));
                } else if (n == "xor") {
                    v = static_cast<double>(static_cast<qint64>(a) ^ static_cast<qint64>(b));
                } else if (n == "bitshift") {
                    const qint64 value = static_cast<qint64>(a);
                    const int shift = static_cast<int>(b);
                    v = static_cast<double>(shift >= 0 ? value << std::min(shift, 62) : value >> std::min(-shift, 62));
                } else {
                    return false;  // not an operator of the calculator
                }
                s.push_back(v);
            }
        }
        return true;
    }

    std::vector<Op> m_program;
};

}  // namespace

std::vector<double> Function::operator()(std::vector<double> in) const {
    in.resize(static_cast<size_t>(inputs()), 0.0);
    for (size_t i = 0; i < in.size(); ++i) {
        in[i] = std::clamp(in[i], std::min(m_domain[2 * i], m_domain[2 * i + 1]),
                           std::max(m_domain[2 * i], m_domain[2 * i + 1]));
    }
    std::vector<double> out = evaluate(in);
    for (size_t i = 0; i < out.size() && 2 * i + 1 < m_range.size(); ++i) {
        out[i] = std::clamp(out[i], std::min(m_range[2 * i], m_range[2 * i + 1]),
                            std::max(m_range[2 * i], m_range[2 * i + 1]));
    }
    return out;
}

std::shared_ptr<Function> Function::parse(const Reader& reader, const Value& value, int depth) {
    if (depth > MAX_DEPTH) {
        return nullptr;
    }
    Dict dict;
    QByteArray data;
    bool isStream = false;
    if (value.kind() == Value::Kind::Ref) {
        isStream = reader.stream(value.toRef(), dict, data);
        if (!isStream) {
            const Value object = reader.object(value.toRef());
            if (object.kind() != Value::Kind::Dict) {
                return nullptr;
            }
            dict = object.toDict();
        }
    } else if (value.kind() == Value::Kind::Dict) {
        dict = value.toDict();
    } else {
        return nullptr;
    }
    const Value* typeValue = dict.find("FunctionType");
    if (!typeValue) {
        return nullptr;
    }
    const int type = reader.resolve(*typeValue).toInt();
    std::shared_ptr<Function> result;
    const auto common = [&](Function& function) {
        function.m_domain = entry(reader, dict, "Domain");
        function.m_range = entry(reader, dict, "Range");
        return !function.m_domain.empty() && function.m_domain.size() % 2 == 0 && function.m_range.size() % 2 == 0;
    };
    if (type == 0 && isStream) {
        auto sampled = std::make_shared<Sampled>();
        if (common(*sampled) && !sampled->m_range.empty() && sampled->init(reader, dict, data)) {
            result = sampled;
        }
    } else if (type == 2) {
        auto exponential = std::make_shared<Exponential>();
        if (common(*exponential) && exponential->init(reader, dict)) {
            result = exponential;
        }
    } else if (type == 3) {
        auto stitching = std::make_shared<Stitching>();
        if (common(*stitching) && stitching->init(reader, dict, depth)) {
            result = stitching;
        }
    } else if (type == 4 && isStream) {
        auto calculator = std::make_shared<Calculator>();
        if (common(*calculator) && calculator->init(data)) {
            result = calculator;
        }
    }
    return result;
}

}  // namespace Pdf

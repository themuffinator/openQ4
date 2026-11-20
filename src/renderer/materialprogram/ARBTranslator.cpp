// Copyright (C) 2026 DarkMatter Productions
#include "ARBTranslator.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <set>
#include <utility>

namespace oq4material {
namespace {

constexpr int MaxLocalParameters = 96;   // ARB_vertex_program minimum
constexpr int MaxEnvParameters = 96;
constexpr int MaxTextureUnits = 8;       // ARB fragmentMap units (MAX_FRAGMENT_IMAGES)
constexpr int MaxTexCoords = 8;
constexpr int MaxArrayElements = 256;

// ---------------------------------------------------------------------------
// Section selection and tokens
// ---------------------------------------------------------------------------

// Mirrors R_LoadARBProgram: the first header, up to and including the first
// END. A comment or identifier containing END truncates the program there,
// exactly as it does for the OpenGL driver.
bool SelectSection(const std::string &file, bool vertex, std::string &section, std::string &error) {
    const char *header = vertex ? "!!ARBvp" : "!!ARBfp";
    const size_t start = file.find(header);
    if (start == std::string::npos) {
        error = std::string("missing ") + header + " header";
        return false;
    }
    const size_t end = file.find("END", start);
    if (end == std::string::npos) {
        error = "missing END terminator";
        return false;
    }
    section = file.substr(start, end + 3 - start);
    return true;
}

enum class TokenKind { Identifier, Number, Punct };

struct Token {
    TokenKind kind;
    std::string text;
    int line;
};

bool IdentifierStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
bool IdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
bool Digit(char c) { return c >= '0' && c <= '9'; }

bool Tokenize(const std::string &text, size_t begin, std::vector<Token> &tokens, std::string &error) {
    int line = 1;
    for (size_t i = 0; i < begin; ++i) { if (text[i] == '\n') { ++line; } }
    for (size_t i = begin; i < text.size();) {
        const char c = text[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '#') {
            while (i < text.size() && text[i] != '\n') { ++i; }
            continue;
        }
        const size_t start = i;
        if (IdentifierStart(c)) {
            while (i < text.size() && IdentifierChar(text[i])) { ++i; }
            tokens.push_back({TokenKind::Identifier, text.substr(start, i - start), line});
            continue;
        }
        if (Digit(c) || (c == '.' && i + 1 < text.size() && Digit(text[i + 1]))) {
            while (i < text.size() && Digit(text[i])) { ++i; }
            // "0..3" is a range: the first period belongs to "..".
            if (i < text.size() && text[i] == '.' && !(i + 1 < text.size() && text[i + 1] == '.')) {
                ++i;
                while (i < text.size() && Digit(text[i])) { ++i; }
            }
            if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
                size_t exponent = i + 1;
                if (exponent < text.size() && (text[exponent] == '+' || text[exponent] == '-')) { ++exponent; }
                if (exponent < text.size() && Digit(text[exponent])) {
                    i = exponent;
                    while (i < text.size() && Digit(text[i])) { ++i; }
                }
            }
            tokens.push_back({TokenKind::Number, text.substr(start, i - start), line});
            continue;
        }
        if (c == '.' && i + 1 < text.size() && text[i + 1] == '.') {
            tokens.push_back({TokenKind::Punct, "..", line});
            i += 2;
            continue;
        }
        if (std::strchr(";,.[]{}=-+|", c) != nullptr) {
            tokens.push_back({TokenKind::Punct, std::string(1, c), line});
            ++i;
            continue;
        }
        error = "line " + std::to_string(line) + ": unexpected character '" + std::string(1, c) + "'";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// GLSL emission helpers
// ---------------------------------------------------------------------------

std::string Float(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    std::string text = buffer;
    if (text.find_first_of(".eEn") == std::string::npos) { text += ".0"; }
    if (text == "inf" || text == "-inf" || text.find("nan") != std::string::npos) { return value < 0 ? "-1e38" : "1e38"; }
    return text;
}

std::string Vec4(const double v[4]) {
    return "vec4(" + Float(v[0]) + ", " + Float(v[1]) + ", " + Float(v[2]) + ", " + Float(v[3]) + ")";
}

const char *const Components = "xyzw";

int ComponentIndex(char c, bool fragment) {
    switch (c) {
    case 'x': return 0;
    case 'y': return 1;
    case 'z': return 2;
    case 'w': return 3;
    default: break;
    }
    if (fragment) {
        switch (c) {
        case 'r': return 0;
        case 'g': return 1;
        case 'b': return 2;
        case 'a': return 3;
        default: break;
        }
    }
    return -1;
}

enum class SymbolKind { Temp, Address, Attrib, Param, ParamArray, Output };

struct Symbol {
    SymbolKind kind;
    std::string glsl;      // value expression, lvalue or array variable
    int size = 1;          // ParamArray element count
};

struct Operand {
    std::string base;      // vec4 expression before swizzle/negation
    bool negate = false;
    int swizzle[4] = {0, 1, 2, 3};
    int swizzleCount = 0;  // 0: none, 1: scalar, 4: full
};

struct Destination {
    std::string lvalue;
    int mask = 0xF;
    bool address = false;
    bool resultColorOrDepth = false;
};

enum class Program { Vertex, Fragment };

struct Translator {
    Program program;
    std::string name;
    std::vector<Token> tokens;
    size_t pos = 0;
    std::string error;

    std::map<std::string, Symbol> symbols;
    std::string body;           // statements inside main()
    std::set<std::string> helpers;
    bool positionInvariant = false;
    bool usesTangent = false;
    bool usesBitangent = false;
    // Outputs written; the epilogue copies them to the GLSL built-ins.
    std::set<std::string> outputs;
    unsigned textureMask = 0;
    unsigned cubeMask = 0;
    int implicitArrays = 0;

    // Shared with the caller: parameter slot allocation spans both stages.
    std::vector<ARBParameter> *parameters = nullptr;
    std::map<std::pair<int, int>, std::string> *parameterNames = nullptr;

    bool vertex() const { return program == Program::Vertex; }
    bool fragment() const { return program == Program::Fragment; }

    bool Fail(const std::string &message) {
        if (error.empty()) {
            const int line = pos < tokens.size() ? tokens[pos].line : (tokens.empty() ? 1 : tokens.back().line);
            error = name + ":" + std::to_string(line) + ": " + message;
        }
        return false;
    }

    const Token *Peek(size_t ahead = 0) const { return pos + ahead < tokens.size() ? &tokens[pos + ahead] : nullptr; }
    bool At(const char *text, size_t ahead = 0) const {
        const Token *token = Peek(ahead);
        return token != nullptr && token->text == text;
    }
    // ".name" where name is one of the given binding qualifiers.
    bool AtQualifier(std::initializer_list<const char *> names) const {
        if (!At(".")) { return false; }
        const Token *token = Peek(1);
        if (token == nullptr || token->kind != TokenKind::Identifier) { return false; }
        for (const char *candidate : names) { if (token->text == candidate) { return true; } }
        return false;
    }
    bool Accept(const char *text) {
        if (At(text)) { ++pos; return true; }
        return false;
    }
    bool Expect(const char *text) {
        if (Accept(text)) { return true; }
        const Token *token = Peek();
        return Fail(std::string("expected '") + text + "'" + (token ? " before '" + token->text + "'" : " at end of program"));
    }
    bool ExpectIdentifier(std::string &out) {
        const Token *token = Peek();
        if (token == nullptr || token->kind != TokenKind::Identifier) { return Fail("expected an identifier"); }
        out = token->text;
        ++pos;
        return true;
    }
    bool ExpectInteger(int &out) {
        const Token *token = Peek();
        if (token == nullptr || token->kind != TokenKind::Number || token->text.find_first_of(".eE") != std::string::npos) {
            return Fail("expected an integer");
        }
        out = std::atoi(token->text.c_str());
        ++pos;
        return true;
    }
    bool ExpectSignedFloat(double &out) {
        bool negative = false;
        if (Accept("-")) { negative = true; } else { Accept("+"); }
        const Token *token = Peek();
        if (token == nullptr || token->kind != TokenKind::Number) { return Fail("expected a number"); }
        out = std::strtod(token->text.c_str(), nullptr);
        if (negative) { out = -out; }
        ++pos;
        return true;
    }

    // ------------------------------------------------------------------
    // Program parameters
    // ------------------------------------------------------------------

    bool Parameter(ARBParameterSpace space, int index, std::string &out) {
        if (index < 0 || index >= (space == ARBParameterSpace::VertexEnv || space == ARBParameterSpace::FragmentEnv
                ? MaxEnvParameters : MaxLocalParameters)) {
            return Fail("program parameter index " + std::to_string(index) + " is out of range");
        }
        if (space == ARBParameterSpace::VertexEnv && !ARBVertexEnvironmentDefined(index)) {
            return Fail("program.env[" + std::to_string(index) + "] has no value for vertex programs in material stages");
        }
        if (space == ARBParameterSpace::FragmentEnv && !ARBFragmentEnvironmentDefined(index)) {
            return Fail("program.env[" + std::to_string(index) + "] has no value for fragment programs in material stages");
        }
        const auto key = std::make_pair(static_cast<int>(space), index);
        auto found = parameterNames->find(key);
        if (found != parameterNames->end()) { out = found->second; return true; }
        if (parameters->size() >= static_cast<size_t>(MaxParameters)) {
            return Fail("more than " + std::to_string(MaxParameters) + " distinct program parameters");
        }
        static const char *prefixes[] = {"arbVertexLocal", "arbVertexEnv", "arbFragmentLocal", "arbFragmentEnv"};
        out = prefixes[static_cast<int>(space)] + std::to_string(index);
        parameters->push_back({out, space, index});
        parameterNames->emplace(key, out);
        return true;
    }

    // Parses "[n]" or, where allowed, "[a..b]".
    bool IndexRange(int &first, int &last, bool allowRange) {
        if (!Expect("[") || !ExpectInteger(first)) { return false; }
        last = first;
        if (Accept("..")) {
            if (!allowRange) { return Fail("a parameter range is only valid in a PARAM array"); }
            if (!ExpectInteger(last)) { return false; }
            if (last < first) { return Fail("descending parameter range"); }
        }
        return Expect("]");
    }

    void Helper(const char *name) { helpers.insert(name); }

    std::string MatrixRow(const std::string &matrix, int row, const std::string &modifier) {
        const std::string r = std::to_string(row);
        if (modifier == "transpose") { return matrix + "[" + r + "]"; }
        if (modifier == "inverse" || modifier == "invtrans") {
            Helper("inverse");
            const std::string inverse = "arbInverse(" + matrix + ")";
            if (modifier == "invtrans") { return inverse + "[" + r + "]"; }
            return "vec4(" + inverse + "[0][" + r + "], " + inverse + "[1][" + r + "], " + inverse + "[2][" + r + "], " + inverse + "[3][" + r + "])";
        }
        return "vec4(" + matrix + "[0][" + r + "], " + matrix + "[1][" + r + "], " + matrix + "[2][" + r + "], " + matrix + "[3][" + r + "])";
    }

    // state.matrix.<name>[.<modifier>][.row[a..b]] after "state" "." "matrix".
    bool MatrixBinding(std::vector<std::string> &values, bool allowMultiple) {
        if (!Expect(".")) { return false; }
        std::string matrixName;
        if (!ExpectIdentifier(matrixName)) { return false; }
        std::string matrix;
        if (matrixName == "modelview") {
            if (At("[")) {
                int first = 0, last = 0;
                if (!IndexRange(first, last, false)) { return false; }
                if (first != 0) { return Fail("vertex-blend modelview matrices are not available to material stages"); }
            }
            matrix = "gl_ModelViewMatrix";
        } else if (matrixName == "projection") {
            matrix = "gl_ProjectionMatrix";
        } else if (matrixName == "mvp") {
            matrix = "gl_ModelViewProjectionMatrix";
        } else if (matrixName == "texture") {
            int unit = 0;
            if (At("[")) {
                int last = 0;
                if (!IndexRange(unit, last, false)) { return false; }
            }
            if (unit < 0 || unit >= MaxTexCoords) { return Fail("texture matrix unit out of range"); }
            matrix = "gl_TextureMatrix[" + std::to_string(unit) + "]";
        } else {
            return Fail("state.matrix." + matrixName + " is not available to material stages");
        }
        std::string modifier;
        int firstRow = 0, lastRow = 3;
        bool explicitRows = false;
        while (Accept(".")) {
            std::string part;
            if (!ExpectIdentifier(part)) { return false; }
            if (part == "row") {
                if (!IndexRange(firstRow, lastRow, allowMultiple)) { return false; }
                if (firstRow < 0 || lastRow > 3) { return Fail("matrix row out of range"); }
                explicitRows = true;
                break;
            }
            if ((part == "inverse" || part == "transpose" || part == "invtrans") && modifier.empty()) {
                modifier = part;
                continue;
            }
            return Fail("unknown matrix binding component '" + part + "'");
        }
        if (!explicitRows && !allowMultiple) { return Fail("a whole matrix binding needs a PARAM array"); }
        for (int row = firstRow; row <= lastRow; ++row) { values.push_back(MatrixRow(matrix, row, modifier)); }
        return true;
    }

    // Parses one PARAM binding, expanding ranges and matrices when allowed.
    bool ParamBinding(std::vector<std::string> &values, bool allowMultiple) {
        const Token *token = Peek();
        if (token == nullptr) { return Fail("missing parameter binding"); }
        if (token->text == "{") {
            ++pos;
            double v[4] = {0.0, 0.0, 0.0, 1.0};
            int count = 0;
            do {
                if (count == 4) { return Fail("constant vector has more than four components"); }
                if (!ExpectSignedFloat(v[count])) { return false; }
                ++count;
            } while (Accept(","));
            if (!Expect("}")) { return false; }
            values.push_back(Vec4(v));
            return true;
        }
        if (token->kind == TokenKind::Number || token->text == "-" || token->text == "+") {
            double s = 0.0;
            if (!ExpectSignedFloat(s)) { return false; }
            const double v[4] = {s, s, s, s};
            values.push_back(Vec4(v));
            return true;
        }
        if (token->text == "program") {
            ++pos;
            std::string space;
            if (!Expect(".") || !ExpectIdentifier(space)) { return false; }
            const bool env = space == "env";
            if (!env && space != "local") { return Fail("unknown program parameter space '" + space + "'"); }
            int first = 0, last = 0;
            if (!IndexRange(first, last, allowMultiple)) { return false; }
            if (last - first + 1 > MaxArrayElements) { return Fail("parameter range is too large"); }
            const ARBParameterSpace kind = vertex()
                ? (env ? ARBParameterSpace::VertexEnv : ARBParameterSpace::VertexLocal)
                : (env ? ARBParameterSpace::FragmentEnv : ARBParameterSpace::FragmentLocal);
            for (int i = first; i <= last; ++i) {
                std::string uniform;
                if (!Parameter(kind, i, uniform)) { return false; }
                values.push_back(uniform);
            }
            return true;
        }
        if (token->text == "state") {
            ++pos;
            std::string group;
            if (!Expect(".") || !ExpectIdentifier(group)) { return false; }
            if (group == "matrix") { return MatrixBinding(values, allowMultiple); }
            return Fail("state." + group + " is fixed-function state that material stages do not provide");
        }
        return Fail("unsupported parameter binding '" + token->text + "'");
    }

    // vertex.* (vertex programs) after "vertex".
    bool VertexAttribute(std::string &out) {
        if (!Expect(".")) { return false; }
        std::string attribute;
        if (!ExpectIdentifier(attribute)) { return false; }
        auto texCoord = [&](int unit) -> bool {
            if (unit < 0 || unit >= MaxTexCoords) { return Fail("texture coordinate unit out of range"); }
            // Material stages only supply unit 0's array; the others read the
            // current OpenGL texture coordinate, which the engine never sets.
            out = unit == 0 ? "gl_MultiTexCoord0" : "vec4(0.0, 0.0, 0.0, 1.0)";
            return true;
        };
        if (attribute == "position") { out = "gl_Vertex"; return true; }
        if (attribute == "normal") { out = "vec4(gl_Normal, 1.0)"; return true; }
        if (attribute == "color") {
            if (AtQualifier({"primary", "secondary"})) {
                pos += 2;
                if (tokens[pos - 1].text != "primary") { return Fail("vertex.color.secondary is not supplied to material stages"); }
            }
            out = "gl_Color";
            return true;
        }
        if (attribute == "texcoord") {
            int unit = 0;
            if (At("[")) {
                int last = 0;
                if (!IndexRange(unit, last, false)) { return false; }
            }
            return texCoord(unit);
        }
        if (attribute == "attrib") {
            int index = 0, last = 0;
            if (!IndexRange(index, last, false)) { return false; }
            // The material path enables 9/10 explicitly; 0/2/3/8 alias the
            // conventional position/normal/color/texcoord0 arrays it binds.
            switch (index) {
            case 0: out = "gl_Vertex"; return true;
            case 2: out = "vec4(gl_Normal, 1.0)"; return true;
            case 3: out = "gl_Color"; return true;
            case 8: return texCoord(0);
            case 9: usesTangent = true; out = "vec4(attr_Tangent, 1.0)"; return true;
            case 10: usesBitangent = true; out = "vec4(attr_Bitangent, 1.0)"; return true;
            default: return Fail("vertex.attrib[" + std::to_string(index) + "] is not supplied to material stages");
            }
        }
        return Fail("vertex." + attribute + " is not supplied to material stages");
    }

    // fragment.* (fragment programs) after "fragment".
    bool FragmentAttribute(std::string &out) {
        if (!Expect(".")) { return false; }
        std::string attribute;
        if (!ExpectIdentifier(attribute)) { return false; }
        if (attribute == "position") { out = "gl_FragCoord"; return true; }
        if (attribute == "color") {
            if (AtQualifier({"primary", "secondary"})) {
                pos += 2;
                if (tokens[pos - 1].text != "primary") { return Fail("fragment.color.secondary is not supported"); }
            }
            out = "gl_Color";
            return true;
        }
        if (attribute == "texcoord") {
            int unit = 0;
            if (At("[")) {
                int last = 0;
                if (!IndexRange(unit, last, false)) { return false; }
            }
            if (unit < 0 || unit >= MaxTexCoords) { return Fail("texture coordinate unit out of range"); }
            out = "gl_TexCoord[" + std::to_string(unit) + "]";
            return true;
        }
        return Fail("fragment." + attribute + " is not supported");
    }

    // result.* after "result"; returns the epilogue-copied local.
    bool ResultBinding(std::string &out, bool &colorOrDepth) {
        colorOrDepth = false;
        if (!Expect(".")) { return false; }
        std::string target;
        if (!ExpectIdentifier(target)) { return false; }
        if (vertex()) {
            if (target == "position") {
                if (positionInvariant) { return Fail("result.position cannot be written with ARB_position_invariant"); }
                out = "arbOutPosition";
            } else if (target == "color") {
                std::string face = "front", which = "primary";
                // A following ".xyz" is the write mask, not a color qualifier.
                while (AtQualifier({"front", "back", "primary", "secondary"})) {
                    const std::string part = tokens[pos + 1].text;
                    pos += 2;
                    if (part == "front" || part == "back") { face = part; } else { which = part; }
                }
                // Secondary colors only feed fixed-function color sum, which the
                // engine never enables, and fragment.color.secondary is rejected.
                out = which == "secondary" ? "arbOutUnused" : face == "back" ? "arbOutBackColor" : "arbOutColor";
            } else if (target == "texcoord") {
                int unit = 0;
                if (At("[")) {
                    int last = 0;
                    if (!IndexRange(unit, last, false)) { return false; }
                }
                if (unit < 0 || unit >= MaxTexCoords) { return Fail("texture coordinate unit out of range"); }
                out = "arbOutTexCoord" + std::to_string(unit);
            } else if (target == "fogcoord" || target == "pointsize") {
                // Only fixed-function fog and point rasterization consume these;
                // neither is enabled for material stages.
                out = "arbOutUnused";
            } else {
                return Fail("unknown vertex result '" + target + "'");
            }
        } else {
            if (target == "color") {
                out = "arbOutColor";
            } else if (target == "depth") {
                out = "arbOutDepth";
            } else {
                return Fail("unknown fragment result '" + target + "'");
            }
            colorOrDepth = true;
        }
        outputs.insert(out);
        return true;
    }

    // ------------------------------------------------------------------
    // Declarations
    // ------------------------------------------------------------------

    bool Declare(const std::string &symbolName, Symbol symbol) {
        if (!symbols.emplace(symbolName, std::move(symbol)).second) { return Fail("'" + symbolName + "' is already declared"); }
        return true;
    }

    // ARB identifiers may contain '$', and GLSL reserves "__"; every authored
    // name is prefixed, so it never collides with a GLSL keyword or built-in.
    static std::string Local(const std::string &symbolName) {
        std::string result = "u_";
        for (char c : symbolName) {
            if (c == '$') { result += "_S"; continue; }
            if (c == '_' && result.back() == '_') { result += 'U'; }
            result += c;
        }
        return result;
    }

    bool TempDeclaration(bool address) {
        do {
            std::string symbolName;
            if (!ExpectIdentifier(symbolName)) { return false; }
            if (address) {
                if (!vertex()) { return Fail("ADDRESS registers are only valid in vertex programs"); }
                if (!Declare(symbolName, {SymbolKind::Address, Local(symbolName)})) { return false; }
                body += "    int " + Local(symbolName) + " = 0;\n";
            } else {
                if (!Declare(symbolName, {SymbolKind::Temp, Local(symbolName)})) { return false; }
                body += "    vec4 " + Local(symbolName) + " = vec4(0.0);\n";
            }
        } while (Accept(","));
        return Expect(";");
    }

    bool AttribDeclaration() {
        std::string symbolName;
        if (!ExpectIdentifier(symbolName) || !Expect("=")) { return false; }
        std::string value;
        if (vertex()) {
            if (!Accept("vertex")) { return Fail("ATTRIB must bind a vertex attribute"); }
            if (!VertexAttribute(value)) { return false; }
        } else {
            if (!Accept("fragment")) { return Fail("ATTRIB must bind a fragment attribute"); }
            if (!FragmentAttribute(value)) { return false; }
        }
        if (!Declare(symbolName, {SymbolKind::Attrib, value})) { return false; }
        return Expect(";");
    }

    bool ParamDeclaration() {
        std::string symbolName;
        if (!ExpectIdentifier(symbolName)) { return false; }
        if (Accept("[")) {
            int declared = -1;
            if (!At("]") && !ExpectInteger(declared)) { return false; }
            if (!Expect("]") || !Expect("=") || !Expect("{")) { return false; }
            std::vector<std::string> values;
            do {
                if (!ParamBinding(values, true)) { return false; }
                if (values.size() > static_cast<size_t>(MaxArrayElements)) { return Fail("PARAM array is too large"); }
            } while (Accept(","));
            if (!Expect("}")) { return false; }
            if (declared >= 0 && static_cast<size_t>(declared) != values.size()) {
                return Fail("PARAM array size does not match its initializer");
            }
            if (values.empty()) { return Fail("empty PARAM array"); }
            const std::string local = Local(symbolName);
            std::string init = "    vec4 " + local + "[" + std::to_string(values.size()) + "];\n";
            for (size_t i = 0; i < values.size(); ++i) {
                init += "    " + local + "[" + std::to_string(i) + "] = " + values[i] + ";\n";
            }
            body += init;
            if (!Declare(symbolName, {SymbolKind::ParamArray, local, static_cast<int>(values.size())})) { return false; }
            return Expect(";");
        }
        if (!Expect("=")) { return false; }
        std::vector<std::string> values;
        if (!ParamBinding(values, false)) { return false; }
        if (values.size() != 1) { return Fail("a PARAM needs exactly one value"); }
        const std::string local = Local(symbolName);
        body += "    vec4 " + local + " = " + values[0] + ";\n";
        if (!Declare(symbolName, {SymbolKind::Param, local})) { return false; }
        return Expect(";");
    }

    bool OutputDeclaration() {
        std::string symbolName;
        if (!ExpectIdentifier(symbolName) || !Expect("=")) { return false; }
        if (!Accept("result")) { return Fail("OUTPUT must bind a result register"); }
        std::string lvalue;
        bool colorOrDepth = false;
        if (!ResultBinding(lvalue, colorOrDepth)) { return false; }
        if (!Declare(symbolName, {SymbolKind::Output, lvalue})) { return false; }
        return Expect(";");
    }

    bool AliasDeclaration() {
        std::string symbolName, target;
        if (!ExpectIdentifier(symbolName) || !Expect("=") || !ExpectIdentifier(target)) { return false; }
        auto found = symbols.find(target);
        if (found == symbols.end()) { return Fail("ALIAS of undeclared '" + target + "'"); }
        Symbol copy = found->second;
        if (!Declare(symbolName, copy)) { return false; }
        return Expect(";");
    }

    bool OptionStatement() {
        std::string option;
        if (!ExpectIdentifier(option)) { return false; }
        if (vertex()) {
            if (option == "ARB_position_invariant") {
                if (outputs.count("arbOutPosition")) { return Fail("ARB_position_invariant after writing result.position"); }
                positionInvariant = true;
            } else {
                return Fail("unsupported vertex program option " + option);
            }
        } else if (option != "ARB_precision_hint_fastest" && option != "ARB_precision_hint_nicest") {
            // ARB_fog_* blends with fixed-function fog state that material
            // stages do not set; NV options need instructions this does not have.
            return Fail("unsupported fragment program option " + option);
        }
        return Expect(";");
    }

    // ------------------------------------------------------------------
    // Operands
    // ------------------------------------------------------------------

    bool Swizzle(Operand &operand) {
        if (!At(".")) { return true; }
        const Token *token = Peek(1);
        if (token == nullptr || token->kind != TokenKind::Identifier) { return true; }
        const std::string &text = token->text;
        if (text.size() != 1 && text.size() != 4) { return Fail("invalid swizzle '." + text + "'"); }
        bool letters = false, xyzw = false;
        for (size_t i = 0; i < text.size(); ++i) {
            const int index = ComponentIndex(text[i], fragment());
            if (index < 0) { return Fail("invalid swizzle '." + text + "'"); }
            if (std::strchr("rgba", text[i]) != nullptr) { letters = true; } else { xyzw = true; }
            operand.swizzle[i] = index;
        }
        if (letters && xyzw) { return Fail("mixed xyzw and rgba swizzle '." + text + "'"); }
        operand.swizzleCount = static_cast<int>(text.size());
        pos += 2;
        return true;
    }

    // An array element: constant index, or (vertex programs) A0.x +/- offset.
    bool ArrayElement(const Symbol &symbol, std::string &out) {
        if (!Expect("[")) { return false; }
        const Token *token = Peek();
        if (token != nullptr && token->kind == TokenKind::Number) {
            int index = 0;
            if (!ExpectInteger(index)) { return false; }
            if (index < 0 || index >= symbol.size) { return Fail("PARAM array index out of range"); }
            out = symbol.glsl + "[" + std::to_string(index) + "]";
            return Expect("]");
        }
        std::string addressName;
        if (!ExpectIdentifier(addressName)) { return false; }
        auto found = symbols.find(addressName);
        if (found == symbols.end() || found->second.kind != SymbolKind::Address) {
            return Fail("relative addressing needs an ADDRESS register");
        }
        if (!Expect(".")) { return false; }
        std::string component;
        if (!ExpectIdentifier(component)) { return false; }
        if (component != "x") { return Fail("ADDRESS registers only have an x component"); }
        int offset = 0;
        if (Accept("+")) {
            if (!ExpectInteger(offset)) { return false; }
        } else if (Accept("-")) {
            if (!ExpectInteger(offset)) { return false; }
            offset = -offset;
        }
        if (!Expect("]")) { return false; }
        // Out-of-range relative reads are undefined in ARB; clamp so the GLSL
        // never indexes outside the array.
        out = symbol.glsl + "[int(clamp(float(" + found->second.glsl + " + (" + std::to_string(offset) + ")), 0.0, " +
            std::to_string(symbol.size - 1) + ".0))]";
        return true;
    }

    bool SourceOperand(Operand &operand) {
        operand = Operand();
        if (Accept("-")) { operand.negate = true; } else { Accept("+"); }
        const Token *token = Peek();
        if (token == nullptr) { return Fail("missing source operand"); }
        if (token->text == "{" || token->kind == TokenKind::Number) {
            std::vector<std::string> values;
            if (!ParamBinding(values, false)) { return false; }
            operand.base = values[0];
            return Swizzle(operand);
        }
        if (token->kind != TokenKind::Identifier) { return Fail("invalid source operand '" + token->text + "'"); }
        if (token->text == "vertex") {
            if (!vertex()) { return Fail("vertex attributes are only valid in vertex programs"); }
            ++pos;
            if (!VertexAttribute(operand.base)) { return false; }
            return Swizzle(operand);
        }
        if (token->text == "fragment") {
            if (!fragment()) { return Fail("fragment attributes are only valid in fragment programs"); }
            ++pos;
            if (!FragmentAttribute(operand.base)) { return false; }
            return Swizzle(operand);
        }
        if (token->text == "program" || token->text == "state") {
            std::vector<std::string> values;
            if (!ParamBinding(values, false)) { return false; }
            operand.base = values[0];
            return Swizzle(operand);
        }
        if (token->text == "result") { return Fail("result registers cannot be read"); }
        auto found = symbols.find(token->text);
        if (found == symbols.end()) { return Fail("undeclared identifier '" + token->text + "'"); }
        ++pos;
        const Symbol &symbol = found->second;
        switch (symbol.kind) {
        case SymbolKind::Temp:
        case SymbolKind::Attrib:
        case SymbolKind::Param:
            operand.base = symbol.glsl;
            break;
        case SymbolKind::ParamArray:
            if (!ArrayElement(symbol, operand.base)) { return false; }
            break;
        case SymbolKind::Output:
            return Fail("output '" + token->text + "' cannot be read");
        case SymbolKind::Address:
            return Fail("ADDRESS register '" + token->text + "' is not a value");
        }
        return Swizzle(operand);
    }

    std::string Vector(const Operand &operand) const {
        std::string value = operand.base;
        if (operand.swizzleCount == 1) {
            value = "vec4((" + value + ")." + Components[operand.swizzle[0]] + ")";
        } else if (operand.swizzleCount == 4) {
            std::string swizzle;
            for (int i = 0; i < 4; ++i) { swizzle += Components[operand.swizzle[i]]; }
            if (swizzle != "xyzw") { value = "(" + value + ")." + swizzle; }
        }
        return operand.negate ? "(-" + value + ")" : value;
    }

    // Scalar instructions read one component. A source without a scalar
    // selector reads its first (swizzled) component, as permissive drivers do.
    std::string Scalar(const Operand &operand) const {
        const int component = operand.swizzleCount == 0 ? 0 : operand.swizzle[0];
        const std::string value = "(" + operand.base + ")." + Components[component];
        return operand.negate ? "(-" + value + ")" : value;
    }

    bool DestinationOperand(Destination &destination) {
        destination = Destination();
        const Token *token = Peek();
        if (token == nullptr || token->kind != TokenKind::Identifier) { return Fail("missing destination"); }
        if (token->text == "result") {
            ++pos;
            if (!ResultBinding(destination.lvalue, destination.resultColorOrDepth)) { return false; }
        } else {
            auto found = symbols.find(token->text);
            if (found == symbols.end()) { return Fail("undeclared destination '" + token->text + "'"); }
            ++pos;
            const Symbol &symbol = found->second;
            if (symbol.kind == SymbolKind::Temp || symbol.kind == SymbolKind::Output) {
                destination.lvalue = symbol.glsl;
            } else if (symbol.kind == SymbolKind::Address) {
                destination.lvalue = symbol.glsl;
                destination.address = true;
            } else {
                return Fail("'" + token->text + "' cannot be written");
            }
        }
        if (At(".") && Peek(1) != nullptr && Peek(1)->kind == TokenKind::Identifier) {
            const std::string text = Peek(1)->text;
            int mask = 0, previous = -1;
            bool letters = false, xyzw = false;
            for (char c : text) {
                const int index = ComponentIndex(c, fragment());
                if (index < 0 || index <= previous) { return Fail("invalid write mask '." + text + "'"); }
                if (std::strchr("rgba", c) != nullptr) { letters = true; } else { xyzw = true; }
                previous = index;
                mask |= 1 << index;
            }
            if (letters && xyzw) { return Fail("mixed xyzw and rgba write mask '." + text + "'"); }
            destination.mask = mask;
            pos += 2;
        }
        if (destination.address && destination.mask == 0xF) { destination.mask = 1; }
        if (destination.address && destination.mask != 1) { return Fail("ADDRESS writes must use the x mask"); }
        return true;
    }

    void Assign(const Destination &destination, const std::string &value, bool saturate) {
        std::string expression = saturate ? "clamp(" + value + ", 0.0, 1.0)" : value;
        if (destination.mask == 0xF) {
            body += "    " + destination.lvalue + " = " + expression + ";\n";
            return;
        }
        std::string mask;
        for (int i = 0; i < 4; ++i) { if (destination.mask & (1 << i)) { mask += Components[i]; } }
        body += "    " + destination.lvalue + "." + mask + " = (" + expression + ")." + mask + ";\n";
    }

    // ------------------------------------------------------------------
    // Instructions
    // ------------------------------------------------------------------

    bool TextureTarget(int &unit, bool &cube) {
        std::string word;
        if (!ExpectIdentifier(word)) { return false; }
        if (word != "texture") { return Fail("expected a texture image unit"); }
        unit = 0;
        if (At("[")) {
            int last = 0;
            if (!IndexRange(unit, last, false)) { return false; }
        }
        if (unit < 0 || unit >= MaxTextureUnits) { return Fail("texture image unit " + std::to_string(unit) + " is out of range"); }
        if (!Expect(",")) { return false; }
        std::string target;
        const Token *token = Peek();
        if (token == nullptr) { return Fail("missing texture target"); }
        target = token->text;
        ++pos;
        // "2D" tokenizes as the number 2 followed by the identifier D.
        if (token->kind == TokenKind::Number && At("D")) {
            target += "D";
            ++pos;
        }
        if (target == "2D") { cube = false; }
        else if (target == "CUBE") { cube = true; }
        else { return Fail("texture target " + target + " is not supported; 2D and CUBE are"); }
        const unsigned bit = 1u << unit;
        if ((textureMask & bit) && ((cubeMask & bit) != 0) != cube) {
            return Fail("texture[" + std::to_string(unit) + "] is used with two different targets");
        }
        textureMask |= bit;
        if (cube) { cubeMask |= bit; }
        return true;
    }

    bool Instruction(const std::string &mnemonic) {
        std::string opcode = mnemonic;
        bool saturate = false;
        if (opcode.size() > 4 && opcode.compare(opcode.size() - 4, 4, "_SAT") == 0) {
            if (!fragment()) { return Fail("_SAT is only valid in fragment programs"); }
            opcode = opcode.substr(0, opcode.size() - 4);
            saturate = true;
        }
        struct Shape { const char *name; int sources; bool vp; bool fp; };
        static const Shape shapes[] = {
            {"ABS", 1, true, true}, {"ADD", 2, true, true}, {"ARL", 1, true, false}, {"CMP", 3, false, true},
            {"COS", 1, false, true}, {"DP3", 2, true, true}, {"DP4", 2, true, true}, {"DPH", 2, true, true},
            {"DST", 2, true, true}, {"EX2", 1, true, true}, {"EXP", 1, true, false}, {"FLR", 1, true, true},
            {"FRC", 1, true, true}, {"KIL", 0, false, true}, {"LG2", 1, true, true}, {"LIT", 1, true, true},
            {"LOG", 1, true, false}, {"LRP", 3, false, true}, {"MAD", 3, true, true}, {"MAX", 2, true, true},
            {"MIN", 2, true, true}, {"MOV", 1, true, true}, {"MUL", 2, true, true}, {"POW", 2, true, true},
            {"RCP", 1, true, true}, {"RSQ", 1, true, true}, {"SCS", 1, false, true}, {"SGE", 2, true, true},
            {"SIN", 1, false, true}, {"SLT", 2, true, true}, {"SUB", 2, true, true}, {"SWZ", -1, true, true},
            {"TEX", -2, false, true}, {"TXB", -2, false, true}, {"TXP", -2, false, true}, {"XPD", 2, true, true},
        };
        const Shape *shape = nullptr;
        for (const Shape &candidate : shapes) { if (opcode == candidate.name) { shape = &candidate; break; } }
        if (shape == nullptr) { return Fail("unknown instruction " + mnemonic); }
        if ((vertex() && !shape->vp) || (fragment() && !shape->fp)) {
            return Fail(opcode + " is not valid in " + (vertex() ? "vertex" : "fragment") + " programs");
        }

        if (opcode == "KIL") {
            Operand source;
            if (!SourceOperand(source)) { return false; }
            body += "    if (any(lessThan(" + Vector(source) + ", vec4(0.0)))) discard;\n";
            return Expect(";");
        }

        Destination destination;
        if (!DestinationOperand(destination)) { return false; }
        if (destination.address != (opcode == "ARL")) {
            return Fail(destination.address ? "only ARL can write an ADDRESS register" : "ARL must write an ADDRESS register");
        }
        if (!Expect(",")) { return false; }

        if (opcode == "SWZ") {
            bool negateAll = false;
            if (Accept("-")) { negateAll = true; } else { Accept("+"); }
            Operand source;
            if (!SourceOperand(source)) { return false; }
            if (source.negate || source.swizzleCount != 0) { return Fail("SWZ takes an unswizzled source"); }
            std::string components[4];
            for (int i = 0; i < 4; ++i) {
                if (!Expect(",")) { return false; }
                bool negate = negateAll;
                if (Accept("-")) { negate = !negate; } else { Accept("+"); }
                const Token *token = Peek();
                if (token == nullptr) { return Fail("missing SWZ component"); }
                std::string value;
                if (token->text == "0" || token->text == "1") {
                    value = token->text == "0" ? "0.0" : "1.0";
                } else if (token->kind == TokenKind::Identifier && token->text.size() == 1 &&
                           ComponentIndex(token->text[0], fragment()) >= 0) {
                    value = "(" + source.base + ")." + Components[ComponentIndex(token->text[0], fragment())];
                } else {
                    return Fail("invalid SWZ component '" + token->text + "'");
                }
                ++pos;
                components[i] = negate ? "(-" + value + ")" : value;
            }
            Assign(destination, "vec4(" + components[0] + ", " + components[1] + ", " + components[2] + ", " + components[3] + ")", saturate);
            return Expect(";");
        }

        if (shape->sources == -2) {
            Operand coordinate;
            if (!SourceOperand(coordinate) || !Expect(",")) { return false; }
            int unit = 0;
            bool cube = false;
            if (!TextureTarget(unit, cube)) { return false; }
            const std::string sampler = "arbTexture" + std::to_string(unit);
            const std::string c = Vector(coordinate);
            std::string value;
            if (opcode == "TEX") {
                value = cube ? "textureCube(" + sampler + ", (" + c + ").xyz)" : "texture2D(" + sampler + ", (" + c + ").xy)";
            } else if (opcode == "TXB") {
                value = cube ? "textureCube(" + sampler + ", (" + c + ").xyz, (" + c + ").w)"
                             : "texture2D(" + sampler + ", (" + c + ").xy, (" + c + ").w)";
            } else {
                value = cube ? "textureCube(" + sampler + ", (" + c + ").xyz / (" + c + ").w)"
                             : "texture2DProj(" + sampler + ", " + c + ")";
            }
            Assign(destination, value, saturate);
            return Expect(";");
        }

        Operand a, b, c;
        if (!SourceOperand(a)) { return false; }
        if (shape->sources >= 2 && (!Expect(",") || !SourceOperand(b))) { return false; }
        if (shape->sources >= 3 && (!Expect(",") || !SourceOperand(c))) { return false; }
        const std::string A = Vector(a), B = Vector(b), C = Vector(c);
        std::string value;
        if (opcode == "ABS") { value = "abs(" + A + ")"; }
        else if (opcode == "ADD") { value = "(" + A + " + " + B + ")"; }
        else if (opcode == "ARL") {
            body += "    " + destination.lvalue + " = int(floor(" + Scalar(a) + "));\n";
            return Expect(";");
        }
        else if (opcode == "CMP") { Helper("cmp"); value = "arbCmp(" + A + ", " + B + ", " + C + ")"; }
        else if (opcode == "COS") { value = "vec4(cos(" + Scalar(a) + "))"; }
        else if (opcode == "DP3") { value = "vec4(dot((" + A + ").xyz, (" + B + ").xyz))"; }
        else if (opcode == "DP4") { value = "vec4(dot(" + A + ", " + B + "))"; }
        else if (opcode == "DPH") { value = "vec4(dot((" + A + ").xyz, (" + B + ").xyz) + (" + B + ").w)"; }
        else if (opcode == "DST") { value = "vec4(1.0, (" + A + ").y * (" + B + ").y, (" + A + ").z, (" + B + ").w)"; }
        else if (opcode == "EX2") { value = "vec4(exp2(" + Scalar(a) + "))"; }
        else if (opcode == "EXP") { Helper("exp"); value = "arbExp(" + Scalar(a) + ")"; }
        else if (opcode == "FLR") { value = "floor(" + A + ")"; }
        else if (opcode == "FRC") { value = "fract(" + A + ")"; }
        else if (opcode == "LG2") { value = "vec4(log2(abs(" + Scalar(a) + ")))"; }
        else if (opcode == "LIT") { Helper("lit"); value = "arbLit(" + A + ")"; }
        else if (opcode == "LOG") { Helper("log"); value = "arbLog(" + Scalar(a) + ")"; }
        else if (opcode == "LRP") { value = "(" + A + " * " + B + " + (vec4(1.0) - " + A + ") * " + C + ")"; }
        else if (opcode == "MAD") { value = "(" + A + " * " + B + " + " + C + ")"; }
        else if (opcode == "MAX") { value = "max(" + A + ", " + B + ")"; }
        else if (opcode == "MIN") { value = "min(" + A + ", " + B + ")"; }
        else if (opcode == "MOV") { value = A; }
        else if (opcode == "MUL") { value = "(" + A + " * " + B + ")"; }
        else if (opcode == "POW") { Helper("pow"); value = "vec4(arbPow(" + Scalar(a) + ", " + Scalar(b) + "))"; }
        else if (opcode == "RCP") { value = "vec4(1.0 / " + Scalar(a) + ")"; }
        else if (opcode == "RSQ") { value = "vec4(inversesqrt(abs(" + Scalar(a) + ")))"; }
        else if (opcode == "SCS") { value = "vec4(cos(" + Scalar(a) + "), sin(" + Scalar(a) + "), 0.0, 0.0)"; }
        else if (opcode == "SGE") { value = "vec4(greaterThanEqual(" + A + ", " + B + "))"; }
        else if (opcode == "SIN") { value = "vec4(sin(" + Scalar(a) + "))"; }
        else if (opcode == "SLT") { value = "vec4(lessThan(" + A + ", " + B + "))"; }
        else if (opcode == "SUB") { value = "(" + A + " - " + B + ")"; }
        else if (opcode == "XPD") { value = "vec4(cross((" + A + ").xyz, (" + B + ").xyz), 0.0)"; }
        else { return Fail("unhandled instruction " + opcode); }
        Assign(destination, value, saturate);
        return Expect(";");
    }

    bool Parse(const std::string &section) {
        const std::string header = vertex() ? "!!ARBvp1.0" : "!!ARBfp1.0";
        if (section.compare(0, header.size(), header) != 0) {
            error = name + ": only " + header + " programs are supported";
            return false;
        }
        if (!Tokenize(section, header.size(), tokens, error)) {
            error = name + ": " + error;
            return false;
        }
        if (tokens.empty() || tokens.back().text != "END") {
            error = name + ": program does not end with END";
            return false;
        }
        tokens.pop_back();
        while (pos < tokens.size()) {
            const Token &token = tokens[pos];
            if (token.kind != TokenKind::Identifier) { return Fail("expected a statement, found '" + token.text + "'"); }
            ++pos;
            bool ok;
            if (token.text == "OPTION") { ok = OptionStatement(); }
            else if (token.text == "TEMP") { ok = TempDeclaration(false); }
            else if (token.text == "ADDRESS") { ok = TempDeclaration(true); }
            else if (token.text == "ATTRIB") { ok = AttribDeclaration(); }
            else if (token.text == "PARAM") { ok = ParamDeclaration(); }
            else if (token.text == "OUTPUT") { ok = OutputDeclaration(); }
            else if (token.text == "ALIAS") { ok = AliasDeclaration(); }
            else { ok = Instruction(token.text); }
            if (!ok) { return false; }
        }
        return true;
    }

    std::string Helpers() const {
        std::string result;
        if (helpers.count("cmp")) {
            result += "vec4 arbCmp(vec4 a, vec4 b, vec4 c) {\n"
                      "    return vec4(a.x < 0.0 ? b.x : c.x, a.y < 0.0 ? b.y : c.y, a.z < 0.0 ? b.z : c.z, a.w < 0.0 ? b.w : c.w);\n}\n";
        }
        if (helpers.count("pow")) {
            result += "float arbPow(float a, float b) { return b == 0.0 ? 1.0 : pow(abs(a), b); }\n";
        }
        if (helpers.count("lit")) {
            result += "vec4 arbLit(vec4 s) {\n"
                      "    float y = max(s.y, 0.0);\n"
                      "    float w = clamp(s.w, -128.0, 128.0);\n"
                      "    float specular = s.x > 0.0 ? (y > 0.0 ? pow(y, w) : (w == 0.0 ? 1.0 : 0.0)) : 0.0;\n"
                      "    return vec4(1.0, max(s.x, 0.0), specular, 1.0);\n}\n";
        }
        if (helpers.count("exp")) {
            result += "vec4 arbExp(float s) { float f = floor(s); return vec4(exp2(f), s - f, exp2(s), 1.0); }\n";
        }
        if (helpers.count("log")) {
            result += "vec4 arbLog(float s) {\n"
                      "    float a = abs(s);\n"
                      "    float e = floor(log2(a));\n"
                      "    return vec4(e, a / exp2(e), log2(a), 1.0);\n}\n";
        }
        if (helpers.count("inverse")) {
            result += R"(mat4 arbInverse(mat4 m) {
    float a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3];
    float a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3];
    float a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3];
    float a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3];
    float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    float r = det != 0.0 ? 1.0 / det : 0.0;
    return mat4(
        vec4(a11 * b11 - a12 * b10 + a13 * b09, a02 * b10 - a01 * b11 - a03 * b09,
             a31 * b05 - a32 * b04 + a33 * b03, a22 * b04 - a21 * b05 - a23 * b03) * r,
        vec4(a12 * b08 - a10 * b11 - a13 * b07, a00 * b11 - a02 * b08 + a03 * b07,
             a32 * b02 - a30 * b05 - a33 * b01, a20 * b05 - a22 * b02 + a23 * b01) * r,
        vec4(a10 * b10 - a11 * b08 + a13 * b06, a01 * b08 - a00 * b10 - a03 * b06,
             a30 * b04 - a31 * b02 + a33 * b00, a21 * b02 - a20 * b04 - a23 * b00) * r,
        vec4(a11 * b07 - a10 * b09 - a12 * b06, a00 * b09 - a01 * b07 + a02 * b06,
             a31 * b01 - a30 * b03 - a32 * b00, a20 * b03 - a21 * b01 + a22 * b00) * r);
}
)";
        }
        return result;
    }
};

std::string Uniforms(const std::vector<ARBParameter> &parameters, bool vertex) {
    std::string result;
    for (const ARBParameter &parameter : parameters) {
        const bool vertexSpace = parameter.space == ARBParameterSpace::VertexLocal || parameter.space == ARBParameterSpace::VertexEnv;
        if (vertexSpace == vertex) { result += "uniform vec4 " + parameter.name + ";\n"; }
    }
    return result;
}

std::string VertexShader(const Translator *vertex, const std::vector<ARBParameter> &parameters,
                         unsigned fragmentTexCoords) {
    std::string source = "#version 120\n";
    source += Uniforms(parameters, true);
    if (vertex == nullptr) {
        // Fixed-function vertex processing as the material stage leaves it:
        // identity texture matrices and only unit 0's coordinate array.
        source += "void main() {\n    gl_Position = ftransform();\n    gl_FrontColor = gl_Color;\n    gl_BackColor = gl_Color;\n";
        for (int unit = 0; unit < MaxTexCoords; ++unit) {
            if (fragmentTexCoords & (1u << unit)) {
                source += "    gl_TexCoord[" + std::to_string(unit) + "] = " +
                    (unit == 0 ? std::string("gl_TextureMatrix[0] * gl_MultiTexCoord0") : std::string("vec4(0.0, 0.0, 0.0, 1.0)")) + ";\n";
            }
        }
        return source + "}\n";
    }
    if (vertex->usesTangent) { source += "attribute vec3 attr_Tangent;\n"; }
    if (vertex->usesBitangent) { source += "attribute vec3 attr_Bitangent;\n"; }
    source += vertex->Helpers();
    source += "void main() {\n";
    source += "    vec4 arbOutPosition = vec4(0.0, 0.0, 0.0, 1.0);\n";
    source += "    vec4 arbOutColor = vec4(0.0, 0.0, 0.0, 1.0);\n";
    source += "    vec4 arbOutBackColor = vec4(0.0, 0.0, 0.0, 1.0);\n";
    source += "    vec4 arbOutUnused = vec4(0.0);\n";
    for (int unit = 0; unit < MaxTexCoords; ++unit) {
        source += "    vec4 arbOutTexCoord" + std::to_string(unit) + " = vec4(0.0, 0.0, 0.0, 1.0);\n";
    }
    source += vertex->body;
    source += vertex->positionInvariant ? "    gl_Position = ftransform();\n" : "    gl_Position = arbOutPosition;\n";
    if (vertex->outputs.count("arbOutColor")) { source += "    gl_FrontColor = arbOutColor;\n"; }
    if (vertex->outputs.count("arbOutBackColor")) { source += "    gl_BackColor = arbOutBackColor;\n"; }
    else if (vertex->outputs.count("arbOutColor")) {
        // Two-sided vertex color is disabled, so the front color is used for
        // every face.
        source += "    gl_BackColor = arbOutColor;\n";
    }
    for (int unit = 0; unit < MaxTexCoords; ++unit) {
        const std::string output = "arbOutTexCoord" + std::to_string(unit);
        if (vertex->outputs.count(output)) { source += "    gl_TexCoord[" + std::to_string(unit) + "] = " + output + ";\n"; }
    }
    return source + "}\n";
}

std::string FragmentShader(const Translator *fragment, const std::vector<ARBParameter> &parameters) {
    std::string source = "#version 120\n";
    if (fragment == nullptr) {
        // Without a fragment program the material path binds no textures, so
        // the interpolated color is the only defined result.
        return source + "void main() {\n    gl_FragColor = gl_Color;\n}\n";
    }
    for (int unit = 0; unit < MaxTextureUnits; ++unit) {
        if (fragment->textureMask & (1u << unit)) {
            source += std::string("uniform ") + ((fragment->cubeMask & (1u << unit)) ? "samplerCube" : "sampler2D") +
                " arbTexture" + std::to_string(unit) + ";\n";
        }
    }
    source += Uniforms(parameters, false);
    source += fragment->Helpers();
    source += "void main() {\n";
    // Unwritten result components are undefined in ARB. Opaque black matches
    // the native implementations of the stock programs that write only RGB.
    source += "    vec4 arbOutColor = vec4(0.0, 0.0, 0.0, 1.0);\n";
    if (fragment->outputs.count("arbOutDepth")) { source += "    vec4 arbOutDepth = vec4(gl_FragCoord.z);\n"; }
    source += fragment->body;
    source += "    gl_FragColor = arbOutColor;\n";
    if (fragment->outputs.count("arbOutDepth")) { source += "    gl_FragDepth = arbOutDepth.z;\n"; }
    return source + "}\n";
}

// Texture coordinate sets a fragment program reads; a fixed-function vertex
// stage only needs to produce those.
unsigned ReadTexCoords(const std::string &body) {
    unsigned mask = 0;
    for (int unit = 0; unit < MaxTexCoords; ++unit) {
        if (body.find("gl_TexCoord[" + std::to_string(unit) + "]") != std::string::npos) { mask |= 1u << unit; }
    }
    return mask;
}

} // namespace

bool ARBVertexEnvironmentDefined(int index) {
    // RB_SetProgramEnvironment: 0 current-render scale, 1 global eye.
    // RB_SetProgramEnvironmentSpace: 5 local eye, 6-8 model matrix rows.
    return index == 0 || index == 1 || (index >= 5 && index <= 8);
}

bool ARBFragmentEnvironmentDefined(int index) {
    // 0 current-render scale, 1 window-to-texture scale.
    return index == 0 || index == 1;
}

bool TranslateARB(const ARBTranslateRequest &request, ARBTranslation &result) {
    result = ARBTranslation();
    if (request.vertexSource.empty() && request.fragmentSource.empty()) {
        result.diagnostic = "no ARB program source";
        return false;
    }
    for (const std::string *source : {&request.vertexSource, &request.fragmentSource}) {
        if (source->size() > MaxSourceBytes || source->find('\0') != std::string::npos) {
            result.diagnostic = "oversized or embedded-NUL ARB program source";
            return false;
        }
    }
    std::map<std::pair<int, int>, std::string> parameterNames;
    Translator vertex{Program::Vertex, request.vertexName};
    Translator fragment{Program::Fragment, request.fragmentName};
    for (Translator *translator : {&vertex, &fragment}) {
        translator->parameters = &result.parameters;
        translator->parameterNames = &parameterNames;
    }
    const bool hasVertex = !request.vertexSource.empty();
    const bool hasFragment = !request.fragmentSource.empty();
    std::string section;
    if (hasVertex) {
        if (!SelectSection(request.vertexSource, true, section, result.diagnostic)) {
            result.diagnostic = request.vertexName + ": " + result.diagnostic;
            return false;
        }
        if (!vertex.Parse(section)) { result.diagnostic = vertex.error; result.parameters.clear(); return false; }
    }
    if (hasFragment) {
        if (!SelectSection(request.fragmentSource, false, section, result.diagnostic)) {
            result.diagnostic = request.fragmentName + ": " + result.diagnostic;
            result.parameters.clear();
            return false;
        }
        if (!fragment.Parse(section)) { result.diagnostic = fragment.error; result.parameters.clear(); return false; }
    }
    result.positionInvariant = hasVertex ? vertex.positionInvariant : true;
    result.textureMask = hasFragment ? fragment.textureMask : 0;
    result.cubeTextureMask = hasFragment ? fragment.cubeMask : 0;

    CompileRequest &compile = result.request;
    compile.vertexName = hasVertex ? request.vertexName : request.fragmentName + " (fixed-function vertex)";
    compile.fragmentName = hasFragment ? request.fragmentName : request.vertexName + " (fixed-function fragment)";
    compile.vertexSource = VertexShader(hasVertex ? &vertex : nullptr, result.parameters,
                                        hasFragment ? ReadTexCoords(fragment.body) : 0);
    compile.fragmentSource = FragmentShader(hasFragment ? &fragment : nullptr, result.parameters);
    for (size_t i = 0; i < result.parameters.size(); ++i) {
        compile.parameters.push_back({result.parameters[i].name, static_cast<int>(i), 4});
    }
    for (int unit = 0; unit < MaxTextureUnits; ++unit) {
        if (result.textureMask & (1u << unit)) { compile.textures.push_back({"arbTexture" + std::to_string(unit), unit}); }
    }
    return true;
}

} // namespace oq4material

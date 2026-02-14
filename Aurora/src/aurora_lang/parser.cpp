#include "aurora/lang/parser.hpp"

#include <cctype>
#include <cmath>
#include <regex>
#include <stdexcept>
#include <utility>

namespace aurora::lang {
namespace {

enum class TokenKind { kIdentifier, kNumber, kString, kSymbol, kEnd };

struct Token {
  TokenKind kind = TokenKind::kEnd;
  std::string text;
  int line = 1;
  int column = 1;
};

class ParseException final : public std::runtime_error {
 public:
  ParseException(int line, int column, const std::string& message)
      : std::runtime_error(message), line_(line), column_(column) {}

  [[nodiscard]] int line() const { return line_; }
  [[nodiscard]] int column() const { return column_; }

 private:
  int line_ = 1;
  int column_ = 1;
};

bool IsIdentifierStart(char ch) { return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_'; }

bool IsIdentifierPart(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '#' || ch == '+' || ch == '-' ||
         ch == '$';
}

bool IsNumericStart(char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '+' || ch == '-'; }

class Lexer {
 public:
  explicit Lexer(const std::string& source) : source_(source) {}

  std::vector<Token> Tokenize() {
    std::vector<Token> out;
    while (!AtEnd()) {
      SkipTrivia();
      if (AtEnd()) {
        break;
      }

      const int line = line_;
      const int column = column_;
      const char ch = Peek();
      if (ch == '"' || ch == '\'') {
        out.push_back(Token{TokenKind::kString, ReadString(ch), line, column});
        continue;
      }
      if (IsIdentifierStart(ch)) {
        out.push_back(Token{TokenKind::kIdentifier, ReadIdentifier(), line, column});
        continue;
      }
      if (IsNumericStart(ch) && MightBeNumber()) {
        out.push_back(Token{TokenKind::kNumber, ReadNumberWithUnit(), line, column});
        continue;
      }
      if (IsSymbol(ch)) {
        out.push_back(Token{TokenKind::kSymbol, std::string(1, ch), line, column});
        Advance();
        continue;
      }
      throw ParseException(line, column, "Unexpected character: '" + std::string(1, ch) + "'");
    }
    out.push_back(Token{TokenKind::kEnd, "", line_, column_});
    return out;
  }

 private:
  bool AtEnd() const { return index_ >= source_.size(); }

  char Peek(size_t lookahead = 0) const {
    if (index_ + lookahead >= source_.size()) {
      return '\0';
    }
    return source_[index_ + lookahead];
  }

  void Advance() {
    if (AtEnd()) {
      return;
    }
    if (source_[index_] == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
    ++index_;
  }

  void SkipTrivia() {
    while (!AtEnd()) {
      if (std::isspace(static_cast<unsigned char>(Peek())) != 0) {
        Advance();
        continue;
      }
      if (Peek() == '/' && Peek(1) == '/') {
        while (!AtEnd() && Peek() != '\n') {
          Advance();
        }
        continue;
      }
      if (Peek() == '/' && Peek(1) == '*') {
        Advance();
        Advance();
        while (!AtEnd() && !(Peek() == '*' && Peek(1) == '/')) {
          Advance();
        }
        if (AtEnd()) {
          throw ParseException(line_, column_, "Unterminated block comment");
        }
        Advance();
        Advance();
        continue;
      }
      break;
    }
  }

  bool MightBeNumber() const {
    if (std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
      return true;
    }
    if ((Peek() == '+' || Peek() == '-') && std::isdigit(static_cast<unsigned char>(Peek(1))) != 0) {
      return true;
    }
    return false;
  }

  std::string ReadIdentifier() {
    std::string out;
    out.push_back(Peek());
    Advance();
    while (!AtEnd() && IsIdentifierPart(Peek())) {
      out.push_back(Peek());
      Advance();
    }
    return out;
  }

  std::string ReadNumberWithUnit() {
    std::string out;
    if (Peek() == '+' || Peek() == '-') {
      out.push_back(Peek());
      Advance();
    }
    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
      out.push_back(Peek());
      Advance();
    }
    if (!AtEnd() && Peek() == '.') {
      out.push_back(Peek());
      Advance();
      while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
        out.push_back(Peek());
        Advance();
      }
    }
    if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
      out.push_back(Peek());
      Advance();
      if (Peek() == '+' || Peek() == '-') {
        out.push_back(Peek());
        Advance();
      }
      while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())) != 0) {
        out.push_back(Peek());
        Advance();
      }
    }
    while (!AtEnd() && std::isalpha(static_cast<unsigned char>(Peek())) != 0) {
      out.push_back(Peek());
      Advance();
    }
    return out;
  }

  std::string ReadString(char quote) {
    Advance();
    std::string out;
    while (!AtEnd() && Peek() != quote) {
      if (Peek() == '\\') {
        Advance();
        if (AtEnd()) {
          throw ParseException(line_, column_, "Unterminated string escape");
        }
        const char esc = Peek();
        switch (esc) {
          case 'n':
            out.push_back('\n');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case '\\':
            out.push_back('\\');
            break;
          case '"':
            out.push_back('"');
            break;
          case '\'':
            out.push_back('\'');
            break;
          default:
            out.push_back(esc);
            break;
        }
        Advance();
        continue;
      }
      out.push_back(Peek());
      Advance();
    }
    if (AtEnd()) {
      throw ParseException(line_, column_, "Unterminated string literal");
    }
    Advance();
    return out;
  }

  bool IsSymbol(char ch) const {
    switch (ch) {
      case '{':
      case '}':
      case '[':
      case ']':
      case '(':
      case ')':
      case ':':
      case ',':
      case '.':
      case '|':
      case '=':
        return true;
      default:
        return false;
    }
  }

  const std::string& source_;
  size_t index_ = 0;
  int line_ = 1;
  int column_ = 1;
};

struct NumberUnitParse {
  bool ok = false;
  double value = 0.0;
  std::string unit;
};

NumberUnitParse ParseNumberUnitToken(const std::string& text) {
  static const std::regex kPattern(R"(^([+-]?(?:(?:\d+\.\d*)|(?:\d+)|(?:\.\d+))(?:[eE][+-]?\d+)?)([A-Za-z]+)?$)");
  std::smatch match;
  if (!std::regex_match(text, match, kPattern)) {
    return {};
  }
  NumberUnitParse out;
  out.ok = true;
  out.value = std::stod(match[1].str());
  if (match.size() > 2) {
    out.unit = match[2].str();
  }
  return out;
}

std::string ValueAsString(const ParamValue& value) {
  switch (value.kind) {
    case ParamValue::Kind::kString:
    case ParamValue::Kind::kIdentifier:
      return value.string_value;
    case ParamValue::Kind::kNumber:
      return std::to_string(value.number_value);
    case ParamValue::Kind::kUnitNumber:
      return std::to_string(value.unit_number_value.value) + value.unit_number_value.unit;
    default:
      return value.DebugString();
  }
}

double ValueAsNumber(const ParamValue& value, double fallback = 0.0) {
  if (value.kind == ParamValue::Kind::kNumber) {
    return value.number_value;
  }
  if (value.kind == ParamValue::Kind::kUnitNumber) {
    return value.unit_number_value.value;
  }
  return fallback;
}

UnitNumber ValueAsUnitNumber(const ParamValue& value, const std::string& default_unit = "s") {
  if (value.kind == ParamValue::Kind::kUnitNumber) {
    return value.unit_number_value;
  }
  if (value.kind == ParamValue::Kind::kNumber) {
    return UnitNumber{value.number_value, default_unit};
  }
  throw std::runtime_error("Expected numeric time literal, got " + value.DebugString());
}

std::map<std::string, ParamValue> ValueAsObject(const ParamValue& value) {
  if (value.kind != ParamValue::Kind::kObject) {
    throw std::runtime_error("Expected object, got " + value.DebugString());
  }
  return value.object_values;
}

std::vector<ParamValue> ValueAsList(const ParamValue& value) {
  if (value.kind != ParamValue::Kind::kList) {
    throw std::runtime_error("Expected list, got " + value.DebugString());
  }
  return value.list_values;
}

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  AuroraFile Parse() {
    AuroraFile file;
    while (!AtEnd()) {
      if (MatchIdentifier("aurora")) {
        ParseAuroraHeader(file);
      } else if (MatchIdentifier("assets")) {
        ParseAssets(file);
      } else if (MatchIdentifier("outputs")) {
        ParseOutputs(file);
      } else if (MatchIdentifier("globals")) {
        ParseGlobals(file);
      } else if (MatchIdentifier("bus")) {
        file.buses.push_back(ParseBus());
      } else if (MatchIdentifier("patch")) {
        file.patches.push_back(ParsePatch());
      } else if (MatchIdentifier("score")) {
        ParseScore(file);
      } else {
        const Token& t = Peek();
        throw ParseException(t.line, t.column, "Unexpected top-level token: " + t.text);
      }
    }
    if (file.version.empty()) {
      const Token& t = tokens_.front();
      throw ParseException(t.line, t.column, "Missing aurora { version: \"...\" } header");
    }
    return file;
  }

 private:
  bool AtEnd() const { return Peek().kind == TokenKind::kEnd; }

  const Token& Peek(size_t lookahead = 0) const {
    const size_t idx = position_ + lookahead;
    if (idx >= tokens_.size()) {
      return tokens_.back();
    }
    return tokens_[idx];
  }

  const Token& Consume() {
    const Token& t = Peek();
    if (!AtEnd()) {
      ++position_;
    }
    return t;
  }

  bool MatchSymbol(char symbol) {
    if (Peek().kind == TokenKind::kSymbol && Peek().text.size() == 1 && Peek().text[0] == symbol) {
      Consume();
      return true;
    }
    return false;
  }

  bool MatchIdentifier(const std::string& identifier) {
    if (Peek().kind == TokenKind::kIdentifier && Peek().text == identifier) {
      Consume();
      return true;
    }
    return false;
  }

  void ExpectSymbol(char symbol, const std::string& context) {
    if (!MatchSymbol(symbol)) {
      const Token& t = Peek();
      throw ParseException(t.line, t.column, "Expected '" + std::string(1, symbol) + "' in " + context);
    }
  }

  std::string ExpectIdentifierLike(const std::string& context) {
    const Token& t = Peek();
    if (t.kind == TokenKind::kIdentifier || t.kind == TokenKind::kString) {
      Consume();
      return t.text;
    }
    throw ParseException(t.line, t.column, "Expected identifier in " + context);
  }

  std::string ParseDottedIdentifier(const std::string& context) {
    std::string out = ExpectIdentifierLike(context);
    while (MatchSymbol('.')) {
      out += ".";
      out += ExpectIdentifierLike(context);
    }
    return out;
  }

  ParamValue ParseValue() {
    const Token& t = Peek();
    if (t.kind == TokenKind::kString) {
      Consume();
      return ParamValue::String(t.text);
    }
    if (t.kind == TokenKind::kNumber) {
      Consume();
      const NumberUnitParse parsed = ParseNumberUnitToken(t.text);
      if (!parsed.ok) {
        throw ParseException(t.line, t.column, "Invalid numeric literal: " + t.text);
      }
      if (parsed.unit.empty()) {
        return ParamValue::Number(parsed.value);
      }
      return ParamValue::Unit(parsed.value, parsed.unit);
    }
    if (t.kind == TokenKind::kIdentifier) {
      Consume();
      if (t.text == "true") {
        return ParamValue::Bool(true);
      }
      if (t.text == "false") {
        return ParamValue::Bool(false);
      }
      if (MatchSymbol('(')) {
        std::vector<ParamValue> args;
        if (!MatchSymbol(')')) {
          while (true) {
            args.push_back(ParseValue());
            if (MatchSymbol(')')) {
              break;
            }
            ExpectSymbol(',', "call arguments");
          }
        }
        return ParamValue::Call(t.text, std::move(args));
      }
      return ParamValue::Identifier(t.text);
    }
    if (MatchSymbol('{')) {
      std::map<std::string, ParamValue> object;
      if (!MatchSymbol('}')) {
        while (true) {
          const Token& key_token = Peek();
          if (key_token.kind != TokenKind::kIdentifier && key_token.kind != TokenKind::kString &&
              key_token.kind != TokenKind::kNumber) {
            throw ParseException(key_token.line, key_token.column, "Expected object key");
          }
          const std::string key = key_token.text;
          Consume();
          ExpectSymbol(':', "object key/value pair");
          object[key] = ParseValue();
          if (MatchSymbol('}')) {
            break;
          }
          MatchSymbol(',');
        }
      }
      return ParamValue::Object(std::move(object));
    }
    if (MatchSymbol('[')) {
      std::vector<ParamValue> list;
      if (!MatchSymbol(']')) {
        while (true) {
          list.push_back(ParseValue());
          if (MatchSymbol(']')) {
            break;
          }
          ExpectSymbol(',', "list literal");
        }
      }
      return ParamValue::List(std::move(list));
    }
    throw ParseException(t.line, t.column, "Expected value literal");
  }

  std::map<std::string, ParamValue> ParseObjectBody() {
    ExpectSymbol('{', "object body");
    std::map<std::string, ParamValue> object;
    if (MatchSymbol('}')) {
      return object;
    }
    while (true) {
      const Token& key_token = Peek();
      if (key_token.kind != TokenKind::kIdentifier && key_token.kind != TokenKind::kString &&
          key_token.kind != TokenKind::kNumber) {
        throw ParseException(key_token.line, key_token.column, "Expected object key");
      }
      const std::string key = key_token.text;
      Consume();
      ExpectSymbol(':', "object key/value pair");
      object[key] = ParseValue();
      if (MatchSymbol('}')) {
        break;
      }
      MatchSymbol(',');
    }
    return object;
  }

  std::string ParseStemValue(const ParamValue& value) {
    if (value.kind == ParamValue::Kind::kCall && value.string_value == "stem" && !value.list_values.empty()) {
      return ValueAsString(value.list_values.front());
    }
    return ValueAsString(value);
  }

  GraphDefinition ParseGraph(const ParamValue& graph_value) {
    GraphDefinition graph;
    const auto graph_obj = ValueAsObject(graph_value);

    const auto nodes_it = graph_obj.find("nodes");
    if (nodes_it != graph_obj.end()) {
      for (const auto& node_value : ValueAsList(nodes_it->second)) {
        const auto node_obj = ValueAsObject(node_value);
        GraphNode node;
        const auto id_it = node_obj.find("id");
        const auto type_it = node_obj.find("type");
        if (id_it == node_obj.end() || type_it == node_obj.end()) {
          throw std::runtime_error("graph node must contain id and type");
        }
        node.id = ValueAsString(id_it->second);
        node.type = ValueAsString(type_it->second);
        const auto params_it = node_obj.find("params");
        if (params_it != node_obj.end() && params_it->second.kind == ParamValue::Kind::kObject) {
          node.params = params_it->second.object_values;
        }
        graph.nodes.push_back(std::move(node));
      }
    }

    const auto connect_it = graph_obj.find("connect");
    if (connect_it != graph_obj.end()) {
      for (const auto& conn_value : ValueAsList(connect_it->second)) {
        const auto conn_obj = ValueAsObject(conn_value);
        GraphConnection conn;
        const auto from_it = conn_obj.find("from");
        const auto to_it = conn_obj.find("to");
        if (from_it == conn_obj.end() || to_it == conn_obj.end()) {
          throw std::runtime_error("graph connection must contain from and to");
        }
        conn.from = ValueAsString(from_it->second);
        conn.to = ValueAsString(to_it->second);
        const auto rate_it = conn_obj.find("rate");
        if (rate_it != conn_obj.end()) {
          conn.rate = ValueAsString(rate_it->second);
        }
        const auto map_it = conn_obj.find("map");
        if (map_it != conn_obj.end() && map_it->second.kind == ParamValue::Kind::kObject) {
          conn.map = map_it->second.object_values;
        }
        graph.connections.push_back(std::move(conn));
      }
    }

    const auto io_it = graph_obj.find("io");
    if (io_it != graph_obj.end()) {
      const auto io_obj = ValueAsObject(io_it->second);
      const auto out_it = io_obj.find("out");
      if (out_it != io_obj.end()) {
        graph.out = ValueAsString(out_it->second);
      }
    }

    return graph;
  }

  PatchDefinition ParsePatch() {
    PatchDefinition patch;
    patch.name = ExpectIdentifierLike("patch name");
    const auto body = ParseObjectBody();

    if (const auto it = body.find("poly"); it != body.end()) {
      patch.poly = static_cast<int>(ValueAsNumber(it->second, 8.0));
    }
    if (const auto it = body.find("voice_steal"); it != body.end()) {
      patch.voice_steal = ValueAsString(it->second);
    }
    if (const auto it = body.find("mono"); it != body.end() && it->second.kind == ParamValue::Kind::kBool) {
      patch.mono = it->second.bool_value;
    }
    if (const auto it = body.find("binaural"); it != body.end() && it->second.kind == ParamValue::Kind::kObject) {
      const auto binaural_obj = it->second.object_values;
      if (const auto enabled_it = binaural_obj.find("enabled");
          enabled_it != binaural_obj.end() && enabled_it->second.kind == ParamValue::Kind::kBool) {
        patch.binaural.enabled = enabled_it->second.bool_value;
      }
      if (const auto shift_it = binaural_obj.find("shift"); shift_it != binaural_obj.end()) {
        if (shift_it->second.kind == ParamValue::Kind::kUnitNumber && shift_it->second.unit_number_value.unit == "Hz") {
          patch.binaural.shift_hz = shift_it->second.unit_number_value.value;
        } else {
          patch.binaural.shift_hz = ValueAsNumber(shift_it->second, patch.binaural.shift_hz);
        }
      } else if (const auto shift_hz_it = binaural_obj.find("shift_hz"); shift_hz_it != binaural_obj.end()) {
        if (shift_hz_it->second.kind == ParamValue::Kind::kUnitNumber && shift_hz_it->second.unit_number_value.unit == "Hz") {
          patch.binaural.shift_hz = shift_hz_it->second.unit_number_value.value;
        } else {
          patch.binaural.shift_hz = ValueAsNumber(shift_hz_it->second, patch.binaural.shift_hz);
        }
      }
      if (const auto mix_it = binaural_obj.find("mix"); mix_it != binaural_obj.end()) {
        patch.binaural.mix = ValueAsNumber(mix_it->second, patch.binaural.mix);
      }
    }
    if (const auto it = body.find("out"); it != body.end()) {
      patch.out_stem = ParseStemValue(it->second);
    } else {
      patch.out_stem = patch.name;
    }
    if (const auto it = body.find("send"); it != body.end() && it->second.kind == ParamValue::Kind::kObject) {
      SendDefinition send;
      const auto send_obj = it->second.object_values;
      if (const auto bus_it = send_obj.find("bus"); bus_it != send_obj.end()) {
        send.bus = ValueAsString(bus_it->second);
      }
      if (const auto amount_it = send_obj.find("amount"); amount_it != send_obj.end()) {
        if (amount_it->second.kind == ParamValue::Kind::kUnitNumber && amount_it->second.unit_number_value.unit == "dB") {
          send.amount_db = amount_it->second.unit_number_value.value;
        } else if (amount_it->second.kind == ParamValue::Kind::kNumber) {
          send.amount_db = amount_it->second.number_value;
        }
      }
      patch.send = send;
    }
    if (const auto it = body.find("graph"); it != body.end()) {
      patch.graph = ParseGraph(it->second);
    }
    return patch;
  }

  BusDefinition ParseBus() {
    BusDefinition bus;
    bus.name = ExpectIdentifierLike("bus name");
    const auto body = ParseObjectBody();
    if (const auto it = body.find("out"); it != body.end()) {
      bus.out_stem = ParseStemValue(it->second);
    } else {
      bus.out_stem = bus.name;
    }
    if (const auto it = body.find("graph"); it != body.end()) {
      bus.graph = ParseGraph(it->second);
    }
    return bus;
  }

  PlayEvent ParsePlayEvent() {
    PlayEvent event;
    event.patch = ExpectIdentifierLike("play patch name");
    const ParamValue body_value = ParseValue();
    const auto body = ValueAsObject(body_value);

    if (const auto it = body.find("at"); it != body.end()) {
      event.at = ValueAsUnitNumber(it->second);
    }
    if (const auto it = body.find("dur"); it != body.end()) {
      event.dur = ValueAsUnitNumber(it->second);
    }
    if (const auto it = body.find("vel"); it != body.end()) {
      event.vel = ValueAsNumber(it->second, 1.0);
    }
    if (const auto it = body.find("pitch"); it != body.end()) {
      if (it->second.kind == ParamValue::Kind::kList) {
        event.pitch_values = it->second.list_values;
      } else {
        event.pitch_values.push_back(it->second);
      }
    }
    if (const auto it = body.find("params"); it != body.end() && it->second.kind == ParamValue::Kind::kObject) {
      event.params = it->second.object_values;
    }
    return event;
  }

  AutomateEvent ParseAutomateEvent() {
    AutomateEvent event;
    event.target = ParseDottedIdentifier("automation target");
    event.curve = ExpectIdentifierLike("automation curve");

    ExpectSymbol('{', "automation block");
    if (!MatchSymbol('}')) {
      while (true) {
        const Token& tk = Peek();
        if (tk.kind != TokenKind::kNumber) {
          throw ParseException(tk.line, tk.column, "Expected time key in automation map");
        }
        Consume();
        const NumberUnitParse parsed = ParseNumberUnitToken(tk.text);
        if (!parsed.ok) {
          throw ParseException(tk.line, tk.column, "Invalid automation time key: " + tk.text);
        }
        UnitNumber time{parsed.value, parsed.unit.empty() ? "s" : parsed.unit};
        ExpectSymbol(':', "automation point");
        ParamValue value = ParseValue();
        event.points.push_back({time, value});
        if (MatchSymbol('}')) {
          break;
        }
        MatchSymbol(',');
      }
    }
    return event;
  }

  SeqEvent ParseSeqEvent() {
    SeqEvent event;
    event.patch = ExpectIdentifierLike("seq patch name");
    const ParamValue body_value = ParseValue();
    event.fields = ValueAsObject(body_value);
    return event;
  }

  SectionDefinition ParseSection() {
    SectionDefinition section;
    section.name = ExpectIdentifierLike("section name");
    if (!MatchIdentifier("at")) {
      const Token& t = Peek();
      throw ParseException(t.line, t.column, "Expected 'at' in section header");
    }
    section.at = ValueAsUnitNumber(ParseValue());

    if (!MatchIdentifier("dur")) {
      const Token& t = Peek();
      throw ParseException(t.line, t.column, "Expected 'dur' in section header");
    }
    section.dur = ValueAsUnitNumber(ParseValue());

    if (MatchSymbol('|')) {
      while (true) {
        const std::string key = ExpectIdentifierLike("section directive key");
        ExpectSymbol('=', "section directive");
        section.directives[key] = ParseValue();
        if (!MatchSymbol(',')) {
          break;
        }
      }
    }

    ExpectSymbol('{', "section body");
    while (!MatchSymbol('}')) {
      if (MatchIdentifier("play")) {
        section.events.push_back(ParsePlayEvent());
      } else if (MatchIdentifier("automate")) {
        section.events.push_back(ParseAutomateEvent());
      } else if (MatchIdentifier("seq")) {
        section.events.push_back(ParseSeqEvent());
      } else {
        const Token& t = Peek();
        throw ParseException(t.line, t.column, "Unknown event in section: " + t.text);
      }
    }
    return section;
  }

  struct ScorePattern {
    std::vector<SectionDefinition> sections;
    UnitNumber span;
  };

  int ParsePositiveInteger(const std::string& context) {
    const Token& t = Peek();
    if (t.kind != TokenKind::kNumber) {
      throw ParseException(t.line, t.column, "Expected positive integer in " + context);
    }
    Consume();
    const NumberUnitParse parsed = ParseNumberUnitToken(t.text);
    if (!parsed.ok || !parsed.unit.empty()) {
      throw ParseException(t.line, t.column, "Expected unitless integer in " + context);
    }
    const double rounded = std::round(parsed.value);
    if (std::fabs(parsed.value - rounded) > 1e-9 || rounded <= 0.0) {
      throw ParseException(t.line, t.column, "Expected positive integer in " + context);
    }
    return static_cast<int>(rounded);
  }

  UnitNumber AddUnits(const UnitNumber& lhs, const UnitNumber& rhs, const std::string& context) {
    std::string unit = lhs.unit;
    if (unit.empty()) {
      unit = rhs.unit;
    }
    std::string rhs_unit = rhs.unit;
    if (rhs_unit.empty()) {
      rhs_unit = unit;
    }
    if (unit != rhs_unit) {
      throw std::runtime_error("Mismatched time units in " + context + ": " + lhs.unit + " vs " + rhs.unit);
    }
    return UnitNumber{lhs.value + rhs.value, unit};
  }

  UnitNumber MulUnit(const UnitNumber& value, int multiplier) {
    return UnitNumber{value.value * static_cast<double>(multiplier), value.unit};
  }

  UnitNumber ComputeSpan(const std::vector<SectionDefinition>& sections, const std::string& context) {
    if (sections.empty()) {
      return UnitNumber{0.0, "s"};
    }
    bool have_max = false;
    UnitNumber max_end{0.0, ""};
    for (const auto& section : sections) {
      const UnitNumber end = AddUnits(section.at, section.dur, context);
      if (!have_max) {
        max_end = end;
        have_max = true;
        continue;
      }
      if (max_end.unit.empty()) {
        max_end.unit = end.unit;
      }
      std::string end_unit = end.unit.empty() ? max_end.unit : end.unit;
      if (max_end.unit != end_unit) {
        throw std::runtime_error("Mismatched time units in " + context + ": " + max_end.unit + " vs " + end.unit);
      }
      if (end.value > max_end.value) {
        max_end = UnitNumber{end.value, max_end.unit};
      }
    }
    if (max_end.unit.empty()) {
      max_end.unit = "s";
    }
    return max_end;
  }

  void AppendShiftedSections(const std::vector<SectionDefinition>& input, const UnitNumber& offset,
                             std::vector<SectionDefinition>* out, const std::string& context) {
    for (const auto& section : input) {
      SectionDefinition shifted = section;
      shifted.at = AddUnits(section.at, offset, context);
      out->push_back(std::move(shifted));
    }
  }

  std::vector<SectionDefinition> ParseScoreItems(bool allow_pattern_declaration) {
    std::vector<SectionDefinition> items;
    while (!MatchSymbol('}')) {
      if (MatchIdentifier("section")) {
        items.push_back(ParseSection());
        continue;
      }

      if (MatchIdentifier("repeat")) {
        const int repeat_count = ParsePositiveInteger("repeat count");
        ExpectSymbol('{', "repeat block");
        const auto repeated_items = ParseScoreItems(allow_pattern_declaration);
        const UnitNumber span = ComputeSpan(repeated_items, "repeat body span");
        if (span.value <= 0.0) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Repeat body span must be > 0");
        }
        for (int i = 0; i < repeat_count; ++i) {
          const UnitNumber offset = MulUnit(span, i);
          AppendShiftedSections(repeated_items, offset, &items, "repeat expansion");
        }
        continue;
      }

      if (MatchIdentifier("loop")) {
        if (!MatchIdentifier("for")) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Expected 'for' in loop declaration");
        }
        const UnitNumber loop_dur = ValueAsUnitNumber(ParseValue());
        ExpectSymbol('{', "loop block");
        const auto loop_items = ParseScoreItems(false);
        const UnitNumber span = ComputeSpan(loop_items, "loop body span");
        if (span.value <= 0.0) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Loop body span must be > 0");
        }
        const UnitNumber loop_dur_norm = AddUnits(UnitNumber{0.0, span.unit}, loop_dur, "loop duration");
        const int count = static_cast<int>(std::floor(loop_dur_norm.value / span.value));
        for (int i = 0; i < count; ++i) {
          const UnitNumber offset = MulUnit(span, i);
          AppendShiftedSections(loop_items, offset, &items, "loop expansion");
        }
        continue;
      }

      if (allow_pattern_declaration && MatchIdentifier("pattern")) {
        const std::string pattern_name = ExpectIdentifierLike("pattern name");
        ExpectSymbol('{', "pattern block");
        const auto pattern_items = ParseScoreItems(false);
        ScorePattern pattern;
        pattern.sections = pattern_items;
        pattern.span = ComputeSpan(pattern_items, "pattern span");
        score_patterns_[pattern_name] = std::move(pattern);
        continue;
      }

      if (MatchIdentifier("play")) {
        const std::string pattern_name = ExpectIdentifierLike("pattern reference");
        if (!MatchIdentifier("x")) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Expected 'x' in pattern play statement");
        }
        const int count = ParsePositiveInteger("pattern repeat count");
        UnitNumber start_offset{0.0, "s"};
        if (MatchIdentifier("at")) {
          start_offset = ValueAsUnitNumber(ParseValue());
        }
        const auto it = score_patterns_.find(pattern_name);
        if (it == score_patterns_.end()) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Unknown pattern: " + pattern_name);
        }
        const ScorePattern& pattern = it->second;
        if (pattern.span.value <= 0.0) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Pattern span must be > 0: " + pattern_name);
        }
        const UnitNumber start = AddUnits(UnitNumber{0.0, pattern.span.unit}, start_offset, "pattern play offset");
        for (int i = 0; i < count; ++i) {
          const UnitNumber offset = AddUnits(start, MulUnit(pattern.span, i), "pattern play expansion");
          AppendShiftedSections(pattern.sections, offset, &items, "pattern play expansion");
        }
        continue;
      }

      const Token& t = Peek();
      throw ParseException(t.line, t.column, "Unknown score item: " + t.text);
    }
    return items;
  }

  void ParseScore(AuroraFile& file) {
    ExpectSymbol('{', "score block");
    const auto sections = ParseScoreItems(true);
    file.sections.insert(file.sections.end(), sections.begin(), sections.end());
  }

  void ParseAuroraHeader(AuroraFile& file) {
    const auto body = ParseObjectBody();
    if (const auto it = body.find("version"); it != body.end()) {
      file.version = ValueAsString(it->second);
      return;
    }
    const Token& t = Peek();
    throw ParseException(t.line, t.column, "aurora header missing version");
  }

  void ParseAssets(AuroraFile& file) {
    const auto body = ParseObjectBody();
    if (const auto it = body.find("samples_dir"); it != body.end()) {
      file.assets.samples_dir = ValueAsString(it->second);
    }
    if (const auto it = body.find("samples"); it != body.end() && it->second.kind == ParamValue::Kind::kObject) {
      for (const auto& [name, value] : it->second.object_values) {
        file.assets.samples[name] = ValueAsString(value);
      }
    }
  }

  void ParseOutputs(AuroraFile& file) {
    const auto body = ParseObjectBody();
    if (const auto it = body.find("stems_dir"); it != body.end()) {
      file.outputs.stems_dir = ValueAsString(it->second);
    }
    if (const auto it = body.find("midi_dir"); it != body.end()) {
      file.outputs.midi_dir = ValueAsString(it->second);
    }
    if (const auto it = body.find("mix_dir"); it != body.end()) {
      file.outputs.mix_dir = ValueAsString(it->second);
    }
    if (const auto it = body.find("meta_dir"); it != body.end()) {
      file.outputs.meta_dir = ValueAsString(it->second);
    }
    if (const auto it = body.find("master"); it != body.end()) {
      file.outputs.master = ValueAsString(it->second);
    }
    if (const auto it = body.find("render_json"); it != body.end()) {
      file.outputs.render_json = ValueAsString(it->second);
    }
  }

  void ParseGlobals(AuroraFile& file) {
    const auto body = ParseObjectBody();
    if (const auto it = body.find("sr"); it != body.end()) {
      file.globals.sr = static_cast<int>(ValueAsNumber(it->second, 48000.0));
    }
    if (const auto it = body.find("block"); it != body.end()) {
      file.globals.block = static_cast<int>(ValueAsNumber(it->second, 256.0));
    }
    if (const auto it = body.find("tempo"); it != body.end()) {
      file.globals.tempo = ValueAsNumber(it->second, 60.0);
    }
    if (const auto it = body.find("tail_policy"); it != body.end()) {
      const ParamValue& tail = it->second;
      if (tail.kind == ParamValue::Kind::kCall && tail.string_value == "fixed" && !tail.list_values.empty()) {
        const UnitNumber t = ValueAsUnitNumber(tail.list_values.front());
        double seconds = t.value;
        if (t.unit == "ms") {
          seconds /= 1000.0;
        } else if (t.unit == "min") {
          seconds *= 60.0;
        } else if (t.unit == "h") {
          seconds *= 3600.0;
        }
        file.globals.tail_policy.fixed_seconds = seconds;
      }
    }
    if (const auto it = body.find("tempo_map"); it != body.end() && it->second.kind == ParamValue::Kind::kList) {
      for (const ParamValue& point_value : it->second.list_values) {
        const auto point_obj = ValueAsObject(point_value);
        TempoPoint point;
        if (const auto at_it = point_obj.find("at"); at_it != point_obj.end()) {
          point.at = ValueAsUnitNumber(at_it->second);
        }
        if (const auto bpm_it = point_obj.find("bpm"); bpm_it != point_obj.end()) {
          point.bpm = ValueAsNumber(bpm_it->second, 60.0);
        }
        file.globals.tempo_map.push_back(point);
      }
    }
  }

  std::vector<Token> tokens_;
  size_t position_ = 0;
  std::map<std::string, ScorePattern> score_patterns_;
};

}  // namespace

ParseResult ParseAuroraSource(const std::string& source) {
  ParseResult result;
  try {
    Lexer lexer(source);
    Parser parser(lexer.Tokenize());
    result.file = parser.Parse();
    result.ok = true;
  } catch (const ParseException& ex) {
    result.ok = false;
    result.diagnostics.push_back(Diagnostic{ex.line(), ex.column(), ex.what()});
  } catch (const std::exception& ex) {
    result.ok = false;
    result.diagnostics.push_back(Diagnostic{1, 1, ex.what()});
  }
  return result;
}

}  // namespace aurora::lang

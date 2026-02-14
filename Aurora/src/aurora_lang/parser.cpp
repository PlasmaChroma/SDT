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

UnitNumber ValueAsUnitNumber(const ParamValue& value, int line, int column, const std::string& context,
                             const std::string& default_unit = "s") {
  if (value.kind == ParamValue::Kind::kUnitNumber) {
    return value.unit_number_value;
  }
  if (value.kind == ParamValue::Kind::kNumber) {
    return UnitNumber{value.number_value, default_unit};
  }
  throw ParseException(line, column, "Expected numeric time literal in " + context + ", got " + value.DebugString());
}

std::map<std::string, ParamValue> ValueAsObject(const ParamValue& value, int line, int column, const std::string& context) {
  if (value.kind != ParamValue::Kind::kObject) {
    throw ParseException(line, column, "Expected object in " + context + ", got " + value.DebugString());
  }
  return value.object_values;
}

std::vector<ParamValue> ValueAsList(const ParamValue& value, int line, int column, const std::string& context) {
  if (value.kind != ParamValue::Kind::kList) {
    throw ParseException(line, column, "Expected list in " + context + ", got " + value.DebugString());
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
      } else if (MatchIdentifier("section")) {
        ParseTopLevelSectionTemplate();
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
    const Token& graph_token = Peek();
    GraphDefinition graph;
    const auto graph_obj = ValueAsObject(graph_value, graph_token.line, graph_token.column, "graph");

    const auto nodes_it = graph_obj.find("nodes");
    if (nodes_it != graph_obj.end()) {
      for (const auto& node_value : ValueAsList(nodes_it->second, graph_token.line, graph_token.column, "graph.nodes")) {
        const auto node_obj = ValueAsObject(node_value, graph_token.line, graph_token.column, "graph.nodes[]");
        GraphNode node;
        const auto id_it = node_obj.find("id");
        const auto type_it = node_obj.find("type");
        if (id_it == node_obj.end() || type_it == node_obj.end()) {
          throw ParseException(graph_token.line, graph_token.column, "graph node must contain id and type");
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
      for (const auto& conn_value : ValueAsList(connect_it->second, graph_token.line, graph_token.column, "graph.connect")) {
        const auto conn_obj = ValueAsObject(conn_value, graph_token.line, graph_token.column, "graph.connect[]");
        GraphConnection conn;
        const auto from_it = conn_obj.find("from");
        const auto to_it = conn_obj.find("to");
        if (from_it == conn_obj.end() || to_it == conn_obj.end()) {
          throw ParseException(graph_token.line, graph_token.column, "graph connection must contain from and to");
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
      const auto io_obj = ValueAsObject(io_it->second, graph_token.line, graph_token.column, "graph.io");
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
    if (const auto it = body.find("legato"); it != body.end() && it->second.kind == ParamValue::Kind::kBool) {
      patch.legato = it->second.bool_value;
    }
    if (const auto it = body.find("retrig"); it != body.end()) {
      patch.retrig = ValueAsString(it->second);
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
    if (const auto it = body.find("channels"); it != body.end()) {
      bus.channels = static_cast<int>(std::round(ValueAsNumber(it->second, 1.0)));
    }
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
    const Token& body_token = Peek();
    const ParamValue body_value = ParseValue();
    const auto body = ValueAsObject(body_value, body_token.line, body_token.column, "play event");

    if (const auto it = body.find("at"); it != body.end()) {
      event.at = ValueAsUnitNumber(it->second, body_token.line, body_token.column, "play.at");
    }
    if (const auto it = body.find("dur"); it != body.end()) {
      event.dur = ValueAsUnitNumber(it->second, body_token.line, body_token.column, "play.dur");
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

  PlayEvent ParseGateLikeEvent(const std::string& context, const UnitNumber& default_dur) {
    PlayEvent event;
    event.patch = ExpectIdentifierLike(context + " patch name");
    const Token& body_token = Peek();
    const ParamValue body_value = ParseValue();
    const auto body = ValueAsObject(body_value, body_token.line, body_token.column, context + " event");

    if (const auto it = body.find("at"); it != body.end()) {
      event.at = ValueAsUnitNumber(it->second, body_token.line, body_token.column, context + ".at");
    }
    if (const auto it = body.find("dur"); it != body.end()) {
      event.dur = ValueAsUnitNumber(it->second, body_token.line, body_token.column, context + ".dur");
    } else {
      event.dur = default_dur;
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
    const Token& body_token = Peek();
    const ParamValue body_value = ParseValue();
    event.fields = ValueAsObject(body_value, body_token.line, body_token.column, "seq event");
    return event;
  }

  SetEvent ParseSetEvent() {
    SetEvent event;
    event.target = ParseDottedIdentifier("set target");
    ExpectSymbol('=', "set event");
    event.value = ParseValue();
    return event;
  }

  std::vector<SectionEvent> ParseSectionEvents() {
    std::vector<SectionEvent> events;
    while (!MatchSymbol('}')) {
      if (MatchIdentifier("repeat")) {
        const int repeat_count = ParsePositiveInteger("section repeat count");
        ExpectSymbol('{', "section repeat block");
        const auto repeated_events = ParseSectionEvents();
        const Token& repeat_token = Peek();
        const UnitNumber span =
            ComputeSectionEventSpan(repeated_events, "section repeat body span", repeat_token.line, repeat_token.column);
        if (span.value <= 0.0) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Section repeat body span must be > 0");
        }
        for (int i = 0; i < repeat_count; ++i) {
          const UnitNumber offset = MulUnit(span, i);
          AppendShiftedSectionEvents(repeated_events, offset, &events, repeat_token.line, repeat_token.column,
                                     "section repeat expansion");
        }
        continue;
      }
      if (MatchIdentifier("set")) {
        events.push_back(ParseSetEvent());
      } else if (MatchIdentifier("use")) {
        const Token& use_token = Peek();
        const ReusableCall call = ParseReusableCall("use", "Expected 'x' in use statement");
        SectionDefinition expanded;
        ExpandReusableIntoSection(call, &expanded, use_token.line, use_token.column, "use");
        for (const auto& e : expanded.events) {
          events.push_back(e);
        }
      } else if (MatchIdentifier("play")) {
        events.push_back(ParsePlayEvent());
      } else if (MatchIdentifier("trigger")) {
        events.push_back(ParseGateLikeEvent("trigger", UnitNumber{0.01, "s"}));
      } else if (MatchIdentifier("gate")) {
        events.push_back(ParseGateLikeEvent("gate", UnitNumber{0.25, "s"}));
      } else if (MatchIdentifier("automate")) {
        events.push_back(ParseAutomateEvent());
      } else if (MatchIdentifier("seq")) {
        events.push_back(ParseSeqEvent());
      } else {
        const Token& t = Peek();
        throw ParseException(t.line, t.column, "Unknown event in section: " + t.text);
      }
    }
    return events;
  }

  SectionDefinition ParseSection() {
    SectionDefinition section;
    section.name = ExpectIdentifierLike("section name");
    if (!MatchIdentifier("at")) {
      const Token& t = Peek();
      throw ParseException(t.line, t.column, "Expected 'at' in section header");
    }
    const Token& at_token = Peek();
    section.at = ValueAsUnitNumber(ParseValue(), at_token.line, at_token.column, "section.at");

    if (!MatchIdentifier("dur")) {
      const Token& t = Peek();
      throw ParseException(t.line, t.column, "Expected 'dur' in section header");
    }
    const Token& dur_token = Peek();
    section.dur = ValueAsUnitNumber(ParseValue(), dur_token.line, dur_token.column, "section.dur");

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
    section.events = ParseSectionEvents();
    return section;
  }

  struct ScorePattern {
    std::vector<SectionDefinition> sections;
    UnitNumber span;
  };

  struct ReusableCall {
    std::string name;
    int count = 1;
    UnitNumber start_offset{0.0, "s"};
  };

  ReusableCall ParseReusableCall(const std::string& context, const std::string& x_error) {
    ReusableCall call;
    call.name = ExpectIdentifierLike(context + " name");
    if (!MatchIdentifier("x")) {
      const Token& t = Peek();
      throw ParseException(t.line, t.column, x_error);
    }
    call.count = ParsePositiveInteger(context + " repeat count");
    if (MatchIdentifier("at")) {
      const Token& at_token = Peek();
      call.start_offset = ValueAsUnitNumber(ParseValue(), at_token.line, at_token.column, context + " offset");
    }
    return call;
  }

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

  UnitNumber AddUnits(const UnitNumber& lhs, const UnitNumber& rhs, const std::string& context, int line, int column) {
    std::string unit = lhs.unit;
    if (unit.empty()) {
      unit = rhs.unit;
    }
    std::string rhs_unit = rhs.unit;
    if (rhs_unit.empty()) {
      rhs_unit = unit;
    }
    if (unit != rhs_unit) {
      throw ParseException(line, column, "Mismatched time units in " + context + ": " + lhs.unit + " vs " + rhs.unit);
    }
    return UnitNumber{lhs.value + rhs.value, unit};
  }

  UnitNumber MulUnit(const UnitNumber& value, int multiplier) {
    return UnitNumber{value.value * static_cast<double>(multiplier), value.unit};
  }

  UnitNumber ComputeSpan(const std::vector<SectionDefinition>& sections, const std::string& context, int line, int column) {
    if (sections.empty()) {
      return UnitNumber{0.0, "s"};
    }
    bool have_max = false;
    UnitNumber max_end{0.0, ""};
    for (const auto& section : sections) {
      const UnitNumber end = AddUnits(section.at, section.dur, context, line, column);
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
        throw ParseException(line, column, "Mismatched time units in " + context + ": " + max_end.unit + " vs " + end.unit);
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
                             std::vector<SectionDefinition>* out, const std::string& context, int line, int column) {
    for (const auto& section : input) {
      SectionDefinition shifted = section;
      shifted.at = AddUnits(section.at, offset, context, line, column);
      out->push_back(std::move(shifted));
    }
  }

  const ScorePattern& ResolveReusable(const ReusableCall& call, int line, int column, const std::string& context) {
    const auto it = score_patterns_.find(call.name);
    if (it == score_patterns_.end()) {
      throw ParseException(line, column, "Unknown " + context + ": " + call.name);
    }
    if (it->second.span.value <= 0.0) {
      throw ParseException(line, column, "Reusable " + context + " span must be > 0: " + call.name);
    }
    return it->second;
  }

  void ExpandReusableToScore(const ReusableCall& call, std::vector<SectionDefinition>* out, int line, int column,
                             const std::string& context) {
    const ScorePattern& pattern = ResolveReusable(call, line, column, context);
    const UnitNumber start = AddUnits(UnitNumber{0.0, pattern.span.unit}, call.start_offset, context + " offset", line, column);
    for (int i = 0; i < call.count; ++i) {
      const UnitNumber offset = AddUnits(start, MulUnit(pattern.span, i), context + " expansion", line, column);
      AppendShiftedSections(pattern.sections, offset, out, context + " expansion", line, column);
    }
  }

  SectionEvent ShiftSectionEvent(const SectionEvent& event, const UnitNumber& offset, int line, int column,
                                 const std::string& context) {
    if (std::holds_alternative<PlayEvent>(event)) {
      PlayEvent shifted = std::get<PlayEvent>(event);
      shifted.at = AddUnits(shifted.at, offset, context + " play offset", line, column);
      return shifted;
    }
    if (std::holds_alternative<AutomateEvent>(event)) {
      AutomateEvent shifted = std::get<AutomateEvent>(event);
      for (auto& point : shifted.points) {
        point.first = AddUnits(point.first, offset, context + " automate offset", line, column);
      }
      return shifted;
    }
    if (std::holds_alternative<SetEvent>(event)) {
      return std::get<SetEvent>(event);
    }
    SeqEvent shifted = std::get<SeqEvent>(event);
    if (const auto it = shifted.fields.find("at"); it != shifted.fields.end()) {
      UnitNumber seq_at = ValueAsUnitNumber(it->second, line, column, context + " seq.at");
      seq_at = AddUnits(seq_at, offset, context + " seq.at offset", line, column);
      shifted.fields["at"] = ParamValue::Unit(seq_at.value, seq_at.unit);
    } else {
      shifted.fields["at"] = ParamValue::Unit(offset.value, offset.unit);
    }
    return shifted;
  }

  UnitNumber ComputeSectionEventSpan(const std::vector<SectionEvent>& events, const std::string& context, int line, int column) {
    bool have_max = false;
    UnitNumber max_end{0.0, ""};
    for (const auto& event : events) {
      if (std::holds_alternative<SetEvent>(event)) {
        continue;
      }

      UnitNumber start{0.0, "s"};
      UnitNumber dur{0.0, "s"};
      bool has_timed_extent = false;
      if (std::holds_alternative<PlayEvent>(event)) {
        const auto& play = std::get<PlayEvent>(event);
        start = play.at;
        dur = play.dur;
        has_timed_extent = true;
      } else if (std::holds_alternative<AutomateEvent>(event)) {
        const auto& automate = std::get<AutomateEvent>(event);
        if (automate.points.empty()) {
          continue;
        }
        UnitNumber min_t = automate.points.front().first;
        UnitNumber max_t = automate.points.front().first;
        for (const auto& point : automate.points) {
          const UnitNumber t = point.first;
          AddUnits(min_t, UnitNumber{0.0, t.unit}, context + " automation unit check", line, column);
          if (t.value < min_t.value) {
            min_t = t;
          }
          if (t.value > max_t.value) {
            max_t = t;
          }
        }
        start = min_t;
        dur = UnitNumber{max_t.value - min_t.value, max_t.unit};
        has_timed_extent = true;
      } else if (std::holds_alternative<SeqEvent>(event)) {
        const auto& seq = std::get<SeqEvent>(event);
        if (const auto it = seq.fields.find("at"); it != seq.fields.end()) {
          start = ValueAsUnitNumber(it->second, line, column, context + " seq.at");
          has_timed_extent = true;
        }
        if (const auto it = seq.fields.find("dur"); it != seq.fields.end()) {
          dur = ValueAsUnitNumber(it->second, line, column, context + " seq.dur");
          has_timed_extent = true;
        }
      }
      if (!has_timed_extent) {
        continue;
      }
      const UnitNumber end = AddUnits(start, dur, context + " section event span", line, column);
      if (!have_max) {
        max_end = end;
        have_max = true;
        continue;
      }
      AddUnits(max_end, UnitNumber{0.0, end.unit}, context + " section event span", line, column);
      if (end.value > max_end.value) {
        max_end = UnitNumber{end.value, max_end.unit.empty() ? end.unit : max_end.unit};
      }
    }
    if (!have_max) {
      return UnitNumber{0.0, "s"};
    }
    if (max_end.unit.empty()) {
      max_end.unit = "s";
    }
    return max_end;
  }

  void AppendShiftedSectionEvents(const std::vector<SectionEvent>& input, const UnitNumber& offset,
                                  std::vector<SectionEvent>* out, int line, int column, const std::string& context) {
    for (const auto& event : input) {
      out->push_back(ShiftSectionEvent(event, offset, line, column, context));
    }
  }

  void ExpandReusableIntoSection(const ReusableCall& call, SectionDefinition* out_section, int line, int column,
                                 const std::string& context) {
    const ScorePattern& pattern = ResolveReusable(call, line, column, context);
    const UnitNumber start = AddUnits(UnitNumber{0.0, pattern.span.unit}, call.start_offset, context + " offset", line, column);
    for (int i = 0; i < call.count; ++i) {
      const UnitNumber iter_offset = AddUnits(start, MulUnit(pattern.span, i), context + " expansion", line, column);
      for (const auto& templ_section : pattern.sections) {
        const UnitNumber section_offset =
            AddUnits(iter_offset, templ_section.at, context + " section offset", line, column);
        for (const auto& event : templ_section.events) {
          out_section->events.push_back(ShiftSectionEvent(event, section_offset, line, column, context));
        }
      }
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
        const Token& repeat_token = Peek();
        const UnitNumber span = ComputeSpan(repeated_items, "repeat body span", repeat_token.line, repeat_token.column);
        if (span.value <= 0.0) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Repeat body span must be > 0");
        }
        for (int i = 0; i < repeat_count; ++i) {
          const UnitNumber offset = MulUnit(span, i);
          AppendShiftedSections(repeated_items, offset, &items, "repeat expansion", repeat_token.line, repeat_token.column);
        }
        continue;
      }

      if (MatchIdentifier("loop")) {
        if (!MatchIdentifier("for")) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Expected 'for' in loop declaration");
        }
        const Token& loop_dur_token = Peek();
        const UnitNumber loop_dur =
            ValueAsUnitNumber(ParseValue(), loop_dur_token.line, loop_dur_token.column, "loop duration");
        ExpectSymbol('{', "loop block");
        const auto loop_items = ParseScoreItems(false);
        const Token& loop_token = Peek();
        const UnitNumber span = ComputeSpan(loop_items, "loop body span", loop_token.line, loop_token.column);
        if (span.value <= 0.0) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Loop body span must be > 0");
        }
        const UnitNumber loop_dur_norm =
            AddUnits(UnitNumber{0.0, span.unit}, loop_dur, "loop duration", loop_token.line, loop_token.column);
        const int count = static_cast<int>(std::floor(loop_dur_norm.value / span.value));
        for (int i = 0; i < count; ++i) {
          const UnitNumber offset = MulUnit(span, i);
          AppendShiftedSections(loop_items, offset, &items, "loop expansion", loop_token.line, loop_token.column);
        }
        continue;
      }

      if (allow_pattern_declaration && MatchIdentifier("pattern")) {
        const std::string pattern_name = ExpectIdentifierLike("pattern name");
        if (score_patterns_.contains(pattern_name)) {
          const Token& t = Peek();
          throw ParseException(t.line, t.column, "Duplicate reusable section/pattern name: " + pattern_name);
        }
        ExpectSymbol('{', "pattern block");
        const auto pattern_items = ParseScoreItems(false);
        ScorePattern pattern;
        pattern.sections = pattern_items;
        const Token& pattern_token = Peek();
        pattern.span = ComputeSpan(pattern_items, "pattern span", pattern_token.line, pattern_token.column);
        score_patterns_[pattern_name] = std::move(pattern);
        continue;
      }

      if (MatchIdentifier("use")) {
        const Token& use_token = Peek();
        const ReusableCall call = ParseReusableCall("use", "Expected 'x' in use statement");
        ExpandReusableToScore(call, &items, use_token.line, use_token.column, "use");
        continue;
      }

      if (MatchIdentifier("play")) {
        const Token& pattern_play_token = Peek();
        const ReusableCall call = ParseReusableCall("pattern play", "Expected 'x' in pattern play statement");
        ExpandReusableToScore(call, &items, pattern_play_token.line, pattern_play_token.column, "pattern");
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

  void ParseTopLevelSectionTemplate() {
    const SectionDefinition section = ParseSection();
    if (score_patterns_.contains(section.name)) {
      const Token& t = Peek();
      throw ParseException(t.line, t.column, "Duplicate reusable section/pattern name: " + section.name);
    }
    ScorePattern pattern;
    pattern.sections.push_back(section);
    const Token& top_level_section_token = Peek();
    pattern.span =
        ComputeSpan(pattern.sections, "top-level section template span", top_level_section_token.line,
                    top_level_section_token.column);
    score_patterns_[section.name] = std::move(pattern);
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
        const Token& tail_token = Peek();
        const UnitNumber t =
            ValueAsUnitNumber(tail.list_values.front(), tail_token.line, tail_token.column, "globals.tail_policy.fixed");
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
        const Token& tempo_token = Peek();
        const auto point_obj = ValueAsObject(point_value, tempo_token.line, tempo_token.column, "globals.tempo_map[]");
        TempoPoint point;
        if (const auto at_it = point_obj.find("at"); at_it != point_obj.end()) {
          point.at =
              ValueAsUnitNumber(at_it->second, tempo_token.line, tempo_token.column, "globals.tempo_map[].at");
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

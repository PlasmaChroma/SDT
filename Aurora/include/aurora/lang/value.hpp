#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace aurora::lang {

struct UnitNumber {
  double value = 0.0;
  std::string unit;
};

struct ParamValue {
  enum class Kind {
    kNull,
    kBool,
    kNumber,
    kUnitNumber,
    kString,
    kIdentifier,
    kList,
    kObject,
    kCall
  };

  Kind kind = Kind::kNull;
  bool bool_value = false;
  double number_value = 0.0;
  UnitNumber unit_number_value{};
  std::string string_value;
  std::vector<ParamValue> list_values;
  std::map<std::string, ParamValue> object_values;

  static ParamValue Null() { return ParamValue{}; }

  static ParamValue Bool(bool v) {
    ParamValue out;
    out.kind = Kind::kBool;
    out.bool_value = v;
    return out;
  }

  static ParamValue Number(double v) {
    ParamValue out;
    out.kind = Kind::kNumber;
    out.number_value = v;
    return out;
  }

  static ParamValue Unit(double v, std::string unit) {
    ParamValue out;
    out.kind = Kind::kUnitNumber;
    out.unit_number_value = UnitNumber{v, std::move(unit)};
    return out;
  }

  static ParamValue String(std::string v) {
    ParamValue out;
    out.kind = Kind::kString;
    out.string_value = std::move(v);
    return out;
  }

  static ParamValue Identifier(std::string v) {
    ParamValue out;
    out.kind = Kind::kIdentifier;
    out.string_value = std::move(v);
    return out;
  }

  static ParamValue List(std::vector<ParamValue> v) {
    ParamValue out;
    out.kind = Kind::kList;
    out.list_values = std::move(v);
    return out;
  }

  static ParamValue Object(std::map<std::string, ParamValue> v) {
    ParamValue out;
    out.kind = Kind::kObject;
    out.object_values = std::move(v);
    return out;
  }

  static ParamValue Call(std::string callee, std::vector<ParamValue> args) {
    ParamValue out;
    out.kind = Kind::kCall;
    out.string_value = std::move(callee);
    out.list_values = std::move(args);
    return out;
  }

  [[nodiscard]] bool IsIdentifier(const std::string& expected) const {
    return kind == Kind::kIdentifier && string_value == expected;
  }

  [[nodiscard]] bool IsString(const std::string& expected) const {
    return kind == Kind::kString && string_value == expected;
  }

  [[nodiscard]] bool IsNumberLike() const { return kind == Kind::kNumber || kind == Kind::kUnitNumber; }

  [[nodiscard]] std::optional<double> TryNumber() const {
    if (kind == Kind::kNumber) {
      return number_value;
    }
    if (kind == Kind::kUnitNumber) {
      return unit_number_value.value;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::string DebugString() const {
    std::ostringstream oss;
    switch (kind) {
      case Kind::kNull:
        oss << "null";
        break;
      case Kind::kBool:
        oss << (bool_value ? "true" : "false");
        break;
      case Kind::kNumber:
        oss << number_value;
        break;
      case Kind::kUnitNumber:
        oss << unit_number_value.value << unit_number_value.unit;
        break;
      case Kind::kString:
        oss << "\"" << string_value << "\"";
        break;
      case Kind::kIdentifier:
        oss << string_value;
        break;
      case Kind::kList:
        oss << "[...]";
        break;
      case Kind::kObject:
        oss << "{...}";
        break;
      case Kind::kCall:
        oss << string_value << "(...)";
        break;
    }
    return oss.str();
  }
};

}  // namespace aurora::lang


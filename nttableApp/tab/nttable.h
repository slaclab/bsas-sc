#ifndef TAB_NTTABLE_H
#define TAB_NTTABLE_H

#include <pvxs/data.h>
#include <pvxs/sharedArray.h>

#include <epicsAssert.h>

namespace tabulator {
namespace nt {

/** A NormativeTypes Table.
 *
 * Enforces invariants:
 *
 *   * Number of labels must be equal to number of columns
 *   * All columns must be scalar_t[] (array of scalar)
 *
 * @code
 * std::vector<NTTable::ColumnSpec> columns {
 *   { TypeCode::Float64A, "floats", "floats label" },
 *   { TypeCode::Int32A, "ints", "ints label" }
 * };
 * auto it = columns.begin();
 * NTTable table(columns.begin(), columns.end());
 * auto def = table.build();
 * auto val = table.create(); // instantiate a Value; val["labels"] will be
 * pre-populated.
 * @endcode
 */
class NTTable {
  pvxs::shared_array<const std::string> _labels;
  pvxs::TypeDef _def;

public:
  static const std::string
      LABELS_FIELD; // Name of the field that holds the labels
  static const std::string
      COLUMNS_FIELD; // Name of the field that holds the columns

  struct ColumnSpec {
    pvxs::TypeCode type_code;
    std::string name;
    std::string label;

    ColumnSpec(pvxs::TypeCode type_code, const std::string &name,
               const std::string &label)
        : type_code(type_code), name(name), label(label) {
      auto kind = type_code.kind();

      assert(kind != pvxs::Kind::Compound);
      assert(kind != pvxs::Kind::Null);
      assert(type_code.isarray());
    }
  };

  template <typename Iter> NTTable(Iter cur, const Iter end) {
    auto labels = std::vector<std::string>();
    auto value = pvxs::TypeDef(pvxs::TypeCode::Struct, {});

    while (cur != end) {
      value += {pvxs::Member(cur->type_code, cur->name)};
      labels.push_back(cur->label);
      ++cur;
    }

    _labels =
        pvxs::shared_array<const std::string>(labels.begin(), labels.end());

    _def = pvxs::TypeDef(
        pvxs::TypeCode::Struct, "epics:nt/NTTable:1.0",
        {pvxs::members::StringA(LABELS_FIELD), value.as(COLUMNS_FIELD)});
  }

  //! A TypeDef which can be appended
  pvxs::TypeDef build() const { return _def; }

  //! Instantiate
  //! The field labels will be pre-populated
  inline pvxs::Value create() const {
    auto v = build().create();
    v[LABELS_FIELD] = _labels;
    return v;
  }
};

} // namespace nt
} // namespace tabulator

#endif
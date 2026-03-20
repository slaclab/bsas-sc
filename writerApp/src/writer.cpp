#include "writer.h"

#include <exception>
#include <iostream>
#include <set>
#include <vector>

#include <pvxs/log.h>

#include <epicsTime.h>

#include <highfive/H5File.hpp>

DEFINE_LOGGER(LOG, "writer");

static const std::string META_GROUP = "/meta";
static const std::string META_PVNAMES = "pvnames";
static const std::string META_COLUMN_PREFIXES = "column_prefixes";
static const std::string META_LABELS = "labels";
static const std::string META_COLUMNS = "columns";
static const std::string META_TYPES = "pvxs_types";

static const std::string ATTR_INPUT_PV = "Input PV";
static const std::string ATTR_SIGNAL = "Signal";
static const std::string ATTR_LABEL = "NTTable label";
static const std::string ATTR_COLUMN = "NTTable column";

static const char *DATA_GROUP = "/data";

namespace H5 = HighFive;

namespace tabulator {

// Assumption: input type is array
static H5::DataType pvxs_to_h5_type(pvxs::TypeCode t) {
  switch (t.code) {
#define CASE(PT, HT)                                                           \
  case pvxs::TypeCode::PT:                                                     \
    return H5::create_datatype<HT>()
    CASE(Bool, bool);
    CASE(BoolA, bool);
    CASE(Int8, int8_t);
    CASE(Int16, int16_t);
    CASE(Int32, int32_t);
    CASE(Int64, int64_t);
    CASE(UInt8, uint8_t);
    CASE(UInt16, uint16_t);
    CASE(UInt32, uint32_t);
    CASE(UInt64, uint64_t);
    CASE(Int8A, int8_t);
    CASE(Int16A, int16_t);
    CASE(Int32A, int32_t);
    CASE(Int64A, int64_t);
    CASE(UInt8A, uint8_t);
    CASE(UInt16A, uint16_t);
    CASE(UInt32A, uint32_t);
    CASE(UInt64A, uint64_t);
    CASE(Float32, float);
    CASE(Float64, double);
    CASE(Float32A, float);
    CASE(Float64A, double);
    CASE(String, std::string);
    CASE(StringA, std::string);
#undef CASE

  default:
    throw "Can't map pvxs type to hdf5 type";
  }
}

static bool parts(const std::string &name, const std::string &sep,
                  std::string *prefix, std::string *suffix) {
  if (!prefix && !suffix)
    return true;

  auto i = name.rfind(sep);

  if (i == std::string::npos)
    return false;

  if (prefix)
    *prefix = name.substr(0, i);

  if (suffix)
    *suffix = name.substr(i + 1);

  return true;
}

void Writer::build_file_structure(size_t chunk_size) {
  epicsTimeStamp start, end;
  epicsTimeGetCurrent(&start);

  file_->createAttribute(ATTR_INPUT_PV, input_pv_);

  H5::DataSetCreateProps props;
  props.add(H5::Chunking({chunk_size}));

  log_debug_printf(LOG, "Building file structure with chunk_size=%lu\n",
                   chunk_size);

  // Groups to hold metadata and data
  auto meta_group = file_->createGroup(META_GROUP);
  log_debug_printf(LOG, "  Created metadata group %s\n",
                   meta_group.getPath().c_str());

  auto data_group = file_->createGroup(DATA_GROUP);
  log_debug_printf(LOG, "  Created data group %s\n",
                   data_group.getPath().c_str());

  auto root_group = data_group.createGroup(root_group_);
  log_debug_printf(LOG, "  Created root group %s\n",
                   root_group.getPath().c_str());

  // Metadata
  std::set<std::string> pvnames_set; // Set of seen PV names
  std::vector<std::string>
      pvnames; // PV names, in order (e.g. ["SIM:STAT:0", "SIM:STAT:1", ...])
  std::vector<std::string>
      column_prefixes; // Column prefixes, in order (e.g. ["pv0", "pv1", ...])
  std::vector<std::string>
      columns; // Columns (e.g. ["pv0_min", "pv0_max", "pv0_std", ...])
  std::vector<std::string>
      labels; // Labels (e.g. ["SIM:STAT:0 min", "SIM:STAT:0 max", ...])
  std::vector<uint8_t> types; // PVXS types for each column

  for (auto c : type_->columns) {
    columns.push_back(c.name);
    labels.push_back(c.label);
    types.push_back(c.type_code.code);
  }

  for (auto c : type_->time_columns) {
    auto ds = root_group.createDataSet(
        c.name, H5::DataSpace({0}, {H5::DataSpace::UNLIMITED}),
        pvxs_to_h5_type(c.type_code), props);

    ds.createAttribute(ATTR_LABEL, c.label);
    ds.createAttribute(ATTR_COLUMN, c.name);
    datasets_.emplace(c.name, ds);
  }

  for (auto c : type_->data_columns) {
    std::string pvname, column_prefix, column_suffix;

    if (!parts(c.label, label_sep_, &pvname, NULL))
      throw std::runtime_error(
          std::string("Invalid label name (must contain '") + label_sep_ +
          "'): " + c.label);

    if (!parts(c.name, col_sep_, &column_prefix, &column_suffix))
      throw std::runtime_error(
          std::string("Invalid column name (must contain '") + col_sep_ +
          "'): " + c.name);

    if (pvnames_set.find(pvname) == pvnames_set.end()) {
      pvnames.push_back(pvname);
      pvnames_set.insert(pvname);
      column_prefixes.push_back(column_prefix);

      auto g = root_group.createGroup(column_prefix);
      g.createAttribute(ATTR_SIGNAL, pvname);
    }

    auto group = root_group.getGroup(column_prefix);

    auto ds = group.createDataSet(
        column_suffix, H5::DataSpace({0}, {H5::DataSpace::UNLIMITED}),
        pvxs_to_h5_type(c.type_code), props);

    ds.createAttribute(ATTR_LABEL, c.label);
    ds.createAttribute(ATTR_COLUMN, c.name);

    datasets_.emplace(c.name, ds);
  }

  // Fill meta datasets
  meta_group.createDataSet(META_PVNAMES, pvnames);
  meta_group.createDataSet(META_COLUMN_PREFIXES, column_prefixes);
  meta_group.createDataSet(META_COLUMNS, columns);
  meta_group.createDataSet(META_LABELS, labels);
  meta_group.createDataSet(META_TYPES, types);

  epicsTimeGetCurrent(&end);
  log_debug_printf(LOG, "Built file structure in %.3f sec\n",
                   epicsTimeDiffInSeconds(&end, &start));
}

Writer::Writer(const std::string &input_pv, const std::string &path,
               const std::string &root_group, const std::string &label_sep,
               const std::string &col_sep)
    : input_pv_(input_pv), type_(nullptr), file_path_(path),
      file_(new H5::File(path, H5F_ACC_EXCL)), root_group_(root_group),
      label_sep_(label_sep), col_sep_(col_sep) {
  log_debug_printf(LOG, "Writing to file '%s'\n", path.c_str());
}

template <typename T>
static void write_dataset(H5::DataSet &dataset, const TimeTableValue &value,
                          const std::string &colname) {
  auto data = value.get_column_as<T>(colname);
  size_t len = data.size();

  auto dims = dataset.getDimensions();
  dims[0] += len;
  dataset.resize(dims);
  dataset.select({dims[0] - len}, {len}).write_raw(data.dataPtr().get());
}

void Writer::write(pvxs::Value value) {

  if (!value) {
    log_warn_printf(LOG, "Empty value, skip writing%s\n", "");
    return;
  }

  size_t num_rows = 1024;

  if (type_ == nullptr) {

    log_debug_printf(LOG, "First update, extracting type%s\n", "");
    type_.reset(new TimeTable(value));

    // Set chunk size to the size of this first update
    build_file_structure(num_rows);
  } else {
    // Check that the update has data to be written
    num_rows = type_->wrap(value, false)
                   .get_column_as<TimeTable::SECONDS_PAST_EPOCH_T>(
                       TimeTable::SECONDS_PAST_EPOCH_COL)
                   .size();

    if (num_rows == 0) {
      log_warn_printf(LOG, "Zero rows, skip writing%s\n", "");
      return;
    }
  }

  epicsTimeStamp start, end;
  epicsTimeGetCurrent(&start);

  auto tvalue = type_->wrap(value, true);

  for (auto c : type_->columns) {
    auto ds = datasets_.find(c.name);
    if (ds == datasets_.end())
      throw std::logic_error(std::string("Can't find dataset: ") + c.name);

    switch (c.type_code.code) {
#define CASE(PT, T)                                                            \
  case pvxs::TypeCode::PT:                                                     \
    write_dataset<T>(ds->second, tvalue, c.name);                              \
    break
      CASE(BoolA, bool);
      CASE(Int8A, int8_t);
      CASE(Int16A, int16_t);
      CASE(Int32A, int32_t);
      CASE(Int64A, int64_t);
      CASE(UInt8A, uint8_t);
      CASE(UInt16A, uint16_t);
      CASE(UInt32A, uint32_t);
      CASE(UInt64A, uint64_t);
      CASE(Float32A, float);
      CASE(Float64A, double);
      CASE(StringA, std::string);
#undef CASE

    default:
      throw std::runtime_error(std::string("Unexpected type") +
                               c.type_code.name());
    }
  }

  file_->flush();
  epicsTimeGetCurrent(&end);
  log_debug_printf(LOG, "Wrote update to file in %.3f sec (%lu rows)\n",
                   epicsTimeDiffInSeconds(&end, &start), num_rows);
}

std::string Writer::get_file_path() const { return file_path_; }

} // namespace tabulator

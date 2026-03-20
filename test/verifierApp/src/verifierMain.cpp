#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <clipp.h>

#include <epicsStdio.h>

#include <highfive/H5File.hpp>

#include <pvxs/client.h>
#include <pvxs/log.h>

#include <tab/timetable.h>

DEFINE_LOGGER(LOG, "verifier");

namespace H5 = HighFive;

namespace tabulator {

static const std::string META_GROUP = "/meta";
static const std::string META_PVNAMES = "pvnames";
static const std::string META_COLUMN_PREFIXES = "column_prefixes";
static const std::string META_LABELS = "labels";
static const std::string META_COLUMNS = "columns";
static const std::string META_TYPES = "pvxs_types";

static const char *DATA_GROUP = "/data";

static std::atomic<bool> g_stop_requested(false);

static void on_signal(int) { g_stop_requested.store(true); }

static bool parts(const std::string &name, const std::string &sep,
                  std::string *prefix, std::string *suffix) {
  auto i = name.rfind(sep);
  if (i == std::string::npos)
    return false;
  if (prefix)
    *prefix = name.substr(0, i);
  if (suffix)
    *suffix = name.substr(i + sep.size());
  return true;
}

static std::string join_path(const std::string &base,
                             const std::string &child) {
  if (base.empty())
    return child;
  if (base.back() == '/')
    return base + child;
  return base + "/" + child;
}

static std::string basename_of(const std::string &path) {
  auto pos = path.find_last_of('/');
  if (pos == std::string::npos)
    return path;
  return path.substr(pos + 1);
}

static bool starts_with(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

static std::vector<std::string> list_h5_files(const std::string &root) {
  std::vector<std::string> files;
  std::vector<std::string> stack{root};

  while (!stack.empty()) {
    std::string dir = stack.back();
    stack.pop_back();

    DIR *dp = opendir(dir.c_str());
    if (!dp)
      throw std::runtime_error(std::string("Failed to open directory: ") + dir);

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
      std::string name = de->d_name;
      if (name == "." || name == "..")
        continue;

      std::string path = join_path(dir, name);
      struct stat s = {};
      if (stat(path.c_str(), &s) < 0)
        continue;

      if (S_ISDIR(s.st_mode)) {
        stack.push_back(path);
      } else if (path.size() > 3 && path.rfind(".h5") == path.size() - 3) {
        files.push_back(path);
      }
    }
    closedir(dp);
  }

  std::sort(files.begin(), files.end());
  return files;
}

struct ColumnSpec {
  pvxs::TypeCode type_code;
  std::string name;
  std::string label;

  ColumnSpec(pvxs::TypeCode type_code, std::string name, std::string label)
      : type_code(type_code), name(std::move(name)), label(std::move(label)) {}
};

struct SnapshotLayout {
  std::string input_pv;
  std::vector<std::string> pvnames;
  std::vector<std::string> prefixes;
  std::vector<std::string> columns;
  std::vector<std::string> labels;
  std::vector<uint8_t> types;
  std::vector<ColumnSpec> specs;
};

struct SnapshotEvent {
  std::map<std::string, std::vector<std::string>> columns;
};

static std::string json_escape(const std::string &input) {
  std::ostringstream out;
  for (char c : input) {
    switch (c) {
    case '\\':
      out << "\\\\";
      break;
    case '"':
      out << "\\\"";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << c;
      break;
    }
  }
  return out.str();
}

static std::string join_strings(const std::vector<std::string> &values,
                                const char *sep) {
  std::ostringstream out;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i)
      out << sep;
    out << values[i];
  }
  return out.str();
}

static std::vector<std::string> split_simple(const std::string &text,
                                             char sep) {
  std::vector<std::string> out;
  std::string current;
  std::istringstream in(text);
  while (std::getline(in, current, sep))
    out.push_back(current);
  return out;
}

static SnapshotLayout read_layout(const H5::File &file) {
  SnapshotLayout layout;

  auto meta = file.getGroup(META_GROUP);
  meta.getDataSet(META_PVNAMES).read(layout.pvnames);
  meta.getDataSet(META_COLUMN_PREFIXES).read(layout.prefixes);
  meta.getDataSet(META_COLUMNS).read(layout.columns);
  meta.getDataSet(META_LABELS).read(layout.labels);
  meta.getDataSet(META_TYPES).read(layout.types);

  if (layout.columns.size() != layout.labels.size() ||
      layout.columns.size() != layout.types.size())
    throw std::runtime_error(
        "Snapshot metadata arrays have inconsistent lengths");

  for (size_t i = 0; i < layout.columns.size(); ++i) {
    pvxs::TypeCode code;
    code.code = static_cast<pvxs::TypeCode::code_t>(layout.types[i]);
    layout.specs.emplace_back(code, layout.columns[i], layout.labels[i]);
  }

  return layout;
}

static std::string expected_dataset_path(const std::string &root_group,
                                         const ColumnSpec &spec,
                                         const std::string &col_sep) {
  if (spec.name == TimeTable::SECONDS_PAST_EPOCH_COL ||
      spec.name == TimeTable::NANOSECONDS_COL ||
      spec.name == TimeTable::PULSE_ID_COL)
    return join_path(join_path(DATA_GROUP, root_group), spec.name);

  std::string prefix;
  std::string suffix;
  if (!parts(spec.name, col_sep, &prefix, &suffix))
    throw std::runtime_error(
        std::string("Invalid column name (must contain '") + col_sep +
        "'): " + spec.name);

  return join_path(join_path(join_path(DATA_GROUP, root_group), prefix),
                   suffix);
}

static std::string serialize_layout_line(const SnapshotLayout &layout) {
  std::ostringstream payload;
  payload << "pvnames=" << join_strings(layout.pvnames, ",")
          << ";prefixes=" << join_strings(layout.prefixes, ",")
          << ";columns=" << join_strings(layout.columns, ",")
          << ";labels=" << join_strings(layout.labels, ",") << ";types=";
  for (size_t i = 0; i < layout.types.size(); ++i) {
    if (i)
      payload << ",";
    payload << static_cast<unsigned int>(layout.types[i]);
  }
  return std::string("{\"type\":\"meta\",\"payload\":\"") +
         json_escape(payload.str()) + "\"}";
}

static std::string
serialize_event_line(const tabulator::TimeTableValue &value) {
  std::ostringstream payload;
  bool first_field = true;
  for (const auto &col : value.type.columns) {
    if (!first_field)
      payload << ";";
    first_field = false;
    payload << col.name << "=";

    if (col.type_code == pvxs::TypeCode::StringA) {
      auto strings = value.get_column_as<std::string>(col.name);
      for (size_t i = 0; i < strings.size(); ++i) {
        if (i)
          payload << ",";
        payload << json_escape(strings[i]);
      }
    } else if (col.type_code == pvxs::TypeCode::BoolA) {
      auto vals = value.get_column_as<uint8_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << static_cast<unsigned int>(vals[i]);
      }
    } else if (col.type_code == pvxs::TypeCode::Int8A) {
      auto vals = value.get_column_as<int8_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << static_cast<int>(vals[i]);
      }
    } else if (col.type_code == pvxs::TypeCode::Int16A) {
      auto vals = value.get_column_as<int16_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << vals[i];
      }
    } else if (col.type_code == pvxs::TypeCode::Int32A) {
      auto vals = value.get_column_as<int32_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << vals[i];
      }
    } else if (col.type_code == pvxs::TypeCode::Int64A) {
      auto vals = value.get_column_as<int64_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << vals[i];
      }
    } else if (col.type_code == pvxs::TypeCode::UInt8A) {
      auto vals = value.get_column_as<uint8_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << static_cast<unsigned int>(vals[i]);
      }
    } else if (col.type_code == pvxs::TypeCode::UInt16A) {
      auto vals = value.get_column_as<uint16_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << vals[i];
      }
    } else if (col.type_code == pvxs::TypeCode::UInt32A) {
      auto vals = value.get_column_as<uint32_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << vals[i];
      }
    } else if (col.type_code == pvxs::TypeCode::UInt64A) {
      auto vals = value.get_column_as<uint64_t>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << vals[i];
      }
    } else if (col.type_code == pvxs::TypeCode::Float32A) {
      auto vals = value.get_column_as<float>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << std::setprecision(9) << vals[i];
      }
    } else if (col.type_code == pvxs::TypeCode::Float64A) {
      auto vals = value.get_column_as<double>(col.name);
      for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
          payload << ",";
        payload << std::setprecision(17) << vals[i];
      }
    } else {
      throw std::runtime_error(
          std::string("Unsupported type in snapshot serialize: ") +
          col.type_code.name());
    }
  }
  return std::string("{\"type\":\"event\",\"payload\":\"") +
         json_escape(payload.str()) + "\"}";
}

static std::vector<std::string> parse_list(const std::string &value, char sep) {
  return split_simple(value, sep);
}

static std::vector<std::string> parse_value_list(const std::string &value) {
  return split_simple(value, ',');
}

static std::map<std::string, std::string>
parse_payload_fields(const std::string &payload) {
  std::map<std::string, std::string> fields;
  size_t pos = 0;
  while (pos < payload.size()) {
    auto eq = payload.find('=', pos);
    if (eq == std::string::npos)
      break;
    auto semi = payload.find(';', eq + 1);
    auto key = payload.substr(pos, eq - pos);
    auto val = payload.substr(
        eq + 1, semi == std::string::npos ? std::string::npos : semi - eq - 1);
    fields[key] = val;
    if (semi == std::string::npos)
      break;
    pos = semi + 1;
  }
  return fields;
}

static std::string extract_json_string_field(const std::string &line,
                                             const std::string &field) {
  auto key = std::string("\"") + field + "\":\"";
  auto pos = line.find(key);
  if (pos == std::string::npos)
    throw std::runtime_error("Malformed snapshot line");
  pos += key.size();
  std::string out;
  bool escape = false;
  for (; pos < line.size(); ++pos) {
    char c = line[pos];
    if (escape) {
      switch (c) {
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case '"':
        out.push_back('"');
        break;
      default:
        out.push_back(c);
        break;
      }
      escape = false;
    } else if (c == '\\') {
      escape = true;
    } else if (c == '"') {
      break;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

static SnapshotLayout
snapshot_layout_from_meta_payload(const std::string &payload) {
  SnapshotLayout layout;
  auto fields = parse_payload_fields(payload);
  layout.pvnames = parse_list(fields["pvnames"], ',');
  layout.prefixes = parse_list(fields["prefixes"], ',');
  layout.columns = parse_list(fields["columns"], ',');
  layout.labels = parse_list(fields["labels"], ',');
  auto type_strings = parse_list(fields["types"], ',');
  for (const auto &type_string : type_strings) {
    if (type_string.empty())
      continue;
    layout.types.push_back(static_cast<uint8_t>(std::stoul(type_string)));
  }
  for (size_t i = 0; i < layout.columns.size(); ++i) {
    pvxs::TypeCode code;
    code.code = static_cast<pvxs::TypeCode::code_t>(layout.types[i]);
    layout.specs.emplace_back(code, layout.columns[i], layout.labels[i]);
  }
  return layout;
}

class SnapshotWriter {
private:
  std::ofstream out_;
  std::unique_ptr<tabulator::TimeTable> type_;

  void build_file_structure() {
    SnapshotLayout layout;
    layout.input_pv = "";
    for (auto c : type_->columns) {
      layout.columns.push_back(c.name);
      layout.labels.push_back(c.label);
      layout.types.push_back(static_cast<uint8_t>(c.type_code.code));
    }
    for (auto c : type_->data_columns) {
      std::string pvname;
      std::string prefix;
      if (!parts(c.label, ".", &pvname, NULL))
        throw std::runtime_error(std::string("Invalid label name: ") + c.label);
      if (!parts(c.name, "_", &prefix, NULL))
        throw std::runtime_error(std::string("Invalid column name: ") + c.name);
      if (std::find(layout.pvnames.begin(), layout.pvnames.end(), pvname) ==
          layout.pvnames.end()) {
        layout.pvnames.push_back(pvname);
        layout.prefixes.push_back(prefix);
      }
    }
    out_ << serialize_layout_line(layout) << "\n";
  }

public:
  SnapshotWriter(const std::string &snapshot_file,
                 const tabulator::TimeTable &type)
      : out_(snapshot_file), type_(new tabulator::TimeTable(type)) {
    if (!out_)
      throw std::runtime_error("Failed to open snapshot file: " +
                               snapshot_file);
    build_file_structure();
  }

  void append(const tabulator::TimeTableValue &value) {
    auto tvalue = type_->wrap(value.get(), true);
    out_ << serialize_event_line(tvalue) << "\n";
    out_.flush();
  }
};

template <typename T> static std::string to_string_exact(T value) {
  std::ostringstream out;
  out << std::setprecision(17) << value;
  return out.str();
}

template <typename T>
static std::vector<T> read_vector(const H5::DataSet &dataset) {
  std::vector<T> data;
  dataset.read(data);
  return data;
}

static std::vector<std::string> column_to_strings(const H5::DataSet &dataset,
                                                  pvxs::TypeCode code) {
  switch (code.code) {
  case pvxs::TypeCode::BoolA: {
    auto v = read_vector<uint8_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(x ? "1" : "0");
    return out;
  }
  case pvxs::TypeCode::Int8A: {
    auto v = read_vector<int8_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(static_cast<int>(x)));
    return out;
  }
  case pvxs::TypeCode::Int16A: {
    auto v = read_vector<int16_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(x));
    return out;
  }
  case pvxs::TypeCode::Int32A: {
    auto v = read_vector<int32_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(x));
    return out;
  }
  case pvxs::TypeCode::Int64A: {
    auto v = read_vector<int64_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(x));
    return out;
  }
  case pvxs::TypeCode::UInt8A: {
    auto v = read_vector<uint8_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(static_cast<unsigned int>(x)));
    return out;
  }
  case pvxs::TypeCode::UInt16A: {
    auto v = read_vector<uint16_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(x));
    return out;
  }
  case pvxs::TypeCode::UInt32A: {
    auto v = read_vector<uint32_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(x));
    return out;
  }
  case pvxs::TypeCode::UInt64A: {
    auto v = read_vector<uint64_t>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(x));
    return out;
  }
  case pvxs::TypeCode::Float32A: {
    auto v = read_vector<float>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(x));
    return out;
  }
  case pvxs::TypeCode::Float64A: {
    auto v = read_vector<double>(dataset);
    std::vector<std::string> out;
    for (auto x : v)
      out.push_back(to_string_exact(x));
    return out;
  }
  case pvxs::TypeCode::StringA: {
    return read_vector<std::string>(dataset);
  }
  default:
    throw std::runtime_error(std::string("Unsupported dataset type: ") +
                             code.name());
  }
}

static std::map<std::string, std::vector<std::string>>
read_actual_columns(const std::string &actual_file,
                    const SnapshotLayout &layout, const std::string &root_group,
                    const std::string &col_sep) {
  H5::File actual(actual_file, H5::File::ReadOnly);
  auto actual_layout = read_layout(actual);

  if (layout.columns != actual_layout.columns)
    throw std::runtime_error(
        "Column names differ between snapshot and writer file");
  if (layout.labels != actual_layout.labels)
    throw std::runtime_error(
        "Column labels differ between snapshot and writer file");
  if (layout.types != actual_layout.types)
    throw std::runtime_error(
        "Column types differ between snapshot and writer file");

  std::map<std::string, std::vector<std::string>> out;
  for (const auto &spec : actual_layout.specs) {
    auto path = expected_dataset_path(root_group, spec, col_sep);
    out[spec.name] = column_to_strings(actual.getDataSet(path), spec.type_code);
  }
  return out;
}

static int run_capture(const std::string &input_pv,
                       const std::string &snapshot_file, double timeout_sec) {
  std::mutex lock;
  std::condition_variable cv;
  std::atomic<bool> have_value(false);
  std::unique_ptr<SnapshotWriter> snapshot;
  std::unique_ptr<tabulator::TimeTable> type;

  pvxs::client::Context client(pvxs::client::Context::fromEnv());
  auto sub = client.monitor(input_pv)
                 .record("pipeline", false)
                 .record("queueSize", 8u)
                 .record("ackAny", 1u)
                 .event([&cv, &have_value](pvxs::client::Subscription &) {
                   have_value.store(true);
                   cv.notify_one();
                 })
                 .maskDisconnected(false)
                 .exec();

  std::unique_lock<std::mutex> guard(lock);
  auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(static_cast<int>(timeout_sec * 1000.0));

  while (!g_stop_requested.load()) {
    try {
      if (!cv.wait_for(guard, std::chrono::milliseconds(250), [&have_value]() {
            return have_value.load() || g_stop_requested.load();
          })) {
        if (std::chrono::steady_clock::now() > deadline)
          throw std::runtime_error("Timed out waiting for merged PV event");
        continue;
      }

      if (g_stop_requested.load())
        break;

      bool saw_value = false;
      while (auto value = sub->pop()) {
        if (!value)
          continue;

        saw_value = true;

        if (!type) {
          type.reset(new tabulator::TimeTable(value));
          snapshot.reset(new SnapshotWriter(snapshot_file, *type));
        }

        snapshot->append(type->wrap(value, true));
      }

      if (saw_value)
        deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<int>(timeout_sec * 1000.0));

      if (!saw_value && g_stop_requested.load())
        break;

      have_value.store(false);
    } catch (pvxs::client::Disconnect &) {
      break;
    }
  }

  if (!snapshot)
    throw std::runtime_error("Capture mode exited without a merged PV value");

  while (!g_stop_requested.load())
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

  return 0;
}

static int run_verify(const std::string &snapshot_file,
                      const std::string &base_directory,
                      const std::string &file_prefix,
                      const std::string &root_group,
                      const std::string &col_sep) {
  struct stat snapshot_stat = {};
  if (stat(snapshot_file.c_str(), &snapshot_stat) < 0)
    throw std::runtime_error(std::string("Missing snapshot file: ") +
                             snapshot_file);

  SnapshotLayout layout;
  std::vector<SnapshotEvent> events;
  {
    std::ifstream in(snapshot_file);
    if (!in)
      throw std::runtime_error("Failed to open snapshot file: " +
                               snapshot_file);
    std::string line;
    while (std::getline(in, line)) {
      if (line.find("\"type\":\"meta\"") != std::string::npos) {
        layout = snapshot_layout_from_meta_payload(
            extract_json_string_field(line, "payload"));
      } else if (line.find("\"type\":\"event\"") != std::string::npos) {
        SnapshotEvent ev;
        auto payload = extract_json_string_field(line, "payload");
        auto fields = parse_payload_fields(payload);
        for (const auto &entry : fields)
          ev.columns[entry.first] = parse_value_list(entry.second);
        events.push_back(std::move(ev));
      }
    }
  }

  if (events.empty())
    throw std::runtime_error("Snapshot file contains no events");

  auto files = list_h5_files(base_directory);
  std::vector<std::string> filtered;
  for (const auto &file : files) {
    auto base = basename_of(file);
    if (file_prefix.empty() || starts_with(base, file_prefix))
      filtered.push_back(file);
  }

  if (filtered.empty())
    throw std::runtime_error("No HDF5 files found for verification");

  for (const auto &file : filtered) {
    auto actual_columns =
        read_actual_columns(file, layout, root_group, col_sep);
    size_t offset = 0;
    for (const auto &event : events) {
      size_t event_rows = std::numeric_limits<size_t>::max();
      for (const auto &spec : layout.specs) {
        auto it = event.columns.find(spec.name);
        if (it == event.columns.end())
          throw std::runtime_error("Missing column in snapshot event: " +
                                   spec.name);
        const auto &expected_vals = it->second;
        const auto &actual_vals = actual_columns.at(spec.name);
        if (event_rows == std::numeric_limits<size_t>::max())
          event_rows = expected_vals.size();
        else if (event_rows != expected_vals.size())
          throw std::runtime_error(
              "Snapshot event has inconsistent column lengths for column: " +
              spec.name);
        if (offset + expected_vals.size() > actual_vals.size())
          throw std::runtime_error(
              "Writer file is shorter than snapshot for column: " + spec.name);
        for (size_t i = 0; i < expected_vals.size(); ++i) {
          if (expected_vals[i] != actual_vals[offset + i]) {
            throw std::runtime_error("Value mismatch for column " + spec.name);
          }
        }
      }
      if (event_rows == std::numeric_limits<size_t>::max())
        throw std::runtime_error("Snapshot event has no columns");
      offset += event_rows;
    }
    const auto &first_column = actual_columns.begin()->second;
    if (offset != first_column.size())
      throw std::runtime_error(
          "Snapshot row count does not match writer output");
  }

  return 0;
}

} // namespace tabulator

int main(int argc, char *argv[]) {
  pvxs::logger_config_env();

  std::signal(SIGINT, tabulator::on_signal);
  std::signal(SIGTERM, tabulator::on_signal);

  std::string mode;
  std::string input_pv = "SIM:MERGED";
  std::string snapshot_file;
  std::string base_directory;
  std::string file_prefix = "bsas";
  std::string root_group = "data";
  std::string col_sep = "_";
  double timeout_sec = 30.0;

  auto cli =
      (clipp::required("--mode").doc("Operation mode: capture or verify") &
           clipp::value("mode", mode),

       clipp::option("--input-pv").doc("Name of the merged PV to capture") &
           clipp::value("input_pv", input_pv),

       clipp::option("--snapshot-file")
               .doc("Path to the temporary JSONL snapshot file") &
           clipp::value("snapshot_file", snapshot_file),

       clipp::option("--base-directory")
               .doc("Root directory containing writer HDF5 output") &
           clipp::value("base_directory", base_directory),

       clipp::option("--file-prefix").doc("File prefix used by the writer") &
           clipp::value("file_prefix", file_prefix),

       clipp::option("--column-sep")
               .doc(
                   "Separator between PV identifier and original column name") &
           clipp::value("col_sep", col_sep),

       clipp::option("--timeout-sec")
               .doc("Timeout while waiting for a merged PV snapshot") &
           clipp::value("timeout_sec", timeout_sec));

  std::stringstream ss;
  ss << clipp::make_man_page(cli, argv[0]);
  auto man_page = ss.str();

  if (!clipp::parse(argc, argv, cli)) {
    fputs(man_page.c_str(), stderr);
    return 1;
  }

  if (mode.empty()) {
    fputs(man_page.c_str(), stderr);
    return 1;
  }

  try {
    if (mode == "capture") {
      if (snapshot_file.empty())
        throw std::runtime_error("--snapshot-file is required in capture mode");
      return tabulator::run_capture(input_pv, snapshot_file, timeout_sec);
    }

    if (mode == "verify") {
      if (snapshot_file.empty())
        throw std::runtime_error("--snapshot-file is required in verify mode");
      if (base_directory.empty())
        throw std::runtime_error("--base-directory is required in verify mode");
      return tabulator::run_verify(snapshot_file, base_directory, file_prefix,
                                   root_group, col_sep);
    }

    throw std::runtime_error(std::string("Unknown mode: ") + mode);
  } catch (const std::exception &ex) {
    log_err_printf(LOG, "Exception: %s\n", ex.what());
    return 1;
  }
}

#include <algorithm>
#include <atomic>
#include <cctype>
#include <csignal>
#include <condition_variable>
#include <chrono>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
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

static const std::string ATTR_INPUT_PV = "Input PV";
static const std::string ATTR_SIGNAL = "Signal";
static const std::string ATTR_LABEL = "NTTable label";
static const std::string ATTR_COLUMN = "NTTable column";

static const char *DATA_GROUP = "/data";

static std::atomic<bool> g_stop_requested(false);

static void on_signal(int) {
    g_stop_requested.store(true);
}

static bool parts(const std::string &name, const std::string &sep, std::string *prefix, std::string *suffix) {
    auto i = name.rfind(sep);
    if (i == std::string::npos)
        return false;
    if (prefix)
        *prefix = name.substr(0, i);
    if (suffix)
        *suffix = name.substr(i + sep.size());
    return true;
}

static H5::DataType pvxs_to_h5_type(pvxs::TypeCode t) {
    switch(t.code) {
        #define CASE(PT,HT) case pvxs::TypeCode::PT: return H5::create_datatype<HT>()
        CASE(Bool,     bool);
        CASE(BoolA,    bool);
        CASE(Int8,     int8_t);
        CASE(Int16,    int16_t);
        CASE(Int32,    int32_t);
        CASE(Int64,    int64_t);
        CASE(UInt8,    uint8_t);
        CASE(UInt16,   uint16_t);
        CASE(UInt32,   uint32_t);
        CASE(UInt64,   uint64_t);
        CASE(Int8A,    int8_t);
        CASE(Int16A,   int16_t);
        CASE(Int32A,   int32_t);
        CASE(Int64A,   int64_t);
        CASE(UInt8A,   uint8_t);
        CASE(UInt16A,  uint16_t);
        CASE(UInt32A,  uint32_t);
        CASE(UInt64A,  uint64_t);
        CASE(Float32,  float);
        CASE(Float64,  double);
        CASE(Float32A, float);
        CASE(Float64A, double);
        CASE(String,   std::string);
        CASE(StringA,  std::string);
        #undef CASE
        default:
            throw std::runtime_error(std::string("Can't map pvxs type to hdf5 type: ") + t.name());
    }
}

static std::string join_path(const std::string &base, const std::string &child) {
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
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static std::vector<std::string> list_h5_files(const std::string &root) {
    std::vector<std::string> files;
    std::vector<std::string> stack{root};

    while(!stack.empty()) {
        std::string dir = stack.back();
        stack.pop_back();

        DIR *dp = opendir(dir.c_str());
        if (!dp)
            throw std::runtime_error(std::string("Failed to open directory: ") + dir);

        struct dirent *de;
        while((de = readdir(dp)) != NULL) {
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
    : type_code(type_code), name(std::move(name)), label(std::move(label))
    {}
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

static SnapshotLayout read_layout(const H5::File &file) {
    SnapshotLayout layout;

    auto meta = file.getGroup(META_GROUP);
    layout.pvnames = meta.getDataSet(META_PVNAMES).read<std::vector<std::string>>();
    layout.prefixes = meta.getDataSet(META_COLUMN_PREFIXES).read<std::vector<std::string>>();
    layout.columns = meta.getDataSet(META_COLUMNS).read<std::vector<std::string>>();
    layout.labels = meta.getDataSet(META_LABELS).read<std::vector<std::string>>();
    layout.types = meta.getDataSet(META_TYPES).read<std::vector<uint8_t>>();

    if (layout.columns.size() != layout.labels.size() || layout.columns.size() != layout.types.size())
        throw std::runtime_error("Snapshot metadata arrays have inconsistent lengths");

    for (size_t i = 0; i < layout.columns.size(); ++i) {
        pvxs::TypeCode code;
        code.code = layout.types[i];
        layout.specs.emplace_back(code, layout.columns[i], layout.labels[i]);
    }

    return layout;
}

static std::string expected_dataset_path(const std::string &root_group, const ColumnSpec &spec, const std::string &col_sep) {
    if (spec.name == TimeTable::SECONDS_PAST_EPOCH_COL ||
        spec.name == TimeTable::NANOSECONDS_COL ||
        spec.name == TimeTable::PULSE_ID_COL)
        return join_path(join_path(DATA_GROUP, root_group), spec.name);

    std::string prefix;
    std::string suffix;
    if (!parts(spec.name, col_sep, &prefix, &suffix))
        throw std::runtime_error(std::string("Invalid column name (must contain '") + col_sep + "'): " + spec.name);

    return join_path(join_path(join_path(DATA_GROUP, root_group), prefix), suffix);
}

template<typename T>
static std::vector<T> read_vector(const H5::DataSet &dataset) {
    return dataset.read<std::vector<T>>();
}

template<typename T>
static void compare_vectors(const std::vector<T> &expected, const std::vector<T> &actual, const std::string &what) {
    if (expected.size() != actual.size())
        throw std::runtime_error(what + ": size mismatch");
    if (!std::equal(expected.begin(), expected.end(), actual.begin()))
        throw std::runtime_error(what + ": value mismatch");
}

static void compare_dataset(const H5::DataSet &expected_ds, const H5::DataSet &actual_ds, const std::string &path, pvxs::TypeCode code) {
    switch(code.code) {
        case pvxs::TypeCode::BoolA:
            compare_vectors(read_vector<bool>(expected_ds), read_vector<bool>(actual_ds), path);
            break;
        case pvxs::TypeCode::Int8A:
            compare_vectors(read_vector<int8_t>(expected_ds), read_vector<int8_t>(actual_ds), path);
            break;
        case pvxs::TypeCode::Int16A:
            compare_vectors(read_vector<int16_t>(expected_ds), read_vector<int16_t>(actual_ds), path);
            break;
        case pvxs::TypeCode::Int32A:
            compare_vectors(read_vector<int32_t>(expected_ds), read_vector<int32_t>(actual_ds), path);
            break;
        case pvxs::TypeCode::Int64A:
            compare_vectors(read_vector<int64_t>(expected_ds), read_vector<int64_t>(actual_ds), path);
            break;
        case pvxs::TypeCode::UInt8A:
            compare_vectors(read_vector<uint8_t>(expected_ds), read_vector<uint8_t>(actual_ds), path);
            break;
        case pvxs::TypeCode::UInt16A:
            compare_vectors(read_vector<uint16_t>(expected_ds), read_vector<uint16_t>(actual_ds), path);
            break;
        case pvxs::TypeCode::UInt32A:
            compare_vectors(read_vector<uint32_t>(expected_ds), read_vector<uint32_t>(actual_ds), path);
            break;
        case pvxs::TypeCode::UInt64A:
            compare_vectors(read_vector<uint64_t>(expected_ds), read_vector<uint64_t>(actual_ds), path);
            break;
        case pvxs::TypeCode::Float32A:
            compare_vectors(read_vector<float>(expected_ds), read_vector<float>(actual_ds), path);
            break;
        case pvxs::TypeCode::Float64A:
            compare_vectors(read_vector<double>(expected_ds), read_vector<double>(actual_ds), path);
            break;
        case pvxs::TypeCode::StringA:
            compare_vectors(read_vector<std::string>(expected_ds), read_vector<std::string>(actual_ds), path);
            break;
        default:
            throw std::runtime_error(std::string("Unsupported dataset type for compare: ") + code.name());
    }
}

class SnapshotWriter {
private:
    std::string snapshot_file_;
    std::string input_pv_;
    std::string root_group_;
    std::string label_sep_;
    std::string col_sep_;
    std::unique_ptr<H5::File> file_;
    std::unique_ptr<tabulator::TimeTable> type_;
    std::map<std::string, H5::DataSet> datasets_;
    std::set<std::string> pvnames_seen_;
    std::vector<std::string> pvnames_;
    std::vector<std::string> prefixes_;
    std::vector<std::string> columns_;
    std::vector<std::string> labels_;
    std::vector<uint8_t> types_;
    bool initialized_;

    template<typename T>
    void append_dataset(H5::DataSet &ds, const std::string &name, const T &data) {
        auto dims = ds.getDimensions();
        dims[0] += data.size();
        ds.resize(dims);
        ds.select({dims[0] - data.size()}, {data.size()}).write_raw(data.dataPtr().get());
    }

    void build_file_structure() {
        file_->createAttribute(ATTR_INPUT_PV, input_pv_);

        H5::DataSetCreateProps props;
        props.add(H5::Chunking({1u}));

        auto meta = file_->createGroup(META_GROUP);
        auto data_group = file_->createGroup(DATA_GROUP);
        auto root = data_group.createGroup(root_group_);

        for (auto c : type_->columns) {
            columns_.push_back(c.name);
            labels_.push_back(c.label);
            types_.push_back(c.type_code.code);
        }

        for (auto c : type_->time_columns) {
            auto ds = root.createDataSet(c.name, H5::DataSpace({0}, {H5::DataSpace::UNLIMITED}), pvxs_to_h5_type(c.type_code), props);
            ds.createAttribute(ATTR_LABEL, c.label);
            ds.createAttribute(ATTR_COLUMN, c.name);
            datasets_.emplace(c.name, ds);
        }

        for (auto c : type_->data_columns) {
            std::string pvname;
            std::string prefix;
            std::string suffix;
            if (!parts(c.label, label_sep_, &pvname, NULL))
                throw std::runtime_error(std::string("Invalid label name (must contain '") + label_sep_ + "'): " + c.label);
            if (!parts(c.name, col_sep_, &prefix, &suffix))
                throw std::runtime_error(std::string("Invalid column name (must contain '") + col_sep_ + "'): " + c.name);

            if (pvnames_seen_.insert(pvname).second) {
                pvnames_.push_back(pvname);
                prefixes_.push_back(prefix);
                auto g = root.createGroup(prefix);
                g.createAttribute(ATTR_SIGNAL, pvname);
            }

            auto group = root.getGroup(prefix);
            auto ds = group.createDataSet(suffix, H5::DataSpace({0}, {H5::DataSpace::UNLIMITED}), pvxs_to_h5_type(c.type_code), props);
            ds.createAttribute(ATTR_LABEL, c.label);
            ds.createAttribute(ATTR_COLUMN, c.name);
            datasets_.emplace(c.name, ds);
        }

        meta.createDataSet(META_PVNAMES, pvnames_);
        meta.createDataSet(META_COLUMN_PREFIXES, prefixes_);
        meta.createDataSet(META_COLUMNS, columns_);
        meta.createDataSet(META_LABELS, labels_);
        meta.createDataSet(META_TYPES, types_);

        initialized_ = true;
    }

public:
    SnapshotWriter(const std::string &snapshot_file, const std::string &input_pv, const std::string &root_group,
        const std::string &label_sep, const std::string &col_sep, const tabulator::TimeTable &type)
    : snapshot_file_(snapshot_file), input_pv_(input_pv), root_group_(root_group), label_sep_(label_sep), col_sep_(col_sep),
      file_(new H5::File(snapshot_file, H5::File::Overwrite)), type_(new tabulator::TimeTable(type)),
      datasets_(), pvnames_seen_(), pvnames_(), prefixes_(), columns_(), labels_(), types_(), initialized_(false)
    {
        build_file_structure();
    }

    void append(const tabulator::TimeTableValue &value) {
        if (!initialized_)
            throw std::logic_error("SnapshotWriter not initialized");

        auto tvalue = type_->wrap(value.get(), true);

        for (auto c : type_->columns) {
            auto ds = datasets_.find(c.name);
            if (ds == datasets_.end())
                throw std::logic_error(std::string("Can't find snapshot dataset: ") + c.name);

            switch (c.type_code.code) {
                #define CASE(PT, T) case pvxs::TypeCode::PT: append_dataset<T>(ds->second, c.name, tvalue.get_column_as<T>(c.name)); break
                CASE(BoolA,    bool);
                CASE(Int8A,    int8_t);
                CASE(Int16A,   int16_t);
                CASE(Int32A,   int32_t);
                CASE(Int64A,   int64_t);
                CASE(UInt8A,   uint8_t);
                CASE(UInt16A,  uint16_t);
                CASE(UInt32A,  uint32_t);
                CASE(UInt64A,  uint64_t);
                CASE(Float32A, float);
                CASE(Float64A, double);
                CASE(StringA,  std::string);
                #undef CASE
                default:
                    throw std::runtime_error(std::string("Unsupported type in snapshot append: ") + c.type_code.name());
            }
        }

        file_->flush();
    }
};
}

static void ensure_same_layout(const SnapshotLayout &expected, const SnapshotLayout &actual) {
    if (expected.pvnames != actual.pvnames)
        throw std::runtime_error("Snapshot and file pvname lists differ");
    if (expected.prefixes != actual.prefixes)
        throw std::runtime_error("Snapshot and file prefix lists differ");
    if (expected.columns != actual.columns)
        throw std::runtime_error("Snapshot and file column lists differ");
    if (expected.labels != actual.labels)
        throw std::runtime_error("Snapshot and file label lists differ");
    if (expected.types != actual.types)
        throw std::runtime_error("Snapshot and file type lists differ");
}

static void compare_hdf5_files(const std::string &snapshot_file, const std::string &actual_file,
    const std::string &root_group, const std::string &col_sep)
{
    H5::File expected(snapshot_file, H5::File::ReadOnly);
    H5::File actual(actual_file, H5::File::ReadOnly);

    auto expected_layout = read_layout(expected);
    auto actual_layout = read_layout(actual);
    ensure_same_layout(expected_layout, actual_layout);

    for (const auto &spec : expected_layout.specs) {
        std::string path = expected_dataset_path(root_group, spec, col_sep);
        auto expected_ds = expected.getDataSet(path);
        auto actual_ds = actual.getDataSet(path);
        compare_dataset(expected_ds, actual_ds, path, spec.type_code);
    }
}

static int run_capture(const std::string &input_pv, const std::string &snapshot_file, const std::string &root_group,
    const std::string &label_sep, const std::string &col_sep, double timeout_sec)
{
    std::mutex lock;
    std::condition_variable cv;
    std::atomic<bool> have_value(false);
    std::unique_ptr<SnapshotWriter> snapshot;
    std::unique_ptr<tabulator::TimeTable> type;

    pvxs::client::Context client(pvxs::client::Context::fromEnv());
    auto sub = client
        .monitor(input_pv)
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
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout_sec * 1000.0));

    while(!g_stop_requested.load()) {
        if (!cv.wait_for(guard, std::chrono::milliseconds(250), [&have_value]() { return have_value.load() || g_stop_requested.load(); })) {
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
                snapshot.reset(new SnapshotWriter(snapshot_file, input_pv, root_group, label_sep, col_sep, *type));
            }

            snapshot->append(type->wrap(value, true));
        }

        if (saw_value)
            deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(timeout_sec * 1000.0));

        if (!saw_value && g_stop_requested.load())
            break;

        have_value.store(false);
    }

    if (!snapshot)
        throw std::runtime_error("Capture mode exited without a merged PV value");

    while(!g_stop_requested.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return 0;
}

static int run_verify(const std::string &snapshot_file, const std::string &base_directory,
    const std::string &file_prefix, const std::string &root_group, const std::string &col_sep)
{
    struct stat snapshot_stat = {};
    if (stat(snapshot_file.c_str(), &snapshot_stat) < 0)
        throw std::runtime_error(std::string("Missing snapshot file: ") + snapshot_file);

    auto files = list_h5_files(base_directory);
    std::vector<std::string> filtered;
    for (const auto &file : files) {
        auto base = basename_of(file);
        if (file_prefix.empty() || (starts_with(base, file_prefix) && base.size() > 3 && base.rfind(".h5") == base.size() - 3))
            filtered.push_back(file);
    }

    if (filtered.empty())
        throw std::runtime_error("No HDF5 files found for verification");

    for (const auto &file : filtered)
        compare_hdf5_files(snapshot_file, file, root_group, col_sep);

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
    std::string label_sep = ".";
    std::string col_sep = "_";
    double timeout_sec = 30.0;

    auto cli = (
        clipp::required("--mode")
            .doc("Operation mode: capture or verify")
            & clipp::value("mode", mode),

        clipp::option("--input-pv")
            .doc("Name of the merged PV to capture")
            & clipp::value("input_pv", input_pv),

        clipp::option("--snapshot-file")
            .doc("Path to the temporary HDF5 snapshot file")
            & clipp::value("snapshot_file", snapshot_file),

        clipp::option("--base-directory")
            .doc("Root directory containing writer HDF5 output")
            & clipp::value("base_directory", base_directory),

        clipp::option("--file-prefix")
            .doc("File prefix used by the writer")
            & clipp::value("file_prefix", file_prefix),

        clipp::option("--root-group")
            .doc("Root group used in the HDF5 layout")
            & clipp::value("root_group", root_group),

        clipp::option("--label-sep")
            .doc("Separator between PV name and column name in labels")
            & clipp::value("label_sep", label_sep),

        clipp::option("--column-sep")
            .doc("Separator between PV identifier and original column name")
            & clipp::value("col_sep", col_sep),

        clipp::option("--timeout-sec")
            .doc("Timeout while waiting for a merged PV snapshot")
            & clipp::value("timeout_sec", timeout_sec)
    );

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
            return tabulator::run_capture(input_pv, snapshot_file, root_group, label_sep, col_sep, timeout_sec);
        }

        if (mode == "verify") {
            if (snapshot_file.empty())
                throw std::runtime_error("--snapshot-file is required in verify mode");
            if (base_directory.empty())
                throw std::runtime_error("--base-directory is required in verify mode");
            return tabulator::run_verify(snapshot_file, base_directory, file_prefix, root_group, col_sep);
        }

        throw std::runtime_error(std::string("Unknown mode: ") + mode);
    } catch(const std::exception &ex) {
        log_err_printf(LOG, "Exception: %s\n", ex.what());
        return 1;
    }
}

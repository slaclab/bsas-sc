#include <registryFunction.h>
#include <epicsExport.h>
#include <aSubRecord.h>
#include <menuFtype.h>
#include <errlog.h>
#include <dbAccess.h>
#include <alarm.h>
#include <devSup.h>
#include <epicsVersion.h>
#include <epicsStdio.h>
#include <initHooks.h>

#include <vector>
#include <iostream>
#include <random>
#include <cmath>
#include <string>

#include <pvxs/server.h>
#include <pvxs/sharedpv.h>
#include <pvxs/log.h>
#include <pvxs/iochooks.h>
#include <pvxs/nt.h>

#include <tab/nttable.h>
#include <tab/timetable.h>

#define PI 3.14159265

DEFINE_LOGGER(LOG, "sim");

namespace {
struct PendingPV {
    std::string name;
    pvxs::server::SharedPV pv;
};

std::vector<PendingPV> pending_pvs;
bool hook_registered = false;

void registerPendingPVs(initHookState state) {
    if(state != initHookAfterCaServerRunning)
        return;

    for(auto & req : pending_pvs)
        pvxs::ioc::server().addPV(req.name, req.pv);

    pending_pvs.clear();
}

void ensurePendingHook() {
    if(!hook_registered) {
        initHookRegister(&registerPendingPVs);
        hook_registered = true;
    }
}
}

using tabulator::nt::NTTable;
using tabulator::TimeTable;
using tabulator::TimeTableScalar;
using tabulator::TimeTableStat;
using tabulator::TimeTableValue;

enum Config : uint8_t {
    TIMESTAMP_UTAG      = 0x01,
    ALARM_SEVERITY      = 0x02,
    ALARM_CONDITION     = 0x04,
    ALARM_MESSAGE       = 0x08,
    STAT                = 0x10,
};

struct SimSource {
    const std::unique_ptr<TimeTable> type;
    virtual TimeTableValue simulate(size_t num_rows) = 0;
    virtual size_t num_samples_per_row() const = 0;
    virtual ~SimSource() {}

    SimSource(std::unique_ptr<TimeTable> && type)
    : type(std::move(type))
    {}
};

// 1 Hz sinusoid
class SimSourceScalar : public SimSource {
private:
    constexpr static const double HIHI = 0.99;
    constexpr static const double HIGH = 0.95;
    constexpr static const double LOW  = -0.95;
    constexpr static const double LOLO = -0.99;

    size_t t_;

public:
    const double step_sec;

    SimSourceScalar(const TimeTableScalar::Config & columns, double step_sec)
    : SimSource(std::unique_ptr<TimeTable>(new TimeTableScalar(columns))), t_(0), step_sec(step_sec)
    {}

    TimeTableValue simulate(size_t num_rows) {

        auto output = type->create();

        pvxs::shared_array<TimeTableScalar::VALUE_T> value(num_rows);

        for (size_t i = 0; i < num_rows; ++i) {
            value[i] = sin(t_*step_sec*2*PI);
            ++t_;
        }

        const TimeTableScalar::Config *config = &static_cast<const TimeTableScalar*>(type.get())->config;

        // Populate with zeroes for now
        if (config->utag) {
            pvxs::shared_array<const TimeTableScalar::UTAG_T> utag(num_rows, 0);
            output.set_column(TimeTableScalar::UTAG_COL, utag);
        }

        // Hard-coded alarm limits
        if (config->alarm_sev) {
            pvxs::shared_array<TimeTableScalar::ALARM_SEV_T> alarm_sev(num_rows);

            for (size_t i = 0; i < num_rows; ++i) {
                auto v = value[i];

                if (v < LOLO || v > HIHI)
                    alarm_sev[i] = epicsSevMajor;
                else if (v < LOW || v > HIGH)
                    alarm_sev[i] = epicsSevMinor;
                else
                    alarm_sev[i] = epicsSevNone;
            }

            output.set_column(TimeTableScalar::ALARM_SEV_COL, alarm_sev.freeze());
        }

        if (config->alarm_cond) {
            pvxs::shared_array<TimeTableScalar::ALARM_COND_T> alarm_cond(num_rows);

            for (size_t i = 0; i < num_rows; ++i) {
                auto v = value[i];

                if (v < LOLO)
                    alarm_cond[i] = epicsAlarmLoLo;
                else if (v < LOW)
                    alarm_cond[i] = epicsAlarmLow;
                else if (v > HIHI)
                    alarm_cond[i] = epicsAlarmHiHi;
                else if (v > HIGH)
                    alarm_cond[i] = epicsAlarmHigh;
                else
                    alarm_cond[i] = epicsAlarmNone;
            }

            output.set_column(TimeTableScalar::ALARM_COND_COL, alarm_cond.freeze());
        }

        // Populate with empty messages for now
        if (config->alarm_message) {
            pvxs::shared_array<const TimeTableScalar::ALARM_MSG_T> alarm_msg(num_rows, std::string(""));
            output.set_column(TimeTableScalar::ALARM_MSG_COL, alarm_msg);
        }

        output.set_column(TimeTableScalar::VALUE_COL, value.freeze());
        return output;
    }

    size_t num_samples_per_row() const {
        return 1;
    }
};

class SimSourceStat : public SimSource {
private:
    size_t t_;

public:
    const size_t num_samp;
    const double step_sec;

    SimSourceStat(size_t num_samp, double step_sec)
    : SimSource(std::unique_ptr<TimeTable>(new TimeTableStat())), t_(0), num_samp(num_samp), step_sec(step_sec)
    {}

    TimeTableValue simulate(size_t num_rows) {

        pvxs::shared_array<TimeTableStat::VAL_T> val_col(num_rows);
        pvxs::shared_array<TimeTableStat::NUM_SAMP_T> num_samp_col(num_rows, static_cast<TimeTableStat::NUM_SAMP_T>(num_samp));
        pvxs::shared_array<TimeTableStat::MIN_T> min_col(num_rows);
        pvxs::shared_array<TimeTableStat::MAX_T> max_col(num_rows);
        pvxs::shared_array<TimeTableStat::MEAN_T> mean_col(num_rows);
        pvxs::shared_array<TimeTableStat::RMS_T> rms_col(num_rows);

        double sub_step_sec = step_sec / num_samp;

        for (size_t row = 0; row < num_rows; ++row) {
            std::vector<double> fast_samples(num_samp);

            double min = std::numeric_limits<double>::max();
            double max = -std::numeric_limits<double>::max();
            double sum = 0.0;
            double sumsq = 0.0;

            for (size_t i = 0; i < num_samp; ++i) {
                double sample = fast_samples[i] = sin((t_*step_sec + i*sub_step_sec)*2*PI);
                min = std::min(min, sample);
                max = std::max(max, sample);
                sum += sample;
                sumsq = std::pow(sample, 2);
            }

            double mean = sum / num_samp;
            double rms = std::sqrt(sumsq / num_samp);

            val_col[row] = mean;
            min_col[row] = min;
            max_col[row] = max;
            mean_col[row] = mean;
            rms_col[row] = rms;

            ++t_;
        }

        auto output = type->create();

        output.set_column(TimeTableStat::VAL_COL, val_col.freeze());
        output.set_column(TimeTableStat::NUM_SAMP_COL, num_samp_col.freeze());
        output.set_column(TimeTableStat::MIN_COL, min_col.freeze());
        output.set_column(TimeTableStat::MAX_COL, max_col.freeze());
        output.set_column(TimeTableStat::MEAN_COL, mean_col.freeze());
        output.set_column(TimeTableStat::RMS_COL, rms_col.freeze());

        return output;
    }

    size_t num_samples_per_row() const {
        return num_samp;
    }
};

static std::string gen_table_name(const std::string & prefix, size_t count, size_t i) {
    int width = ceil(log2(count) / log2(16));
    char name[1024];
    epicsSnprintf(name, sizeof(name), "%s:%0*lX", prefix.c_str(), width, i);
    return std::string(name);
}

static std::vector<std::string> gen_signal_names(size_t table_idx, size_t num_signals) {
    std::vector<std::string> names;

    int width = ceil(log2(num_signals) / log2(16));

    for (size_t i = table_idx*num_signals; i < (table_idx+1)*num_signals; ++i) {
        char name[1024];
        epicsSnprintf(name, sizeof(name), "SIM:SIG:%0*lX", width, i);
        names.emplace_back(name);
    }

    for (auto & name : names)
        log_debug_printf(LOG, "%s(table_idx=%ld, num_signals=%ld) signal='%s'\n", __FUNCTION__, table_idx, num_signals, name.c_str());

    return names;
}

static TimeTable gen_type(const std::vector<std::string> & names, const SimSource & source, const std::string & label_sep, const std::string & col_sep) {
    std::vector<NTTable::ColumnSpec> data_columns;

    size_t sig_idx = 0;
    for (const auto & name : names) {
        log_debug_printf(LOG, "%s(...) generating type for %s\n", __FUNCTION__, name.c_str());
        for (const auto & col : source.type->data_columns) {
            std::string col_name = std::string("pv") + std::to_string(sig_idx) + col_sep + col.label;
            std::string label_name = name + label_sep + col.label;
            data_columns.emplace_back(col.type_code, col_name, label_name);
        }
        ++sig_idx;
    }

    for (const auto & col : data_columns)
        log_debug_printf(LOG, "%s(...)   col.label='%s', col.name='%s'\n", __FUNCTION__, col.label.c_str(), col.name.c_str());

    return TimeTable(data_columns);
}

// Represents a single simulated NTTable, with associated PV
struct Table {
    const std::vector<std::string> signal_names;
    const TimeTable type;
    pvxs::server::SharedPV pv;

    Table(size_t table_idx, size_t num_signals, const SimSource & source, const std::string & label_sep, const std::string & col_sep)
    : signal_names(gen_signal_names(table_idx, num_signals)), type(gen_type(signal_names, source, label_sep, col_sep)),
        pv(pvxs::server::SharedPV::buildReadonly())
    {}
};

static std::vector<Table> gen_tables(size_t num_tables, size_t num_signals, const SimSource & source,
    const std::string & label_sep, const std::string & col_sep)
{
    log_debug_printf(LOG, "%s(num_tables=%ld, num_signals=%ld)\n", __FUNCTION__, num_tables, num_signals);
    std::vector<Table> tables;
    for (size_t table_idx = 0; table_idx < num_tables; ++table_idx) {
        log_debug_printf(LOG, "%s(num_tables=%ld, num_signals=%ld) table_idx=%lu\n", __FUNCTION__, num_tables, num_signals, table_idx);
        tables.emplace_back(table_idx, num_signals, source, label_sep, col_sep);
    }

    return tables;
}

struct SimTables {

    // Configuration
    const std::string name;
    const double step_sec;

    std::unique_ptr<SimSource> source;  // Single "real" source (which means all signals will have the same values at the same timestamp)
    std::vector<Table> tables;

    // State
    epicsTimeStamp ts;
    TimeTable::PULSE_ID_T pulseId; // TODO: pulseId should be part of the epicsTimeStamp once the version of BASE supports it

    SimTables(const char *name, double step_sec, size_t num_tables, size_t num_signals, std::unique_ptr<SimSource> && source,
        const std::string & output_table_prefix, const std::string & label_sep, const std::string & col_sep)
    : name(name), step_sec(step_sec), source(std::move(source)), tables(std::move(gen_tables(num_tables, num_signals, *this->source, label_sep, col_sep)))
    {
        size_t table_idx = 0;
        for (auto & table : tables) {
            auto initial = table.type.create();

            auto pvname = gen_table_name(output_table_prefix, num_tables, table_idx++);
            pending_pvs.push_back({pvname, table.pv});
            table.pv.open(initial.get());
            ensurePendingHook();
        }

        epicsTimeGetCurrent(&ts);
        pulseId = 0;

        log_debug_printf(LOG, "Sim[%s]: initialized %lu output tables\n", name, num_tables);
    }

    void process(size_t num_rows) {
        epicsTimeStamp start, end;
        epicsTimeGetCurrent(&start);

        log_debug_printf(LOG, "Sim[%s]: processing %lu rows for %lu output tables\n", name.c_str(), num_rows, tables.size());

        // Generate an update for all tables
        for (auto & table : tables) {
            // Output
            auto output = table.type.create();

            // Timestamps
            pvxs::shared_array<TimeTable::SECONDS_PAST_EPOCH_T> secondsPastEpoch(num_rows);
            pvxs::shared_array<TimeTable::NANOSECONDS_T> nanoseconds(num_rows);
            pvxs::shared_array<TimeTable::PULSE_ID_T> pulseId(num_rows);

            for (size_t i = 0; i < num_rows; ++i) {
                epicsTimeStamp row_ts = ts;
                epicsTimeAddSeconds(&row_ts, i*step_sec);

                TimeTable::PULSE_ID_T row_pulse_id = this->pulseId + i*source->num_samples_per_row();

                secondsPastEpoch[i] = row_ts.secPastEpoch;
                nanoseconds[i] = row_ts.nsec;
                pulseId[i] = row_pulse_id;
            }

            output.set_column(TimeTable::SECONDS_PAST_EPOCH_COL, secondsPastEpoch.freeze());
            output.set_column(TimeTable::NANOSECONDS_COL, nanoseconds.freeze());
            output.set_column(TimeTable::PULSE_ID_COL, pulseId.freeze());

            // Outer columns
            auto outer_column = table.type.data_columns.begin();
            auto v = source->simulate(num_rows);

            for (size_t i = 0; i < table.signal_names.size(); ++i) {
                for (auto inner_column : v.type.data_columns) {
                    output.set_column(outer_column->name, v.get_column_as<void>(inner_column.name));
                    ++outer_column;
                }
            }

            table.pv.post(output.get());
        }

        epicsTimeAddSeconds(&ts, num_rows*step_sec);
        this->pulseId += num_rows*source->num_samples_per_row();

        epicsTimeGetCurrent(&end);
        log_debug_printf(LOG, "Sim[%s]: processed %lu rows for %lu tables in %.3f sec\n",
            name.c_str(), num_rows, tables.size(), epicsTimeDiffInSeconds(&end, &start));
    }
};

// Called during aSub initialization
static long sim_init(aSubRecord *prec) {
    #define CHECK_INP(FT,INP,TYP)\
        do {\
            if (prec->FT != menuFtype ## TYP) {\
                errlogSevPrintf(errlogMajor, "%s: incorrect input type for " #INP "; expected " #TYP "\n", prec->name);\
                return S_dev_badInpType;\
            }\
        } while(0)

    CHECK_INP(fta, INPA, LONG);     // Number of Tables
    CHECK_INP(ftb, INPB, LONG);     // Number of Signals in each Table
    CHECK_INP(ftc, INPC, LONG);     // Column selection
    CHECK_INP(ftd, INPD, LONG);     // Number of compressed samples (if using STAT simulation)
    CHECK_INP(fte, INPE, DOUBLE);   // Time Step (sec)
    CHECK_INP(ftf, INPF, LONG);     // Number of rows in each update
    CHECK_INP(ftg, INPG, STRING);   // Label separator (between PV name and column label)
    CHECK_INP(fth, INPH, STRING);   // Column separator (PV identifier and column name)
    #undef CHECK_INP

    long num_tables       = *static_cast<long*>(prec->a);
    long num_signals      = *static_cast<long*>(prec->b);
    long config           = *static_cast<long*>(prec->c);
    long num_samp         = *static_cast<long*>(prec->d);
    double step_sec       = *static_cast<double*>(prec->e);
    const char *label_sep = static_cast<const char *>(prec->g);
    const char *col_sep   = static_cast<const char *>(prec->h);

    // Assume our name is xxxx_ASUB, chop off the _ASUB suffix to derive the V7 PV name
    std::string output_table_prefix(prec->name);
    {
        size_t found = output_table_prefix.rfind("_ASUB");
        if (found == std::string::npos)
            throw std::runtime_error(std::string("Expected record name '") + prec->name + "' to end in '_ASUB'");
        output_table_prefix.resize(found);
    }

    log_debug_printf(LOG,
        "sim_init[%s]: Simulating %ld %s Tables, each with %ld Signals, step=%.3f sec (stat samples=%ld) "
        "in output prefix=%s, label separator='%s', column separator='%s'\n",
        prec->name, num_tables, (config & Config::STAT) != 0 ? "statistics" : "scalar", num_signals,
        step_sec, num_samp, output_table_prefix.c_str(), label_sep, col_sep);

    std::unique_ptr<SimSource> source;

    if ((config & Config::STAT) != 0) {
        source.reset(new SimSourceStat(num_samp, step_sec));
    } else {
        TimeTableScalar::Config scalar_config(
            (config & Config::TIMESTAMP_UTAG) != 0,
            (config & Config::ALARM_SEVERITY) != 0,
            (config & Config::ALARM_CONDITION) != 0,
            (config & Config::ALARM_MESSAGE) != 0
        );

        source.reset(new SimSourceScalar(scalar_config, step_sec));
    }

    SimTables *sim = new SimTables(prec->name, step_sec, num_tables, num_signals, std::move(source), output_table_prefix, label_sep, col_sep);
    log_debug_printf(LOG, "created simtables%s\n", "");
    prec->dpvt = static_cast<void*>(sim);

    return 0;
}

static long sim_proc(aSubRecord *prec) {
    SimTables *sim = static_cast<SimTables*>(prec->dpvt);

    if (!sim) {
        log_crit_printf(LOG, "sim_proc[%s] record in bad state\n", prec->name);
        errlogSevPrintf(errlogMajor, "%s: record in bad state\n", prec->name);
        return S_dev_NoInit;
    }

    long num_rows = *static_cast<long*>(prec->f);
    sim->process(num_rows);

    return 0;
}

static void simulatorPvxsRegistrar()
{
    ensurePendingHook();
}

epicsExportRegistrar(simulatorPvxsRegistrar);

epicsRegisterFunction(sim_init);
epicsRegisterFunction(sim_proc);
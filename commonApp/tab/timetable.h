#ifndef TAB_TIMETABLE_H
#define TAB_TIMETABLE_H

#include <pvxs/data.h>
#include <tab/nttable.h>

namespace tabulator {

class TimeTableValue;

struct TimeTable {
public:
  typedef uint32_t SECONDS_PAST_EPOCH_T;
  typedef uint32_t NANOSECONDS_T;
  typedef uint64_t PULSE_ID_T;

  static const std::string SECONDS_PAST_EPOCH_COL;
  static const std::string NANOSECONDS_COL;
  static const std::string PULSE_ID_COL;

  static const std::string SECONDS_PAST_EPOCH_LABEL;
  static const std::string NANOSECONDS_LABEL;
  static const std::string PULSE_ID_LABEL;

  static const nt::NTTable::ColumnSpec SECONDS_PAST_EPOCH;
  static const nt::NTTable::ColumnSpec NANOSECONDS;
  static const nt::NTTable::ColumnSpec PULSE_ID;

  const std::vector<nt::NTTable::ColumnSpec> columns;
  const std::vector<nt::NTTable::ColumnSpec> time_columns;
  const std::vector<nt::NTTable::ColumnSpec> data_columns;
  const nt::NTTable nttable;

  TimeTable(const pvxs::Value &value);
  TimeTable(const std::vector<nt::NTTable::ColumnSpec> &data_columns);

  bool is_valid(const pvxs::Value &value) const;
  TimeTableValue create() const;
  TimeTableValue wrap(pvxs::Value value, bool validate = false) const;
};

struct TimeTableScalar : public TimeTable {
  struct Config {
    bool utag;
    bool alarm_sev;
    bool alarm_cond;
    bool alarm_message;

    Config(bool utag, bool alarm_sev, bool alarm_cond, bool alarm_message)
        : utag(utag), alarm_sev(alarm_sev), alarm_cond(alarm_cond),
          alarm_message(alarm_message) {}
  };

  typedef double VALUE_T;
  typedef uint64_t UTAG_T;
  typedef uint16_t ALARM_SEV_T;
  typedef uint16_t ALARM_COND_T;
  typedef std::string ALARM_MSG_T;

  static const std::string VALUE_COL;
  static const std::string UTAG_COL;
  static const std::string ALARM_SEV_COL;
  static const std::string ALARM_COND_COL;
  static const std::string ALARM_MSG_COL;

  static const std::string VALUE_LABEL;
  static const std::string UTAG_LABEL;
  static const std::string ALARM_SEV_LABEL;
  static const std::string ALARM_COND_LABEL;
  static const std::string ALARM_MSG_LABEL;

  static const nt::NTTable::ColumnSpec VALUE;
  static const nt::NTTable::ColumnSpec UTAG;
  static const nt::NTTable::ColumnSpec ALARM_SEV;
  static const nt::NTTable::ColumnSpec ALARM_COND;
  static const nt::NTTable::ColumnSpec ALARM_MSG;

  const Config config;

  TimeTableScalar(Config config);
};

struct TimeTableStat : public TimeTable {
  typedef double VAL_T;
  typedef uint32_t NUM_SAMP_T;
  typedef double MIN_T;
  typedef double MAX_T;
  typedef double MEAN_T;
  typedef double RMS_T;

  static const std::string VAL_COL;
  static const std::string NUM_SAMP_COL;
  static const std::string MIN_COL;
  static const std::string MAX_COL;
  static const std::string MEAN_COL;
  static const std::string RMS_COL;

  static const std::string VAL_LABEL;
  static const std::string NUM_SAMP_LABEL;
  static const std::string MIN_LABEL;
  static const std::string MAX_LABEL;
  static const std::string MEAN_LABEL;
  static const std::string RMS_LABEL;

  static const nt::NTTable::ColumnSpec VAL;
  static const nt::NTTable::ColumnSpec NUM_SAMP;
  static const nt::NTTable::ColumnSpec MIN;
  static const nt::NTTable::ColumnSpec MAX;
  static const nt::NTTable::ColumnSpec MEAN;
  static const nt::NTTable::ColumnSpec RMS;

  TimeTableStat();
};

class TimeTableValue {
public:
  const TimeTable type;

private:
  pvxs::Value value_;

  TimeTableValue(TimeTable type, pvxs::Value value)
      : type(type), value_(value) {}

public:
  static TimeTableValue from(pvxs::Value v, bool validate = false);

  pvxs::Value get() const { return value_; }

  inline pvxs::shared_array<const std::string> get_labels() const {
    return value_[nt::NTTable::LABELS_FIELD]
        .as<pvxs::shared_array<const std::string>>();
  }

  inline pvxs::Value get_column(const std::string &col_name) const {
    return value_[nt::NTTable::COLUMNS_FIELD][col_name];
  }

  template <typename T>
  inline pvxs::shared_array<const T>
  get_column_as(const std::string &col_name) const {
    auto v = get_column(col_name);
    return v ? v.as<pvxs::shared_array<const T>>()
             : pvxs::shared_array<const T>();
  }

  template <typename T>
  void set_column(const std::string &col_name,
                  pvxs::shared_array<const T> contents) {
    value_[nt::NTTable::COLUMNS_FIELD][col_name] = contents;
  }

  template <typename I>
  void set_column(const std::string &col_name, I begin, I end) {
    value_[nt::NTTable::COLUMNS_FIELD][col_name] =
        pvxs::shared_array<const typename I::value_type>(begin, end);
  }

  friend class TimeTable;
};

} // namespace tabulator

#endif

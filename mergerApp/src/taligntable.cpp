#include "taligntable.h"

#include <cmath>
#include <exception>
#include <set>
#include <vector>

#include <epicsMutex.h>
#include <epicsStdio.h>
#include <epicsTime.h>
#include <epicsVersion.h>

#include <pvxs/log.h>
#include <pvxs/sharedArray.h>

DEFINE_LOGGER(LOG, "taligntable");

typedef epicsGuard<epicsMutex> Guard;

namespace tabulator {

TimeBounds::TimeBounds() { reset(); }

void TimeBounds::reset() {
  valid = false;
  earliest_start = TimeSpan::MAX_TS;
  earliest_end = TimeSpan::MAX_TS;
  latest_start = TimeSpan::MIN_TS;
  latest_end = TimeSpan::MIN_TS;
}

nt::NTTable::ColumnSpec
TimeAlignedTable::prefixed_colspec(size_t idx, size_t total,
                                   const std::string &pvname,
                                   const nt::NTTable::ColumnSpec &spec) {
  int width = ceil(log2(total) / log2(16));
  char colprefix_buf[512] = {};
  epicsSnprintf(colprefix_buf, sizeof(colprefix_buf), "tbl%0*lu", width, idx);
  std::string colprefix(colprefix_buf);

  return {spec.type_code, colprefix + col_sep_ + spec.name,
          pvname + label_sep_ + spec.label};
}

void TimeAlignedTable::initialize() {
  Guard G(lock_);

  if (type_)
    return;

  static const nt::NTTable::ColumnSpec VALID{pvxs::TypeCode::BoolA, "valid",
                                             "valid"};

  std::vector<nt::NTTable::ColumnSpec> data_columns;
  size_t idx = 0;

  // Check that all buffers are initialized
  for (const auto &buf : buffers_) {
    if (!buf.second.initialized())
      return;
  }

  // Build type
  for (const auto &buf : buffers_) {
    const auto &pvname = buf.first;
    data_columns.emplace_back(
        prefixed_colspec(idx, buffers_.size(), pvname, VALID));

    for (const auto &spec : buf.second.data_columns())
      data_columns.emplace_back(
          prefixed_colspec(idx, buffers_.size(), pvname, spec));

    ++idx;
  }

  type_.reset(new TimeTable(data_columns));
}

TimeAlignedTable::TimeAlignedTable(const std::vector<std::string> &pvlist,
                                   const std::string &label_sep,
                                   const std::string &col_sep)
    : label_sep_(label_sep), col_sep_(col_sep), lock_(), buffers_(), type_() {
  for (auto pv : pvlist)
    buffers_[pv] = {};

  log_debug_printf(LOG, "TimeAlignedTable(%lu PVs)\n", pvlist.size());
}

bool TimeAlignedTable::initialized() const {
  Guard G(lock_);
  return type_.get() != 0;
}

size_t TimeAlignedTable::force_initialize() {
  Guard G(lock_);

  std::set<std::string> bufs_to_remove;

  for (auto &buf : buffers_) {
    if (!buf.second.initialized()) {
      bufs_to_remove.insert(buf.first);
      log_warn_printf(LOG, "Dropping '%s'\n", buf.first.c_str());
    }
  }

  for (auto &name : bufs_to_remove)
    buffers_.erase(name);

  initialize();
  return buffers_.size();
}

TimeBounds TimeAlignedTable::get_timebounds() const {
  Guard G(lock_);

  // Collect timespans
  std::vector<TimeSpan> timespans;

  for (const auto &buf : buffers_)
    timespans.emplace_back(buf.second.time_span());

  return TimeBounds(timespans.begin(), timespans.end());
}

void TimeAlignedTable::push(const std::string &name, pvxs::Value value) {
  log_debug_printf(LOG, "push(name=%s, value.valid=%d)\n", name.c_str(),
                   value.valid());

  Guard G(lock_);

  // Push the value to the correct buffer
  buffers_.at(name).push(value);

  // Create type if we don't have it already
  initialize();
}

static void copy_row(std::vector<pvxs::shared_array<void>> &dest,
                     size_t dest_idx,
                     const std::vector<pvxs::shared_array<const void>> src,
                     size_t src_idx) {
  if (dest.size() != src.size())
    throw std::logic_error("Can't copy between different sized arrays");

  auto dest_it = dest.begin();
  auto src_it = src.begin();

  for (; dest_it != dest.end() && src_it != src.end(); ++dest_it, ++src_it) {
    switch (dest_it->original_type()) {
#define CASE_COPY_ELEM(AT, T)                                                  \
  case pvxs::ArrayType::AT:                                                    \
    *(static_cast<T *>(dest_it->data()) + dest_idx) =                          \
        *(static_cast<const T *>(src_it->data()) + src_idx);                   \
    break

      CASE_COPY_ELEM(Bool, bool);
      CASE_COPY_ELEM(Int8, int8_t);
      CASE_COPY_ELEM(Int16, int16_t);
      CASE_COPY_ELEM(Int32, int32_t);
      CASE_COPY_ELEM(Int64, int64_t);
      CASE_COPY_ELEM(UInt8, uint8_t);
      CASE_COPY_ELEM(UInt16, uint16_t);
      CASE_COPY_ELEM(UInt32, uint32_t);
      CASE_COPY_ELEM(UInt64, uint64_t);
      CASE_COPY_ELEM(Float32, float);
      CASE_COPY_ELEM(Float64, double);
      CASE_COPY_ELEM(String, std::string);

#undef CASE_COPY_ELEM

    default:
      throw std::runtime_error("Don't know how to copy this element type");
    }
  }
}

static void set_empty_row(std::vector<pvxs::shared_array<void>> &dest,
                          size_t idx) {
  for (auto dest_it = dest.begin(); dest_it != dest.end(); ++dest_it) {
    switch (dest_it->original_type()) {
#define CASE_SET_ELEM(AT, T)                                                   \
  case pvxs::ArrayType::AT:                                                    \
    *(static_cast<T *>(dest_it->data()) + idx) = {};                           \
    break

      CASE_SET_ELEM(Bool, bool);
      CASE_SET_ELEM(Int8, int8_t);
      CASE_SET_ELEM(Int16, int16_t);
      CASE_SET_ELEM(Int32, int32_t);
      CASE_SET_ELEM(Int64, int64_t);
      CASE_SET_ELEM(UInt8, uint8_t);
      CASE_SET_ELEM(UInt16, uint16_t);
      CASE_SET_ELEM(UInt32, uint32_t);
      CASE_SET_ELEM(UInt64, uint64_t);
      CASE_SET_ELEM(Float32, float);
      CASE_SET_ELEM(Float64, double);
      CASE_SET_ELEM(String, std::string);

#undef CASE_SET_ELEM

    default:
      throw std::runtime_error("Don't know how to set this element type");
    }
  }
}

pvxs::Value TimeAlignedTable::extract(const TimeStamp &start_ts,
                                      const TimeStamp &end_ts) {
  Guard G(lock_);

  // Sanity check
  if (start_ts > end_ts) {
    char message[1024];
    epicsSnprintf(message, sizeof(message),
                  "TimeAlignedTable::extract: Expected start=%u.%09u.%016lX to "
                  "be before end=%u.%09u.%016lX",
                  start_ts.ts.secPastEpoch, start_ts.ts.nsec, start_ts.utag,
                  end_ts.ts.secPastEpoch, end_ts.ts.nsec, end_ts.utag);
    throw std::runtime_error(message);
  }

  // Sorted unique timestamps
  std::set<TimeStamp> timestamps;

  for (const auto &buf : buffers_)
    buf.second.extract_timestamps_between(start_ts, end_ts, timestamps);

  size_t num_rows = timestamps.size();

  log_debug_printf(
      LOG, "extract(start=%u.%09u.%016lX, end=%u.%09u.%016lX) --> %lu rows\n",
      start_ts.ts.secPastEpoch, start_ts.ts.nsec, start_ts.utag,
      end_ts.ts.secPastEpoch, end_ts.ts.nsec, end_ts.utag, num_rows);

  // Prepare output columns and Value
  std::vector<pvxs::shared_array<void>> time_columns;
  std::vector<pvxs::shared_array<void>> data_columns;
  std::vector<pvxs::shared_array<void>> output_columns;
  TimeTableValue output_value(type_->create());

  // Generate timestamp arrays
  {
    pvxs::shared_array<uint32_t> secondsPastEpoch(num_rows);
    pvxs::shared_array<uint32_t> nanoseconds(num_rows);
    pvxs::shared_array<uint64_t> userTags(num_rows);

    size_t i = 0;
    for (const auto &ts : timestamps) {
      secondsPastEpoch[i] = ts.ts.secPastEpoch;
      nanoseconds[i] = ts.ts.nsec;
      userTags[i] = ts.utag;
      ++i;
    }

    // Add timestamps to output columns
    time_columns.emplace_back(secondsPastEpoch.castTo<void>());
    time_columns.emplace_back(nanoseconds.castTo<void>());
    time_columns.emplace_back(userTags.castTo<void>());
  }

  // Extract values from each of our buffers (each buffer contains updates for a
  // single input Table PV)
  for (auto &buf : buffers_) {
    // These will hold the final extracted values
    pvxs::shared_array<bool> valid(num_rows);
    auto column_values = buf.second.allocate_containers(num_rows);

    // Iterate over each result row
    size_t row = 0;
    auto row_ts_it = timestamps.begin();

    buf.second.consume_each_row(
        [this, num_rows, &row, &row_ts_it, start_ts, end_ts, &valid,
         &column_values](
            const TimeStamp &buf_row_ts,
            const std::vector<pvxs::shared_array<const void>> &buf_cols,
            size_t buf_idx) -> bool {
          TimeStamp row_ts = *row_ts_it;

          // Check if we are past the end, stop if we are
          if (row >= num_rows || buf_row_ts > end_ts)
            return true;

          // If timestamps are equal, values are valid
          if (buf_row_ts == row_ts) {
            valid[row] = true;
            copy_row(column_values, row, buf_cols, buf_idx);
            ++row;
            ++row_ts_it;
            return false;
          }

          // If the buffer is ahead of us (in time), it means there are missing
          // values. Skip this iteration (by filling in invalid values)
          if (buf_row_ts > row_ts) {
            valid[row] = false;
            set_empty_row(column_values, row);
            return false;
          }

          // If we got here, it means the buffer is behind us (in time).
          // Skip this iteration (do nothing)
          ++row;
          ++row_ts_it;
          return false;
        });

    // The remaining rows are invalid
    for (; row < num_rows; ++row) {
      valid[row] = false;
      set_empty_row(column_values, row);
    }

    // We built all columns from this buffer, save them
    log_debug_printf(LOG, "extract() - generated %lu data columns\n",
                     column_values.size() + 1);
    data_columns.emplace_back(valid.castTo<void>());
    data_columns.insert(data_columns.end(), column_values.begin(),
                        column_values.end());
  }

  log_debug_printf(LOG, "extract() - generated %lu timestamp columns\n",
                   time_columns.size());
  output_columns.insert(output_columns.end(), time_columns.begin(),
                        time_columns.end());
  output_columns.insert(output_columns.end(), data_columns.begin(),
                        data_columns.end());

  time_columns.clear();
  data_columns.clear();

  // Paranoia
  if (type_->columns.size() != output_columns.size())
    throw std::logic_error(
        "Mismatch between number of columns in type definition and in output");

  log_debug_printf(LOG, "extract() - generated %lu total columns\n",
                   output_columns.size());

  // Set columns in Value
  // zip()
  auto cs = type_->columns.begin();
  auto oc = output_columns.begin();

  for (; cs != type_->columns.end(); ++cs, ++oc)
    output_value.set_column(cs->name, (*oc).freeze());

  log_debug_printf(LOG, "extract() - generated complete value%s\n", "");

  return output_value.get();
}

pvxs::Value TimeAlignedTable::create() const {
  Guard G(lock_);
  if (initialized())
    return type_->create().get();

  return {};
}

void TimeAlignedTable::dump() const {
  Guard G(lock_);

  /*printf("TimeAlignedTable [ncols=%lu align=%u us start=%u.%u
     earliest_end=%u.%u latest_end=%u.%u]\n", num_columns_, alignment_usec_,
      time_span_.start.secPastEpoch, time_span_.start.nsec,
      time_span_.earliest_end.secPastEpoch, time_span_.earliest_end.nsec,
      time_span_.latest_end.secPastEpoch, time_span_.latest_end.nsec
      );*/

  /*for (size_t col = 0; col < num_columns_; ++col) {
      size_t s = timestamps_[col].size();
      if (s > 0) {
          epicsTimeStamp first_ts = timestamps_[col][0];
          epicsTimeStamp last_ts = timestamps_[col][s-1];
          double first_val = values_[col][0];
          double last_val = values_[col][s-1];

          double ts_diff = epicsTimeDiffInSeconds(&last_ts, &first_ts);

          printf("[%lu] (%.3f s) ts=%u.%u v=%.3f   ts=%u.%u v=%.3f\n",
              col, ts_diff,
              first_ts.secPastEpoch, first_ts.nsec, first_val,
              last_ts.secPastEpoch, last_ts.nsec, last_val);

      } else {
          printf("[%lu] <empty>\n", col);
      }
  }*/
}

} // namespace tabulator
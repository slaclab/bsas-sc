#ifndef TAB_TALIGNTABLE_H
#define TAB_TALIGNTABLE_H

#include <map>
#include <vector>

#include <epicsMutex.h>
#include <epicsTime.h>

#include <pvxs/data.h>

#include "tablebuffer.h"

namespace tabulator {

struct TimeBounds {
  bool valid;
  TimeStamp earliest_start;
  TimeStamp earliest_end;
  TimeStamp latest_start;
  TimeStamp latest_end;

  TimeBounds();

  template <typename I> TimeBounds(const I &begin, const I &end) {
    reset();

    if (begin == end)
      return;

    for (I it = begin; it != end; ++it) {
      if (!it->valid)
        continue;

      earliest_start = std::min(it->start, earliest_start);
      earliest_end = std::min(it->end, earliest_end);
      latest_start = std::max(it->start, latest_start);
      latest_end = std::max(it->end, latest_end);

      valid = true;
    }
  }

  void reset();
};

class TimeAlignedTable {

private:
  const std::string label_sep_;
  const std::string col_sep_;

  mutable epicsMutex lock_;
  std::map<std::string, TableBuffer> buffers_;
  std::unique_ptr<TimeTable> type_;

  void initialize();
  nt::NTTable::ColumnSpec prefixed_colspec(size_t idx, size_t total,
                                           const std::string &pvname,
                                           const nt::NTTable::ColumnSpec &spec);

public:
  TimeAlignedTable(const std::vector<std::string> &pvlist,
                   const std::string &label_sep, const std::string &col_sep);

  // Returns true if all inner buffers were initialized (got at least 1 update),
  // false otherwise
  bool initialized() const;

  // Forces this table to be initialized with whatever buffers are available
  // Internal buffers that are not initialized by the time this is called will
  // be dropped. Returns the number of remaining internal buffers
  size_t force_initialize();

  TimeBounds get_timebounds() const;

  // Push a new update to one of the buffers
  // Throws std::out_of_range if name is not part of this table
  void push(const std::string &name, pvxs::Value value);

  // Extract a time-aligned table chunk, between start and end
  pvxs::Value extract(const TimeStamp &start, const TimeStamp &end);

  // Build an empty value
  pvxs::Value create() const;

  void dump() const;
};

} // namespace tabulator

#endif
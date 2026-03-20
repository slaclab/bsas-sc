#include <string>
#include <iostream>
#include <fstream>
#include <deque>
#include <stdexcept>
#include <utility>

#include <epicsEvent.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsStdio.h>

#include <pvxs/log.h>
#include <pvxs/util.h>
#include <pvxs/client.h>
#include <pvxs/server.h>
#include <pvxs/sharedpv.h>

#include <clipp.h>

#include <tab/nttable.h>

#include <iostream>

#include "taligntable.h"

DEFINE_LOGGER(LOG, "merger");
DEFINE_LOGGER(LISTENER_LOG, "merger.listener");
DEFINE_LOGGER(REACTOR_LOG, "merger.reactor");
DEFINE_LOGGER(SERVER_LOG, "merger.server");

using tabulator::nt::NTTable;
using tabulator::TimeSpan;
using tabulator::TimeStamp;
using tabulator::TimeBounds;
using tabulator::TimeAlignedTable;

static const size_t QUEUE_SIZE = 1024u;

class Runnable : public epicsThreadRunable {
protected:
    std::shared_ptr<pvxs::MPMCFIFO<Runnable*>> dead_;
    bool running_;
    epicsThread thread_;

    Runnable(const char *name, std::shared_ptr<pvxs::MPMCFIFO<Runnable*>> dead)
    : dead_(dead), running_(false), thread_(*this, name, epicsThreadGetStackSize(epicsThreadStackMedium), epicsThreadPriorityMedium)
    {}

    // Natural stop
    void stopped() {
        running_ = false;
        dead_->push(this);
    }

public:
    void start() {
        running_ = true;
        thread_.start();
    }

    // Force stop
    virtual void stop(double delay) {
        if (running_) {
            running_ = false;
            thread_.exitWait(delay);
            dead_->push(this);
        }
    }

    virtual ~Runnable() {};
};

 class Listener : public Runnable {
private:
    pvxs::client::Context client_;
    pvxs::MPMCFIFO<std::pair<size_t, std::shared_ptr<pvxs::client::Subscription>>> queue_;
    std::vector<std::shared_ptr<pvxs::client::Subscription>> subscriptions_;
    std::shared_ptr<TimeAlignedTable> taligned_table_;

public:
    Listener(
        std::shared_ptr<pvxs::MPMCFIFO<Runnable*>> dead, const std::vector<std::string> & pvlist,
        const std::shared_ptr<TimeAlignedTable> & taligned_table
    ) : Runnable(typeid(Listener).name(), dead),
      client_(pvxs::client::Context::fromEnv()), queue_(QUEUE_SIZE),
      subscriptions_(), taligned_table_(taligned_table)
    {
        // Create subscriptions
        size_t col_idx = 0;
        for (auto pvname : pvlist) {
            try {
                subscriptions_.emplace_back(
                    client_
                        .monitor(pvname)
                        // Work around older pvxs monitor parser which expects record._options.pipeline.
                        .record("pipeline", false)
                        .record("queueSize", static_cast<uint32_t>(QUEUE_SIZE))
                        .record("ackAny", 1u)
                        .event([this, col_idx](pvxs::client::Subscription &sub) {
                            this->queue_.push(std::make_pair(col_idx, sub.shared_from_this()));
                        })
                        .exec());
            } catch (const std::exception& e) {
                log_err_printf(LISTENER_LOG,
                    "Failed to subscribe to '%s': %s\n",
                    pvname.c_str(), e.what());
            } catch (...) {
                log_err_printf(LISTENER_LOG,
                    "Failed to subscribe to '%s': unknown exception\n",
                    pvname.c_str());
            }
            ++col_idx;
        }

        if (subscriptions_.empty()) {
            throw std::runtime_error("Listener failed to subscribe to any PV from --pvlist");
        }
    }

    void run() {
        log_info_printf(LISTENER_LOG, "Starting%s\n", "");
        log_info_printf(LISTENER_LOG, "  # subscriptions=%lu\n", subscriptions_.size());

        while (running_) {
            auto item = queue_.pop();

            if (!running_)
                break;

            auto col_idx = item.first;
            auto sub = item.second;

            if (!sub)
                break;

            try {
                auto value = sub->pop();

                if (!value)
                    continue;

                //std::cout << sub->name() << std::endl << update.get();
                taligned_table_->push(sub->name(), value);

            } catch (std::out_of_range & ex) {
                log_warn_printf(LISTENER_LOG, "Can't push new data for '%s', cancelling sub\n", sub->name().c_str());
                sub->cancel();
            } catch (pvxs::client::Connected & ex) {
                log_info_printf(LISTENER_LOG, "PV connected: %s\n", sub->name().c_str());
            } catch (pvxs::client::Disconnect & ex) {
                log_warn_printf(LISTENER_LOG, "PV disconnected: %s\n", sub->name().c_str());
            } catch (std::exception &e) {
                log_err_printf(LISTENER_LOG, "Error: %s %s\n", sub->name().c_str(), e.what());
                break;
            } catch (...) {
                log_err_printf(LISTENER_LOG, "Error: %s unknown exception\n", sub->name().c_str());
                break;
            }

            queue_.push(std::make_pair(col_idx, sub));
        }
        log_info_printf(LISTENER_LOG, "Ending%s\n", "");
        stopped();
    }

    void stop(double delay) {
        // Ensure we "pump" the queue
        running_ = false;
        queue_.emplace(0, nullptr);
        Runnable::stop(delay);
    }

    virtual ~Listener() {}
};

class Reactor : public Runnable {
private:
    std::shared_ptr<TimeAlignedTable> taligned_table_;
    double period_;
    double timeout_;
    pvxs::server::SharedPV pv_;

public:
    Reactor(
        std::shared_ptr<pvxs::MPMCFIFO<Runnable*>> dead,
        const std::shared_ptr<TimeAlignedTable> & taligned_table, double period,
        double timeout, pvxs::server::SharedPV & pv)
    : Runnable(typeid(Reactor).name(), dead),
      taligned_table_(taligned_table), period_(period), timeout_(timeout), pv_(pv)
    {
        assert(period > 0.0);
        assert(timeout == 0 || timeout > period);
    }

    bool prepare(double sleepPeriod) {
        // Wait until all PVs have at least 1 update
        epicsTimeStamp start_ts, now_ts;
        epicsTimeGetCurrent(&start_ts);
        epicsTimeGetCurrent(&now_ts);

        log_debug_printf(REACTOR_LOG, "Waiting until all PVs have at least one update%s\n", "");

        while (running_ && (timeout_ == 0 || epicsTimeDiffInSeconds(&now_ts, &start_ts) < timeout_) && !taligned_table_->initialized()) {
            epicsThreadSleep(sleepPeriod);
            epicsTimeGetCurrent(&now_ts);
            continue;
        }

        if (!running_)
            return false;

        size_t remaining = taligned_table_->force_initialize();

        if (!remaining) {
            log_err_printf(REACTOR_LOG, "Failed to connect to any PV... Exiting.%s\n", "");
            return false;
        }

        if (!taligned_table_->initialized())
            throw std::logic_error("Table should be initialized by this point");

        return true;
    }

    virtual void run() {
        double sleepPeriod = period_ / 5.0;

        log_info_printf(REACTOR_LOG, "Starting%s\n", "");
        log_info_printf(REACTOR_LOG, "  period=%.6f s\n", period_);
        log_info_printf(REACTOR_LOG, "  timeout=%.6f s\n", timeout_);
        log_info_printf(REACTOR_LOG, "  refresh=%.6f s\n", sleepPeriod);

        if (!prepare(sleepPeriod)) {
            log_info_printf(REACTOR_LOG, "Ending%s\n", "");
            stopped();
            return;
        }

        // Now that all PVs are connected, extract NTTable
        auto initial = taligned_table_->create();
        pv_.open(initial);

        epicsTimeStamp last_update;
        epicsTimeGetCurrent(&last_update);

        while (running_) {
            epicsTimeStamp now;
            epicsTimeGetCurrent(&now);

            double secs_since_last_update = epicsTimeDiffInSeconds(&now, &last_update);

            if (timeout_ > 0 && secs_since_last_update > timeout_) {
                log_err_printf(
                    REACTOR_LOG, "Timed out waiting for updates. Waited for %.1f sec (timeout=%.1f sec)\n",
                    secs_since_last_update, timeout_
                );
                break;
            }

            TimeBounds bounds = taligned_table_->get_timebounds();

            if (!bounds.valid) {
                epicsThreadSleep(sleepPeriod);
                continue;
            }

            TimeSpan shortest(bounds.earliest_start, bounds.earliest_end);
            TimeSpan longest(bounds.earliest_start, bounds.latest_end);

            log_debug_printf(REACTOR_LOG, "Considering timespans shortest=%.6f s, longest=%.6f\n",
                shortest.span_sec(), longest.span_sec());

            if (shortest.span_sec() < period_ && longest.span_sec() < timeout_) {
                epicsThreadSleep(sleepPeriod);
                continue;
            }

            TimeStamp start = bounds.earliest_start;
            TimeStamp end = start;
            epicsTimeAddSeconds(&end.ts, period_);

            log_debug_printf(REACTOR_LOG, "Extracting merged table spanning %.3f sec: %u.%u -- %u.%u\n",
                epicsTimeDiffInSeconds(&end.ts, &start.ts), start.ts.secPastEpoch, start.ts.nsec,
                end.ts.secPastEpoch, end.ts.nsec);

            auto value = taligned_table_->extract(start, end);
            pv_.post(value);
            epicsTimeGetCurrent(&last_update);
        }

        log_info_printf(REACTOR_LOG, "Ending%s\n", "");
        stopped();
    }

    virtual ~Reactor() {}
};

static std::vector<std::string> pvlist_from_file(const std::string & filename) {
    // Open input file and fetch PV names
    std::ifstream filestream(filename);
    std::string line;
    std::vector<std::string> pvlist;

    while(std::getline(filestream, line)) {
        auto begin = line.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            continue;

        auto end = line.find_last_not_of(" \t\r\n");
        auto pv = line.substr(begin, end - begin + 1);

        if (pv.empty() || pv[0] == '#')
            continue;

        pvlist.push_back(pv);
    }

    return pvlist;
}

int main (int argc, char *argv[]) {
    std::string pvlist_file;
    double period_sec;
    double timeout_sec = 0.0;
    std::string pvname;
    std::string label_sep = ".";
    std::string col_sep = "_";

    pvxs::logger_config_env();

    auto cli = (
        clipp::required("--pvlist")
            .doc("File with list of input NTTable PVs to be merged (newline-separated).")
            & clipp::value("pvlist", pvlist_file),

        clipp::required("--period-sec")
            .doc("Update publication period, in seconds.")
            & clipp::value("period_sec", period_sec),

        clipp::option("--timeout-sec")
            .doc("Time window to wait for laggards, in seconds. Default: 0 (wait forever).")
            & clipp::value("timeout_sec", timeout_sec),

        clipp::required("--pvname")
            .doc("Name of the output PV.")
            & clipp::value("pvname", pvname),

        clipp::option("--label-sep")
            .doc(std::string("Separator between PV name and column name in labels. Default: '") + label_sep + "'.")
            & clipp::value("label_sep", label_sep),

        clipp::option("--column-sep")
            .doc(std::string("Separator between PV identifier and original column name. Default: '") + col_sep + "'.")
            & clipp::value("col_sep", col_sep)
    );

    auto man_page = clipp::make_man_page(cli, argv[0]);

    if (!clipp::parse(argc, argv, cli)) {
        std::cerr << man_page;
        return 1;
    }

    // Validate arguments
    #define VALIDATE_ARG(COND, FMT, ARG)\
        do {\
            if (COND) {\
                log_err_printf(LOG, FMT, ARG);\
                std::cerr << man_page;\
                return 1;\
            }\
        } while(0)

    VALIDATE_ARG(period_sec <= 0.0, "Invalid period: %.6f seconds\n", period_sec);
    VALIDATE_ARG(timeout_sec < 0.0 || (timeout_sec > 0 && timeout_sec < period_sec), "Invalid timeout: %.6f seconds\n", timeout_sec);
    #undef VALIDATE_ARG

    try {
        std::vector<std::string> pvlist(pvlist_from_file(pvlist_file));

        // Create
        log_info_printf(LOG, "Starting%s\n", "");
        log_info_printf(LOG, "  pvlist=%s [%lu PVs]\n", pvlist_file.c_str(), pvlist.size());
        log_info_printf(LOG, "  period=%.6f s\n", period_sec);
        log_info_printf(LOG, "  timeout=%.6f s%s\n", timeout_sec, timeout_sec == 0 ? " (wait forever)" : "");
        log_info_printf(LOG, "  pvname=%s\n", pvname.c_str());
        log_info_printf(LOG, "  label-sep=%s\n", label_sep.c_str());
        log_info_printf(LOG, "  column-sep=%s\n", col_sep.c_str());

        // Shared objects
        auto dead_queue = std::make_shared<pvxs::MPMCFIFO<Runnable*>>();
        auto taligned_table(std::make_shared<TimeAlignedTable>(pvlist, label_sep, col_sep));
        pvxs::server::SharedPV pv(pvxs::server::SharedPV::buildReadonly());

        // Prepare workers
        Listener listener(dead_queue, pvlist, taligned_table);
        Reactor reactor(dead_queue, taligned_table, period_sec, timeout_sec, pv);

        // Prepare server
        pvxs::server::Server server(pvxs::server::Config::fromEnv().build());
        server.addPV(pvname, pv);

        // Run workers and server
        listener.start();
        reactor.start();
        server.start();

        // Wait for one thread to die
        // CTRL+C is handled by the Server thread
        auto dead = dead_queue->pop();

        // Close the PV, stop the server
        pv.close();
        server.stop();

        // Ask other threads to stop, if they are not dead yet
        if (dynamic_cast<Runnable*>(&listener) != dead)
            listener.stop(1.0);

        if (dynamic_cast<Runnable*>(&reactor) != dead)
            reactor.stop(1.0);

        log_info_printf(LOG, "Exiting%s\n", "");

        return 0;
    } catch (const std::exception& e) {
        log_err_printf(LOG, "Fatal startup error: %s\n", e.what());
        return 2;
    } catch (...) {
        log_err_printf(LOG, "Fatal startup error: %s\n", "unknown exception");
        return 3;
    }
}

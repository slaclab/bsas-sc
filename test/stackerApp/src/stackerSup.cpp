#include <registryFunction.h>
#include <epicsExport.h>
#include <aSubRecord.h>
#include <menuFtype.h>
#include <errlog.h>
#include <dbAccess.h>
#include <alarm.h>
#include <devSup.h>

#include <vector>
#include <iostream>

#include <pvxs/server.h>
#include <pvxs/sharedpv.h>
#include <pvxs/log.h>
#include <pvxs/iochooks.h>
#include <initHooks.h>
#include <pvxs/nt.h>

#include <tab/nttable.h>
#include <tab/timetable.h>

#ifndef dbGetAlarmMsg
#  define dbGetAlarmMsg(LINK, STAT, SEVR, BUF, BUFLEN) dbGetAlarm(LINK, STAT, SEVR)
#endif
#ifndef dbGetTimeStampTag
typedef epicsUInt64 epicsUTag;
#  define dbGetTimeStampTag(LINK, STAMP, TAG) dbGetTimeStamp(LINK, STAMP)
#endif


DEFINE_LOGGER(LOG, "stacker");

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
using tabulator::TimeTableScalar;

enum Columns : uint8_t {
    TIMESTAMP_AND_VALUE = 0x00,
    TIMESTAMP_UTAG      = 0x01,
    ALARM_SEVERITY      = 0x02,
    ALARM_CONDITION     = 0x04,
    ALARM_MESSAGE       = 0x08,
};

struct Stacker {
    // Configuration
    const std::string name;
    const double period_sec;

    // pvxs
    const TimeTableScalar type;
    pvxs::server::SharedPV pv;

    // Buffered data (single buffered for now)
    std::vector<TimeTableScalar::SECONDS_PAST_EPOCH_T> secondsPastEpoch;
    std::vector<TimeTableScalar::NANOSECONDS_T> nanoseconds;
    std::vector<TimeTableScalar::VALUE_T> values;
    std::vector<TimeTableScalar::UTAG_T> utags;
    std::vector<TimeTableScalar::ALARM_SEV_T> severities;
    std::vector<TimeTableScalar::ALARM_COND_T> conditions;
    std::vector<TimeTableScalar::ALARM_MSG_T> messages;

    Stacker(const char *name, TimeTableScalar::Config config, double period_sec,
        const char * output_pv_name)
    :name(name), period_sec(period_sec), type(config),
     pv(pvxs::server::SharedPV::buildReadonly())
    {
        pending_pvs.push_back({output_pv_name, pv});

        auto initial = type.create();
        pv.open(initial.get());
        ensurePendingHook();

        log_info_printf(LOG, "Stacker[%s]: initialized\n", name);
    }

    void push(
        epicsTimeStamp timestamp, double value, epicsUTag utag, epicsEnum16 severity, epicsEnum16 condition,
        const char * message
    ) {
        /*log_debug_printf(LOG, "Stacker[%s].push(ts=%u.%u,val=%.3f,tag=%lu,sev=%u,cond=%u,msg=%s)\n",
            name.c_str(), timestamp.secPastEpoch, timestamp.nsec, value, utag, severity, condition, message);*/

        size_t n = secondsPastEpoch.size();

        if (n > 0) {
            // Check if non-monotonically-decreasing
            epicsTimeStamp prev {
                secondsPastEpoch[n-1], nanoseconds[n-1]
            };

            double time_diff_sec = epicsTimeDiffInSeconds(&timestamp, &prev);

            if (time_diff_sec <= 0) {
                log_warn_printf(LOG, "Stacker[%s].push(): skipping update with non-increasing timestamp (diff=%.6f sec)\n",
                    name.c_str(), time_diff_sec);
                reset();
                return;
            }

            // Check if we should publish
            epicsTimeStamp oldest {
                secondsPastEpoch[0], nanoseconds[0]
            };

            double time_diff = epicsTimeDiffInSeconds(&timestamp, &oldest);

            if (time_diff >= period_sec) {
                publish();
                reset();
            }
        }

        secondsPastEpoch.push_back(timestamp.secPastEpoch);
        nanoseconds.push_back(timestamp.nsec);
        values.push_back(value);

        if (type.config.utag)
            utags.push_back(utag);

        if (type.config.alarm_sev)
            severities.push_back(severity);

        if (type.config.alarm_cond)
            conditions.push_back(condition);

        if (type.config.alarm_message)
            messages.push_back(message);
    }

    void publish() {
        auto val = type.create();

        #define SET_IF(C, P, V)\
            do {\
                if (C)\
                    val.set_column(TimeTableScalar::P ## _COL, V.begin(), V.end());\
            } while(0)

        SET_IF(true, SECONDS_PAST_EPOCH, secondsPastEpoch);
        SET_IF(true, NANOSECONDS, nanoseconds);
        SET_IF(true, VALUE, values);

        SET_IF(type.config.utag, UTAG, utags);
        SET_IF(type.config.alarm_sev, ALARM_SEV, severities);
        SET_IF(type.config.alarm_cond, ALARM_COND, conditions);
        SET_IF(type.config.alarm_message, ALARM_MSG, messages);

        pv.post(std::move(val.get()));

        size_t n = secondsPastEpoch.size();
        epicsTimeStamp first{secondsPastEpoch[0], nanoseconds[0]};
        epicsTimeStamp last{secondsPastEpoch[n-1], nanoseconds[n-1]};

        log_debug_printf(LOG, "Stacker[%s].publish() %lu samples (%.6f sec)\n",
            name.c_str(), secondsPastEpoch.size(), epicsTimeDiffInSeconds(&last, &first));
    }

    void reset() {
        secondsPastEpoch.clear();
        nanoseconds.clear();
        values.clear();
        utags.clear();
        severities.clear();
        conditions.clear();
        messages.clear();
    }
};

// Called during aSub initialization
static long stacker_init(aSubRecord *prec) {

    // TODO: set alarms
    #define CHECK_INP(FT,INP,TYP)\
        do {\
            if (prec->FT != menuFtype ## TYP) {\
                errlogSevPrintf(errlogMajor, "%s: incorrect input type for " #INP "; expected " #TYP "\n", prec->name);\
                return S_dev_badInpType;\
            }\
        } while(0)

    CHECK_INP(fta, INPA, DOUBLE);
    CHECK_INP(ftb, INPB, LONG);
    CHECK_INP(ftc, INPC, DOUBLE);
    //CHECK_INP(ftd, INPD, CHAR);
    #undef CHECK_INP

    long columns = *static_cast<long*>(prec->b);
    double period_sec = *static_cast<double*>(prec->c);
    const char *output_pv_name = static_cast<const char*>(prec->d);

    TimeTableScalar::Config config(
        (columns & Columns::TIMESTAMP_UTAG) != 0,
        (columns & Columns::ALARM_SEVERITY) != 0,
        (columns & Columns::ALARM_CONDITION) != 0,
        (columns & Columns::ALARM_MESSAGE) != 0
    );

    Stacker *stacker = new Stacker(prec->name, config, period_sec, output_pv_name);
    prec->dpvt = static_cast<void*>(stacker);

    log_debug_printf(LOG, "stacker_init[%s]: initialized\n", prec->name);

    return 0;
}

// Called when aSub is processed
static long stacker_proc(aSubRecord *prec) {
    epicsTimeStamp start, end;
    epicsTimeGetCurrent(&start);

    Stacker *stacker = static_cast<Stacker*>(prec->dpvt);

    if (!stacker) {
        log_crit_printf(LOG, "stacker_proc[%s] record in bad state\n", prec->name);
        errlogSevPrintf(errlogMajor, "%s: record in bad state\n", prec->name);
        return S_dev_NoInit;
    }

    // Fetch input value
    double value = *static_cast<double*>(prec->a);

    // Fetch input timestamp and tag
    epicsTimeStamp timestamp = {};
    epicsUTag tag = 0;
    if (dbGetTimeStampTag(&prec->inpa, &timestamp, &tag) != 0) {
        log_err_printf(LOG, "stacker_proc[%s] failed to fetch input timestamp\n", prec->name);
        errlogSevPrintf(errlogMajor, "%s: failed to fetch input timestamp\n", prec->name);
    }

    // Fetch input alarm
    epicsEnum16 severity = 0;
    epicsEnum16 condition = 0;
    char message[256] = {};
    if (dbGetAlarmMsg(&prec->inpa, &condition, &severity, message, sizeof(message)) != 0) {
        log_err_printf(LOG, "stacker_proc[%s] failed to fetch input alarm\n", prec->name);
        errlogSevPrintf(errlogMajor, "%s: failed to fetch input alarm\n", prec->name);
    }

    log_debug_printf(LOG, "stacker_proc[%s]: ts=%u.%u val=%.6f tag=%llu sev=%u cond=%u msg=%s\n",
        prec->name, timestamp.secPastEpoch, timestamp.nsec, value, tag, severity, condition, message);

    stacker->push(timestamp, value, tag, severity, condition, message);
    epicsTimeGetCurrent(&end);

    return 0;
}

static void stackerPvxsRegistrar()
{
    ensurePendingHook();
}

epicsExportRegistrar(stackerPvxsRegistrar);

epicsRegisterFunction(stacker_init);
epicsRegisterFunction(stacker_proc);
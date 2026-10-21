#ifndef AMREX_AMG_MESSAGE_STATS_H_
#define AMREX_AMG_MESSAGE_STATS_H_

#include <AMReX_Config.H>
#include <AMReX_INT.H>

namespace amrex::detail {

// Counts logical MPI calls made by AMG setup, not messages inside collectives.
struct AMGMessageStats {
    Long sends = 0;
    Long bytes = 0;
    Long collectives = 0;
};

inline thread_local AMGMessageStats* active_amg_message_stats = nullptr;

struct AMGMessageScope {
    explicit AMGMessageScope (AMGMessageStats* stats)
        : previous(active_amg_message_stats)
    {
        active_amg_message_stats = stats;
    }
    ~AMGMessageScope () { active_amg_message_stats = previous; }
    AMGMessageScope (AMGMessageScope const&) = delete;
    AMGMessageScope& operator= (AMGMessageScope const&) = delete;
    AMGMessageStats* previous;
};

inline void count_amg_send (Long bytes)
{
    if (active_amg_message_stats) {
        ++active_amg_message_stats->sends;
        active_amg_message_stats->bytes += bytes;
    }
}

inline void count_amg_collective ()
{
    if (active_amg_message_stats) { ++active_amg_message_stats->collectives; }
}

}

#endif

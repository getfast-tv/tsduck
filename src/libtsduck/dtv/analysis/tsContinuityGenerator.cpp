//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2025, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/license
//
//----------------------------------------------------------------------------

#include "tsContinuityGenerator.h"
#include "tsjsonObject.h"
#include "tsNullReport.h"


//----------------------------------------------------------------------------
// Constructors and destructors
//----------------------------------------------------------------------------

ts::ContinuityGenerator::ContinuityGenerator(const PIDInitialCCMap& initialCc, Report* report) :
    _report(report != nullptr ? report : &NULLREP),
    _initial_cc(initialCc)
{
    // Initialize _pid_filter with all PID's in _initial_cc.
    for (const auto& [pid, cc] : _initial_cc) {
        _pid_filter.set(pid);
    }
}


//----------------------------------------------------------------------------
// Change the output device to report errors.
//----------------------------------------------------------------------------

void ts::ContinuityGenerator::setReport(Report* report)
{
    _report = report != nullptr ? report : &NULLREP;
}


//----------------------------------------------------------------------------
// Reset all collected information
//----------------------------------------------------------------------------

void ts::ContinuityGenerator::reset()
{
    _total_packets = 0;
    _processed_packets = 0;
    _fix_count = 0;
    _pid_states.clear();
}

//----------------------------------------------------------------------------
// PIDState access
//----------------------------------------------------------------------------

uint8_t ts::ContinuityGenerator::firstCC(PID pid) const
{
    auto it = _pid_states.find(pid);
    return it == _pid_states.end() ? INVALID_CC : it->second.first_cc;
}

uint8_t ts::ContinuityGenerator::lastCC(PID pid) const
{
    auto it = _pid_states.find(pid);
    return it == _pid_states.end() ? INVALID_CC : it->second.last_cc_out;
}

//----------------------------------------------------------------------------
// Build the first part of an error message.
//----------------------------------------------------------------------------

ts::UString ts::ContinuityGenerator::linePrefix(PID pid) const
{
    return UString::Format(u"%spacket index: %'d, PID: %n", _prefix, _total_packets, pid);
}

//----------------------------------------------------------------------------
// Log a JSON message.
//----------------------------------------------------------------------------

void ts::ContinuityGenerator::logJSON(PID pid, const UChar* type, size_t packet_count)
{
    json::Object root;
    root.add(u"index", _total_packets);
    root.add(u"pid", pid);
    root.add(u"type", type);
    if (packet_count != NPOS) {
        root.add(u"packets", packet_count);
    }
    _report->log(_severity, _prefix + root.oneLiner(*_report));
}


//----------------------------------------------------------------------------
// Detect / fix error on packet.
//----------------------------------------------------------------------------

bool ts::ContinuityGenerator::feedPacketInternal(TSPacket* pkt, bool update)
{
    assert(pkt != nullptr);
    const PID pid = pkt->getPID();
    bool result = true;

    // The null PID is never eligible for CC processing.
    if (pid != PID_NULL && _pid_filter.test(pid)) {

        const bool has_payload = pkt->hasPayload();
        if (!has_payload) {
            _processed_packets++;
            return true;
        }

        // Get or create PID context.
        PIDState& state(_pid_states[pid]);
        const bool new_pid = state.first_cc == INVALID_CC;

        if (new_pid) {
            // First packet on this PID
            if (update) {
                state.first_cc = _initial_cc[pid];
                pkt->clearDiscontinuityIndicator();
                pkt->setCC(state.first_cc);
                _fix_count++;
                result = false;
            }
        }
        else {
            // Generate a smooth stream
            if (update) {
                pkt->clearDiscontinuityIndicator();
                pkt->setCC((state.last_cc_out + 1) & CC_MASK);
                _fix_count++;
                result = false;
            }
        }

        // Save actual CC for next time.
        state.last_cc_out = pkt->getCC();
        _processed_packets++;
    }

    // Count total packets.
    _total_packets++;
    return result;
}

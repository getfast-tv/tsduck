//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2025, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/license
//
//----------------------------------------------------------------------------
//!
//!  @file
//!  Continuity counters generation and repair.
//!
//----------------------------------------------------------------------------

#pragma once
#include "tsTSPacket.h"
#include "tsReport.h"

namespace ts {
    //!
    //! Continuity counters generation.
    //! @ingroup libtsduck mpeg
    //!
    class TSDUCKDLL ContinuityGenerator
    {
    public:
        using PIDInitialCCMap = std::map<PID, uint8_t>;

        //!
        //! Constructor.
        //! @param [in] initialCc The map of PID's and their initial CC values.
        //! @param [in] report Where to report discontinuity errors. Drop errors if null.
        //!
        explicit ContinuityGenerator(const PIDInitialCCMap& initialCc, Report* report = nullptr);

        // Implementation note:
        // "= default" definitions required for copy constructor and assignments so that
        // the compiler understands that we know what we do with the pointer member _report.
        // Important: take care in case of internal modification, do not break the ownership
        // of pointers because the compiler will no longer complain.

        //!
        //! Copy constructor.
        //! @param [in] other Other instance to copy.
        //!
        ContinuityGenerator(const ContinuityGenerator& other) = default;

        //!
        //! Assignment operator.
        //! @param [in] other Other instance to copy.
        //! @return A reference to this instance.
        //!
        ContinuityGenerator& operator=(const ContinuityGenerator& other) = default;

        //!
        //! Reset all collected information.
        //! Do not change processing options (display and/or fix errors).
        //!
        void reset();

        //!
        //! Process a constant TS packet.
        //! Can be used only to report discontinuity errors.
        //! @param [in] pkt A transport stream packet.
        //! @return True if the packet has no discontinuity error. False if it has an error.
        //!
        bool feedPacket(const TSPacket& pkt) { return feedPacketInternal(const_cast<TSPacket*>(&pkt), false); }

        //!
        //! Process or modify a TS packet.
        //! @param [in,out] pkt A transport stream packet.
        //! It can be modified only when error fixing or generator mode is activated.
        //! @return True if the packet had no discontinuity error and is unmodified.
        //! False if the packet had an error or was modified.
        //!
        bool feedPacket(TSPacket& pkt) { return feedPacketInternal(&pkt, true); }

        //!
        //! Get the total number of TS packets.
        //! @return The total number of TS packets.
        //!
        PacketCounter totalPackets() const { return _total_packets; }

        //!
        //! Get the number of processed TS packets.
        //! Only packets from selected PID's are counted.
        //! @return The number of processed TS packets.
        //!
        PacketCounter processedPackets() const { return _processed_packets; }

        //!
        //! Get the number of fixed (modified) TS packets.
        //! @return The number of fixed (modified) TS packets.
        //!
        PacketCounter fixCount() const { return _fix_count; }

        //!
        //! Change the output device to report errors.
        //! @param [in] report Where to report discontinuity errors. Drop errors if null.
        //!
        void setReport(Report* report);

        //!
        //! Get the first CC in a PID.
        //! @param [in] pid The PID to check.
        //! @return The first CC value in the PID or ts::INVALID_CC when the PID is not filtered.
        //! The first CC in a PID is never modified.
        //!
        uint8_t firstCC(PID pid) const;

        //!
        //! Get the last CC in a PID.
        //! @param [in] pid The PID to check.
        //! @return The last CC value in the PID or ts::INVALID_CC when the PID is not filtered.
        //! This is the output CC value, possibly modified.
        //!
        uint8_t lastCC(PID pid) const;

        //!
        //! Log the last CC for each PID.
        //!
        void logLastCC() const;

    private:
        // PID generation state
        class PIDState
        {
        public:
            PIDState() = default;              // Constructor
            uint8_t  first_cc = INVALID_CC;    // First CC value in a PID.
            uint8_t  last_cc_out = INVALID_CC; // Last output CC value in a PID.
        };

        // A map of PID state, indexed by PID.
        using PIDStateMap = std::map<PID,PIDState>;

        // Private members.
        Report*       _report;                    // Where to report errors, never null.
        PacketCounter _total_packets = 0;         // Total number of packets.
        PacketCounter _processed_packets = 0;     // Number of processed packets.
        PacketCounter _fix_count = 0;             // Number of fixed (modified) packets.
        PIDSet        _pid_filter {};             // Current set of filtered PID's.
        PIDStateMap   _pid_states {};             // State of all PID's.
        PIDInitialCCMap _initial_cc {};           // Initial CC values for all PID's.

        // Internal version of feedPacket.
        // The packet is modified only if update is true.
        bool feedPacketInternal(TSPacket* pkt, bool update);

        // Build the first part of an error message.
        UString linePrefix(PID pid) const;

        // Log a JSON message.
        void logJSON(PID pid, const UChar* type, size_t packet_count = NPOS);
    };
}

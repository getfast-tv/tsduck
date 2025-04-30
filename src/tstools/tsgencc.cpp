//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2025, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/license
//
//----------------------------------------------------------------------------
//
//  Generate continuity counters in a TS file
//
//----------------------------------------------------------------------------

#include "tsMain.h"
#include "tsContinuityGenerator.h"
TS_MAIN(MainCode);


//----------------------------------------------------------------------------
//  Command line options
//----------------------------------------------------------------------------

namespace {
    class Options: public ts::Args
    {
        TS_NOBUILD_NOCOPY(Options);
    public:
        Options(int argc, char *argv[]);

        bool         test = false;          // Test mode
        // bool         no_replicate = false;  // Option --no-replicate-duplicated
        ts::UString  pids {};               // PID's to fix
        ts::UString  filename {};           // File name
        std::fstream file {};               // File buffer

        // Check if there was an I/O error on the file.
        // Print an error message if this is the case.
        bool fileError(const ts::UChar* message);
    };
}

// Constructor.
Options::Options(int argc, char *argv[]) :
    Args(u"Fix continuity counters in a transport stream based on previous file packets", u"[options] filename")
{
    option(u"", 0, FILENAME, 1, 1);
    help(u"", u"MPEG capture file to be modified.");

    option(u"noaction");
    help(u"noaction", u"Legacy equivalent of --no-action.");

    option(u"no-action", 'n');
    help(u"no-action", u"Display what should be performed but do not modify the file.");

    option(u"pid", 'p', STRING);
    help(u"pid",
         u"Set specific PID's to be generated and their starting values in the specificied file. "
         u"PID's that are not specified are not changed . "
         u"Format is 'pid:cc,pid:cc,...'. For example: '16:1,32:2' will generate PID 16 with CC=1 and PID 32 with CC=2.");

    analyze(argc, argv);

    filename = value(u"");
    test = present(u"no-action") || present(u"noaction");
    pids = value(u"pid");

    exitOnError();
}

// Check error on file
bool Options::fileError(const ts::UChar* message)
{
    if (file) {
        return false;
    }
    else {
        error(u"%s: %s", filename, message);
        return true;
    }
}

/**
 * @brief Parses a string in "PID=CC,PID=CC,..." format into a map of PID to initial Continuity Counter.
 *
 * The input string is expected to be a comma-separated list of key-value pairs,
 * where the key is the decimal representation of a PID (uint16_t) and the value
 * is the decimal representation of a Continuity Counter (uint8_t).
 * Example format: "256=13,257=8,100=5"
 *
 * @param input_str The input string in the specified format.
 * @return A std::map where keys are PIDs (uint16_t) and values are their initial
 * Continuity Counters (uint8_t). Returns an empty map if the input is invalid.
 * Prints warnings to stderr for invalid segments or values out of range.
 */
ts::ContinuityGenerator::PIDInitialCCMap parsePidInitialCCString(const std::string& input_str, Options& opt) {
    ts::ContinuityGenerator::PIDInitialCCMap result_map;

    std::stringstream ss(input_str);
    std::string segment; // To hold each "PID=CC" pair

    // Split the string by comma delimiter
    while(std::getline(ss, segment, ',')) {
        std::stringstream segment_ss(segment);
        std::string pid_str;
        std::string cc_str;

        // Split each segment by the '=' delimiter
        if (std::getline(segment_ss, pid_str, '=') && std::getline(segment_ss, cc_str)) {
            try {
                // Parse PID string to unsigned long first to check range
                unsigned long pid_ul = std::stoul(pid_str);
                if (pid_ul > std::numeric_limits<uint16_t>::max()) {
                    opt.warning(u"Warning: PID value out of range for uint16_t, skipping segment: %s", segment);
                    continue; // Skip this segment if PID is out of range
                }
                uint16_t pid = static_cast<uint16_t>(pid_ul);

                // Parse Continuity Counter string to unsigned long
                unsigned long cc_ul = std::stoul(cc_str);
                if (cc_ul > 15) {
                    opt.warning(u"Warning: Continuity Counter value out of range for uint8_t in segment: %s. Capping to 15.", segment.c_str());
                    // Cap the value to the maximum for uint8_t if it exceeds
                    cc_ul = 15;
                }
                uint8_t cc = static_cast<uint8_t>(cc_ul);

                // Insert into the map. If a PID is repeated, the last value for that PID will overwrite.
                result_map[pid] = cc;

            } catch (const std::invalid_argument& ia) {
                opt.error(u"Invalid number format in segment: %s - %s", segment.c_str(), ia.what());
                // Handle error or skip invalid segment
            } catch (const std::out_of_range& oor) {
                opt.error(u"Number out of range during parsing in segment: %s - %s", segment.c_str(), oor.what());
                // Handle error or skip invalid segment
            }
        } else {
            opt.error(u"Invalid segment format (expected PID=CC): %s", segment.c_str());
            // Handle error or skip malformed segment
        }
    }

    return result_map;
}

//----------------------------------------------------------------------------
//  Program entry point
//----------------------------------------------------------------------------

int MainCode(int argc, char *argv[])
{
    Options opt(argc, argv);

    ts::ContinuityGenerator::PIDInitialCCMap initialCc = ::parsePidInitialCCString(opt.pids.toUTF8(), opt);

    ts::ContinuityGenerator generator(initialCc, &opt);

    // Configure the CC analyzer.
    generator.setDisplay(true);
    generator.setMessageSeverity(opt.test ? ts::Severity::Info : ts::Severity::Verbose);

    // Open file in read/write mode (CC are overwritten)
    std::ios::openmode mode = std::ios::in | std::ios::binary;
    if (!opt.test) {
        mode |= std::ios::out;
    }

    opt.file.open(opt.filename.toUTF8().c_str(), mode);

    if (!opt.file) {
        opt.error(u"cannot open file %s", opt.filename);
        return EXIT_FAILURE;
    }

    // Process all packets in the file
    ts::TSPacket pkt;

    for (;;) {

        // Save position of current packet
        const std::ios::pos_type pos = opt.file.tellg();
        if (opt.fileError(u"error getting file position")) {
            break;
        }

        // Read a TS packet
        if (!pkt.read(opt.file, true, opt)) {
            break; // end of file
        }

        // Process packet
        if (!generator.feedPacket(pkt) && !opt.test) {
            // Packet was modified, need to rewrite it.
            // Rewind to beginning of current packet
            opt.file.seekp(pos);
            if (opt.fileError(u"error setting file position")) {
                break;
            }
            // Rewrite the packet
            pkt.write(opt.file, opt);
            if (opt.fileError(u"error rewriting packet")) {
                break;
            }
            // Make sure the get position is ok
            opt.file.seekg(opt.file.tellp());
            if (opt.fileError(u"error setting file position")) {
                break;
            }
        }
    }

    opt.info(u"%'d packets read, %'d packets updated", generator.totalPackets(), generator.fixCount());

    opt.file.close();

    return opt.valid() ? EXIT_SUCCESS : EXIT_FAILURE;
}

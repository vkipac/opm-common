/*
   Copyright 2026 Equinor ASA.

   This file is part of the Open Porous Media project (OPM).

   OPM is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   OPM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPM_IO_FLUXFILE_HPP
#define OPM_IO_FLUXFILE_HPP

#include <string>
#include <vector>

namespace Opm::EclIO {

class FluxFile
{
public:
    enum class Phase : int {
        Oil = 1,
        Water = 2,
        Gas = 4,
    };

    enum class Mode : int {
        Flux = 1,
        Pressure = 2,
        Both = 3,
    };

    enum class Sampling : int {
        Instant = 0,
        Averaged = 1,
    };

    struct Header {
        int version = formatVersion();
        int parentNx = 0;
        int parentNy = 0;
        int parentNz = 0;
        int boxI1 = 0;
        int boxJ1 = 0;
        int boxK1 = 0;
        int boxNx = 0;
        int boxNy = 0;
        int boxNz = 0;
        int numCells = 0;
        int numBoundaryFaces = 0;
        int numReportSteps = 0;
        int numPhases = 0;
        int phaseMask = 0;
        bool hasTemperature = false;
        Mode mode = Mode::Flux;
        Sampling sampling = Sampling::Averaged;

        /// Number of embedded parent summary vectors.  Matches
        /// Data::summaryKeys.size().
        int numSummaryKeys = 0;

        /// Number of embedded parent summary samples.  Matches
        /// Data::summarySamples.size().
        int numSummarySamples = 0;

        /// Whether the summary samples were taken at sub-report-step
        /// resolution.  False means summary data, if any, is only available
        /// at report-step boundaries.
        bool summaryPerTimestep = false;

        /// Minimum time, in seconds, that the producer enforced between
        /// consecutive summary samples.  Zero means every time step was
        /// sampled.  Diagnostic only.
        double summaryMinSampleInterval = 0.0;

        /// Whether the boundary records were written at sub-report-step
        /// resolution.  False means one record per report step.
        bool boundaryPerTimestep = false;

        /// Minimum time, in seconds, that the producer enforced between
        /// consecutive boundary records.  Zero means every time step was
        /// written.  Diagnostic only.
        double boundaryMinSampleInterval = 0.0;

        bool operator==(const Header& other) const;

        bool hasPhase(Phase phase) const
        {
            return (this->phaseMask & static_cast<int>(phase)) != 0;
        }
    };

    struct BoundaryFace {
        int interiorLocalCell = 0;
        int direction = 0;
        int exteriorGlobalCell = -1;
        double transmissibility = 0.0;

        /// PVT region of the cell on the far side of the face.
        ///
        /// A reduced run has no cell there, so without this it would have to
        /// substitute its own region when evaluating the density, formation
        /// volume factor and viscosity of an inflowing stream.
        ///
        /// Declared last so that existing aggregate initialisation of the
        /// preceding members keeps working.
        int exteriorPvtRegion = 0;

        bool operator==(const BoundaryFace& other) const;
    };

    /// One record of sector boundary data.
    ///
    /// Records are not tied to the report step sequence: the producer writes
    /// one per time step, subject to a minimum interval between consecutive
    /// records, so several records may share the same \c reportStep.
    ///
    /// A record covers the half-open interval
    /// <tt>[startTime, startTime + stepLength]</tt>, in seconds.  The \c rates
    /// are the TIME-AVERAGE over that interval, so that holding them constant
    /// across it reproduces the flow over the interval exactly.  The state
    /// arrays -- \c pressures, \c swat, \c sgas, \c rs, \c rv and
    /// \c temperature -- are the exterior cell values at the END of the
    /// interval.
    ///
    /// Consumers should select a record by TIME rather than by report step
    /// index, which works for either cadence.
    struct ReportStep {
        int reportStep = 0;
        int simStep = 0;
        double startTime = 0.0;
        double stepLength = 0.0;
        // Flattened face-major, then active phases in canonical Oil/Water/Gas
        // order filtered by Header::phaseMask.
        std::vector<double> rates;
        std::vector<double> pressures;
        std::vector<double> swat;
        std::vector<double> sgas;
        std::vector<double> rs;
        std::vector<double> rv;
        std::vector<double> temperature;

        /// Component mass rates across each boundary face, in the same
        /// face-major layout as \c rates, positive into the sector.
        ///
        /// These are COMPONENT masses, not phase masses: the oil entry is the
        /// mass of the oil component in both phases and the gas entry is the
        /// mass of the gas component in both phases. The producing run forms
        /// them from its own flux and the UPWIND cell's inverse formation
        /// volume factor, Rs and Rv, using the INTERIOR cell's reference
        /// densities so that the consumer's conversion back to surface volumes
        /// is exact.
        ///
        /// A consumer can impose them directly. That matters because for flow
        /// entering the sector the upstream cell lies outside it, so the
        /// consumer would otherwise have to substitute its own state; and
        /// because splitting a phase mass by Rs/Rv after the fact is not
        /// possible without that state.
        ///
        /// Declared last so that existing aggregate initialisation of the
        /// preceding members keeps working. Empty when unavailable.
        std::vector<double> massRates;

        /// Relative permeability of the exterior cell, face-major over the
        /// active phases like \c rates.
        ///
        /// Written in Pressure mode. A reduced run needs these to form the
        /// mobility of an inflowing stream: evaluating the saturation
        /// functions of its own cell at the exterior saturations would use the
        /// wrong SATNUM region, the wrong scaled end points and none of the
        /// parent's hysteresis history.
        std::vector<double> relPerm;

        /// Capillary pressure of the exterior cell relative to the reference
        /// phase, same layout as \c relPerm, and written alongside it.
        std::vector<double> capPressure;

        /// Pore-volume weighted sums over the cells OUTSIDE the sector, used to
        /// rebuild the producing run's field averages.
        ///
        /// Sixteen values: eight weighted by hydrocarbon pore volume followed
        /// by eight weighted by total pore volume, each being pressure,
        /// temperature, rs, rv, rsw, rvw, pore volume and salt concentration,
        /// already multiplied by the weight and not yet divided by it.
        ///
        /// A reduced run adds these to its own sums before forming the
        /// averages that convert a reservoir volume target to surface rates.
        /// Without them it would average over its own cells only, and a well
        /// on RESV control would be given a different target than in the full
        /// model. Keeping sums rather than averages means the reduced run still
        /// reflects its own changes, such as a well it has added.
        std::vector<double> externalRegionSums;

        bool operator==(const ReportStep& other) const;
    };

    /// One snapshot of the parent run's summary vectors.
    ///
    /// Samples are independent of the report-step sequence: the producer
    /// emits one per time step, subject to a minimum interval between
    /// consecutive samples.
    struct SummarySample {
        /// End time, in seconds, of the interval this sample represents.
        double time = 0.0;

        /// One entry per Data::summaryKeys, in the same order.
        ///
        /// Entries whose keyword is of summary type Rate hold the
        /// TIME-AVERAGE over the interval since the previous sample (or
        /// since t = 0 for the first sample), so that holding the value
        /// piecewise-constant across that interval reproduces the parent's
        /// production over it exactly.  All other entries -- Total,
        /// Pressure, Ratio, ProdIndex, Mode and Count -- hold the
        /// instantaneous value at 'time'.
        std::vector<double> values;

        bool operator==(const SummarySample& other) const;
    };

    struct Data {
        Header header;
        std::vector<std::string> names;
        std::vector<int> localToGlobal;
        std::vector<BoundaryFace> boundaryFaces;
        std::vector<std::string> summaryKeys;
        std::vector<ReportStep> reportSteps;
        std::vector<SummarySample> summarySamples;

        bool operator==(const Data& other) const;
    };

    static constexpr int formatVersion()
    {
        // 4: FLXRCON added, the pore-volume weighted sums outside the sector.
        // 3: FLXMASS holds component masses. Version 2 wrote phase masses
        //    there, which a consumer cannot split by Rs/Rv, so those files
        //    are rejected rather than silently misread.
        return 4;
    }

    static void write(const std::string& filename, bool formatted, const Data& data);
    static Data read(const std::string& filename, bool preload = true);

private:
    static void validateForWrite(const Data& data);
    static void validateAfterRead(const Data& data);
};

} // namespace Opm::EclIO

#endif // OPM_IO_FLUXFILE_HPP
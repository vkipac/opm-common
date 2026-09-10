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

        bool operator==(const BoundaryFace& other) const;
    };

    struct ReportStep {
        int reportStep = 0;
        int simStep = 0;
        double startTime = 0.0;
        double stepLength = 0.0;
        std::vector<double> rates;
        std::vector<double> pressures;
        std::vector<double> swat;
        std::vector<double> sgas;
        std::vector<double> rs;
        std::vector<double> rv;
        std::vector<double> temperature;
        std::vector<double> summaryValues;

        bool operator==(const ReportStep& other) const;
    };

    struct Data {
        Header header;
        std::vector<std::string> names;
        std::vector<int> localToGlobal;
        std::vector<BoundaryFace> boundaryFaces;
        std::vector<std::string> summaryKeys;
        std::vector<ReportStep> reportSteps;

        bool operator==(const Data& other) const;
    };

    static constexpr int formatVersion()
    {
        return 1;
    }

    static void write(const std::string& filename, bool formatted, const Data& data);
    static Data read(const std::string& filename, bool preload = true);

private:
    static void validateForWrite(const Data& data);
    static void validateAfterRead(const Data& data);
};

} // namespace Opm::EclIO

#endif // OPM_IO_FLUXFILE_HPP
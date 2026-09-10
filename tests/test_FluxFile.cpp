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

#include "config.h"

#define BOOST_TEST_MODULE Test FluxFile
#include <boost/test/unit_test.hpp>

#include <opm/io/eclipse/EclOutput.hpp>
#include <opm/io/eclipse/FluxFile.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "WorkArea.hpp"

namespace {

Opm::EclIO::FluxFile::Data sampleData()
{
    using FluxFile = Opm::EclIO::FluxFile;

    FluxFile::Data data;
    data.header.parentNx = 20;
    data.header.parentNy = 30;
    data.header.parentNz = 10;
    data.header.boxI1 = 2;
    data.header.boxJ1 = 9;
    data.header.boxK1 = 1;
    data.header.boxNx = 19;
    data.header.boxNy = 10;
    data.header.boxNz = 10;
    data.header.numCells = 4;
    data.header.numBoundaryFaces = 2;
    data.header.numReportSteps = 2;
    data.header.numPhases = 3;
    data.header.hasTemperature = true;
    data.header.mode = FluxFile::Mode::Both;
    data.header.sampling = FluxFile::Sampling::Averaged;

    data.names = {"BASE", "REGION_2", "METRIC"};
    data.localToGlobal = {248, 249, 268, -1};
    data.boundaryFaces = {
        {0, 0, 247, 1.5},
        {2, 3, 288, 2.5},
    };
    data.summaryKeys = {"FOPR", "GGPR"};
    data.reportSteps = {
        {
            0,
            0,
            0.0,
            0.5,
            {10.0, 11.0, 12.0, 20.0, 21.0, 22.0},
            {200.0, 210.0},
            {0.15, 0.25},
            {0.05, 0.15},
            {100.0, 110.0},
            {5.0, 6.0},
            {330.0, 331.0},
            {1000.0, 2000.0},
        },
        {
            1,
            3,
            0.5,
            28.0,
            {13.0, 14.0, 15.0, 23.0, 24.0, 25.0},
            {220.0, 230.0},
            {0.16, 0.26},
            {0.06, 0.16},
            {120.0, 130.0},
            {7.0, 8.0},
            {332.0, 333.0},
            {1100.0, 2100.0},
        },
    };

    return data;
}

void expectRoundTrip(const std::string& filename, const bool formatted)
{
    const auto written = sampleData();
    Opm::EclIO::FluxFile::write(filename, formatted, written);
    const auto readBack = Opm::EclIO::FluxFile::read(filename);
    BOOST_CHECK(readBack == written);
}

} // namespace

BOOST_AUTO_TEST_CASE(BinaryRoundTrip)
{
    WorkArea work;
    expectRoundTrip("SAMPLE.FLUX", false);
}

BOOST_AUTO_TEST_CASE(FormattedRoundTrip)
{
    WorkArea work;
    expectRoundTrip("SAMPLE.FFLUX", true);
}

BOOST_AUTO_TEST_CASE(RejectsUnsupportedVersion)
{
    WorkArea work;

    Opm::EclIO::EclOutput output("BADVERSION.FLUX", false);
    output.write("FLUXHEAD", std::vector<int>{99, 20, 30, 10, 2, 9, 1, 19, 10, 10, 4, 2, 1, 3, 0, 1, 1, 0});
    output.write("FLUXNAMS", std::vector<std::string>{"BASE", "REGION_2"}, 32);
    output.write("FLUXNCNT", std::vector<int>{2, 0});
    output.write("LOCGLOB", std::vector<int>{1, 2, 3, 4});
    output.write("FLUXCELL", std::vector<int>{0, 1});
    output.write("FLUXDIR", std::vector<int>{0, 1});
    output.write("FLUXNNC", std::vector<int>{10, 11});
    output.write("FLUXTRAN", std::vector<double>{1.0, 2.0});
    output.write("FLXSTEP", std::vector<int>{0});
    output.write("FLXSIM", std::vector<int>{0});
    output.write("FLXTIME", std::vector<double>{0.0});
    output.write("FLXDT", std::vector<double>{1.0});
    output.write("FLXRATE", std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});

    BOOST_CHECK_THROW(Opm::EclIO::FluxFile::read("BADVERSION.FLUX"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(RejectsMissingRequiredArray)
{
    WorkArea work;

    Opm::EclIO::EclOutput output("MISSING.FLUX", false);
    output.write("FLUXHEAD", std::vector<int>{1, 20, 30, 10, 2, 9, 1, 19, 10, 10, 4, 2, 1, 3, 0, 1, 1, 0});
    output.write("FLUXNAMS", std::vector<std::string>{"BASE", "REGION_2"}, 32);
    output.write("FLUXNCNT", std::vector<int>{2, 0});
    output.write("LOCGLOB", std::vector<int>{1, 2, 3, 4});
    output.write("FLUXCELL", std::vector<int>{0, 1});
    output.write("FLUXDIR", std::vector<int>{0, 1});
    output.write("FLUXTRAN", std::vector<double>{1.0, 2.0});
    output.write("FLXSTEP", std::vector<int>{0});
    output.write("FLXSIM", std::vector<int>{0});
    output.write("FLXTIME", std::vector<double>{0.0});
    output.write("FLXDT", std::vector<double>{1.0});
    output.write("FLXRATE", std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});

    BOOST_CHECK_THROW(Opm::EclIO::FluxFile::read("MISSING.FLUX"), std::runtime_error);
}
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

#include <opm/io/eclipse/EclFile.hpp>
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
    data.header.phaseMask = static_cast<int>(FluxFile::Phase::Oil)
                          | static_cast<int>(FluxFile::Phase::Water)
                          | static_cast<int>(FluxFile::Phase::Gas);
    data.header.hasTemperature = true;
    data.header.mode = FluxFile::Mode::Both;
    data.header.sampling = FluxFile::Sampling::Averaged;
    data.header.numSummaryKeys = 2;
    data.header.numSummarySamples = 3;
    data.header.summaryPerTimestep = true;
    data.header.summaryMinSampleInterval = 86400.0;
    data.header.boundaryPerTimestep = true;
    data.header.boundaryMinSampleInterval = 43200.0;

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
        },
    };

    // Summary samples are deliberately independent of the report-step
    // sequence: three samples spanning two report steps.
    data.summarySamples = {
        {0.5, {1000.0, 2000.0}},
        {10.0, {1100.0, 2100.0}},
        {28.5, {1200.0, 2200.0}},
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

Opm::EclIO::FluxFile::Data gasOilData()
{
    using FluxFile = Opm::EclIO::FluxFile;

    auto data = sampleData();
    data.header.numPhases = 2;
    data.header.phaseMask = static_cast<int>(FluxFile::Phase::Oil)
                          | static_cast<int>(FluxFile::Phase::Gas);

    for (auto& step : data.reportSteps) {
        step.rates = {
            step.rates[0], step.rates[2],
            step.rates[3], step.rates[5],
        };
        step.swat.clear();
    }

    return data;
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

BOOST_AUTO_TEST_CASE(OnlyPresentPhasesAreWritten)
{
    WorkArea work;

    const auto written = gasOilData();
    Opm::EclIO::FluxFile::write("GASOIL.FLUX", false, written);

    Opm::EclIO::EclFile file("GASOIL.FLUX", Opm::EclIO::EclFile::Formatted{false}, true);
    BOOST_CHECK(!file.hasKey("FLXSATW"));
    BOOST_CHECK(file.hasKey("FLXSATG"));
    BOOST_CHECK(file.hasKey("FLXRS"));
    BOOST_CHECK(file.hasKey("FLXRV"));

    const auto readBack = Opm::EclIO::FluxFile::read("GASOIL.FLUX");
    BOOST_CHECK(readBack == written);
}

BOOST_AUTO_TEST_CASE(RejectsUnsupportedVersion)
{
    WorkArea work;

    Opm::EclIO::EclOutput output("BADVERSION.FLUX", false);
    output.write("FLUXHEAD", std::vector<int>{99, 20, 30, 10, 2, 9, 1, 19, 10, 10, 4, 2, 1, 3, 0, 1, 1, 7, 0, 0, 0, 0});
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
    output.write("FLUXHEAD", std::vector<int>{2, 20, 30, 10, 2, 9, 1, 19, 10, 10, 4, 2, 1, 3, 0, 1, 1, 7, 0, 0, 0, 0});
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

BOOST_AUTO_TEST_CASE(SummarySamplesAreIndependentOfReportSteps)
{
    WorkArea work;

    const auto written = sampleData();

    // Three summary samples against two report steps: the sample series is
    // sampled at sub-report-step resolution and must survive a round trip
    // without being forced onto the report-step grid.
    BOOST_REQUIRE_EQUAL(written.summarySamples.size(), 3U);
    BOOST_REQUIRE_EQUAL(written.reportSteps.size(), 2U);

    Opm::EclIO::FluxFile::write("SMRY.FLUX", false, written);

    Opm::EclIO::EclFile file("SMRY.FLUX", Opm::EclIO::EclFile::Formatted{false}, true);
    BOOST_CHECK(file.hasKey("SMRYTIME"));
    BOOST_CHECK(file.hasKey("SMRYVALS"));
    BOOST_CHECK(file.hasKey("SMRYMINT"));

    const auto readBack = Opm::EclIO::FluxFile::read("SMRY.FLUX");
    BOOST_CHECK(readBack == written);

    BOOST_REQUIRE_EQUAL(readBack.summarySamples.size(), 3U);
    BOOST_CHECK_CLOSE(readBack.summarySamples[1].time, 10.0, 1e-12);
    BOOST_REQUIRE_EQUAL(readBack.summarySamples[1].values.size(), 2U);
    BOOST_CHECK_CLOSE(readBack.summarySamples[1].values[0], 1100.0, 1e-12);
    BOOST_CHECK_CLOSE(readBack.summarySamples[1].values[1], 2100.0, 1e-12);
    BOOST_CHECK_CLOSE(readBack.header.summaryMinSampleInterval, 86400.0, 1e-12);
    BOOST_CHECK(readBack.header.summaryPerTimestep);

    // The boundary cadence metadata travels independently of the summary data.
    BOOST_CHECK(readBack.header.boundaryPerTimestep);
    BOOST_CHECK_CLOSE(readBack.header.boundaryMinSampleInterval, 43200.0, 1e-12);
}

BOOST_AUTO_TEST_CASE(RoundTripWithoutSummarySamples)
{
    WorkArea work;

    auto written = sampleData();
    written.summaryKeys.clear();
    written.summarySamples.clear();
    written.header.numSummaryKeys = 0;
    written.header.numSummarySamples = 0;
    written.header.summaryPerTimestep = false;
    written.header.summaryMinSampleInterval = 0.0;

    Opm::EclIO::FluxFile::write("NOSMRY.FLUX", false, written);

    Opm::EclIO::EclFile file("NOSMRY.FLUX", Opm::EclIO::EclFile::Formatted{false}, true);
    BOOST_CHECK(!file.hasKey("SMRYTIME"));
    BOOST_CHECK(!file.hasKey("SMRYVALS"));

    // The boundary interval is written even when there is no summary payload.
    BOOST_CHECK(file.hasKey("FLXMINT"));

    const auto readBack = Opm::EclIO::FluxFile::read("NOSMRY.FLUX");
    BOOST_CHECK(readBack == written);
    BOOST_CHECK_CLOSE(readBack.header.boundaryMinSampleInterval, 43200.0, 1e-12);
}

BOOST_AUTO_TEST_CASE(RejectsInconsistentSummarySampleWidth)
{
    WorkArea work;

    auto data = sampleData();
    data.summarySamples[1].values.pop_back();

    BOOST_CHECK_THROW(Opm::EclIO::FluxFile::write("BADWIDTH.FLUX", false, data),
                      std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(RejectsDecreasingSummarySampleTimes)
{
    WorkArea work;

    auto data = sampleData();
    data.summarySamples[2].time = data.summarySamples[1].time - 1.0;

    BOOST_CHECK_THROW(Opm::EclIO::FluxFile::write("BADTIME.FLUX", false, data),
                      std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(RejectsSummarySampleCountMismatch)
{
    WorkArea work;

    auto data = sampleData();
    data.header.numSummarySamples = 99;

    BOOST_CHECK_THROW(Opm::EclIO::FluxFile::write("BADCOUNT.FLUX", false, data),
                      std::invalid_argument);
}
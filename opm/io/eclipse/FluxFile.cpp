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

#include <opm/io/eclipse/FluxFile.hpp>

#include <opm/common/ErrorMacros.hpp>

#include <opm/io/eclipse/EclFile.hpp>
#include <opm/io/eclipse/EclOutput.hpp>

#include <fmt/format.h>

#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

template <class Member>
using BoundaryMemberType = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Opm::EclIO::FluxFile::BoundaryFace>().*std::declval<Member>())>>;

template <class Member>
using StepMemberType = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Opm::EclIO::FluxFile::ReportStep>().*std::declval<Member>())>>;

constexpr int hasTemperatureIndex = 14;
constexpr int modeIndex = 15;
constexpr int samplingIndex = 16;
constexpr int phaseMaskIndex = 17;
constexpr int numSummaryKeysIndex = 18;
constexpr int numSummarySamplesIndex = 19;
constexpr int summaryPerTimestepIndex = 20;
constexpr int headerSize = 21;

std::vector<int> makeHeader(const Opm::EclIO::FluxFile::Header& header)
{
    return {
        header.version,
        header.parentNx,
        header.parentNy,
        header.parentNz,
        header.boxI1,
        header.boxJ1,
        header.boxK1,
        header.boxNx,
        header.boxNy,
        header.boxNz,
        header.numCells,
        header.numBoundaryFaces,
        header.numReportSteps,
        header.numPhases,
        header.hasTemperature ? 1 : 0,
        static_cast<int>(header.mode),
        static_cast<int>(header.sampling),
        header.phaseMask,
        header.numSummaryKeys,
        header.numSummarySamples,
        header.summaryPerTimestep ? 1 : 0,
    };
}

Opm::EclIO::FluxFile::Header parseHeader(const std::vector<int>& values)
{
    if (values.size() < headerSize) {
        OPM_THROW(std::runtime_error,
                  fmt::format("FLUXHEAD must contain at least {} integers, got {}",
                              headerSize, values.size()));
    }

    Opm::EclIO::FluxFile::Header header;
    header.version = values[0];
    header.parentNx = values[1];
    header.parentNy = values[2];
    header.parentNz = values[3];
    header.boxI1 = values[4];
    header.boxJ1 = values[5];
    header.boxK1 = values[6];
    header.boxNx = values[7];
    header.boxNy = values[8];
    header.boxNz = values[9];
    header.numCells = values[10];
    header.numBoundaryFaces = values[11];
    header.numReportSteps = values[12];
    header.numPhases = values[13];
    header.hasTemperature = values[hasTemperatureIndex] != 0;
    header.mode = static_cast<Opm::EclIO::FluxFile::Mode>(values[modeIndex]);
    header.sampling = static_cast<Opm::EclIO::FluxFile::Sampling>(values[samplingIndex]);
    header.phaseMask = values[phaseMaskIndex];
    header.numSummaryKeys = values[numSummaryKeysIndex];
    header.numSummarySamples = values[numSummarySamplesIndex];
    header.summaryPerTimestep = values[summaryPerTimestepIndex] != 0;
    return header;
}

template <class T>
void requireArray(Opm::EclIO::EclFile& file, const std::string& name)
{
    if (!file.hasKey(name)) {
        OPM_THROW(std::runtime_error, fmt::format("Missing required FLUX array '{}'", name));
    }

    file.template get<T>(name);
}

template <class T>
const std::vector<T>& optionalArray(Opm::EclIO::EclFile& file, const std::string& name)
{
    static const std::vector<T> empty;
    if (!file.hasKey(name)) {
        return empty;
    }

    return file.template get<T>(name);
}

template <class Member>
std::vector<BoundaryMemberType<Member>> flattenBoundary(
    const std::vector<Opm::EclIO::FluxFile::BoundaryFace>& faces,
    Member member)
{
    std::vector<BoundaryMemberType<Member>> values;
    values.reserve(faces.size());
    for (const auto& face : faces) {
        values.push_back(face.*member);
    }
    return values;
}

template <class Member>
std::vector<StepMemberType<Member>> flattenStepMeta(
    const std::vector<Opm::EclIO::FluxFile::ReportStep>& steps,
    Member member)
{
    std::vector<StepMemberType<Member>> values;
    values.reserve(steps.size());
    for (const auto& step : steps) {
        values.push_back(step.*member);
    }
    return values;
}

std::vector<double> flattenVectors(const std::vector<Opm::EclIO::FluxFile::ReportStep>& steps,
                                   const std::vector<double> Opm::EclIO::FluxFile::ReportStep::* member)
{
    std::vector<double> values;
    for (const auto& step : steps) {
        const auto& field = step.*member;
        values.insert(values.end(), field.begin(), field.end());
    }
    return values;
}

std::vector<std::string> makeNames(const Opm::EclIO::FluxFile::Data& data)
{
    auto names = data.names;
    names.insert(names.end(), data.summaryKeys.begin(), data.summaryKeys.end());
    return names;
}

std::vector<double>
flattenSummaryTimes(const std::vector<Opm::EclIO::FluxFile::SummarySample>& samples)
{
    std::vector<double> times;
    times.reserve(samples.size());
    for (const auto& sample : samples) {
        times.push_back(sample.time);
    }
    return times;
}

std::vector<double>
flattenSummaryValues(const std::vector<Opm::EclIO::FluxFile::SummarySample>& samples)
{
    std::vector<double> values;
    for (const auto& sample : samples) {
        values.insert(values.end(), sample.values.begin(), sample.values.end());
    }
    return values;
}

bool hasAnyValues(const std::vector<Opm::EclIO::FluxFile::ReportStep>& steps,
                  const std::vector<double> Opm::EclIO::FluxFile::ReportStep::* member)
{
    for (const auto& step : steps) {
        if (!(step.*member).empty()) {
            return true;
        }
    }

    return false;
}

Opm::EclIO::EclFile openFluxFile(const std::string& filename, bool preload)
{
    try {
        return {filename, Opm::EclIO::EclFile::Formatted{false}, preload};
    }
    catch (const std::exception&) {
        return {filename, Opm::EclIO::EclFile::Formatted{true}, preload};
    }
}

} // namespace

namespace Opm::EclIO {

bool FluxFile::Header::operator==(const Header& other) const = default;

bool FluxFile::BoundaryFace::operator==(const BoundaryFace& other) const = default;

bool FluxFile::ReportStep::operator==(const ReportStep& other) const = default;

bool FluxFile::SummarySample::operator==(const SummarySample& other) const = default;

bool FluxFile::Data::operator==(const Data& other) const = default;

void FluxFile::write(const std::string& filename, bool formatted, const Data& data)
{
    validateForWrite(data);

    EclOutput output(filename, formatted);
    output.write("FLUXHEAD", makeHeader(data.header));
    output.write("FLUXNAMS", makeNames(data), 32);
    output.write("FLUXNCNT", std::vector<int>{static_cast<int>(data.names.size()), static_cast<int>(data.summaryKeys.size())});
    output.write("LOCGLOB", data.localToGlobal);
    output.write("FLUXCELL", flattenBoundary(data.boundaryFaces, &BoundaryFace::interiorLocalCell));
    output.write("FLUXDIR", flattenBoundary(data.boundaryFaces, &BoundaryFace::direction));
    output.write("FLUXNNC", flattenBoundary(data.boundaryFaces, &BoundaryFace::exteriorGlobalCell));
    output.write("FLUXTRAN", flattenBoundary(data.boundaryFaces, &BoundaryFace::transmissibility));
    output.write("FLXSTEP", flattenStepMeta(data.reportSteps, &ReportStep::reportStep));
    output.write("FLXSIM", flattenStepMeta(data.reportSteps, &ReportStep::simStep));
    output.write("FLXTIME", flattenStepMeta(data.reportSteps, &ReportStep::startTime));
    output.write("FLXDT", flattenStepMeta(data.reportSteps, &ReportStep::stepLength));

    if ((static_cast<int>(data.header.mode) & static_cast<int>(Mode::Flux)) != 0) {
        output.write("FLXRATE", flattenVectors(data.reportSteps, &ReportStep::rates));
    }

    if ((static_cast<int>(data.header.mode) & static_cast<int>(Mode::Pressure)) != 0) {
        output.write("FLXPRES", flattenVectors(data.reportSteps, &ReportStep::pressures));

        if (data.header.hasPhase(Phase::Water)) {
            output.write("FLXSATW", flattenVectors(data.reportSteps, &ReportStep::swat));
        }

        if (data.header.hasPhase(Phase::Gas)) {
            output.write("FLXSATG", flattenVectors(data.reportSteps, &ReportStep::sgas));
        }

        if (hasAnyValues(data.reportSteps, &ReportStep::rs)) {
            output.write("FLXRS", flattenVectors(data.reportSteps, &ReportStep::rs));
        }

        if (hasAnyValues(data.reportSteps, &ReportStep::rv)) {
            output.write("FLXRV", flattenVectors(data.reportSteps, &ReportStep::rv));
        }

        if (data.header.hasTemperature) {
            output.write("FLXTEMP", flattenVectors(data.reportSteps, &ReportStep::temperature));
        }
    }

    if (!data.summaryKeys.empty()) {
        output.write("SMRYMINT", std::vector<double>{data.header.summaryMinSampleInterval});
        output.write("SMRYTIME", flattenSummaryTimes(data.summarySamples));
        output.write("SMRYVALS", flattenSummaryValues(data.summarySamples));
    }

    output.flushStream();
}

FluxFile::Data FluxFile::read(const std::string& filename, bool preload)
{
    EclFile file = openFluxFile(filename, preload);

    requireArray<int>(file, "FLUXHEAD");
    requireArray<std::string>(file, "FLUXNAMS");
    requireArray<int>(file, "FLUXNCNT");
    requireArray<int>(file, "LOCGLOB");
    requireArray<int>(file, "FLUXCELL");
    requireArray<int>(file, "FLUXDIR");
    requireArray<int>(file, "FLUXNNC");
    requireArray<double>(file, "FLUXTRAN");
    requireArray<int>(file, "FLXSTEP");
    requireArray<int>(file, "FLXSIM");
    requireArray<double>(file, "FLXTIME");
    requireArray<double>(file, "FLXDT");

    Data data;
    data.header = parseHeader(file.get<int>("FLUXHEAD"));

    if (data.header.version != formatVersion()) {
        OPM_THROW(std::runtime_error,
                  fmt::format("Unsupported FLUXHEAD version {} in '{}'; expected {}",
                              data.header.version, filename, formatVersion()));
    }

    const auto fluxEnabled = (static_cast<int>(data.header.mode) & static_cast<int>(Mode::Flux)) != 0;
    const auto pressureEnabled = (static_cast<int>(data.header.mode) & static_cast<int>(Mode::Pressure)) != 0;

    if (fluxEnabled) {
        requireArray<double>(file, "FLXRATE");
    }

    if (pressureEnabled) {
        requireArray<double>(file, "FLXPRES");
        if (data.header.hasPhase(Phase::Water)) {
            requireArray<double>(file, "FLXSATW");
        }
        if (data.header.hasPhase(Phase::Gas)) {
            requireArray<double>(file, "FLXSATG");
        }
        if (data.header.hasTemperature) {
            requireArray<double>(file, "FLXTEMP");
        }
    }

    const auto& fluxnams = file.get<std::string>("FLUXNAMS");
    const auto& nameCounts = file.get<int>("FLUXNCNT");
    if (nameCounts.size() != 2) {
        OPM_THROW(std::runtime_error,
                  fmt::format("FLUXNCNT must contain 2 integers, got {}", nameCounts.size()));
    }

    const auto numNames = static_cast<std::size_t>(nameCounts[0]);
    const auto numSummaryKeys = static_cast<std::size_t>(nameCounts[1]);
    if (fluxnams.size() != numNames + numSummaryKeys) {
        OPM_THROW(std::runtime_error,
                  fmt::format("FLUXNAMS size {} does not match FLUXNCNT ({}, {})",
                              fluxnams.size(), numNames, numSummaryKeys));
    }

    data.names.assign(fluxnams.begin(), fluxnams.begin() + numNames);
    data.summaryKeys.assign(fluxnams.begin() + numNames, fluxnams.end());
    data.localToGlobal = file.get<int>("LOCGLOB");

    const auto& fluxCell = file.get<int>("FLUXCELL");
    const auto& fluxDir = file.get<int>("FLUXDIR");
    const auto& fluxNnc = file.get<int>("FLUXNNC");
    const auto& fluxTran = file.get<double>("FLUXTRAN");
    if (!(fluxCell.size() == fluxDir.size() && fluxDir.size() == fluxNnc.size() && fluxNnc.size() == fluxTran.size())) {
        OPM_THROW(std::runtime_error, "Boundary face arrays in FLUX file have inconsistent sizes");
    }

    data.boundaryFaces.reserve(fluxCell.size());
    for (std::size_t index = 0; index < fluxCell.size(); ++index) {
        data.boundaryFaces.push_back(BoundaryFace{fluxCell[index], fluxDir[index], fluxNnc[index], fluxTran[index]});
    }

    const auto& reportSteps = file.get<int>("FLXSTEP");
    const auto& simSteps = file.get<int>("FLXSIM");
    const auto& startTimes = file.get<double>("FLXTIME");
    const auto& stepLengths = file.get<double>("FLXDT");
    if (!(reportSteps.size() == simSteps.size() && simSteps.size() == startTimes.size() && startTimes.size() == stepLengths.size())) {
        OPM_THROW(std::runtime_error, "Report-step metadata arrays in FLUX file have inconsistent sizes");
    }

    const auto& rates = optionalArray<double>(file, "FLXRATE");
    const auto& pressures = optionalArray<double>(file, "FLXPRES");
    const auto& swat = optionalArray<double>(file, "FLXSATW");
    const auto& sgas = optionalArray<double>(file, "FLXSATG");
    const auto& rs = optionalArray<double>(file, "FLXRS");
    const auto& rv = optionalArray<double>(file, "FLXRV");
    const auto& temperature = optionalArray<double>(file, "FLXTEMP");
    const auto& summaryTimes = optionalArray<double>(file, "SMRYTIME");
    const auto& summaryValues = optionalArray<double>(file, "SMRYVALS");
    const auto& summaryMinInterval = optionalArray<double>(file, "SMRYMINT");

    data.header.summaryMinSampleInterval =
        summaryMinInterval.empty() ? 0.0 : summaryMinInterval.front();

    data.reportSteps.resize(reportSteps.size());
    auto splitPerStep = [&](const std::vector<double>& flat, std::size_t perStep, auto setter, const std::string& name) {
        if (flat.empty()) {
            return;
        }
        if (flat.size() != perStep * data.reportSteps.size()) {
            OPM_THROW(std::runtime_error,
                      fmt::format("{} size {} does not match {} values per step x {} steps",
                                  name, flat.size(), perStep, data.reportSteps.size()));
        }
        for (std::size_t step = 0; step < data.reportSteps.size(); ++step) {
            auto begin = flat.begin() + static_cast<std::ptrdiff_t>(step * perStep);
            setter(data.reportSteps[step], std::vector<double>(begin, begin + static_cast<std::ptrdiff_t>(perStep)));
        }
    };

    for (std::size_t step = 0; step < data.reportSteps.size(); ++step) {
        data.reportSteps[step].reportStep = reportSteps[step];
        data.reportSteps[step].simStep = simSteps[step];
        data.reportSteps[step].startTime = startTimes[step];
        data.reportSteps[step].stepLength = stepLengths[step];
    }

    const auto perFacePhaseValues = static_cast<std::size_t>(data.header.numBoundaryFaces * data.header.numPhases);
    const auto perFaceValues = static_cast<std::size_t>(data.header.numBoundaryFaces);
    const auto perSummaryValues = data.summaryKeys.size();

    splitPerStep(rates, perFacePhaseValues,
                 [](ReportStep& step, std::vector<double> values) { step.rates = std::move(values); },
                 "FLXRATE");
    splitPerStep(pressures, perFaceValues,
                 [](ReportStep& step, std::vector<double> values) { step.pressures = std::move(values); },
                 "FLXPRES");
    splitPerStep(swat, perFaceValues,
                 [](ReportStep& step, std::vector<double> values) { step.swat = std::move(values); },
                 "FLXSATW");
    splitPerStep(sgas, perFaceValues,
                 [](ReportStep& step, std::vector<double> values) { step.sgas = std::move(values); },
                 "FLXSATG");
    splitPerStep(rs, perFaceValues,
                 [](ReportStep& step, std::vector<double> values) { step.rs = std::move(values); },
                 "FLXRS");
    splitPerStep(rv, perFaceValues,
                 [](ReportStep& step, std::vector<double> values) { step.rv = std::move(values); },
                 "FLXRV");
    splitPerStep(temperature, perFaceValues,
                 [](ReportStep& step, std::vector<double> values) { step.temperature = std::move(values); },
                 "FLXTEMP");

    if (!summaryTimes.empty() || !summaryValues.empty()) {
        if (perSummaryValues == 0) {
            OPM_THROW(std::runtime_error,
                      "FLUX file contains summary samples but no summary keys");
        }

        if (summaryValues.size() != perSummaryValues * summaryTimes.size()) {
            OPM_THROW(std::runtime_error,
                      fmt::format("SMRYVALS size {} does not match {} keys x {} samples",
                                  summaryValues.size(), perSummaryValues, summaryTimes.size()));
        }

        data.summarySamples.resize(summaryTimes.size());
        for (std::size_t sample = 0; sample < summaryTimes.size(); ++sample) {
            const auto begin = summaryValues.begin()
                + static_cast<std::ptrdiff_t>(sample * perSummaryValues);

            data.summarySamples[sample].time = summaryTimes[sample];
            data.summarySamples[sample].values
                .assign(begin, begin + static_cast<std::ptrdiff_t>(perSummaryValues));
        }
    }

    validateAfterRead(data);
    return data;
}

void FluxFile::validateForWrite(const Data& data)
{
    if (data.header.version != formatVersion()) {
        OPM_THROW(std::invalid_argument,
                  fmt::format("FluxFile writer only supports version {}, got {}",
                              formatVersion(), data.header.version));
    }

    if (data.header.numCells != static_cast<int>(data.localToGlobal.size())) {
        OPM_THROW(std::invalid_argument,
                  fmt::format("Header numCells {} does not match LOCGLOB size {}",
                              data.header.numCells, data.localToGlobal.size()));
    }

    if (data.header.numBoundaryFaces != static_cast<int>(data.boundaryFaces.size())) {
        OPM_THROW(std::invalid_argument,
                  fmt::format("Header numBoundaryFaces {} does not match boundary face count {}",
                              data.header.numBoundaryFaces, data.boundaryFaces.size()));
    }

    if (data.header.numReportSteps != static_cast<int>(data.reportSteps.size())) {
        OPM_THROW(std::invalid_argument,
                  fmt::format("Header numReportSteps {} does not match report step count {}",
                              data.header.numReportSteps, data.reportSteps.size()));
    }

    if (data.header.numPhases != std::popcount(static_cast<unsigned int>(data.header.phaseMask))) {
        OPM_THROW(std::invalid_argument,
                  fmt::format("Header numPhases {} does not match phaseMask 0x{:x}",
                              data.header.numPhases, data.header.phaseMask));
    }

    const auto fluxEnabled = (static_cast<int>(data.header.mode) & static_cast<int>(Mode::Flux)) != 0;
    const auto pressureEnabled = (static_cast<int>(data.header.mode) & static_cast<int>(Mode::Pressure)) != 0;
    const auto perFaceValues = static_cast<std::size_t>(data.header.numBoundaryFaces);
    const auto perFacePhaseValues = static_cast<std::size_t>(data.header.numBoundaryFaces * data.header.numPhases);

    for (const auto& step : data.reportSteps) {
        if (fluxEnabled && step.rates.size() != perFacePhaseValues) {
            OPM_THROW(std::invalid_argument,
                      fmt::format("Each FLXRATE step must contain {} values, got {}",
                                  perFacePhaseValues, step.rates.size()));
        }

        if (pressureEnabled) {
            const auto checkSize = [&](const std::vector<double>& values, const std::string& name) {
                if (values.size() != perFaceValues) {
                    OPM_THROW(std::invalid_argument,
                              fmt::format("Each {} step must contain {} values, got {}",
                                          name, perFaceValues, values.size()));
                }
            };

            checkSize(step.pressures, "FLXPRES");

            if (data.header.hasPhase(Phase::Water)) {
                checkSize(step.swat, "FLXSATW");
            }
            else if (!step.swat.empty()) {
                OPM_THROW(std::invalid_argument, "Water saturation values provided, but water is not active");
            }

            if (data.header.hasPhase(Phase::Gas)) {
                checkSize(step.sgas, "FLXSATG");
            }
            else if (!step.sgas.empty()) {
                OPM_THROW(std::invalid_argument, "Gas saturation values provided, but gas is not active");
            }

            if (!step.rs.empty()) {
                checkSize(step.rs, "FLXRS");
            }

            if (!step.rv.empty()) {
                checkSize(step.rv, "FLXRV");
            }

            if (data.header.hasTemperature) {
                checkSize(step.temperature, "FLXTEMP");
            }
            else if (!step.temperature.empty()) {
                OPM_THROW(std::invalid_argument, "Temperature values provided, but header.hasTemperature is false");
            }
        }
    }

    if (data.header.numSummaryKeys != static_cast<int>(data.summaryKeys.size())) {
        OPM_THROW(std::invalid_argument,
                  fmt::format("Header numSummaryKeys {} does not match summary key count {}",
                              data.header.numSummaryKeys, data.summaryKeys.size()));
    }

    if (data.header.numSummarySamples != static_cast<int>(data.summarySamples.size())) {
        OPM_THROW(std::invalid_argument,
                  fmt::format("Header numSummarySamples {} does not match summary sample count {}",
                              data.header.numSummarySamples, data.summarySamples.size()));
    }

    if (!data.summarySamples.empty() && data.summaryKeys.empty()) {
        OPM_THROW(std::invalid_argument,
                  "Summary samples provided, but no summary keys are defined");
    }

    auto previousTime = -std::numeric_limits<double>::max();
    for (const auto& sample : data.summarySamples) {
        if (sample.values.size() != data.summaryKeys.size()) {
            OPM_THROW(std::invalid_argument,
                      fmt::format("Each summary sample must contain {} values, got {}",
                                  data.summaryKeys.size(), sample.values.size()));
        }

        if (sample.time < previousTime) {
            OPM_THROW(std::invalid_argument,
                      fmt::format("Summary sample times must be non-decreasing, "
                                  "got {} after {}", sample.time, previousTime));
        }

        previousTime = sample.time;
    }
}

void FluxFile::validateAfterRead(const Data& data)
{
    if (data.header.numCells != static_cast<int>(data.localToGlobal.size())) {
        OPM_THROW(std::runtime_error,
                  fmt::format("LOCGLOB size {} does not match FLUXHEAD numCells {}",
                              data.localToGlobal.size(), data.header.numCells));
    }

    if (data.header.numBoundaryFaces != static_cast<int>(data.boundaryFaces.size())) {
        OPM_THROW(std::runtime_error,
                  fmt::format("Boundary face count {} does not match FLUXHEAD numBoundaryFaces {}",
                              data.boundaryFaces.size(), data.header.numBoundaryFaces));
    }

    if (data.header.numReportSteps != static_cast<int>(data.reportSteps.size())) {
        OPM_THROW(std::runtime_error,
                  fmt::format("Report step count {} does not match FLUXHEAD numReportSteps {}",
                              data.reportSteps.size(), data.header.numReportSteps));
    }

    if (data.header.numPhases != std::popcount(static_cast<unsigned int>(data.header.phaseMask))) {
        OPM_THROW(std::runtime_error,
                  fmt::format("FLUXHEAD numPhases {} does not match phaseMask 0x{:x}",
                              data.header.numPhases, data.header.phaseMask));
    }

    if (data.header.numSummaryKeys != static_cast<int>(data.summaryKeys.size())) {
        OPM_THROW(std::runtime_error,
                  fmt::format("Summary key count {} does not match FLUXHEAD numSummaryKeys {}",
                              data.summaryKeys.size(), data.header.numSummaryKeys));
    }

    if (data.header.numSummarySamples != static_cast<int>(data.summarySamples.size())) {
        OPM_THROW(std::runtime_error,
                  fmt::format("Summary sample count {} does not match FLUXHEAD numSummarySamples {}",
                              data.summarySamples.size(), data.header.numSummarySamples));
    }
}

} // namespace Opm::EclIO
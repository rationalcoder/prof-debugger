#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <cerrno>
#include <memory>
#include <vector>
#include <windows.h>
#include "bucket_array.hpp"

using namespace std;

struct FileVersion
{
    uint16_t patch = 0;
    uint8_t minor = 0;
    uint8_t major = 0;
};

// Note: you can't memcpy this from binary b/c of padding,
// so the fields are out of order.
struct FileHeader
{
    uint32_t signature = 0;
    FileVersion version;
    uint64_t profiledProcessId = 0;
    int64_t cpuFrequencyRatio = 0;
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
    uint32_t numBlocks = 0;
    uint32_t numDescriptors = 0; // In the binary format, this...
    uint64_t blocksMemoryUsage = 0; // goes after this.
    uint64_t descriptorsMemoryUsage = 0;

    uint32_t descriptorSectionSize() const
    {
        return numDescriptors * 2 + descriptorsMemoryUsage;
    }
};

struct Descriptor
{
    uint32_t id = 0;
    uint32_t line = 0;
    uint32_t color = 0;
    uint8_t type = 0;
    uint8_t status = 0;
    uint16_t nameLength = 0;
    std::unique_ptr<char[]> name;
    std::unique_ptr<char[]> fileName;
};

struct SizedDescriptor
{
    uint16_t size = 0;
    Descriptor descriptor;
};

struct Block
{
};

struct BlockList
{
};

using SizedDescriptorList = std::vector<SizedDescriptor>;

struct ProfilerData
{
    FileHeader header;
    SizedDescriptorList sizedDescriptorList;
};

bool parse_args(int argc, char** argv, std::string& inFile1, std::string& inFile2)
{
    (void)argc;
    inFile1 = argv[1];
    inFile2 = argv[2];
    return true;
}

bool parse_prof_file(FILE* file, ProfilerData& data)
{
    // Parse the Header
    FileHeader& header = data.header;
    auto contiguousOffset = offsetof(FileHeader, numBlocks);
    if (fread(&header, 1, contiguousOffset, file) != contiguousOffset) return false;

    if (fread(&header.numBlocks, 1, sizeof(uint32_t), file) != sizeof(uint32_t)) return false;
    if (fread(&header.blocksMemoryUsage, 1, sizeof(uint64_t), file) != sizeof(uint64_t)) return false;
    if (fread(&header.numDescriptors, 1, sizeof(uint32_t), file) != sizeof(uint32_t)) return false;
    if (fread(&header.descriptorsMemoryUsage, 1, sizeof(uint64_t), file) != sizeof(uint64_t)) return false;

    // Parse the Descriptors
    SizedDescriptorList& list = data.sizedDescriptorList;
    list.resize(header.numDescriptors);

    int numDescriptors = (int)header.numDescriptors;
    for (int i = 0; i < numDescriptors; ++i) {
        constexpr size_t dynamicPartOffset = offsetof(Descriptor, name);
        SizedDescriptor& cur = list[i];
        Descriptor& desc = cur.descriptor;

        // Read the contiguous parts, up until the name and file name fields.
        if (fread(&cur, 1, sizeof(uint16_t), file) != sizeof(uint16_t)) return false;
        if (fread(&desc, 1, dynamicPartOffset, file) != dynamicPartOffset) return false;

        uint16_t nameLength = desc.nameLength;
        uint16_t fileNameLength = cur.size - dynamicPartOffset - nameLength;
        desc.name = std::unique_ptr<char[]>(new char[nameLength]);
        desc.fileName = std::unique_ptr<char[]>(new char[fileNameLength]);

        if (fread(desc.name.get(), 1, nameLength, file) != nameLength) return false;
        if (fread(desc.fileName.get(), 1, fileNameLength, file) != fileNameLength) return false;
    }

    return true;
}

void write_profiler_data(const ProfilerData& data, FILE* file)
{
    const FileHeader& header = data.header;
    fprintf(file, "Header Contents:\n");
    char* signature = (char*)&header.signature;
    fprintf(file, "Signature: %d(%c%c%c%c)\n", header.signature, signature[0], signature[1], signature[2], signature[3]);

    const FileVersion& version = header.version;
    fprintf(file, "Version: %d.%d.%d\n", version.major, version.minor, version.patch);
    fprintf(file, "Profiled Process ID: %llu\n", header.profiledProcessId);
    fprintf(file, "CPU Frequency Ratio: %lld\n", header.cpuFrequencyRatio);
    fprintf(file, "Begin Time: %llu\n", header.beginTime);

    double duration = 100*((header.endTime - header.beginTime)/(double)header.cpuFrequencyRatio);
    fprintf(file, "End Time: %llu (Total time: %f ms)\n", header.endTime, duration);
    fprintf(file, "Num Blocks: %u\n", header.numBlocks);
    fprintf(file, "Blocks Memory Usage: %llu\n", header.blocksMemoryUsage);
    fprintf(file, "Num Descriptors: %u\n", header.numDescriptors);
    fprintf(file, "Descriptor Memory Usage: %llu\n", header.descriptorsMemoryUsage);

    fprintf(file, "Descriptors:\n");
}

bool print_file(const std::string& fileName)
{
    FILE* file = fopen(fileName.c_str(), "rb");
    if (!file) {
        printf("Failed to open \"%s\" for reading: %s\n", fileName.c_str(), strerror(errno));
        return false;
    }

    ProfilerData data;
    parse_prof_file(file, data);
    fprintf(stdout, "File: %s\n", fileName.c_str());
    write_profiler_data(data, stdout);

    fclose(file);
    return true;
}

int main(int argc, char** argv)
{
    std::string inFile1;
    std::string inFile2;
    if (!parse_args(argc, argv, inFile1, inFile2)) return EXIT_FAILURE;
    if (!print_file(inFile1)) return EXIT_FAILURE;

    printf("\n");
    if (!print_file(inFile2)) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

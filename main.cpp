#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <thread>
#include <cerrno>
#include <memory>
#include <vector>
#include <unordered_map>
#include <windows.h>

#include <fmt/fmt.hpp>

#define CHECK_FREAD(dst, file)\
do {\
    int result = fread(dst, 1, sizeof(*dst), file);\
    if (result != sizeof(*dst)) {\
        fprintf(stderr, "Error reading field: " #dst\
                        ": (Expected %d, Actual: %d) %s\n", sizeof(*dst), result, \
                        strerror(errno));\
        return false;\
    }\
} while (false)

#define CHECK_FREAD_CUSTOM(dst, file, size)\
do {\
    if (fread(dst, 1, size, file) != size) return false;\
} while (false)

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

// ARGB (Little Endian Order)
union Color
{
    struct
    {
        uint8_t b;
        uint8_t g;
        uint8_t r;
        uint8_t a;
    };
    uint32_t packed = 0;
};

struct Descriptor
{
    uint32_t id = 0;
    uint32_t line = 0;
    Color color;
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

struct ThreadInfo
{
    uint64_t id;
    std::unique_ptr<char[]> name;
    uint32_t numContextSwitches;
    uint32_t numBlocks;
    uint16_t nameLength;
};

struct ContextSwitch
{
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
    uint64_t targetThreadId = 0;
    std::unique_ptr<char[]> processInfo;
};

struct Block
{
    uint64_t beginTime = 0;
    uint64_t endTime = 0;
    uint32_t id = 0;
    std::unique_ptr<char[]> runtimeName;
};

struct SizedContextSwitch
{
    uint16_t size = 0;
    ContextSwitch contextSwitch;
};

struct SizedBlock
{
    uint16_t size = 0;
    Block block;
};

using SizedContextSwitchList = std::vector<SizedContextSwitch>;
using SizedBlockList = std::vector<SizedBlock>;
using SizedDescriptorList = std::vector<SizedDescriptor>;

struct ThreadData
{
    ThreadInfo info;
    SizedContextSwitchList switches;
    SizedBlockList blocks;
};

using ThreadDataList = std::vector<ThreadData>;

struct ProfilerData
{
    FileHeader header;
    SizedDescriptorList sizedDescriptorList;
    ThreadDataList threadDataList;
};

bool parse_args(int argc, char** argv, std::string& inFile1, std::string& inFile2)
{
    (void)argc;
    inFile1 = argv[1];
    inFile2 = argv[2];
    return true;
}

bool parse_header(FILE* file, ProfilerData& data)
{
    FileHeader& header = data.header;
    auto contiguousOffset = offsetof(FileHeader, numBlocks);
    CHECK_FREAD_CUSTOM(&header, file, contiguousOffset);
    CHECK_FREAD(&header.numBlocks, file);
    CHECK_FREAD(&header.blocksMemoryUsage, file);
    CHECK_FREAD(&header.numDescriptors, file);
    CHECK_FREAD(&header.descriptorsMemoryUsage, file);

    return true;
}

bool parse_descriptors(FILE* file, ProfilerData& data)
{
    SizedDescriptorList& list = data.sizedDescriptorList;
    list.resize(data.header.numDescriptors);

    int numDescriptors = (int)data.header.numDescriptors;
    for (int i = 0; i < numDescriptors; i++) {
        constexpr size_t dynamicPartOffset = offsetof(Descriptor, name);
        SizedDescriptor& cur = list[i];
        Descriptor& desc = cur.descriptor;

        // Read the contiguous parts, up until the name and file name fields.
        CHECK_FREAD(&cur.size, file);
        CHECK_FREAD_CUSTOM(&desc, file, dynamicPartOffset);

        uint16_t nameLength = desc.nameLength;
        uint16_t fileNameLength = cur.size - dynamicPartOffset - nameLength;
        desc.name = std::unique_ptr<char[]>(new char[nameLength]);
        desc.fileName = std::unique_ptr<char[]>(new char[fileNameLength]);

        CHECK_FREAD_CUSTOM(desc.name.get(), file, desc.nameLength);
        CHECK_FREAD_CUSTOM(desc.fileName.get(), file, fileNameLength);
    }

    return true;
}

bool parse_thread_data(FILE* file, ProfilerData& data)
{
    // "THREAD EVENTS AND BLOCKS" (See doc.) sections continue until eof.

    int cur = ftell(file);
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek: %s\n", strerror(errno));
        return false;
    }

    int fileSize = ftell(file);
    if (fseek(file, cur, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to seek: %s\n", strerror(errno));
        return false;
    }

    int offset = cur;
    for (int i = 0; offset < fileSize; i++, offset = ftell(file)) {
        data.threadDataList.emplace_back();

        ThreadData& threadData = data.threadDataList[i];
        ThreadInfo& info = threadData.info;
        SizedContextSwitchList& cSwitches = threadData.switches;
        SizedBlockList& blocks = threadData.blocks;

        CHECK_FREAD(&info.id, file);
        CHECK_FREAD(&info.nameLength, file);

        uint32_t nameLength = info.nameLength;
        info.name = std::unique_ptr<char[]>(new char[nameLength]);
        CHECK_FREAD_CUSTOM(info.name.get(), file, nameLength);


        CHECK_FREAD(&info.numContextSwitches, file);

        uint32_t numContextSwitches = info.numContextSwitches;
        cSwitches.resize(numContextSwitches);
        for (int i = 0; i < (int)numContextSwitches; i++) {
            constexpr uint32_t contiguousOffset = offsetof(ContextSwitch, processInfo);
            SizedContextSwitch& ss = cSwitches[i];
            ContextSwitch& cur = ss.contextSwitch;

            CHECK_FREAD(&ss.size, file);
            CHECK_FREAD_CUSTOM(&cur, file, contiguousOffset);

            uint32_t processInfoSize = ss.size - contiguousOffset;
            cur.processInfo = std::unique_ptr<char[]>(new char[processInfoSize]);
        }

        CHECK_FREAD(&info.numBlocks, file);
        uint32_t numBlocks = info.numBlocks;
        blocks.resize(numBlocks);
        for (int i = 0; i < (int)numBlocks; i++) {
            constexpr uint32_t contiguousOffset = sizeof(uint64_t)*2 + sizeof(uint32_t);
            SizedBlock& sb = blocks[i];
            Block& cur = sb.block;

            CHECK_FREAD(&sb.size, file);
            CHECK_FREAD(&cur.beginTime, file);
            CHECK_FREAD(&cur.endTime, file);
            CHECK_FREAD(&cur.id, file);

            if (sb.size == 0) sb.size = 21;

            uint32_t runtimeNameSize = sb.size - contiguousOffset;
            cur.runtimeName = std::unique_ptr<char[]>(new char[runtimeNameSize]);
            CHECK_FREAD_CUSTOM(cur.runtimeName.get(), file, runtimeNameSize);
        }
    }

    return true;
}

bool parse_prof_file(FILE* file, ProfilerData& data)
{
    if (!parse_header(file, data)) return false;
    if (!parse_descriptors(file, data)) return false;
    if (!parse_thread_data(file, data)) return false;

    return true;
}

void write_header(const FileHeader& header, FILE* file)
{
    fprintf(file, "Header:\n");
    const char* signature = (const char*)&header.signature;
    fmt::ifprintf(file, 4, "Signature: %d(%c%c%c%c)\n", header.signature, signature[0], signature[1],
                                                        signature[2], signature[3]);
    const FileVersion& version = header.version;
    fmt::ifprintf(file, 4, "Version: %d.%d.%d\n", version.major, version.minor, version.patch);
    fmt::ifprintf(file, 4, "Profiled Process ID: %llu\n", header.profiledProcessId);
    fmt::ifprintf(file, 4, "CPU Frequency Ratio: %lld\n", header.cpuFrequencyRatio);
    fmt::ifprintf(file, 4, "Begin Time: %llu\n", header.beginTime);

    double duration = 100*((header.endTime - header.beginTime)/(double)header.cpuFrequencyRatio);
    fmt::ifprintf(file, 4, "End Time: %llu (Total time: %f ms)\n", header.endTime, duration);
    fmt::ifprintf(file, 4, "Num Blocks: %u\n", header.numBlocks);
    fmt::ifprintf(file, 4, "Blocks Memory Usage: %llu\n", header.blocksMemoryUsage);
    fmt::ifprintf(file, 4, "Num Descriptors: %u\n", header.numDescriptors);
    fmt::ifprintf(file, 4, "Descriptor Memory Usage: %llu\n", header.descriptorsMemoryUsage);
}

void write_descriptors(const SizedDescriptorList& list, FILE* file)
{
    fprintf(file, "Descriptors:\n");
    for (const SizedDescriptor& sd : list) {
        const Descriptor& d = sd.descriptor;
        fmt::ifprintf(file, 4, "Descriptor for block: %s\n", d.name.get());
        fmt::ifprintf(file, 4, "Size: %u\n", sd.size);
        fmt::ifprintf(file, 4, "ID: %u\n", d.id);
        fmt::ifprintf(file, 4, "Line: %u\n", d.line);
        const Color& color = d.color;
        fmt::ifprintf(file, 4, "Color(ARGB): Packed: %u, Unpacked: (%u, %u, %u, %u)\n",
                               color.packed, color.a, color.r, color.g, color.b);
        fmt::ifprintf(file, 4, "Type: %u\n", d.type);
        fmt::ifprintf(file, 4, "Status: %u\n", d.status);
        fmt::ifprintf(file, 4, "Name Length: %u\n", d.nameLength);
        fmt::ifprintf(file, 4, "Name: %s\n", d.name.get());

        uint32_t fileNameLength = sd.size - offsetof(Descriptor, name) - d.nameLength;
        fmt::ifprintf(file, 4, "File Name: %s (%u bytes)\n", d.fileName.get(), fileNameLength);
    }
}

void write_thread_data(const ThreadDataList& list, FILE* file)
{
    fprintf(file, "Per Thread Data:\n");
    for (int i = 0; i < (int)list.size(); i++) {
        const ThreadData& cur = list[i];
        const ThreadInfo& info = cur.info;

        fmt::ifprintf(file, 0, "Thread Info:\n");
        fmt::ifprintf(file, 4, "ID: %llu\n", info.id);
        fmt::ifprintf(file, 4, "Name: %s\n", info.name.get());
        fmt::ifprintf(file, 4, "Name Length: %u\n", info.nameLength);
        fmt::ifprintf(file, 4, "Num Context Switches: %u\n", info.numContextSwitches);
        fmt::ifprintf(file, 4, "Num Blocks: %u\n", info.numBlocks);
        fprintf(file, "\n");

        fmt::Tree tree;
        tree.fprintf_root(file, "Context Switches:\n");

        const SizedContextSwitchList& switchList = cur.switches;

        int64_t numSwitches = (int64_t)switchList.size();
        for (int64_t i = 0; i < numSwitches; i++) {
            const SizedContextSwitch& sw = switchList[i];
            const ContextSwitch& cs = sw.contextSwitch;

            tree.fprintf_last_if(i == numSwitches-1, file, 1, "CS %d:\n", i);
            tree.fprintf_level(file, 2, "Process Info:\n", cs.processInfo.get());
            tree.fprintf_level(file, 2, "Target Thread ID:\n", cs.targetThreadId);
            tree.fprintf_level(file, 2, "Begin Time: %llu\n", cs.beginTime);
            tree.fprintf_level(file, 2, "End Time: %llu\n", cs.endTime);
            tree.fprintf_last(file, 2, "Size:\n", sw.size);
        }

        tree.fprintf_root(file, "Blocks:\n");

        const SizedBlockList& blocks = cur.blocks;
        int64_t numBlocks = (int64_t)blocks.size();
        for (int64_t i = 0; i < (int64_t)numBlocks; i++) {
            const SizedBlock& sb = blocks[i];
            const Block& cur = sb.block;

            tree.fprintf_last_if(i == numBlocks-1, file, 1, "Block %d:\n", i);
            tree.fprintf_level(file, 2, "Size: %u\n", sb.size);
            tree.fprintf_level(file, 2, "Name: %s\n", cur.runtimeName.get());
            tree.fprintf_level(file, 2, "ID: %u\n", cur.id);
            tree.fprintf_level(file, 2, "Begin Time: %llu\n", cur.beginTime);
            tree.fprintf_last(file, 2, "End Time: %llu\n", cur.endTime);
        }

        fprintf(file, "\n");
    }
}

void write_profiler_data(const ProfilerData& data, FILE* file)
{
    write_header(data.header, file);
    fprintf(file, "\n");

    write_descriptors(data.sizedDescriptorList, file);
    fprintf(file, "\n");

    write_thread_data(data.threadDataList, file);
}

bool print_file(const std::string& fileName)
{
    FILE* file = fopen(fileName.c_str(), "rb");
    if (!file) {
        printf("Failed to open \"%s\" for reading: %s\n", fileName.c_str(), strerror(errno));
        return false;
    }

    ProfilerData data;
    if (!parse_prof_file(file, data)) return false;
    write_profiler_data(data, stdout);

    fclose(file);
    return true;
}

int main(int argc, char** argv)
{
    std::string inFile1;
    std::string inFile2;
    if (!parse_args(argc, argv, inFile1, inFile2)) return EXIT_FAILURE;
    
    fprintf(stdout, "File: %s\n", inFile1.c_str());
    if (!print_file(inFile1)) {
        fprintf(stderr, "Error Parsing \"%s\"\n", inFile1.c_str());
        return EXIT_FAILURE;
    }

    printf("\n");
    fprintf(stdout, "File: %s\n", inFile2.c_str());
    if (!print_file(inFile2)) {
        fprintf(stderr, "Error Parsing \"%s\"\n", inFile2.c_str());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include "BigEndianBitReader.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <cstring>
#include <string>

constexpr size_t FINISH_TIME_BASE = 0x04 * 8;

std::string finishTimeString(BigEndianBitReader& ghostReader) {
    std::stringstream ss;

    uint16_t minutes = ghostReader.readBits(FINISH_TIME_BASE, 7);
    uint16_t seconds = ghostReader.readBits(FINISH_TIME_BASE + 7, 7);
    uint16_t milliseconds = ghostReader.readBits(FINISH_TIME_BASE + 14, 10);

    ss << std::setfill('0')               // pad with zeros
       << std::setw(2) << minutes         // 2 digits for minutes
       << ":" 
       << std::setw(2) << seconds         // 2 digits for seconds
       << "."
       << std::setw(3) << milliseconds;   // 3 digits for milliseconds

    return ss.str();
}

void writeBits(std::fstream& file, uint64_t value, size_t bitOffset, size_t bitCount) {
    if (bitCount == 0) return;

    size_t byteOffset = bitOffset / 8;
    size_t bitInByte = bitOffset % 8;

    // Calculate total affected bytes
    size_t totalBits = bitInByte + bitCount;
    size_t bytesNeeded = (totalBits + 7) / 8;

    // Read existing bytes
    std::vector<uint8_t> buffer(bytesNeeded, 0);
    file.seekg(byteOffset, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), bytesNeeded);

    // Insert bits
    for (size_t i = 0; i < bitCount; ++i) {
        size_t srcBitPos = bitCount - 1 - i;   // MSB of value first
        uint8_t bitVal = (value >> srcBitPos) & 1;

        size_t bufBitPos = bitInByte + i;
        size_t bufByte = bufBitPos / 8;
        size_t bufBit = 7 - (bufBitPos % 8);   // MSB-first in each byte

        buffer[bufByte] &= ~(1 << bufBit);
        buffer[bufByte] |= bitVal << bufBit;
    }

    // Write back
    file.seekp(byteOffset, std::ios::beg);
    file.write(reinterpret_cast<char*>(buffer.data()), bytesNeeded);
}


// Generate CRC32 table
std::vector<uint32_t> makeCRCTable() {
    std::vector<uint32_t> crcTable(256);
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xEDB88320 ^ (c >> 1);
            else
                c = c >> 1;
        }
        crcTable[n] = c;
    }
    return crcTable;
}

// Compute CRC32 from an ifstream, excluding the last 4 bytes
uint32_t crc32(std::fstream& file) {
    static std::vector<uint32_t> crcTable = makeCRCTable();
    uint32_t crc = 0xFFFFFFFF;

    // Remember current position
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();

    if (fileSize < 4) {
        throw std::runtime_error("File too small to contain a CRC32!");
    }

    // Go back to start
    file.seekg(0, std::ios::beg);

    const size_t bufferSize = 4096;
    std::vector<char> buffer(bufferSize);

    BigEndianBitReader br{ file };

    std::streampos bytesToRead = fileSize - static_cast<std::streamoff>(4); // exclude last 4 bytes
    while (bytesToRead > 0) {
        size_t chunkSize = static_cast<size_t>(std::min<std::streampos>(bytesToRead, bufferSize));
        file.read(buffer.data(), chunkSize);
        std::streamsize readCount = file.gcount();

        for (std::streamsize i = 0; i < readCount; i++) {
            crc = (crc >> 8) ^ crcTable[(crc ^ static_cast<uint8_t>(buffer[i])) & 0xFF];
        }

        bytesToRead -= readCount;
    }

    return crc ^ 0xFFFFFFFF;
}

// Copy a file
bool copyFile(const std::string& src, const std::string& dest) {
    std::ifstream in(src, std::ios::binary);
    std::ofstream out(dest, std::ios::binary);
    if (!in || !out) return false;
    out << in.rdbuf();
    return true;
}

// Write CRC32 into the last 4 bytes of a file (big-endian)
bool writeCRCToFile(const std::string& filename, uint32_t crc) {
    std::ofstream file(filename, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return false;

    file.seekp(-4, std::ios::end);
    uint8_t bytes[4] = {
        static_cast<uint8_t>((crc >> 24) & 0xFF),
        static_cast<uint8_t>((crc >> 16) & 0xFF),
        static_cast<uint8_t>((crc >> 8) & 0xFF),
        static_cast<uint8_t>(crc & 0xFF)
    };
    file.write(reinterpret_cast<char*>(bytes), 4);
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rkg file>\n";
        return 1;
    }

    std::fstream ghostStream{ argv[1], std::ios::binary | std::ios::in | std::ios::out};
    std::filesystem::path ghostPath{ argv[1] };
    BigEndianBitReader ghostReader{ ghostStream };

    constexpr uint32_t RKGD_MAGIC = 1380665156;

    if (/* std::filesystem::file_size(ghostPath) < 10240 || */ ghostReader.readUInt32(0) != RKGD_MAGIC) {
        std::cerr << "This doesn't seem to be a valid RKG!\n";
        return 1;
    }

    std::cout << "Original finish time: " << finishTimeString(ghostReader) << "\n\n";

    /*
    uint32_t crc = crc32(ghostStream);
    std::cout << "Calculated CRC32: 0x" << std::hex << crc << '\n';
    */

    std::cout << "Input minutes value (0-127): ";
    uint16_t minutes{};
    std::cin >> minutes;

    std::cout << "Input seconds value (0-127): ";
    uint16_t seconds{};
    std::cin >> seconds;

    std::cout << "Input milliseconds value (0-1023): ";
    uint16_t milliseconds{};
    std::cin >> milliseconds;

    std::stringstream copyFileName{};

    for (size_t i{ 0 }; i < strlen(argv[1]) - 4; i++) {
        copyFileName << argv[1][i];
    }
    copyFileName << "_OUTPUT.rkg";

    
    if (!copyFile(argv[1], copyFileName.str())) {
        std::cerr << "Failed to copy file!" << std::endl;
        return 1;
    }

    std::fstream ghostFileCopy(copyFileName.str(), std::ios::binary | std::ios::in | std::ios::out);
    if (!ghostFileCopy) {
        std::cerr << "Failed to open copied file!" << std::endl;
        return 1;
    }

    writeBits(ghostFileCopy, minutes, FINISH_TIME_BASE, 7);
    writeBits(ghostFileCopy, seconds, FINISH_TIME_BASE + 7, 7);
    writeBits(ghostFileCopy, milliseconds, FINISH_TIME_BASE + 14, 10);


    uint32_t calculatedCRC = 0;
    try {
        calculatedCRC = crc32(ghostFileCopy);
    } catch (const std::exception& e) {
        std::cerr << "Error computing CRC32: " << e.what() << std::endl;
        return 1;
    }
    ghostFileCopy.close();

    // Write CRC32 into last 4 bytes
    if (!writeCRCToFile(copyFileName.str(), calculatedCRC)) {
        std::cerr << "Failed to write CRC32 into file!" << std::endl;
        return 1;
    }
    /*
    std::cout << "Successfully updated CRC32 in " << copyFileName.str()
              << " to 0x" << std::hex << calculatedCRC << std::dec << '\n';
    */

    std::cout << "\nSuccessfully wrote to " << copyFileName.str() << '\n';


    return 0;
}

#ifndef BIG_ENDIAN_BIT_READER_H
#define BIG_ENDIAN_BIT_READER_H

#include <fstream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

class BigEndianBitReader {
private:
    std::fstream& m_stream;
    std::vector<uint8_t> m_buffer;
    size_t m_bufferBitOffset;
    mutable size_t m_cachedFileSize;
    mutable bool m_fileSizeCached;
    
    // Helper: Get file size in bytes
    size_t getFileSize() const {
        if (m_fileSizeCached) {
            return m_cachedFileSize;
        }
        
        auto currentPos = m_stream.tellg();
        m_stream.seekg(0, std::ios::end);
        m_cachedFileSize = static_cast<size_t>(m_stream.tellg());
        m_stream.seekg(currentPos, std::ios::beg);
        m_fileSizeCached = true;
        
        return m_cachedFileSize;
    }
    
    // Helper: Read bytes from stream into buffer starting at bit position
    void fillBuffer(size_t bitPos, size_t numBytes) {
        size_t bytePos = bitPos / 8;
        m_stream.seekg(bytePos, std::ios::beg);
        
        m_buffer.resize(numBytes);
        m_stream.read(reinterpret_cast<char*>(m_buffer.data()), numBytes);
        
        if (!m_stream) {
            throw std::runtime_error("Failed to read from stream");
        }
        
        m_bufferBitOffset = bitPos % 8;
    }
    
    // Helper: Extract bits from buffer
    uint64_t extractBits(size_t numBits) {
        if (numBits > 64) {
            throw std::invalid_argument("Cannot read more than 64 bits at once");
        }
        
        uint64_t result = 0;
        size_t bitsRead = 0;
        size_t bufIdx = 0;
        
        while (bitsRead < numBits && bufIdx < m_buffer.size()) {
            uint8_t byte = m_buffer[bufIdx];
            size_t bitsInByte = 8 - m_bufferBitOffset;
            size_t bitsToRead = std::min(bitsInByte, numBits - bitsRead);
            
            // Extract bits from current byte
            uint8_t mask = (1 << bitsToRead) - 1;
            uint8_t bits = (byte >> (bitsInByte - bitsToRead)) & mask;
            
            result = (result << bitsToRead) | bits;
            bitsRead += bitsToRead;
            
            m_bufferBitOffset += bitsToRead;
            if (m_bufferBitOffset >= 8) {
                m_bufferBitOffset = 0;
                bufIdx++;
            }
        }
        
        return result;
    }

public:
    explicit BigEndianBitReader(std::fstream& fs) 
        : m_stream(fs), m_bufferBitOffset(0), m_cachedFileSize(0), m_fileSizeCached(false) {
        if (!m_stream.is_open()) {
            throw std::invalid_argument("Stream is not open");
        }
    }
    
    // Read numBits from bit position bitPos
    uint64_t readBits(size_t bitPos, size_t numBits) {
        if (numBits == 0 || numBits > 64) {
            throw std::invalid_argument("numBits must be between 1 and 64");
        }
        
        // Calculate how many bytes we need to read
        size_t startBitInByte = bitPos % 8;
        size_t totalBitsNeeded = startBitInByte + numBits;
        size_t bytesNeeded = (totalBitsNeeded + 7) / 8;
        
        fillBuffer(bitPos, bytesNeeded);
        return extractBits(numBits);
    }
    
    // Read numBytes starting from bit position bitPos
    std::vector<uint8_t> readBytesFromBitPos(size_t bitPos, size_t numBytes) {
        std::vector<uint8_t> result;
        result.reserve(numBytes);
        
        for (size_t i = 0; i < numBytes; i++) {
            uint64_t byte = readBits(bitPos + i * 8, 8);
            result.push_back(static_cast<uint8_t>(byte));
        }
        
        return result;
    }
    
    // Read numBytes starting from byte position bytePos
    std::vector<uint8_t> readBytes(size_t bytePos, size_t numBytes) {
        return readBytesFromBitPos(bytePos * 8, numBytes);
    }
    
    // Convenience functions for common types
    uint8_t readUInt8(size_t bitPos) {
        return static_cast<uint8_t>(readBits(bitPos, 8));
    }
    
    uint16_t readUInt16(size_t bitPos) {
        return static_cast<uint16_t>(readBits(bitPos, 16));
    }
    
    uint32_t readUInt32(size_t bitPos) {
        return static_cast<uint32_t>(readBits(bitPos, 32));
    }
    
    uint64_t readUInt64(size_t bitPos) {
        return readBits(bitPos, 64);
    }
    
    // Read signed integers (sign-extend)
    int64_t readSignedBits(size_t bitPos, size_t numBits) {
        if (numBits == 0 || numBits > 64) {
            throw std::invalid_argument("numBits must be between 1 and 64");
        }
        
        uint64_t value = readBits(bitPos, numBits);
        
        // Sign extend if the sign bit is set
        if (numBits < 64 && (value & (1ULL << (numBits - 1)))) {
            uint64_t signExtMask = ~((1ULL << numBits) - 1);
            value |= signExtMask;
        }
        
        return static_cast<int64_t>(value);
    }
    
    // Read from end of file: negative offset means distance from end
    // E.g., readBitsFromEnd(-8, 8) reads the last byte of the file
    uint64_t readBitsFromEnd(int64_t bitOffsetFromEnd, size_t numBits) {
        if (bitOffsetFromEnd > 0) {
            throw std::invalid_argument("Offset from end must be negative or zero");
        }
        
        size_t fileSizeBits = getFileSize() * 8;
        int64_t absoluteBitPos = static_cast<int64_t>(fileSizeBits) + bitOffsetFromEnd;
        
        if (absoluteBitPos < 0) {
            throw std::out_of_range("Offset extends before start of file");
        }
        
        return readBits(static_cast<size_t>(absoluteBitPos), numBits);
    }
    
    // Read bytes from end of file: negative offset means distance from end
    // E.g., readBytesFromEnd(-4, 4) reads the last 4 bytes of the file
    std::vector<uint8_t> readBytesFromEnd(int64_t byteOffsetFromEnd, size_t numBytes) {
        if (byteOffsetFromEnd > 0) {
            throw std::invalid_argument("Offset from end must be negative or zero");
        }
        
        size_t fileSizeBytes = getFileSize();
        int64_t absoluteBytePos = static_cast<int64_t>(fileSizeBytes) + byteOffsetFromEnd;
        
        if (absoluteBytePos < 0) {
            throw std::out_of_range("Offset extends before start of file");
        }
        
        return readBytes(static_cast<size_t>(absoluteBytePos), numBytes);
    }
    
    // Get file size in bytes
    size_t fileSizeBytes() const {
        return getFileSize();
    }
    
    // Get file size in bits
    size_t fileSizeBits() const {
        return getFileSize() * 8;
    }
    
    // Invalidate cached file size (call if file is modified externally)
    void invalidateFileSizeCache() {
        m_fileSizeCached = false;
    }
};

// Example usage:
/*
int main() {
    std::ifstream file("data.bin", std::ios::binary);
    BigEndianBitReader reader(file);
    
    // Read 12 bits from bit position 0
    uint64_t val1 = reader.readBits(0, 12);
    
    // Read 3 bits from bit position 12 (crosses byte boundary)
    uint64_t val2 = reader.readBits(12, 3);
    
    // Read 4 bytes from bit position 100
    std::vector<uint8_t> bytes = reader.readBytesFromBitPos(100, 4);
    
    // Read 2 bytes from byte position 50
    std::vector<uint8_t> bytes2 = reader.readBytes(50, 2);
    
    // Read a 32-bit integer from bit position 200
    uint32_t val3 = reader.readUInt32(200);
    
    // Read the last byte of the file
    uint64_t lastByte = reader.readBitsFromEnd(-8, 8);
    
    // Read the last 4 bytes of the file
    std::vector<uint8_t> lastFourBytes = reader.readBytesFromEnd(-4, 4);
    
    // Read 16 bits starting 32 bits before the end
    uint64_t val4 = reader.readBitsFromEnd(-32, 16);
    
    // Get file size
    size_t sizeInBytes = reader.fileSizeBytes();
    size_t sizeInBits = reader.fileSizeBits();
    
    return 0;
}
*/

#endif // BIG_ENDIAN_BIT_READER_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <sys/mman.h>    // For mmap
#include <sys/stat.h>    // For fstat
#include <fcntl.h>       // For open
#include <unistd.h>      // For close
#include <chrono>
#include <cstring>       // For memchr
#include <cassert>

// -------------------- Bloom Filter Implementation ----------------------

// You may need to adjust BLOOM_SIZE based on the expected number of keys.
// For very large files, a larger m may be necessary.
const size_t BLOOM_SIZE   = 1000000000;  // e.g., 1e9 bits (~125MB); tune as needed.
const size_t BLOOM_HASHES = 7;           // Number of hash functions

// A simple Bloom filter class for strings.
class BloomFilter {
    // For maximum speed and predictable memory, we use a vector of uint32_t
    // as our bit array. Each uint32_t holds 32 bits.
    std::vector<uint32_t> bit_array;
    size_t m; // total number of bits
    size_t k; // number of hash functions

    // Set the bit at position pos.
    inline void set_bit(size_t pos) {
        bit_array[pos >> 5] |= (1u << (pos & 31));
    }

    // Test the bit at position pos.
    inline bool test_bit(size_t pos) const {
        return bit_array[pos >> 5] & (1u << (pos & 31));
    }

public:
    BloomFilter(size_t m_bits, size_t num_hashes)
        : bit_array((m_bits + 31) / 32, 0), m(m_bits), k(num_hashes) {}

    // Add an item to the Bloom filter.
    void add(const std::string& item) {
        // Use two independent hash values (using std::hash with different salt)
        size_t hash1 = std::hash<std::string>{}(item);
        size_t hash2 = std::hash<std::string>{}("salt" + item);
        for (size_t i = 0; i < k; ++i) {
            size_t combined = hash1 + i * hash2;
            size_t idx = combined % m;
            set_bit(idx);
        }
    }

    // Check whether an item is possibly in the Bloom filter.
    bool possibly_contains(const std::string& item) const {
        size_t hash1 = std::hash<std::string>{}(item);
        size_t hash2 = std::hash<std::string>{}("salt" + item);
        for (size_t i = 0; i < k; ++i) {
            size_t combined = hash1 + i * hash2;
            size_t idx = combined % m;
            if (!test_bit(idx))
                return false;
        }
        return true;
    }
};

// -------------------- Memory-Mapped File Helpers ----------------------

// A simple RAII structure to hold a memory-mapped file.
struct MMapFile {
    char* data;
    size_t size;
    int fd;

    MMapFile(const std::string& filename) : data(nullptr), size(0), fd(-1) {
        fd = open(filename.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error("Failed to open file: " + filename);
        }
        struct stat sb;
        if (fstat(fd, &sb) < 0) {
            close(fd);
            throw std::runtime_error("Failed to get file size: " + filename);
        }
        size = sb.st_size;
        data = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        if (data == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("Failed to mmap file: " + filename);
        }
    }

    ~MMapFile() {
        if (data && data != MAP_FAILED) {
            munmap(data, size);
        }
        if (fd >= 0) {
            close(fd);
        }
    }
};

// -------------------- Multithreaded Segment Processing ----------------------

// Instead of using a thread-safe queue, we partition file2 (query file)
// into segments that are processed concurrently.
void process_segment(const char* data, size_t seg_start, size_t seg_end,
                     const BloomFilter& bloom,
                     std::ofstream& out, std::mutex& out_mutex) {
    // Process from seg_start to seg_end; the data is memory-mapped.
    size_t pos = seg_start;
    while (pos < seg_end) {
        // Find next newline; if none, set end = seg_end.
        const char* line_start = data + pos;
        const char* newline = static_cast<const char*>(memchr(line_start, '\n', seg_end - pos));
        size_t line_len = newline ? (newline - line_start) : (seg_end - pos);
        // Construct the line as std::string without the newline.
        std::string line(line_start, line_len);
        // Advance pos (if newline found, skip it).
        pos += line_len + (newline ? 1 : 0);

        // (Optionally trim carriage returns or whitespace here.)
        if (!line.empty() && bloom.possibly_contains(line)) {
            // Write to output file (synchronized).
            std::lock_guard<std::mutex> lock(out_mutex);
            out << line << "\n";
        }
    }
}

// -------------------- Main Function ----------------------

int main(int argc, char* argv[]) {
    try {
        auto start_time = std::chrono::high_resolution_clock::now();

        std::string file1, file2, outfile;
        if (argc >= 4) {
            file1 = argv[1];
            file2 = argv[2];
            outfile = argv[3];
        } else {
            std::cout << "Enter file1 (keys) path: ";
            std::cin >> file1;
            std::cout << "Enter file2 (queries) path: ";
            std::cin >> file2;
            std::cout << "Enter output file path: ";
            std::cin >> outfile;
        }

        // ---------------- Build Bloom Filter from file1 ----------------
        std::cout << "Mapping file1: " << file1 << "\n";
        MMapFile mmap1(file1);
        // Build Bloom filter by scanning file1 for lines.
        BloomFilter bloom(BLOOM_SIZE, BLOOM_HASHES);
        {
            size_t pos = 0;
            size_t count = 0;
            while (pos < mmap1.size) {
                // Find next newline.
                const char* line_start = mmap1.data + pos;
                const char* newline = static_cast<const char*>(memchr(line_start, '\n', mmap1.size - pos));
                size_t len = newline ? (newline - line_start) : (mmap1.size - pos);
                if (len > 0) {
                    std::string line(line_start, len);
                    bloom.add(line);
                    ++count;
                }
                pos += len + (newline ? 1 : 0);
            }
            std::cout << "Bloom filter built from " << count << " lines from file1.\n";
        }

        // ---------------- Process file2 using multithreading ----------------
        std::cout << "Mapping file2: " << file2 << "\n";
        MMapFile mmap2(file2);
        size_t file2_size = mmap2.size;
        // Determine number of threads (use hardware concurrency).
        unsigned int num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 2;
        std::cout << "Using " << num_threads << " worker threads.\n";

        // Partition file2 into segments. We choose nearly equal segments by file offset.
        std::vector<std::pair<size_t, size_t>> segments;
        size_t base_seg_size = file2_size / num_threads;
        size_t seg_start = 0;
        for (unsigned int i = 0; i < num_threads; ++i) {
            size_t seg_end = (i == num_threads - 1) ? file2_size : seg_start + base_seg_size;
            // Adjust seg_end to the next newline (unless at end of file)
            if (seg_end < file2_size) {
                const char* p = mmap2.data + seg_end;
                while (seg_end < file2_size && *p != '\n') {
                    ++seg_end;
                    ++p;
                }
                // Move past the newline if present.
                if (seg_end < file2_size) ++seg_end;
            }
            segments.emplace_back(seg_start, seg_end);
            seg_start = seg_end;
        }

        // Open output file.
        std::ofstream out(outfile);
        if (!out.is_open()) {
            throw std::runtime_error("Failed to open output file: " + outfile);
        }
        std::mutex out_mutex; // protects out

        // Launch worker threads, each processing a segment.
        std::vector<std::thread> workers;
        for (unsigned int i = 0; i < num_threads; ++i) {
            auto seg = segments[i];
            workers.emplace_back(process_segment,
                                 mmap2.data,
                                 seg.first,
                                 seg.second,
                                 std::cref(bloom),
                                 std::ref(out),
                                 std::ref(out_mutex));
        }

        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }

        out.close();
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        std::cout << "Matching complete. Results saved to " << outfile << ".\n";
        std::cout << "Elapsed time: " << elapsed.count() << " seconds.\n";
        return 0;
    }
    catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <stdexcept>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <secp256k1.h>
#include <csignal>

// --- Constants ---
const char* BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

// --- Signal Handling ---
volatile std::sig_atomic_t stop = 0;
void handleInterrupt(int) {
    stop = 1;
}

// --- Utility Functions ---
std::string toHex(const std::vector<unsigned char>& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : data) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

// --- Base58 Encoding ---
std::string Base58Encode(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> input(data);
    std::string result;

    size_t zeros = 0;
    while (zeros < input.size() && input[zeros] == 0) {
        ++zeros;
    }

    std::vector<unsigned char> b58((input.size() - zeros) * 138 / 100 + 1);
    size_t length = 0;

    for (size_t i = zeros; i < input.size(); ++i) {
        int carry = input[i];
        size_t j = 0;
        for (auto it = b58.rbegin(); (carry != 0 || j < length) && it != b58.rend(); ++it, ++j) {
            carry += 256 * (*it);
            *it = carry % 58;
            carry /= 58;
        }
        length = j;
    }

    auto it = b58.begin() + (b58.size() - length);
    while (it != b58.end() && *it == 0) ++it;

    result.reserve(zeros + (b58.end() - it));
    while (zeros--) result += '1';
    while (it != b58.end()) result += BASE58_ALPHABET[*it++];

    return result;
}

// --- Hashing Functions ---
std::vector<unsigned char> sha256(const std::vector<unsigned char>& input) {
    std::vector<unsigned char> hash(SHA256_DIGEST_LENGTH);
    SHA256(input.data(), input.size(), hash.data());
    return hash;
}

std::vector<unsigned char> sha512(const std::vector<unsigned char>& input) {
    std::vector<unsigned char> hash(SHA512_DIGEST_LENGTH);
    SHA512(input.data(), input.size(), hash.data());
    return hash;
}

// --- RIPEMD160 Hashing ---
std::vector<unsigned char> ripemd160(const std::vector<unsigned char>& input) {
    std::vector<unsigned char> hash(RIPEMD160_DIGEST_LENGTH);
    RIPEMD160(input.data(), input.size(), hash.data());
    return hash;
}

// --- Generate Public Keys ---
std::vector<unsigned char> getPublicKey(secp256k1_context* ctx, const std::vector<unsigned char>& privKey, bool compressed) {
    if (privKey.size() != 32) {
        throw std::invalid_argument("Private key must be 32 bytes");
    }
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, privKey.data())) {
        throw std::runtime_error("Failed to create public key");
    }
    size_t len = compressed ? 33 : 65;
    std::vector<unsigned char> output(len);
    secp256k1_ec_pubkey_serialize(ctx, output.data(), &len, &pubkey, compressed ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED);
    return output;
}

// --- Address Generation ---
std::string generateP2PKHAddress(const std::vector<unsigned char>& pubKey) {
    auto hash160 = ripemd160(sha256(pubKey));
    std::vector<unsigned char> payload = {0x00};
    payload.insert(payload.end(), hash160.begin(), hash160.end());
    auto checksum = sha256(sha256(payload));
    payload.insert(payload.end(), checksum.begin(), checksum.begin() + 4);
    return Base58Encode(payload);
}

std::string generateP2SHAddress(const std::vector<unsigned char>& pubKey) {
    auto h1 = sha256(pubKey);
    auto h2 = ripemd160(h1);
    std::vector<unsigned char> rs = {0x00, 0x14};
    rs.insert(rs.end(), h2.begin(), h2.end());
    auto h3 = sha256(rs);
    auto h4 = ripemd160(h3);
    std::vector<unsigned char> payload = {0x05};
    payload.insert(payload.end(), h4.begin(), h4.end());
    auto checksum = sha256(sha256(payload));
    payload.insert(payload.end(), checksum.begin(), checksum.begin() + 4);
    return Base58Encode(payload);
}

// --- File Handling ---
class FileHandler {
public:
    FileHandler(const std::string& shaFileName, const std::string& saveFileName)
        : shaFile_(shaFileName, std::ios::app), saveFile_(saveFileName, std::ios::app) {
        if (!shaFile_.is_open() || !saveFile_.is_open()) {
            throw std::runtime_error("Failed to open output files");
        }
    }

    void writeSha(const std::string& hashHex, bool withNewline) {
        shaFile_ << (withNewline ? "With newline: " : "Without newline: ") << hashHex << "\n";
        shaFile_.flush();
    }

    void writeAddresses(const std::string& input, const std::string& label, const std::string& hashHex,
                       const std::string& addrP2PKHCompressed, const std::string& addrP2PKHUncompressed,
                       const std::string& addrP2SHCompressed) {
        saveFile_ << "======STRING : " << input << " (" << label << ")======\n";
        saveFile_ << "KEY: " << hashHex << "\n";
        saveFile_ << "ADDRESS : " << addrP2PKHCompressed << "\n";
        saveFile_ << "ADDRESS : " << addrP2PKHUncompressed << "\n";
        saveFile_ << "ADDRESS : " << addrP2SHCompressed << "\n\n";
        saveFile_.flush();
    }

    void saveCheckpoint(const std::string& currentHex) {
        std::ofstream checkpoint("checkpoint.txt");
        if (checkpoint.is_open()) {
            checkpoint << currentHex << "\n";
            checkpoint.flush();
        }
    }

private:
    std::ofstream shaFile_;
    std::ofstream saveFile_;
};

// --- Infinite Hash Loop ---
void infiniteHashLoop(int option, const std::string& inputStr) {
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (!ctx) {
        throw std::runtime_error("Failed to create secp256k1 context");
    }

    FileHandler fileHandler("infinitesha.txt", "saved.txt");
    std::string current = inputStr;

    try {
        while (!stop) {
            // Compute inputs
            std::string input_with_newline = current + "\n";
            std::vector<unsigned char> input_bytes_with_newline(input_with_newline.begin(), input_with_newline.end());
            std::vector<unsigned char> input_bytes_without_newline(current.begin(), current.end()); // Always ASCII

            // Apply hashing based on option
            auto applyHashing = [&](const std::vector<unsigned char>& input) -> std::vector<unsigned char> {
                if (option == 1) {
                    // Mode 1: Single SHA-256
                    return sha256(input);
                } else if (option == 2) {
                    // Mode 2: SHA-512 then SHA-256
                    auto sha512res = sha512(input);
                    return sha256(sha512res);
                }
                throw std::invalid_argument("Invalid hashing option");
            };

            // Compute hashes
            auto hash_with_newline = applyHashing(input_bytes_with_newline);
            auto hash_without_newline = applyHashing(input_bytes_without_newline);

            // Convert to hex
            std::string hashHex_with_newline = toHex(hash_with_newline);
            std::string hashHex_without_newline = toHex(hash_without_newline);

            // Write hashes
            fileHandler.writeSha(hashHex_with_newline, true);
            fileHandler.writeSha(hashHex_without_newline, false);

            // Generate and write addresses
            auto generateAddresses = [&](const std::vector<unsigned char>& hash, const std::string& label, const std::string& hashHex) {
                std::vector<unsigned char> privKey = hash;
                if (privKey.size() > 32) privKey.resize(32);
                while (privKey.size() < 32) privKey.insert(privKey.begin(), 0x00);

                auto compressedPub = getPublicKey(ctx, privKey, true);
                auto uncompressedPub = getPublicKey(ctx, privKey, false);

                std::string addrP2PKHCompressed = generateP2PKHAddress(compressedPub);
                std::string addrP2PKHUncompressed = generateP2PKHAddress(uncompressedPub);
                std::string addrP2SHCompressed = generateP2SHAddress(compressedPub);

                fileHandler.writeAddresses(current, label, hashHex, addrP2PKHCompressed, addrP2PKHUncompressed, addrP2SHCompressed);
            };

            // Generate addresses for both cases
            generateAddresses(hash_with_newline, "with newline", hashHex_with_newline);
            generateAddresses(hash_without_newline, "without newline", hashHex_without_newline);

            // Update current string for next iteration
            current = hashHex_without_newline;

            // Save checkpoint
            fileHandler.saveCheckpoint(current);
        }
    } catch (const std::exception& e) {
        fileHandler.saveCheckpoint(current); // Now in scope
        secp256k1_context_destroy(ctx);
        throw;
    }

    secp256k1_context_destroy(ctx);
}

// --- Main ---
int main(int argc, char* argv[]) {
    try {
        if (argc != 3) {
            throw std::invalid_argument("Usage: " + std::string(argv[0]) + " <option> <input_string>\n" +
                                        "Option 1: SHA-256 endlessly\nOption 2: Alternate SHA-512 and SHA-256 endlessly");
        }

        int option = std::atoi(argv[1]);
        if (option != 1 && option != 2) {
            throw std::invalid_argument("Invalid option. Choose 1 or 2.");
        }

        std::string inputStr = argv[2];

        // Set up signal handler
        std::signal(SIGINT, handleInterrupt);

        // Check for checkpoint
        std::ifstream checkpoint("checkpoint.txt");
        if (checkpoint.is_open()) {
            std::getline(checkpoint, inputStr);
            checkpoint.close();
        }

        infiniteHashLoop(option, inputStr);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

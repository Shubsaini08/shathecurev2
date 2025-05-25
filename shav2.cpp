#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <secp256k1.h>
#include <cassert>

// --- Base58 Encoding ---
const char* BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::string Base58Encode(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> input(data);
    std::string result;

    // Count leading zeros
    size_t zeros = 0;
    while (zeros < input.size() && input[zeros] == 0) {
        ++zeros;
    }

    std::vector<unsigned char> b58((input.size() - zeros) * 138 / 100 + 1);
    size_t length = 0;

    for (size_t i = zeros; i < input.size(); i++) {
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

    while (zeros--) result += '1';
    while (it != b58.end()) result += BASE58_ALPHABET[*it++];

    return result;
}

// --- Helper Functions ---
std::vector<unsigned char> sha256(const std::vector<unsigned char>& input) {
    std::vector<unsigned char> hash(SHA256_DIGEST_LENGTH);
    SHA256(input.data(), input.size(), hash.data());
    return hash;
}

std::vector<unsigned char> sha256_string(const std::string& input) {
    return sha256(std::vector<unsigned char>(input.begin(), input.end()));
}

std::vector<unsigned char> sha512(const std::string& input) {
    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);
    return std::vector<unsigned char>(hash, hash + SHA512_DIGEST_LENGTH);
}

std::vector<unsigned char> ripemd160(const std::vector<unsigned char>& input) {
    std::vector<unsigned char> hash(RIPEMD160_DIGEST_LENGTH);
    RIPEMD160(input.data(), input.size(), hash.data());
    return hash;
}

std::string toHex(const std::vector<unsigned char>& data) {
    std::ostringstream oss;
    for (auto b : data) oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return oss.str();
}

std::vector<unsigned char> hexToBytes(const std::string& hex) {
    std::vector<unsigned char> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back(static_cast<unsigned char>(strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return bytes;
}

// --- Generate Public Keys ---
std::vector<unsigned char> getPublicKey(secp256k1_context* ctx, const std::vector<unsigned char>& privKey, bool compressed) {
    secp256k1_pubkey pubkey;
    assert(secp256k1_ec_pubkey_create(ctx, &pubkey, privKey.data()));

    size_t len = compressed ? 33 : 65;
    std::vector<unsigned char> output(len);
    secp256k1_ec_pubkey_serialize(ctx, output.data(), &len, &pubkey, compressed ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED);

    return output;
}

std::string generateP2PKHAddress(const std::vector<unsigned char>& pubKey) {
    auto hash160 = ripemd160(sha256(pubKey));
    std::vector<unsigned char> payload = {0x00};
    payload.insert(payload.end(), hash160.begin(), hash160.end());

    auto checksum = sha256(sha256(payload));
    payload.insert(payload.end(), checksum.begin(), checksum.begin() + 4);

    return Base58Encode(payload);
}

// --- Infinite Loop ---
void infiniteHashLoop(int mode, const std::string& inputStr) {
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    std::string current = inputStr;

    std::ofstream shaFile("infinitesha.txt", std::ios::app);
    std::ofstream saveFile("saved.txt", std::ios::app);

    while (true) {
        std::vector<unsigned char> hash;

        if (mode == 1) {
            hash = sha256_string(current);
        } else if (mode == 2) {
            auto sha512res = sha512(current);
            hash = sha256(sha512res);
        } else {
            std::cerr << "Invalid mode\n";
            break;
        }

        std::string hashHex = toHex(hash);
        shaFile << hashHex << "\n";
        shaFile.flush();

        // Convert SHA hash to private key (ensure 32 bytes)
        if (hash.size() > 32) hash.resize(32);
        while (hash.size() < 32) hash.insert(hash.begin(), 0x00);

        auto compressedPub = getPublicKey(ctx, hash, true);
        auto uncompressedPub = getPublicKey(ctx, hash, false);

        std::string addrCompressed = generateP2PKHAddress(compressedPub);
        std::string addrUncompressed = generateP2PKHAddress(uncompressedPub);

        saveFile << "======STRING : " << current << "======\n";
        saveFile << "KEY: " << hashHex << "\n";
        saveFile << "ADDRESS : " << addrCompressed << "\n";
        saveFile << "ADDRESS : " << addrUncompressed << "\n\n";
        saveFile.flush();

        current = hashHex;
    }

    secp256k1_context_destroy(ctx);
}

// --- Main ---
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <option 1|2> <input_string>\n";
        return 1;
    }

    int option = std::atoi(argv[1]);
    std::string inputStr = argv[2];

    infiniteHashLoop(option, inputStr);

    return 0;
}

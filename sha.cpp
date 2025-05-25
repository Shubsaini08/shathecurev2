#include <openssl/sha.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstdlib>

// Compute SHA-256 of a string and return as hex
std::string sha256_hash(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.size());
    SHA256_Final(hash, &sha256);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return oss.str();
}

// Compute SHA-512 of a string and return as hex
std::string sha512_hash(const std::string& data) {
    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512_CTX sha512;
    SHA512_Init(&sha512);
    SHA512_Update(&sha512, data.c_str(), data.size());
    SHA512_Final(hash, &sha512);

    std::ostringstream oss;
    for (int i = 0; i < SHA512_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return oss.str();
}

// Option 1: endlessly hash input with SHA-256
void option1_loop(const std::string& input_string, const std::string& output_file) {
    std::ofstream file(output_file);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << output_file << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::string current_hash = input_string;
    while (true) {
        current_hash = sha256_hash(current_hash);
        file << current_hash << std::endl;
        file.flush();
    }
}

// Option 2: endlessly alternate SHA-512 and SHA-256
void option2_loop(const std::string& input_string, const std::string& output_file) {
    std::ofstream file(output_file);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << output_file << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::string current_hash = input_string;
    while (true) {
        current_hash = sha512_hash(current_hash);
        current_hash = sha256_hash(current_hash);
        file << current_hash << std::endl;
        file.flush();
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <option> <input_string>" << std::endl;
        std::cout << "Option 1: SHA-256 endlessly" << std::endl;
        std::cout << "Option 2: Alternate SHA-512 and SHA-256 endlessly" << std::endl;
        return EXIT_FAILURE;
    }

    int option = std::atoi(argv[1]);
    std::string input_string = argv[2];
    const std::string output_file = "infinitesha.txt";

    if (option == 1) {
        std::cout << "Running Option 1: SHA-256 endlessly" << std::endl;
        option1_loop(input_string, output_file);
    } else if (option == 2) {
        std::cout << "Running Option 2: Alternate SHA-512 and SHA-256 endlessly" << std::endl;
        option2_loop(input_string, output_file);
    } else {
        std::cerr << "Invalid option. Choose 1 or 2." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

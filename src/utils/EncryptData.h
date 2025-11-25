

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <vector>




namespace EncryptData {

    // Helper: generate random IV
    static std::vector<unsigned char> generateIV(size_t size = 16) {
        std::vector<unsigned char> iv(size);
        if (!RAND_bytes(iv.data(), static_cast<int>(size))) {
            throw std::runtime_error("Failed to generate IV");
        }
        return iv;
    }


    std::string encryptMessage(const std::string& plainText) {
        if (encryptionKey.empty()) {
            throw std::runtime_error("Encryption key not set");
        }

        // Prepare key and IV
        std::vector<unsigned char> key(32, 0); // AES-256 key size
        std::copy(encryptionKey.begin(),
                encryptionKey.begin() + std::min(encryptionKey.size(), key.size()),
                key.begin());

        auto iv = generateIV();

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create cipher context");

        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EncryptInit failed");
        }

        std::vector<unsigned char> cipherText(plainText.size() + EVP_MAX_BLOCK_LENGTH);
        int len = 0, cipherLen = 0;

        if (EVP_EncryptUpdate(ctx, cipherText.data(), &len,
                            reinterpret_cast<const unsigned char*>(plainText.data()),
                            static_cast<int>(plainText.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EncryptUpdate failed");
        }
        cipherLen = len;

        if (EVP_EncryptFinal_ex(ctx, cipherText.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("EncryptFinal failed");
        }
        cipherLen += len;

        EVP_CIPHER_CTX_free(ctx);

        // Prepend IV to ciphertext for later decryption
        std::string result(reinterpret_cast<char*>(iv.data()), iv.size());
        result.append(reinterpret_cast<char*>(cipherText.data()), cipherLen);
        return result;
    }

    
    std::string decryptMessage(const std::string& cipherInput) {
        if (encryptionKey.empty()) {
            throw std::runtime_error("Encryption key not set");
        }

        // Extract IV (first 16 bytes)
        std::vector<unsigned char> iv(cipherInput.begin(), cipherInput.begin() + 16);
        std::string cipherText = cipherInput.substr(16);

        std::vector<unsigned char> key(32, 0);
        std::copy(encryptionKey.begin(),
                encryptionKey.begin() + std::min(encryptionKey.size(), key.size()),
                key.begin());

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create cipher context");

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("DecryptInit failed");
        }

        std::vector<unsigned char> plainText(cipherText.size() + EVP_MAX_BLOCK_LENGTH);
        int len = 0, plainLen = 0;

        if (EVP_DecryptUpdate(ctx, plainText.data(), &len,
                            reinterpret_cast<const unsigned char*>(cipherText.data()),
                            static_cast<int>(cipherText.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("DecryptUpdate failed");
        }
        plainLen = len;

        if (EVP_DecryptFinal_ex(ctx, plainText.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("DecryptFinal failed");
        }
        plainLen += len;

        EVP_CIPHER_CTX_free(ctx);

        return std::string(reinterpret_cast<char*>(plainText.data()), plainLen);
    }

}
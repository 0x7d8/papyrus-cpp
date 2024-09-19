#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <sys/stat.h>
#include <filesystem>

#include <openssl/md5.h>
#include <openssl/sha.h>

class Storage {
private:
	std::string _path;

	std::string bufToHex(const unsigned char *buffer, size_t length) {
		std::string hex;
		for (size_t i = 0; i < length; i++) {
			char c[3];
			sprintf(c, "%02x", buffer[i]);
			hex += c;
		}

		return hex;
	}
public:
	Storage(const std::string &path) : _path(path) {
		struct stat info;
		if (stat(path.c_str(), &info) != 0) {
			std::filesystem::create_directories(path);
		}
	}

	std::string get() {
		return _path;
	}

	std::ofstream store(const std::string &filename) {
		return std::ofstream(_path + "/" + filename);
	}

	void remove(const std::string &filename) {
		if (std::filesystem::exists(_path + "/" + filename)) {
			std::filesystem::remove(_path + "/" + filename);
		}
	}

	// Does the final touches (hashing it to md5, sha256, sha512)
	std::map<std::string, std::string> finalize(const std::string &filename) {
		std::map<std::string, std::string> hashes;

		std::ifstream file(_path + "/" + filename, std::ios::binary);
		if (!file.is_open()) {
			return hashes;
		}

		MD5_CTX md5Context;
		MD5_Init(&md5Context);

		SHA256_CTX sha256Context;
		SHA256_Init(&sha256Context);

		SHA512_CTX sha512Context;
		SHA512_Init(&sha512Context);

		char buffer[1024];
		while (file.read(buffer, sizeof(buffer))) {
			MD5_Update(&md5Context, buffer, sizeof(buffer));
			SHA256_Update(&sha256Context, buffer, sizeof(buffer));
			SHA512_Update(&sha512Context, buffer, sizeof(buffer));
		}

		MD5_Final((unsigned char *)buffer, &md5Context);
		hashes["md5"] = bufToHex((unsigned char *)buffer, MD5_DIGEST_LENGTH);

		SHA256_Final((unsigned char *)buffer, &sha256Context);
		hashes["sha256"] = bufToHex((unsigned char *)buffer, SHA256_DIGEST_LENGTH);

		SHA512_Final((unsigned char *)buffer, &sha512Context);
		hashes["sha512"] = bufToHex((unsigned char *)buffer, SHA512_DIGEST_LENGTH);

		file.close();

		if (std::filesystem::exists(_path + "/" + hashes["md5"])) {
			std::filesystem::remove(_path + "/" + hashes["md5"]);
		}

		std::filesystem::rename(_path + "/" + filename, _path + "/" + hashes["md5"]);

		return hashes;
	}

	std::ifstream retrieve(const std::string &filename) {
		return std::ifstream(_path + "/" + filename);
	}

	uintmax_t size(const std::string &filename) {
		auto stat = std::filesystem::file_size(_path + "/" + filename);

		return stat;
	}
};
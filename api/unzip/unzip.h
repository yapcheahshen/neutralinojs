#ifndef NEU_UNZIP_H
#define NEU_UNZIP_H
#include "lib/json/json.hpp"

using json = nlohmann::json;
using namespace std;
namespace unzip {
	namespace controllers {
		json readFile(const json &input);
		json fileList(const json &input);
	}
}

#endif
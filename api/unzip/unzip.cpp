#include <stdio.h>
#include <stdlib.h>

#include "api/unzip/junzip.h"
#include "api/unzip/unzip.h"
#include "lib/json/json.hpp"
#include "helpers.h"
#include "settings.h"
#include "resources.h"
#include "api/filesystem/filesystem.h"

using namespace std;
using json = nlohmann::json;

namespace unzip {
    int processFile(JZFile* zip) {
        JZFileHeader header;
        char filename[1024];
        unsigned char* data;

        if (jzReadLocalFileHeader(zip, &header, filename, sizeof(filename))) {
            printf("Couldn't read local file header!");
            return -1;
        }

        if ((data = (unsigned char*)malloc(*(uint32_t*)header.uncompressedSize)) == NULL) {
            printf("Couldn't allocate memory!");
            return -1;
        }

        printf("  %s, %d / %d bytes at offset %0X\n", filename,
            *(uint32_t*)header.compressedSize, *(uint32_t*)header.uncompressedSize, header.offset);

        if (jzReadData(zip, &header, data) != Z_OK) {
            printf("Couldn't read file data!");
            free(data);
            return -1;
        }

        free(data);

        return 0;
    }

string getZipRoot(){
	string zipRoot;
        if (resources::getMode() == "directory") {
            json jZipRoot =settings::getOptionForCurrentMode("zipRoot");
            if (jZipRoot.is_null()) {
                zipRoot = "unzip/";
            }
            else {
                zipRoot = jZipRoot.get<string>();
            }
        }
        return zipRoot;
}
 JZFile* openZip(string fn) {
        FILE* fp;
        string zipRoot = getZipRoot();

        string zipfn = zipRoot + fn;
        int err = fopen_s(&fp, zipfn.c_str(), "rb");
        if (err) return 0;
        JZFile* zip = jzfile_from_stdio_file(fp);
        return zip;
    }

    //file Content must be UTF 8 string
    string readFileContent(JZFile* zip, string fn) {
        JZFileHeader header;
        char filename[1024]; //assuming same with fn
        if (jzReadLocalFileHeader(zip, &header, filename, sizeof(filename))) return "";  
        int size = *(uint32_t*)header.uncompressedSize;
        char*  data = (char*) malloc(1 + size );
        *(data+size) = '\0';
        if (jzReadData(zip, &header, data) != Z_OK) {
            printf("Couldn't read file data!");
            free(data);
            return "";
        }
        string out = (string)(data);
        free(data);
        return out;
    }

    int findFileCallback(JZFile* zip, int idx, JZFileHeader* header, char* filename, void* _file) {
        long offset = zip->tell(zip); // store current position
        auto file = (static_cast<json*>(_file));

        if ((string)filename == (*file)["path"].get<string>()) {
            if (zip->seek(zip, header->offset, SEEK_SET)) return 0;
            (*file)["found"] = true;
            (*file)["content"]= readFileContent(zip, (string)filename);
            return 0;
        }
        zip->seek(zip, offset, SEEK_SET); // return to position
        return 1; // continue
    }
    int fileListingCallback(JZFile* zip, int idx, JZFileHeader* header, char* filename, void* _files) {
        long offset = zip->tell(zip); // store current position
        auto files = (static_cast<json*>(_files));

        if (zip->seek(zip, header->offset, SEEK_SET)) {
            printf("Cannot seek in zip file!");
            return 0; // abort
        }
        int size = *(uint32_t*)header->uncompressedSize; // need casting  , stupid patch on windows
        (*files)[string(filename)] = { {"size"  , size}, {"idx",idx} };  //just return the uncompressedSize and idx in zip
        //processFile(zip); // alters file offset
        zip->seek(zip, offset, SEEK_SET); // return to position
        return 1; // continue
    }
namespace controllers {

static bool endsWith(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}
json zipList(const json& input) { //list all available 
	json ip;
    string zipRoot= getZipRoot();
    ip["path"] = zipRoot;
    json op = fs::controllers::readDirectory(ip);
    json ret;
    for (const auto& it : op["returnValue"].items()) {
        string entry = it.value()["entry"].get<string>();
        if (endsWith(entry, ".zip")) {
            json ip2;
            ip2["path"] = zipRoot+entry;
            json res = fs::controllers::getStats(ip2);
            ret[entry] = res["returnValue"]["size"].get<int>();
        }
    }
    json out;
    out["returnValue"] = ret;
    out["success"] = true;
	return out;
}
json readFile(const json& input) {
    json output;
    if (!helpers::hasRequiredFields(input, { "zip" }) || !helpers::hasRequiredFields(input, { "path" })) {
        output["error"] = helpers::makeMissingArgErrorPayload();
        return output;
    }

    string zipfn = input["zip"].get<string>();
    JZFile* zip = openZip(zipfn);
    if (!zip) {
        output["error"] = helpers::makeErrorPayload("NE_FS_FILRDER", "cannot open zip " + zipfn);
        return output;
    }
    JZEndRecord endRecord;
    jzReadEndRecord(zip, &endRecord);

    string path = input["path"].get < string>();
    json file;
    file["path"] = path;

    jzReadCentralDirectory(zip, &endRecord, findFileCallback, &file );
    zip->close(zip);
    if (file["found"].get<bool>()) {
        string str = file["content"].get<string>();
        output["returnValue"] = str;
        output["success"] = true;
    } else {
        output["error"]= helpers::makeErrorPayload("NE_FS_FILRDER", "cannot open file in zip " + path);
    }
    return output;
}
json fileList(const json &input){
    json output;
    
    JZEndRecord endRecord;
    
    if( !helpers::hasRequiredFields(input, {"zip"})  ) {
        output["error"] = helpers::makeMissingArgErrorPayload();
        return output;
    }

    string zipfn = input["zip"].get<string>();
    JZFile* zip=openZip(zipfn);

    if (!zip) {
    	 output["error"] =helpers::makeErrorPayload("NE_FS_FILRDER", "cannot open zip " + zipfn);
        return output;
   }
    jzReadEndRecord(zip, &endRecord);   
    json files;
    jzReadCentralDirectory(zip, &endRecord, fileListingCallback, &files);
    zip->close(zip);
    output["returnValue"] = files;
    output["success"] = true;
    return output;
}

}
}
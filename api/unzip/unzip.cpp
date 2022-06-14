#include <stdio.h>
#include <stdlib.h>
#include <filesystem>
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

map<string, JZFile *> pool;
map<string, json > directories;
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
    void split(std::string& string, std::vector<std::string>& tokens, const char& delim) {
        std::string ea;
        std::stringstream stream(string);
        while (getline(stream, ea, delim))
            tokens.push_back(ea);
    }
    int getFileOffset(json zipdirectory,string filename) {
        filesystem::path path(filename);
        string dir = path.parent_path().string();
        string fn = path.filename().string();
        int offset = -1;
        json* directory = &zipdirectory;
        if (fn != "") { //dig into the 
            vector <string> folders;
            split(dir, folders, '/');
            for (string s : folders) {
                directory = &((*directory)[s]);
            }
            if ((*directory)[string(fn)].is_number()) {
                offset = (*directory)[string(fn)];
            }
        }
        return offset;
    }
    int directoryCallback(JZFile* zip, int idx, JZFileHeader* header, char* filename, void* _files) {
        long offset = zip->tell(zip); // store current position
        auto files = (static_cast<json*>(_files));
        if (zip->seek(zip, header->offset, SEEK_SET)) {
            printf("Cannot seek in zip file!");
            return 0; // abort
        }
        filesystem::path path(filename);
        string dir = path.parent_path().string(); 
        string fn = path.filename().string(); 

        if (fn != "") { //dig into the 
            json *directory = files;
            vector <string> folders;
            split(dir, folders, '/');
            for (string s : folders){
                if ((*directory)[s].is_null()) (*directory)[s] = {};
                directory = &((*directory)[s]);
            }
            (*directory)[string(fn)] = header->offset ;
        }
        zip->seek(zip, offset, SEEK_SET); // return to position
        return 1; // continue

    }
    json buildDirectory(JZFile *zip) {
        JZEndRecord endRecord;
        json files;
        jzReadEndRecord(zip, &endRecord);
        jzReadCentralDirectory(zip, &endRecord, directoryCallback, &files);

        return files;
    }
    JZFile* openZip(string fn) {
            auto itr = pool.find(fn);
            if (itr !=pool.end()) {
                return itr->second;
            }
            FILE* fp;
            string zipRoot = getZipRoot();

            string zipfn = zipRoot + fn;
            int err = fopen_s(&fp, zipfn.c_str(), "rb");
            if (err) return 0;
            JZFile* zip = jzfile_from_stdio_file(fp);
            json files= buildDirectory(zip);
            pool.insert({ fn, zip });
            directories.insert({ fn,files });
            return zip;
    }

    //file Content must be UTF 8 string
    string readFileContent(JZFile* zip, int offset) {
        JZFileHeader header;
        char filename[1024]; //assuming same with fn

        if (zip->seek(zip, offset, SEEK_SET)) {
            printf("Cannot seek in zip file!");
            return ""; // abort
        }

        if (jzReadLocalFileHeader(zip, &header, filename, sizeof(filename))) return "";  
        int size = *(uint32_t*)header.uncompressedSize;
        char*  data = (char*) malloc(1 + size );
        *(data+size) = 0;
        if (jzReadData(zip, &header, data) != Z_OK) {
            printf("Couldn't read file data!");
            free(data);
            return "";
        }
        string out = data;
        free(data);
        return out;
    }


namespace controllers {

static bool endsWith(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}
json zipList(const json& input) { //list all zip files in zip folder
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
    string path = input["path"].get<string>();
    auto itr = directories.find(zipfn);
    json directory=itr->second;
    int offset = getFileOffset(directory, path);
    if (offset >= 0) {
        string content = readFileContent(zip, offset);
        if (content != "") {
            output["returnValue"] = content;
            output["success"] = true;
            return output;
        }
    }
    output["error"]= helpers::makeErrorPayload("NE_FS_FILRDER", "cannot open file in zip " + path);
    
    return output;
}
json fileList(const json &input){
    json output;
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
    auto itr = directories.find(zipfn);
    if (itr != directories.end()) {
        output["returnValue"] = itr->second;
        output["success"] = true;
    }
    else {
        output["error"] = helpers::makeErrorPayload("NE_FS_FILRDER", "cannot get directories in zip");
    }
    return output;
}

}
}
#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cerrno>

namespace framework {
struct Grids {
    static std::vector<int64_t> read(const std::string& filename){
        std::ifstream ifs{filename};

        if(!ifs){
            fprintf(stderr, "open failed: %s(%d)\n", strerror(errno), errno);
            abort();
        }

        std::vector<int64_t> ts{};
        std::string line{};
        while(std::getline(ifs,line)){
            if(line.empty()) continue;

            ts.push_back(std::stoll(line));
        }

        return ts;
    }
};
}
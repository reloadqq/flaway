#include "uuid.hpp"
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <chrono>
#include <random>

std::string
GenerateUuid()
{
        std::stringstream uuid;

        std::random_device rd;
        std::seed_seq seq = { rd(), rd(), rd(), rd(),
            static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()) };
        std::mt19937 rng(seq);
        std::uniform_int_distribution<> udist;

        uuid << std::hex;
        
        uuid << udist(rng) << "_" << udist(rng);

        return uuid.str();
}

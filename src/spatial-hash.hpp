#ifndef SPATIAL_HASH_H
#define SPATIAL_HASH_H

#include <vector>
#include <cstdint>
#include "star-utils.hpp"

namespace lost {

class SpatialHash {
public:

    void Build(const Catalog &catalog);


    std::vector<uint16_t> GetStarsNear(const Vec3 &boresight) const;

private:
    static constexpr int GRID_SIZE = 10;
    static constexpr int NUM_BINS = GRID_SIZE * GRID_SIZE * GRID_SIZE;

    std::vector<uint16_t> sortedCatalogIndices;
    int binOffsets[NUM_BINS + 1] = {0};


    int VectorToBin(const Vec3 &v) const;
    void VectorToGridCoords(const Vec3 &v, int &x, int &y, int &z) const;
};

}

#endif
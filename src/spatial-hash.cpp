#include "spatial-hash.hpp" 
#include "star-utils.hpp"   
#include <vector>
#include <algorithm>      
#include <cstdint>      
#include <cmath>            

namespace lost {


int SpatialHash::VectorToBin(const Vec3 &v) const {
    int x = static_cast<int>((v.x + 1.0) * 0.5 * GRID_SIZE);
    int y = static_cast<int>((v.y + 1.0) * 0.5 * GRID_SIZE);
    int z = static_cast<int>((v.z + 1.0) * 0.5 * GRID_SIZE);

    x = std::max(0, std::min(x, GRID_SIZE - 1));
    y = std::max(0, std::min(y, GRID_SIZE - 1));
    z = std::max(0, std::min(z, GRID_SIZE - 1));

    return x + (y * GRID_SIZE) + (z * GRID_SIZE * GRID_SIZE);
}

void SpatialHash::Build(const Catalog &catalog) {
    struct StarBinPair {
        uint16_t catalogIndex;
        int bin;
    };
    std::vector<StarBinPair> pairs(catalog.size());


    for (size_t i = 0; i < catalog.size(); ++i) {
        pairs[i] = { static_cast<uint16_t>(i), VectorToBin(catalog[i].spatial) };
    }


    std::sort(pairs.begin(), pairs.end(), [](const StarBinPair &a, const StarBinPair &b) {
        return a.bin < b.bin;
    });


    sortedCatalogIndices.resize(catalog.size());
    std::fill(std::begin(binOffsets), std::end(binOffsets), 0);

    for (size_t i = 0; i < pairs.size(); ++i) {
        sortedCatalogIndices[i] = pairs[i].catalogIndex;
        

        if (i == 0 || pairs[i].bin != pairs[i - 1].bin) {
            binOffsets[pairs[i].bin] = i;
        }
    }


    for (int i = NUM_BINS - 1; i >= 0; --i) {
        if (binOffsets[i] == 0 && i != pairs[0].bin) {
            binOffsets[i] = binOffsets[i + 1];
        }
    }
    binOffsets[NUM_BINS] = catalog.size(); 
}

void SpatialHash::VectorToGridCoords(const Vec3 &v, int &x, int &y, int &z) const {
    x = static_cast<int>((v.x + 1.0) * 0.5 * GRID_SIZE);
    y = static_cast<int>((v.y + 1.0) * 0.5 * GRID_SIZE);
    z = static_cast<int>((v.z + 1.0) * 0.5 * GRID_SIZE);


    x = std::max(0, std::min(x, GRID_SIZE - 1));
    y = std::max(0, std::min(y, GRID_SIZE - 1));
    z = std::max(0, std::min(z, GRID_SIZE - 1));
}

std::vector<uint16_t> SpatialHash::GetStarsNear(const Vec3 &boresight) const {
    std::vector<uint16_t> nearbyStars;
    

    nearbyStars.reserve(200);

    int cx, cy, cz;
    VectorToGridCoords(boresight, cx, cy, cz);

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                
                int nx = cx + dx;
                int ny = cy + dy;
                int nz = cz + dz;

                if (nx >= 0 && nx < GRID_SIZE &&
                    ny >= 0 && ny < GRID_SIZE &&
                    nz >= 0 && nz < GRID_SIZE) {

                    int binIndex = nx + (ny * GRID_SIZE) + (nz * GRID_SIZE * GRID_SIZE);

                    int startIdx = binOffsets[binIndex];
                    int endIdx = binOffsets[binIndex + 1];


                    for (int i = startIdx; i < endIdx; ++i) {
                        nearbyStars.push_back(sortedCatalogIndices[i]);
                    }
                }
            }
        }
    }

    return nearbyStars;
}
}
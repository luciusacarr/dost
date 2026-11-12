#ifndef DATABASE_BUILDER_H
#define DATABASE_BUILDER_H

#include <stdlib.h>

#include <array>
#include <cstdint>
#include <vector>

#include "serialize-helpers.hpp"
#include "star-utils.hpp"

namespace lost {

const int32_t kCatalogMagicValue = 0xF9A283BC;

inline bool isFlagSet(uint32_t dbFlags, uint32_t flag);

/**
 * A data structure enabling constant-time range queries into fixed numerical data.
 *
 * @note Not an instantiable database on its own -- used in other databases
 */
// TODO: QueryConservative, QueryExact, QueryTrapezoidal?
class KVectorIndex {
public:
    explicit KVectorIndex(DeserializeContext *des);

    long QueryLiberal(decimal minQueryDistance, decimal maxQueryDistance, long *upperIndex) const;

    /// The number of data points in the data referred to by the kvector
    long NumValues() const { return numValues; };
    long NumBins() const { return numBins; };
    /// Upper bound on elements
    decimal Max() const { return max; };
    // Lower bound on elements
    decimal Min() const { return min; };
private:
    long BinFor(decimal dist) const;

    long numValues;
    decimal min;
    decimal max;
    decimal binWidth;
    long numBins;
    const int32_t *bins;
};

void SerializePairDistanceKVector(SerializeContext *, const Catalog &, decimal minDistance, decimal maxDistance, long numBins);

/**
 * A database storing distances between pairs of stars.
 * Supports fast range queries to find all pairs of stars separated by approximately a certain distance.
 * @warning Sensitive to uncalibrated camera parameters
 */
class PairDistanceKVectorDatabase {
public:
    explicit PairDistanceKVectorDatabase(DeserializeContext *des);

    const int16_t *FindPairsLiberal(decimal min, decimal max, const int16_t **end) const;
    const int16_t *FindPairsExact(const Catalog &, decimal min, decimal max, const int16_t **end) const;
    std::vector<decimal> StarDistances(int16_t star, const Catalog &) const;

    /// Upper bound on stored star pair distances
    decimal MaxDistance() const { return index.Max(); };
    /// Lower bound on stored star pair distances
    decimal MinDistance() const { return index.Min(); };
    /// Exact number of stored pairs
    long NumPairs() const;

    /// Magic value to use when storing inside a MultiDatabase
    static const int32_t kMagicValue; // 0x2536f009
    // apparently you're supposed to not actually put the value of the static variables here, but
    // rather in a cpp implementation file.
private:
    KVectorIndex index;
    // TODO: endianness
    const int16_t *pairs;
};

// Filter star catalog to just the ones that'll be used to generate patterns
std::vector<uint16_t> TetraPreprocessCatalog(const Catalog&,
                                             const decimal maxFovDeg,
                                             int numPatternStarsPerFov);

void SerializeTetraDatabase(SerializeContext*,
                            const Catalog&,
                            decimal maxFovDeg,
                            const std::vector<uint16_t>& catalogPatternStarInds,
                            int numEdgeRatioBins);

// Tetra star pattern = 4 star IDs (Tetra-internal indices)
using TetraPattern = std::array<uint16_t, TetraConstants::numPatternStars>;

// Number of 32-bit words that hold the packed patterns of a Tetra database
size_t TetraPatternWords(size_t numPatterns, int patternBits);

/**
 * A database storing Tetra star patterns
 *
 * Patterns are grouped into hash buckets and stored back to back in bucket order, so the table has
 * no empty slots. The first star of a pattern is its earliest star in the Tetra catalog, and the
 * other three are stored as positions in that star's neighbor list (the later Tetra stars within
 * maxFov of it), which takes far fewer bits than three star indices.
 *
 * Layout:
 * | size (bytes)                 | name              | description                                              |
 * |------------------------------+-------------------+----------------------------------------------------------|
 * | sizeof(decimal)              | maxFov            | max angle (deg) between any 2 pattern stars              |
 * | 4                            | numEdgeRatioBins  | number of bins for edge ratio discretization             |
 * | 4                            | numBuckets        | number of hash buckets                                   |
 * | 4                            | numPatterns       | number of patterns                                       |
 * | 4                            | numNeighbors      | total length of all neighbor lists                       |
 * | 2                            | tetraCatalogSize  | number of stars in Tetra preprocessed catalog            |
 * | 1                            | starBits          | bits per Tetra star index in a packed pattern            |
 * | 1                            | neighborBits      | bits per neighbor list position in a packed pattern      |
 * | 4 * (numPatterns >> 16)      | bucketBlockStarts | entry h-1: first bucket starting at pattern >= h * 65536 |
 * | 4 * (tetraCatalogSize + 1)   | neighborStarts    | where each star's neighbor list starts in neighbors      |
 * | 4 * TetraPatternWords()      | patternWords      | patterns, starBits + 3 * neighborBits bits each          |
 * | 2 * (numBuckets + 1)         | bucketStartsLow   | low 16 bits of the index of each bucket's first pattern  |
 * | 2 * numNeighbors             | neighbors         | neighbor lists (Tetra indices), back to back             |
 * | 2 * tetraCatalogSize         | catalogIndices    | maps Tetra index to real catalog index                   |
 * |------------------------------+-------------------+----------------------------------------------------------|
 */
class TetraDatabase {
   public:
    explicit TetraDatabase(DeserializeContext* des);

    // Max FOV (in degrees) allowed between stars in the same pattern
    decimal MaxFov() const { return maxFov_; }

    // Number of bins used for edge ratio binning
    int NumEdgeRatioBins() const { return numEdgeRatioBins_; }

    // Number of hash buckets the patterns are grouped into
    uint32_t NumBuckets() const { return numBuckets_; }

    // Number of patterns in the database
    uint32_t NumPatterns() const { return numPatterns_; }

    // Get the 4-tuple pattern at index, in [0, NumPatterns())
    TetraPattern GetPattern(size_t index) const;

    // Returns all the patterns in a bucket, in [0, NumBuckets())
    std::vector<TetraPattern> GetPatternMatches(size_t bucket) const;

    uint16_t CatalogIndex(int tetraCatalogInd) const;

    /// Magic value to use when storing inside a MultiDatabase
    static const int32_t kMagicValue;

   private:
    // Index of the first pattern in a bucket. bucket may be NumBuckets(), giving NumPatterns().
    uint32_t BucketStart(uint32_t bucket) const;

    decimal maxFov_;
    int32_t numEdgeRatioBins_;
    uint32_t numBuckets_;
    uint32_t numPatterns_;
    uint32_t numBucketBlocks_;
    uint16_t tetraCatalogSize_;
    int starBits_;
    int neighborBits_;
    int patternBits_;
    const uint32_t* bucketBlockStarts_;
    const uint32_t* neighborStarts_;
    const uint32_t* patternWords_;
    const uint16_t* bucketStartsLow_;
    const uint16_t* neighbors_;
    const uint16_t* catalogIndices_;
};

// /**
//  * @brief Stores "inner angles" between star triples
//  * @details Unsensitive to first-order error in basic camera
//  * parameters (eg, wrong FOV or principal point), can be sensitive to second-order errors (eg,
//  * camera distortion, which may cause the effective FOV or principal point to be different in
//  * different parts of the image). Used for Mortari's Non-Dimensional Star-ID
//  */
// class TripleInnerKVectorDatabase {
// public:
//     explicit TripleInnerKVectorDatabase(const unsigned char *databaseBytes);

//     void FindTriplesLiberal(decimal min, decimal max, long **begin, long **end) const;
// private:
//     KVectorIndex index;
//     int16_t *triples;
// };

/**
 * A database that contains multiple databases
 * This is almost always the database that is actually passed to star-id algorithms in the real world, since you'll want to store at least the catalog plus one specific database.
 * Multi-databases are essentially a map from "magic values" to database buffers.
 */

#define MULTI_DB_FLOAT_FLAG 0x0001 // By default, our DB is in double mode.

class MultiDatabase {
public:
    /// Create a multidatabase from a serialized multidatabase.
    explicit MultiDatabase(const unsigned char *buffer) : buffer(buffer) { };
    const unsigned char *SubDatabasePointer(int32_t magicValue) const;
private:
    const unsigned char *buffer;
};

class MultiDatabaseEntry {
public:
    MultiDatabaseEntry(int32_t magicValue, std::vector<unsigned char> bytes) // I wonder if making `bytes` a reference would avoid making two copies, or maybe it would be worse by preventing copy-elision
        : magicValue(magicValue), bytes(bytes) { }

    int32_t magicValue;
    uint32_t flags;
    std::vector<unsigned char> bytes;
};

using MultiDatabaseDescriptor = std::vector<MultiDatabaseEntry>;

void SerializeMultiDatabase(SerializeContext *, const MultiDatabaseDescriptor &dbs, uint32_t flags);

}

#endif

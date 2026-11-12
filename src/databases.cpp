#include "databases.hpp"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#include "attitude-utils.hpp"
#include "serialize-helpers.hpp"
#include "star-utils.hpp"

namespace lost {

const int32_t PairDistanceKVectorDatabase::kMagicValue = 0x2536f009;
// Changed from 0x26683787 when the layout changed, so that old databases are not read as new ones
const int32_t TetraDatabase::kMagicValue = 0x5A1D7E72;

inline bool isFlagSet(uint32_t dbFlags, uint32_t flag) {
   return (dbFlags & flag) != 0;
}

struct KVectorPair {
    int16_t index1;
    int16_t index2;
    decimal distance;
};

bool CompareKVectorPairs(const KVectorPair &p1, const KVectorPair &p2) {
    return p1.distance < p2.distance;
}

// just the index part of the kvector, doesn't store the sorted list it refers to. This makes it
// flexible whether used to store star distance angles, an angle of a triangle, etc.
/**
 K-vector index layout. No magic value because its never used on its own.
 | size          | name       | description                                                 |
 |---------------+------------+-------------------------------------------------------------|
 | 4             | numEntries |                                                             |
 | sizeof decimal  | min        | minimum value contained in the database                     |
 | sizeof decimal  | max        | max value contained in index                                |
 | 4             | numBins    |                                                             |
 | 4*(numBins+1) | bins       | The `i'th bin (starting from zero) stores how many pairs of |
 |               |            | stars have a distance lesst han or equal to:                |
 |               |            | min+i*(max-min)/numBins                                     |
 */

// apparently there's no easy way to accept an iterator argument. Hate java all you want, but at
// least it can do that!
// https://stackoverflow.com/questions/5054087/declare-a-function-accepting-generic-iterator or
// rather, the correct way is to use a template and just use the ++ and * operators willy-nilly,
// which will throw an error if the operators are not implemented.

/**
 * Serialize a KVector index to disk.
 * Use SerializeLengthKVectorIndex to determine how long the buffer should be.
 * @param values The actual entries the kvector should be referring to, sorted in ascending order.
 * @pre values must be sorted in ascending order!
 * @param min,max Guaranteed bounds on the entries of values
 * @todo Consider replacing min and max parameters by just calculating the min and max of values?
 * @param numBins the number of "bins" the KVector should use. A higher number makes query results "tighter" but takes up more disk space. Usually should be set somewhat smaller than (max-min) divided by the "width" of the typical query.
 * @param buffer[out] index is written here.
 */
void SerializeKVectorIndex(SerializeContext *ser, const std::vector<decimal> &values, decimal min, decimal max, long numBins) {
    std::vector<int32_t> kVector(numBins+1); // We store sums before and after each bin
    decimal binWidth = (max - min) / numBins;

    // generate the k-vector part
    // Idea: When we find the first star that's across any bin boundary, we want to update all the newly sealed bins
    long lastBin = 0; // first bin the last star belonged to
    for (int32_t i = 0; i < (int32_t)values.size(); i++) {
        if (i > 0) {
            assert(values[i] >= values[i-1]);
        }
        assert(values[i] >= min);
        assert(values[i] <= max);
        long thisBin = (long)ceil((values[i] - min) / binWidth); // first bin we belong to
        assert(thisBin >= 0);
        assert(thisBin <= numBins); // thisBin == numBins is acceptable since kvector length == numBins + 1
        // if thisBin > lastBin, then no more stars can be added to those bins between thisBin and lastBin, so set them.
        for (long bin = lastBin; bin < thisBin; bin++) {
            kVector[bin] = i; // our index is the number of pairs with shorter distance
        }
        lastBin = thisBin;
    }
    for (long bin = lastBin; bin <= numBins; bin++) {
        kVector[bin] = values.size();
    }

    // verify kvector
    int32_t lastBinVal = -1;
    for (const int32_t &bin : kVector) {
        assert(bin >= lastBinVal);
        lastBinVal = bin;
    }

    // metadata fields
    SerializePrimitive<int32_t>(ser, values.size());
    SerializePrimitive<decimal>(ser, min);
    SerializePrimitive<decimal>(ser, max);
    SerializePrimitive<int32_t>(ser, numBins);

    // kvector index field
    for (const int32_t &bin : kVector) {
        SerializePrimitive<int32_t>(ser, bin);
    }
}

/// Construct from serialized buffer.
KVectorIndex::KVectorIndex(DeserializeContext *des) {

    numValues = DeserializePrimitive<int32_t>(des);
    min = DeserializePrimitive<decimal>(des);
    max = DeserializePrimitive<decimal>(des);
    numBins = DeserializePrimitive<int32_t>(des);

    assert(min >= DECIMAL(0.0));
    assert(max > min);
    binWidth = (max - min) / numBins;

    bins = DeserializeArray<int32_t>(des, numBins+1);
}

/**
 * Finds all the entries in the given range, and possibly a few just outside the range on the ends.
 * @param upperIndex[out] Is set to the index of the last returned value +1.
 * @return the index (starting from zero) of the first value matching the query
 */
long KVectorIndex::QueryLiberal(decimal minQueryDistance, decimal maxQueryDistance, long *upperIndex) const {
    assert(maxQueryDistance > minQueryDistance);
    if (maxQueryDistance >= max) {
        maxQueryDistance = max - DECIMAL(0.00001); // TODO: better way to avoid hitting the bottom bin
    }
    if (minQueryDistance <= min) {
        minQueryDistance = min + DECIMAL(0.00001);
    }
    if (minQueryDistance > max || maxQueryDistance < min) {
        *upperIndex = 0;
        return 0;
    }
    long lowerBin = BinFor(minQueryDistance);
    long upperBin = BinFor(maxQueryDistance);
    assert(upperBin >= lowerBin);
    assert(upperBin <= numBins);
    // bins[lowerBin-1]=number of pairs <= r < query distance, so it is the index of the
    // first possible item that might be equal to the query distance
    int lowerIndex = bins[lowerBin-1];
    if (lowerIndex >= numValues) {
        // all pairs have distance less than queried. Return value is irrelevant as long as
        // numReturned=0
        return 0;
    }
    // bins[upperBin]=number of pairs <= r >= query distance
    *upperIndex = bins[upperBin];
    return lowerIndex;
}

/// return the lowest-indexed bin that contains the number of pairs with distance <= dist
long KVectorIndex::BinFor(decimal query) const {
    long result = (long)ceil((query - min) / binWidth);
    assert(result >= 0);
    assert(result <= numBins);
    return result;
}

/**
 pair K-vector database layout. The kvector appears before the bulk pair data because it contains the
 number of pairs, which is necessary to read the bulk pair data.

     | size (bytes)             | name         | description                                                 |
     |--------------------------+--------------+-------------------------------------------------------------|
     | sizeof kvectorIndex      | kVectorIndex | Serialized KVector index                                    |
     | 2*sizeof(int16)*numPairs | pairs        | Bulk pair data                                              |
 */
std::vector<KVectorPair> CatalogToPairDistances(const Catalog &catalog, decimal minDistance, decimal maxDistance) {
    std::vector<KVectorPair> result;
    for (int16_t i = 0; i < (int16_t)catalog.size(); i++) {
        for (int16_t k = i+1; k < (int16_t)catalog.size(); k++) {

            KVectorPair pair = { i, k, AngleUnit(catalog[i].spatial, catalog[k].spatial) };
            assert(isfinite(pair.distance));
            assert(pair.distance >= 0);
            assert(pair.distance <= DECIMAL_M_PI);

            if (pair.distance >= minDistance && pair.distance <= maxDistance) {
                // we'll sort later
                result.push_back(pair);
            }
        }
    }
    return result;
}

/**
 * Serialize a pair-distance KVector into buffer.
 * Use SerializeLengthPairDistanceKVector to determine how large the buffer needs to be. See command line documentation for other options.
 */
void SerializePairDistanceKVector(SerializeContext *ser, const Catalog &catalog, decimal minDistance, decimal maxDistance, long numBins) {
    std::vector<int32_t> kVector(numBins+1); // numBins = length, all elements zero
    std::vector<KVectorPair> pairs = CatalogToPairDistances(catalog, minDistance, maxDistance);

    // sort pairs in increasing order.
    std::sort(pairs.begin(), pairs.end(), CompareKVectorPairs);
    std::vector<decimal> distances;

    for (const KVectorPair &pair : pairs) {
        distances.push_back(pair.distance);
    }

    // index field
    SerializeKVectorIndex(ser, distances, minDistance, maxDistance, numBins);

    // bulk pairs field
    for (const KVectorPair &pair : pairs) {
        SerializePrimitive<int16_t>(ser, pair.index1);
        SerializePrimitive<int16_t>(ser, pair.index2);
    }
}

std::vector<uint16_t> TetraPreprocessCatalog(const Catalog& catalog,
                                             const decimal maxFovDeg,
                                             int numPatternStarsPerFov) {
    // To eliminate ambiguity (double stars), enforce that all
    // pattern stars must be > 0.05 degrees apart
    const decimal starMinSepRad = DegToRad(DECIMAL(0.05));
    const decimal maxFovRad = DegToRad(maxFovDeg);

    // Catalog is sorted brightest first so brighter stars get priority,
    // banking on the assumption that brighter stars have lower centroiding error
    std::vector<uint16_t> patternStarInds{0};

    for (size_t i = 1; i < catalog.size(); i++) {
        const Vec3& vec = catalog[i].spatial;

        // Reject catalog star if it is a double star of an already-kept star, or if there are
        // already numPatternStarsPerFov kept stars within maxFov/2 of it
        bool keep = true;
        int numPatternStarsInFov = 0;
        for (uint16_t j : patternStarInds) {
            decimal angle = Angle(vec, catalog[j].spatial);
            if (angle < starMinSepRad) {
                keep = false;
                break;
            }
            if (angle < maxFovRad / 2 && ++numPatternStarsInFov >= numPatternStarsPerFov) {
                keep = false;
                break;
            }
        }

        if (keep) {
            patternStarInds.push_back(i);
        }
    }
    return patternStarInds;
}

TetraDatabase::TetraDatabase(DeserializeContext* des) {
    maxFov_ = DeserializePrimitive<decimal>(des);
    numEdgeRatioBins_ = DeserializePrimitive<int32_t>(des);
    numBuckets_ = DeserializePrimitive<uint32_t>(des);
    numPatterns_ = DeserializePrimitive<uint32_t>(des);
    uint32_t numNeighbors = DeserializePrimitive<uint32_t>(des);
    tetraCatalogSize_ = DeserializePrimitive<uint16_t>(des);
    starBits_ = DeserializePrimitive<uint8_t>(des);
    neighborBits_ = DeserializePrimitive<uint8_t>(des);
    patternBits_ = starBits_ + (TetraConstants::numPatternStars - 1) * neighborBits_;

    numBucketBlocks_ = numPatterns_ >> 16;
    bucketBlockStarts_ = DeserializeArray<uint32_t>(des, numBucketBlocks_);
    neighborStarts_ = DeserializeArray<uint32_t>(des, (long)tetraCatalogSize_ + 1);
    patternWords_ =
        DeserializeArray<uint32_t>(des, (long)TetraPatternWords(numPatterns_, patternBits_));
    bucketStartsLow_ = DeserializeArray<uint16_t>(des, (long)numBuckets_ + 1);
    neighbors_ = DeserializeArray<uint16_t>(des, numNeighbors);
    catalogIndices_ = DeserializeArray<uint16_t>(des, tetraCatalogSize_);
}

uint64_t TetraPatternWords(uint64_t numPatterns, int patternBits) {
    // Two words of padding so that a pattern can always be read as three whole words
    return (numPatterns * patternBits + 31) / 32 + 2;
}

uint32_t TetraDatabase::BucketStart(uint32_t bucket) const {
    // The high 16 bits are the number of 65536-pattern blocks that start at or before the bucket
    const uint32_t* blocksEnd = bucketBlockStarts_ + numBucketBlocks_;
    uint32_t high = std::upper_bound(bucketBlockStarts_, blocksEnd, bucket) - bucketBlockStarts_;
    return high << 16 | bucketStartsLow_[bucket];
}

TetraPattern TetraDatabase::GetPattern(size_t index) const {
    uint64_t bitPos = (uint64_t)index * patternBits_;
    size_t word = bitPos >> 5;
    int shift = bitPos & 31;
    uint64_t packed = (patternWords_[word] | (uint64_t)patternWords_[word + 1] << 32) >> shift;
    if (shift + patternBits_ > 64) {
        packed |= (uint64_t)patternWords_[word + 2] << (64 - shift);
    }

    TetraPattern res;
    res[0] = packed & ((1u << starBits_) - 1);
    packed >>= starBits_;
    const uint16_t* neighbors = neighbors_ + neighborStarts_[res[0]];
    for (int i = 1; i < TetraConstants::numPatternStars; i++) {
        res[i] = neighbors[packed & ((1u << neighborBits_) - 1)];
        packed >>= neighborBits_;
    }
    return res;
}

uint16_t TetraDatabase::CatalogIndex(int tetraCatalogInd) const {
    return catalogIndices_[tetraCatalogInd];
}

std::vector<TetraPattern> TetraDatabase::GetPatternMatches(size_t bucket) const {
    uint32_t start = BucketStart(bucket);
    uint32_t end = BucketStart(bucket + 1);
    std::vector<TetraPattern> res;
    res.reserve(end - start);
    for (uint32_t i = start; i < end; i++) {
        res.push_back(GetPattern(i));
    }
    return res;
}

/// Create the database from a serialized buffer.
PairDistanceKVectorDatabase::PairDistanceKVectorDatabase(DeserializeContext *des)
    : index(KVectorIndex(des)) {

    pairs = DeserializeArray<int16_t>(des, 2*index.NumValues());
}

/// Return the value in the range [low,high] which is closest to num
decimal Clamp(decimal num, decimal low, decimal high) {
    return num < low ? low : num > high ? high : num;
}

/**
 * Return at least all the star pairs whose inter-star distance is between min and max
 * @param end[out] Is set to an "off-the-end" pointer, one past the last pair being returned by the query.
 * @return A pointer to the start of the matched pairs. Each pair is stored as simply two 16-bit integers, each of which is a catalog index. (you must increment the pointer twice to get to the next pair).
 */
const int16_t *PairDistanceKVectorDatabase::FindPairsLiberal(
    decimal minQueryDistance, decimal maxQueryDistance, const int16_t **end) const {

    assert(maxQueryDistance <= DECIMAL_M_PI);

    long upperIndex = -1;
    long lowerIndex = index.QueryLiberal(minQueryDistance, maxQueryDistance, &upperIndex);
    *end = &pairs[upperIndex * 2];
    return &pairs[lowerIndex * 2];
}

const int16_t *PairDistanceKVectorDatabase::FindPairsExact(const Catalog &catalog,
                                                           decimal minQueryDistance, decimal maxQueryDistance, const int16_t **end) const {

    // Instead of computing the angle for every pair in the database, we pre-compute the /cosines/
    // of the min and max query distances so that we can compare against dot products directly! As
    // angle increases, cosine decreases, up to DECIMAL_M_PI (and queries larger than that don't really make
    // sense anyway)
    assert(maxQueryDistance <= DECIMAL_M_PI);

    decimal maxQueryCos = DECIMAL_COS(minQueryDistance);
    decimal minQueryCos = DECIMAL_COS(maxQueryDistance);

    long liberalUpperIndex;
    long liberalLowerIndex = index.QueryLiberal(minQueryDistance, maxQueryDistance, &liberalUpperIndex);
    // now we need to find the first and last index that actually matches the query
    // step the lower index forward
    // There's no good reason to be using >= and <= for the comparison against max/min, but the tests fail otherwise (because they use angle, with its acos, instead of forward cos like us). It's an insignificant difference.
    while (liberalLowerIndex < liberalUpperIndex
           && catalog[pairs[liberalLowerIndex*2]].spatial * catalog[pairs[liberalLowerIndex*2+1]].spatial >= maxQueryCos
        )
    { liberalLowerIndex++; }

    // step the upper index backward
    while (liberalLowerIndex < liberalUpperIndex
           // the liberalUpperIndex is past the end of the logically returned range, so we need to subtract 1
           && catalog[pairs[(liberalUpperIndex-1)*2]].spatial * catalog[pairs[(liberalUpperIndex-1)*2+1]].spatial <= minQueryCos
        )
    { liberalUpperIndex--; }

    *end = &pairs[liberalUpperIndex * 2];
    return &pairs[liberalLowerIndex * 2];
}

/// Number of star pairs stored in the database
long PairDistanceKVectorDatabase::NumPairs() const {
    return index.NumValues();
}

/// Return the distances from the given star to each star it's paired with in the database (for debugging).
std::vector<decimal> PairDistanceKVectorDatabase::StarDistances(int16_t star, const Catalog &catalog) const {
    std::vector<decimal> result;
    for (int i = 0; i < NumPairs(); i++) {
        if (pairs[i*2] == star || pairs[i*2+1] == star) {
            result.push_back(AngleUnit(catalog[pairs[i*2]].spatial, catalog[pairs[i*2+1]].spatial));
        }
    }
    return result;
}

/// Number of bits needed to store any value in [0, numValues)
static int BitsFor(size_t numValues) {
    int bits = 1;
    while (bits < 16 && ((size_t)1 << bits) < numValues) {
        bits++;
    }
    return bits;
}

/// Smallest prime >= n. A prime number of buckets keeps TetraFindPatternIndex well distributed.
static uint32_t NextPrime(uint32_t n) {
    for (n = std::max<uint32_t>(n, 2);; n++) {
        bool isPrime = true;
        for (uint32_t d = 2; (uint64_t)d * d <= n; d++) {
            if (n % d == 0) {
                isPrime = false;
                break;
            }
        }
        if (isPrime) return n;
    }
}

void SerializeTetraDatabase(SerializeContext* ser,
                            const Catalog& catalog,
                            decimal maxFovDeg,
                            const std::vector<uint16_t>& catalogPatternStarInds,
                            int numEdgeRatioBins) {
    const decimal maxFovRad = DegToRad(maxFovDeg);
    const decimal cosMaxFov = DECIMAL_COS(maxFovRad);

    const size_t numPatternStars = catalogPatternStarInds.size();

    // Helper to look up a star's spatial vector given Tetra-internal star catalog index
    auto spatial = [&](size_t tetraInd) -> const Vec3& {
        return catalog[catalogPatternStarInds[tetraInd]].spatial;
    };

    // Neighbors of a star: the later stars within maxFov of it. Every pattern is generated from
    // its earliest star, so its other three stars are all neighbors of that star, and each 4-star
    // group is generated exactly once.
    std::vector<uint32_t> neighborStarts{0};
    std::vector<uint16_t> neighbors;
    size_t maxNeighbors = 0;
    for (size_t firstStarInd = 0; firstStarInd < numPatternStars; firstStarInd++) {
        const Vec3& v = spatial(firstStarInd);
        for (size_t starId = firstStarInd + 1; starId < numPatternStars; starId++) {
            if (v * spatial(starId) > cosMaxFov) {
                neighbors.push_back(starId);
            }
        }
        maxNeighbors = std::max(maxNeighbors, neighbors.size() - neighborStarts.back());
        neighborStarts.push_back(neighbors.size());
    }

    // A pattern is packed as its first star, then the positions of its other three stars in the
    // first star's neighbor list
    const int starBits = BitsFor(numPatternStars);
    const int neighborBits = BitsFor(maxNeighbors);
    const int patternBits = starBits + (TetraConstants::numPatternStars - 1) * neighborBits;

    auto unpack = [&](uint64_t packed) {
        TetraPattern patt;
        patt[0] = packed & ((1u << starBits) - 1);
        for (int i = 1; i < TetraConstants::numPatternStars; i++) {
            int pos = packed >> (starBits + (i - 1) * neighborBits) & ((1u << neighborBits) - 1);
            patt[i] = neighbors[neighborStarts[patt[0]] + pos];
        }
        return patt;
    };

    std::vector<uint64_t> patterns;
    for (size_t firstStarInd = 0; firstStarInd < numPatternStars; firstStarInd++) {
        size_t numNeighbors = neighborStarts[firstStarInd + 1] - neighborStarts[firstStarInd];
        CombinationIterator combos(3, numNeighbors);
        while (combos.next()) {
            const std::vector<int>& idx = combos.indices();
            uint64_t packed = firstStarInd;
            for (int i = 0; i < 3; i++) {
                packed |= (uint64_t)idx[i] << (starBits + i * neighborBits);
            }
            TetraPattern patt = unpack(packed);

            bool pattFits = AllPairsWithinFov(
                TetraConstants::numPatternStars,
                [&](int i) -> const Vec3& { return spatial(patt[i]); },
                cosMaxFov);

            if (pattFits) {
                patterns.push_back(packed);
            }
        }
    }

    std::cerr << "Tetra found " << patterns.size() << " patterns" << std::endl;
    if ((uint64_t)patterns.size() > UINT32_MAX) {
        std::cerr << "Too many Tetra patterns for the database format." << std::endl;
        exit(1);
    }

    // Hash every pattern to a bucket
    const uint32_t numBuckets = NextPrime(patterns.size() / TetraConstants::patternsPerBucket);
    std::vector<uint32_t> patternBuckets;
    patternBuckets.reserve(patterns.size());
    std::vector<uint32_t> bucketStarts(numBuckets + 1);
    for (uint64_t packed : patterns) {
        TetraPattern patt = unpack(packed);
        std::vector<decimal> edgeRatios = TetraEdgeRatios(
            TetraConstants::numPatternStars, [&](int i) -> const Vec3& { return spatial(patt[i]); });

        std::vector<int> binnedEdgeRatios;
        for (decimal edgeRatio : edgeRatios) {
            binnedEdgeRatios.push_back(int(edgeRatio * numEdgeRatioBins));
        }

        uint32_t bucket = TetraFindPatternIndex(
            binnedEdgeRatios.data(), binnedEdgeRatios.size(), numEdgeRatioBins, numBuckets);
        patternBuckets.push_back(bucket);
        bucketStarts[bucket + 1]++;
    }
    for (uint32_t bucket = 0; bucket < numBuckets; bucket++) {
        bucketStarts[bucket + 1] += bucketStarts[bucket];
    }

    // Store the patterns back to back in bucket order, patternBits each. There are no empty slots:
    // bucket b is patterns [bucketStarts[b], bucketStarts[b + 1])
    const uint64_t numPatternWords = TetraPatternWords(patterns.size(), patternBits);
    if (numPatternWords > std::vector<uint32_t>().max_size()) {
        std::cerr << "Tetra pattern table is too large for this machine." << std::endl;
        exit(1);
    }
    std::vector<uint32_t> patternWords(numPatternWords);
    std::vector<uint32_t> nextInBucket(bucketStarts.begin(), bucketStarts.end() - 1);
    for (size_t i = 0; i < patterns.size(); i++) {
        uint64_t bitPos = (uint64_t)nextInBucket[patternBuckets[i]]++ * patternBits;
        size_t word = bitPos >> 5;
        int shift = bitPos & 31;
        patternWords[word] |= (uint32_t)(patterns[i] << shift);
        patternWords[word + 1] |= (uint32_t)(patterns[i] >> (32 - shift));
        if (shift > 0) {
            patternWords[word + 2] |= (uint32_t)(patterns[i] >> (64 - shift));
        }
    }

    // Only the low 16 bits of each bucket start are stored per bucket. The high bits come from
    // bucketBlockStarts: entry h - 1 is the first bucket that starts at or after pattern h * 65536
    std::vector<uint32_t> bucketBlockStarts;
    for (uint32_t bucket = 0; bucket <= numBuckets; bucket++) {
        while (bucketBlockStarts.size() < bucketStarts[bucket] >> 16) {
            bucketBlockStarts.push_back(bucket);
        }
    }

    SerializePrimitive<decimal>(ser, maxFovDeg);                       // maxFov_
    SerializePrimitive<int32_t>(ser, numEdgeRatioBins);                // numEdgeRatioBins_
    SerializePrimitive<uint32_t>(ser, numBuckets);                     // numBuckets_
    SerializePrimitive<uint32_t>(ser, patterns.size());                // numPatterns_
    SerializePrimitive<uint32_t>(ser, neighbors.size());               // numNeighbors
    SerializePrimitive<uint16_t>(ser, catalogPatternStarInds.size());  // tetraCatalogSize_
    SerializePrimitive<uint8_t>(ser, starBits);                        // starBits_
    SerializePrimitive<uint8_t>(ser, neighborBits);                    // neighborBits_
    for (uint32_t bucket : bucketBlockStarts) {
        SerializePrimitive<uint32_t>(ser, bucket);
    }
    for (uint32_t start : neighborStarts) {
        SerializePrimitive<uint32_t>(ser, start);
    }
    for (uint32_t word : patternWords) {
        SerializePrimitive<uint32_t>(ser, word);
    }
    for (uint32_t start : bucketStarts) {
        SerializePrimitive<uint16_t>(ser, start & 0xFFFF);
    }
    for (uint16_t neighbor : neighbors) {
        SerializePrimitive<uint16_t>(ser, neighbor);
    }
    for (uint16_t ind : catalogPatternStarInds) {
        SerializePrimitive<uint16_t>(ser, ind);
    }
}

/**
   MultiDatabase memory layout:

   | size | name           | description                                 |
   |------+----------------+---------------------------------------------|
   |    4 | magicValue     | unique database identifier                  |
   |    4 | flags          | [X, X, X, isDouble?]                        |
   |    4 | databaseLength | length in bytes (32-bit unsigned)           |
   |    n | database       | the entire database. 8-byte aligned         |
   |  ... | ...            | More databases (each has value, length, db) |
   |    4 | caboose        | 4 null bytes indicate the end               |
 */

/**
 * @brief return a pointer to the start of the database type indicated by the magic value, if such
 * a sub-database is present in the database
 * @param magicValue
 * @return Returns a pointer to the start of the database type indicated by the magic value, null if
 * not found
 */
const unsigned char *MultiDatabase::SubDatabasePointer(int32_t magicValue) const {
    DeserializeContext desValue(buffer);
    DeserializeContext *des = &desValue; // just for naming consistency with how we use `des` elsewhere

    assert(magicValue != 0);
    while (true) {
        int32_t curMagicValue = DeserializePrimitive<int32_t>(des);
        if (curMagicValue == 0) {
            return nullptr;
        }
        uint32_t dbFlags = DeserializePrimitive<uint32_t>(des);

        // Ensure that our database is using the same type as the runtime.
        #ifdef LOST_FLOAT_MODE
            if (!isFlagSet(dbFlags, MULTI_DB_FLOAT_FLAG)) {
                std::cerr << "LOST was compiled in float mode. This database was serialized in double mode and is incompatible." << std::endl;
                exit(1);
            }
        #else
            if (isFlagSet(dbFlags, MULTI_DB_FLOAT_FLAG)) {
                std::cerr << "LOST was compiled in double mode. This database was serialized in float mode and is incompatible." << std::endl;
                exit(1);
            }
        #endif

        uint32_t dbLength = DeserializePrimitive<uint32_t>(des);
        assert(dbLength > 0);
        DeserializePadding<uint64_t>(des); // align to an 8-byte boundary
        const unsigned char *curSubDatabasePointer = DeserializeArray<unsigned char>(des, dbLength);
        if (curMagicValue == magicValue) {
            return curSubDatabasePointer;
        }
    }
    // shouldn't ever make it here. Compiler should remove this assertion as unreachable.
    assert(false);
}

void SerializeMultiDatabase(SerializeContext *ser,
                            const MultiDatabaseDescriptor &dbs,
                            uint32_t flags) {
    for (const MultiDatabaseEntry &multiDbEntry : dbs) {
        SerializePrimitive<int32_t>(ser, multiDbEntry.magicValue);
        SerializePrimitive<uint32_t>(ser, flags);
        SerializePrimitive<uint32_t>(ser, multiDbEntry.bytes.size());
        SerializePadding<uint64_t>(ser);
        std::copy(multiDbEntry.bytes.cbegin(), multiDbEntry.bytes.cend(), std::back_inserter(ser->buffer));
    }
    SerializePrimitive<int32_t>(ser, 0); // caboose
}

}  // namespace lost

// TODO: after creating the database, print more statistics, such as average number of pairs per
// star, stars per bin, space distribution between array and index.

#ifndef STAR_ID_H
#define STAR_ID_H

#include <vector>

#include "centroiders.hpp"
#include "star-utils.hpp"
#include "camera.hpp"
#include "spatial-hash.hpp"
#include "kalman-filter.hpp"

namespace lost {

/**
 * A star idenification algorithm.
 * An algorithm which takes a list of centroids plus some (possibly algorithm-specific) database, and then determines which centroids corresponds to which catalog stars.
 */
class StarIdAlgorithm {
public:
    /// Actualy perform the star idenification. This is the "main" function for StarIdAlgorithm
    virtual StarIdentifiers Go(
        const unsigned char *database, const Stars &, const Catalog &, const Camera &) const = 0;

    virtual ~StarIdAlgorithm() { };
};

/// A star-id algorithm that returns random results. For debugging.
class DummyStarIdAlgorithm final : public StarIdAlgorithm {
public:
    StarIdentifiers Go(const unsigned char *database, const Stars &, const Catalog &, const Camera &) const;
};

/**
 * A star-id algorithm based on assigning votes for each centroid-catalog pair then choosing the highest voted catalog stars.
 * While the geometric voting algorithm is the simplest true star-id algorithm I know of and is quite fast, it has some reliability issues and there are no statistical guarantees on how often it will return the wrong result. It will also frequently fail to idenify images with few stars.
 */
class GeometricVotingStarIdAlgorithm : public StarIdAlgorithm {
public:
    StarIdentifiers Go(const unsigned char *database, const Stars &, const Catalog &, const Camera &) const;

    /**
     * @param tolerance Angular tolerance (Two inter-star distances are considered the same if within this many radians)
     */
    explicit GeometricVotingStarIdAlgorithm(decimal tolerance): tolerance(tolerance) { };
private:
    decimal tolerance;
};


/**
 * The "de facto" star-id algorithm used in many real-world missions.
 * Pyramid searches through groups of 4 stars in the image. For each one it tries to find a corresponding 4-star pattern in the database. Four stars is enough that pyramid is often able to uniquely match the first 4-star pattern it tries, making it fast and reliable. However, this only holds true if the camera is calibrated and has low centroid error.
 */
class PyramidStarIdAlgorithm final : public StarIdAlgorithm {
public:
    StarIdentifiers Go(const unsigned char *database, const Stars &, const Catalog &, const Camera &) const;
    /**
     * @param tolerance Angular tolerance (Two inter-star distances are considered the same if within this many radians)
     * @param numFalseStars an estimate of the number of false stars in the whole celestial sphere
     * (not just the field of view). Eg, if you estimate 10 dead pixels in a 40 degree FOV, you'd
     * want to multiply that up to a hundred-something numFalseStars.
     * @param maxMismatchProbability The maximum allowable probability for any star to be mis-id'd.
     * @param cutoff Maximum number of pyramids to iterate through before giving up.
     */
    PyramidStarIdAlgorithm(decimal tolerance, int numFalseStars, decimal maxMismatchProbability, long cutoff)
        : tolerance(tolerance), numFalseStars(numFalseStars),
          maxMismatchProbability(maxMismatchProbability), cutoff(cutoff) { };
private:
    decimal tolerance;
    int numFalseStars;
    decimal maxMismatchProbability;
    long cutoff;
};

/**
 * Utilizes previous information to solve for ID's in constant time.
 */
class TrackingMode final : public StarIdAlgorithm {
public:
    std::vector<std::pair<StarIdentifier, Vec2>> GetProjections(
            const unsigned char * database, 
            const Stars & stars, 
            const Catalog &catalog, 
            const Camera &camera,
            int &outWindowSize) const;

    StarIdentifiers Go(const unsigned char *database, const Stars &stars, const Catalog &catalog, const Camera &camera) const override;
    /**
     * @param tolerance Angular tolerance (Two inter-star distances are considered the same if within this many radians)
     * @param numFalseStars an estimate of the number of false stars in the whole celestial sphere
     * (not just the field of view). Eg, if you estimate 10 dead pixels in a 40 degree FOV, you'd
     * want to multiply that up to a hundred-something numFalseStars.
     * @param maxMismatchProbability The maximum allowable probability for any star to be mis-id'd.
     * @param cutoff Maximum number of pyramids to iterate through before giving up.
     */

     void UpdateTrackingVector(const std::pair<Quaternion, decimal>& newTvec) {
        this->trackingVector = newTvec; 
    }

    void UpdateEKF(const Quaternion& measuredAttitude) {
        ekf.Update(measuredAttitude);
        prevFix = measuredAttitude;  // EXPERIMENT
        havePrevFix = true;
    }

    void ResetEKF(const Quaternion& newGlobalFix) {
        ekf.Reset(newGlobalFix);
        // EXPERIMENT: two fixes one frame apart give the rate directly, so don't restart from w = 0
        if (havePrevFix && trackingVector.second > 0) {
            Quaternion dq = newGlobalFix * prevFix.Conjugate();
            if (dq.real < 0) dq = Quaternion(-dq.real, -dq.i, -dq.j, -dq.k);
            decimal dt = trackingVector.second;
            ekf.w = {2 * dq.i / dt, 2 * dq.j / dt, 2 * dq.k / dt};
        }
        prevFix = newGlobalFix;
        havePrevFix = true;
        isEkfInitialized = true;
        missedFrames = 0;
    }

    // EXPERIMENT: a frame without a fix breaks the one-frame-apart chain
    void NoteMissedFix() { havePrevFix = false; }
    Quaternion prevFix;
    bool havePrevFix = false;

    int missedFrames = 0;



    TrackingMode(decimal tolerance, int numFalseStars, decimal maxMismatchProbability, long cutoff, std::pair<Quaternion, decimal> trackingVector)
        : tolerance(tolerance), numFalseStars(numFalseStars),
          maxMismatchProbability(maxMismatchProbability), cutoff(cutoff),
          trackingVector(trackingVector) { };
private:
    decimal tolerance;
    int numFalseStars;
    decimal maxMismatchProbability;
    long cutoff;
    std::pair<Quaternion, decimal> trackingVector;


    mutable SpatialHash spatialHash; 
    mutable bool isSpatialHashBuilt = false;

    mutable AttitudeEKF ekf;
    mutable bool isEkfInitialized = false;
};

}

#endif

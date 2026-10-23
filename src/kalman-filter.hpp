#ifndef ATTITUDE_EKF_H
#define ATTITUDE_EKF_H

#include <array>
#include "star-utils.hpp" 

namespace lost {

using Vec6 = std::array<decimal, 6>;
using Mat6 = std::array<std::array<decimal, 6>, 6>;

class AttitudeEKF {
public:
    
    Quaternion q;
    Vec3 w;      

    
    Mat6 P; 
    Mat6 Q; 
    std::array<std::array<decimal, 3>, 3> R;

    
    AttitudeEKF();

    
    void Reset(const Quaternion& initial_attitude);

    
    void Predict(decimal dt);

    
    void Update(const Quaternion& q_measured);
};

} 

#endif
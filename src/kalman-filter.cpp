#include "kalman-filter.hpp"
#include <cmath>
#include <iostream>

namespace lost {


AttitudeEKF::AttitudeEKF() {
    q = Quaternion(1.0, 0.0, 0.0, 0.0); 
    w = {0.0, 0.0, 0.0};                


    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            P[i][j] = 0.0;
            Q[i][j] = 0.0;
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R[i][j] = 0.0;
        }
    }


    for (int i = 0; i < 3; ++i) {
        P[i][i] = 1.0;       // High initial attitude uncertainty
        P[i+3][i+3] = 1.0;   // High initial velocity uncertainty
        Q[i][i] = 1e-4;      // Small attitude process noise
        Q[i+3][i+3] = 1e-3;  // Velocity process noise (tumble variation)
        R[i][i] = 1e-6;      // Tracker measurement noise (pixel/lens error)
    }
}


// Reset after a LiS drought. 
void AttitudeEKF::Reset(const Quaternion& initial_attitude) {
    q = initial_attitude;
    w = {0.0, 0.0, 0.0}; // Assume zero velocity until proven otherwise
    
    // Blow up the covariance so the filter trusts the incoming measurements 
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            P[i][j] = (i == j) ? 1.0 : 0.0; 
        }
    }
}


void AttitudeEKF::Predict(decimal dt) {

    Quaternion q_delta(
        1.0, 
        0.5 * w.x * dt, 
        0.5 * w.y * dt, 
        0.5 * w.z * dt
    );

    q = q_delta * q;

    // normalize
    decimal invMag = 1.0 / std::sqrt(q.real*q.real + q.i*q.i + q.j*q.j + q.k*q.k);
    q.real *= invMag;
    q.i *= invMag;
    q.j *= invMag;
    q.k *= invMag;


    Mat6 Phi = {0};
    Phi[0][0] = 1.0;                  Phi[0][1] =  w.z * dt; Phi[0][2] = -w.y * dt;
    Phi[1][0] = -w.z * dt; Phi[1][1] = 1.0;                  Phi[1][2] =  w.x * dt;
    Phi[2][0] =  w.y * dt; Phi[2][1] = -w.x * dt; Phi[2][2] = 1.0;

    Phi[0][3] = dt;  Phi[1][4] = dt;  Phi[2][5] = dt;
    Phi[3][3] = 1.0; Phi[4][4] = 1.0; Phi[5][5] = 1.0;


    Mat6 Temp = {0};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            for (int k = 0; k < 6; ++k) {
                if (Phi[i][k] != 0.0) {
                    Temp[i][j] += Phi[i][k] * P[k][j];
                }
            }
        }
    }


    Mat6 P_new = {0};
    for (int i = 0; i < 6; ++i) {
        for (int j = i; j < 6; ++j) {
            decimal sum = 0;
            for (int k = 0; k < 6; ++k) {
                if (Phi[j][k] != 0.0) { 
                    // Phi[j][k] acts as Phi^T[k][j]
                    sum += Temp[i][k] * Phi[j][k]; 
                }
            }
            
            P_new[i][j] = sum + Q[i][j];
            
            
            if (i != j) {
                P_new[j][i] = P_new[i][j];
            }
        }
    }
    
    
    P = P_new;
}


void AttitudeEKF::Update(const Quaternion& q_measured) {

    Quaternion q_error = q_measured * q.Conjugate();


    if (q_error.real < 0.0) {
        q_error.real = -q_error.real;
        q_error.i = -q_error.i;
        q_error.j = -q_error.j;
        q_error.k = -q_error.k;
    }


    std::array<decimal, 3> y = {
        2.0 * q_error.i,
        2.0 * q_error.j,
        2.0 * q_error.k
    };

    std::array<std::array<decimal, 3>, 3> S;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            S[i][j] = P[i][j] + R[i][j];
        }
    }


    decimal det = S[0][0] * (S[1][1] * S[2][2] - S[2][1] * S[1][2]) -
                  S[0][1] * (S[1][0] * S[2][2] - S[1][2] * S[2][0]) +
                  S[0][2] * (S[1][0] * S[2][1] - S[1][1] * S[2][0]);

    if (std::abs(det) < 1e-9) return;

    decimal invDet = 1.0 / det;
    std::array<std::array<decimal, 3>, 3> Sinv;
    Sinv[0][0] = (S[1][1] * S[2][2] - S[2][1] * S[1][2]) * invDet;
    Sinv[0][1] = (S[0][2] * S[2][1] - S[0][1] * S[2][2]) * invDet;
    Sinv[0][2] = (S[0][1] * S[1][2] - S[0][2] * S[1][1]) * invDet;
    Sinv[1][0] = (S[1][2] * S[2][0] - S[1][0] * S[2][2]) * invDet;
    Sinv[1][1] = (S[0][0] * S[2][2] - S[0][2] * S[2][0]) * invDet;
    Sinv[1][2] = (S[1][0] * S[0][2] - S[0][0] * S[1][2]) * invDet;
    Sinv[2][0] = (S[1][0] * S[2][1] - S[2][0] * S[1][1]) * invDet;
    Sinv[2][1] = (S[2][0] * S[0][1] - S[0][0] * S[2][1]) * invDet;
    Sinv[2][2] = (S[0][0] * S[1][1] - S[1][0] * S[0][1]) * invDet;


    std::array<std::array<decimal, 3>, 6> K = {0};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                K[i][j] += P[i][k] * Sinv[k][j];
            }
        }
    }


    std::array<decimal, 6> dx = {0};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 3; ++j) {
            dx[i] += K[i][j] * y[j];
        }
    }


    w.x += dx[3];
    w.y += dx[4];
    w.z += dx[5];

    Quaternion q_correction(
        1.0, 
        0.5 * dx[0], 
        0.5 * dx[1], 
        0.5 * dx[2]
    );
    
    q = q_correction * q;
    
    // normalize
    decimal invMag = 1.0 / std::sqrt(q.real*q.real + q.i*q.i + q.j*q.j + q.k*q.k);
    q.real *= invMag;
    q.i *= invMag;
    q.j *= invMag;
    q.k *= invMag;


    Mat6 P_new = {0};

    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            

            decimal updated_value = P[i][j]; 

            updated_value -= (K[i][0] * P[0][j] + 
                              K[i][1] * P[1][j] + 
                              K[i][2] * P[2][j]);
            
            P_new[i][j] = updated_value;
        }
    }

    for (int i = 0; i < 6; ++i) {
        for (int j = i; j < 6; ++j) {
            P[i][j] = (P_new[i][j] + P_new[j][i]) * 0.5;
            P[j][i] = P[i][j];
        }
    }

    //std::cout << "P[0][0] after update: " << P[0][0] << std::endl;
}

} 

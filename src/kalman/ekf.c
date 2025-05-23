#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>


typedef struct {
    float state[10]; // State: [px, py, pz, vx, vy, vz, qx, qy, qz, qw]
    float covar[10][10];
    float proc_noise[10][10];
    float measure_noise[7][7];
} EKF;

bool invert(float A[7][7], float A_inv[7][7]) {
    float L[7][7] = {0};
    float LT_inv[7][7] = {0};
    float L_inv[7][7] = {0};

    // Cholesky decomposition: A = L * L^T
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j <= i; j++) {
            float sum = A[i][j];
            for (int k = 0; k < j; k++) sum -= L[i][k] * L[j][k];

            if (i == j) {
                if (sum <= 0.0f) return false; // Not positive definite
                L[i][j] = sqrtf(sum);
            } else {
                L[i][j] = sum / L[j][j];
            }
        }
    }

    // Invert L
    for (int i = 0; i < 7; i++) {
        L_inv[i][i] = 1.0f / L[i][i];
        for (int j = i + 1; j < 7; j++) {
            float sum = 0.0f;
            for (int k = i; k < j; k++) sum -= L[j][k] * L_inv[k][i];
            L_inv[j][i] = sum / L[j][j];
        }
    }

    // Compute A_inv = (L^T)^-1 * L^-1
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j <= i; j++) {
            float sum = 0.0f;
            for (int k = i; k < 7; k++) sum += L_inv[k][i] * L_inv[k][j];
            A_inv[i][j] = A_inv[j][i] = sum;
        }
    }

    return true;
}


void normalize_quaternion(float q[4]) {
    float magnitude = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) {
        q[i] /= magnitude;
    }
}

// gx = gyro x, ax = accel x, ax_mg = accel x in mg
// dt = time; dt = .01 is 100hz imu update.
void ekf_predict(EKF *ekf, float dt, float gx, float gy, float gz, float ax_mg, float ay_mg, float az_mg) {
    float ax = ax_mg * 9.8066f / 1000.0f;
    float ay = ay_mg * 9.8066f / 1000.0f;
    float az = az_mg * 9.8066f / 1000.0f;

    float qx = ekf->state[6];
    float qy = ekf->state[7];
    float qz = ekf->state[8];
    float qw = ekf->state[9];

    float dq[4] = {
        0.5f * (-gx * qx - gy * qy - gz * qz),
        0.5f * (gx * qw + gy * qz - gz * qy),
        0.5f * (-gx * qz + gy * qw + gz * qx),
        0.5f * (gx * qy - gy * qx + gz * qw),
    };

    qx += dq[0] * dt;
    qy += dq[1] * dt;
    qz += dq[2] * dt;
    qw += dq[3] * dt;

    float q[4] = { qx, qy, qz, qw };
    normalize_quaternion(q);

    ekf->state[6] = q[0];
    ekf->state[7] = q[1];
    ekf->state[8] = q[2];
    ekf->state[9] = q[3];

    float R[3][3] = {
        {1 - 2*q[1]*q[1] - 2*q[2]*q[2], 2*q[0]*q[1] - 2*q[2]*q[3], 2*q[0]*q[2] + 2*q[1]*q[3]},
        {2*q[0]*q[1] + 2*q[2]*q[3], 1 - 2*q[0]*q[0] - 2*q[2]*q[2], 2*q[1]*q[2] - 2*q[0]*q[3]},
        {2*q[0]*q[2] - 2*q[1]*q[3], 2*q[1]*q[2] + 2*q[0]*q[3], 1 - 2*q[0]*q[0] - 2*q[1]*q[1]}
    };
    
    // a*_w is acceleration in the world frame
    float ax_w = R[0][0]*ax + R[0][1]*ay + R[0][2]*az;
    float ay_w = R[1][0]*ax + R[1][1]*ay + R[1][2]*az;
    float az_w = R[2][0]*ax + R[2][1]*ay + R[2][2]*az + 9.80665f;

    ekf->state[3] += ax_w * dt; // vx
    ekf->state[4] += ay_w * dt; // vy
    ekf->state[5] += az_w * dt; // vz

    ekf->state[0] += ekf->state[3] * dt; // px
    ekf->state[1] += ekf->state[4] * dt; // py
    ekf->state[2] += ekf->state[5] * dt; // pz
}

void ekf_update(EKF *ekf, float dt, float pressure, float ax_mg, float ay_mg, float az_mg, float gx, float gy, float gz) {
    float measure[7];
    float measure_predicted[7];
    float residual[7];
    float jacobian[7][10] = {0};
    
// Fill in partial derivatives of predicted accel wrt quaternion
float g = 9.80665f;
    float ax = ax_mg * 9.8066f / 1000.0f;
    float ay = ay_mg * 9.8066f / 1000.0f;
    float az = az_mg * 9.8066f / 1000.0f;

    float pressure_sea = 101325.0f;
    float altitude = 44330.0f * (1.0f - powf(pressure / pressure_sea, 0.1903));

    float qx = ekf->state[6];
    float qy = ekf->state[7];
    float qz = ekf->state[8];
    float qw = ekf->state[9];

    float gx_predicted = gx;
    float gy_predicted = gy;
    float gz_predicted = gz;
    
    float R[3][3] = {
        {1 - 2 * qy * qy - 2 * qz * qz, 2 * qx * qy - 2 * qz * qw, 2 *qx*qz + 2 * qy * qw},
        {2 * qx * qy + 2 * qz * qw, 1 - 2 * qx * qx - 2 * qz * qz, 2 * qy * qz - 2 * qx * qw},
        {2 * qx * qz - 2 * qy * qw, 2 * qy * qz + 2 * qx * qw, 1 - 2 * qx * qx - 2 * qy * qy}
    };

    float ax_w = ekf->state[3] / dt;
    float ay_w = ekf->state[4] / dt;
    float az_w = ekf->state[5] / dt + 9.80665f;

    //body frame
    float ax_b = R[0][0] * ax_w + R[1][0] * ay_w + R[2][0] * az_w;
    float ay_b = R[0][1] * ax_w + R[1][1] * ay_w + R[2][1] * az_w;
    float az_b = R[0][2] * ax_w + R[1][2] * ay_w + R[2][2] * az_w;

    jacobian[0][2] = 1.0f;
    
    jacobian[1][6] =  2 * qz * g;      // ∂ax/∂qx
    jacobian[1][7] = -2 * qw * g;      // ∂ax/∂qy
    jacobian[1][8] =  2 * qx * g;      // ∂ax/∂qz
    jacobian[1][9] = -2 * qy * g;      // ∂ax/∂qw

    jacobian[2][6] =  2 * qw * g;      // ∂ay/∂qx
    jacobian[2][7] =  2 * qz * g;      // ∂ay/∂qy
    jacobian[2][8] =  2 * qy * g;      // ∂ay/∂qz
    jacobian[2][9] =  2 * qx * g;      // ∂ay/∂qw

    jacobian[3][6] = -4 * qx * g;      // ∂az/∂qx
    jacobian[3][7] = -4 * qy * g;      // ∂az/∂qy
    
    measure[0] = altitude;
    measure[1] = ax;
    measure[2] = ay;
    measure[3] = az;
    measure[4] = gx;
    measure[5] = gy;
    measure[6] = gz;

    measure_predicted[0] = ekf->state[2];
    measure_predicted[1] = ax_b;
    measure_predicted[2] = ay_b;
    measure_predicted[3] = az_b;
    measure_predicted[4] = gx;
    measure_predicted[5] = gy;
    measure_predicted[6] = gz;

    for (int i = 0; i < 7; i++) {
        residual[i] = measure[i] - measure_predicted[i];
    }

    // P = covariance * Ht = jacobian transposed
    float PHt[10][7] = {0};
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 10; k++) {
                PHt[i][j] += ekf->covar[i][k] * jacobian[j][k];
            }
        }
    }
    // Innovation covariance
    // S = Jacobian * covar * jacobian transposed  + mesasurement noise
    // S = HPHt + R
    float S[7][7] = {0};
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 10; k++) {
                S[i][j] += jacobian[i][k] * PHt[k][j];
            }
            S[i][j] += ekf->measure_noise[i][j];
        }
    }

    float S_inverse[7][7] = {0};
    if (!invert(S, S_inverse)) {
        return;
    }
    
    // Kalman gain
    float K[10][7] = {0};
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 7; j++) {
            for (int k = 0; k < 7; k++) {
                K[i][j] += PHt[i][k] * S_inverse[k][j];
            }
        }
    }

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 7; j++) {
            ekf->state[i] += K[i][j] * residual[j];
        }
    }

    // Kalman gain * jacobian
    float KH[10][10] = {0};
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            for (int k = 0; k < 7; k++) {
                KH[i][j] += K[i][k] * jacobian[k][j];
            }
        }
    }


    float covar_new[10][10] = {0};
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 10; j++) {
            float delta = (i == j) ? 1.0f : 0.0f;
            for (int k = 0; k < 10; k++)
                covar_new[i][j] += (delta - KH[i][k]) * ekf->covar[k][j];
        }

    memcpy(ekf->covar, covar_new, sizeof(covar_new));
}

//
//EKF ekf = {0};
//ekf.state[6] = 0; // qx
//ekf.state[7] = 0; // qy
//ekf.state[8] = 0; // qz
//ekf.state[9] = 1; // qw

// for (int i = 0; i < 10; i++) {
//     ekf.covar[i][i] 1.0f;
//     ekf.proc_noise[i] = 0.1f;
// }
//
// for (int i = 0; i < 7; i++) {
//     ekf.measure_noise[i][i] = 1.0f;
// }



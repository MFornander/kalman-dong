#include <cmath>

#include "orientation_filter.h"

namespace kd {

namespace {


// out = [v]x, the matrix with [v]x * w == v.cross(w)
void skew(const Vec3& v, float out[3][3]) {
    out[0][0] = 0;    out[0][1] = -v.z; out[0][2] = v.y;
    out[1][0] = v.z;  out[1][1] = 0;    out[1][2] = -v.x;
    out[2][0] = -v.y; out[2][1] = v.x;  out[2][2] = 0;
}

bool invert3(const float m[3][3], float out[3][3]) {
    float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    if (std::fabs(det) < 1e-20f) return false;
    float inv = 1.f / det;
    out[0][0] = c00 * inv;
    out[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv;
    out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv;
    out[1][0] = c01 * inv;
    out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv;
    out[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv;
    out[2][0] = c02 * inv;
    out[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv;
    out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv;
    return true;
}

}  // namespace

void KalmanFilter::reset(const Quat& orientation) {
    q_ = orientation.normalized();
    bias_ = {};
    for (auto& row : P_)
        for (float& v : row) v = 0.f;
    for (int i = 0; i < 3; ++i) {
        P_[i][i] = noise_.initialTilt * noise_.initialTilt;
        P_[i + 3][i + 3] = noise_.initialBias * noise_.initialBias;
    }
}

void KalmanFilter::update(const Vec3& gyro, const Vec3& accel, float dt) {
    predict(gyro - bias_, dt);
    correct(accel);
}

// Error dynamics, orientation error in the body frame:
//   d(theta)/dt = -[w]x theta - d(bias),   d(bias)/dt = noise
void KalmanFilter::predict(const Vec3& rate, float dt) {
    q_ = (q_ * Quat::fromRotationVector(rate * dt)).normalized();

    float F[6][6] = {};
    float w[3][3];
    skew(rate, w);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) F[i][j] = (i == j ? 1.f : 0.f) - w[i][j] * dt;
        F[i][i + 3] = -dt;
        F[i + 3][i + 3] = 1.f;
    }

    // P = F P F^T + Q
    float FP[6][6];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) {
            float s = 0.f;
            for (int k = 0; k < 6; ++k) s += F[i][k] * P_[k][j];
            FP[i][j] = s;
        }
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) {
            float s = 0.f;
            for (int k = 0; k < 6; ++k) s += FP[i][k] * F[j][k];
            P_[i][j] = s;
        }

    float scale = noise_.scaleError * rate.norm() * dt;
    float qTheta = noise_.angleWalk * noise_.angleWalk * dt + scale * scale;
    float qBias = noise_.biasWalk * noise_.biasWalk * dt;
    for (int i = 0; i < 3; ++i) {
        P_[i][i] += qTheta;
        P_[i + 3][i + 3] += qBias;
    }
}

// Measurement: the accelerometer's direction, which at rest is "up" in the
// body frame. Predicted h = q^-1 * up; for a small body-frame error theta,
// h(theta) ~= h + [h]x theta, so H = [ [h]x  0 ].
void KalmanFilter::correct(const Vec3& accel) {
    float aNorm = accel.norm();
    float offG = std::fabs(aNorm - kGravity) / kGravity;
    if (aNorm < 1e-3f || offG > noise_.rejectBeyond) return;

    Vec3 h = q_.conj().rotate(Vec3{0, 0, 1});
    Vec3 z = accel * (1.f / aNorm);
    Vec3 y = z - h;

    float Hs[3][3];  // H's nonzero 3x3 block; the bias block is zero
    skew(h, Hs);

    // PHt = P H^T (6x3), S = H P H^T + R (3x3)
    float PHt[6][3];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0.f;
            for (int k = 0; k < 3; ++k) s += P_[i][k] * Hs[j][k];
            PHt[i][j] = s;
        }
    float sigma = noise_.accelDirection + noise_.swingDistrust * offG;
    float S[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0.f;
            for (int k = 0; k < 3; ++k) s += Hs[i][k] * PHt[k][j];
            S[i][j] = s + (i == j ? sigma * sigma : 0.f);
        }
    float Sinv[3][3];
    if (!invert3(S, Sinv)) return;

    const float yv[3] = {y.x, y.y, y.z};
    float mahalanobis = 0.f;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) mahalanobis += yv[i] * Sinv[i][j] * yv[j];
    if (mahalanobis > noise_.innovationGate) return;

    float K[6][3];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0.f;
            for (int k = 0; k < 3; ++k) s += PHt[i][k] * Sinv[k][j];
            K[i][j] = s;
        }

    float dx[6];
    for (int i = 0; i < 6; ++i) dx[i] = K[i][0] * yv[0] + K[i][1] * yv[1] + K[i][2] * yv[2];
    q_ = (q_ * Quat::fromRotationVector({dx[0], dx[1], dx[2]})).normalized();
    bias_ += Vec3{dx[3], dx[4], dx[5]};

    // P = (I - K H) P, with K H only touching the orientation columns:
    // (K H)[i][j] = sum_k K[i][k] Hs[k][j] for j < 3.
    float KH[6][3];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 3; ++j) KH[i][j] = K[i][0] * Hs[0][j] + K[i][1] * Hs[1][j] + K[i][2] * Hs[2][j];
    float newP[6][6];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) {
            float s = P_[i][j];
            for (int k = 0; k < 3; ++k) s -= KH[i][k] * P_[k][j];
            newP[i][j] = s;
        }
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) P_[i][j] = 0.5f * (newP[i][j] + newP[j][i]);
}

}  // namespace kd

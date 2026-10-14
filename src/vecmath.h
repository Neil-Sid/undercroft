#pragma once
#include <cmath>

constexpr float PI = 3.14159265f;

struct Vec2 { float x = 0, y = 0; };
struct Vec4 { float x = 0, y = 0, z = 0, w = 0; };

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(Vec3 b) const { return {x * b.x, y * b.y, z * b.z}; }
    Vec3& operator+=(Vec3 b) { return *this = *this + b; }
};

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) { float l = length(a); return l > 0 ? a * (1.0f / l) : a; }
inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
inline float smooth(float t) { t = clamp01(t); return t * t * (3 - 2 * t); }

// Shortest signed angle from a to b, in (-PI, PI].
inline float angleDelta(float a, float b) {
    float d = std::fmod(b - a, 2 * PI);
    if (d > PI) d -= 2 * PI;
    if (d <= -PI) d += 2 * PI;
    return d;
}

// Column-major 4x4 matrix, the layout OpenGL expects.
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += at(i, k) * b.at(k, j);
                r.at(i, j) = s;
            }
        return r;
    }
    Vec4 operator*(Vec4 v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
                at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
    }
    Vec3 point(Vec3 p) const {
        Vec4 r = *this * Vec4{p.x, p.y, p.z, 1};
        return {r.x, r.y, r.z};
    }
};

inline Mat4 translate(Vec3 t) {
    Mat4 r;
    r.m[12] = t.x, r.m[13] = t.y, r.m[14] = t.z;
    return r;
}

inline Mat4 scale(Vec3 s) {
    Mat4 r;
    r.m[0] = s.x, r.m[5] = s.y, r.m[10] = s.z;
    return r;
}

inline Mat4 rotateX(float a) {
    Mat4 r;
    float c = std::cos(a), s = std::sin(a);
    r.at(1, 1) = c, r.at(1, 2) = -s, r.at(2, 1) = s, r.at(2, 2) = c;
    return r;
}

inline Mat4 rotateY(float a) {
    Mat4 r;
    float c = std::cos(a), s = std::sin(a);
    r.at(0, 0) = c, r.at(0, 2) = s, r.at(2, 0) = -s, r.at(2, 2) = c;
    return r;
}

inline Mat4 rotateZ(float a) {
    Mat4 r;
    float c = std::cos(a), s = std::sin(a);
    r.at(0, 0) = c, r.at(0, 1) = -s, r.at(1, 0) = s, r.at(1, 1) = c;
    return r;
}

// Rotation in Z, then X, then Y order: roll, pitch, yaw.
inline Mat4 rotateXYZ(Vec3 r) { return rotateY(r.y) * rotateX(r.x) * rotateZ(r.z); }

inline Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    Mat4 r;
    float f = 1 / std::tan(fovY / 2);
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
    r.at(2, 3) = 2 * zFar * zNear / (zNear - zFar);
    r.at(3, 2) = -1;
    r.at(3, 3) = 0;
    return r;
}

inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = normalize(target - eye), s = normalize(cross(f, up)), u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x, r.at(0, 1) = s.y, r.at(0, 2) = s.z, r.at(0, 3) = -dot(s, eye);
    r.at(1, 0) = u.x, r.at(1, 1) = u.y, r.at(1, 2) = u.z, r.at(1, 3) = -dot(u, eye);
    r.at(2, 0) = -f.x, r.at(2, 1) = -f.y, r.at(2, 2) = -f.z, r.at(2, 3) = dot(f, eye);
    return r;
}

// Inverse of lookAt: camera space (x right, y up, -z forward) to world.
inline Mat4 cameraToWorld(Vec3 eye, Vec3 forward, Vec3 up) {
    Vec3 f = normalize(forward), s = normalize(cross(f, up)), u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x, r.at(1, 0) = s.y, r.at(2, 0) = s.z;
    r.at(0, 1) = u.x, r.at(1, 1) = u.y, r.at(2, 1) = u.z;
    r.at(0, 2) = -f.x, r.at(1, 2) = -f.y, r.at(2, 2) = -f.z;
    r.at(0, 3) = eye.x, r.at(1, 3) = eye.y, r.at(2, 3) = eye.z;
    return r;
}

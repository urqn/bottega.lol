#pragma once

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s)        const { return { x * s, y * s, z * s }; }
    float dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
};

struct Mat4 {
    float m[16];
};

#pragma once

#include <vector>

struct NVGcontext;

namespace morph::ui {

struct Particle {
    float x, y;
    float vx, vy;
    float radius;
    float alpha;
    float phase;
};

class ParticleSystem {
public:
    ParticleSystem();

    void Init(float screen_w, float screen_h, int count = 30);
    void Update(float dt, float gyro_x = 0.f, float gyro_y = 0.f);
    void Draw(NVGcontext* vg);

private:
    std::vector<Particle> m_particles;
    float m_screen_w{1280.f};
    float m_screen_h{720.f};
    float m_time{};
};

} // namespace morph::ui

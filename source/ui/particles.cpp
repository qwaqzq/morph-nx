#include "ui/particles.hpp"
#include <nanovg.h>
#include <cstdlib>
#include <cmath>

namespace morph::ui {

ParticleSystem::ParticleSystem() {}

void ParticleSystem::Init(float screen_w, float screen_h, int count) {
    m_screen_w = screen_w;
    m_screen_h = screen_h;
    m_particles.resize(count);

    for (int i = 0; i < count; i++) {
        auto& p = m_particles[i];
        p.x = (float)(std::rand() % (int)screen_w);
        p.y = (float)(std::rand() % (int)screen_h);
        p.vx = 0.f;
        p.vy = 0.f;
        p.radius = 5.f + (float)(std::rand() % 25);
        p.alpha = 0.06f + (float)(std::rand() % 10) / 100.f;
        p.phase = (float)(std::rand() % 628) / 100.f;
    }
}

void ParticleSystem::Update(float dt, float gyro_x, float gyro_y) {
    m_time += dt;

    for (auto& p : m_particles) {
        float speed = 1.0f - (p.radius / 20.f) * 0.5f;

        p.x += std::sin(p.phase + m_time * 0.2f * speed) * 0.2f;
        p.y += std::cos(p.phase * 1.3f + m_time * 0.15f * speed) * 0.15f;

        float sens = (1.0f - p.radius / 20.f) * 3.0f + 0.5f;
        p.vx += gyro_y * sens;
        p.vy += gyro_x * sens;

        p.vx *= 0.93f;
        p.vy *= 0.93f;

        p.x += p.vx * dt * 60.f;
        p.y += p.vy * dt * 60.f;

        float pad = p.radius * 2.f;
        if (p.x < -pad) p.x = m_screen_w + p.radius;
        if (p.x > m_screen_w + pad) p.x = -p.radius;
        if (p.y < -pad) p.y = m_screen_h + p.radius;
        if (p.y > m_screen_h + pad) p.y = -p.radius;
    }
}

void ParticleSystem::Draw(NVGcontext* vg) {
    for (const auto& p : m_particles) {
        auto paint = nvgRadialGradient(vg,
            p.x, p.y,
            p.radius * 0.5f,   // solid core
            p.radius,           // soft edge
            nvgRGBA(255, 255, 255, (unsigned char)(p.alpha * 255.f)),
            nvgRGBA(255, 255, 255, (unsigned char)(p.alpha * 40.f)));

        nvgBeginPath(vg);
        nvgCircle(vg, p.x, p.y, p.radius);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }
}

} // namespace morph::ui

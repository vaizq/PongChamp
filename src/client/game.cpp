#include "game.h"
#include <chrono>
#include <cstring>
#include <raylib.h>
#include "util.h"
#include <format>
#include "protocol.h"

using namespace std::chrono_literals;


int renderWidth = 1280;
int renderHeight = 960;
float viewHeight = 50.f;



float aspectRatio() {
    return 1.0f * renderWidth / renderHeight;
}

rl::Vector2 screenCenter() {
    return rl::Vector2{renderWidth/2.f, renderHeight/2.f};
}

float hpx() {
    return renderHeight / viewHeight;
}

rl::Vector2 Game::worldPosToScreenCoord(rl::Vector2 pos) {
    const rl::Vector2 d = pos - player.pos;
    return screenCenter() + hpx() * d;
}

rl::Vector2 Game::screenCoordToWorldPos(rl::Vector2 coord) {
    const auto d = coord - screenCenter();
    return player.pos + d / hpx();
}

Game::Game(const char* serverAddr)
    : con{udp::endpoint{asio::ip::make_address(serverAddr), 6969}}
{
    con.listen(proto::updateChannel, [this](char* data, size_t n) {
        proto::Header h;
        proto::GameState state;

        if (n != sizeof h + sizeof state) {
            fprintf(stderr, "ERROR\t invalid datalen\n");
            return;
        }

        std::memcpy(&h, data, sizeof h);
        if (player.id != h.playerId) {
            printf("INFO\t different player id (my: %d header: %d)\n", player.id, h.playerId);
            player.id = h.playerId;
        }

        if (h.payloadSize != sizeof state) {
            fprintf(stderr, "ERROR\t, invalid payloadsize\n");
            return;
        }

        std::memcpy(&state, data + sizeof h, sizeof state);

        enemies.clear();
        for (int i = 0; i < state.numPlayers; ++i) {
            proto::Player& p = state.players[i];
            if (p.id == player.id) {
                player.pos = p.pos;
                player.velo = p.velo;
                player.stats = p.stats;
            } else {
                enemies.push_back(p);
            }
        }

        bullets.clear();
        for (int i = 0; i < state.numBullets; ++i) {
            bullets.push_back(state.bullets[i]);
        }
    });
}

void Game::init() {
    rl::InitWindow(renderWidth, renderHeight, "Verilandia");
    rl::SetTargetFPS(144);
    rl::HideCursor();

    prevUpdate = Clock::now();
    player.pos.x = 0;
    player.pos.y = 0;
    player.velo.x = 0;
    player.velo.y = 0;
}

void Game::update() {
    renderWidth = rl::GetRenderWidth();
    renderHeight = rl::GetRenderHeight();

    const auto now = Clock::now();
    const float dt = std::chrono::duration_cast<std::chrono::duration<float>>(now - prevUpdate).count();
    prevUpdate = now;

    const auto prevVelo = player.velo;

    if (rl::IsKeyPressed(rl::KEY_A)) {
        player.velo.x = -proto::playerSpeed;
    } else if (rl::IsKeyReleased(rl::KEY_A)) {
        if (rl::IsKeyDown(rl::KEY_D)) {
            player.velo.x = proto::playerSpeed;
        } else {
            player.velo.x = 0;
        }
    }

    if (rl::IsKeyPressed(rl::KEY_D)) {
        player.velo.x = proto::playerSpeed;
    } else if (rl::IsKeyReleased(rl::KEY_D)) {
        if (rl::IsKeyDown(rl::KEY_A)) {
            player.velo.x = -proto::playerSpeed;
        } else {
            player.velo.x = 0;
        }

    }

    if (rl::IsKeyPressed(rl::KEY_W)) {
        player.velo.y = -proto::playerSpeed;
    } else if (rl::IsKeyReleased(rl::KEY_W)) {
        if (rl::IsKeyDown(rl::KEY_S)) {
            player.velo.y = proto::playerSpeed;
        } else {
            player.velo.y = 0;
        }

    }

    if (rl::IsKeyPressed(rl::KEY_S)) {
        player.velo.y = proto::playerSpeed;
    } else if (rl::IsKeyReleased(rl::KEY_S)) {
        if (rl::IsKeyDown(rl::KEY_W)) {
            player.velo.y = -proto::playerSpeed;
        } else {
            player.velo.y = 0;
        }
    }

    if (rl::IsKeyDown(rl::KEY_TAB)) {
        viewStats = true;
    } else {
        viewStats = false;
    }

    if (rl::IsMouseButtonPressed(rl::MOUSE_BUTTON_LEFT)) {
        const auto target = rl::GetMousePosition();
        const auto direction = {target - player.pos};
        eventShoot();
    }

    if (player.velo.x != prevVelo.x || player.velo.y != prevVelo.y) {
        eventMove();
    }

    viewHeight += 5.f * rl::GetMouseWheelMove();

    player.pos = player.pos + dt * player.velo;

    for (auto& enemy : enemies) {
        enemy.pos = enemy.pos + dt * enemy.velo;
    }

    for (auto& bullet : bullets) {
        bullet.pos = bullet.pos + dt * bullet.velo;
    }

    con.poll();
}

void Game::render() {
    rl::BeginDrawing();
    rl::ClearBackground(rl::BLACK);

    {
        const auto pos = screenCenter();
        const float r = proto::playerRadius * hpx();
        rl::DrawCircleV(screenCenter(), r, rl::WHITE);
        rl::DrawText(std::format("({:.1f}, {:.1f})", player.pos.x, player.pos.y).c_str(), pos.x + r, pos.y - r, 12, rl::WHITE);
    }

    for(auto& enemy : enemies) {
        const auto pos = worldPosToScreenCoord(enemy.pos);
        const float r = proto::enemyRadius * hpx();
        rl::DrawCircleV(pos, r, rl::GRAY);
        rl::DrawText(std::format("({:.1f}, {:.1f})", enemy.pos.x, enemy.pos.y).c_str(), pos.x + r, pos.y - r, 12, rl::WHITE);
    }

    {
        const auto pos = rl::GetMousePosition();
        const auto wpos = screenCoordToWorldPos(pos);
        player.target = wpos;
        const float w = 20.0f;
        rl::DrawLine(pos.x - w/2, pos.y, pos.x + w/2, pos.y, rl::GREEN);
        rl::DrawLine(pos.x, pos.y - w/2, pos.x, pos.y + w/2, rl::GREEN);
        rl::DrawText(std::format("({:.1f}, {:.1f})", wpos.x, wpos.y).c_str(), pos.x + w, pos.y - w, 12, rl::WHITE);
    }

    {
        for(auto& bullet : bullets) {
            const auto pos = worldPosToScreenCoord(bullet.pos);
            const float r = proto::bulletRadius * hpx();
            rl::DrawCircleV(pos, r, rl::GRAY);
            rl::DrawText(std::format("{}", bullet.shooterID).c_str(), pos.x + r, pos.y - r, 12, rl::WHITE);
        }
    }

    {
        const float ping = std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(con.getPing()).count();
        rl::DrawText(std::format("ping {:.3f}ms", ping).c_str(), 10, 10, 16, rl::WHITE);
    }

    if (viewStats) {
        std::vector<proto::Player> players{enemies.begin(), enemies.end()};
        players.push_back(player);
        std::sort(players.begin(), players.end(), [](const proto::Player& a, const proto::Player& b) {
            return (a.stats.kills - a.stats.deaths) > (b.stats.kills - b.stats.kills);
        });
        std::stringstream ss;
        for (const auto& p : players) {
                ss << std::format("ID: {:<6}\tKILLS: {:<6}\tDEATHS: {:<6}\tK/D: {:<6.1f}",
                              p.id, p.stats.kills, p.stats.deaths, 1.0f * p.stats.kills / p.stats.deaths);

            if (p.id == player.id) {
                ss << " <--- YOU";
            }

            ss << '\n';
        }

        rl::DrawText(ss.str().c_str(), 10, 50, 18, rl::GREEN);
    }

    rl::EndDrawing();
}

void Game::run() {

    init();

    while (!rl::WindowShouldClose()) {
        update();
        render();
    }

    rl::CloseWindow();
}

void Game::eventMove() {
    proto::Move move{player.velo};
    proto::Header h{player.id, sizeof move};
    auto [bufOut, n] = proto::makeMessage(h, &move);

    printf("send move event with id %d\n", player.id);
    con.write(proto::moveChannel, bufOut, n, [bufOut](auto, auto) {
        delete[] bufOut;
    });
}

void Game::eventShoot() {
    
    auto diff = (player.target - player.pos); 
    if (diff.x == 0 && diff.y == 0) {
        fprintf(stderr, "ERROR\t can't shoot yourself\n");
        return;
    }

    diff = (diff / sqrt(diff.x*diff.x + diff.y*diff.y));

    const proto::Bullet bullet(
        player.pos + proto::playerRadius * diff, 
        proto::bulletSpeed * diff,
        player.id
    );

    bullets.push_back(bullet);

    proto::Shoot shoot{bullet};
    auto [bufOut, n] = proto::makeMessage(
        {player.id, sizeof shoot}, 
        &shoot
    );

    con.write(proto::shootChannel, bufOut, n, [bufOut](auto, auto) {
        delete[] bufOut;
    });
}

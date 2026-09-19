#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

// ============================================================
// ForgeUI MicroAsteroids
// ESP32-S3 + ST7789 240x240 + Analog Joystick
// ============================================================

// -------------------- Display -------------------------------

#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC    9
#define TFT_RST   10
#define TFT_CS    8

#define SCREEN_W 240
#define SCREEN_H 240

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC,
    TFT_CS,
    TFT_SCLK,
    TFT_MOSI,
    GFX_NOT_DEFINED,
    HSPI
);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus,
    TFT_RST,
    0,
    true,
    SCREEN_W,
    SCREEN_H
);

// -------------------- Joystick ------------------------------

constexpr int JOY_X  = 6;
constexpr int JOY_Y  = 5;
constexpr int JOY_SW = 4;

int joyCentreX = 2048;
int joyCentreY = 2048;

// -------------------- Colours -------------------------------

constexpr uint16_t C_BLACK   = 0x0000;
constexpr uint16_t C_WHITE   = 0xFFFF;
constexpr uint16_t C_CYAN    = 0x07FF;
constexpr uint16_t C_BLUE    = 0x001F;
constexpr uint16_t C_GREEN   = 0x07E0;
constexpr uint16_t C_YELLOW  = 0xFFE0;
constexpr uint16_t C_RED     = 0xF800;
constexpr uint16_t C_MAGENTA = 0xF81F;
constexpr uint16_t C_GREY    = 0x8410;
constexpr uint16_t C_DKGREY  = 0x3186;

// -------------------- Framebuffer ---------------------------

// 240 x 240 RGB565 = 115,200 bytes.
// ESP32-S3 has ample memory for this project.

uint16_t *frameBuffer = nullptr;

Arduino_Canvas *canvas = nullptr;

// -------------------- Game state ----------------------------

enum GameState
{
    TITLE,
    PLAYING,
    PLAYER_EXPLODING,
    GAME_OVER
};

GameState gameState = TITLE;

uint32_t score = 0;
uint32_t highScore = 0;

int lives = 3;
int wave = 1;

unsigned long lastFrame = 0;
unsigned long stateStart = 0;

// -------------------- Ship ----------------------------------

struct Ship
{
    float x;
    float y;

    float vx;
    float vy;

    float angle;

    bool thrusting;
};

Ship ship;

// -------------------- Bullets -------------------------------

struct Bullet
{
    float x;
    float y;

    float vx;
    float vy;

    int life;

    bool active;
};

constexpr int MAX_BULLETS = 8;
Bullet bullets[MAX_BULLETS];

unsigned long lastShot = 0;

// -------------------- Asteroids -----------------------------

enum AsteroidSize
{
    AST_LARGE = 3,
    AST_MEDIUM = 2,
    AST_SMALL = 1
};

struct Asteroid
{
    float x;
    float y;

    float vx;
    float vy;

    float rotation;
    float rotationSpeed;

    int size;

    bool active;

    uint8_t shape;
};

constexpr int MAX_ASTEROIDS = 18;
Asteroid asteroids[MAX_ASTEROIDS];

// -------------------- Particles -----------------------------

struct Particle
{
    float x;
    float y;

    float vx;
    float vy;

    int life;

    uint16_t colour;

    bool active;
};

constexpr int MAX_PARTICLES = 42;
Particle particles[MAX_PARTICLES];

// -------------------- Stars ---------------------------------

struct Star
{
    int x;
    int y;

    uint16_t colour;
};

constexpr int STAR_COUNT = 38;
Star stars[STAR_COUNT];

// ============================================================
// Helpers
// ============================================================

float degToRad(float degrees)
{
    return degrees * 0.01745329252f;
}

float wrapFloat(float value, float maximum)
{
    while (value < 0)
        value += maximum;

    while (value >= maximum)
        value -= maximum;

    return value;
}

bool buttonPressed()
{
    return digitalRead(JOY_SW) == LOW;
}

float joystickAxis(int raw, int centre)
{
    constexpr int deadZone = 180;

    int delta = raw - centre;

    if (abs(delta) < deadZone)
        return 0.0f;

    float value = 0.0f;

    if (delta > 0)
    {
        int range = 4095 - centre - deadZone;

        if (range > 0)
            value =
                (float)(delta - deadZone) /
                (float)range;
    }
    else
    {
        int range = centre - deadZone;

        if (range > 0)
            value =
                (float)(delta + deadZone) /
                (float)range;
    }

    return constrain(value, -1.0f, 1.0f);
}

float distanceSquared(
    float x1,
    float y1,
    float x2,
    float y2)
{
    float dx = x1 - x2;
    float dy = y1 - y2;

    // Handle screen wrapping for collision distance.
    if (fabsf(dx) > SCREEN_W / 2)
        dx =
            dx > 0
                ? dx - SCREEN_W
                : dx + SCREEN_W;

    if (fabsf(dy) > SCREEN_H / 2)
        dy =
            dy > 0
                ? dy - SCREEN_H
                : dy + SCREEN_H;

    return dx * dx + dy * dy;
}

int asteroidRadius(int size)
{
    if (size == AST_LARGE)
        return 17;

    if (size == AST_MEDIUM)
        return 11;

    return 6;
}

// ============================================================
// Canvas helpers
// ============================================================

void clearFrame()
{
    canvas->fillScreen(C_BLACK);
}

void presentFrame()
{
    canvas->flush();
}

void centredText(
    const char *text,
    int y,
    int size,
    uint16_t colour)
{
    canvas->setTextSize(size);
    canvas->setTextColor(colour);

    int16_t x1;
    int16_t y1;
    uint16_t w;
    uint16_t h;

    canvas->getTextBounds(
        text,
        0,
        0,
        &x1,
        &y1,
        &w,
        &h
    );

    canvas->setCursor(
        (SCREEN_W - w) / 2,
        y
    );

    canvas->print(text);
}

// ============================================================
// Joystick calibration
// ============================================================

void calibrateJoystick()
{
    clearFrame();

    centredText(
        "FORGEUI",
        70,
        3,
        C_CYAN
    );

    centredText(
        "CALIBRATING",
        112,
        2,
        C_WHITE
    );

    centredText(
        "RELEASE STICK",
        140,
        1,
        C_GREEN
    );

    presentFrame();

    long totalX = 0;
    long totalY = 0;

    constexpr int samples = 64;

    for (int i = 0; i < samples; i++)
    {
        totalX += analogRead(JOY_X);
        totalY += analogRead(JOY_Y);

        delay(5);
    }

    joyCentreX = totalX / samples;
    joyCentreY = totalY / samples;

    Serial.printf(
        "Joystick centre X=%d Y=%d\n",
        joyCentreX,
        joyCentreY
    );
}

// ============================================================
// Stars
// ============================================================

void initStars()
{
    for (int i = 0; i < STAR_COUNT; i++)
    {
        stars[i].x =
            random(2, SCREEN_W - 2);

        stars[i].y =
            random(24, SCREEN_H - 2);

        int brightness =
            random(0, 3);

        if (brightness == 0)
            stars[i].colour = C_DKGREY;
        else if (brightness == 1)
            stars[i].colour = C_GREY;
        else
            stars[i].colour = C_WHITE;
    }
}

void drawStars()
{
    for (int i = 0; i < STAR_COUNT; i++)
    {
        canvas->drawPixel(
            stars[i].x,
            stars[i].y,
            stars[i].colour
        );
    }
}

// ============================================================
// Particles
// ============================================================

void spawnParticle(
    float x,
    float y,
    float vx,
    float vy,
    int life,
    uint16_t colour)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (particles[i].active)
            continue;

        particles[i].x = x;
        particles[i].y = y;

        particles[i].vx = vx;
        particles[i].vy = vy;

        particles[i].life = life;
        particles[i].colour = colour;

        particles[i].active = true;

        return;
    }
}

void updateParticles()
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        Particle &p = particles[i];

        if (!p.active)
            continue;

        p.x += p.vx;
        p.y += p.vy;

        p.x = wrapFloat(p.x, SCREEN_W);
        p.y = wrapFloat(p.y, SCREEN_H);

        p.vx *= 0.98f;
        p.vy *= 0.98f;

        p.life--;

        if (p.life <= 0)
        {
            p.active = false;
            continue;
        }

        canvas->drawPixel(
            (int)p.x,
            (int)p.y,
            p.colour
        );

        if (p.life > 10)
        {
            canvas->drawPixel(
                ((int)p.x + 1) % SCREEN_W,
                (int)p.y,
                p.colour
            );
        }
    }
}

// ============================================================
// Ship
// ============================================================

void resetShip()
{
    ship.x = SCREEN_W / 2.0f;
    ship.y = SCREEN_H / 2.0f;

    ship.vx = 0;
    ship.vy = 0;

    ship.angle = -90.0f;

    ship.thrusting = false;
}

void drawShip()
{
    float a = degToRad(ship.angle);

    float noseX =
        ship.x + cosf(a) * 11.0f;

    float noseY =
        ship.y + sinf(a) * 11.0f;

    float leftA =
        a + 2.45f;

    float rightA =
        a - 2.45f;

    float leftX =
        ship.x + cosf(leftA) * 9.0f;

    float leftY =
        ship.y + sinf(leftA) * 9.0f;

    float rightX =
        ship.x + cosf(rightA) * 9.0f;

    float rightY =
        ship.y + sinf(rightA) * 9.0f;

    canvas->drawLine(
        (int)noseX,
        (int)noseY,
        (int)leftX,
        (int)leftY,
        C_CYAN
    );

    canvas->drawLine(
        (int)leftX,
        (int)leftY,
        (int)rightX,
        (int)rightY,
        C_WHITE
    );

    canvas->drawLine(
        (int)rightX,
        (int)rightY,
        (int)noseX,
        (int)noseY,
        C_CYAN
    );

    canvas->fillCircle(
        (int)ship.x,
        (int)ship.y,
        2,
        C_WHITE
    );

    if (ship.thrusting)
    {
        float exhaustA =
            a + PI;

        float ex =
            ship.x +
            cosf(exhaustA) * 13.0f;

        float ey =
            ship.y +
            sinf(exhaustA) * 13.0f;

        canvas->drawLine(
            (int)ship.x,
            (int)ship.y,
            (int)ex,
            (int)ey,
            C_YELLOW
        );

        if (random(0, 2))
        {
            spawnParticle(
                ex,
                ey,
                cosf(exhaustA) *
                    random(8, 18) * 0.08f,
                sinf(exhaustA) *
                    random(8, 18) * 0.08f,
                random(5, 12),
                random(0, 2)
                    ? C_YELLOW
                    : C_RED
            );
        }
    }
}

void updateShip()
{
    int rawX = analogRead(JOY_X);
    int rawY = analogRead(JOY_Y);

    float steer =
        joystickAxis(
            rawX,
            joyCentreX
        );

    float thrustInput =
        joystickAxis(
            rawY,
            joyCentreY
        );

    // Horizontal joystick rotates ship.
    ship.angle += steer * 5.0f;

    if (ship.angle >= 360.0f)
        ship.angle -= 360.0f;

    if (ship.angle < 0.0f)
        ship.angle += 360.0f;

    // Depending on physical stick orientation,
    // forward is normally negative Y.
    float thrust = -thrustInput;

    ship.thrusting =
        thrust > 0.15f;

    float a =
        degToRad(ship.angle);

    if (thrust > 0.15f)
    {
        ship.vx +=
            cosf(a) *
            thrust *
            0.11f;

        ship.vy +=
            sinf(a) *
            thrust *
            0.11f;
    }
    else if (thrust < -0.20f)
    {
        // Pulling backwards acts as brake/reverse thrust.
        ship.vx +=
            cosf(a) *
            thrust *
            0.055f;

        ship.vy +=
            sinf(a) *
            thrust *
            0.055f;
    }

    // Mild space drag keeps the game controllable.
    ship.vx *= 0.995f;
    ship.vy *= 0.995f;

    float speed =
        sqrtf(
            ship.vx * ship.vx +
            ship.vy * ship.vy
        );

    constexpr float maxSpeed = 3.2f;

    if (speed > maxSpeed)
    {
        ship.vx =
            ship.vx /
            speed *
            maxSpeed;

        ship.vy =
            ship.vy /
            speed *
            maxSpeed;
    }

    ship.x += ship.vx;
    ship.y += ship.vy;

    ship.x =
        wrapFloat(
            ship.x,
            SCREEN_W
        );

    ship.y =
        wrapFloat(
            ship.y,
            SCREEN_H
        );
}

// ============================================================
// Bullets
// ============================================================

void fireBullet()
{
    if (millis() - lastShot < 180)
        return;

    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (bullets[i].active)
            continue;

        float a =
            degToRad(ship.angle);

        bullets[i].x =
            ship.x +
            cosf(a) * 12.0f;

        bullets[i].y =
            ship.y +
            sinf(a) * 12.0f;

        bullets[i].vx =
            ship.vx +
            cosf(a) * 5.0f;

        bullets[i].vy =
            ship.vy +
            sinf(a) * 5.0f;

        bullets[i].life = 42;
        bullets[i].active = true;

        lastShot = millis();

        return;
    }
}

void updateBullets()
{
    for (int i = 0; i < MAX_BULLETS; i++)
    {
        Bullet &b = bullets[i];

        if (!b.active)
            continue;

        b.x += b.vx;
        b.y += b.vy;

        b.x =
            wrapFloat(
                b.x,
                SCREEN_W
            );

        b.y =
            wrapFloat(
                b.y,
                SCREEN_H
            );

        b.life--;

        if (b.life <= 0)
        {
            b.active = false;
            continue;
        }

        canvas->fillCircle(
            (int)b.x,
            (int)b.y,
            2,
            C_YELLOW
        );
    }
}

// ============================================================
// Asteroids
// ============================================================

void clearAsteroids()
{
    for (int i = 0; i < MAX_ASTEROIDS; i++)
        asteroids[i].active = false;
}

int findFreeAsteroid()
{
    for (int i = 0; i < MAX_ASTEROIDS; i++)
    {
        if (!asteroids[i].active)
            return i;
    }

    return -1;
}

void spawnAsteroid(
    float x,
    float y,
    int size,
    float baseSpeed = 1.0f)
{
    int index = findFreeAsteroid();

    if (index < 0)
        return;

    Asteroid &a =
        asteroids[index];

    a.x = x;
    a.y = y;

    float direction =
        random(0, 628) / 100.0f;

    float velocity =
        baseSpeed *
        random(70, 130) /
        100.0f;

    a.vx =
        cosf(direction) *
        velocity;

    a.vy =
        sinf(direction) *
        velocity;

    a.rotation =
        random(0, 360);

    a.rotationSpeed =
        random(-20, 21) /
        10.0f;

    a.size = size;
    a.active = true;
    a.shape = random(0, 4

            );
}

void spawnWave()
{
    clearAsteroids();

    int count =
        constrain(
            3 + wave,
            4,
            8
        );

    for (int i = 0; i < count; i++)
    {
        float x;
        float y;

        // Spawn around screen edges and away from player.
        int edge = random(0, 4);

        if (edge == 0)
        {
            x = random(0, SCREEN_W);
            y = 4;
        }
        else if (edge == 1)
        {
            x = SCREEN_W - 4;
            y = random(24, SCREEN_H);
        }
        else if (edge == 2)
        {
            x = random(0, SCREEN_W);
            y = SCREEN_H - 4;
        }
        else
        {
            x = 4;
            y = random(24, SCREEN_H);
        }

        spawnAsteroid(
            x,
            y,
            AST_LARGE,
            0.65f + wave * 0.07f
        );
    }
}

void drawAsteroid(
    const Asteroid &a)
{
    int radius =
        asteroidRadius(a.size);

    constexpr int POINTS = 8;

    int px[POINTS];
    int py[POINTS];

    for (int i = 0; i < POINTS; i++)
    {
        float angle =
            degToRad(
                a.rotation +
                i * (360.0f / POINTS)
            );

        // Deterministic rough outline.
        float roughness =
            0.78f +
            (((i + a.shape * 3) % 4) * 0.09f);

        float r =
            radius * roughness;

        px[i] =
            (int)(
                a.x +
                cosf(angle) * r
            );

        py[i] =
            (int)(
                a.y +
                sinf(angle) * r
            );
    }

    uint16_t colour;

    if (a.size == AST_LARGE)
        colour = C_WHITE;
    else if (a.size == AST_MEDIUM)
        colour = C_CYAN;
    else
        colour = C_GREY;

    for (int i = 0; i < POINTS; i++)
    {
        int next =
            (i + 1) % POINTS;

        canvas->drawLine(
            px[i],
            py[i],
            px[next],
            py[next],
            colour
        );
    }

    // A few internal rock details.
    canvas->drawLine(
        (int)a.x - radius / 3,
        (int)a.y - radius / 4,
        (int)a.x + radius / 4,
        (int)a.y + radius / 5,
        C_DKGREY
    );
}

void updateAsteroids()
{
    for (int i = 0; i < MAX_ASTEROIDS; i++)
    {
        Asteroid &a =
            asteroids[i];

        if (!a.active)
            continue;

        a.x += a.vx;
        a.y += a.vy;

        a.x =
            wrapFloat(
                a.x,
                SCREEN_W
            );

        a.y =
            wrapFloat(
                a.y,
                SCREEN_H
            );

        a.rotation +=
            a.rotationSpeed;

        drawAsteroid(a);
    }
}

int activeAsteroidCount()
{
    int count = 0;

    for (int i = 0; i < MAX_ASTEROIDS; i++)
    {
        if (asteroids[i].active)
            count++;
    }

    return count;
}

// ============================================================
// Asteroid destruction / splitting
// ============================================================

void asteroidExplosion(
    float x,
    float y,
    int size)
{
    int particlesToCreate =
        size == AST_LARGE
            ? 14
            : size == AST_MEDIUM
                ? 10
                : 7;

    for (int i = 0; i < particlesToCreate; i++)
    {
        float angle =
            random(0, 628) /
            100.0f;

        float speed =
            random(5, 24) /
            10.0f;

        spawnParticle(
            x,
            y,
            cosf(angle) * speed,
            sinf(angle) * speed,
            random(10, 26),
            random(0, 3) == 0
                ? C_YELLOW
                : C_WHITE
        );
    }
}

void destroyAsteroid(
    int asteroidIndex)
{
    Asteroid &a =
        asteroids[asteroidIndex];

    if (!a.active)
        return;

    float x = a.x;
    float y = a.y;

    float oldVx = a.vx;
    float oldVy = a.vy;

    int oldSize = a.size;

    a.active = false;

    asteroidExplosion(
        x,
        y,
        oldSize
    );

    if (oldSize == AST_LARGE)
    {
        score += 20;

        spawnAsteroid(
            x,
            y,
            AST_MEDIUM,
            1.25f
        );

        spawnAsteroid(
            x + 2,
            y + 2,
            AST_MEDIUM,
            1.25f
        );
    }
    else if (oldSize == AST_MEDIUM)
    {
        score += 50;

        spawnAsteroid(
            x,
            y,
            AST_SMALL,
            1.65f
        );

        spawnAsteroid(
            x + 2,
            y - 2,
            AST_SMALL,
            1.65f
        );
    }
    else
    {
        score += 100;
    }

    // Add some inherited motion to newly spawned fragments.
    for (int i = 0; i < MAX_ASTEROIDS; i++)
    {
        Asteroid &fragment =
            asteroids[i];

        if (!fragment.active)
            continue;

        if (distanceSquared(
                fragment.x,
                fragment.y,
                x,
                y) < 30.0f)
        {
            fragment.vx +=
                oldVx * 0.25f;

            fragment.vy +=
                oldVy * 0.25f;
        }
    }
}

// ============================================================
// Bullet collisions
// ============================================================

void checkBulletCollisions()
{
    for (int b = 0; b < MAX_BULLETS; b++)
    {
        if (!bullets[b].active)
            continue;

        for (int a = 0; a < MAX_ASTEROIDS; a++)
        {
            if (!asteroids[a].active)
                continue;

            float radius =
                asteroidRadius(
                    asteroids[a].size
                );

            float d2 =
                distanceSquared(
                    bullets[b].x,
                    bullets[b].y,
                    asteroids[a].x,
                    asteroids[a].y
                );

            if (d2 <= radius * radius)
            {
                bullets[b].active = false;

                destroyAsteroid(a);

                break;
            }
        }
    }
}

// ============================================================
// Player collision
// ============================================================

void explodePlayer()
{
    gameState = PLAYER_EXPLODING;
    stateStart = millis();

    if (score > highScore)
        highScore = score;

    for (int i = 0; i < 30; i++)
    {
        float angle =
            random(0, 628) /
            100.0f;

        float speed =
            random(8, 32) /
            10.0f;

        uint16_t colour;

        int c = random(0, 4);

        if (c == 0)
            colour = C_RED;
        else if (c == 1)
            colour = C_YELLOW;
        else if (c == 2)
            colour = C_CYAN;
        else
            colour = C_WHITE;

        spawnParticle(
            ship.x,
            ship.y,
            cosf(angle) * speed,
            sinf(angle) * speed,
            random(15, 35),
            colour
        );
    }
}

void checkPlayerCollision()
{
    constexpr float shipRadius = 7.0f;

    for (int i = 0; i < MAX_ASTEROIDS; i++)
    {
        if (!asteroids[i].active)
            continue;

        float radius =
            asteroidRadius(
                asteroids[i].size
            );

        float totalRadius =
            radius +
            shipRadius;

        if (distanceSquared(
                ship.x,
                ship.y,
                asteroids[i].x,
                asteroids[i].y)
            <= totalRadius * totalRadius)
        {
            explodePlayer();
            return;
        }
    }
}

// ============================================================
// Radar / threat ring
// ============================================================

void drawRadar()
{
    // Subtle ring around player.
    canvas->drawCircle(
        (int)ship.x,
        (int)ship.y,
        24,
        C_DKGREY
    );

    // Highlight nearby threats.
    for (int i = 0; i < MAX_ASTEROIDS; i++)
    {
        if (!asteroids[i].active)
            continue;

        float d2 =
            distanceSquared(
                ship.x,
                ship.y,
                asteroids[i].x,
                asteroids[i].y
            );

        if (d2 > 55.0f * 55.0f)
            continue;

        float dx =
            asteroids[i].x -
            ship.x;

        float dy =
            asteroids[i].y -
            ship.y;

        if (fabsf(dx) > SCREEN_W / 2)
            dx =
                dx > 0
                    ? dx - SCREEN_W
                    : dx + SCREEN_W;

        if (fabsf(dy) > SCREEN_H / 2)
            dy =
                dy > 0
                    ? dy - SCREEN_H
                    : dy + SCREEN_H;

        float angle =
            atan2f(dy, dx);

        int rx =
            (int)(
                ship.x +
                cosf(angle) * 24.0f
            );

        int ry =
            (int)(
                ship.y +
                sinf(angle) * 24.0f
            );

        canvas->fillCircle(
            rx,
            ry,
            2,
            C_RED
        );
    }
}

// ============================================================
// HUD
// ============================================================

void drawHUD()
{
    canvas->fillRect(
        0,
        0,
        SCREEN_W,
        22,
        C_BLACK
    );

    canvas->drawFastHLine(
        0,
        21,
        SCREEN_W,
        C_DKGREY
    );

    canvas->setTextSize(1);

    canvas->setTextColor(C_CYAN);
    canvas->setCursor(4, 4);
    canvas->print("FORGEUI");

    canvas->setTextColor(C_WHITE);
    canvas->setCursor(58, 4);
    canvas->printf(
        "SCORE %05lu",
        (unsigned long)score
    );

    canvas->setTextColor(C_YELLOW);
    canvas->setCursor(146, 4);
    canvas->printf(
        "W%d",
        wave
    );

    canvas->setTextColor(C_GREEN);
    canvas->setCursor(178, 4);
    canvas->print("L");

    for (int i = 0; i < lives; i++)
    {
        int x =
            190 + i * 13;

        canvas->drawTriangle(
            x + 5, 3,
            x, 12,
            x + 10, 12,
            C_GREEN
        );
    }
}

// ============================================================
// Title screen
// ============================================================

void drawTitle()
{
    clearFrame();
    drawStars();

    canvas->drawCircle(
        SCREEN_W / 2,
        112,
        62,
        C_DKGREY
    );

    canvas->drawCircle(
        SCREEN_W / 2,
        112,
        64,
        C_BLUE
    );

    centredText(
        "FORGEUI",
        46,
        3,
        C_CYAN
    );

    centredText(
        "MICRO",
        83,
        2,
        C_WHITE
    );

    centredText(
        "ASTEROIDS",
        108,
        3,
        C_WHITE
    );

    // Decorative ship
    canvas->drawTriangle(
        120, 148,
        110, 166,
        130, 166,
        C_CYAN
    );

    canvas->fillCircle(
        120,
        158,
        2,
        C_WHITE
    );

    centredText(
        "ROTATE  THRUST  FIRE",
        188,
        1,
        C_GREY
    );

    if (((millis() / 450) % 2) == 0)
    {
        centredText(
            "PRESS STICK TO LAUNCH",
            211,
            1,
            C_GREEN
        );
    }

    presentFrame();
}

// ============================================================
// Game over
// ============================================================

void drawGameOver()
{
    clearFrame();
    drawStars();

    centredText(
        "MISSION LOST",
        62,
        3,
        C_RED
    );

    char scoreText[40];

    snprintf(
        scoreText,
        sizeof(scoreText),
        "SCORE %lu",
        (unsigned long)score
    );

    centredText(
        scoreText,
        112,
        2,
        C_WHITE
    );

    char bestText[40];

    snprintf(
        bestText,
        sizeof(bestText),
        "BEST %lu",
        (unsigned long)highScore
    );

    centredText(
        bestText,
        140,
        2,
        C_CYAN
    );

    char waveText[24];

    snprintf(
        waveText,
        sizeof(waveText),
        "WAVE %d",
        wave
    );

    centredText(
        waveText,
        168,
        1,
        C_YELLOW
    );

    if (((millis() / 450) % 2) == 0)
    {
        centredText(
            "PRESS TO RELAUNCH",
            205,
            1,
            C_GREEN
        );
    }

    presentFrame();
}

// ============================================================
// Player explosion screen
// ============================================================

void updatePlayerExplosion()
{
    clearFrame();

    drawStars();

    updateAsteroids();
    updateParticles();

    drawHUD();

    centredText(
        "SHIP LOST",
        106,
        2,
        C_RED
    );

    presentFrame();

    if (millis() - stateStart > 1200)
    {
        lives--;

        if (lives <= 0)
        {
            gameState =
                GAME_OVER;

            stateStart =
                millis();

            return;
        }

        resetShip();

        // Give the player a little breathing room.
        for (int i = 0; i < MAX_ASTEROIDS; i++)
        {
            if (!asteroids[i].active)
                continue;

            if (distanceSquared(
                    asteroids[i].x,
                    asteroids[i].y,
                    ship.x,
                    ship.y)
                < 50.0f * 50.0f)
            {
                asteroids[i].x =
                    random(0, 2)
                        ? 10
                        : SCREEN_W - 10;

                asteroids[i].y =
                    random(
                        30,
                        SCREEN_H - 10
                    );
            }
        }

        gameState = PLAYING;
    }
}

// ============================================================
// Start game
// ============================================================

void startGame()
{
    score = 0;
    lives = 3;
    wave = 1;

    resetShip();

    for (int i = 0; i < MAX_BULLETS; i++)
        bullets[i].active = false;

    for (int i = 0; i < MAX_PARTICLES; i++)
        particles[i].active = false;

    clearAsteroids();

    spawnWave();

    gameState = PLAYING;
    stateStart = millis();
}

// ============================================================
// Main gameplay
// ============================================================

void updateGame()
{
    clearFrame();

    drawStars();

    updateShip();

    // Fire while button is held.
    if (buttonPressed())
        fireBullet();

    updateBullets();
    updateAsteroids();

    checkBulletCollisions();

    updateParticles();

    drawRadar();
    drawShip();
    drawHUD();

    checkPlayerCollision();

    if (gameState != PLAYING)
        return;

    // New wave after all fragments are destroyed.
    if (activeAsteroidCount() == 0)
    {
        wave++;

        if (wave > 99)
            wave = 99;

        spawnWave();
    }

    presentFrame();
}

// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("==============================");
    Serial.println("FORGEUI MICRO ASTEROIDS");
    Serial.println("ESP32-S3 + ST7789 240x240");
    Serial.println("JOY X=6 Y=5 SW=4");
    Serial.println("==============================");

    pinMode(
        JOY_SW,
        INPUT_PULLUP
    );

    analogReadResolution(12);

    // Physically proven square-display configuration.
    if (!gfx->begin())
    {
        Serial.println(
            "DISPLAY INIT FAILED"
        );

        while (true)
            delay(1000);
    }

    // Allocate a full 240x240 RGB565 off-screen canvas.
    canvas =
        new Arduino_Canvas(
            SCREEN_W,
            SCREEN_H,
            gfx
        );

    if (canvas == nullptr ||
        !canvas->begin())
    {
        Serial.println(
            "CANVAS INIT FAILED"
        );

        while (true)
            delay(1000);
    }

    randomSeed(
        analogRead(JOY_X) ^
        analogRead(JOY_Y) ^
        micros()
    );

    initStars();

    calibrateJoystick();

    gameState = TITLE;
    stateStart = millis();

    // Don't treat a held calibration button as start.
    while (buttonPressed())
        delay(10);
}

// ============================================================
// Main loop
// ============================================================

void loop()
{
    // ~30 FPS
    if (millis() - lastFrame < 33)
        return;

    lastFrame = millis();

    static bool previousButton = false;

    bool currentButton =
        buttonPressed();

    bool buttonEdge =
        currentButton &&
       
                !previousButton;

    previousButton =
        currentButton;

    switch (gameState)
    {
        case TITLE:
        {
            drawTitle();

            if (buttonEdge)
                startGame();

            break;
        }

        case PLAYING:
        {
            updateGame();
            break;
        }

        case PLAYER_EXPLODING:
        {
            updatePlayerExplosion();
            break;
        }

        case GAME_OVER:
        {
            drawGameOver();

            if (buttonEdge)
                startGame();

            break;
        }
    
    }
}
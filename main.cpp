// ============================================================
//  Spinning Cube with Spotlight  –  OpenGL + FreeGLUT
//  Build: cl /EHsc main.cpp /link opengl32.lib glu32.lib freeglut.lib
//         (or use the provided CMakeLists.txt)
// ============================================================
#include <GL/freeglut.h>
#include <cmath>
#include <cstdio>

// ── state ────────────────────────────────────────────────────
static float spotCutoff = 30.0f;   // spotlight cone half-angle
static float spotExponent = 20.0f; // focus sharpness
static bool paused = false;

// ── spotlight position (spherical, aimed at origin) ────────
// initial cartesian (4.5, 4.0, 5.5) → spherical
static float spotRadius = 8.155f;     // distance from origin
static float spotAzimuth = 0.6857f;   // radians, around +Y (atan2(x,z))
static float spotElevation = 0.5129f; // radians, up from xz-plane
static float spotPos[3] = {4.5f, 4.0f, 5.5f};

static const float SPOT_RADIUS_STEP = 0.25f;  // world units per keypress
static const float SPOT_ANGLE_STEP = 0.05f;   // radians (~2.9°) per keypress
static const float SPOT_ELEV_LIMIT = 1.5533f; // ~89° — avoid gimbal at the pole

static void updateSpotPos()
{
    if (spotElevation > SPOT_ELEV_LIMIT)
        spotElevation = SPOT_ELEV_LIMIT;
    if (spotElevation < -SPOT_ELEV_LIMIT)
        spotElevation = -SPOT_ELEV_LIMIT;
    if (spotRadius < 1.0f)
        spotRadius = 1.0f;

    float ce = cosf(spotElevation);
    spotPos[0] = spotRadius * ce * sinf(spotAzimuth);
    spotPos[1] = spotRadius * sinf(spotElevation);
    spotPos[2] = spotRadius * ce * cosf(spotAzimuth);
}

// ── rotation matrix (column-major, accumulated) ─────────────
static GLfloat rotMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// ── mouse / momentum state ──────────────────────────────────
static const float DEFAULT_SPEED = 0.8f;  // idle spin (deg/frame)
static const float DRAG_SENS = 0.4f;      // mouse-pixel → degrees
static const float FRICTION = 0.995f;     // velocity decay per frame (gentle)
static const float RESTORE_RATE = 0.006f; // how fast idle spin blends back in
static const float THROW_SCALE = 1.0f;    // multiplier: drag speed → throw speed

static bool dragging = false;
static int lastMX = 0, lastMY = 0; // last mouse pos while dragging
static float velX = 0.0f;          // angular velocity (horizontal)
static float velY = 0.0f;          // angular velocity (vertical)
static float accumH = 0.0f;        // accumulated horizontal delta this frame
static float accumV = 0.0f;        // accumulated vertical delta this frame
static float idleBlend = 1.0f;     // 0 = full momentum, 1 = full idle spin

// camera axes in world space (extracted from the view matrix each frame)
static float camRight[3] = {1.0f, 0.0f, 0.0f};
static float camUp[3] = {0.0f, 1.0f, 0.0f};

// ── colours / materials ──────────────────────────────────────
static const GLfloat faceDiffuse[6][4] = {
    {0.50f, 0.12f, 0.12f, 1.0f}, // +Z  red
    {0.12f, 0.33f, 0.54f, 1.0f}, // -Z  blue
    {0.15f, 0.48f, 0.18f, 1.0f}, // +Y  green
    {0.57f, 0.42f, 0.06f, 1.0f}, // -Y  yellow
    {0.54f, 0.27f, 0.06f, 1.0f}, // +X  orange
    {0.51f, 0.15f, 0.48f, 1.0f}, // -X  purple
};
static const GLfloat faceSpecular[6][4] = {
    {1.00f, 0.50f, 0.50f, 1.0f}, // +Z  red
    {0.50f, 0.75f, 1.00f, 1.0f}, // -Z  blue
    {0.50f, 1.00f, 0.55f, 1.0f}, // +Y  green
    {1.00f, 0.90f, 0.40f, 1.0f}, // -Y  yellow
    {1.00f, 0.70f, 0.40f, 1.0f}, // +X  orange
    {1.00f, 0.50f, 1.00f, 1.0f}, // -X  purple
};
static const GLfloat cubeShininess[] = {128.0f};

// floor
static const GLfloat floorAmbient[] = {0.10f, 0.10f, 0.10f, 1.0f};
static const GLfloat floorDiffuse[] = {0.30f, 0.30f, 0.35f, 1.0f};
static const GLfloat floorSpecular[] = {0.20f, 0.20f, 0.20f, 1.0f};
static const GLfloat floorShine[] = {10.0f};

static bool showDebug = false;

// ── debug: draw spotlight cone + direction ───────────────────
static void drawSpotlightDebug()
{
    if (!showDebug)
        return;

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST); // draw on top of everything

    // spotlight parameters
    float px = spotPos[0], py = spotPos[1], pz = spotPos[2];    // position
    float dx = -spotPos[0], dy = -spotPos[1], dz = -spotPos[2]; // aim at origin

    // normalize direction
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    dx /= len;
    dy /= len;
    dz /= len;

    // how far the debug cone extends
    float coneLen = 8.0f;

    // ── center line (yellow) ────────────────────────────────
    glColor3f(1.0f, 1.0f, 0.3f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex3f(px, py, pz);
    glVertex3f(px + dx * coneLen, py + dy * coneLen, pz + dz * coneLen);
    glEnd();

    // ── build a local coordinate frame around the direction ─
    float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
    if (fabsf(dy) > 0.9f)
    {
        upX = 1.0f;
        upY = 0.0f;
        upZ = 0.0f;
    }

    // right = dir × up
    float rx = dy * upZ - dz * upY;
    float ry = dz * upX - dx * upZ;
    float rz = dx * upY - dy * upX;
    float rlen = sqrtf(rx * rx + ry * ry + rz * rz);
    rx /= rlen;
    ry /= rlen;
    rz /= rlen;

    // actual up = right × dir
    float ux = ry * dz - rz * dy;
    float uy = rz * dx - rx * dz;
    float uz = rx * dy - ry * dx;

    // cone radius at the far end
    float halfAngleRad = spotCutoff * 3.14159265f / 180.0f;
    float radius = coneLen * tanf(halfAngleRad);

    // center of the cone's base circle
    float cx = px + dx * coneLen;
    float cy = py + dy * coneLen;
    float cz = pz + dz * coneLen;

    int segments = 24;

    // ── cone ribs: lines from light source to circle edge ───
    glColor3f(1.0f, 0.6f, 0.1f);
    glLineWidth(1.0f);
    for (int i = 0; i < segments; ++i)
    {
        float angle = 2.0f * 3.14159265f * i / segments;
        float cosA = cosf(angle);
        float sinA = sinf(angle);

        float ex = cx + radius * (cosA * rx + sinA * ux);
        float ey = cy + radius * (cosA * ry + sinA * uy);
        float ez = cz + radius * (cosA * rz + sinA * uz);

        // draw every 4th rib to keep it clean
        if (i % 4 == 0)
        {
            glBegin(GL_LINES);
            glVertex3f(px, py, pz);
            glVertex3f(ex, ey, ez);
            glEnd();
        }
    }

    // ── cone base circle ────────────────────────────────────
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; ++i)
    {
        float angle = 2.0f * 3.14159265f * i / segments;
        float cosA = cosf(angle);
        float sinA = sinf(angle);

        float ex = cx + radius * (cosA * rx + sinA * ux);
        float ey = cy + radius * (cosA * ry + sinA * uy);
        float ez = cz + radius * (cosA * rz + sinA * uz);

        glVertex3f(ex, ey, ez);
    }
    glEnd();

    // ── target crosshair at origin (red) ────────────────────
    glColor3f(1.0f, 0.2f, 0.2f);
    glLineWidth(1.5f);
    float cs = 0.4f;
    glBegin(GL_LINES);
    glVertex3f(-cs, 0, 0);
    glVertex3f(cs, 0, 0);
    glVertex3f(0, -cs, 0);
    glVertex3f(0, cs, 0);
    glVertex3f(0, 0, -cs);
    glVertex3f(0, 0, cs);
    glEnd();

    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// ── draw helpers ─────────────────────────────────────────────
static void
drawCube()
{
    // each face: normal then 4 vertices (CCW)
    static const GLfloat n[6][3] = {{0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}};
    static const GLint faces[6][4] = {{0, 1, 2, 3}, {7, 6, 5, 4}, {3, 2, 6, 7}, {4, 5, 1, 0}, {5, 6, 2, 1}, {0, 3, 7, 4}};
    static const GLfloat v[8][3] = {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}};

    glMaterialfv(GL_FRONT, GL_SHININESS, cubeShininess);

    glPushMatrix();
    glMultMatrixf(rotMatrix);

    for (int i = 0; i < 6; ++i)
    {
        GLfloat ambient[4] = {faceDiffuse[i][0] * 0.05f, faceDiffuse[i][1] * 0.05f,
                              faceDiffuse[i][2] * 0.05f, 1.0f};
        glMaterialfv(GL_FRONT, GL_AMBIENT, ambient);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, faceDiffuse[i]);
        glMaterialfv(GL_FRONT, GL_SPECULAR, faceSpecular[i]);
        glColor3fv(faceDiffuse[i]);
        glBegin(GL_QUADS);
        glNormal3fv(n[i]);
        for (int j = 0; j < 4; ++j)
            glVertex3fv(v[faces[i][j]]);
        glEnd();
    }

    glPopMatrix();
}

static void
drawWalls()
{
    glMaterialfv(GL_FRONT, GL_AMBIENT, floorAmbient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, floorDiffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, floorSpecular);
    glMaterialfv(GL_FRONT, GL_SHININESS, floorShine);
    glColor3fv(faceDiffuse[0]);

    glBegin(GL_QUADS);
    {
        // back wall (z = -6), normal facing camera
        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertex3f(-6.0f, -2.0f, -6.0f);
        glVertex3f(6.0f, -2.0f, -6.0f);
        glVertex3f(6.0f, 6.0f, -6.0f);
        glVertex3f(-6.0f, 6.0f, -6.0f);

        // left wall (x = -6), normal facing right
        glNormal3f(1.0f, 0.0f, 0.0f);
        glVertex3f(-6.0f, -2.0f, 6.0f);
        glVertex3f(-6.0f, -2.0f, -6.0f);
        glVertex3f(-6.0f, 6.0f, -6.0f);
        glVertex3f(-6.0f, 6.0f, 6.0f);

        // right wall (x = 6), normal facing left
        glNormal3f(-1.0f, 0.0f, 0.0f);
        glVertex3f(6.0f, -2.0f, -6.0f);
        glVertex3f(6.0f, -2.0f, 6.0f);
        glVertex3f(6.0f, 6.0f, 6.0f);
        glVertex3f(6.0f, 6.0f, -6.0f);
    }
    glEnd();
}

static void
drawFloor()
{
    glMaterialfv(GL_FRONT, GL_AMBIENT, floorAmbient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, floorDiffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, floorSpecular);
    glMaterialfv(GL_FRONT, GL_SHININESS, floorShine);

    glBegin(GL_QUADS);
    glNormal3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-6.0f, -2.0f, -6.0f);
    glVertex3f(-6.0f, -2.0f, 6.0f);
    glVertex3f(6.0f, -2.0f, 6.0f);
    glVertex3f(6.0f, -2.0f, -6.0f);
    glEnd();
}

// small yellow sphere marks where the spotlight is
static void
drawLightMarker()
{
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 0.3f);
    glPushMatrix();
    glTranslatef(spotPos[0], spotPos[1], spotPos[2]);
    glutSolidSphere(0.15, 16, 16);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

// ── rotation helpers ─────────────────────────────────────────
// Multiply rotMatrix (in-place) by an incremental rotation of
// `degrees` around axis (ax,ay,az).
static void
applyRotation(float degrees, float ax, float ay, float az)
{
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glRotatef(degrees, ax, ay, az); // incremental rotation first
    glMultMatrixf(rotMatrix);       // then existing orientation
    glGetFloatv(GL_MODELVIEW_MATRIX, rotMatrix);
    glPopMatrix();
}

// ── spotlight setup ──────────────────────────────────────────
static void
setupSpotlight()
{
    // position (w=1 → positional light)
    GLfloat pos[] = {spotPos[0], spotPos[1], spotPos[2], 1.0f};
    // direction: point toward the origin
    GLfloat dir[] = {-spotPos[0], -spotPos[1], -spotPos[2]};

    GLfloat ambient[] = {0.05f, 0.05f, 0.05f, 1.0f};
    GLfloat diffuse[] = {1.0f, 0.95f, 0.8f, 1.0f};
    GLfloat specular[] = {1.0f, 1.0f, 1.0f, 1.0f};

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, dir);
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, spotCutoff);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, spotExponent);

    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    // attenuation (gentle fall-off)
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 0.5f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.0f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.0f);
}

// ── dim fill light so the scene isn't pitch black outside the cone ──
static void
setupFillLight()
{
    GLfloat pos[] = {-4.0f, 3.0f, 5.0f, 1.0f};
    GLfloat ambient[] = {0.08f, 0.08f, 0.10f, 1.0f};
    GLfloat diffuse[] = {0.45f, 0.45f, 0.50f, 1.0f};
    GLfloat spec[] = {0.0f, 0.0f, 0.0f, 1.0f};

    glLightfv(GL_LIGHT1, GL_POSITION, pos);
    glLightfv(GL_LIGHT1, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT1, GL_SPECULAR, spec);
    glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, 180.0f); // omnidirectional
}

static void
setupBounceLight()
{
    GLfloat pos[] = {0.0f, -1.8f, 0.0f, 1.0f}; // just above floor
    GLfloat ambient[] = {0.05f, 0.05f, 0.05f, 1.0f};
    GLfloat diffuse[] = {0.18f, 0.16f, 0.14f, 1.0f}; // warm, dim upward bounce
    GLfloat spec[] = {0.0f, 0.0f, 0.0f, 1.0f};

    glLightfv(GL_LIGHT2, GL_POSITION, pos);
    glLightfv(GL_LIGHT2, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT2, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT2, GL_SPECULAR, spec);
    glLightf(GL_LIGHT2, GL_SPOT_CUTOFF, 180.0f);
}

// ── GLUT callbacks ───────────────────────────────────────────
static void
display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    // camera
    gluLookAt(5.0,
              3.5,
              7.0, // eye
              0.0,
              0.0,
              0.0, // centre
              0.0,
              1.0,
              0.0); // up

    // extract camera axes from the view matrix (column-major layout)
    // row 0 = right,  row 1 = up
    GLfloat mv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    camRight[0] = mv[0];
    camRight[1] = mv[4];
    camRight[2] = mv[8];
    camUp[0] = mv[1];
    camUp[1] = mv[5];
    camUp[2] = mv[9];

    // lights (set after camera so they're in world space)
    setupSpotlight();
    setupFillLight();
    setupBounceLight();

    // room
    drawFloor();
    drawWalls();

    // light marker
    drawLightMarker();

    drawCube();
    drawSpotlightDebug();

    glutSwapBuffers();
}

static void
reshape(int w, int h)
{
    if (h == 0)
        h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(50.0, (double)w / h, 0.5, 50.0);
    glMatrixMode(GL_MODELVIEW);
}

static void
timer(int /*unused*/)
{
    if (!paused)
    {
        if (dragging)
        {
            // ── snapshot accumulated mouse deltas as velocity ────
            velX = accumH * THROW_SCALE;
            velY = accumV * THROW_SCALE;
            accumH = 0.0f;
            accumV = 0.0f;
        }
        else
        {
            // ── momentum contribution (decaying throw) ──────────
            float momentumWeight = 1.0f - idleBlend;
            if (momentumWeight > 0.0f)
            {
                applyRotation(velX * momentumWeight, camUp[0], camUp[1], camUp[2]);
                applyRotation(velY * momentumWeight, camRight[0], camRight[1], camRight[2]);

                velX *= FRICTION;
                velY *= FRICTION;
            }

            // ── idle contribution (default tumble, fading in) ───
            if (idleBlend > 0.0f)
            {
                applyRotation(DEFAULT_SPEED * idleBlend, 0.3f, 1.0f, 0.2f);
            }

            // ── gradually restore idle blend ────────────────────
            if (idleBlend < 1.0f)
            {
                idleBlend += RESTORE_RATE;
                if (idleBlend > 1.0f)
                    idleBlend = 1.0f;
            }
        }
    }
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0); // ~60 FPS
}

static void
keyboard(unsigned char key, int /*x*/, int /*y*/)
{
    switch (key)
    {
    case 27: // ESC
        glutLeaveMainLoop();
        break;

    case ' ':
        paused = !paused;
        break;

    case '+':
    case '=':
        spotCutoff = fmin(spotCutoff + 2.0f, 90.0f);
        break;

    case '-':
    case '_':
        spotCutoff = fmax(spotCutoff - 2.0f, 5.0f);
        break;

    case 'l':
    case 'L':
        spotExponent = fmin(spotExponent + 5.0f, 128.0f);
        break;

    case 'k':
    case 'K':
        spotExponent = fmax(spotExponent - 5.0f, 0.0f);
        break;

    // ── spotlight movement (spherical, aimed at origin) ─
    // W/S = closer/further, A/D = left/right (azimuth), R/E = up/down (elevation)
    case 'w':
    case 'W':
        spotRadius -= SPOT_RADIUS_STEP;
        updateSpotPos();
        break;
    case 's':
    case 'S':
        spotRadius += SPOT_RADIUS_STEP;
        updateSpotPos();
        break;
    case 'a':
    case 'A':
        spotAzimuth -= SPOT_ANGLE_STEP;
        updateSpotPos();
        break;
    case 'd':
    case 'D':
        spotAzimuth += SPOT_ANGLE_STEP;
        updateSpotPos();
        break;
    case 'r':
    case 'R':
        spotElevation += SPOT_ANGLE_STEP;
        updateSpotPos();
        break;
    case 'e':
    case 'E':
        spotElevation -= SPOT_ANGLE_STEP;
        updateSpotPos();
        break;
    case 'g':
    case 'G':
        showDebug = !showDebug;
        break;
    case 'p':
    case 'P':
    {
        const float RAD2DEG = 180.0f / 3.14159265f;
        float dx = -spotPos[0], dy = -spotPos[1], dz = -spotPos[2];
        float dlen = sqrtf(dx * dx + dy * dy + dz * dz);
        printf("\n── spotlight debug ─────────────────────────────\n");
        printf("  spherical : r=%.4f  az=%.4f rad (%.2f deg)  el=%.4f rad (%.2f deg)\n",
               spotRadius, spotAzimuth, spotAzimuth * RAD2DEG,
               spotElevation, spotElevation * RAD2DEG);
        printf("  position  : (%.4f, %.4f, %.4f)\n", spotPos[0], spotPos[1], spotPos[2]);
        printf("  direction : (%.4f, %.4f, %.4f)  |d|=%.4f\n", dx, dy, dz, dlen);
        printf("  dir norm  : (%.4f, %.4f, %.4f)\n", dx / dlen, dy / dlen, dz / dlen);
        printf("  cutoff=%.2f deg  exponent=%.2f\n", spotCutoff, spotExponent);
        printf("  debug-basis flip threshold |dy|>0.9 : |dy/|d||=%.4f -> %s\n",
               fabsf(dy / dlen), (fabsf(dy / dlen) > 0.9f) ? "ALT UP (x-axis)" : "normal up (y-axis)");
        fflush(stdout);
        break;
    }
    }
}

// ── mouse callbacks ──────────────────────────────────────────
static void
mouseButton(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
    {
        if (state == GLUT_DOWN)
        {
            dragging = true;
            lastMX = x;
            lastMY = y;
            velX = 0.0f;
            velY = 0.0f;
            accumH = 0.0f;
            accumV = 0.0f;
        }
        else
        {
            // GLUT_UP — release: kick into momentum mode.
            // idleBlend starts at 0 (full throw) and climbs back to 1.
            dragging = false;
            idleBlend = 0.0f;
        }
    }
}

static void
mouseMotion(int x, int y)
{
    if (!dragging)
        return;

    int dx = x - lastMX;
    int dy = y - lastMY;
    lastMX = x;
    lastMY = y;

    // horizontal drag → rotate around camera's up axis (screen vertical)
    // vertical   drag → rotate around camera's right axis (screen horizontal)
    float rotH = dx * DRAG_SENS;
    float rotV = dy * DRAG_SENS;

    applyRotation(rotH, camUp[0], camUp[1], camUp[2]);
    applyRotation(rotV, camRight[0], camRight[1], camRight[2]);

    // accumulate this frame's total delta (snapshotted by timer)
    accumH += rotH;
    accumV += rotV;
}

// ── init ─────────────────────────────────────────────────────
static void
initGL()
{
    glClearColor(0.08f, 0.06f, 0.05f, 1.0f); // dark warm charcoal
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_LIGHT2);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);

    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
    GLfloat globalAmbient[] = {0.10f, 0.10f, 0.10f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);
}

// ── main ─────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(960, 720);
    glutCreateWindow("Spinning Cube with Spotlight");

    initGL();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouseButton);
    glutMotionFunc(mouseMotion);
    glutTimerFunc(0, timer, 0);

    glutMainLoop();
    return 0;
};

// ============================================================
//  Spinning Cube with Spotlight  –  OpenGL + FreeGLUT
//  Build: cl /EHsc main.cpp /link opengl32.lib glu32.lib freeglut.lib
//         (or use the provided CMakeLists.txt)
// ============================================================
#include <GL/freeglut.h>
#include <cmath>

// ── state ────────────────────────────────────────────────────
static float spotCutoff = 30.0f;  // spotlight cone half-angle
static float spotExponent = 20.0f;  // focus sharpness
static bool  paused = false;

// ── rotation matrix (column-major, accumulated) ─────────────
static GLfloat rotMatrix[16] = {
    1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1
};

// ── mouse / momentum state ──────────────────────────────────
static const float DEFAULT_SPEED = 0.8f;   // idle spin (deg/frame)
static const float DRAG_SENS = 0.4f;   // mouse-pixel → degrees
static const float FRICTION = 0.995f; // velocity decay per frame (gentle)
static const float RESTORE_RATE = 0.006f; // how fast idle spin blends back in
static const float THROW_SCALE = 1.0f;   // multiplier: drag speed → throw speed

static bool  dragging = false;
static int   lastMX = 0, lastMY = 0;     // last mouse pos while dragging
static float velX = 0.0f;              // angular velocity (horizontal)
static float velY = 0.0f;              // angular velocity (vertical)
static float accumH = 0.0f;              // accumulated horizontal delta this frame
static float accumV = 0.0f;              // accumulated vertical delta this frame
static float idleBlend = 1.0f;              // 0 = full momentum, 1 = full idle spin

// camera axes in world space (extracted from the view matrix each frame)
static float camRight[3] = { 1.0f, 0.0f, 0.0f };
static float camUp[3] = { 0.0f, 1.0f, 0.0f };

// ── colours / materials ──────────────────────────────────────
static const GLfloat cubeAmbient[] = { 0.15f, 0.05f, 0.20f, 1.0f };
static const GLfloat cubeDiffuse[] = { 0.55f, 0.25f, 0.70f, 1.0f };
static const GLfloat cubeSpecular[] = { 1.00f, 1.00f, 1.00f, 1.0f };
static const GLfloat cubeShininess[] = { 80.0f };

// floor
static const GLfloat floorAmbient[] = { 0.10f, 0.10f, 0.10f, 1.0f };
static const GLfloat floorDiffuse[] = { 0.30f, 0.30f, 0.35f, 1.0f };
static const GLfloat floorSpecular[] = { 0.20f, 0.20f, 0.20f, 1.0f };
static const GLfloat floorShine[] = { 10.0f };

// ── rotation helpers ─────────────────────────────────────────
// Multiply rotMatrix (in-place) by an incremental rotation of
// `degrees` around axis (ax,ay,az).  Uses glRotatef on a temp
// matrix and reads it back — simple & correct.
static void applyRotation(float degrees, float ax, float ay, float az)
{
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glRotatef(degrees, ax, ay, az);   // incremental rotation first
    glMultMatrixf(rotMatrix);          // then existing orientation
    glGetFloatv(GL_MODELVIEW_MATRIX, rotMatrix);
    glPopMatrix();
}

// ── draw helpers ─────────────────────────────────────────────
static void drawCube()
{
    // each face: normal then 4 vertices (CCW)
    static const GLfloat n[6][3] = {
        { 0, 0, 1}, { 0, 0,-1}, { 0, 1, 0},
        { 0,-1, 0}, { 1, 0, 0}, {-1, 0, 0}
    };
    static const GLint faces[6][4] = {
        {0,1,2,3}, {4,5,6,7}, {3,2,6,7},
        {0,1,5,4}, {1,2,6,5}, {0,3,7,4}
    };
    static const GLfloat v[8][3] = {
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1}
    };

    glBegin(GL_QUADS);
    for (int i = 0; i < 6; ++i) {
        glNormal3fv(n[i]);
        for (int j = 0; j < 4; ++j)
            glVertex3fv(v[faces[i][j]]);
    }
    glEnd();
}

static void drawFloor()
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
static void drawLightMarker()
{
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 0.3f);
    glPushMatrix();
    glTranslatef(3.0f, 4.0f, 2.0f);
    glutSolidSphere(0.15, 16, 16);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

// ── spotlight setup ──────────────────────────────────────────
static void setupSpotlight()
{
    // position (w=1 → positional light)
    GLfloat pos[] = { 3.0f, 4.0f, 2.0f, 1.0f };
    // direction: point toward the origin
    GLfloat dir[] = { -3.0f, -4.0f, -2.0f };

    GLfloat ambient[] = { 0.05f, 0.05f, 0.05f, 1.0f };
    GLfloat diffuse[] = { 1.0f,  0.95f, 0.8f,  1.0f };
    GLfloat specular[] = { 1.0f,  1.0f,  1.0f,  1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, dir);
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, spotCutoff);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, spotExponent);

    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    // attenuation (gentle fall-off)
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.05f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.01f);
}

// ── dim fill light so the scene isn't pitch black outside the cone ──
static void setupFillLight()
{
    GLfloat pos[] = { -4.0f, 3.0f, 5.0f, 1.0f };
    GLfloat ambient[] = { 0.08f, 0.08f, 0.10f, 1.0f };
    GLfloat diffuse[] = { 0.12f, 0.12f, 0.15f, 1.0f };
    GLfloat spec[] = { 0.0f,  0.0f,  0.0f,  1.0f };

    glLightfv(GL_LIGHT1, GL_POSITION, pos);
    glLightfv(GL_LIGHT1, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT1, GL_SPECULAR, spec);
    glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, 180.0f); // omnidirectional
}

// ── GLUT callbacks ───────────────────────────────────────────
static void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    // camera
    gluLookAt(5.0, 3.5, 7.0,   // eye
        0.0, 0.0, 0.0,   // centre
        0.0, 1.0, 0.0);  // up

    // extract camera axes from the view matrix (column-major layout)
    // row 0 = right,  row 1 = up
    GLfloat mv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    camRight[0] = mv[0];  camRight[1] = mv[4];  camRight[2] = mv[8];
    camUp[0] = mv[1];  camUp[1] = mv[5];  camUp[2] = mv[9];

    // lights (set after camera so they're in world space)
    setupSpotlight();
    setupFillLight();

    // floor
    drawFloor();

    // light marker
    drawLightMarker();

    // spinning cube
    glMaterialfv(GL_FRONT, GL_AMBIENT, cubeAmbient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, cubeDiffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, cubeSpecular);
    glMaterialfv(GL_FRONT, GL_SHININESS, cubeShininess);

    glPushMatrix();
    glMultMatrixf(rotMatrix);
    drawCube();
    glPopMatrix();

    glutSwapBuffers();
}

static void reshape(int w, int h)
{
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(50.0, (double)w / h, 0.5, 50.0);
    glMatrixMode(GL_MODELVIEW);
}

static void timer(int /*unused*/)
{
    if (!paused) {
        if (dragging) {
            // ── snapshot accumulated mouse deltas as velocity ────
            velX = accumH * THROW_SCALE;
            velY = accumV * THROW_SCALE;
            accumH = 0.0f;
            accumV = 0.0f;
        }
        else {
            // ── momentum contribution (decaying throw) ──────────
            float momentumWeight = 1.0f - idleBlend;
            if (momentumWeight > 0.0f) {
                applyRotation(velX * momentumWeight, camUp[0], camUp[1], camUp[2]);
                applyRotation(velY * momentumWeight, camRight[0], camRight[1], camRight[2]);

                velX *= FRICTION;
                velY *= FRICTION;
            }

            // ── idle contribution (default tumble, fading in) ───
            if (idleBlend > 0.0f) {
                applyRotation(DEFAULT_SPEED * idleBlend, 0.3f, 1.0f, 0.2f);
            }

            // ── gradually restore idle blend ────────────────────
            if (idleBlend < 1.0f) {
                idleBlend += RESTORE_RATE;
                if (idleBlend > 1.0f) idleBlend = 1.0f;
            }
        }
    }
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);   // ~60 FPS
}

static void keyboard(unsigned char key, int /*x*/, int /*y*/)
{
    switch (key) {
    case 27:  // ESC
        glutLeaveMainLoop();
        break;
    case ' ':
        paused = !paused;
        break;
    case '+': case '=':
        spotCutoff = fmin(spotCutoff + 2.0f, 90.0f);
        break;
    case '-': case '_':
        spotCutoff = fmax(spotCutoff - 2.0f, 5.0f);
        break;
    case 'f': case 'F':
        spotExponent = fmin(spotExponent + 5.0f, 128.0f);
        break;
    case 'd': case 'D':
        spotExponent = fmax(spotExponent - 5.0f, 0.0f);
        break;
    }
}

// ── mouse callbacks ──────────────────────────────────────────
static void mouseButton(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON) {
        if (state == GLUT_DOWN) {
            dragging = true;
            lastMX = x;
            lastMY = y;
            velX = 0.0f;
            velY = 0.0f;
            accumH = 0.0f;
            accumV = 0.0f;
        }
        else {
            // GLUT_UP — release: kick into momentum mode.
            // idleBlend starts at 0 (full throw) and climbs back to 1.
            dragging = false;
            idleBlend = 0.0f;
        }
    }
}

static void mouseMotion(int x, int y)
{
    if (!dragging) return;

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
static void initGL()
{
    glClearColor(0.04f, 0.04f, 0.06f, 1.0f);   // near-black
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);

    // nicer look: separate specular colour after texturing
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
}

// ── main ─────────────────────────────────────────────────────
int main(int argc, char** argv)
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
}
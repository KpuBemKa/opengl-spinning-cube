#include <GL/glut.h>
#include <cmath>

// Rotation angles
static float angleX = 20.0f;
static float angleY = 0.0f;

// Cube vertex positions
static const float vertices[8][3] = {
    {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
    {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
};

// 6 faces, each with 4 vertex indices
static const int faces[6][4] = {
    {0, 1, 2, 3}, // back
    {4, 5, 6, 7}, // front
    {0, 4, 7, 3}, // left
    {1, 5, 6, 2}, // right
    {3, 2, 6, 7}, // top
    {0, 1, 5, 4}  // bottom
};

// Vibrant face colors (RGBA)
static const float colors[6][4] = {
    {0.95f, 0.26f, 0.21f, 1.0f}, // red    — back
    {0.13f, 0.59f, 0.95f, 1.0f}, // blue   — front
    {0.30f, 0.69f, 0.31f, 1.0f}, // green  — left
    {1.00f, 0.76f, 0.03f, 1.0f}, // yellow — right
    {0.61f, 0.15f, 0.69f, 1.0f}, // purple — top
    {1.00f, 0.34f, 0.13f, 1.0f}  // orange — bottom
};

void drawCube() {
    for (int f = 0; f < 6; ++f) {
        // Filled face
        glColor4fv(colors[f]);
        glBegin(GL_QUADS);
        for (int v = 0; v < 4; ++v)
            glVertex3fv(vertices[faces[f][v]]);
        glEnd();

        // Dark outline for each edge
        glColor3f(0.08f, 0.08f, 0.08f);
        glLineWidth(1.8f);
        glBegin(GL_LINE_LOOP);
        for (int v = 0; v < 4; ++v)
            glVertex3fv(vertices[faces[f][v]]);
        glEnd();
    }
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    // Camera pull-back
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef(angleX, 1.0f, 0.0f, 0.0f);
    glRotatef(angleY, 0.0f, 1.0f, 0.0f);

    drawCube();
    glutSwapBuffers();
}

void timer(int /*value*/) {
    angleY += 0.8f;
    if (angleY >= 360.0f) angleY -= 360.0f;
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0); // ~60 fps
}

void reshape(int w, int h) {
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)w / h, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW);
}

void keyboard(unsigned char key, int /*x*/, int /*y*/) {
    if (key == 27 || key == 'q') // ESC or Q to quit
        exit(0);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(700, 700);
    glutCreateWindow("Spinning Cube — press Q or ESC to quit");

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.10f, 0.10f, 0.14f, 1.0f); // dark background

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, timer, 0);

    glutMainLoop();
    return 0;
}
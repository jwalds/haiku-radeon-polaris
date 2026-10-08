/*
 * sdlgl: SDL2 on Wayland with a desktop OpenGL (compatibility) context
 * through EGL and Zink, as ioquake3's renderers create it. Draws a spinning
 * triangle with fixed-function calls and reports the input events it gets.
 *
 * build: gcc -o sdlgl sdlgl.c `sdl2-config --cflags --libs`
 * usage: sdlgl [frames]
 */
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#define NO_SDL_GLEXT
#include <SDL_opengl.h>

// GL entry points come from the context (EGL), as in ioquake3's renderers;
// libGL is not linked.
#define GL_FUNCTIONS(X) \
	X(const GLubyte *, glGetString, (GLenum)) \
	X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
	X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, glClear, (GLbitfield)) \
	X(void, glMatrixMode, (GLenum)) \
	X(void, glLoadIdentity, (void)) \
	X(void, glRotatef, (GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, glBegin, (GLenum)) \
	X(void, glEnd, (void)) \
	X(void, glColor3f, (GLfloat, GLfloat, GLfloat)) \
	X(void, glVertex2f, (GLfloat, GLfloat))

#define DECLARE(type, name, args) static type (*name##_)args;
GL_FUNCTIONS(DECLARE)
#define glGetString glGetString_
#define glViewport glViewport_
#define glClearColor glClearColor_
#define glClear glClear_
#define glMatrixMode glMatrixMode_
#define glLoadIdentity glLoadIdentity_
#define glRotatef glRotatef_
#define glBegin glBegin_
#define glEnd glEnd_
#define glColor3f glColor3f_
#define glVertex2f glVertex2f_


int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int frames = argc > 1 ? atoi(argv[1]) : 300;

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		printf("[!] SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	printf("video driver: %s\n", SDL_GetCurrentVideoDriver());

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_Window *window = SDL_CreateWindow("SDL2 OpenGL on Zink",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (window == NULL) {
		printf("[!] SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GLContext context = SDL_GL_CreateContext(window);
	if (context == NULL) {
		printf("[!] SDL_GL_CreateContext: %s\n", SDL_GetError());
		return 1;
	}
#define LOAD(type, name, args) \
	name##_ = (type (*)args)SDL_GL_GetProcAddress(#name);
	GL_FUNCTIONS(LOAD)
	printf("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER),
		glGetString(GL_VERSION));
	SDL_GL_SetSwapInterval(0);

	Uint32 start = SDL_GetTicks();
	int events = 0, frame;
	for (frame = 0; frame < frames; frame++) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			events++;
			if (event.type == SDL_QUIT)
				frame = frames;
			else if (event.type == SDL_KEYDOWN)
				printf("key down: %s\n", SDL_GetKeyName(event.key.keysym.sym));
			else if (event.type == SDL_MOUSEBUTTONDOWN)
				printf("mouse button %d\n", event.button.button);
			else if (event.type == SDL_WINDOWEVENT)
				printf("window event %d\n", event.window.event);
		}
		glViewport(0, 0, 640, 480);
		glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glRotatef(frame * 2.0f, 0, 0, 1);
		glBegin(GL_TRIANGLES);
		glColor3f(1, 0, 0);
		glVertex2f(0.0f, 0.7f);
		glColor3f(0, 1, 0);
		glVertex2f(-0.6f, -0.5f);
		glColor3f(0, 0, 1);
		glVertex2f(0.6f, -0.5f);
		glEnd();
		SDL_GL_SwapWindow(window);
	}
	Uint32 elapsed = SDL_GetTicks() - start;
	printf("%d frames in %u ms (%.1f fps), %d events\n", frame, elapsed,
		frame * 1000.0 / (elapsed ? elapsed : 1), events);

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	// Haiku's in-process Wayland server closes the window of a destroyed
	// surface asynchronously; give it time before the connection goes away.
	SDL_Delay(300);
	SDL_Quit();
	return 0;
}

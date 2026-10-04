/*
 * glwl: OpenGL ES 2 on screen through Zink (Mesa's OpenGL on Vulkan) and
 * RADV: EGL on Wayland with a spinning triangle, as vkwl does in Vulkan.
 * Run with MESA_LOADER_DRIVER_OVERRIDE=zink (EGL then uses Zink's kopper
 * path, which presents through RADV's Wayland WSI).
 *
 * build: gcc -o glwl glwl.c xdg-shell-protocol.c -lEGL -lGLESv2
 *        -lwayland-client -lwayland-egl -lm
 * usage: glwl [frames [width height]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell-client-protocol.h"

enum { kWidth = 640, kHeight = 480 };

static struct wl_compositor *sCompositor;
static struct xdg_wm_base *sWmBase;
static int sConfigured, sClosed;


static void
WmBasePing(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener sWmBaseListener = {WmBasePing};


static void
RegistryGlobal(void *data, struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		sCompositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		sWmBase = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(sWmBase, &sWmBaseListener, NULL);
	}
}

static void
RegistryRemove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener sRegistryListener = {
	RegistryGlobal, RegistryRemove};


static void
SurfaceConfigure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	xdg_surface_ack_configure(surface, serial);
	sConfigured = 1;
}

static const struct xdg_surface_listener sSurfaceListener = {SurfaceConfigure};


static void
ToplevelConfigure(void *data, struct xdg_toplevel *toplevel, int32_t width,
	int32_t height, struct wl_array *states)
{
}

static void
ToplevelClose(void *data, struct xdg_toplevel *toplevel)
{
	sClosed = 1;
}

static const struct xdg_toplevel_listener sToplevelListener = {
	ToplevelConfigure, ToplevelClose};



static double
Now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}


static const char *kVertexShader =
	"uniform float angle;\n"
	"attribute vec2 position;\n"
	"attribute vec3 color;\n"
	"varying vec3 vColor;\n"
	"void main() {\n"
	"	float c = cos(angle), s = sin(angle);\n"
	"	gl_Position = vec4(c * position.x - s * position.y,\n"
	"		s * position.x + c * position.y, 0.0, 1.0);\n"
	"	vColor = color;\n"
	"}\n";

static const char *kFragmentShader =
	"precision mediump float;\n"
	"varying vec3 vColor;\n"
	"void main() {\n"
	"	gl_FragColor = vec4(vColor, 1.0);\n"
	"}\n";


static GLuint
CompileShader(GLenum type, const char *source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		printf("[!] shader: %s\n", log);
		exit(1);
	}
	return shader;
}


int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int frames = argc > 1 ? atoi(argv[1]) : 600;
	int width = argc > 3 ? atoi(argv[2]) : kWidth;
	int height = argc > 3 ? atoi(argv[3]) : kHeight;

	// --- Wayland window
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		printf("[!] wl_display_connect failed\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &sRegistryListener, NULL);
	wl_display_roundtrip(display);
	if (sCompositor == NULL || sWmBase == NULL) {
		printf("[!] no wl_compositor or xdg_wm_base\n");
		return 1;
	}
	struct wl_surface *surface = wl_compositor_create_surface(sCompositor);
	struct xdg_surface *xdgSurface = xdg_wm_base_get_xdg_surface(sWmBase,
		surface);
	xdg_surface_add_listener(xdgSurface, &sSurfaceListener, NULL);
	struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdgSurface);
	xdg_toplevel_add_listener(toplevel, &sToplevelListener, NULL);
	xdg_toplevel_set_title(toplevel, "OpenGL ES on Zink on Radeon RX 560");
	wl_surface_commit(surface);
	while (!sConfigured)
		wl_display_dispatch(display);
	printf("window configured\n");

	// --- EGL
	EGLDisplay eglDisplay = eglGetDisplay((EGLNativeDisplayType)display);
	EGLint major, minor;
	if (!eglInitialize(eglDisplay, &major, &minor)) {
		printf("[!] eglInitialize: %#x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d, %s\n", major, minor,
		eglQueryString(eglDisplay, EGL_VENDOR));
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint configAttributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLConfig config;
	EGLint count = 0;
	if (!eglChooseConfig(eglDisplay, configAttributes, &config, 1, &count)
		|| count == 0) {
		printf("[!] eglChooseConfig: %#x\n", eglGetError());
		return 1;
	}
	const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE};
	EGLContext context = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT,
		contextAttributes);
	if (context == EGL_NO_CONTEXT) {
		printf("[!] eglCreateContext: %#x\n", eglGetError());
		return 1;
	}
	struct wl_egl_window *eglWindow = wl_egl_window_create(surface, width,
		height);
	EGLSurface eglSurface = eglCreateWindowSurface(eglDisplay, config,
		(EGLNativeWindowType)eglWindow, NULL);
	if (eglSurface == EGL_NO_SURFACE) {
		printf("[!] eglCreateWindowSurface: %#x\n", eglGetError());
		return 1;
	}
	if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, context)) {
		printf("[!] eglMakeCurrent: %#x\n", eglGetError());
		return 1;
	}
	printf("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER),
		glGetString(GL_VERSION));

	// --- triangle
	GLuint program = glCreateProgram();
	glAttachShader(program, CompileShader(GL_VERTEX_SHADER, kVertexShader));
	glAttachShader(program, CompileShader(GL_FRAGMENT_SHADER,
		kFragmentShader));
	glBindAttribLocation(program, 0, "position");
	glBindAttribLocation(program, 1, "color");
	glLinkProgram(program);
	GLint ok;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		printf("[!] program link failed\n");
		return 1;
	}
	glUseProgram(program);
	GLint angleLocation = glGetUniformLocation(program, "angle");
	static const GLfloat positions[] = {0.0f, 0.7f, -0.6f, -0.5f, 0.6f, -0.5f};
	static const GLfloat colors[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glViewport(0, 0, width, height);

	double start = Now(), last = start;
	int frame;
	for (frame = 0; frame < frames && !sClosed; frame++) {
		glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(angleLocation, frame * 0.02f);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		if (!eglSwapBuffers(eglDisplay, eglSurface)) {
			printf("[!] frame %d: eglSwapBuffers: %#x\n", frame,
				eglGetError());
			break;
		}
		wl_display_dispatch_pending(display);
		double now = Now();
		if (now - last >= 2.0) {
			printf("frame %d, %.1f fps\n", frame + 1,
				(frame + 1) / (now - start));
			last = now;
		}
	}
	double elapsed = Now() - start;
	printf("%d frames in %.1f s (%.1f fps)%s\n", frame, elapsed,
		frame / elapsed, sClosed ? ", window closed" : "");

	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
		EGL_NO_CONTEXT);
	eglDestroySurface(eglDisplay, eglSurface);
	wl_egl_window_destroy(eglWindow);
	eglDestroyContext(eglDisplay, context);
	eglTerminate(eglDisplay);
	xdg_toplevel_destroy(toplevel);
	xdg_surface_destroy(xdgSurface);
	wl_surface_destroy(surface);
	wl_display_disconnect(display);
	return 0;
}

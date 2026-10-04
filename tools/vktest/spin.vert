#version 450
layout(push_constant) uniform Push { float angle; float aspect; } push;
layout(location = 0) out vec3 color;
const vec2 positions[3] = vec2[](vec2(0.0, -0.8), vec2(0.7, 0.4), vec2(-0.7, 0.4));
const vec3 colors[3] = vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));
void main()
{
	float c = cos(push.angle), s = sin(push.angle);
	vec2 p = mat2(c, s, -s, c) * positions[gl_VertexIndex];
	gl_Position = vec4(p.x / push.aspect, p.y, 0.0, 1.0);
	color = colors[gl_VertexIndex];
}

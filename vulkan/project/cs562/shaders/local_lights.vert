#version 460 core

layout(set = 0, binding = 0) uniform Camera {
	mat4 view;
	mat4 proj;
};

layout(push_constant) uniform LightInfo {
	mat4 model;
	vec3 color;
	float radius;
	vec4 lightCenter;
	vec3 camPos;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 a;
layout(location = 0) out vec4 outPos;

void main(){
	outPos = model * vec4(inPos, 1.f);
	gl_Position = proj * view * outPos;
}

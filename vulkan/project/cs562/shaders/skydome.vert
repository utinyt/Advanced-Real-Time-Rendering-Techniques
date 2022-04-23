#version 460 core

layout(binding = 0) uniform Camera {
	mat4 view;
	mat4 proj;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 0) out vec3 outNormal;

void main(){
	vec4 pos = proj * mat4(mat3(view)) * vec4(inPos, 1.f);
	gl_Position = pos.xyww;
	outNormal = inNormal;
}

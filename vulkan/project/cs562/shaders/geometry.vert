#version 460 core

layout(binding = 0) uniform Camera {
	mat4 view;
	mat4 proj;
};

layout(push_constant) uniform ObjectInfo {
	mat4 model;
	vec3 diffuse;
	float roughness;
	vec3 specular;
	float metallic;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 0) out vec4 outPos;
layout(location = 1) out vec3 outNormal;

void main(){
	outPos = model * vec4(inPos, 1.f);
	gl_Position = proj * view * outPos;
	outNormal = mat3(transpose(inverse(model))) * inNormal;
}

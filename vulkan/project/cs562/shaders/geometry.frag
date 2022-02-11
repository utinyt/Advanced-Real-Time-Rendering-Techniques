#version 460 core

layout(push_constant) uniform ObjectInfo {
	mat4 model;
	vec3 diffuse;
	float roughness;
	vec3 specular;
	float metallic;
};

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 0) out vec4 outPos;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDiffuse;
layout(location = 3) out vec4 outSpecular;

void main(){
	outPos = inPos;
	outNormal = vec4(inNormal, 1.f);
	outDiffuse = vec4(diffuse, roughness);
	outSpecular = vec4(specular, metallic);
}

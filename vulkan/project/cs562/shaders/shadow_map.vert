#version 460 core

layout(push_constant) uniform PS {
	mat4 model;
	mat4 lightProjView;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 0) out vec4 outPos;

void main(){
	outPos = lightProjView * model * vec4(inPos, 1.f);
	gl_Position = outPos;
}

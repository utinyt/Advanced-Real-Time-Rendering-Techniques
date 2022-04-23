#version 460 core

layout(location = 0) in vec3 inNormal;
layout(location = 0) out vec4 outPos;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDiffuse;
layout(location = 3) out vec4 outSpecular;

void main(){
	outPos = vec4(0, 0, 0, 1);
	outNormal = vec4(inNormal, -1.f);
	outDiffuse = vec4(-1, -1, -1, -1);
	outSpecular = vec4(-1, -1, -1, -1);
}

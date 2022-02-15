#version 460 core

layout(location = 0) in vec4 inPos;
layout(location = 0) out vec4 outDepth;

void main(){
	float d2 = inPos.w * inPos.w;
	float d3 = d2 * inPos.w;
	float d4 = d3 * inPos.w;

	outDepth = vec4(inPos.w, d2, d3, d4);
}
